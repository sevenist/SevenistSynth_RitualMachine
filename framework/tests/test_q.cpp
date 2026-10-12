#include "test.h"
#include "engine/core/arena.h"
#include "engine/dsp/block.h"
#include "engine/dsp/q.h"

using namespace sc;

TEST(q15_saturation) {
    CHECK_EQ(sat16(40000), 32767);
    CHECK_EQ(sat16(-40000), -32768);
    CHECK_EQ(add15(30000, 30000), 32767);
    CHECK_EQ(sub15(-30000, 30000), -32768);
    CHECK_EQ(neg15(-32768), 32767);
    CHECK_EQ(abs15(-32768), 32767);
}

TEST(q15_mul_rounding_and_range) {
    CHECK_EQ(mul15(16384, 16384), 8192);          // 0.5 * 0.5 = 0.25
    CHECK_EQ(mul15(-32768, -32768), 32767);       // saturates instead of wrapping
    CHECK_EQ(mul15(32767, 32767), 32766);
    CHECK_EQ(mul15(1, 1), 0);
    CHECK_EQ(mul15(0, 12345), 0);
    // exhaustive-ish: result within 1 LSB of the ideal product
    for (int a = -32768; a < 32768; a += 257)
        for (int b = -32768; b < 32768; b += 263) {
            double ideal = (double)a * b / 32768.0;
            if (ideal > 32767) ideal = 32767;
            CHECK_NEAR(mul15((q15)a, (q15)b), ideal, 0.51);
        }
}

TEST(q31_mul_and_conversions) {
    CHECK_EQ(mul31(1 << 30, 1 << 30), 1 << 29);   // 0.5 * 0.5
    CHECK_EQ(mul31(kQ31Min, kQ31Min), kQ31Max);
    CHECK_EQ(to31(16384), 16384 << 16);
    CHECK_EQ(to15(to31(-12345)), -12345);          // round trip is exact
    CHECK_EQ(to15(kQ31Max), 32767);
    CHECK_EQ(mul31_15(1 << 30, 16384), 1 << 29);
}

TEST(mulq_general_formats) {
    // Q2.30 coefficient 1.5 times q31 state 0.25 -> 0.375
    q31 coef = (q31)(1.5 * (1 << 30)), state = (q31)(0.25 * 2147483648.0);
    CHECK_NEAR(q31_to_double(mulq(state, coef, 30)), 0.375, 1e-8);
    // result beyond range saturates
    CHECK_EQ(mulq(kQ31Max, (q31)(1.9 * (1 << 30)), 30), kQ31Max);
}

TEST(float_bridges) {
    CHECK_EQ(q15_from_float(0.5f), 16384);
    CHECK_EQ(q15_from_float(-1.0f), -32768);
    CHECK_EQ(q15_from_float(2.0f), 32767);
    CHECK_NEAR(q15_to_float(16384), 0.5, 1e-9);
    CHECK_NEAR(q31_to_double(q31_from_float(-0.25)), -0.25, 1e-9);
}

TEST(block_kernels) {
    q15 a[kBlock], b[kBlock], d[kBlock];
    for (int i = 0; i < kBlock; i++) { a[i] = (q15)(i * 100); b[i] = (q15)(-i * 50); }
    block_add(d, a, b);
    for (int i = 0; i < kBlock; i++) CHECK_EQ(d[i], add15(a[i], b[i]));
    block_gain(d, a, 16384);                       // x0.5
    for (int i = 0; i < kBlock; i++) CHECK_EQ(d[i], mul15(a[i], 16384));
    block_clear(d);
    block_mac(d, a, 8192);
    block_mac(d, a, 8192);
    for (int i = 0; i < kBlock; i++) CHECK_EQ(d[i], add15(mul15(a[i], 8192), mul15(a[i], 8192)));
    CHECK_EQ(block_peak(a), a[kBlock - 1]);
}

TEST(arena_alloc_alignment_and_exhaustion) {
    alignas(16) static uint8_t mem[256];
    Arena ar(mem, sizeof mem);
    uint8_t *p1 = (uint8_t *)ar.alloc(3, 1);
    int32_t *p2 = ar.alloc_array<int32_t>(4);
    CHECK(p1 != nullptr && p2 != nullptr);
    CHECK_EQ(((uintptr_t)p2) % alignof(int32_t), 0);
    for (int i = 0; i < 4; i++) CHECK_EQ(p2[i], 0);        // zero-filled
    size_t m = ar.mark();
    CHECK(ar.alloc(1000) == nullptr);
    CHECK(ar.failed());
    ar.reset();
    ar.alloc(100);
    ar.rewind(0);
    CHECK_EQ(ar.used(), 0);
    CHECK(ar.high_water() >= 100);
    (void)m;
}
