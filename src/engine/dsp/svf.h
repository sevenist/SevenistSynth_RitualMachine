#pragma once
// Zero-delay-feedback (TPT) state-variable filter, fixed point (ADR-014). One 2-pole section gives LP, BP,
// HP and notch from the same two integrators; N sections cascade into 12..48 dB/oct.
//
// Number formats:
//   signal / state   int32 Q28 (1.0 = 2^28, range +-8: resonance headroom; beyond that the state saturates,
//                    which acts as a limiter instead of wrapping)
//   g = tan(pi fc/fs) unsigned Q4.28   (table, no float)
//   k = 1/Q          Q3.29, k in (0, 4)
//   a1,a2,a3         Q31 (all < 1 for g > 0, k >= 0.25)
//   1/(1 + g(g+k)) uses a division-free Newton reciprocal, so a coefficient update costs about 25 multiplies
//   and can run per sample when the cutoff is audio-rate modulated.
#include <cstdint>
#include "engine/dsp/q.h"
#include "engine/dsp/tables.h"

namespace sc {

constexpr uint32_t kSvfMaxInc = 1932735283u;        // 0.45 * 2^32: highest cutoff (fc = 0.45 fs)
constexpr int kSvfHeadroomBits = 3;                  // Q28 signal: 3 bits above full scale

constexpr int32_t svf_in(q15 x) { return static_cast<int32_t>(x) * (1 << 13); }                 // q15 -> Q28
inline q15 svf_out(int32_t y) { int32_t v = (y + (1 << 12)) >> 13; return sat16(v); }             // Q28 -> q15

// g = tan(pi * fc / fs) from the cutoff phase increment (fc/fs * 2^32). Table + linear interpolation.
inline uint32_t svf_g(uint32_t inc) {
    if (inc > kSvfMaxInc) inc = kSvfMaxInc;
    uint32_t idx = inc >> 23;
    uint32_t frac = (inc >> 7) & 0xFFFFu;
    uint32_t a = kTanTab[idx], b = kTanTab[idx + 1];
    return a + static_cast<uint32_t>((static_cast<uint64_t>(b - a) * frac) >> 16);
}

// 1/D for D = d / 2^26 in [1, 64): returns Q31 (clamped to 2^31 - 1). Newton-Raphson, no division.
inline uint32_t svf_recip_q26(uint32_t d) {
    int n = __builtin_clz(d);
    uint32_t dn = d << n;                                            // [2^31, 2^32): M = dn / 2^32 in [0.5, 1)
    uint64_t r = 0xB4B4B4B4ull - (((0x78787878ull) * dn) >> 32);     // 48/17 - 32/17 M in Q30 (constants in Q30)
    for (int i = 0; i < 3; i++) {
        uint64_t e = (static_cast<uint64_t>(dn) * r) >> 32;          // M * R in Q30 (about 1.0)
        uint64_t t = (1ull << 31) - e;                               // 2 - M R in Q30
        r = (r * t) >> 30;
    }
    int sh = n - 5;                                                  // 1/D = R * 2^(n-6), R in Q30 -> Q31
    uint64_t a = sh >= 0 ? (r << sh) : (r >> -sh);
    return a > 0x7FFFFFFFull ? 0x7FFFFFFFu : static_cast<uint32_t>(a);
}

struct SvfCoef { int32_t a1 = 0x7FFFFFFF, a2 = 0, a3 = 0; int32_t k = 1 << 29; };

inline SvfCoef svf_coef(uint32_t g, int32_t k) {
    SvfCoef c;
    uint32_t g2 = static_cast<uint32_t>((static_cast<uint64_t>(g) * g) >> 30);            // Q26
    uint32_t gk = static_cast<uint32_t>((static_cast<uint64_t>(g) * static_cast<uint32_t>(k)) >> 31);   // Q26
    uint32_t d = (1u << 26) + g2 + gk;
    uint32_t a1 = svf_recip_q26(d);
    c.a1 = static_cast<int32_t>(a1);
    c.a2 = static_cast<int32_t>((static_cast<uint64_t>(g) * a1) >> 28);
    c.a3 = static_cast<int32_t>((static_cast<uint64_t>(g) * static_cast<uint32_t>(c.a2)) >> 28);
    c.k = k;
    return c;
}

struct SvfState { int32_t ic1 = 0, ic2 = 0; };

constexpr int32_t sat_q28(int64_t v) {
    return static_cast<int32_t>(v > 0x7FFFFFFFLL ? 0x7FFFFFFFLL : (v < -0x80000000LL ? -0x80000000LL : v));
}

// One sample. x, lp, bp, hp are Q28. [AUDIO]
inline void svf_tick(const SvfCoef &c, SvfState &s, int32_t x, int32_t &lp, int32_t &bp, int32_t &hp) {
    const int64_t v3 = static_cast<int64_t>(x) - s.ic2;
    const int64_t v1 = ((static_cast<int64_t>(c.a1) * s.ic1 >> 1) + (static_cast<int64_t>(c.a2) * v3 >> 1)) >> 30;
    const int64_t v2 = s.ic2 + (((static_cast<int64_t>(c.a2) * s.ic1 >> 1) + (static_cast<int64_t>(c.a3) * v3 >> 1)) >> 30);
    s.ic1 = sat_q28(2 * v1 - s.ic1);
    s.ic2 = sat_q28(2 * v2 - s.ic2);
    lp = sat_q28(v2);
    bp = sat_q28(v1);
    hp = sat_q28(x - ((static_cast<int64_t>(c.k) * v1) >> 29) - v2);
}

}  // namespace sc
