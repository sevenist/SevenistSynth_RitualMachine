#include "engine/modules/fx_modules.h"
#include <cmath>
#include "engine/dsp/block.h"
#include "engine/dsp/cordic.h"
#include "engine/dsp/delay.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/stft.h"
#include "engine/dsp/util.h"
#include "engine/modules/synth_modules.h"

namespace sc {
namespace {

constexpr int kSemi = 256;
constexpr int NB = kFftN / 2 + 1;                        // bins 0..N/2

uint32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return static_cast<uint32_t>(r);
}

/* ------------------------------------------------------------------ Delay */

class Delay : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Delay", Scope::Global, 2, 2, DLY_N, false, {"L", "R"}, {"L", "R"},
            {{"time", 350 * 16, 2 * 16, ENGINE_DELAY_MAX_MS * 16}, {"feedback", 12000, 0, 31000},
             {"damp", 110 * kSemi, 30 * kSemi, 135 * kSemi}, {"mix", 16384, 0, kUnity}, {"pingpong", 0, 0, 1},
             {"time_mod", 8 * 16, 0, 200 * 16}}};
        return i;
    }
    bool init(Memory &m) override {
        const int max_samples = ENGINE_DELAY_MAX_MS * kSampleRate / 1000;
        return l_.init(*m.bulk, max_samples) && r_.init(*m.bulk, max_samples);
    }
    void reset() override { l_.clear(); r_.clear(); lpl_ = lpr_ = 0; cur_ = target_; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case DLY_TIME: target_ = to_samples(v); break;
            case DLY_FEEDBACK: fb_ = static_cast<q15>(v); break;
            case DLY_DAMP: {
                double fc = 440.0 * std::exp2((v - 69.0 * kSemi) / (12.0 * kSemi));
                damp_ = q31_from_float(1.0 - std::exp(-2.0 * 3.14159265358979 * fc / kSampleRate));
                break;
            }
            case DLY_MIX: mix_ = static_cast<q15>(v); break;
            case DLY_PINGPONG: pingpong_ = v != 0; break;
            case DLY_TIME_MOD: tmod_ = to_samples(v); break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const int64_t d0 = cur_;
        int64_t step = (target_ - cur_) / 24;                                // exponential approach...
        const int64_t max_step = static_cast<int64_t>(kBlock / 4) << 16;     // ...but the delay never moves faster than 1/4 sample per sample
        step = step > max_step ? max_step : (step < -max_step ? -max_step : step);   //    (tape-like pitch swoop of at most +-25 %, never a click)
        cur_ += step;
        if ((target_ - cur_) < 65536 && (cur_ - target_) < 65536) cur_ = target_;
        const int64_t d1 = cur_;
        const q15 *mt = p.mod[DLY_TIME];
        const int64_t lo = 2ll << 16, hi = static_cast<int64_t>(l_.max_delay()) << 16;
        for (int i = 0; i < ctx.frames; i++) {
            int64_t d = d0 + (((d1 - d0) * i) >> kBlockLog2);
            if (mt) d += (static_cast<int64_t>(mt[i]) * tmod_) >> 15;
            d = d < lo ? lo : (d > hi ? hi : d);
            const uint32_t dq = static_cast<uint32_t>(d);
            const q15 yl = l_.read_hermite(dq), yr = r_.read_hermite(dq);
            lpl_ = lp(lpl_, yl);
            lpr_ = lp(lpr_, yr);
            const q15 inl = p.in[0][i], inr = p.in[1][i];
            if (pingpong_) {
                l_.write(sat16(((inl + inr) >> 1) + mul15(to15(lpr_), fb_)));
                r_.write(mul15(to15(lpl_), fb_));
            } else {
                l_.write(sat16(inl + mul15(to15(lpl_), fb_)));
                r_.write(sat16(inr + mul15(to15(lpr_), fb_)));
            }
            p.out[0][i] = sat16(inl + (((static_cast<int32_t>(yl) - inl) * mix_) >> 15));
            p.out[1][i] = sat16(inr + (((static_cast<int32_t>(yr) - inr) * mix_) >> 15));
        }
    }
