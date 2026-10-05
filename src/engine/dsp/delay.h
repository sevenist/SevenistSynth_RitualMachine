#pragma once
// Delay line and the Schroeder building blocks made from it (used by Delay, Chorus and the reverb).
//
// DelayLine: exact-size ring of q15 (no power-of-two rounding: reverb lines are thousands of samples and rounding
// up would waste half the SRAM). Usage per sample is "read, then write": read(d) with d >= 1 returns the sample
// written d writes ago (d = 1 is the previous input). Fractional reads take the delay in Q16 samples; Hermite
// needs d >= 2. The buffer is allocated from a Heap (fast SRAM for short lines, bulk/PSRAM for long ones) and
// released in the destructor.
#include <cstdint>
#include "engine/core/heap.h"
#include "engine/dsp/interp.h"
#include "engine/dsp/q.h"

namespace sc {

class DelayLine {
public:
    DelayLine() = default;
    DelayLine(const DelayLine &) = delete;
    DelayLine &operator=(const DelayLine &) = delete;
    ~DelayLine() { release(); }

    // Room for delays up to `max_delay` samples (plus a few guard samples for the interpolators).
    bool init(Heap &h, int max_delay) {
        release();
        const uint32_t cap = static_cast<uint32_t>(max_delay < 4 ? 4 : max_delay) + 4u;
        buf_ = h.alloc_array<q15>(cap);
        if (!buf_) return false;
        heap_ = &h;
        size_ = cap;
        w_ = 0;
        return true;
    }
    void release() {
        if (heap_) heap_->free(buf_);
        buf_ = nullptr;
        heap_ = nullptr;
        size_ = 0;
    }
    void clear() { if (buf_) for (uint32_t i = 0; i < size_; i++) buf_[i] = 0; }

    int max_delay() const { return buf_ ? static_cast<int>(size_) - 4 : 0; }
    bool ready() const { return buf_ != nullptr; }

    void write(q15 x) { buf_[w_] = x; if (++w_ == size_) w_ = 0; }
    q15 read(int d) const {
        int i = static_cast<int>(w_) - d;
        if (i < 0) i += static_cast<int>(size_);
        return buf_[i];
    }

    q15 read_lerp(uint32_t d_q16) const {
        uint32_t i = d_q16 >> 16;
        return lerp15(read(static_cast<int>(i)), read(static_cast<int>(i) + 1), d_q16 & 0xFFFFu);
    }
    q15 read_hermite(uint32_t d_q16) const {
        int i = static_cast<int>(d_q16 >> 16);
        return hermite15(read(i - 1), read(i), read(i + 1), read(i + 2), d_q16 & 0xFFFFu);
    }

private:
    Heap *heap_ = nullptr;
    q15 *buf_ = nullptr;
    uint32_t size_ = 0, w_ = 0;
};

// Feedback comb with a one-pole low-pass in the loop (Freeverb style): damping makes the tail darker.
class Comb {
public:
    bool init(Heap &h, int delay) { delay_ = delay; lp_ = 0; return line_.init(h, delay); }
    void set_feedback(q15 g) { fb_ = g; }
    void set_damp(q31 coef) { damp_ = coef; }       // 0 = no damping (full band), larger = darker (Q31 coefficient of the one-pole)
    void clear() { line_.clear(); lp_ = 0; }
    q15 process(q15 x) {
        q15 y = line_.read(delay_);
        const int64_t diff = static_cast<int64_t>(to31(y)) - lp_;                                   // up to 2^32
        lp_ = static_cast<q31>(lp_ + ((static_cast<int64_t>(static_cast<int32_t>(diff >> 1)) * (kQ31Max - damp_)) >> 30));                    // one-pole, weight 1 - damp
        line_.write(sat16(x + mul15(to15(lp_), fb_)));
        return y;
    }
private:
    DelayLine line_;
    int delay_ = 1;
    q15 fb_ = 0;
    q31 damp_ = 0, lp_ = 0;
};

// Schroeder all-pass: w = x + g v ; y = v - g w ; v = w delayed. Flat magnitude, smears phase (diffusion).
class Allpass {
public:
    bool init(Heap &h, int delay) { delay_ = delay; return line_.init(h, delay); }
    void set_gain(q15 g) { g_ = g; }
    void clear() { line_.clear(); }
    q15 process(q15 x) {
        q15 v = line_.read(delay_);
        q15 w = sat16(x + mul15(v, g_));
        line_.write(w);
        return sat16(v - mul15(w, g_));
    }
    // Variant with a fractional, modulated delay (Q16 samples, linear interpolation): the decay diffusers of a
    // plate reverb. init() must have been given room for the longest delay.
    q15 process_mod(q15 x, uint32_t d_q16) {
        q15 v = line_.read_lerp(d_q16);
        q15 w = sat16(x + mul15(v, g_));
        line_.write(w);
        return sat16(v - mul15(w, g_));
    }
    q15 tap(int d) const { return line_.read(d); }                      // the internal state w, d samples ago (output taps)
private:
    DelayLine line_;
    int delay_ = 1;
    q15 g_ = 0;
};

}  // namespace sc
