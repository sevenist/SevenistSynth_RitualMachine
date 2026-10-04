#include "engine/modules/synth_modules.h"
#include <cmath>
#include "engine/dsp/block.h"
#include "engine/dsp/curve.h"
#include "engine/dsp/interp.h"
#include "engine/dsp/osc.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/smooth.h"
#include "engine/dsp/svf.h"
#include "engine/dsp/util.h"

namespace sc {
namespace {

constexpr int32_t kSemi = 256;                       // pitch units per semitone (12 * 256 = one octave)

inline int32_t scaled(q15 m, int32_t range) { return static_cast<int32_t>((static_cast<int64_t>(m) * range) >> 15); }

/* ------------------------------------------------------------------ Osc */

template <Scope S>
class Osc : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Osc", S, 1, 1, OSC_N, false, {"pitch"}, {"out"},
            {{"wave", WAVE_SAW_, 0, 5}, {"pitch", 60 * kSemi, 0, 127 * kSemi}, {"pw", 16384, 1024, 31744},
             {"level", kUnity, 0, kUnity}, {"pitch_mod", 12 * kSemi, 0, 96 * kSemi}}};
        return i;
    }
    bool init(Memory &) override { a4_ = inc_a4(); return true; }
    void reset() override { ph_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case OSC_WAVE: wave_ = v; break;
            case OSC_PITCH: pitch_ = v; break;
            case OSC_PW: pw_ = static_cast<q15>(v); break;
            case OSC_LEVEL: level_ = static_cast<q15>(v); break;
            case OSC_PITCH_MOD: pmod_ = v; break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const int n = ctx.frames;
        const q15 *cv = p.in[0], *mp = p.mod[OSC_PITCH], *mw = p.mod[OSC_PW], *ml = p.mod[OSC_LEVEL];
        const bool varying = mp || cv[0] != cv[n - 1];
        uint32_t inc = pitch_to_inc(pitch_ + scaled(cv[0], kPitchCvSpan), a4_);
        for (int i = 0; i < n; i++) {
            if (varying) inc = pitch_to_inc(pitch_ + scaled(cv[i], kPitchCvSpan) + (mp ? scaled(mp[i], pmod_) : 0), a4_);
            q15 pw = mw ? sat16(pw_ + mw[i]) : pw_;
            if (pw < 1024) pw = 1024;
            q15 s;
            switch (wave_) {
                case WAVE_SINE_: s = sine(ph_); break;
                case WAVE_SAW_: s = osc_saw(ph_, inc); break;
                case WAVE_PULSE_: s = osc_pulse(ph_, inc, static_cast<uint32_t>(pw) << 17); break;
                case WAVE_TRI_: s = osc_tri(ph_, inc); break;
                case WAVE_SAW_DOWN_: s = neg15(osc_saw(ph_, inc)); break;
                default: s = noise_.next(); break;
            }
            q15 lv = ml ? sat16(level_ + ml[i]) : level_;
            p.out[0][i] = mul15(s, lv);
            ph_ += inc;
        }
    }
private:
    int32_t wave_ = WAVE_SAW_, pitch_ = 60 * kSemi, pmod_ = 12 * kSemi;
    q15 pw_ = 16384, level_ = kUnity;
    uint32_t ph_ = 0, a4_ = 0;
    Noise noise_{0xC0FFEEu};
};

/* ------------------------------------------------------------------ Env */

// One envelope segment: from -> to over `frames` samples along a curve (dsp/curve.h). All levels are Q31 in an int64.
struct Segment {
    int64_t from = 0, to = 0;
    uint32_t p = 0, inc = 0;
    const CurveLut *lut = nullptr;
    void begin(int64_t y, int64_t target, int32_t frames, const CurveLut *l) {
        from = y; to = target; p = 0; lut = l;
        inc = frames <= 1 ? 0xFFFFFFFFu : static_cast<uint32_t>((1ull << 32) / static_cast<uint64_t>(frames));
    }
    // advances one sample; returns false when the segment has just finished (y = target)
    bool step(int64_t &y) {
        const uint64_t pn = static_cast<uint64_t>(p) + inc;
        if (pn >= (1ull << 32)) { y = to; return false; }
        p = static_cast<uint32_t>(pn);
        y = from + (((to - from) * lut->at(p)) >> 15);
        return true;
    }
};

