#include "engine/modules/fx2_modules.h"
#include <cmath>
#include <cstring>
#include "engine/dsp/block.h"
#include "engine/dsp/delay.h"
#include "engine/dsp/interp.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/svf.h"
#include "engine/dsp/util.h"
#include "engine/modules/builtin.h"

namespace sc {
namespace {

constexpr int32_t kSemi = 256;
constexpr double kPi = 3.14159265358979323846;
inline int32_t scaled(q15 m, int32_t range) { return static_cast<int32_t>((static_cast<int64_t>(m) * range) >> 15); }
inline int32_t tri_q15(uint32_t ph) { return ph < 0x80000000u ? 32767 - static_cast<int32_t>(ph >> 15) : static_cast<int32_t>(ph >> 15) - 98303; }   // -1..+1
inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// phase increment of an LFO rate given in pitch units relative to 1 Hz
inline uint32_t lfo_inc(int32_t rate, uint32_t one_hz) { return exp2_scale(one_hz, clampi(rate, -6 * 12 * kSemi, 8 * 12 * kSemi) * 256 / 12); }

template <typename T>
ModuleType type_of() { static T probe; return {&probe.info(), &create_module<T>}; }

/* ------------------------------------------------------------------ Phaser */

class Phaser : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Phaser", Scope::Global, 2, 2, PHS_N, false, {"L", "R"}, {"L", "R"},
            {{"rate", 12 * kSemi * (-1), -6 * 12 * kSemi, 8 * 12 * kSemi}, {"depth", 20000, 0, kUnity}, {"center", 79 * kSemi, 36 * kSemi, 110 * kSemi},
             {"feedback", 16000, -31000, 31000}, {"stages", 3, 1, 4}, {"mix", 16384, 0, kUnity}}};
        return i;
    }
    bool init(Memory &) override { a4_ = inc_a4(); one_hz_ = hz_to_inc(1.0f); return true; }
    void reset() override { std::memset(zl_, 0, sizeof zl_); std::memset(zr_, 0, sizeof zr_); fl_ = fr_ = 0; ph_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case PHS_RATE: rate_ = v; break;
            case PHS_DEPTH: depth_ = static_cast<q15>(v); break;
            case PHS_CENTER: center_ = v; break;
            case PHS_FEEDBACK: fb_ = static_cast<q15>(v); break;
            case PHS_STAGES: stages_ = clampi(v, 1, 4) * 2; break;
            case PHS_MIX: mix_ = static_cast<q15>(v); break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const uint32_t inc = lfo_inc(rate_, one_hz_);
        const int n = ctx.frames;
        const int32_t tl = tri_q15(ph_), tr = tri_q15(ph_ + 0x40000000u);
        const int32_t sweep = 4 * 12 * kSemi;
        const int64_t al = coef(center_ + scaled(static_cast<q15>(tl), scaled(depth_, sweep))), ar = coef(center_ + scaled(static_cast<q15>(tr), scaled(depth_, sweep)));
        for (int i = 0; i < n; i++) {
            p.out[0][i] = run(p.in[0][i], al, zl_, fl_);
            p.out[1][i] = run(p.in[1][i], ar, zr_, fr_);
        }
        ph_ += inc * static_cast<uint32_t>(n);
    }