private:
    static constexpr int kBlockLog2 = (kBlock == 8 ? 3 : kBlock == 16 ? 4 : kBlock == 32 ? 5 : kBlock == 64 ? 6 : kBlock == 128 ? 7 : 8);
    static int64_t to_samples(int32_t sixteenth_ms) { return static_cast<int64_t>(sixteenth_ms) * kSampleRate * 65536 / 16000; }
    q31 lp(q31 state, q15 y) const {
        const int64_t diff = static_cast<int64_t>(to31(y)) - state;
        return static_cast<q31>(state + ((diff >> 1) * damp_ >> 30));
    }
    DelayLine l_, r_;
    int64_t cur_ = to_samples(350 * 16), target_ = to_samples(350 * 16), tmod_ = to_samples(8 * 16);
    q15 fb_ = 12000, mix_ = 16384;
    q31 damp_ = kQ31Max / 2, lpl_ = 0, lpr_ = 0;
    bool pingpong_ = false;
};

/* ------------------------------------------------------------------ SpectralFx */

class SpectralFx : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"SpectralFx", Scope::Global, 1, 1, SPX_N, false, {"in"}, {"out"},
            {{"mode", SPXM_THRU, 0, 5}, {"amount", 4096, 0, kUnity}, {"lo", 20 * kSemi, 0, 135 * kSemi}, {"hi", 135 * kSemi, 0, 135 * kSemi},
             {"shift", 0, -24 * kSemi, 24 * kSemi}, {"freeze", 0, 0, 1}, {"mix", kUnity, 0, kUnity}}};
        return i;
    }
    bool init(Memory &m) override {
        heap_ = m.fast;
        arrays_ = static_cast<uint8_t *>(m.fast->alloc(NB * (4 + 8 + 4 + 4 + 4 + 8)));
        if (!arrays_) return false;
        uint8_t *p = arrays_;
        mag_ = reinterpret_cast<uint32_t *>(p); p += NB * 4;
        adv_ = reinterpret_cast<int64_t *>(p); p += NB * 8;
        prev_ = reinterpret_cast<uint32_t *>(p); p += NB * 4;
        synth_ = reinterpret_cast<uint32_t *>(p); p += NB * 4;
        hmag_ = reinterpret_cast<uint32_t *>(p); p += NB * 4;
        hadv_ = reinterpret_cast<int64_t *>(p);
        return stft_.init(*m.fast, false) && dry_.init(*m.fast, Stft::latency());
    }
    ~SpectralFx() override { if (heap_) heap_->free(arrays_); }
    void reset() override {
        stft_.reset(); dry_.clear();
        for (int k = 0; k < NB; k++) { mag_[k] = 0; adv_[k] = 0; prev_[k] = 0; synth_[k] = 0; hmag_[k] = 0; hadv_[k] = 0; }
        have_hold_ = false;
        synced_ = false;
    }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case SPX_MODE: mode_ = v; synced_ = false; break;
            case SPX_AMOUNT: amount_ = static_cast<q15>(v); break;
            case SPX_LO: lo_bin_ = to_bin(v); break;
            case SPX_HI: hi_bin_ = to_bin(v); break;
            case SPX_SHIFT: ratio_ = static_cast<int64_t>(std::lround(65536.0 * std::exp2(v / (12.0 * kSemi)))); synced_ = false; break;
            case SPX_FREEZE: freeze_ = v != 0; break;
            case SPX_MIX: mix_ = static_cast<q15>(v); break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        q15 wet[kBlock];
        stft_.process(p.in[0], nullptr, wet, ctx.frames, [this](StftFrame &f) { on_frame(f); });
        for (int i = 0; i < ctx.frames; i++) {
            const q15 dry = dry_.read(Stft::latency());
            dry_.write(p.in[0][i]);
            p.out[0][i] = mix_ == kUnity ? wet[i] : sat16(dry + (((static_cast<int32_t>(wet[i]) - dry) * mix_) >> 15));
        }
    }
