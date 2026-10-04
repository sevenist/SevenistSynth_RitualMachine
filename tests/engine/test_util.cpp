#include "test.h"
#include "engine/dsp/interp.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/smooth.h"
#include "engine/dsp/util.h"

using namespace sc;

TEST(sine_table_accuracy) {
    double worst = 0;
    for (uint64_t p = 0; p < 4294967296ull; p += 4099) {
        double ref = std::sin(2.0 * 3.14159265358979323846 * (double)p / 4294967296.0) * 32767.0;
        double e = std::fabs((double)sine((uint32_t)p) - ref);
        if (e > worst) worst = e;
    }
    CHECK(worst < 2.0);                        // LSBs of q15
    CHECK_EQ(sine(0), 0);
    CHECK_EQ(sine(0x40000000u), 32767);        // quarter cycle = +1
    CHECK_EQ(sine(0xC0000000u), -32767);
}

TEST(sine_thd_below_minus_80db) {
    const int N = 1 << 14;                     // coherent: exactly 37 cycles
    std::vector<double> ref(N), out(N);
    uint32_t inc = (uint32_t)(37ull * 4294967296ull / N);
    uint32_t ph = 0;
    for (int i = 0; i < N; i++, ph += inc) {
        ref[i] = std::sin(2.0 * 3.14159265358979323846 * 37.0 * i / N) * 32767.0;
        out[i] = sine(ph);
    }
    CHECK(tst::snr_db(ref, out) > 80.0);
}

TEST(exp2_scale_accuracy_in_cents) {
    double worst = 0;
    for (int32_t x = -(3 << 16); x <= (3 << 16); x += 997) {
        uint32_t base = 1u << 24;
        double ref = (double)base * std::exp2((double)x / 65536.0);
        double got = (double)exp2_scale(base, x);
        double cents = std::fabs(1200.0 * std::log2(got / ref));
        if (cents > worst) worst = cents;
    }
    CHECK(worst < 0.2);
    CHECK_EQ(exp2_scale(1000u, 0), 1000u);
    CHECK_EQ(exp2_scale(1000u, 1 << 16), 2000u);
    CHECK_EQ(exp2_scale(1000u, -(1 << 16)), 500u);
    CHECK_EQ(exp2_scale(0xF0000000u, 8 << 16), 0xFFFFFFFFu);        // saturates
    CHECK_EQ(exp2_scale(1000u, -(40 << 16)), 0u);
}

TEST(pitch_to_inc_matches_equal_temperament) {
    uint32_t a4 = inc_a4();
    CHECK_NEAR(inc_to_hz(pitch_to_inc(kPitchA4, a4)), 440.0, 0.05);
    CHECK_NEAR(inc_to_hz(pitch_to_inc(kPitchA4 + 12 * kPitchUnit, a4)), 880.0, 0.1);
    CHECK_NEAR(inc_to_hz(pitch_to_inc(60 * kPitchUnit, a4)), 261.6256, 0.05);   // C4
    CHECK_NEAR(inc_to_hz(pitch_to_inc(kPitchA4 + 128, a4)), 440.0 * std::exp2(0.5 / 12.0), 0.1);   // quarter-tone
    CHECK(pitch_to_inc(127 * 256 + 255 * 20, a4) <= 2147483648u);               // never above Nyquist
    CHECK(pitch_to_inc(-100000, a4) < 10u);                                     // far below audio: ~0
}