private:
    // first-order all-pass H = (a + z^-1) / (1 + a z^-1) with its 90 degree point at f: a = (g - 1) / (g + 1), g = tan(pi f / fs) (Q28); returned as Q30
    int64_t coef(int32_t pitch) const {
        const int64_t g = svf_g(pitch_to_inc(pitch, a4_));
        return ((g - (1ll << 28)) * (1ll << 30)) / ((1ll << 28) + g);
    }
    q15 run(q15 x, int64_t a, int32_t *z, int32_t &fbstate) const {
        int32_t v = (static_cast<int32_t>(x) + static_cast<int32_t>((static_cast<int64_t>(fbstate) * fb_) >> 15)) * 256;        // Q23, with the feedback of the last stage
        v = clampi(v, -(1 << 30), (1 << 30));
        for (int s = 0; s < stages_; s++) {
            const int32_t y = static_cast<int32_t>((a * v) >> 30) + z[s];
            z[s] = v - static_cast<int32_t>((a * y) >> 30);
            v = y;
        }
        fbstate = v >> 8;
        const int32_t wet = v >> 8;
        const int32_t dry = x;
        return sat16(dry + (((wet - dry) * mix_) >> 15));
    }
    int32_t rate_ = -12 * kSemi, center_ = 79 * kSemi, stages_ = 6;
    q15 depth_ = 20000, fb_ = 16000, mix_ = 16384;
    uint32_t a4_ = 0, one_hz_ = 0, ph_ = 0;
    mutable int32_t zl_[8] = {}, zr_[8] = {};
    mutable int32_t fl_ = 0, fr_ = 0;
};

/* ------------------------------------------------------------------ Flanger */

class Flanger : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Flanger", Scope::Global, 2, 2, FLG_N, false, {"L", "R"}, {"L", "R"},
            {{"rate", -12 * kSemi, -6 * 12 * kSemi, 8 * 12 * kSemi}, {"depth", 20000, 0, kUnity}, {"delay", 32, 8, 160},
             {"feedback", 16000, -31000, 31000}, {"mix", 20000, 0, kUnity}}};
        return i;
    }
    bool init(Memory &m) override {
        one_hz_ = hz_to_inc(1.0f);
        const int room = static_cast<int>(0.022 * kSampleRate) + 8;
        return l_.init(*m.fast, room) && r_.init(*m.fast, room);
    }
    void reset() override { l_.clear(); r_.clear(); ph_ = 0; fl_ = fr_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case FLG_RATE: rate_ = v; break;
            case FLG_DEPTH: depth_ = static_cast<q15>(v); break;
            case FLG_DELAY: delay_ = v; break;
            case FLG_FEEDBACK: fb_ = static_cast<q15>(v); break;
            case FLG_MIX: mix_ = static_cast<q15>(v); break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const uint32_t inc = lfo_inc(rate_, one_hz_);
        const int64_t centre = (static_cast<int64_t>(delay_) * kSampleRate / 16000) << 16;             // Q16 samples
        for (int i = 0; i < ctx.frames; i++) {
            ph_ += inc;
            const int64_t dl = centre + ((centre * depth_ >> 15) * tri_q15(ph_) >> 15), dr = centre + ((centre * depth_ >> 15) * tri_q15(ph_ + 0x40000000u) >> 15);
            const uint32_t d_l = static_cast<uint32_t>(dl < (3 << 16) ? (3 << 16) : dl), d_r = static_cast<uint32_t>(dr < (3 << 16) ? (3 << 16) : dr);
            const q15 inl = p.in[0][i], inr = p.in[1][i];
            const q15 wl = l_.read_hermite(d_l), wr = r_.read_hermite(d_r);
            l_.write(sat16(inl + mul15(wl, fb_)));
            r_.write(sat16(inr + mul15(wr, fb_)));
            p.out[0][i] = sat16(inl + mul15(wl, mix_));
            p.out[1][i] = sat16(inr + mul15(wr, mix_));
        }
    }
private:
    DelayLine l_, r_;
    int32_t rate_ = -12 * kSemi, delay_ = 32, fl_ = 0, fr_ = 0;
    q15 depth_ = 20000, fb_ = 16000, mix_ = 20000;
    uint32_t one_hz_ = 0, ph_ = 0;
};

/* ------------------------------------------------------------------ Tremolo / auto-pan */

