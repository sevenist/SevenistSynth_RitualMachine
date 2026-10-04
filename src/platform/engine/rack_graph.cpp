#include "platform/engine/rack_graph.h"
#include <algorithm>
#include <cmath>
#include "engine/modules/builtin.h"
#include "engine/modules/fx_modules.h"
#include "engine/modules/fx2_modules.h"
#include "engine/modules/motion_seq.h"
#include "engine/modules/osc_engines.h"
#include "engine/modules/sampler_modules.h"
#include "engine/modules/synth_modules.h"
#include "platform/engine/dx7_convert.h"
#include "platform/engine/fm_patch_gain.h"

namespace sc {
namespace {

constexpr int kSemi = 256;

q15 q(double x) { return static_cast<q15>(std::lround(std::fmax(-1.0, std::fmin(1.0, x)) * 32767.0)); }
int32_t hz_pitch(double hz) { return static_cast<int32_t>(std::lround(69.0 * kSemi + 12.0 * kSemi * std::log2(hz / 440.0))); }
int32_t ms_i(double ms) { return static_cast<int32_t>(std::lround(ms < 1 ? 1 : ms)); }

struct B {
    GraphDesc &g;
    const Registry &reg;
    bool ok = true;
    NodeDesc *add(int id, int type) {
        NodeDesc *n = g.add_node(reg, id, type);
        if (!n) ok = false;
        return n;
    }
    void cable(int src, int sp, int dst, Dst kind, int dp, q15 depth = kUnity) { if (!g.connect(src, sp, dst, kind, dp, depth)) ok = false; }
};

int node_of(int rack_id) { return RN_MODULES + 2 * (rack_id % 100); }

int osc_wave(int w) {
    static const int map[6] = {WAVE_SINE_, WAVE_PULSE_, WAVE_SAW_DOWN_, WAVE_SAW_, WAVE_TRI_, WAVE_NOISE_};   // Sine Pulse SawDn SawUp Tri Noise
    return map[w < 0 ? 0 : (w > 5 ? 5 : w)];
}
int lfo_shape(int s) {
    static const int map[4] = {LFOS_SINE, LFOS_TRI, LFOS_SAW_DOWN, LFOS_SQUARE};
    return map[s < 0 ? 0 : (s > 3 ? 3 : s)];
}

struct Target { int node; int param; bool supported; };

// Where a modulator's cable lands and how many of the rack's depth units (st / oct / %) a full-scale signal means there. The engine modules'
// own modulation ranges are set to match when their nodes are built (OSC_PITCH_MOD 96 st, FLT_CUT_MOD 8 oct, SHP_DRIVE_MOD 4 oct).
struct DepthTarget { int dst; double range; };
bool depth_target(const rack_slot_t &t, int param, bool supported, DepthTarget &out) {
    if (!supported) return false;
    switch (t.type) {
        case MOD_OSC:
            if (t.v[MP_OC_WAVE] >= OC_FIRST_ENGINE) {              // an engine: pitch, level, timbre, morph
                static const int dst[4] = {OSCX_PITCH, OSCX_LEVEL, OSCX_TIMBRE, OSCX_MORPH};
                out = DepthTarget{dst[param & 3], param == 0 ? 96.0 : 100.0};
                return param < 4;
            }
            out = param == 0 ? DepthTarget{OSC_PITCH, 96.0} : (param == 1 ? DepthTarget{OSC_LEVEL, 100.0} : DepthTarget{OSC_PW, 100.0});
            return param < 3;
        case MOD_SAMPLER: out = DepthTarget{SMPR_TUNE, 96.0}; return param == 0;
        case MOD_FILTER:  out = DepthTarget{FLT_CUTOFF, 8.0}; return param == 0;
        case MOD_SAT:     out = DepthTarget{SHP_DRIVE, 4.0}; return param == 0;
        default:          return false;
    }
}

}  // namespace

bool rack_graph_build(const rack_t &rack, const synth_params_t &params, const Registry &reg, RackGraph &out,
                      const int16_t *slot_of_file, int n_files) {
    out.g = GraphDesc{};
    out.fm = rack.cfg.type == SYNTH_FM;
    out.ms_count = 0;
    B b{out.g, reg};
    const synth_config_t &cfg = rack.cfg;

    /* ---- voice ---- */
    b.add(RN_NOTE, T_NOTE_IN);
    const double vol = cfg.volume;
    NodeDesc *vo = b.add(RN_VOICE_OUT, T_VOICE_OUT);
    vo->param[VO_LEVEL] = q(vol / 2.0);

    if (out.fm) {
        out.dx7_node = RN_DX7;
        NodeDesc *d = b.add(RN_DX7, T_DX7);
        d->param[DX7_GAIN] = static_cast<int32_t>(std::lround(fm_patch_gain[cfg.fm_patch & 127] * 8192.0));
        out.fm_patch = dx7_convert(cfg.fm);
        vo->param[VO_TAIL_MS] = static_cast<int32_t>(out.fm_patch.release_ms + 500);
        b.cable(RN_NOTE, 0, RN_DX7, Dst::In, 0);
        b.cable(RN_NOTE, 1, RN_DX7, Dst::In, 1);
        b.cable(RN_DX7, 0, RN_VOICE_OUT, Dst::In, 0);
    } else {
        // ---- audio chain
        int n_sources = 0;
        for (int i = 0; i < rack.count; i++) if (rack.slot[i].type == MOD_OSC || rack.slot[i].type == MOD_SAMPLER) n_sources++;
        const double lvl_scale = n_sources > 1 ? 1.0 / std::sqrt(static_cast<double>(n_sources)) : 1.0;

        Target tgt[RACK_MAX];                                        // where a modulator aimed at slot i lands
        for (auto &t : tgt) t = Target{0, 0, false};
        NodeDesc *chain_gain[RACK_MAX] = {}; int chain_in[RACK_MAX] = {};                       // targeted oscillators: their gain into the chain (0 when muted)
        int run = 0;                                                 // node id of the running signal, 0 = none yet

        for (int i = 0; i < rack.count; i++) {
            if (!rack_slot_is_audio(&rack, i)) continue;
            const rack_slot_t &s = rack.slot[i];
            const int id = node_of(s.id), aux = id + 1;
            if (s.type == MOD_OSC) {
                const int wave = static_cast<int>(s.v[MP_OC_WAVE]);
                const bool eng = wave >= OC_FIRST_ENGINE;
                NodeDesc *o = b.add(id, eng ? T_OSCX : T_OSC);
                if (!o) break;
                const int32_t tune = 60 * kSemi + static_cast<int32_t>(std::lround((s.v[MP_OC_COARSE] + s.v[MP_OC_FINE] / 100.0) * kSemi));
                int lvl_idx;
                if (eng) {
                    o->param[OSCX_ENGINE] = wave - OC_FIRST_ENGINE;
                    o->param[OSCX_PITCH] = tune;
                    o->param[OSCX_TIMBRE] = q(s.v[MP_OC_PW]);
                    o->param[OSCX_MORPH] = q(s.v[MP_OC_MORPH]);
                    o->param[OSCX_PITCH_MOD] = 96 * kSemi;
                    lvl_idx = OSCX_LEVEL;
                    b.cable(RN_NOTE, 1, id, Dst::In, 1);                 // the gate strikes the string / modes
                } else {
                    o->param[OSC_WAVE] = osc_wave(wave);
                    o->param[OSC_PITCH] = tune;
                    o->param[OSC_PW] = q(s.v[MP_OC_PW]);
                    o->param[OSC_PITCH_MOD] = 96 * kSemi;                // cable depth = semitones / 96
                    lvl_idx = OSC_LEVEL;
                }
                b.cable(RN_NOTE, 0, id, Dst::In, 0);
                tgt[i] = Target{id, eng ? static_cast<int>(OSCX_PITCH) : static_cast<int>(OSC_PITCH), true};
                const bool modulating = s.tgt_id != 0 && rack_find(&rack, s.tgt_id) != RACK_NONE;
                if (!modulating) {
                    o->param[lvl_idx] = q(s.v[MP_OC_LEVEL] * lvl_scale);
                    if (run == 0) run = id;
                    else {
                        NodeDesc *m = b.add(aux, T_MIX4_V);
                        if (!m) break;
                        m->param[0] = kUnity; m->param[1] = kUnity; m->param[2] = 0; m->param[3] = 0;
                        b.cable(run, 0, aux, Dst::In, 0);
                        b.cable(id, 0, aux, Dst::In, 1);
                        run = aux;
                    }
                } else {
                    // the oscillator runs at full level so the modulation signal does not depend on Lvl or Mute;
                    // a mixer applies Lvl (and Mute) on its way into the chain
                    o->param[lvl_idx] = kUnity;
                    NodeDesc *m = b.add(aux, T_MIX4_V);
                    if (!m) break;
                    const int in = run == 0 ? 0 : 1;
                    m->param[0] = kUnity; m->param[1] = kUnity; m->param[2] = 0; m->param[3] = 0;
                    m->param[in] = q(s.v[MP_OC_LEVEL] * lvl_scale);
                    if (run != 0) b.cable(run, 0, aux, Dst::In, 0);
                    b.cable(id, 0, aux, Dst::In, in);
                    chain_gain[i] = m; chain_in[i] = in;
                    run = aux;
                }
            } else if (s.type == MOD_SAMPLER) {
                // a sample player: a source like an oscillator. No file (or a file the bank could not take) = not in the chain.
                const int f = static_cast<int>(s.v[MP_SM_FILE]) - 1;
                if (f < 0 || f >= n_files || !slot_of_file || slot_of_file[f] < 0) continue;
                NodeDesc *sp = b.add(id, T_SAMPLER);
                if (!sp) break;
                sp->param[SMPR_SAMPLE] = slot_of_file[f];
                sp->param[SMPR_INSTRUMENT] = -1;
                sp->param[SMPR_TUNE] = static_cast<int32_t>(std::lround((s.v[MP_SM_COARSE] + s.v[MP_SM_FINE] / 100.0) * kSemi));
                sp->param[SMPR_ROOT] = -1;
                sp->param[SMPR_LOOP] = static_cast<int32_t>(s.v[MP_SM_LOOP]) - 1;        // File (-1) / Off / Fwd / Ping
                sp->param[SMPR_REVERSE] = s.v[MP_SM_REV] > 0.5f ? 1 : 0;
                sp->param[SMPR_START] = static_cast<int32_t>(std::lround(s.v[MP_SM_START] * 16.0));
                sp->param[SMPR_SLICE] = static_cast<int32_t>(s.v[MP_SM_SLICE]) - 1;      // 0 = whole sample -> -1
                sp->param[SMPR_SLICE_MODE] = s.v[MP_SM_SMODE] > 0.5f ? 1 : 0;
                sp->param[SMPR_GAIN] = q(s.v[MP_SM_LEVEL] * lvl_scale);
                sp->param[SMPR_INTERP] = 1;
                sp->param[SMPR_TRACK] = s.v[MP_SM_TRACK] > 0.5f ? 0 : 1;
                b.cable(RN_NOTE, 0, id, Dst::In, 0);
                tgt[i] = Target{id, SMPR_TUNE, true};
                if (run == 0) run = id;
                else {
                    NodeDesc *m = b.add(aux, T_MIX4_V);
                    if (!m) break;
                    m->param[0] = kUnity; m->param[1] = kUnity; m->param[2] = 0; m->param[3] = 0;
                    b.cable(run, 0, aux, Dst::In, 0);
                    b.cable(id, 0, aux, Dst::In, 1);
                    run = aux;
                }
            } else if (s.type == MOD_FILTER) {
                const int type = static_cast<int>(s.v[MP_FL_TYPE]);
                if (run == 0 || type == FILT_OFF) continue;           // a filter with nothing to its left (or switched off) does nothing
                NodeDesc *f = b.add(id, T_FILTER_V);
                if (!f) break;
                static const int mode[6] = {FLTM_LP, FLTM_LP, FLTM_BP, FLTM_HP, FLTM_LP, FLTM_NOTCH};
                const int sections = type == FILT_LP24 ? 2 : 1;
                f->param[FLT_MODE] = mode[type];
                f->param[FLT_SECTIONS] = sections;
                f->param[FLT_CUTOFF] = hz_pitch(s.v[MP_FL_CUT]);
                const double q_last = sections == 2 ? 1.30656 : 0.70711;
                const double boost = (s.v[MP_FL_RES] / q_last - 1.0) / 15.0;       // Q of the last section = Butterworth Q x (1 + 15 res)
                f->param[FLT_RES] = q(boost > 0.0 ? boost : 0.0);
                f->param[FLT_CUT_MOD] = 96 * kSemi;                   // full-scale modulation = 8 octaves
                b.cable(run, 0, id, Dst::In, 0);
                tgt[i] = Target{id, FLT_CUTOFF, true};
                // the filter's own envelope (an ENV module aimed at the cutoff replaces it, see below)
                bool replaced = false;
                for (int j = 0; j < rack.count; j++)
                    if (rack.slot[j].type == MOD_ENV && rack.slot[j].tgt_id == s.id && rack.slot[j].tgt_param == 0) replaced = true;
                if (!replaced && s.v[MP_FL_ENVAMT] > 0) {
                    NodeDesc *e = b.add(aux, T_ENV);
                    if (!e) break;
                    e->param[ENV_ATTACK] = ms_i(s.v[MP_FL_A]); e->param[ENV_DECAY] = ms_i(s.v[MP_FL_D]);
                    e->param[ENV_SUSTAIN] = q(s.v[MP_FL_S]); e->param[ENV_RELEASE] = ms_i(s.v[MP_FL_R]);
                    e->param[ENV_A_CURVE] = q(s.v[MP_FL_ACV] / 100.0); e->param[ENV_D_CURVE] = q(s.v[MP_FL_DCV] / 100.0); e->param[ENV_R_CURVE] = q(s.v[MP_FL_RCV] / 100.0);
                    b.cable(RN_NOTE, 1, aux, Dst::In, 0);
                    b.cable(aux, 0, id, Dst::Param, FLT_CUTOFF, q(s.v[MP_FL_ENVAMT] / 8.0));
                }
                run = id;
            } else if (s.type == MOD_COMB) {
                if (run == 0) continue;                                  // a processor with nothing to its left does nothing
                NodeDesc *c = b.add(id, T_COMB);
                if (!c) break;
                c->param[CMB_TUNE] = 60 * kSemi + static_cast<int32_t>(std::lround(s.v[MP_RS_TUNE] * kSemi));
                c->param[CMB_FEEDBACK] = std::max<int32_t>(-31000, std::min<int32_t>(31000, q(s.v[MP_RS_FB] / 100.0)));
                c->param[CMB_DAMP] = hz_pitch(s.v[MP_RS_DAMP]);
                c->param[CMB_INTERVAL] = static_cast<int32_t>(s.v[MP_RS_INT]);
                c->param[CMB_MIX] = q(s.v[MP_RS_MIX]);
                b.cable(run, 0, id, Dst::In, 0);
                b.cable(RN_NOTE, 0, id, Dst::In, 1);                     // the pitch CV: the resonator follows the key
                run = id;
            } else if (s.type == MOD_SAT) {
                if (run == 0) continue;
                NodeDesc *sh = b.add(id, T_SHAPER_V);
                if (!sh) break;
                sh->param[SHP_MODE] = static_cast<int>(s.v[MP_SA_MODE]) % SHPM_N;        // the rack's list is the engine's order
                sh->param[SHP_DRIVE] = static_cast<int32_t>(std::lround(std::log2(s.v[MP_SA_DRIVE] < 1 ? 1 : s.v[MP_SA_DRIVE]) * kSemi));
                sh->param[SHP_MIX] = q(s.v[MP_SA_MIX]);
                sh->param[SHP_BITS] = 4;
                sh->param[SHP_DRIVE_MOD] = 4 * 256;                      // cable depth = octaves / 4
                b.cable(run, 0, id, Dst::In, 0);
                tgt[i] = Target{id, SHP_DRIVE, true};
                run = id;
            }
        }

        // ---- modulators
        for (int i = 0; i < rack.count; i++) {
            const rack_slot_t &s = rack.slot[i];
            if (!rack_slot_is_mod(&rack, i) || !s.tgt_id) continue;
            if (s.type == MOD_OSC && !chain_gain[i]) continue;       // (its target is gone)
            const int ti = rack_find(&rack, s.tgt_id);
            if (ti == RACK_NONE || ti == i) continue;
            const rack_slot_t &t = rack.slot[ti];
            const int id = node_of(s.id);
            DepthTarget dt;
            if (!depth_target(t, s.tgt_param, tgt[ti].supported, dt)) continue;   // not realised (resonance, mix, modulating a modulator, ...)
            const int dst_param = dt.dst;
            // the modulator itself
            double dpth = 1.0;
            if (s.type == MOD_LFO) {
                NodeDesc *l = b.add(id, T_LFO_V);
                if (!l) break;
                l->param[LFO_SHAPE] = lfo_shape(static_cast<int>(s.v[MP_LF_SHAPE]));
                l->param[LFO_RATE] = static_cast<int32_t>(std::lround(12.0 * kSemi * std::log2(s.v[MP_LF_RATE])));
                dpth = s.v[MP_LF_DEPTH];
            } else if (s.type == MOD_OSC) {
                dpth = s.v[MP_OC_DEPTH];                             // the node already exists: it is the chain's oscillator
                if (s.v[MP_OC_MUTE] > 0.5f) chain_gain[i]->param[chain_in[i]] = 0;
            } else if (s.type == MOD_ENV) {
                NodeDesc *e = b.add(id, T_ENV);
                if (!e) break;
                e->param[ENV_ATTACK] = ms_i(s.v[MP_EN_A]); e->param[ENV_DECAY] = ms_i(s.v[MP_EN_D]);
                e->param[ENV_SUSTAIN] = q(s.v[MP_EN_S]); e->param[ENV_RELEASE] = ms_i(s.v[MP_EN_R]);
                e->param[ENV_HOLD] = static_cast<int32_t>(s.v[MP_EN_HOLD]); e->param[ENV_START] = q(s.v[MP_EN_START]);
                e->param[ENV_A_CURVE] = q(s.v[MP_EN_ACV] / 100.0); e->param[ENV_D_CURVE] = q(s.v[MP_EN_DCV] / 100.0); e->param[ENV_R_CURVE] = q(s.v[MP_EN_RCV] / 100.0);
                b.cable(RN_NOTE, 1, id, Dst::In, 0);
                dpth = s.v[MP_EN_DEPTH];
            } else if (s.type == MOD_EG) {
                NodeDesc *e = b.add(id, T_EG);
                if (!e) break;
                for (int pt = 0; pt < 4; pt++) {
                    e->param[EG_T1 + 3 * pt] = static_cast<int32_t>(s.v[3 * pt]);
                    e->param[EG_L1 + 3 * pt] = q(s.v[3 * pt + 1] / 100.0);
                    e->param[EG_C1 + 3 * pt] = q(s.v[3 * pt + 2] / 100.0);
                }
                e->param[EG_SUSTAIN] = static_cast<int32_t>(s.v[MP_EG_SUS]); e->param[EG_RELEASE] = ms_i(s.v[MP_EG_REL]);
                e->param[EG_RCURVE] = q(s.v[MP_EG_RCV] / 100.0); e->param[EG_ONESHOT] = s.v[MP_EG_ONE] > 0.5f ? 1 : 0;
                b.cable(RN_NOTE, 1, id, Dst::In, 0);
                dpth = s.v[MP_EG_DEPTH];
            } else {
                continue;                                            // MS: not realised yet
            }
            b.cable(id, 0, tgt[ti].node, Dst::Param, dst_param, q(dpth / dt.range));       // Dpth is in the target's unit
        }

        // ---- motion sequencers: one engine node per MS module, one cable per lane that has a realisable target
        for (int i = 0; i < rack.count && out.ms_count < MS_POOL; i++) {
            if (rack.slot[i].type != MOD_MSEQ) continue;
            const ms_pattern_t &pat = *rack_ms_const(&rack, i);
            int wired[MS_LANES] = {};
            bool any = false;
            for (int l = 0; l < MS_LANES; l++) {
                const ms_lane_t &ln = pat.lane[l];
                const int ti = ln.tgt_id ? rack_find(&rack, ln.tgt_id) : RACK_NONE;
                if (ti == RACK_NONE || ti == i || !tgt[ti].supported || !rack_ms_target_ok(static_cast<module_type_t>(rack.slot[ti].type), ln.tgt_param)) continue;
                wired[l] = ti + 1;
                any = true;
            }
            if (!any) continue;
            const int id = node_of(rack.slot[i].id);
            if (!b.add(id, T_MSEQ)) break;
            for (int l = 0; l < MS_LANES; l++) {
                if (!wired[l]) continue;
                const ms_lane_t &ln = pat.lane[l];
                const int ti = wired[l] - 1;
                const rack_slot_t &t = rack.slot[ti];
                DepthTarget dt;
                if (!depth_target(t, ln.tgt_param, true, dt)) continue;
                b.cable(id, l, tgt[ti].node, Dst::Param, dt.dst, q(ln.depth / dt.range));
            }
            MotionBlob &mb = out.ms_blob[out.ms_count];
            for (int l = 0; l < MS_LANES; l++) {
                for (int k = 0; k < MS_STEPS; k++) mb.val[l][k] = pat.lane[l].val[k];
                mb.active[l] = pat.lane[l].active;
                mb.linear[l] = pat.lane[l].linear;
                mb.bipolar[l] = pat.lane[l].bipolar;
            }
            out.ms_node[out.ms_count++] = id;
        }

        // ---- amplitude: the global AMP ENV page shapes every note
        NodeDesc *ae = b.add(RN_AMP_ENV, T_ENV);
        NodeDesc *av = b.add(RN_AMP_VCA, T_VCA_V);
        if (ae && av) {
            ae->param[ENV_ATTACK] = ms_i(params.amp_env.attack_ms); ae->param[ENV_DECAY] = ms_i(params.amp_env.decay_ms);
            ae->param[ENV_SUSTAIN] = q(params.amp_env.sustain); ae->param[ENV_RELEASE] = ms_i(params.amp_env.release_ms);
            ae->param[ENV_HOLD] = static_cast<int32_t>(params.amp_env.hold_ms);
            ae->param[ENV_A_CURVE] = q(params.amp_env.a_curve / 100.0); ae->param[ENV_D_CURVE] = q(params.amp_env.d_curve / 100.0); ae->param[ENV_R_CURVE] = q(params.amp_env.r_curve / 100.0);
            av->param[VCA_LEVEL] = 0;
            b.cable(RN_NOTE, 1, RN_AMP_ENV, Dst::In, 0);
            if (run != 0) b.cable(run, 0, RN_AMP_VCA, Dst::In, 0);
            b.cable(RN_AMP_ENV, 0, RN_AMP_VCA, Dst::Param, VCA_LEVEL);
            b.cable(RN_AMP_VCA, 0, RN_VOICE_OUT, Dst::In, 0);
            vo->param[VO_TAIL_MS] = ms_i(params.amp_env.release_ms) + 400;
        }
    }

    /* ---- master effects: BusIn -> the four slots of the FX rack in order -> MasterOut ---- */
    NodeDesc *bus = b.add(RN_BUS, T_BUS_IN);
    NodeDesc *ma = b.add(RN_MASTER, T_MASTER_OUT);
    if (bus && ma) {
        ma->param[0] = kUnity;
        int srcl = RN_BUS, srcr = RN_BUS, portl = 0, portr = 1;
        for (int k = 0; k < FXR_SLOTS; k++) {
            const fx_slot_t &fs = cfg.fxr.slot[k];
            if (fs.type == FX_NONE) continue;
            const int id = RN_FX + 2 * k;
            auto hz_pitch100 = [](int hz100) { return static_cast<int32_t>(std::lround(12.0 * kSemi * std::log2(hz100 / 100.0))); };
            auto wire = [&](int node) {                                           // the stereo effect node `node` takes the running signal
                b.cable(srcl, portl, node, Dst::In, 0); b.cable(srcr, portr, node, Dst::In, 1);
                srcl = srcr = node; portl = 0; portr = 1;
            };
            switch (fs.type) {
                case FX_DRIVE: {
                    NodeDesc *l = b.add(id, T_SHAPER_G), *r = b.add(id + 1, T_SHAPER_G);
                    if (!l || !r) break;
                    for (NodeDesc *n : {l, r}) {
                        n->param[SHP_MODE] = fs.v[0]; n->param[SHP_DRIVE] = fs.v[1] * 64; n->param[SHP_MIX] = q(fs.v[2] / 100.0); n->param[SHP_BITS] = fs.v[3];
                    }
                    b.cable(srcl, portl, id, Dst::In, 0); b.cable(srcr, portr, id + 1, Dst::In, 0);
                    srcl = id; srcr = id + 1; portl = portr = 0;
                } break;
                case FX_CHORUS: {
                    NodeDesc *n = b.add(id, T_CHORUS);
                    if (!n) break;
                    n->param[CHR_MODE] = fs.v[0]; n->param[CHR_MIX] = q(fs.v[1] / 100.0);
                    wire(id);
                } break;
                case FX_PHASER: {
                    NodeDesc *n = b.add(id, T_PHASER);
                    if (!n) break;
                    n->param[PHS_RATE] = hz_pitch100(fs.v[0]); n->param[PHS_DEPTH] = q(fs.v[1] / 100.0); n->param[PHS_FEEDBACK] = q(fs.v[2] / 100.0); n->param[PHS_MIX] = q(fs.v[3] / 100.0);
                    wire(id);
                } break;
                case FX_FLANGER: {
                    NodeDesc *n = b.add(id, T_FLANGER);
                    if (!n) break;
                    n->param[FLG_RATE] = hz_pitch100(fs.v[0]); n->param[FLG_DEPTH] = q(fs.v[1] / 100.0); n->param[FLG_FEEDBACK] = q(fs.v[2] / 100.0); n->param[FLG_MIX] = q(fs.v[3] / 100.0);
                    wire(id);
                } break;
                case FX_TREMOLO: {
                    NodeDesc *n = b.add(id, T_TREMOLO);
                    if (!n) break;
                    n->param[TRM_RATE] = hz_pitch100(fs.v[0]); n->param[TRM_DEPTH] = q(fs.v[1] / 100.0); n->param[TRM_SHAPE] = fs.v[2]; n->param[TRM_MODE] = fs.v[3];
                    wire(id);
                } break;
                case FX_COMP: {
                    NodeDesc *n = b.add(id, T_COMP);
                    if (!n) break;
                    n->param[CMP_THRESH] = fs.v[0]; n->param[CMP_RATIO] = fs.v[1] * 10; n->param[CMP_ATTACK] = 5; n->param[CMP_RELEASE] = fs.v[2]; n->param[CMP_MAKEUP] = fs.v[3];
                    wire(id);
                } break;
                case FX_EQ: {
                    NodeDesc *n = b.add(id, T_EQ3);
                    if (!n) break;
                    n->param[EQ_LOW] = fs.v[0] * 10; n->param[EQ_MID] = fs.v[1] * 10; n->param[EQ_MIDF] = fs.v[2] * kSemi; n->param[EQ_HIGH] = fs.v[3] * 10;
                    wire(id);
                } break;
                case FX_SHIFT: {
                    NodeDesc *n = b.add(id, T_SHIFTER);
                    if (!n) break;
                    n->param[SFT_MODE] = fs.v[0]; n->param[SFT_FREQ] = hz_pitch100(fs.v[1]); n->param[SFT_MIX] = q(fs.v[2] / 100.0);
                    wire(id);
                } break;
                case FX_DELAY: {
                    NodeDesc *n = b.add(id, T_DELAY);
                    if (!n) break;
                    n->param[DLY_TIME] = fs.v[0] * 16;
                    n->param[DLY_FEEDBACK] = std::min<int32_t>(q(fs.v[1] / 100.0), 31000);
                    n->param[DLY_MIX] = q(fs.v[2] / 100.0);
                    n->param[DLY_PINGPONG] = fs.v[3];
                    wire(id);
                } break;
                case FX_REVERB: {
                    NodeDesc *n = b.add(id, T_REVERB);
                    if (!n) break;
                    n->param[RVB_MIX] = q(fs.v[0] / 100.0);
                    n->param[RVB_DECAY] = std::min<int32_t>(q(fs.v[1] / 100.0), 32400);
                    n->param[RVB_SIZE] = q(fs.v[2] / 100.0);
                    n->param[RVB_DAMP] = 130 * kSemi - static_cast<int32_t>(std::lround(fs.v[3] / 100.0 * 75.0 * kSemi));
                    n->param[RVB_PREDELAY] = 20;
                    wire(id);
                } break;
                case FX_CAB: {
                    NodeDesc *n = b.add(id, T_CONV);
                    if (!n) break;
                    n->param[CNV_IR] = fs.v[0]; n->param[CNV_LENGTH] = fs.v[1]; n->param[CNV_MIX] = q(fs.v[2] / 100.0); n->param[CNV_LEVEL] = static_cast<int32_t>(std::lround(fs.v[3] * 327.67));
                    wire(id);
                } break;
                default: break;
            }
        }
        b.cable(srcl, portl, RN_MASTER, Dst::In, 0);
        b.cable(srcr, portr, RN_MASTER, Dst::In, 1);
    } else {
        b.ok = false;
    }
    return b.ok;
}

}  // namespace sc