private:
    static int to_bin(int32_t pitch) {
        double hz = 440.0 * std::exp2((pitch - 69.0 * kSemi) / (12.0 * kSemi));
        int b = static_cast<int>(std::lround(hz / (static_cast<double>(kSampleRate) / kFftN)));
        return b < 0 ? 0 : (b > kFftN / 2 ? kFftN / 2 : b);
    }

    // Phase-vocoder analysis: magnitude and unwrapped phase advance per hop for every bin.
    void analyse(const StftFrame &f) {
        for (int k = 0; k < NB; k++) {
            uint32_t ph, mg;
            cordic_polar(f.re[k], f.im[k], ph, mg);
            const int32_t dev = static_cast<int32_t>(ph - prev_[k] - (static_cast<uint32_t>(k) << 30));   // deviation from the bin centre, +-pi
            adv_[k] = (static_cast<int64_t>(k) << 30) + dev;
            prev_[k] = ph;
            mag_[k] = mg;
        }
    }
    // Resynthesis with an optional frequency ratio: output bin j takes the nearest source bin j / ratio.
    // `advance` = false on the frame that starts a new run (mode / ratio change, freeze capture): the output
    // phases are then taken from the analysis so that neighbouring bins keep the phase relation of the window's
    // main lobe. At unity ratio the output phase simply is the analysis phase (identity resynthesis).
    void synthesise(StftFrame &f, const uint32_t *mag, const int64_t *adv, bool running) {
        const bool unity = ratio_ == 65536;
        const int64_t inv = unity ? 65536 : (65536ll * 65536ll + ratio_ / 2) / ratio_;
        for (int j = 0; j < NB; j++) {
            const int64_t k = unity ? j : (j * inv + 32768) >> 16;
            if (k >= NB) { f.re[j] = 0; f.im[j] = 0; continue; }
            if (!synced_) synth_[j] = unity ? prev_[k] : static_cast<uint32_t>((static_cast<int64_t>(prev_[k]) * ratio_) >> 16);
            else if (running) synth_[j] += static_cast<uint32_t>(unity ? adv[k] : (adv[k] * ratio_) >> 16);
            polar_to_rect(mag[k], synth_[j], f.re[j], f.im[j]);
        }
        synced_ = true;
    }

    void on_frame(StftFrame &f) {
        switch (mode_) {
            case SPXM_THRU: break;
            case SPXM_GATE: {
                uint32_t peak = 0;
                for (int k = 0; k < NB; k++) {
                    const uint32_t m2 = static_cast<uint32_t>(f.re[k] * f.re[k]) + static_cast<uint32_t>(f.im[k] * f.im[k]);
                    if (m2 > peak) peak = m2;
                }
                const uint32_t t2 = static_cast<uint32_t>((static_cast<int32_t>(amount_) * amount_) >> 15);
                const uint32_t thr = static_cast<uint32_t>((static_cast<uint64_t>(peak) * t2) >> 15);
                for (int k = 0; k < NB; k++) {
                    const uint32_t m2 = static_cast<uint32_t>(f.re[k] * f.re[k]) + static_cast<uint32_t>(f.im[k] * f.im[k]);
                    if (m2 < thr || k < lo_bin_ || k > hi_bin_) { f.re[k] = 0; f.im[k] = 0; }
                }
                break;
            }
            case SPXM_ROBOT:
                for (int k = 0; k < NB; k++) {
                    uint32_t ph, mg;
                    cordic_polar(f.re[k], f.im[k], ph, mg);
                    f.re[k] = sat16(static_cast<int32_t>(mg));
                    f.im[k] = 0;
                }
                break;
            case SPXM_WHISPER:
                for (int k = 0; k < NB; k++) {
                    uint32_t ph, mg;
                    cordic_polar(f.re[k], f.im[k], ph, mg);
                    polar_to_rect(mg, rng_.next_u32(), f.re[k], f.im[k]);
                }
                break;
            case SPXM_FREEZE:
            case SPXM_PITCH:
                if (mode_ == SPXM_FREEZE && freeze_) {
                    if (!have_hold_) {                                  // capture the current spectrum once
                        analyse(f);
                        for (int k = 0; k < NB; k++) { hmag_[k] = mag_[k]; hadv_[k] = adv_[k]; }
                        hexp_ = f.exp;
                        have_hold_ = true;
                        synced_ = false;                                // start from the captured phases
                    }
                    f.exp = hexp_;
                    synthesise(f, hmag_, hadv_, true);
                } else {
                    if (have_hold_) { have_hold_ = false; synced_ = false; }
                    analyse(f);
                    if (ratio_ == 65536) synced_ = false;               // identity: always follow the analysis phase
                    synthesise(f, mag_, adv_, true);
                }
                break;
        }
    }

    Heap *heap_ = nullptr;
    uint8_t *arrays_ = nullptr;
    uint32_t *mag_ = nullptr, *prev_ = nullptr, *synth_ = nullptr, *hmag_ = nullptr;
    int64_t *adv_ = nullptr, *hadv_ = nullptr;
    Stft stft_;
    DelayLine dry_;
    Noise rng_{0x5EEDu};
    int32_t mode_ = SPXM_THRU;
    q15 amount_ = 4096, mix_ = kUnity;
    int lo_bin_ = 0, hi_bin_ = kFftN / 2, hexp_ = 0;
    int64_t ratio_ = 65536;
    bool freeze_ = false, have_hold_ = false, synced_ = false;
};