class Tremolo : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Tremolo", Scope::Global, 2, 2, TRM_N, false, {"L", "R"}, {"L", "R"},
            {{"rate", 2 * 12 * kSemi + 3 * kSemi, -6 * 12 * kSemi, 8 * 12 * kSemi}, {"depth", 20000, 0, kUnity}, {"shape", 0, 0, 2}, {"mode", 0, 0, 1}}};
        return i;
    }
    bool init(Memory &) override { one_hz_ = hz_to_inc(1.0f); return true; }
    void reset() override { ph_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case TRM_RATE: rate_ = v; break;
            case TRM_DEPTH: depth_ = static_cast<q15>(v); break;
            case TRM_SHAPE: shape_ = v; break;
            case TRM_MODE: mode_ = v; break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const uint32_t inc = lfo_inc(rate_, one_hz_);
        for (int i = 0; i < ctx.frames; i++) {
            ph_ += inc;
            int32_t l;                                                                          // -1..+1
            switch (shape_) {
                case 0: l = sine(ph_); break;
                case 1: l = tri_q15(ph_); break;
                default: l = clampi(sine(ph_) * 4, -32767, 32767); break;
            }
            const int32_t up = (l + 32768) >> 1;                                                // 0..1
            const int32_t gl = 32767 - ((up * depth_) >> 15);
            const int32_t gr = mode_ ? 32767 - (((32767 - up) * depth_) >> 15) : gl;
            p.out[0][i] = mul15(p.in[0][i], static_cast<q15>(gl));
            p.out[1][i] = mul15(p.in[1][i], static_cast<q15>(gr));
        }
    }
private:
    int32_t rate_ = 2 * 12 * kSemi + 3 * kSemi, shape_ = 0, mode_ = 0;
    q15 depth_ = 20000;
    uint32_t one_hz_ = 0, ph_ = 0;
};

/* ------------------------------------------------------------------ Compressor */

// log2 of an unsigned value in Q16 (0 -> very small). 256-entry mantissa table.
class Log2Table {
public:
    Log2Table() { for (int i = 0; i <= 256; i++) t_[i] = static_cast<int32_t>(std::lround(std::log2(1.0 + i / 256.0) * 65536.0)); }
    int32_t at(uint32_t x) const {
        if (x == 0) return -(32 << 16);
        const int n = 31 - __builtin_clz(x);
        const uint32_t m = n >= 8 ? (x >> (n - 8)) & 255u : (x << (8 - n)) & 255u;
        return (n << 16) + t_[m];
    }
private:
    int32_t t_[257];
};

class Compressor : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Compressor", Scope::Global, 2, 2, CMP_N, false, {"L", "R"}, {"L", "R"},
            {{"threshold", -18, -60, 0}, {"ratio", 40, 10, 200}, {"attack", 8, 1, 200}, {"release", 150, 10, 2000}, {"makeup", 6, 0, 24}, {"mix", kUnity, 0, kUnity}}};
        return i;
    }
    bool init(Memory &) override { update(); return true; }
    void reset() override { env_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case CMP_THRESH: thr_db_ = v; break;
            case CMP_RATIO: ratio10_ = v; break;
            case CMP_ATTACK: att_ms_ = v; break;
            case CMP_RELEASE: rel_ms_ = v; break;
            case CMP_MAKEUP: mk_db_ = v; break;
            case CMP_MIX: mix_ = static_cast<q15>(v); break;
        }
        update();
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        for (int i = 0; i < ctx.frames; i++) {
            const q15 l = p.in[0][i], r = p.in[1][i];
            const int32_t a = (l < 0 ? -static_cast<int32_t>(l) : l), b = (r < 0 ? -static_cast<int32_t>(r) : r);
            const int64_t x = static_cast<int64_t>(a > b ? a : b) << 16;                       // Q31 peak
            env_ += ((x - env_) * (x > env_ ? ka_ : kr_)) >> 31;
            const int32_t lv = lg_.at(static_cast<uint32_t>(env_));                            // Q16 octaves, 2^31 = 31
            int32_t gain = 0x7FFFFFFF;
            const int32_t over = lv - thr_;
            if (over > 0) gain = static_cast<int32_t>(exp2_scale(0x7FFFFFFFu, -static_cast<int32_t>((static_cast<int64_t>(over) * slope_) >> 16)));
            const int64_t g = (static_cast<int64_t>(gain) * makeup_) >> 12;                    // Q31 * Q12 -> Q31 (makeup 1.0 = 4096)
            const int32_t cl = sat16(static_cast<int32_t>((l * g) >> 31)), cr = sat16(static_cast<int32_t>((r * g) >> 31));
            p.out[0][i] = sat16(l + (((cl - l) * mix_) >> 15));
            p.out[1][i] = sat16(r + (((cr - r) * mix_) >> 15));
        }
    }
