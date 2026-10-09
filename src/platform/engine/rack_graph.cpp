#include "platform/engine/rack_graph.h"
#include <algorithm>
#include <cmath>
#include "engine/modules/builtin.h"
#include "engine/modules/fx_modules.h"
#include "engine/modules/fx2_modules.h"
#include "engine/modules/motion_seq.h"
#include "engine/modules/mi_osc.h"
#include "engine/modules/osc_engines.h"
#include "engine/modules/para_modules.h"
#include "engine/modules/sampler_modules.h"
#include "engine/modules/strings_modules.h"
#include "engine/modules/synth_modules.h"
#include "platform/engine/dx7_convert.h"
#include "platform/engine/fm_patch_gain.h"

namespace sc {
namespace {

constexpr int kSemi = 256;

q15 q(double x) { return static_cast<q15>(std::lround(std::fmax(-1.0, std::fmin(1.0, x)) * 32767.0)); }
// A modulation cable's depth. Full scale is nudged to 32766: an exactly unity cable is wired straight through with no gain stage, so moving the
// depth knob onto or off the maximum would need a full graph rebuild (an audible stall) instead of a live gain change.
q15 qd(double x) { const q15 v = q(x); return v == kUnity ? static_cast<q15>(kUnity - 1) : v; }
int32_t hz_pitch(double hz) { return static_cast<int32_t>(std::lround(69.0 * kSemi + 12.0 * kSemi * std::log2(hz / 440.0))); }

// A rack filter type (FILT_*) as Filter parameters: the SVF types set mode / slope / a Q boost; the light ones (LP6, Ladr, ChLP) set the
// algorithm and pass the rack's Res (Q 0.5..10) as 0..1. Every type is a parameter change of the same node (switching type never rebuilds).
void filter_params(NodeDesc *f, int type, double cut_hz, double res_q) {
    static const int mode[FILT_COUNT] = {FLTM_LP, FLTM_LP, FLTM_BP, FLTM_HP, FLTM_LP, FLTM_NOTCH, FLTM_LP, FLTM_LP, FLTM_LP, FLTM_AP};
    static const int algo[FILT_COUNT] = {FLTA_SVF, FLTA_SVF, FLTA_SVF, FLTA_SVF, FLTA_SVF, FLTA_SVF, FLTA_LP6, FLTA_LADDER, FLTA_CHAM, FLTA_SVF};
    const int sections = type == FILT_LP24 ? 2 : 1;
    f->param[FLT_MODE] = mode[type];
    f->param[FLT_SECTIONS] = sections;
    f->param[FLT_ALGO] = algo[type];
    f->param[FLT_CUTOFF] = hz_pitch(cut_hz);
    if (algo[type] != FLTA_SVF) {
        f->param[FLT_RES] = q((res_q - 0.5) / 9.5);
    } else {
        const double q_last = sections == 2 ? 1.30656 : 0.70711;
        const double boost = (res_q / q_last - 1.0) / 15.0;           // Q of the last section = Butterworth Q x (1 + 15 res)
        f->param[FLT_RES] = q(boost > 0.0 ? boost : 0.0);
    }
}
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

struct Target { int node; int param; bool supported; int node2 = 0; };   // node2: the right channel's node of a pair (0 = none)

// Where a modulator's cable lands and how many of the rack's depth units (st / oct / %) a full-scale signal means there. The engine modules'
// own modulation ranges are set to match when their nodes are built (OSC_PITCH_MOD 96 st, FLT_CUT_MOD 8 oct, SHP_DRIVE_MOD 4 oct).
struct DepthTarget { int dst; double range; };
bool depth_target(const rack_slot_t &t, int param, bool supported, DepthTarget &out) {
    if (!supported) return false;
    switch (t.type) {
        case MOD_OSC:
            if (t.v[MP_OC_WAVE] >= OC_FIRST_MI) {                  // a Mutable Instruments model: the same four
                static const int dst[4] = {MI_PITCH, MI_LEVEL, MI_TIMBRE, MI_MORPH_P};
                out = DepthTarget{dst[param & 3], param == 0 ? 96.0 : 100.0};
                return param < 4;
            }
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

// The running signal of a shared part (a branch after its Para point, row M): a list of cables per channel. Mono = both channels are the
// same signal, so a mono module (filter, saturator) is one node; after a stereo effect it is a pair (a node per channel).
struct Src { int node, port; q15 g; };
struct Run2 {
    Src L[kMaxFanIn], R[kMaxFanIn];
    int n = 0;
    bool mono = true;
    void set_mono(int node, int port) { L[0] = R[0] = Src{node, port, kUnity}; n = 1; mono = true; }
    void set_stereo(int l, int pl, int r, int pr) { L[0] = Src{l, pl, kUnity}; R[0] = Src{r, pr, kUnity}; n = 1; mono = false; }
};

int32_t fx_rate(double hz) { return static_cast<int32_t>(std::lround(12.0 * kSemi * std::log2(hz))); }   // the FX nodes' rate unit

void sat_params(NodeDesc *n, const rack_slot_t &s) {
    n->param[SHP_MODE] = static_cast<int>(s.v[MP_SA_MODE]) % SHPM_N;        // the rack's list is the engine's order
    n->param[SHP_DRIVE] = static_cast<int32_t>(std::lround(std::log2(s.v[MP_SA_DRIVE] < 1 ? 1 : s.v[MP_SA_DRIVE]) * kSemi));
    n->param[SHP_MIX] = q(s.v[MP_SA_MIX]);
    n->param[SHP_BITS] = 4;
    n->param[SHP_DRIVE_MOD] = 4 * 256;                                       // cable depth = octaves / 4
}

void filter_env_params(NodeDesc *e, const rack_slot_t &s) {
    e->param[ENV_ATTACK] = ms_i(s.v[MP_FL_A]); e->param[ENV_DECAY] = ms_i(s.v[MP_FL_D]);
    e->param[ENV_SUSTAIN] = q(s.v[MP_FL_S]); e->param[ENV_RELEASE] = ms_i(s.v[MP_FL_R]);
    e->param[ENV_A_CURVE] = q(s.v[MP_FL_ACV] / 100.0); e->param[ENV_D_CURVE] = q(s.v[MP_FL_DCV] / 100.0); e->param[ENV_R_CURVE] = q(s.v[MP_FL_RCV] / 100.0);
}

// The engine node of an FX module: the stereo global one, or the mono per-voice one of a cheap effect (-1: none, a heavy effect per voice).
int fx_type(int mod, bool voice) {
    switch (mod) {
        case MOD_TREM:     return voice ? T_TREMOLO_V : T_TREMOLO;
        case MOD_EQ:       return voice ? T_EQ3_V : T_EQ3;
        case MOD_RING:     return voice ? T_SHIFTER_V : T_SHIFTER;
        case MOD_PHASER:   return voice ? T_PHASER_V : T_PHASER;
        case MOD_FLANGER:  return voice ? T_FLANGER_V : T_FLANGER;
        case MOD_COMP:     return voice ? T_COMP_V : T_COMP;
        case MOD_DELAY:    return voice ? -1 : T_DELAY;
        case MOD_REVERB:   return voice ? -1 : T_REVERB;
        case MOD_CHORUS:   return voice ? -1 : T_CHORUS;
        case MOD_SPECTRAL: return voice ? -1 : T_SPECTRAL;
        case MOD_CAB:      return voice ? -1 : T_CONV;
        case MOD_ENSEMBLE: return voice ? -1 : T_ENSEMBLE;
        default:           return -1;
    }
}

// An FX module's values (the rack's units, rack.c) as its engine node's parameters (the same for the stereo and the per-voice version).
void fx_params(NodeDesc *n, const rack_slot_t &s) {
    const float *v = s.v;
    switch (s.type) {
        case MOD_CHORUS:
            n->param[CHR_MODE] = static_cast<int32_t>(v[MP_CH_MODE]); n->param[CHR_MIX] = q(v[MP_CH_MIX]);
            break;
        case MOD_PHASER:
            n->param[PHS_RATE] = fx_rate(v[MP_PH_RATE]); n->param[PHS_DEPTH] = q(v[MP_PH_DEPTH]); n->param[PHS_FEEDBACK] = q(v[MP_PH_FB] / 100.0); n->param[PHS_MIX] = q(v[MP_PH_MIX]);
            break;
        case MOD_FLANGER:
            n->param[FLG_RATE] = fx_rate(v[MP_PH_RATE]); n->param[FLG_DEPTH] = q(v[MP_PH_DEPTH]); n->param[FLG_FEEDBACK] = q(v[MP_PH_FB] / 100.0); n->param[FLG_MIX] = q(v[MP_PH_MIX]);
            break;
        case MOD_TREM:
            n->param[TRM_RATE] = fx_rate(v[MP_TR_RATE]); n->param[TRM_DEPTH] = q(v[MP_TR_DEPTH]);
            n->param[TRM_SHAPE] = static_cast<int32_t>(v[MP_TR_SHAPE]); n->param[TRM_MODE] = static_cast<int32_t>(v[MP_TR_MODE]);
            break;
        case MOD_COMP:
            n->param[CMP_THRESH] = static_cast<int32_t>(std::lround(v[MP_CP_THR])); n->param[CMP_RATIO] = static_cast<int32_t>(std::lround(v[MP_CP_RATIO] * 10.0));
            n->param[CMP_ATTACK] = 5; n->param[CMP_RELEASE] = static_cast<int32_t>(std::lround(v[MP_CP_REL]));
            n->param[CMP_MAKEUP] = static_cast<int32_t>(std::lround(v[MP_CP_GAIN]));
            break;
        case MOD_EQ:
            n->param[EQ_LOW] = static_cast<int32_t>(std::lround(v[MP_EQ_LOW] * 10.0)); n->param[EQ_MID] = static_cast<int32_t>(std::lround(v[MP_EQ_MID] * 10.0));
            n->param[EQ_MIDF] = hz_pitch(v[MP_EQ_MIDF]); n->param[EQ_HIGH] = static_cast<int32_t>(std::lround(v[MP_EQ_HIGH] * 10.0));
            break;
        case MOD_RING:
            n->param[SFT_MODE] = static_cast<int32_t>(v[MP_RG_MODE]); n->param[SFT_FREQ] = fx_rate(v[MP_RG_FREQ]); n->param[SFT_MIX] = q(v[MP_RG_MIX]);
            break;
        case MOD_DELAY:
            n->param[DLY_TIME] = static_cast<int32_t>(std::lround(v[MP_DL_TIME])) * 16;
            n->param[DLY_FEEDBACK] = std::min<int32_t>(q(v[MP_DL_FB]), 31000);
            n->param[DLY_MIX] = q(v[MP_DL_MIX]);
            n->param[DLY_PINGPONG] = static_cast<int32_t>(v[MP_DL_PONG]);
            break;
        case MOD_REVERB:
            n->param[RVB_MIX] = q(v[MP_RV_MIX]);
            n->param[RVB_DECAY] = std::min<int32_t>(q(v[MP_RV_DEC]), 32400);
            n->param[RVB_SIZE] = q(v[MP_RV_SIZE]);
            n->param[RVB_DAMP] = 130 * kSemi - static_cast<int32_t>(std::lround(v[MP_RV_DAMP] * 75.0 * kSemi));
            n->param[RVB_PREDELAY] = 20;
            break;
        case MOD_ENSEMBLE:
            n->param[ENS_RATE] = static_cast<int32_t>(std::lround(v[MP_ES_RATE] * 100.0));   // hundredths of Hz
            n->param[ENS_DEPTH] = q(v[MP_ES_DEPTH]); n->param[ENS_SHIMMER] = q(v[MP_ES_SHIM]); n->param[ENS_MIX] = q(v[MP_ES_MIX]);
            break;
        case MOD_SPECTRAL: {                                              // mono STFT on (L + R) / 2, each channel keeps its dry
            auto note = [](double hz) { return std::max<int32_t>(0, std::min<int32_t>(135 * kSemi, hz_pitch(hz))); };
            n->param[SPX_MODE] = static_cast<int32_t>(v[MP_SP_MODE]); n->param[SPX_SHIFT] = static_cast<int32_t>(std::lround(v[MP_SP_SHIFT])) * kSemi;
            n->param[SPX_AMOUNT] = q(v[MP_SP_AMT]); n->param[SPX_MIX] = q(v[MP_SP_MIX]); n->param[SPX_FREEZE] = static_cast<int32_t>(v[MP_SP_HOLD]);
            n->param[SPX_LO] = note(v[MP_SP_LO]); n->param[SPX_HI] = note(v[MP_SP_HI]); n->param[SPX_STEREO] = 1;
        } break;
        case MOD_CAB:
            n->param[CNV_IR] = static_cast<int32_t>(v[MP_CB_IR]); n->param[CNV_LENGTH] = static_cast<int32_t>(std::lround(v[MP_CB_LEN]));
            n->param[CNV_MIX] = q(v[MP_CB_MIX]); n->param[CNV_LEVEL] = q(v[MP_CB_LEVEL]);
            break;
        default: break;
    }
}

}  // namespace

bool rack_graph_build(const rack_t &rack, const synth_params_t &params, const Registry &reg, RackGraph &out,
                      const int16_t *slot_of_file, int n_files) {
    out.g = GraphDesc{};
    out.fm = synth_type_is_fm(rack.cfg.type);
    out.ms_count = 0;
    out.conv_count = 0;
    B b{out.g, reg};
    const synth_config_t &cfg = rack.cfg;
    struct BrOut { bool on; int node_l, port_l, node_r, port_r; };  // a rack branch's output into row M's MIX (Para, mono: the same node twice)
    BrOut bout[RACK_BRANCHES] = {};
    Target tgt[RACK_MAX];                                            // where a modulator aimed at slot i lands
    for (auto &t : tgt) t = Target{0, 0, false};
    bool mod_osc[RACK_MAX] = {};                                     // oscillators that modulate a target (and join the chain through a cable's gain)
    bool tgt_global[RACK_MAX] = {};                                  // the slot's node is global (a modulator aimed at it must be global too)
    int gate_of[RACK_MAX] = {};                                      // GateIn output that gates a global envelope aimed at the slot (its branch's PEnv)
    bool have_amp_env = false, have_gate_in = false;                 // shared by the branches: the per-voice amp envelope, the keyboard gate
    auto ensure_gate_in = [&]() { if (!have_gate_in) { b.add(RN_GATE_IN, T_GATE_IN); have_gate_in = true; } };
    auto amp_env_params = [&](NodeDesc *e) {
        e->param[ENV_ATTACK] = ms_i(params.amp_env.attack_ms); e->param[ENV_DECAY] = ms_i(params.amp_env.decay_ms);
        e->param[ENV_SUSTAIN] = q(params.amp_env.sustain); e->param[ENV_RELEASE] = ms_i(params.amp_env.release_ms);
        e->param[ENV_HOLD] = static_cast<int32_t>(params.amp_env.hold_ms);
        e->param[ENV_A_CURVE] = q(params.amp_env.a_curve / 100.0); e->param[ENV_D_CURVE] = q(params.amp_env.d_curve / 100.0); e->param[ENV_R_CURVE] = q(params.amp_env.r_curve / 100.0);
    };
    auto take = [&](const Src *src, int n, int dst, int port) { for (int k = 0; k < n; k++) b.cable(src[k].node, src[k].port, dst, Dst::In, port, src[k].g); };
    auto env_replaced = [&](int rack_id) {                           // an ENV module aimed at a filter's cutoff replaces the filter's own envelope
        for (int j = 0; j < rack.count; j++) if (rack.slot[j].type == MOD_ENV && rack.slot[j].tgt_id == rack_id && rack.slot[j].tgt_param == 0) return true;
        return false;
    };
    // One module of a shared part (a branch after its Para point, row M) on the running signal `r`, its envelopes gated by GateIn output
    // `gport`. Filter / saturator: one node (mono) or a pair (stereo, a channel each); an effect: its stereo node (the signal is stereo after it).
    auto shared = [&](int i, Run2 &r, int gport) {
        const rack_slot_t &s = rack.slot[i];
        const int id = node_of(s.id);
        if (r.n == 0) return;
        tgt_global[i] = true;
        gate_of[i] = gport;
        if (s.type == MOD_FILTER || s.type == MOD_SAT) {
            const bool flt = s.type == MOD_FILTER;
            const int type = flt ? static_cast<int>(s.v[MP_FL_TYPE]) : 0;
            if (flt && type == FILT_OFF) return;
            const int nt = flt ? T_FILTER_G : T_SHAPER_G;
            NodeDesc *l = b.add(id, nt), *rr = r.mono ? l : b.add(id + 1, nt);
            if (!l || !rr) return;
            for (NodeDesc *n : {l, rr}) {
                if (flt) { filter_params(n, type < FILT_COUNT ? type : FILT_LP, s.v[MP_FL_CUT], s.v[MP_FL_RES]); n->param[FLT_CUT_MOD] = 96 * kSemi; }
                else sat_params(n, s);
            }
            take(r.L, r.n, id, 0);
            if (!r.mono) take(r.R, r.n, id + 1, 0);
            tgt[i] = Target{id, flt ? static_cast<int>(FLT_CUTOFF) : static_cast<int>(SHP_DRIVE), true, r.mono ? 0 : id + 1};
            if (flt && !env_replaced(s.id)) {                        // the filter's own envelope, gated by the keyboard as a whole (GateIn)
                const int env = r.mono ? id + 1 : RN_EXTRA + i;
                NodeDesc *e = b.add(env, T_ENV_G);
                if (e) {
                    filter_env_params(e, s);
                    ensure_gate_in();
                    b.cable(RN_GATE_IN, gport, env, Dst::In, 0);
                    const q15 amt = qd(std::fmax(0.0, s.v[MP_FL_ENVAMT]) / 8.0);
                    b.cable(env, 0, id, Dst::Param, FLT_CUTOFF, amt);
                    if (!r.mono) b.cable(env, 0, id + 1, Dst::Param, FLT_CUTOFF, amt);
                }
            }
            if (r.mono) r.set_mono(id, 0); else r.set_stereo(id, 0, id + 1, 0);
            return;
        }
        const int t = fx_type(s.type, false);
        if (t < 0) return;
        NodeDesc *n = b.add(id, t);
        if (!n) return;
        fx_params(n, s);
        if (s.type == MOD_CAB && out.conv_count < RackGraph::kMaxConv) {                  // its taps go with set_blob (engine_synth.cpp)
            out.conv_node[out.conv_count] = id; out.conv_ir[out.conv_count] = n->param[CNV_IR]; out.conv_len[out.conv_count] = n->param[CNV_LENGTH];
            out.conv_count++;
        }
        take(r.L, r.n, id, 0);
        take(r.R, r.n, id, 1);
        r.set_stereo(id, 0, id, 1);
    };

    /* ---- voice ---- */
    b.add(RN_NOTE, T_NOTE_IN);
    NodeDesc *vo = b.add(RN_VOICE_OUT, T_VOICE_OUT);
    vo->param[VO_LEVEL] = q(0.5);                                    // fixed: the master volume is applied by MasterOut, after the effects

    if (out.fm) {
        out.dx7_node = RN_DX7;
        NodeDesc *d = b.add(RN_DX7, T_DX7);
        d->param[DX7_GAIN] = static_cast<int32_t>(std::lround(fm_patch_gain[cfg.fm_patch & 127] * 8192.0));
        out.fm_patch = dx7_convert(cfg.fm);
        vo->param[VO_TAIL_MS] = static_cast<int32_t>(out.fm_patch.release_ms + 500);
        b.cable(RN_NOTE, 0, RN_DX7, Dst::In, 0);
        b.cable(RN_NOTE, 1, RN_DX7, Dst::In, 1);
        b.cable(RN_DX7, 0, RN_VOICE_OUT, Dst::In, 0);
    } else if (synth_type_is_strings(cfg.type)) {
        // ---- Strings (ADR-037): one module per voice; its envelope is the AMP ENV page (linear: the curves are not used)
        const str_params_t &sp = params.str;
        NodeDesc *s = b.add(RN_STR, T_STRINGS);
        if (s) {
            s->param[STR_WAVE] = sp.wave;
            s->param[STR_OSC] = sp.osc;
            s->param[STR_DETUNE] = static_cast<int32_t>(std::lround(sp.detune * kSemi / 100.0));
            s->param[STR_MIX] = q(sp.mix);
            s->param[STR_PW] = std::max<int32_t>(1638, std::min<int32_t>(31130, q(sp.pw)));
            s->param[STR_ATTACK] = ms_i(params.amp_env.attack_ms);
            s->param[STR_DECAY] = ms_i(params.amp_env.decay_ms);
            s->param[STR_SUSTAIN] = q(params.amp_env.sustain);
            s->param[STR_RELEASE] = ms_i(params.amp_env.release_ms);
            s->param[STR_LP_ON] = sp.lp_on;
            s->param[STR_LP_CUT] = std::max<int32_t>(24 * kSemi, std::min<int32_t>(135 * kSemi, hz_pitch(sp.lp_cut)));
            s->param[STR_LP_ENV] = static_cast<int32_t>(std::lround(sp.lp_env * 12.0 * kSemi));
            s->param[STR_LP_KEY] = q(sp.lp_key);
            s->param[STR_LEVEL] = q(sp.level);
            b.cable(RN_NOTE, 0, RN_STR, Dst::In, 0);
            b.cable(RN_NOTE, 1, RN_STR, Dst::In, 1);
            b.cable(RN_STR, 0, RN_VOICE_OUT, Dst::In, 0);
            vo->param[VO_TAIL_MS] = ms_i(params.amp_env.release_ms) + 400;
        }
    } else {
        // ---- the branches (ADR-041): each its own chain, its own Para split and amp, on its own voice bus (VoiceOut input 1 / 2,
        //      BusIn bus 0 / 1). Their outputs meet in row M's MIX (below). A branch without a source is not built.
        auto voice_amp_env = [&]() -> bool {                         // the AMP ENV page per voice: one node for both branches (same settings, same gate)
            if (have_amp_env) return true;
            NodeDesc *ae = b.add(RN_AMP_ENV, T_ENV);
            if (!ae) return false;
            amp_env_params(ae);
            b.cable(RN_NOTE, 1, RN_AMP_ENV, Dst::In, 0);
            return have_amp_env = true;
        };
        static const int kAmpVca[2] = {RN_AMP_VCA, RN_AMP_VCA2}, kParaGate[2] = {RN_PARA_GATE, RN_PARA_GATE2}, kBus[2] = {RN_BUS, RN_BUS2},
                         kParaAmpEnv[2] = {RN_PARA_AMP_ENV, RN_PARA_AMP_ENV2}, kParaAmpVca[2] = {RN_PARA_AMP_VCA, RN_PARA_AMP_VCA2},
                         kParaAmpVcaR[2] = {RN_PARA_AMP_VCA_R, RN_PARA_AMP_VCA2_R};
        for (int br = 0; br < RACK_BRANCHES; br++) {
            int n_sources = 0;
            for (int i = 0; i < rack.count; i++) if ((rack.slot[i].type == MOD_OSC || rack.slot[i].type == MOD_SAMPLER) && rack.slot[i].row == br) n_sources++;
            if (n_sources == 0) continue;
            const double lvl_scale = n_sources > 1 ? 1.0 / std::sqrt(static_cast<double>(n_sources)) : 1.0;
            // The per-voice running signal: the sum of these nodes' output 0, each through a cable of the given depth (none yet = empty). The
            // next module gets one cable per entry; the plan sums them in one MIX step (one pass, one saturation) instead of mixer nodes.
            struct RunSrc { int node; q15 depth; };
            RunSrc run[kMaxFanIn]; int n_run = 0;
            auto feed = [&](int dst, int port) { for (int k = 0; k < n_run; k++) b.cable(run[k].node, 0, dst, Dst::In, port, run[k].depth); };
            auto set_run = [&](int node) { run[0] = RunSrc{node, kUnity}; n_run = 1; };
            auto add_source = [&](int node, q15 depth, int spare) -> bool {
                if (n_run == kMaxFanIn) {                            // more sources than cables into one input: fold the sum so far into a pass-through mixer
                    NodeDesc *m = b.add(spare, T_MIX4_V);
                    if (!m) return false;
                    m->param[0] = kUnity; m->param[1] = 0; m->param[2] = 0; m->param[3] = 0;
                    feed(spare, 0);
                    set_run(spare);
                }
                run[n_run++] = RunSrc{node, depth};
                return true;
            };
            // Para (ADR-036 stage 2, ADR-041): everything before the branch's shared start runs per voice; that module and what follows run
            // once, on the sum of the branch's voices (the shared builder, as row M). The rack rules keep the sources and the resonator before it.
            const int split_at = rack_shared_start(&rack, br);
            const bool para = split_at != RACK_NONE;
            const int penv = para ? rack.slot[split_at].penv : static_cast<int>(PARA_ENV_LEGATO);
            const int gate_port = penv == PARA_ENV_LEGATO ? 0 : 1;  // GateIn output that gates the shared envelopes (legato / every key)
            bool global = false;                                     // past the split
            Run2 sh;                                                 // the shared part's running signal (mono until a stereo effect)
            auto to_voice_out = [&](int node) {                      // the end of the branch's voice part: into its voice bus
                b.cable(node, 0, RN_VOICE_OUT, Dst::In, br);
                if (br == 1) vo->param[VO_IN2] = 1;
                vo->param[VO_TAIL_MS] = ms_i(params.amp_env.release_ms) + 400;
            };
            auto bus_in = [&]() {                                    // the sum of the branch's voices
                NodeDesc *bus = b.add(kBus[br], T_BUS_IN);
                if (bus) bus->param[BUS_SEL] = br;
            };
            // The end of the voice part: each voice is gated (or gets its own amp envelope), the voices are summed, the shared chain starts.
            auto split = [&]() {
                int tail = 0;
                if (penv == PARA_ENV_VOICE) {
                    NodeDesc *av = b.add(kAmpVca[br], T_VCA_V);
                    if (!voice_amp_env() || !av) return;
                    av->param[VCA_LEVEL] = 0;
                    feed(kAmpVca[br], 0);
                    b.cable(RN_AMP_ENV, 0, kAmpVca[br], Dst::Param, VCA_LEVEL);
                    tail = kAmpVca[br];
                } else {
                    if (!b.add(kParaGate[br], T_PARA_GATE)) return;
                    feed(kParaGate[br], 0);
                    tail = kParaGate[br];
                }
                to_voice_out(tail);                                  // the last chord sounds through the shared release
                bus_in();
                ensure_gate_in();
                sh.set_mono(kBus[br], 0);                            // its output 0 (left; the voices are centred, left = right)
                global = true;
            };

            for (int i = 0; i < rack.count; i++) {
                if (!rack_slot_is_audio(&rack, i)) continue;
                const rack_slot_t &s = rack.slot[i];
                if (s.row != br) continue;
                if (para && !global && i >= split_at) split();
                if (global) { if (s.type != MOD_COMB) shared(i, sh, gate_port); continue; }   // (the rules keep the resonator per voice)
                const int id = node_of(s.id), aux = id + 1;
                if (s.type == MOD_OSC) {
                    const int wave = static_cast<int>(s.v[MP_OC_WAVE]);
                    const bool mi = wave >= OC_FIRST_MI, eng = !mi && wave >= OC_FIRST_ENGINE;
                    NodeDesc *o = b.add(id, mi ? T_MIOSC : (eng ? T_OSCX : T_OSC));
                    if (!o) break;
                    const int32_t tune = 60 * kSemi + static_cast<int32_t>(std::lround((s.v[MP_OC_COARSE] + s.v[MP_OC_FINE] / 100.0) * kSemi));
                    int lvl_idx;
                    if (mi) {
                        o->param[MI_MODEL] = wave - OC_FIRST_MI;
                        o->param[MI_PITCH] = tune;
                        o->param[MI_TIMBRE] = q(s.v[MP_OC_PW]);
                        o->param[MI_MORPH_P] = q(s.v[MP_OC_MORPH]);
                        o->param[MI_HARM] = q(s.v[MP_OC_HARM]);
                        o->param[MI_PITCH_MOD] = 96 * kSemi;
                        lvl_idx = MI_LEVEL;
                        b.cable(RN_NOTE, 1, id, Dst::In, 1);                 // the gate strikes the percussive models
                    } else if (eng) {
                        o->param[OSCX_ENGINE] = wave - OC_FIRST_ENGINE;
                        o->param[OSCX_PITCH] = tune;
                        o->param[OSCX_TIMBRE] = q(s.v[MP_OC_PW]);
                        o->param[OSCX_MORPH] = q(s.v[MP_OC_MORPH]);
                        o->param[OSCX_PITCH_MOD] = 96 * kSemi;
                        o->param[OSCX_QUAL] = static_cast<int32_t>(s.v[MP_OC_QUAL]);
                        lvl_idx = OSCX_LEVEL;
                        b.cable(RN_NOTE, 1, id, Dst::In, 1);                 // the gate strikes the string / modes
                    } else {
                        o->param[OSC_WAVE] = osc_wave(wave);
                        o->param[OSC_PITCH] = tune;
                        o->param[OSC_PW] = q(s.v[MP_OC_PW]);
                        o->param[OSC_PITCH_MOD] = 96 * kSemi;                // cable depth = semitones / 96
                        o->param[OSC_QUAL] = static_cast<int32_t>(s.v[MP_OC_QUAL]);
                        lvl_idx = OSC_LEVEL;
                    }
                    b.cable(RN_NOTE, 0, id, Dst::In, 0);
                    tgt[i] = Target{id, mi ? static_cast<int>(MI_PITCH) : (eng ? static_cast<int>(OSCX_PITCH) : static_cast<int>(OSC_PITCH)), true};
                    const bool modulating = s.tgt_id != 0 && rack_find(&rack, s.tgt_id) != RACK_NONE;
                    if (!modulating) {
                        o->param[lvl_idx] = q(s.v[MP_OC_LEVEL] * lvl_scale);
                        if (!add_source(id, kUnity, aux)) break;
                    } else {
                        // the oscillator runs at full level so the modulation signal does not depend on Lvl or Mute;
                        // its cable into the chain applies Lvl (and Mute: depth 0). Never exactly unity, so a level change stays a live gain write.
                        o->param[lvl_idx] = kUnity;
                        const q15 g = s.v[MP_OC_MUTE] > 0.5f ? 0 : qd(s.v[MP_OC_LEVEL] * lvl_scale);
                        if (!add_source(id, g, aux)) break;
                        mod_osc[i] = true;
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
                    if (!add_source(id, kUnity, aux)) break;
                } else if (s.type == MOD_FILTER) {
                    const int type = static_cast<int>(s.v[MP_FL_TYPE]);
                    if (n_run == 0 || type == FILT_OFF) continue;           // a filter with nothing to its left (or switched off) does nothing
                    NodeDesc *f = b.add(id, T_FILTER_V);
                    if (!f) break;
                    filter_params(f, type < FILT_COUNT ? type : FILT_LP, s.v[MP_FL_CUT], s.v[MP_FL_RES]);
                    f->param[FLT_CUT_MOD] = 96 * kSemi;                   // full-scale modulation = 8 octaves
                    feed(id, 0);
                    tgt[i] = Target{id, FLT_CUTOFF, true};
                    // The filter's own envelope (an ENV module aimed at the cutoff replaces it). Always present (amount 0 = a cable with gain 0):
                    // creating it only above 0 made the amount knob change the graph's shape at 0.00, a full rebuild (a stall of 100+ ms).
                    if (!env_replaced(s.id)) {
                        NodeDesc *e = b.add(aux, T_ENV);
                        if (!e) break;
                        filter_env_params(e, s);
                        b.cable(RN_NOTE, 1, aux, Dst::In, 0);
                        b.cable(aux, 0, id, Dst::Param, FLT_CUTOFF, qd(std::fmax(0.0, s.v[MP_FL_ENVAMT]) / 8.0));
                    }
                    set_run(id);
                } else if (s.type == MOD_COMB) {
                    if (n_run == 0) continue;                                // nothing to its left
                    NodeDesc *c = b.add(id, T_COMB);
                    if (!c) break;
                    c->param[CMB_TUNE] = 60 * kSemi + static_cast<int32_t>(std::lround(s.v[MP_RS_TUNE] * kSemi));
                    c->param[CMB_FEEDBACK] = std::max<int32_t>(-31000, std::min<int32_t>(31000, q(s.v[MP_RS_FB] / 100.0)));
                    c->param[CMB_DAMP] = hz_pitch(s.v[MP_RS_DAMP]);
                    c->param[CMB_INTERVAL] = static_cast<int32_t>(s.v[MP_RS_INT]);
                    c->param[CMB_MIX] = q(s.v[MP_RS_MIX]);
                    feed(id, 0);
                    b.cable(RN_NOTE, 0, id, Dst::In, 1);                     // the pitch CV: the resonator follows the key
                    set_run(id);
                } else if (s.type == MOD_SAT) {
                    if (n_run == 0) continue;
                    NodeDesc *sh_node = b.add(id, T_SHAPER_V);
                    if (!sh_node) break;
                    sat_params(sh_node, s);
                    feed(id, 0);
                    tgt[i] = Target{id, SHP_DRIVE, true};
                    set_run(id);
                } else if (rack_is_fx(static_cast<module_type_t>(s.type))) {
                    // a cheap effect before the Para point: its per-voice (mono) version (the heavy ones are always the split: never here)
                    const int t = fx_type(s.type, true);
                    if (n_run == 0 || t < 0) continue;
                    NodeDesc *n = b.add(id, t);
                    if (!n) break;
                    fx_params(n, s);
                    feed(id, 0);
                    set_run(id);
                }
            }

            if (para && !global) split();                            // no processor: the whole chain is per voice, only the amp is shared

            // the branch's amplitude (the AMP ENV page) and its output into the MIX: Para = one shared amp envelope (unless every voice has
            // its own) on the shared signal (mono, or a pair after a stereo effect); otherwise a per-voice amp, the branch's bus in stereo
            BrOut &o = bout[br];
            if (para) {
                if (penv == PARA_ENV_VOICE) {
                    o = BrOut{true, sh.L[0].node, sh.L[0].port, sh.R[0].node, sh.R[0].port};   // past the split: one node or a pair
                } else {
                    NodeDesc *ge = b.add(kParaAmpEnv[br], T_ENV_G), *gv = b.add(kParaAmpVca[br], T_VCA_G);
                    NodeDesc *gr = sh.mono ? gv : b.add(kParaAmpVcaR[br], T_VCA_G);
                    if (!ge || !gv || !gr) continue;
                    amp_env_params(ge);
                    gv->param[VCA_LEVEL] = 0; gr->param[VCA_LEVEL] = 0;
                    b.cable(RN_GATE_IN, gate_port, kParaAmpEnv[br], Dst::In, 0);
                    take(sh.L, sh.n, kParaAmpVca[br], 0);
                    b.cable(kParaAmpEnv[br], 0, kParaAmpVca[br], Dst::Param, VCA_LEVEL);
                    if (sh.mono) {
                        o = BrOut{true, kParaAmpVca[br], 0, kParaAmpVca[br], 0};
                    } else {
                        take(sh.R, sh.n, kParaAmpVcaR[br], 0);
                        b.cable(kParaAmpEnv[br], 0, kParaAmpVcaR[br], Dst::Param, VCA_LEVEL);
                        o = BrOut{true, kParaAmpVca[br], 0, kParaAmpVcaR[br], 0};
                    }
                }
            } else {
                NodeDesc *av = b.add(kAmpVca[br], T_VCA_V);
                if (!voice_amp_env() || !av) continue;
                av->param[VCA_LEVEL] = 0;
                feed(kAmpVca[br], 0);
                b.cable(RN_AMP_ENV, 0, kAmpVca[br], Dst::Param, VCA_LEVEL);
                to_voice_out(kAmpVca[br]);
                bus_in();
                o = BrOut{true, kBus[br], 0, kBus[br], 1};
            }
        }
    }

    /* ---- row M (ADR-041): the MIX (each branch's output at its level and pan; FM / Strings: their voice bus) -> row M's modules in
     *      slot order (the shared builder: stereo) -> MasterOut. The running signal is a list of cables per channel, so the MIX costs
     *      no node of its own. ---- */
    Run2 m;
    m.mono = false;
    if (synth_type_is_rack(cfg.type)) {
        for (int br = 0; br < RACK_BRANCHES; br++) {
            if (!bout[br].on) continue;
            const double lvl = rack.br_lvl[br], pan = rack.br_pan[br] / 100.0;               // linear pan: the louder side stays at the level
            const double gl = pan > 0 ? 1.0 - pan : 1.0, gr = pan < 0 ? 1.0 + pan : 1.0;
            m.L[m.n] = Src{bout[br].node_l, bout[br].port_l, qd(lvl * gl)};                 // (never exactly unity: a level change stays live)
            m.R[m.n] = Src{bout[br].node_r, bout[br].port_r, qd(lvl * gr)};
            m.n++;
        }
    } else if (b.add(RN_BUS, T_BUS_IN)) {
        m.set_stereo(RN_BUS, 0, RN_BUS, 1);
    }
    NodeDesc *ma = b.add(RN_MASTER, T_MASTER_OUT);
    if (!ma) return false;
    // volume 0..2 = the gain: up to 1 the level itself, above 1 half of it doubled by the boost (min(vol, 1) here made every volume above 1 a gain of 2)
    const bool boost = cfg.volume > 1.0f;
    ma->param[0] = q(boost ? cfg.volume * 0.5f : cfg.volume);
    ma->param[1] = boost ? 1 : 0;
    ma->param[2] = cfg.mono;
    const str_params_t &sp = params.str;
    if (synth_type_is_strings(cfg.type) && sp.ftype != FILT_OFF && sp.ftype < FILT_COUNT) {
        // the Strings type's shared filter, before row M (one per channel; Off = not built, a change of that is a rebuild)
        NodeDesc *fl = b.add(RN_STR_FLT_L, T_FILTER_G), *fr = b.add(RN_STR_FLT_R, T_FILTER_G);
        if (fl && fr) {
            for (NodeDesc *f : {fl, fr}) filter_params(f, sp.ftype, sp.fcut, sp.fres);
            take(m.L, m.n, RN_STR_FLT_L, 0);
            take(m.R, m.n, RN_STR_FLT_R, 0);
            m.set_stereo(RN_STR_FLT_L, 0, RN_STR_FLT_R, 0);
        }
    }
    for (int i = 0; i < rack.count; i++)
        if (rack.slot[i].row == ROW_M && rack_slot_is_audio(&rack, i) && m.n) shared(i, m, 0);   // (row M's envelopes: legato)
    take(m.L, m.n, RN_MASTER, 0);
    take(m.R, m.n, RN_MASTER, 1);

    if (synth_type_is_rack(cfg.type)) {
        // ---- modulators (of both branches and row M; a target that was not built is not supported). A target that runs once (shared) gets
        //      a global modulator; a target that is a pair (a channel each) gets the cable twice.
        for (int i = 0; i < rack.count; i++) {
            const rack_slot_t &s = rack.slot[i];
            if (!rack_slot_is_mod(&rack, i) || !s.tgt_id) continue;
            if (s.type == MOD_OSC && !mod_osc[i]) continue;       // (its target is gone)
            const int ti = rack_find(&rack, s.tgt_id);
            if (ti == RACK_NONE || ti == i) continue;
            const rack_slot_t &t = rack.slot[ti];
            const int id = node_of(s.id);
            DepthTarget dt;
            if (!depth_target(t, s.tgt_param, tgt[ti].supported, dt)) continue;   // not realised (resonance, mix, modulating a modulator, ...)
            const int dst_param = dt.dst;
            // the modulator itself
            double dpth = 1.0;
            const bool to_global = tgt_global[ti];                   // the target runs once (Para, row M): the modulator must be global too
            if (s.type == MOD_LFO) {
                NodeDesc *l = b.add(id, to_global ? T_LFO_G : T_LFO_V);
                if (!l) break;
                l->param[LFO_SHAPE] = lfo_shape(static_cast<int>(s.v[MP_LF_SHAPE]));
                l->param[LFO_RATE] = static_cast<int32_t>(std::lround(12.0 * kSemi * std::log2(s.v[MP_LF_RATE])));
                dpth = s.v[MP_LF_DEPTH];
            } else if (s.type == MOD_OSC) {
                if (to_global) continue;                             // a voice oscillator cannot reach the shared chain
                dpth = s.v[MP_OC_DEPTH];                             // the node already exists: it is the chain's oscillator
            } else if (s.type == MOD_ENV) {
                NodeDesc *e = b.add(id, to_global ? T_ENV_G : T_ENV);
                if (!e) break;
                e->param[ENV_ATTACK] = ms_i(s.v[MP_EN_A]); e->param[ENV_DECAY] = ms_i(s.v[MP_EN_D]);
                e->param[ENV_SUSTAIN] = q(s.v[MP_EN_S]); e->param[ENV_RELEASE] = ms_i(s.v[MP_EN_R]);
                e->param[ENV_HOLD] = static_cast<int32_t>(s.v[MP_EN_HOLD]); e->param[ENV_START] = q(s.v[MP_EN_START]);
                e->param[ENV_A_CURVE] = q(s.v[MP_EN_ACV] / 100.0); e->param[ENV_D_CURVE] = q(s.v[MP_EN_DCV] / 100.0); e->param[ENV_R_CURVE] = q(s.v[MP_EN_RCV] / 100.0);
                if (to_global) { ensure_gate_in(); b.cable(RN_GATE_IN, gate_of[ti], id, Dst::In, 0); } else b.cable(RN_NOTE, 1, id, Dst::In, 0);
                dpth = s.v[MP_EN_DEPTH];
            } else if (s.type == MOD_EG) {
                if (to_global) continue;                             // (no global EG yet: not realised on the shared chain)
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
            const q15 d = qd(dpth / dt.range);                       // Dpth is in the target's unit
            b.cable(id, 0, tgt[ti].node, Dst::Param, dst_param, d);
            if (tgt[ti].node2) b.cable(id, 0, tgt[ti].node2, Dst::Param, dst_param, d);
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
                b.cable(id, l, tgt[ti].node, Dst::Param, dt.dst, qd(ln.depth / dt.range));
                if (tgt[ti].node2) b.cable(id, l, tgt[ti].node2, Dst::Param, dt.dst, qd(ln.depth / dt.range));
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
    }
    return b.ok;
}

}  // namespace sc