/* ------------------------------------------------------------------ Vocoder */

class Vocoder : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Vocoder", Scope::Global, 2, 1, VOC_N, false, {"mod", "car"}, {"out"},
            {{"band_log2", 3, 1, 5}, {"gain", 256, 0, 4096}}};
        return i;
    }
    bool init(Memory &m) override { return stft_.init(*m.fast, true); }
    void reset() override { stft_.reset(); for (auto &g : gain_s_) g = 0; }
    void set_param(int idx, int32_t v) override {
        if (idx == VOC_BAND_LOG2) band_log2_ = v;
        else if (idx == VOC_GAIN) gain_ = v;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        stft_.process(p.in[0], p.in[1], p.out[0], ctx.frames, [this](StftFrame &f) { on_frame(f); });
    }
private:
    void on_frame(StftFrame &f) {
        int16_t mr[NB], mi[NB], cr[NB], ci[NB];
        for (int k = 0; k < NB; k++) {                                   // split X = M + jC using conjugate symmetry
            const int kk = k % kFftN == 0 ? 0 : kFftN - k;
            const int32_t ar = f.re[k], ai = f.im[k], br = f.re[kk], bi = f.im[kk];
            mr[k] = static_cast<int16_t>((ar + br) >> 1);
            mi[k] = static_cast<int16_t>((ai - bi) >> 1);
            cr[k] = static_cast<int16_t>((ai + bi) >> 1);
            ci[k] = static_cast<int16_t>(-((ar - br) >> 1));
        }
        const int bw = 1 << band_log2_;
        for (int b0 = 0, bi_ = 0; b0 < NB; b0 += bw, bi_++) {
            uint64_t em = 0, ec = 0;
            const int b1 = b0 + bw < NB ? b0 + bw : NB;
            for (int k = b0; k < b1; k++) {
                em += static_cast<uint64_t>(mr[k] * mr[k] + mi[k] * mi[k]);
                ec += static_cast<uint64_t>(cr[k] * cr[k] + ci[k] * ci[k]);
            }
            uint64_t ratio_q16 = (em << 16) / (ec + 1);                  // energy ratio, Q16
            uint32_t g = isqrt64(ratio_q16);                             // amplitude ratio, Q8
            if (g > 8192) g = 8192;
            int32_t gs = gain_s_[bi_];
            gs += (static_cast<int32_t>(g) - gs) / 2;                    // smooth across frames
            gain_s_[bi_] = gs;
            for (int k = b0; k < b1; k++) {
                const int32_t gg = (gs * gain_) >> 8;                    // Q8
                f.re[k] = sat16(static_cast<int32_t>((static_cast<int64_t>(cr[k]) * gg) >> 8));
                f.im[k] = sat16(static_cast<int32_t>((static_cast<int64_t>(ci[k]) * gg) >> 8));
            }
        }
        f.im[0] = 0;
        f.im[kFftN / 2] = 0;
        for (int k = 1; k < kFftN / 2; k++) { f.re[kFftN - k] = f.re[k]; f.im[kFftN - k] = sat16(-static_cast<int32_t>(f.im[k])); }
    }
    Stft stft_;
    int32_t band_log2_ = 3, gain_ = 256;
    int32_t gain_s_[NB] = {};
};