private:
    void update() {
        thr_ = static_cast<int32_t>(std::lround((31.0 + thr_db_ / 6.0206) * 65536.0));
        const double ratio = ratio10_ / 10.0;
        slope_ = static_cast<int32_t>(std::lround((1.0 - 1.0 / ratio) * 65536.0));
        auto k = [](double ms) { return static_cast<int64_t>((1.0 - std::exp(-1.0 / (ms * 0.001 * kSampleRate))) * 2147483647.0); };
        ka_ = k(att_ms_ < 1 ? 1 : att_ms_); kr_ = k(rel_ms_ < 1 ? 1 : rel_ms_);
        makeup_ = static_cast<int32_t>(std::lround(std::pow(10.0, mk_db_ / 20.0) * 4096.0));
    }
    Log2Table lg_;
    int64_t env_ = 0, ka_ = 0, kr_ = 0;
    int32_t thr_db_ = -18, ratio10_ = 40, att_ms_ = 8, rel_ms_ = 150, mk_db_ = 6, thr_ = 0, slope_ = 0, makeup_ = 4096;
    q15 mix_ = kUnity;
};

/* ------------------------------------------------------------------ EQ3 */

struct Biquad {                                                              // transposed direct form II, Q32 coefficients, int64 states
    int64_t b0 = 1ll << 32, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    // The feedback uses the output with 12 extra fractional bits: rounding it to q15 inside the loop is amplified by the 1 / A(z) gain of
    // poles close to z = 1 (a 150 Hz shelf at 96 kHz) and ruins the gain at low frequencies.
    q15 tick(q15 x) {
        const int64_t y27 = (b0 * x + z1) >> 20;                                  // Q15 + 12 bits
        z1 = b1 * x - ((a1 * y27) >> 12) + z2;
        z2 = b2 * x - ((a2 * y27) >> 12);
        return sat16(static_cast<int32_t>((y27 + 2048) >> 12));
    }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) {
        auto q = [&](double v) { return static_cast<int64_t>(std::llround(v / A0 * 4294967296.0)); };
        b0 = q(B0); b1 = q(B1); b2 = q(B2); a1 = q(A1); a2 = q(A2);
    }
    // RBJ cookbook shapes
    void peak(double f, double gain_db, double Q) {
        const double A = std::pow(10.0, gain_db / 40.0), w = 2 * kPi * f / kSampleRate, al = std::sin(w) / (2 * Q), c = std::cos(w);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void low_shelf(double f, double gain_db) {
        const double A = std::pow(10.0, gain_db / 40.0), w = 2 * kPi * f / kSampleRate, c = std::cos(w), al = std::sin(w) / 2 * std::sqrt(2.0), sA = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
            (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    void high_shelf(double f, double gain_db) {
        const double A = std::pow(10.0, gain_db / 40.0), w = 2 * kPi * f / kSampleRate, c = std::cos(w), al = std::sin(w) / 2 * std::sqrt(2.0), sA = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
            (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    void reset() { z1 = z2 = 0; }
};

class Eq3 : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Eq3", Scope::Global, 2, 2, EQ_N, false, {"L", "R"}, {"L", "R"},
            {{"low", 0, -150, 150}, {"mid", 0, -150, 150}, {"midf", 84 * kSemi, 36 * kSemi, 120 * kSemi}, {"high", 0, -150, 150}}};
        return i;
    }
    bool init(Memory &) override { update(); return true; }
    void reset() override { for (auto &b : bq_) b.reset(); }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case EQ_LOW: low_ = v; break;
            case EQ_MID: mid_ = v; break;
            case EQ_MIDF: midf_ = v; break;
            case EQ_HIGH: high_ = v; break;
        }
        update();
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        for (int i = 0; i < ctx.frames; i++) {
            q15 l = p.in[0][i], r = p.in[1][i];
            for (int s = 0; s < 3; s++) { l = bq_[s].tick(l); r = bq_[3 + s].tick(r); }
            p.out[0][i] = l;
            p.out[1][i] = r;
        }
    }
