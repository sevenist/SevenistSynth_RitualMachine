#include <string>
#include <cstdlib>
#include "rig.h"
#include "platform/engine/dx7_convert.h"

using namespace sc;
using namespace tst;

namespace {
const double kPi = 3.14159265358979323846;

Dx7Patch blank_patch() {
    Dx7Patch p{};
    p.algorithm = 32;
    for (auto &o : p.op) {
        o.ratio_q16 = 65536;
        o.gain_q28 = 0;
        for (int i = 0; i < 4; i++) { o.eg_l[i] = 99; o.eg_ms[i] = 1; }
    }
    return p;
}

// NoteIn -> Dx7 -> VoiceOut, patch sent as a blob
struct FmBench {
    DspRig rig;
    FmBench() : rig(4) {
        GraphDesc g;
        rig.add(g, 1, T_NOTE_IN);
        rig.add(g, 2, T_DX7);
        rig.add(g, 3, T_VOICE_OUT)->param[VO_TAIL_MS] = 5000;
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(1, 1, 2, Dst::In, 1);
        g.connect(2, 0, 3, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
    }
    void patch(const Dx7Patch &p) { CHECK(rig.eng.set_blob(2, &p, sizeof p)); rig.run(1); }
};

double band_power(const std::vector<double> &x, double hz, double half_width_hz) {
    std::vector<double> w(x.size());
    for (size_t i = 0; i < x.size(); i++) w[i] = x[i] * (0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(x.size())));
    std::vector<double> p = dft_power(w);
    double s = 0;
    for (size_t k = 0; k < p.size(); k++) {
        double f = static_cast<double>(k) * kSampleRate / static_cast<double>(x.size());
        if (std::fabs(f - hz) <= half_width_hz) s += p[k];
    }
    return s;
}
}  // namespace

TEST(dx7_two_operator_fm_follows_bessel_theory) {
    FmBench b;
    Dx7Patch p = blank_patch();
    p.algorithm = 5;                                                   // operator 2 modulates operator 1 (and 4 -> 3, 6 -> 5)
    p.op[0].gain_q28 = 2 * (1 << 28);                                  // carrier, amplitude 2.0 (level 99)
    p.op[1].gain_q28 = static_cast<int32_t>(0.15915494 * 268435456.0); // modulator: peak 0.159 cycles = 1.0 rad of phase deviation
    p.op[1].ratio_q16 = 16384;                                         // 0.25 x the note: sidebands every 110 Hz at A4
    b.patch(p);
    b.rig.eng.note_on(69, 127);
    b.rig.run(DspRig::blocks_for(0.3));
    std::vector<double> y;
    b.rig.run((4096 + kBlock - 1) / kBlock, &y);
    y.resize(4096);
    const double bw = 2.2 * kSampleRate / 4096.0;                      // Hann main lobe
    double c = band_power(y, 440.0, bw), s1 = band_power(y, 550.0, bw) + band_power(y, 330.0, bw);
    double s2 = band_power(y, 660.0, bw) + band_power(y, 220.0, bw);
    // amplitudes: J0(1) = 0.765, J1(1) = 0.440 (both sidebands), J2(1) = 0.115
    double r1 = 10 * std::log10(s1 / (2 * c)), r2 = 10 * std::log10(s2 / (2 * c));
    std::printf("    sideband / carrier: first %.1f dB (theory %.1f), second %.1f dB (theory %.1f)\n", r1, 20 * std::log10(0.44 / 0.765), r2, 20 * std::log10(0.115 / 0.765));
    CHECK_NEAR(r1, 20 * std::log10(0.44 / 0.765), 1.5);
    CHECK_NEAR(r2, 20 * std::log10(0.115 / 0.765), 2.5);
    // level: carrier amplitude 2.0 x 1/4 x velocity 1 x J0 = 0.38 of full scale
    double peak = 0;
    for (double v : y) peak = std::fmax(peak, std::fabs(v));
    CHECK(peak > 0.45 * 32767 && peak < 0.75 * 32767);
}