inline int32_t ms_frames(int32_t ms) {
    const int64_t f = static_cast<int64_t>(ms) * kSampleRate / 1000;
    return f < 1 ? 1 : (f > 0x3FFFFFFF ? 0x3FFFFFFF : static_cast<int32_t>(f));
}

// ADSR with a hold stage, a start level and a curve (tension) on attack, decay and release. Segments have an exact length: the
// attack reaches 1.0 after `attack` ms, the release reaches 0 after `release` ms, whatever the curve.
class Env : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Env", Scope::Voice, 1, 1, ENV_N, false, {"gate"}, {"out"},
            {{"attack", 5, 1, 20000}, {"decay", 200, 1, 20000}, {"sustain", 20000, 0, kUnity}, {"release", 300, 1, 20000},
             {"hold", 0, 0, 20000}, {"start", 0, 0, kUnity}, {"a_curve", 18000, -kUnity, kUnity}, {"d_curve", 20000, -kUnity, kUnity},
             {"r_curve", 20000, -kUnity, kUnity}}};
        return i;
    }
    bool init(Memory &) override { for (int i = 0; i < 3; i++) lut_[i].build(i == 0 ? 18000 : 20000); return true; }
    void reset() override { y_ = 0; stage_ = Idle; gate_ = false; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case ENV_ATTACK: fa_ = ms_frames(v); break;
            case ENV_DECAY: fd_ = ms_frames(v); break;
            case ENV_SUSTAIN: sus_ = static_cast<int64_t>(v) << 16; break;
            case ENV_RELEASE: fr_ = ms_frames(v); break;
            case ENV_HOLD: fh_ = v <= 0 ? 0 : ms_frames(v); break;
            case ENV_START: start_ = static_cast<int64_t>(v) << 16; break;
            case ENV_A_CURVE: lut_[0].build(static_cast<q15>(v)); break;
            case ENV_D_CURVE: lut_[1].build(static_cast<q15>(v)); break;
            case ENV_R_CURVE: lut_[2].build(static_cast<q15>(v)); break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        for (int i = 0; i < ctx.frames; i++) {
            const bool g = p.in[0][i] > 16384;
            if (g && !gate_) { if (y_ < start_) y_ = start_; seg_.begin(y_, kMax, fa_, &lut_[0]); stage_ = Attack; }
            else if (!g && gate_ && stage_ != Idle) { seg_.begin(y_, 0, fr_, &lut_[2]); stage_ = Release; }
            gate_ = g;
            switch (stage_) {
                case Attack:
                    if (!seg_.step(y_)) { if (fh_ > 0) { hold_ = 0; stage_ = Hold; } else { seg_.begin(y_, sus_, fd_, &lut_[1]); stage_ = Decay; } }
                    break;
                case Hold:
                    if (++hold_ >= fh_) { seg_.begin(y_, sus_, fd_, &lut_[1]); stage_ = Decay; }
                    break;
                case Decay:
                    if (!seg_.step(y_)) stage_ = Sustain;
                    break;
                case Sustain:
                    y_ += (sus_ - y_) >> 8;                                   // follows live edits of the sustain level without a step
                    break;
                case Release:
                    if (!seg_.step(y_)) { y_ = 0; stage_ = Idle; }
                    break;
                case Idle: break;
            }
            p.out[0][i] = static_cast<q15>(y_ >> 16);
        }
    }
private:
    enum Stage { Idle, Attack, Hold, Decay, Sustain, Release };
    static constexpr int64_t kMax = 0x7FFFFFFF;
    CurveLut lut_[3];
    Segment seg_;
    int64_t y_ = 0, sus_ = 20000ll << 16, start_ = 0;
    int32_t fa_ = ms_frames(5), fd_ = ms_frames(200), fr_ = ms_frames(300), fh_ = 0, hold_ = 0;
    Stage stage_ = Idle;
    bool gate_ = false;
};

/* ------------------------------------------------------------------ Eg (multi-stage envelope) */

