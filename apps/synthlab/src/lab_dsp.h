#pragma once
// Small float DSP parts for SynthLab's synths: oscillators, a filter, envelopes, noise, smoothing. Each is a few lines, meant to be read,
// copied into a synth and bent. Cheap enough for the ESP32-S3 (its FPU does float, not double: write 0.5f, not 0.5; prefer the helpers
// here to sinf / expf inside a sample loop, and compute coefficients once per block).
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lab {

// On a function that runs only when a value changes (a synth's update()): kept out of the render it is called from, so the sample loop
// stays small and tools/esp32_report.ps1 shows its divisions and libm calls apart from the render's.
#define LAB_COLD __attribute__((noinline, cold))

constexpr float kPi = 3.14159265f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Denormals: a state that decays with no input (a filter's memory, an envelope, a fade) ends in the tiniest floats, where each operation
// costs many times more (on the PC: the Kick spent 40 % of its time there). Once a block, set what is below 1e-15 (-300 dB) to 0: the
// parts below have flush() for that. The tests' bench warns when a synth is slower with denormals than without.
inline void flush(float &x) { if (x < 1e-15f && x > -1e-15f) x = 0.f; }
inline float wrap01(float ph) { return ph - static_cast<float>(static_cast<int>(ph)) + (ph < 0.f ? 1.f : 0.f); }   // into 0..1
inline float mtof(float note) { return 440.f * exp2f((note - 69.f) * (1.f / 12.f)); }
inline float semis(float st) { return exp2f(st * (1.f / 12.f)); }                       // a ratio from semitones
inline float db(float decibels) { return powf(10.f, decibels * 0.05f); }                // a gain from decibels

// sin(2 pi ph), any ph (in cycles; phase modulation drives it negative): a parabola corrected once (error under 0.001), no table.
inline float sin01(float ph) {
    float x = ph - static_cast<float>(static_cast<int>(ph));                            // -1..1
    if (x < 0.f) x += 1.f;                                                              // 0..1
    x = x < 0.5f ? x : x - 1.f;                                                         // -0.5..0.5
    float y = 8.f * x - 16.f * x * (x < 0.f ? -x : x);
    return 0.225f * (y * (y < 0.f ? -y : y) - y) + y;
}

// Soft saturation: a cubic, slope 1 at 0, flat at +-1 from +-1.5. No division: the ESP32-S3's FPU has none (a float division there is a
// library call, __divsf3, dozens of cycles). Avoid `/` in a sample loop: multiply by an inverse computed once a block.
inline float soft_clip(float x) {
    if (x > 1.5f) return 1.f;
    if (x < -1.5f) return -1.f;
    return x - 0.148148148f * x * x * x;                                                // x - 4/27 x^3
}

// 2^x and log2(x) from the float's bits and a polynomial: about 20 cycles where exp2f / log2f / powf cost a few hundred. Errors: 2^x
// 5e-6 relative (0.01 cent of pitch), log2 3e-5 (0.0002 dB). For envelopes, pitch, gains in dB, x^y = fast_exp2(y * fast_log2(x)).
inline float fast_exp2(float x) {
    x = clampf(x, -126.f, 126.f);
    int i = static_cast<int>(x);
    if (x < static_cast<float>(i)) i--;                                                 // floor
    const float f = x - static_cast<float>(i);
    const float p = 1.f + f * (0.693018631f + f * (0.241404768f + f * (0.0520739356f + f * 0.0134934755f)));
    int32_t bits;
    std::memcpy(&bits, &p, 4);
    bits += i * (1 << 23);                                                              // times 2^i: add i to the exponent
    float y;
    std::memcpy(&y, &bits, 4);
    return y;
}
inline float fast_log2(float x) {                                                       // x > 0 (smaller gives -126)
    if (!(x > 1e-37f)) return -126.f;
    int32_t bits;
    std::memcpy(&bits, &x, 4);
    const int e = ((bits >> 23) & 255) - 127;
    bits = (bits & 0x007FFFFF) | (127 << 23);                                           // the mantissa, as a number in 1..2
    float m;
    std::memcpy(&m, &bits, 4);
    const float u = m - 1.f;
    return static_cast<float>(e) + u * (1.4418255f + u * (-0.708678912f + u * (0.415411186f + u * (-0.194408323f + u * 0.0458789501f))));
}
inline float fast_db(float decibels) { return fast_exp2(decibels * 0.166096404f); }     // a gain from decibels
inline float fast_to_db(float gain) { return 6.02059991f * fast_log2(gain); }

// tan(w) for 0 <= w <= 1.1 (its series to w^9): 1.5 % low at 1.05 (a cutoff a third of the rate), 0.01 % under 0.3. For a filter's
// g = tan(pi hz / sr) that moves every block; no libm call.
inline float fast_tan(float w) {
    const float w2 = w * w;
    return w * (1.f + w2 * (0.333333333f + w2 * (0.133333333f + w2 * (0.053968254f + w2 * 0.021869489f))));
}

// The correction that takes the aliasing out of a jump in a waveform (PolyBLEP): t the phase 0..1, dt the phase step per sample.
inline float poly_blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

// A phase and the classic shapes read from it. Call one shape per sample (each advances the phase), with dt = hz / sr.
struct Osc {
    float ph = 0.f;
    float tick(float dt) { const float p = ph; ph += dt; if (ph >= 1.f) ph -= 1.f; return p; }
    float saw(float dt) { const float p = tick(dt); return 2.f * p - 1.f - poly_blep(p, dt); }
    float square(float dt, float pw = 0.5f) {
        const float p = tick(dt);
        float y = p < pw ? 1.f : -1.f;
        y += poly_blep(p, dt);
        y -= poly_blep(wrap01(p - pw + 1.f), dt);
        return y;
    }
    float tri(float dt) { const float p = tick(dt); return p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p; }   // (soft corners: little aliasing)
    float sine(float dt) { return sin01(tick(dt)); }
};

// State-variable filter (the trapezoidal "TPT" form): stable at any cutoff and resonance, low / band / high pass from one run.
// set() once a block; res 0..1: Q from 0.5 to 10 (a loud peak, short of self-oscillation; for more, lower the 1.9 and expect the peak
// to grow as Q: tame it with soft_clip).
struct Svf {
    float ic1 = 0.f, ic2 = 0.f, g = 0.f, k = 2.f, a1 = 0.f, a2 = 0.f, a3 = 0.f;
    float lp = 0.f, bp = 0.f, hp = 0.f;
    void set(float hz, float res, float sr) { set_g(tanf(kPi * clampf(hz, 10.f, sr * 0.45f) / sr), res); }
    // The same with g = tan(pi hz / sr) given (fast_tan, for a cutoff that moves every block: no libm call; one division)
    void set_g(float g_, float res) {
        g = g_;
        k = 2.f - 1.9f * clampf(res, 0.f, 1.f);
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    float run(float x) {                                    // returns the low pass; bp and hp hold the others
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
        return lp;
    }
    void flush() { lab::flush(ic1); lab::flush(ic2); }
};

// A biquad (transposed direct form II) with the usual EQ shapes (Robert Bristow-Johnson's cookbook). Set it when its values change, not
// every block: each set costs a sinf, a cosf and a powf.
struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
    float run(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void flush() { lab::flush(z1); lab::flush(z2); }
    void set_peak(float hz, float q, float gain_db, float sr) {
        const float A = powf(10.f, gain_db / 40.f), w = 2.f * kPi * clampf(hz, 10.f, sr * 0.45f) / sr, al = sinf(w) / (2.f * q), c = cosf(w);
        norm(1.f + al * A, -2.f * c, 1.f - al * A, 1.f + al / A, -2.f * c, 1.f - al / A);
    }
    void set_low_shelf(float hz, float gain_db, float sr) {
        const float A = powf(10.f, gain_db / 40.f), w = 2.f * kPi * clampf(hz, 10.f, sr * 0.45f) / sr, c = cosf(w);
        const float al = sinf(w) * 0.70710678f, s = 2.f * sqrtf(A) * al;           // (shelf slope 1)
        norm(A * ((A + 1.f) - (A - 1.f) * c + s), 2.f * A * ((A - 1.f) - (A + 1.f) * c), A * ((A + 1.f) - (A - 1.f) * c - s),
             (A + 1.f) + (A - 1.f) * c + s, -2.f * ((A - 1.f) + (A + 1.f) * c), (A + 1.f) + (A - 1.f) * c - s);
    }
    void set_high_shelf(float hz, float gain_db, float sr) {
        const float A = powf(10.f, gain_db / 40.f), w = 2.f * kPi * clampf(hz, 10.f, sr * 0.45f) / sr, c = cosf(w);
        const float al = sinf(w) * 0.70710678f, s = 2.f * sqrtf(A) * al;
        norm(A * ((A + 1.f) + (A - 1.f) * c + s), -2.f * A * ((A - 1.f) + (A + 1.f) * c), A * ((A + 1.f) + (A - 1.f) * c - s),
             (A + 1.f) - (A - 1.f) * c + s, 2.f * ((A - 1.f) - (A + 1.f) * c), (A + 1.f) - (A - 1.f) * c - s);
    }
private:
    void norm(float nb0, float nb1, float nb2, float na0, float na1, float na2) {
        const float k = 1.f / na0;
        b0 = nb0 * k; b1 = nb1 * k; b2 = nb2 * k; a1 = na1 * k; a2 = na2 * k;
    }
};

// Takes the DC out (a one-pole high pass at about 10 Hz): after an asymmetric shaper, before anything that measures the level.
struct DcBlock {
    float x1 = 0.f, y1 = 0.f;
    float run(float x) { const float y = x - x1 + 0.9987f * y1; x1 = x; y1 = y; return y; }
    void flush() { lab::flush(x1); lab::flush(y1); }
};

// The per-sample multiplier that falls by 1/e in `ms` (an exponential's speed).
inline float decay_coef(float ms, float sr) { return expf(-1000.f / (clampf(ms, 0.05f, 60000.f) * sr)); }

// ADSR, analog style: the attack rises towards 1.3 and stops at 1 (a curve, not a line), decay and release fall exponentially.
// set() once a block (times in ms, sustain 0..1); next() once a sample.
struct Adsr {
    enum Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float level = 0.f, ka = 0.f, kd = 0.f, kr = 0.f, sus = 1.f;
    void set(float a_ms, float d_ms, float s, float r_ms, float sr) {
        ka = 1.f - decay_coef(a_ms * 0.6f, sr); kd = decay_coef(d_ms * 0.3f, sr); kr = decay_coef(r_ms * 0.3f, sr); sus = clampf(s, 0.f, 1.f);
    }
    void gate_on() { stage = Attack; }                      // from where it is: a stolen voice does not click to zero
    void gate_off() { if (stage != Idle) stage = Release; }
    bool idle() const { return stage == Idle; }
    float next() {
        switch (stage) {
        case Attack: level += (1.3f - level) * ka; if (level >= 1.f) { level = 1.f; stage = Decay; } break;
        case Decay: level = sus + (level - sus) * kd; if (level - sus < 1e-4f) stage = Sustain; break;
        case Sustain: level = sus; break;
        case Release: level *= kr; if (level < 1e-4f) { level = 0.f; stage = Idle; } break;
        default: break;
        }
        return level;
    }
};

// A one-shot exponential decay from `level`: the percussive envelope. trigger() then next() once a sample.
struct Decay {
    float level = 0.f, k = 0.f;
    void set(float ms, float sr) { k = decay_coef(ms, sr); }
    void trigger(float from = 1.f) { level = from; }
    float next() { const float y = level; level *= k; return y; }
    bool done() const { return level < 1e-4f; }
    void flush() { lab::flush(level); }
};

// White noise, -1..1 (a linear congruential generator: fast, good enough to hear).
struct Noise {
    uint32_t s = 22222u;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(static_cast<int32_t>(s)) * (1.f / 2147483648.f); }
};

// A one-pole smoother, for values the encoder moves (a filter's cutoff that should glide, not step). set() once, next() a sample.
struct Smooth {
    float y = 0.f, k = 1.f;
    void set(float ms, float sr) { k = 1.f - decay_coef(ms, sr); }
    float next(float target) { y += (target - y) * k; return y; }
};

}  // namespace lab