TEST(dx7_feedback_makes_harmonics) {
    FmBench b;
    Dx7Patch p = blank_patch();
    p.algorithm = 32;                                                  // six carriers; operator 6 has the feedback
    p.op[5].gain_q28 = 2 * (1 << 28);
    auto harmonic_ratio = [&](int fb) {
        p.feedback_q15 = fb;
        b.patch(p);
        b.rig.eng.all_notes_off();
        b.rig.run(2);
        b.rig.eng.note_on(57, 127);
        b.rig.run(DspRig::blocks_for(0.3));
        std::vector<double> y;
        b.rig.run((4096 + kBlock - 1) / kBlock, &y);
        y.resize(4096);
        const double f0 = 220.0, bw = 2.2 * kSampleRate / 4096.0;
        return 10 * std::log10(band_power(y, 2 * f0, bw) / band_power(y, f0, bw) + 1e-12);
    };
    double none = harmonic_ratio(0), full = harmonic_ratio(32767);
    std::printf("    second harmonic re fundamental: feedback 0 -> %.1f dB, feedback 1 -> %.1f dB\n", none, full);
    CHECK(none < -50.0);
    CHECK(full > -9.0);
}

TEST(dx7_envelope_moves_in_the_log_domain_and_releases) {
    FmBench b;
    Dx7Patch p = blank_patch();
    p.op[0].gain_q28 = 2 * (1 << 28);
    p.op[0].eg_l[3] = 0;   p.op[0].eg_ms[3] = 150;                      // release to 0 in 150 ms; also the start level
    p.op[0].eg_l[0] = 99;  p.op[0].eg_ms[0] = 100;                      // attack to full in 100 ms
    p.op[0].eg_l[1] = 60;  p.op[0].eg_ms[1] = 200;                      // decay
    p.op[0].eg_l[2] = 60;  p.op[0].eg_ms[2] = 100;                      // sustain level
    b.patch(p);
    b.rig.eng.note_on(69, 127);
    std::vector<double> y;
    b.rig.run(DspRig::blocks_for(1.0), &y);
    auto amp_at = [&](double sec) {                                    // rms over 5 ms
        size_t a = static_cast<size_t>(sec * kSampleRate), n = static_cast<size_t>(0.005 * kSampleRate);
        double s = 0;
        for (size_t i = a; i < a + n; i++) s += y[i] * y[i];
        return std::sqrt(s / static_cast<double>(n)) * 1.4142;
    };
    // the DX7 level of the envelope at a time: attack 0 -> 99 in 100 ms, decay 99 -> 60 in 200 ms, then sustain
    auto level_at = [](double t) { return t < 0.1 ? 99.0 * t / 0.1 : (t < 0.3 ? 99.0 - 39.0 * (t - 0.1) / 0.2 : 60.0); };
    auto db_between = [&](double t1, double t2) { return 20 * std::log10(amp_at(t1) / amp_at(t2)); };
    auto theory = [&](double t1, double t2) { return (level_at(t1 + 0.0025) - level_at(t2 + 0.0025)) / 8.0 * 6.0206; };   // 6 dB per 8 levels, at the window centres
    std::printf("    t=95ms vs t=50ms: %.1f dB (theory %.1f); t=95ms vs sustain: %.1f dB (theory %.1f)\n",
                db_between(0.095, 0.05), theory(0.095, 0.05), db_between(0.095, 0.8), theory(0.095, 0.8));
    CHECK_NEAR(db_between(0.095, 0.05), theory(0.095, 0.05), 2.5);       // amplitude doubles every 8 levels: log-domain interpolation
    CHECK_NEAR(db_between(0.095, 0.8), theory(0.095, 0.8), kBlock >= 256 ? 2.5 : 1.5);   // the envelope moves once per block: coarse at 5 ms blocks
    const double sus = amp_at(0.8);

    b.rig.eng.note_off(69);
    std::vector<double> r;
    b.rig.run(DspRig::blocks_for(0.4), &r);
    auto ramp_amp = [&](double sec) {
        size_t a = static_cast<size_t>(sec * kSampleRate), n = static_cast<size_t>(0.004 * kSampleRate);
        double s = 0;
        for (size_t i = a; i < a + n; i++) s += r[i] * r[i];
        return std::sqrt(s / static_cast<double>(n));
    };
    CHECK(ramp_amp(0.02) < sus * 0.8);
    CHECK(ramp_amp(0.3) < 3.0);                                         // released: silent after 150 ms
}