// Four breakpoints, each with a time (ms, 0 = unused), a level and a curve, played in order from 0 (or from the current level on a
// retrigger). `sustain` = how many points are played before the envelope holds until the gate goes off (then it releases to 0 over
// `release` ms with its own curve). `oneshot` ignores the gate: after the last played point it releases at once, so a kick's pitch or
// amplitude shape always plays out in full.
class Eg : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Eg", Scope::Voice, 1, 1, EG_N, false, {"gate"}, {"out"},
            {{"t1", 1, 0, 20000}, {"l1", kUnity, 0, kUnity}, {"c1", 0, -kUnity, kUnity},
             {"t2", 100, 0, 20000}, {"l2", 16384, 0, kUnity}, {"c2", 20000, -kUnity, kUnity},
             {"t3", 0, 0, 20000}, {"l3", 0, 0, kUnity}, {"c3", 20000, -kUnity, kUnity},
             {"t4", 0, 0, 20000}, {"l4", 0, 0, kUnity}, {"c4", 20000, -kUnity, kUnity},
             {"sustain", 2, 1, 4}, {"release", 200, 1, 20000}, {"r_curve", 20000, -kUnity, kUnity}, {"oneshot", 0, 0, 1}}};
        return i;
    }
    bool init(Memory &) override { for (auto &l : lut_) l.build(0); lut_[4].build(20000); return true; }
    void reset() override { y_ = 0; stage_ = Idle; gate_ = false; }
    void set_param(int idx, int32_t v) override {
        if (idx < 12) {
            const int pt = idx / 3;
            switch (idx % 3) {
                case 0: fr_t_[pt] = v <= 0 ? 0 : ms_frames(v); break;
                case 1: lvl_[pt] = static_cast<int64_t>(v) << 16; break;
                default: lut_[pt].build(static_cast<q15>(v)); break;
            }
            return;
        }
        switch (idx) {
            case EG_SUSTAIN: sus_n_ = v < 1 ? 1 : (v > 4 ? 4 : v); break;
            case EG_RELEASE: rel_f_ = ms_frames(v); break;
            case EG_RCURVE: lut_[4].build(static_cast<q15>(v)); break;
            case EG_ONESHOT: oneshot_ = v != 0; break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        for (int i = 0; i < ctx.frames; i++) {
            const bool g = p.in[0][i] > 16384;
            if (g && !gate_) { next_point(0); }
            else if (!g && gate_ && stage_ != Idle && stage_ != Release && !oneshot_) { seg_.begin(y_, 0, rel_f_, &lut_[4]); stage_ = Release; }
            gate_ = g;
            switch (stage_) {
                case Point:
                    if (!seg_.step(y_)) next_point(pt_ + 1);
                    break;
                case Sustain:
                    if (oneshot_) { seg_.begin(y_, 0, rel_f_, &lut_[4]); stage_ = Release; }
                    break;
                case Release:
                    if (!seg_.step(y_)) { y_ = 0; stage_ = Idle; }
                    break;
                case Idle: break;
            }
            p.out[0][i] = static_cast<q15>(y_ >> 16);
        }
    }
private:
    enum Stage { Idle, Point, Sustain, Release };
    void next_point(int from_pt) {                               // starts the next used point before the sustain point, or sustains
        for (int k = from_pt; k < sus_n_; k++) {
            if (fr_t_[k] <= 0) continue;
            pt_ = k;
            seg_.begin(y_, lvl_[k], fr_t_[k], &lut_[k]);
            stage_ = Point;
            return;
        }
        stage_ = Sustain;
    }
    CurveLut lut_[5];                                            // four points + the release
    Segment seg_;
    int64_t y_ = 0, lvl_[4] = {0x7FFFFFFFll, 16384ll << 16, 0, 0};
    int32_t fr_t_[4] = {ms_frames(1), ms_frames(100), 0, 0}, rel_f_ = ms_frames(200), sus_n_ = 2, pt_ = 0;
    Stage stage_ = Idle;
    bool gate_ = false, oneshot_ = false;
};

/* ------------------------------------------------------------------ Lfo */