/* ------------------------------------------------------------------ Chorus */

class Chorus : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Chorus", Scope::Global, 2, 2, CHR_N, false, {"L", "R"}, {"L", "R"},
            {{"mode", CHRM_I, 0, 3}, {"mix", 24000, 0, kUnity}}};
        return i;
    }
    bool init(Memory &m) override {
        center_ = static_cast<int64_t>(3.345e-3 * kSampleRate * 65536.0);
        lp_ = q31_from_float(1.0 - std::exp(-2.0 * 3.14159265358979 * 9000.0 / kSampleRate));   // BBD-like band limit of the wet signal
        const int room = static_cast<int>(0.008 * kSampleRate) + 8;
        return l_.init(*m.fast, room) && r_.init(*m.fast, room);
    }
    void reset() override { l_.clear(); r_.clear(); lpl_ = lpr_ = 0; depth_cur_ = depth_tgt_; gain_cur_ = gain_tgt_; }
    void set_param(int idx, int32_t v) override {
        if (idx == CHR_MODE) {
            static const float rate[4] = {1.0f, 0.513f, 0.863f, 9.75f};
            static const double depth_ms[4] = {0.0, 1.805, 1.805, 0.25};
            const int mo = clamp_i32(v, 0, 3);
            inc_ = hz_to_inc(rate[mo]);
            depth_tgt_ = static_cast<int64_t>(depth_ms[mo] * 1e-3 * kSampleRate * 65536.0);
            wet_on_ = mo != CHRM_OFF;
        } else if (idx == CHR_MIX) {
            mix_ = static_cast<q15>(v);
        }
        gain_tgt_ = wet_on_ ? static_cast<int64_t>(mix_) << 8 : 0;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        for (int i = 0; i < ctx.frames; i++) {
            ph_ += inc_;
            depth_cur_ += (depth_tgt_ - depth_cur_) >> 11;
            gain_cur_ += (gain_tgt_ - gain_cur_) >> 11;
            const int32_t tri = ph_ < 0x80000000u ? 32767 - static_cast<int32_t>(ph_ >> 15) : static_cast<int32_t>(ph_ >> 15) - 98303;   // -1..+1
            const int64_t mod = (depth_cur_ * tri) >> 15;
            const q15 inl = p.in[0][i], inr = p.in[1][i];
            const q15 wl = l_.read_hermite(static_cast<uint32_t>(center_ + mod)), wr = r_.read_hermite(static_cast<uint32_t>(center_ - mod));
            l_.write(inl);
            r_.write(inr);
            lpl_ = lp(lpl_, wl);
            lpr_ = lp(lpr_, wr);
            const int32_t g = static_cast<int32_t>(gain_cur_ >> 8);
            p.out[0][i] = sat16(inl + ((to15(lpl_) * g) >> 15));
            p.out[1][i] = sat16(inr + ((to15(lpr_) * g) >> 15));
        }
    }
private:
    q31 lp(q31 state, q15 y) const {
        const int64_t diff = static_cast<int64_t>(to31(y)) - state;
        return static_cast<q31>(state + ((diff >> 1) * lp_ >> 30));
    }
    DelayLine l_, r_;
    int64_t center_ = 0, depth_cur_ = 0, depth_tgt_ = 0, gain_cur_ = 0, gain_tgt_ = 0;
    uint32_t ph_ = 0, inc_ = 0;
    q31 lp_ = 0, lpl_ = 0, lpr_ = 0;
    q15 mix_ = 24000;
    bool wet_on_ = true;
};