TEST(dx7_all_algorithms_make_sound_and_stay_in_range) {
    FmBench b;
    for (int algo = 1; algo <= 32; algo++) {
        Dx7Patch p = blank_patch();
        p.algorithm = static_cast<uint8_t>(algo);
        p.feedback_q15 = 12000;
        for (int k = 0; k < kDx7Ops; k++) { p.op[k].gain_q28 = 1 << 27; p.op[k].ratio_q16 = (k + 1) * 65536 / 2 + 32768; }
        b.patch(p);
        b.rig.eng.all_notes_off();
        b.rig.run(2);
        b.rig.eng.note_on(60, 100);
        std::vector<double> y;
        b.rig.run(DspRig::blocks_for(0.25), &y);
        double pk = 0;
        for (double v : y) pk = std::fmax(pk, std::fabs(v));
        CHECK(pk > 500.0);
        CHECK(pk <= 32767.0);
    }
}

TEST(dx7_factory_patches_play_and_set_the_loudness_trim) {
    FmBench b;
    const char *out = std::getenv("DX7_GAIN_OUT");
    std::string table;
    int silent = 0, loud = 0;
    for (int n = 0; n < DX7_FACTORY_COUNT; n++) {
        dx7_patch_t cp;
        dx7_load_factory(&cp, n);
        Dx7Patch p = dx7_convert(cp);
        b.patch(p);
        b.rig.eng.all_notes_off();
        b.rig.run(DspRig::blocks_for(0.1));
        b.rig.eng.set_param(2, DX7_GAIN, 8192);
        b.rig.eng.note_on(57, 127);
        // loudest 230 ms window of the first 4 s (slow attacks swell late), like the AMY-era measuring tool
        std::vector<double> y;
        b.rig.run(DspRig::blocks_for(4.0), &y);
        const size_t w = static_cast<size_t>(0.23 * kSampleRate);
        double best = 0, peak = 0;
        for (size_t i = 0; i + w < y.size(); i += w / 2) {
            double s = 0;
            for (size_t k = 0; k < w; k++) s += y[i + k] * y[i + k];
            best = std::fmax(best, std::sqrt(s / static_cast<double>(w)));
        }
        for (double v : y) peak = std::fmax(peak, std::fabs(v));
        if (best < 20.0) silent++;
        if (peak >= 32767.0) loud++;
        double gain = best > 1.0 ? 9000.0 / best : 4.0;                // target: rms 9000 (-11 dBFS) at the voice, before the voice-out level
        if (peak * gain > 28000.0) gain = 28000.0 / peak;              // and never a louder peak than 28000
        if (gain > 4.0) gain = 4.0;                                    // the module's trim goes up to 4x
        char buf[32];
        std::snprintf(buf, sizeof buf, "%s%.3ff,", n % 8 == 0 ? "\n    " : " ", gain);
        table += buf;
    }
    std::printf("    128 factory patches: %d silent, %d reach full scale before trimming\n", silent, loud);
    CHECK(silent <= 4);
    if (out) {
        FILE *f = std::fopen(out, "w");
        std::fprintf(f, "#pragma once\n// GENERATED by tests/engine/test_dx7.cpp (DX7_GAIN_OUT=...) -- do not edit by hand.\n"
                        "// Per-patch loudness trim for the engine's DX7 voice (index = factory patch), times 8192 gives the DX7_GAIN parameter.\n"
                        "static const float fm_patch_gain[128] = {%s\n};\n", table.c_str());
        std::fclose(f);
    }
}