template <Scope S>
class Lfo : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Lfo", S, 0, 1, LFO_N, false, {}, {"out"},
            {{"shape", LFOS_SINE, 0, 5}, {"rate", 2 * 12 * kSemi, -6 * 12 * kSemi, 8 * 12 * kSemi}, {"level", kUnity, 0, kUnity},
             {"unipolar", 0, 0, 1}, {"rate_mod", 2 * 12 * kSemi, 0, 8 * 12 * kSemi}}};
        return i;
    }
    bool init(Memory &) override { one_hz_ = hz_to_inc(1.0f); return true; }
    void reset() override { if (S == Scope::Voice) ph_ = 0; held_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case LFO_SHAPE: shape_ = v; break;
            case LFO_RATE: rate_ = v; break;
            case LFO_LEVEL: level_ = static_cast<q15>(v); break;
            case LFO_UNIPOLAR: uni_ = v != 0; break;
            case LFO_RATE_MOD: rmod_ = v; break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const q15 *mr = p.mod[LFO_RATE];
        uint32_t inc = rate_inc(rate_);
        for (int i = 0; i < ctx.frames; i++) {
            if (mr) inc = rate_inc(rate_ + scaled(mr[i], rmod_));
            int32_t s;
            switch (shape_) {
                case LFOS_SINE: s = sine(ph_); break;
                case LFOS_TRI: s = ph_ < 0x80000000u ? 32767 - static_cast<int32_t>(ph_ >> 15) : static_cast<int32_t>(ph_ >> 15) - 98303; break;
                case LFOS_SAW_DOWN: s = 32767 - static_cast<int32_t>(ph_ >> 16); break;
                case LFOS_SAW_UP: s = static_cast<int32_t>(ph_ >> 16) - 32768; break;
                case LFOS_SQUARE: s = ph_ < 0x80000000u ? 32767 : -32768; break;
                default: s = held_; break;
            }
            uint32_t prev = ph_;
            ph_ += inc;
            if (shape_ == LFOS_SH && ph_ < prev) held_ = static_cast<q15>(noise_.next());
            if (uni_) s = (s + 32768) >> 1;
            p.out[0][i] = mul15(sat16(s), level_);
        }
    }
private:
    uint32_t rate_inc(int32_t r) const { return exp2_scale(one_hz_, clamp_i32(r, -12 * 12 * kSemi, 12 * 12 * kSemi) * 256 / 12); }
    int32_t shape_ = LFOS_SINE, rate_ = 2 * 12 * kSemi, rmod_ = 2 * 12 * kSemi;
    q15 level_ = kUnity, held_ = 0;
    bool uni_ = false;
    uint32_t ph_ = 0, one_hz_ = 0;
    Noise noise_{0xBEEF1u};
};

/* ------------------------------------------------------------------ Filter */

template <Scope S>
class Filter : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Filter", S, 1, 1, FLT_N, false, {"in"}, {"out"},
            {{"mode", FLTM_LP, 0, 3}, {"sections", 2, 1, 4}, {"cutoff", 96 * kSemi, 0, 135 * kSemi}, {"res", 0, 0, kUnity},
             {"cut_mod", 60 * kSemi, 0, 120 * kSemi}}};
        return i;
    }
    bool init(Memory &) override { a4_ = inc_a4(); update_k(); return true; }
    void reset() override { for (auto &s : st_) s = SvfState{}; cut_s_ = cutoff_; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case FLT_MODE: mode_ = v; break;
            case FLT_SECTIONS: n_ = clamp_i32(v, 1, 4); update_k(); break;
            case FLT_CUTOFF: cutoff_ = v; break;
            case FLT_RES: res_ = v; update_k(); break;
            case FLT_CUT_MOD: cmod_ = v; break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const q15 *mc = p.mod[FLT_CUTOFF];
        cut_s_ += (cutoff_ - cut_s_) / 4;                        // control-rate smoothing of knob moves
        if (cut_s_ != cutoff_ && (cutoff_ - cut_s_) < 4 && (cut_s_ - cutoff_) < 4) cut_s_ = cutoff_;
        SvfCoef c[4];
        if (!mc) coefs(c, pitch_to_inc(cut_s_, a4_));
        for (int i = 0; i < ctx.frames; i++) {
            if (mc) coefs(c, pitch_to_inc(cut_s_ + scaled(mc[i], cmod_), a4_));
            int32_t x = svf_in(p.in[0][i]);
            for (int s = 0; s < n_; s++) {
                int32_t lp, bp, hp;
                svf_tick(c[s], st_[s], x, lp, bp, hp);
                switch (mode_) {
                    case FLTM_LP: x = lp; break;
                    case FLTM_BP: x = sat_q28((static_cast<int64_t>(c[s].k) * bp) >> 29); break;     // normalised: 0 dB at the centre
                    case FLTM_HP: x = hp; break;
                    default: x = sat_q28(static_cast<int64_t>(lp) + hp); break;
                }
            }
            p.out[0][i] = svf_out(x);
        }
    }