/* ------------------------------------------------------------------ Reverb (Dattorro plate) */

class Reverb : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Reverb", Scope::Global, 2, 2, RVB_N, false, {"L", "R"}, {"L", "R"},
            {{"predelay", 20, 0, 200}, {"decay", 26000, 0, 32400}, {"damp", 100 * kSemi, 30 * kSemi, 135 * kSemi},
             {"bandwidth", 130 * kSemi, 30 * kSemi, 135 * kSemi}, {"size", 21845, 0, kUnity}, {"mod", 12000, 0, kUnity}, {"mix", 12000, 0, kUnity}}};
        return i;
    }
    bool init(Memory &m) override {
        sc_ = (static_cast<int64_t>(kSampleRate) << 16) / kRefRate;                       // Q16 sample-rate scale
        auto len = [&](int ref, int64_t f_q16) { return static_cast<int>(((static_cast<int64_t>(ref) * sc_ >> 16) * f_q16) >> 16); };
        const int64_t big = kSizeMax;
        bool ok = true;
        static const int in_len[4] = {142, 107, 379, 277};
        static const q15 in_g[4] = {24576, 24576, 20480, 20480};                           // 0.75 0.75 0.625 0.625
        for (int k = 0; k < 4; k++) { ok &= ap_in_[k].init(*m.fast, len(in_len[k], 65536)); ap_in_[k].set_gain(in_g[k]); }
        const int exc = static_cast<int>((16 * sc_) >> 16) + 4;
        ok &= apl1_.init(*m.fast, len(672, 65536) + exc);
        ok &= apl2_.init(*m.fast, len(1800, 65536));
        ok &= apr1_.init(*m.fast, len(908, 65536) + exc);
        ok &= apr2_.init(*m.fast, len(2656, 65536));
        ok &= dl1_.init(*m.fast, len(4453, big)) && dl2_.init(*m.fast, len(3720, big));
        ok &= dr1_.init(*m.fast, len(4217, big)) && dr2_.init(*m.fast, len(3163, big));
        ok &= pre_.init(*m.bulk, kSampleRate / 5 + 4);                                    // 200 ms
        apl1_.set_gain(22938);
        apr1_.set_gain(22938);                                                            // decay diffusion 1 = 0.7
        update_decay();
        recompute_taps(true);
        dcl_.set_corner(8.0f);
        dcr_.set_corner(8.0f);
        dco_l_.set_corner(8.0f);
        dco_r_.set_corner(8.0f);
        inc1_ = hz_to_inc(0.9f);
        inc2_ = hz_to_inc(1.1f);
        return ok;
    }
    void reset() override {
        for (auto &a : ap_in_) a.clear();
        apl1_.clear(); apl2_.clear(); apr1_.clear(); apr2_.clear();
        dl1_.clear(); dl2_.clear(); dr1_.clear(); dr2_.clear(); pre_.clear();
        bw_ = dampl_ = dampr_ = 0;
        el_ = er_ = 0;
        dcl_.reset();
        dcr_.reset();
        dco_l_.reset();
        dco_r_.reset();
        size_cur_ = size_tgt_;
        recompute_taps(true);
    }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case RVB_PREDELAY: pre_samples_ = v * kSampleRate / 1000; break;
            case RVB_DECAY: decay_ = static_cast<q15>(v); update_decay(); break;
            case RVB_DAMP: damp_ = cutoff_coef(v); break;
            case RVB_BANDWIDTH: bw_coef_ = cutoff_coef(v); break;
            case RVB_SIZE: size_tgt_ = 32768 + ((static_cast<int64_t>(v) * 49152) >> 15) - 16384; break;   // 0.5 .. 1.25 in Q16
            case RVB_MOD: mod_ = static_cast<q15>(v); break;
            case RVB_MIX: mix_ = static_cast<q15>(v); break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        // size glides: the tank delays never move faster than a fraction of a sample per sample, and every
        // length / tap position is interpolated per sample (a per-block step would jump by several samples)
        int64_t step = size_tgt_ - size_cur_;
        const int64_t max_step = 48;                                                      // Q16 of the size factor per block
        step = step > max_step ? max_step : (step < -max_step ? -max_step : step);
        bool gliding = false;
        if (step != 0) {
            size_cur_ += step;
            recompute_taps();
            for (int k = 0; k < kPos; k++) pstep_[k] = (offt_[k] - off_[k]) / kBlock;
            gliding = true;
        }
        const int64_t exc = ((16 * sc_) * mod_) >> 15;                                    // Q16 samples
        for (int i = 0; i < ctx.frames; i++) {
            if (gliding) for (int k = 0; k < kPos; k++) off_[k] += pstep_[k];
            const uint32_t len_l1 = pos(0), len_l2 = pos(1), len_r1 = pos(2), len_r2 = pos(3);
            const q15 inl = p.in[0][i], inr = p.in[1][i];
            q15 x = mul15(static_cast<q15>((inl + inr) >> 1), 16384);                     // mono, 6 dB of headroom for the tank
            if (pre_samples_ > 0) { const q15 d = pre_.read(pre_samples_); pre_.write(x); x = d; } else pre_.write(x);
            bw_ = lp(bw_, x, bw_coef_);
            x = to15(bw_);
            for (auto &a : ap_in_) x = a.process(x);

            ph1_ += inc1_;
            ph2_ += inc2_;
            const uint32_t m1 = static_cast<uint32_t>(static_cast<int64_t>(base_l1_) + ((static_cast<int64_t>(sine(ph1_)) * exc) >> 15));
            const uint32_t m2 = static_cast<uint32_t>(static_cast<int64_t>(base_r1_) + ((static_cast<int64_t>(sine(ph2_)) * exc) >> 15));

            // figure-8 tank: each half is fed by the input plus the decayed end of the other half
            const q15 in_l = sat16(x + mul15(er_, decay_)), in_r = sat16(x + mul15(el_, decay_));
            // left half
            const q15 a_l = apl1_.process_mod(in_l, m1);
            const q15 d1_l = dcl_.process(dl1_.read_lerp(len_l1));                       // the DC blocker stops rounding bias from piling up in the loop
            dl1_.write(a_l);
            dampl_ = lp(dampl_, d1_l, damp_);
            const q15 c_l = apl2_.process(mul15(to15(dampl_), decay_));
            el_ = dl2_.read_lerp(len_l2);
            dl2_.write(c_l);
            // right half
            const q15 a_r = apr1_.process_mod(in_r, m2);
            const q15 d1_r = dcr_.process(dr1_.read_lerp(len_r1));
            dr1_.write(a_r);
            dampr_ = lp(dampr_, d1_r, damp_);
            const q15 c_r = apr2_.process(mul15(to15(dampr_), decay_));
            er_ = dr2_.read_lerp(len_r2);
            dr2_.write(c_r);

            // output taps (Dattorro 1997, scaled to this sample rate and size)
            const int32_t yl = dr1_.read_lerp(pos(4)) + dr1_.read_lerp(pos(5)) - apr2_.tap(t_ap_[0]) + dr2_.read_lerp(pos(6))
                             - dl1_.read_lerp(pos(7)) - apl2_.tap(t_ap_[1]) - dl2_.read_lerp(pos(8));
            const int32_t yr = dl1_.read_lerp(pos(9)) + dl1_.read_lerp(pos(10)) - apl2_.tap(t_ap_[2]) + dl2_.read_lerp(pos(11))
                             - dr1_.read_lerp(pos(12)) - apr2_.tap(t_ap_[3]) - dr2_.read_lerp(pos(13));
            const int32_t wl = dco_l_.process(sat16((yl * 19661) >> 14)), wr = dco_r_.process(sat16((yr * 19661) >> 14));   // x 0.6, doubled: gives back the input headroom; DC removed
            p.out[0][i] = sat16(inl + (((wl - inl) * static_cast<int32_t>(mix_)) >> 15));
            p.out[1][i] = sat16(inr + (((wr - inr) * static_cast<int32_t>(mix_)) >> 15));
        }
        if (gliding) for (int k = 0; k < kPos; k++) off_[k] = offt_[k];                 // land exactly on the target
    }