private:
    void update() {
        const double fm = 440.0 * std::pow(2.0, (midf_ / 256.0 - 69.0) / 12.0);
        for (int c = 0; c < 2; c++) {
            bq_[3 * c].low_shelf(150.0, low_ / 10.0);
            bq_[3 * c + 1].peak(fm, mid_ / 10.0, 0.9);
            bq_[3 * c + 2].high_shelf(6000.0, high_ / 10.0);
        }
    }
    Biquad bq_[6];
    int32_t low_ = 0, mid_ = 0, midf_ = 84 * kSemi, high_ = 0;
};

/* ------------------------------------------------------------------ Ring modulator / frequency shifter */

class Shifter : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Shifter", Scope::Global, 2, 2, SFT_N, false, {"L", "R"}, {"L", "R"},
            {{"mode", 0, 0, 2}, {"freq", 12 * kSemi * 7 + 5 * kSemi, -6 * 12 * kSemi, 14 * 12 * kSemi}, {"mix", 16384, 0, kUnity}}};
        return i;
    }
    bool init(Memory &) override {
        one_hz_ = hz_to_inc(1.0f);
        static const double a1[4] = {0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737};
        static const double a2[4] = {0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278};
        for (int k = 0; k < 4; k++) {
            c1_[k] = static_cast<int64_t>(std::llround(a1[k] * a1[k] * 1073741824.0));
            c2_[k] = static_cast<int64_t>(std::llround(a2[k] * a2[k] * 1073741824.0));
        }
        return true;
    }
    void reset() override { std::memset(st_, 0, sizeof st_); ph_ = 0; d1_[0] = d1_[1] = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case SFT_MODE: mode_ = v; break;
            case SFT_FREQ: freq_ = v; break;
            case SFT_MIX: mix_ = static_cast<q15>(v); break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const uint32_t inc = exp2_scale(one_hz_, clampi(freq_, -6 * 12 * kSemi, 14 * 12 * kSemi) * 256 / 12);
        for (int i = 0; i < ctx.frames; i++) {
            ph_ += inc;
            const int32_t c = sine(ph_ + 0x40000000u), s = sine(ph_);
            for (int ch = 0; ch < 2; ch++) {
                const q15 x = p.in[ch][i];
                int32_t y;
                if (mode_ == 0) {
                    y = (static_cast<int32_t>(x) * c) >> 15;
                } else {
                    const int32_t re = chain(c1_, st_[ch][0], x), im = chain(c2_, st_[ch][1], d1_[ch]);   // quadrature pair (the second path sees the input one sample late)
                    d1_[ch] = x;
                    y = mode_ == 1 ? ((re * c + im * s) >> 15) : ((re * c - im * s) >> 15);
                }
                p.out[ch][i] = sat16(x + (((y - x) * mix_) >> 15));
            }
        }
    }
private:
    // four second-order all-pass sections in z^-2: y[n] = k (x[n] + y[n-2]) - x[n-2]
    static int32_t chain(const int64_t *k, int32_t (*st)[4], int32_t x) {
        int32_t v = x << 4;
        for (int s = 0; s < 4; s++) {
            const int32_t y = static_cast<int32_t>((k[s] * (static_cast<int64_t>(v) + st[s][3]) >> 30) - st[s][1]);
            st[s][1] = st[s][0]; st[s][0] = v;
            st[s][3] = st[s][2]; st[s][2] = y;
            v = y;
        }
        return v >> 4;
    }
    int64_t c1_[4] = {}, c2_[4] = {};
    int32_t st_[2][2][4][4] = {};       // [channel][path][section][x1 x2? see chain: {x[n-1], x[n-2], y[n-1], y[n-2]}]
    int32_t d1_[2] = {};
    int32_t mode_ = 0, freq_ = 12 * kSemi * 7 + 5 * kSemi;
    q15 mix_ = 16384;
    uint32_t one_hz_ = 0, ph_ = 0;
};