private:
    void coefs(SvfCoef *c, uint32_t inc) const {
        uint32_t g = svf_g(inc);
        for (int s = 0; s < n_; s++) c[s] = svf_coef(g, k_[s]);
    }
    void update_k() {
        static const float q[4][4] = {{0.70711f}, {0.54120f, 1.30656f}, {0.51764f, 0.70711f, 1.93185f}, {0.50979f, 0.60134f, 0.89998f, 2.56292f}};
        for (int s = 0; s < n_; s++) {
            float qs = q[n_ - 1][s];
            if (s == n_ - 1) qs *= 1.0f + 15.0f * static_cast<float>(res_) / 32768.0f;
            k_[s] = static_cast<int32_t>(536870912.0f / qs);        // 1/Q in Q29
        }
    }
    int32_t mode_ = FLTM_LP, n_ = 2, cutoff_ = 96 * kSemi, cut_s_ = 96 * kSemi, cmod_ = 60 * kSemi, res_ = 0;
    int32_t k_[4] = {};
    SvfState st_[4];
    uint32_t a4_ = 0;
};

/* ------------------------------------------------------------------ Shaper (naive, ADR-013) */

template <Scope S>
class Shaper : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Shaper", S, 1, 1, SHP_N, false, {"in"}, {"out"},
            {{"mode", SHPM_TANH, 0, SHPM_N - 1}, {"drive", 256, 0, 4 * 256}, {"mix", kUnity, 0, kUnity}, {"bits", 8, 1, 15}, {"drive_mod", 2 * 256, 0, 6 * 256}}};
        return i;
    }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case SHP_MODE: mode_ = v; break;
            case SHP_DRIVE: drive_ = v; break;
            case SHP_MIX: mix_ = static_cast<q15>(v); break;
            case SHP_BITS: bits_ = clamp_i32(v, 1, 15); break;
            case SHP_DRIVE_MOD: dmod_ = v; break;
        }
    }
    bool init(Memory &) override { dc_.set_corner(15.0f); return true; }
    void reset() override { dc_.reset(); lp_ = 0; hold_ = 0; cnt_ = 0; }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const q15 *md = p.mod[SHP_DRIVE];
        int32_t gain = gain_q8(drive_);
        for (int i = 0; i < ctx.frames; i++) {
            if (md) gain = gain_q8(drive_ + scaled(md[i], dmod_));
            const q15 x = p.in[0][i];
            const int32_t v = x * gain;                              // Q23 (q15 * Q8)
            q15 y;
            switch (mode_) {
                case SHPM_TANH: y = tanh_q23(v); break;
                case SHPM_CLIP: y = sat16(v >> 8); break;
                case SHPM_FOLD: y = sine(static_cast<uint32_t>(v) << 7); break;
                case SHPM_TUBE: y = dc_.process(sub15(tanh_q23(v + 2516582), 9546)); break;            // bias 0.3: tanh(0.3) = 0.2913 = 9546 (+ trim)
                case SHPM_TAPE: { const q15 t = tanh_q23(v); lp_ += ((static_cast<int32_t>(t) - lp_) * 16000) >> 15; y = sat16(lp_); break; }
                case SHPM_DIODE: y = dc_.process(v >= 0 ? tanh_q23(v) : static_cast<q15>((tanh_q23((v * 7) >> 4) * 19660) >> 15)); break;
                case SHPM_CHEB: {
                    const int32_t c = sat16(v >> 8);                                                    // clamp to +-1
                    const int32_t c2 = (c * c) >> 15, c3 = (c2 * c) >> 15, c5 = (((c3 * c) >> 15) * c) >> 15;
                    y = sat16(((4 * c3 - 3 * c) + (16 * c5 - 20 * c3 + 5 * c)) >> 1);                   // (T3 + T5) / 2
                    break;
                }
                case SHPM_RECT: { const int32_t c = sat16(v >> 8); y = dc_.process(sat16(c < 0 ? -2 * c : 2 * c)); break; }
                default: {                                                                              // decimate + crush
                    const int32_t every = gain >> 8 < 1 ? 1 : (gain >> 8 > 16 ? 16 : gain >> 8);
                    if (++cnt_ >= every) { cnt_ = 0; hold_ = x; }
                    const int sh = 16 - bits_;
                    y = sat16(((hold_ + (1 << (sh - 1))) >> sh) << sh);
                    break;
                }
            }
            p.out[0][i] = mix_ == kUnity ? y : static_cast<q15>(x + (((static_cast<int32_t>(y) - x) * mix_) >> 15));
        }
    }