TEST(lerp_and_hermite) {
    CHECK_EQ(lerp15(0, 1000, 0), 0);
    CHECK_EQ(lerp15(0, 1000, 32768), 500);
    CHECK_EQ(lerp15(-1000, 1000, 16384), -500);
    // Hermite passes through its two inner points and reproduces a straight line exactly
    CHECK_EQ(hermite15(100, 200, 300, 400, 0), 200);
    CHECK_NEAR(hermite15(100, 200, 300, 400, 65535), 300, 1);
    CHECK_NEAR(hermite15(100, 200, 300, 400, 32768), 250, 1);
    // better than linear on a sine: interpolate a 1/8-rate sine at fractional positions
    double eh = 0, el = 0;
    for (int i = 4; i < 200; i++) {
        double t = i + 0.37;
        q15 y[4];
        for (int k = 0; k < 4; k++) y[k] = (q15)std::lround(std::sin(2 * 3.14159265358979 * (i - 1 + k) / 16.0) * 20000.0);
        double ref = std::sin(2 * 3.14159265358979 * t / 16.0) * 20000.0;
        uint32_t fr = (uint32_t)(0.37 * 65536);
        eh = std::fmax(eh, std::fabs(hermite15(y[0], y[1], y[2], y[3], fr) - ref));
        el = std::fmax(el, std::fabs(lerp15(y[1], y[2], fr) - ref));
    }
    CHECK(eh < el * 0.35);
}

TEST(ramp_reaches_target_exactly) {
    Ramp r;
    r.reset(0);
    r.set_target(q31_from_float(0.5), 32);
    q31 last = 0;
    bool monotone = true;
    for (int i = 0; i < 32; i++) { q31 v = r.next(); if (v < last) monotone = false; last = v; }
    CHECK(monotone);
    CHECK_EQ(last, q31_from_float(0.5));
    CHECK(!r.active());
    r.set_target(q31_from_float(-0.5), 16);              // downward and retarget from a value
    for (int i = 0; i < 16; i++) r.next();
    CHECK_EQ(r.value(), q31_from_float(-0.5));
    r.set_target(123, 0);                                // immediate
    CHECK_EQ(r.value(), 123);
}

TEST(onepole_converges_with_expected_time_constant) {
    OnePole p;
    p.reset(0);
    p.set_coef(OnePole::coef_from_ms(10.0f));
    q31 target = q31_from_float(0.8);
    int n63 = -1;
    for (int i = 0; i < kSampleRate; i++) {
        q31 y = p.next(target);
        if (n63 < 0 && y >= (q31)(0.632 * target)) n63 = i + 1;
    }
    CHECK_NEAR(n63, 0.010 * kSampleRate, 0.1 * 0.010 * kSampleRate);
    CHECK_EQ(p.value(), target);                         // snapped, no residual error
}

TEST(noise_statistics) {
    Noise n(42);
    double sum = 0, sq = 0;
    const int N = 200000;
    for (int i = 0; i < N; i++) { double v = n.next() / 32768.0; sum += v; sq += v * v; }
    CHECK_NEAR(sum / N, 0.0, 0.01);
    CHECK_NEAR(std::sqrt(sq / N), 0.5774, 0.01);         // uniform in [-1,1): 1/sqrt(3)
}

TEST(dc_blocker_removes_dc_keeps_tone) {
    DcBlocker dc;
    dc.set_corner(20.0f);
    q15 y = 0;
    for (int i = 0; i < kSampleRate; i++) y = dc.process(16384);     // constant 0.5
    CHECK(std::abs((int)y) < 40);
    // a 1 kHz sine passes with less than 0.1 dB loss
    DcBlocker d2;
    d2.set_corner(20.0f);
    uint32_t ph = 0, inc = hz_to_inc(1000.0f);
    double pin = 0, pout = 0;
    for (int i = 0; i < kSampleRate / 2; i++, ph += inc) {
        q15 x = (q15)(sine(ph) / 2);
        q15 o = d2.process(x);
        if (i > kSampleRate / 4) { pin += (double)x * x; pout += (double)o * o; }
    }
    CHECK_NEAR(10.0 * std::log10(pout / pin), 0.0, 0.1);
}

TEST(softclip_bounded_monotone_odd) {
    int prev = -40000;
    for (int x = -32768; x <= 32767; x++) {
        int y = softclip15((q15)x);
        CHECK(y >= prev);
        prev = y;
        if (x > -32768) CHECK_NEAR(softclip15((q15)-x), -y, 1);     // odd symmetry
    }
    CHECK_EQ(softclip15(0), 0);
    CHECK(softclip15(32767) >= 32766);
    CHECK_NEAR(softclip15(1000), 1500, 3);                          // small signal gain 1.5
}