/* ------------------------------------------------------------------ Convolver */

// Impulse-response generators (control time, doubles): cascades of RBJ biquads run on an impulse, and sums of damped resonances for bodies.
struct DBq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double tick(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; z1 = z2 = 0; }
    static double w(double f) { return 2 * kPi * f / kSampleRate; }
    void lp(double f, double Q) { const double w0 = w(f), al = std::sin(w0) / (2 * Q), c = std::cos(w0); set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void hp(double f, double Q) { const double w0 = w(f), al = std::sin(w0) / (2 * Q), c = std::cos(w0); set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    void bp(double f, double Q) { const double w0 = w(f), al = std::sin(w0) / (2 * Q), c = std::cos(w0); set(al, 0, -al, 1 + al, -2 * c, 1 - al); }
    void pk(double f, double db, double Q) { const double A = std::pow(10.0, db / 40.0), w0 = w(f), al = std::sin(w0) / (2 * Q), c = std::cos(w0); set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
};

void make_ir(int which, int taps, double *out) {
    for (int i = 0; i < taps; i++) out[i] = 0;
    auto cascade = [&](DBq *f, int n) {
        for (int i = 0; i < taps; i++) { double x = i == 0 ? 1.0 : 0.0; for (int k = 0; k < n; k++) x = f[k].tick(x); out[i] += x; }
    };
    auto resonators = [&](const double (*m)[3], int n, double direct) {          // {freq, Q, gain}
        out[0] += direct;
        for (int k = 0; k < n; k++) {
            DBq f; f.bp(m[k][0], m[k][1]);
            for (int i = 0; i < taps; i++) out[i] += m[k][2] * f.tick(i == 0 ? 1.0 : 0.0);
        }
    };
    switch (which) {
        case CNVIR_CAB_1X12: { DBq f[5]; f[0].hp(85, 0.7); f[1].pk(115, 5, 1.2); f[2].pk(2300, 6, 1.1); f[3].lp(5200, 0.8); f[4].lp(6500, 0.7); cascade(f, 5); } break;
        case CNVIR_CAB_4X12: { DBq f[6]; f[0].hp(70, 0.7); f[1].pk(95, 6, 1.1); f[2].pk(420, -4, 1.0); f[3].pk(2000, 5, 1.0); f[4].lp(4400, 0.9); f[5].lp(5200, 0.8); cascade(f, 6); } break;
        case CNVIR_CAB_BRIGHT: { DBq f[4]; f[0].hp(130, 0.7); f[1].pk(3500, 5, 1.0); f[2].lp(7200, 0.7); f[3].pk(800, -2, 0.9); cascade(f, 4); } break;
        case CNVIR_CAB_DARK: { DBq f[4]; f[0].hp(95, 0.7); f[1].pk(700, 4, 0.9); f[2].lp(3000, 0.7); f[3].lp(3600, 0.7); cascade(f, 4); } break;
        case CNVIR_ACOUSTIC: { static const double m[5][3] = {{100, 7, 1.4}, {205, 9, 1.1}, {410, 10, 0.8}, {1200, 8, 0.55}, {2900, 6, 0.35}}; resonators(m, 5, 0.35); } break;
        case CNVIR_VIOLIN: { static const double m[7][3] = {{290, 14, 1.2}, {460, 16, 1.0}, {540, 18, 0.9}, {750, 16, 0.7}, {1100, 14, 0.5}, {2800, 9, 0.45}, {3600, 8, 0.3}}; resonators(m, 7, 0.3); } break;
        case CNVIR_DRUM: { static const double m[3][3] = {{185, 12, 1.5}, {330, 14, 0.8}, {620, 12, 0.5}}; resonators(m, 3, 0.4); } break;
        default: { DBq f[3]; f[0].hp(400, 0.9); f[1].lp(3400, 0.9); f[2].pk(1500, 5, 1.2); cascade(f, 3); } break;
    }
    double e = 0;
    for (int i = 0; i < taps; i++) e += out[i] * out[i];
    const double norm = e > 1e-12 ? 1.0 / std::sqrt(e) : 1.0;
    for (int i = 0; i < taps; i++) out[i] *= norm;
}

class Convolver : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Convolver", Scope::Global, 2, 2, CNV_N, false, {"L", "R"}, {"L", "R"},
            {{"ir", CNVIR_CAB_1X12, 0, CNVIR_COUNT - 1}, {"length", 256, 16, kConvMaxTaps}, {"mix", kUnity, 0, kUnity}, {"level", 16384, 0, kUnity}}};
        return i;
    }
    bool init(Memory &) override { load(CNVIR_CAB_1X12); return true; }
    void reset() override { std::memset(hl_, 0, sizeof hl_); std::memset(hr_, 0, sizeof hr_); w_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case CNV_IR: if (v != ir_) load(v); break;
            case CNV_LENGTH: len_ = clampi(v, 16, kConvMaxTaps); if (ir_ != CNVIR_CUSTOM) load(ir_, true); break;
            case CNV_MIX: mix_ = static_cast<q15>(v); break;
            case CNV_LEVEL: level_ = static_cast<q15>(v); break;
        }
    }
    void set_blob(const void *data, size_t bytes) override {
        if (bytes < 4) return;
        const ConvBlob *b = static_cast<const ConvBlob *>(data);
        if (b->start >= kConvMaxTaps) return;
        if (b->start == 0 && ir_ != CNVIR_CUSTOM) { ir_ = CNVIR_CUSTOM; std::memset(tap_, 0, sizeof tap_); }
        for (int i = 0; i < b->count && b->start + i < kConvMaxTaps && i < kConvChunk; i++) tap_[b->start + i] = b->tap[i];
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const int n = len_;
        for (int i = 0; i < ctx.frames; i++) {
            const q15 l = p.in[0][i], r = p.in[1][i];
            hl_[w_] = l; hr_[w_] = r;
            int64_t sl = 0, sr = 0;
            for (int k = 0; k < n; k++) {
                const int idx = (w_ - k) & (kHist - 1);
                sl += static_cast<int32_t>(tap_[k]) * hl_[idx];
                sr += static_cast<int32_t>(tap_[k]) * hr_[idx];
            }
            w_ = (w_ + 1) & (kHist - 1);
            const int32_t cl = static_cast<int32_t>((sl >> 15) * level_ >> 14), cr = static_cast<int32_t>((sr >> 15) * level_ >> 14);   // level 0.5 = x1
            p.out[0][i] = sat16(l + (((sat16(cl) - l) * mix_) >> 15));
            p.out[1][i] = sat16(r + (((sat16(cr) - r) * mix_) >> 15));
        }
    }