private:
    static q15 tanh_q23(int32_t v) {
        const int32_t c = clamp_i32(v, -(4 << 23) + 1, (4 << 23) - 1);
        const uint32_t u = static_cast<uint32_t>(c + (4 << 23));
        return lerp15(kTanhTab[u >> 16], kTanhTab[(u >> 16) + 1], u & 0xFFFFu);
    }
    static int32_t gain_q8(int32_t drive) { return static_cast<int32_t>(exp2_scale(256u, clamp_i32(drive, 0, 6 * 256) * 256)); }
    int32_t mode_ = SHPM_TANH, drive_ = 256, bits_ = 8, dmod_ = 2 * 256, lp_ = 0, cnt_ = 0;
    q15 mix_ = kUnity, hold_ = 0;
    DcBlocker dc_;
};

/* ------------------------------------------------------------------ small utilities */

template <Scope S>
class Const : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Const", S, 0, 1, 1, false, {}, {"out"}, {{"value", 0, -32768, 32767}}};
        return i;
    }
    void set_param(int, int32_t v) override { v_ = static_cast<q15>(v); }
    void process(const ProcessCtx &ctx, const Ports &p) override { for (int i = 0; i < ctx.frames; i++) p.out[0][i] = v_; }
private:
    q15 v_ = 0;
};

template <Scope S>
class Vca : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Vca", S, 1, 1, 1, false, {"in"}, {"out"}, {{"level", kUnity, -32768, kUnity}}};
        return i;
    }
    void set_param(int, int32_t v) override { g_ = static_cast<q15>(v); }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const q15 *m = p.mod[VCA_LEVEL];
        if (!m) { block_gain(p.out[0], p.in[0], g_, ctx.frames); return; }
        for (int i = 0; i < ctx.frames; i++) p.out[0][i] = mul15(p.in[0][i], sat16(g_ + m[i]));
    }
private:
    q15 g_ = kUnity;
};

template <Scope S>
class Mult : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Mult", S, 2, 1, 0, false, {"a", "b"}, {"out"}, {}};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override { block_mul(p.out[0], p.in[0], p.in[1], ctx.frames); }
};

template <Scope S>
class Mix4 : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Mix4", S, 4, 1, 4, false, {"1", "2", "3", "4"}, {"out"},
            {{"lvl1", 8192, -32768, kUnity}, {"lvl2", 8192, -32768, kUnity}, {"lvl3", 8192, -32768, kUnity}, {"lvl4", 8192, -32768, kUnity}}};
        return i;
    }
    void set_param(int idx, int32_t v) override { if (idx >= 0 && idx < 4) g_[idx] = static_cast<q15>(v); }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const q15 *mod[4] = {p.mod[0], p.mod[1], p.mod[2], p.mod[3]};
        for (int i = 0; i < ctx.frames; i++) {
            int32_t acc = 0;
            for (int k = 0; k < 4; k++) acc += (p.in[k][i] * (mod[k] ? sat16(g_[k] + mod[k][i]) : g_[k]) + (1 << 14)) >> 15;
            p.out[0][i] = sat16(acc);
        }
    }
private:
    q15 g_[4] = {8192, 8192, 8192, 8192};
};

template <typename T>
ModuleType type_of() { static T probe; return {&probe.info(), &create_module<T>}; }

}  // namespace

void register_synth_modules(Registry &r) {
    r.add(T_OSC, type_of<Osc<Scope::Voice>>());
    r.add(T_OSC_G, type_of<Osc<Scope::Global>>());
    r.add(T_ENV, type_of<Env>());
    r.add(T_EG, type_of<Eg>());
    r.add(T_LFO_V, type_of<Lfo<Scope::Voice>>());
    r.add(T_LFO_G, type_of<Lfo<Scope::Global>>());
    r.add(T_FILTER_V, type_of<Filter<Scope::Voice>>());
    r.add(T_FILTER_G, type_of<Filter<Scope::Global>>());
    r.add(T_VCA_V, type_of<Vca<Scope::Voice>>());
    r.add(T_VCA_G, type_of<Vca<Scope::Global>>());
    r.add(T_MIX4_V, type_of<Mix4<Scope::Voice>>());
    r.add(T_MIX4_G, type_of<Mix4<Scope::Global>>());
    r.add(T_MULT_V, type_of<Mult<Scope::Voice>>());
    r.add(T_MULT_G, type_of<Mult<Scope::Global>>());
    r.add(T_SHAPER_V, type_of<Shaper<Scope::Voice>>());
    r.add(T_SHAPER_G, type_of<Shaper<Scope::Global>>());
    r.add(T_CONST_V, type_of<Const<Scope::Voice>>());
    r.add(T_CONST_G, type_of<Const<Scope::Global>>());
}

}  // namespace sc