private:
    static constexpr int kRefRate = 29761;                        // Dattorro's reference sample rate
    static constexpr int kPos = 14;                               // 4 tank lengths + 10 output taps, all size-scaled
    uint32_t pos(int k) const { return static_cast<uint32_t>(off_[k]); }
    static constexpr int64_t kSizeMax = 81920;                    // 1.25 in Q16
    q31 cutoff_coef(int32_t pitch) const {
        const double fc = 440.0 * std::exp2((pitch - 69.0 * kSemi) / (12.0 * kSemi));
        return q31_from_float(1.0 - std::exp(-2.0 * 3.14159265358979 * fc / kSampleRate));
    }
    static q31 lp(q31 state, q15 y, q31 coef) {
        const int64_t diff = static_cast<int64_t>(to31(y)) - state;
        return static_cast<q31>(state + (((diff >> 1) * coef) >> 30));
    }
    void update_decay() {
        int32_t dd2 = decay_ + 4915;                                // decay + 0.15
        dd2 = clamp_i32(dd2, 8192, 16384);                          // clamped to 0.25 .. 0.5
        apl2_.set_gain(static_cast<q15>(dd2));
        apr2_.set_gain(static_cast<q15>(dd2));
    }
    // Q16 delay lengths and tap offsets from the current size
    void recompute_taps(bool snap = false) {
        auto SQ = [&](int ref) { return ((static_cast<int64_t>(ref) * sc_) >> 16) * size_cur_; };   // samples x size -> Q16
        static const int ref[kPos] = {4453, 3720, 4217, 3163, 266, 2974, 1996, 1990, 1066, 353, 3627, 2673, 2111, 121};
        for (int k = 0; k < kPos; k++) {
            int64_t v = SQ(ref[k]);
            offt_[k] = v < 2 * 65536 ? 2 * 65536 : v;
            if (snap) off_[k] = offt_[k];
        }
        auto A = [&](int ref) { return static_cast<int>((static_cast<int64_t>(ref) * sc_) >> 16); };
        t_ap_[0] = A(1913); t_ap_[1] = A(187); t_ap_[2] = A(1228); t_ap_[3] = A(335);
        base_l1_ = static_cast<uint32_t>(static_cast<int64_t>(672) * sc_);                // Q16 samples
        base_r1_ = static_cast<uint32_t>(static_cast<int64_t>(908) * sc_);
    }

    Allpass ap_in_[4], apl1_, apl2_, apr1_, apr2_;
    DelayLine dl1_, dl2_, dr1_, dr2_, pre_;
    int64_t sc_ = 65536, size_cur_ = 65536, size_tgt_ = 65536;
    int64_t off_[kPos] = {}, offt_[kPos] = {}, pstep_[kPos] = {};
    uint32_t base_l1_ = 0, base_r1_ = 0;
    int t_ap_[4] = {};
    DcBlocker dcl_, dcr_, dco_l_, dco_r_;
    uint32_t ph1_ = 0, ph2_ = 0, inc1_ = 0, inc2_ = 0;
    q31 bw_ = 0, dampl_ = 0, dampr_ = 0, damp_ = 0, bw_coef_ = 0;
    q15 el_ = 0, er_ = 0, decay_ = 26000, mod_ = 12000, mix_ = 12000;
    int pre_samples_ = 20 * kSampleRate / 1000;
};

template <typename T>
ModuleType type_of() { static T probe; return {&probe.info(), &create_module<T>}; }

}  // namespace

void register_fx_modules(Registry &r) {
    r.add(T_DELAY, type_of<Delay>());
    r.add(T_SPECTRAL, type_of<SpectralFx>());
    r.add(T_VOCODER, type_of<Vocoder>());
    r.add(T_CHORUS, type_of<Chorus>());
    r.add(T_REVERB, type_of<Reverb>());
}

}  // namespace sc