private:
    static constexpr int kHist = 1024;
    void load(int which, bool length_only = false) {
        (void)length_only;
        ir_ = clampi(which, 0, CNVIR_CUSTOM);
        if (ir_ == CNVIR_CUSTOM) return;
        double d[kConvMaxTaps];
        make_ir(ir_, len_, d);
        for (int i = 0; i < kConvMaxTaps; i++) tap_[i] = i < len_ ? static_cast<int16_t>(std::lround(std::fmax(-1.0, std::fmin(1.0, d[i])) * 32767.0)) : 0;
    }
    int16_t tap_[kConvMaxTaps] = {};
    q15 hl_[kHist] = {}, hr_[kHist] = {};
    int w_ = 0, ir_ = -1, len_ = 256;
    q15 mix_ = kUnity, level_ = 16384;
};

/* ------------------------------------------------------------------ Comb (tuned resonator, voice scope) */

class TunedComb : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"TunedComb", Scope::Voice, 2, 1, CMB_N, false, {"in", "pitch"}, {"out"},
            {{"tune", 60 * kSemi, 12 * kSemi, 127 * kSemi}, {"feedback", 26000, -31000, 31000}, {"damp", 110 * kSemi, 40 * kSemi, 135 * kSemi},
             {"interval", 0, 0, 24}, {"mix", 16384, 0, kUnity}}};
        return i;
    }
    bool init(Memory &m) override {
        a4_ = inc_a4();
        const int room = kSampleRate / 20 + 8;                                                   // down to 20 Hz
        return a_.init(*m.fast, room) && b_.init(*m.fast, room);
    }
    void reset() override { a_.clear(); b_.clear(); lpa_ = lpb_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case CMB_TUNE: tune_ = v; break;
            case CMB_FEEDBACK: fb_ = static_cast<q15>(v); break;
            case CMB_DAMP: damp_ = v; break;
            case CMB_INTERVAL: interval_ = v; break;
            case CMB_MIX: mix_ = static_cast<q15>(v); break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const int n = ctx.frames;
        const q15 *cv = p.in[1];
        const int32_t pitch = tune_ + scaled(cv[0], kPitchCvSpan);                                    // tune (middle C = follow the key as is) + the key's offset from middle C
        const uint32_t inc = pitch_to_inc(pitch, a4_), incb = pitch_to_inc(pitch + interval_ * kSemi, a4_);
        const uint32_t da = delay_of(inc), db = delay_of(incb);
        const uint32_t k = svf_g(pitch_to_inc(damp_, a4_));                                          // tan(pi fc / fs) in Q28, used as a one-pole weight
        const int32_t lp = static_cast<int32_t>((static_cast<uint64_t>(k) << 30) / ((1ull << 28) + k) >> 0);   // g / (1 + g) in Q30
        const int32_t norm = 32767 - (fb_ < 0 ? -fb_ : fb_);
        for (int i = 0; i < n; i++) {
            const q15 x = p.in[0][i];
            const q15 ya = a_.read_hermite(da);
            lpa_ += static_cast<int32_t>((static_cast<int64_t>(to_q23(ya) - lpa_) * lp) >> 30);
            a_.write(sat16(x + mul15(from_q23(lpa_), fb_)));
            int32_t wet = ya;
            if (interval_ > 0) {
                const q15 yb = b_.read_hermite(db);
                lpb_ += static_cast<int32_t>((static_cast<int64_t>(to_q23(yb) - lpb_) * lp) >> 30);
                b_.write(sat16(x + mul15(from_q23(lpb_), fb_)));
                wet = (wet + yb) >> 1;
            }
            const int32_t wn = (wet * norm) >> 14;                                                 // loudness compensation for the feedback gain
            p.out[0][i] = sat16(x + (((sat16(wn) - x) * mix_) >> 15));
        }
    }
