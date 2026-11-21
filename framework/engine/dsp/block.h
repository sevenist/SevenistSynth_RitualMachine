#pragma once
// Block (vector) kernels on q15 buffers. Portable reference implementations: the loops are written so
// that a platform layer can later replace them with SIMD (ESP32-S3 PIE / esp-dsp) behind the same names.
// All buffers are kBlock frames unless `n` is given. dst may alias a source (in-place is allowed).
#include "engine/dsp/config.h"
#include "engine/dsp/q.h"

namespace sc {

// dst[0 .. n) = v: two samples a 32-bit store when dst is 4-byte aligned (the plan's blocks are: 16), else one at a time. The same values
// either way; the 32-bit word through a may_alias type (it writes q15 storage).
typedef uint32_t __attribute__((may_alias)) q15_pair_t;
SC_INLINE bool pairs_ok(const void *p, int n) { return ((reinterpret_cast<uintptr_t>(p) & 3u) | (static_cast<unsigned>(n) & 1u)) == 0; }
SC_INLINE uint32_t q15_pair(q15 v) { return static_cast<uint32_t>(static_cast<uint16_t>(v)) * 0x00010001u; }
SC_INLINE void block_fill(q15 *dst, q15 v, int n = kBlock) {
    if (pairs_ok(dst, n)) {
        const uint32_t w = q15_pair(v);
        q15_pair_t *d = reinterpret_cast<q15_pair_t *>(dst);
        for (int k = 0; k < n / 2; k++) d[k] = w;
        return;
    }
    for (int i = 0; i < n; i++) dst[i] = v;
}
SC_INLINE void block_clear(q15 *dst, int n = kBlock) { block_fill(dst, 0, n); }
inline void block_copy(q15 *dst, const q15 *src, int n = kBlock) { for (int i = 0; i < n; i++) dst[i] = src[i]; }

inline void block_add(q15 *dst, const q15 *a, const q15 *b, int n = kBlock) { for (int i = 0; i < n; i++) dst[i] = add15(a[i], b[i]); }
inline void block_mul(q15 *dst, const q15 *a, const q15 *b, int n = kBlock) { for (int i = 0; i < n; i++) dst[i] = mul15(a[i], b[i]); }

// dst += src (mixing bus accumulate)
inline void block_acc(q15 *dst, const q15 *src, int n = kBlock) { for (int i = 0; i < n; i++) dst[i] = add15(dst[i], src[i]); }

// dst = src * g (constant gain, q15)
inline void block_gain(q15 *dst, const q15 *src, q15 g, int n = kBlock) { for (int i = 0; i < n; i++) dst[i] = mul15(src[i], g); }

// dst += src * g
inline void block_mac(q15 *dst, const q15 *src, q15 g, int n = kBlock) { for (int i = 0; i < n; i++) dst[i] = add15(dst[i], mul15(src[i], g)); }

// The two signal types (ADR-050 step 2, module.h Sig): float = q15 / 32768. q15 -> float is exact; float -> q15 rounds to the nearest
// (ties to even) and clamps, so a q15 value that went through float comes back bit for bit. Single precision only.
//   f_to_q15_c / q15_to_f_c   the portable ones (the desktop, the tests; NaN -> 0)
//   f_to_q15 / q15_to_f       the ones the engine uses: on the ESP32-S3 one FPU instruction each, FLOAT.S and ROUND.S with their
//                             scale of 2^15 (then CLAMPS to 16 bits), no multiply and no branch; elsewhere the portable ones.
// (HWV1, a 32-frame block: float -> q15 1152 cycles with branches, 717 as compiled C; q15 -> float 335 as FLOAT.S + MUL.S.)
constexpr float kQ15ToF = 1.0f / 32768.0f;
inline float q15_to_f_c(q15 x) { return static_cast<float>(x) * kQ15ToF; }
inline q15 f_to_q15_c(float x) {
    float v = x * 32768.0f;
    if (!(v == v)) return 0;
    v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
    // to the nearest, ties to even, without <cmath>'s rounding mode: |v| < 2^15, so adding and taking away 2^23 rounds it in float
    const float big = 8388608.0f;
    const volatile float r = v >= 0.0f ? (v + big) - big : (v - big) + big;
    return static_cast<q15>(static_cast<int32_t>(r));
}
#if defined(__XTENSA__) && !defined(__XTENSA_SOFT_FLOAT__)
inline float q15_to_f(q15 x) {
    float f;
    const int32_t v = x;
    __asm__("float.s %0, %1, 15" : "=f"(f) : "r"(v));
    return f;
}
inline q15 f_to_q15(float x) {      // (ROUND.S gives full scale for NaN: OEQ.S / MOVF make it 0, as the portable one)
    int32_t v;
    const int32_t zero = 0;
    __asm__("round.s %0, %1, 15\n\toeq.s b0, %1, %1\n\tmovf %0, %2, b0\n\tclamps %0, %0, 15" : "=&r"(v) : "f"(x), "r"(zero) : "b0");
    return static_cast<q15>(v);
}
// The blocks, each loop whole in assembly: the zero-overhead LOOP (the compiler gives it up around an inline instruction), 5 and 8
// instructions a sample. HWV1, 32 frames: see the bench's "glue engine-ToF / engine-ToQ" (apps/synthlab).
inline void block_q15_to_f(float *dst, const q15 *src, int n = kBlock) {
    int32_t t;
    float f;
    __asm__ volatile("loopgtz %[n], 1f\n\t"
                     "l16si %[t], %[s], 0\n\t"
                     "addi %[s], %[s], 2\n\t"
                     "float.s %[f], %[t], 15\n\t"
                     "ssi %[f], %[d], 0\n\t"
                     "addi %[d], %[d], 4\n"
                     "1:"
                     : [d] "+r"(dst), [s] "+r"(src), [t] "=&r"(t), [f] "=&f"(f)
                     : [n] "r"(n)
                     : "memory");
}
inline void block_f_to_q15(q15 *dst, const float *src, int n = kBlock) {
    int32_t t;
    float f;
    const int32_t zero = 0;
    __asm__ volatile("loopgtz %[n], 1f\n\t"
                     "lsi %[f], %[s], 0\n\t"
                     "addi %[s], %[s], 4\n\t"
                     "round.s %[t], %[f], 15\n\t"
                     "oeq.s b0, %[f], %[f]\n\t"
                     "movf %[t], %[z], b0\n\t"
                     "clamps %[t], %[t], 15\n\t"
                     "s16i %[t], %[d], 0\n\t"
                     "addi %[d], %[d], 2\n"
                     "1:"
                     : [d] "+r"(dst), [s] "+r"(src), [t] "=&r"(t), [f] "=&f"(f)
                     : [n] "r"(n), [z] "r"(zero)
                     : "b0", "memory");
}
#else
inline float q15_to_f(q15 x) { return q15_to_f_c(x); }
inline q15 f_to_q15(float x) { return f_to_q15_c(x); }
inline void block_q15_to_f(float *dst, const q15 *src, int n = kBlock) { for (int i = 0; i < n; i++) dst[i] = q15_to_f(src[i]); }
inline void block_f_to_q15(q15 *dst, const float *src, int n = kBlock) { for (int i = 0; i < n; i++) dst[i] = f_to_q15(src[i]); }
#endif
inline void block_copy_f(float *dst, const float *src, int n = kBlock) { for (int i = 0; i < n; i++) dst[i] = src[i]; }

// Peak absolute value (metering, tests).
inline q15 block_peak(const q15 *src, int n = kBlock) {
    int32_t p = 0;
    for (int i = 0; i < n; i++) { int32_t v = src[i] < 0 ? -static_cast<int32_t>(src[i]) : src[i]; if (v > p) p = v; }
    return sat16(p);
}

}  // namespace sc