private:
    static int32_t to_q23(q15 v) { return static_cast<int32_t>(v) << 8; }
    static q15 from_q23(int32_t v) { return sat16(v >> 8); }
    static uint32_t delay_of(uint32_t inc) {
        const uint64_t d = inc ? (1ull << 48) / inc : 0;
        const uint64_t lo = 3u << 16, hi = static_cast<uint64_t>(kSampleRate / 20) << 16;
        return static_cast<uint32_t>(d < lo ? lo : (d > hi ? hi : d));
    }
    DelayLine a_, b_;
    uint32_t a4_ = 0;
    int32_t tune_ = 60 * kSemi, damp_ = 110 * kSemi, interval_ = 0, lpa_ = 0, lpb_ = 0;
    q15 fb_ = 26000, mix_ = 16384;
};

}  // namespace

void register_fx2_modules(Registry &r) {
    r.add(T_PHASER, type_of<Phaser>());
    r.add(T_FLANGER, type_of<Flanger>());
    r.add(T_TREMOLO, type_of<Tremolo>());
    r.add(T_COMP, type_of<Compressor>());
    r.add(T_EQ3, type_of<Eq3>());
    r.add(T_SHIFTER, type_of<Shifter>());
    r.add(T_CONV, type_of<Convolver>());
    r.add(T_COMB, type_of<TunedComb>());
}

}  // namespace sc
