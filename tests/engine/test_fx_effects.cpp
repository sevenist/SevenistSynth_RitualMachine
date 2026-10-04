// The newer effects: phaser, flanger, tremolo / auto-pan, compressor, EQ, ring modulator / frequency shifter, convolver, comb.
#include <algorithm>
#include <vector>
#include "rig.h"

using namespace tst;

namespace {
const double kTwoPi = 6.28318530717958647692;

double power_at(const std::vector<double> &x, size_t from, size_t to, double hz) {
    double re = 0, im = 0;
    for (size_t i = from; i < to && i < x.size(); i++) { const double ph = kTwoPi * hz * static_cast<double>(i) / kSampleRate; re += x[i] * std::cos(ph); im += x[i] * std::sin(ph); }
    const double n = static_cast<double>(to - from);
    return (re * re + im * im) / (n * n);
}

// source (global osc: sine at hz, or noise) -> effect -> master
struct FxBench {
    DspRig rig;
    int fx_id = 2;
    explicit FxBench(int type, bool noise, double hz = 1000.0, double level = 0.5) {
        GraphDesc g;
        NodeDesc *o = rig.add(g, 1, T_OSC_G);
        o->param[OSC_WAVE] = noise ? WAVE_NOISE_ : WAVE_SINE_; o->param[OSC_PITCH] = hz_to_pitch(hz); o->param[OSC_LEVEL] = static_cast<int32_t>(level * 32767);
        rig.add(g, 2, type);
        rig.add(g, 3, T_MASTER_OUT);
        g.connect(1, 0, 2, Dst::In, 0); g.connect(1, 0, 2, Dst::In, 1);
        g.connect(2, 0, 3, Dst::In, 0); g.connect(2, 1, 3, Dst::In, 1);
        CHECK(rig.eng.load(g) == Err::Ok);
    }
    void set(int idx, int v) { rig.eng.set_param(fx_id, idx, v); }
    void run(double s, std::vector<double> *l, std::vector<double> *r = nullptr) { rig.run2(DspRig::blocks_for(s), l, r); }
};

double band_db(const std::vector<double> &x, size_t from, size_t n, double f0, double f1) {      // mean power in a band, dB
    std::vector<double> w(x.begin() + static_cast<long>(from), x.begin() + static_cast<long>(from + n));
    std::vector<double> p = dft_power(w);
    double s = 0;
    int c = 0;
    for (size_t k = 1; k < p.size(); k++) { const double f = static_cast<double>(k) * kSampleRate / static_cast<double>(n); if (f >= f0 && f < f1) { s += p[k]; c++; } }
    return 10.0 * std::log10(s / std::max(c, 1) + 1e-12);
}
size_t S(double seconds) { return static_cast<size_t>(seconds * kSampleRate + 0.5); }
double rms_w(const std::vector<double> &x, size_t a, size_t b) { return rms(std::vector<double>(x.begin() + static_cast<long>(a), x.begin() + static_cast<long>(b))); }
}  // namespace

TEST(phaser_cancels_at_its_center_when_mixed_half_and_wet) {
    // six all-pass stages turn the phase by 540 degrees at the center frequency: the wet signal is the inverse of the dry one, so a 50 % mix cancels there
    auto level = [](int mix, int stages, double hz) {
        FxBench b(T_PHASER, false, hz, 0.5);
        b.set(PHS_DEPTH, 0); b.set(PHS_FEEDBACK, 0); b.set(PHS_MIX, mix); b.set(PHS_STAGES, stages); b.set(PHS_CENTER, 79 * 256); b.set(PHS_RATE, -6 * 12 * 256);
        std::vector<double> l;
        b.run(0.4, &l);
        return rms_w(l, S(0.2), S(0.3));
    };
    const double dry = level(0, 3, 784.0), half = level(16384, 3, 784.0), wet = level(32767, 3, 784.0), off = level(16384, 3, 100.0);
    std::printf("    phaser at its center (784 Hz): dry %.0f, 50 %% mix %.0f, wet only %.0f; 100 Hz at 50 %% mix %.0f\n", dry, half, wet, off);
    CHECK(half < 0.2 * dry);                                                       // the notch
    CHECK(wet > 0.9 * dry && wet < 1.1 * dry);                                     // an all-pass keeps the level
    CHECK(off > 0.6 * dry);                                                        // well below the center the phase has turned only a little: the notch is far away
}

TEST(flanger_adds_comb_notches_at_the_delay) {
    FxBench b(T_FLANGER, true);
    b.set(FLG_DEPTH, 0); b.set(FLG_DELAY, 32); b.set(FLG_FEEDBACK, 0); b.set(FLG_MIX, 32767);   // 2 ms: notches at 250, 750 ... peaks at 500, 1000 ...
    std::vector<double> l;
    b.run(0.6, &l);
    const double notch = band_db(l, 8192, 4096, 720, 780), peak = band_db(l, 8192, 4096, 470, 530);
    std::printf("    flanger: band at 750 Hz %.1f dB, at 500 Hz %.1f dB\n", notch, peak);
    CHECK(peak - notch > 10.0);
}

TEST(tremolo_and_autopan_modulate_the_level) {
    FxBench t(T_TREMOLO, false, 440.0, 0.5);
    t.set(TRM_RATE, static_cast<int>(12 * 256 * std::log2(5.0))); t.set(TRM_DEPTH, 32767); t.set(TRM_SHAPE, 0); t.set(TRM_MODE, 0);
    std::vector<double> l;
    t.run(1.0, &l);
    double lo = 1e9, hi = 0;
    for (size_t at = 4800; at + 960 < l.size(); at += 960) { const double v = rms_w(l, at, at + 960); lo = std::fmin(lo, v); hi = std::fmax(hi, v); }
    std::printf("    tremolo at 5 Hz, depth 100 %%: window rms %.0f .. %.0f\n", lo, hi);
    CHECK(hi > 8.0 * (lo + 1.0));
    FxBench a(T_TREMOLO, false, 440.0, 0.5);
    a.set(TRM_RATE, static_cast<int>(12 * 256 * std::log2(2.0))); a.set(TRM_DEPTH, 32767); a.set(TRM_MODE, 1);
    std::vector<double> al, ar;
    a.run(1.0, &al, &ar);
    int opposite = 0, total = 0;
    for (size_t at = 4800; at + 960 < al.size(); at += 960) { const double x = rms_w(al, at, at + 960), y = rms_w(ar, at, at + 960); total++; opposite += (x > 2 * y) || (y > 2 * x); }
    CHECK(opposite > total / 3);                                                 // while one side is loud the other is quiet (auto-pan)
}

TEST(compressor_reduces_loud_signals_by_the_ratio_and_leaves_quiet_ones) {
    auto level_out = [](double amp, int thr, int ratio10, int makeup) {
        FxBench b(T_COMP, false, 1000.0, amp);
        b.set(CMP_THRESH, thr); b.set(CMP_RATIO, ratio10); b.set(CMP_ATTACK, 2); b.set(CMP_RELEASE, 100); b.set(CMP_MAKEUP, makeup); b.set(CMP_MIX, 32767);
        std::vector<double> l;
        b.run(0.8, &l);
        return 20.0 * std::log10(rms_w(l, S(0.5), S(0.7)) / (amp * 32767.0 / std::sqrt(2.0)));
    };
    const double loud = level_out(0.9, -20, 40, 0), quiet = level_out(0.03, -20, 40, 0), made = level_out(0.9, -20, 40, 6);
    // 0.9 amplitude = -0.9 dB peak: 19.1 dB over the threshold, 4:1 -> 14.3 dB of reduction
    std::printf("    compressor 4:1 at -20 dB: loud signal %.1f dB (expect about -14), quiet signal %.1f dB, with 6 dB makeup %.1f dB\n", loud, quiet, made);
    CHECK(loud < -11.0 && loud > -17.0);
    CHECK(quiet > -1.0 && quiet < 1.0);
    CHECK_NEAR(made - loud, 6.0, 1.0);
}

TEST(eq3_shelves_and_peak_move_the_bands) {
    // level change of a sine at `hz` for an EQ setting, in dB
    auto gain_db = [](double hz, int low, int mid, int midf, int high) {
        FxBench b(T_EQ3, false, hz, 0.3);
        b.set(EQ_LOW, low); b.set(EQ_MID, mid); b.set(EQ_MIDF, midf); b.set(EQ_HIGH, high);
        std::vector<double> l;
        b.run(0.5, &l);
        return 20.0 * std::log10(rms_w(l, S(0.3), S(0.4)) / (0.3 * 32767.0 / std::sqrt(2.0)));
    };
    const double low = gain_db(40.0, -120, 0, 84 * 256, 0), high = gain_db(14000.0, 0, 0, 84 * 256, 120), mid_untouched = gain_db(1000.0, -120, 0, 84 * 256, 120);
    const double peak = gain_db(1000.0, 0, 120, 83 * 256 + 100, 0), cut = gain_db(1000.0, 0, -120, 83 * 256 + 100, 0), flat = gain_db(1000.0, 0, 0, 84 * 256, 0);
    std::printf("    eq: low shelf -12 dB at 40 Hz -> %.1f, high shelf +12 dB at 14 kHz -> %.1f, 1 kHz with both shelves %.1f; peak +12 at 1 kHz -> %.1f, -12 -> %.1f, flat %.1f\n",
                low, high, mid_untouched, peak, cut, flat);
    CHECK_NEAR(low, -12.0, 1.5);
    CHECK_NEAR(high, 12.0, 1.5);
    CHECK(std::fabs(mid_untouched) < 1.0);
    CHECK_NEAR(peak, 12.0, 1.5);
    CHECK_NEAR(cut, -12.0, 1.5);
    CHECK(std::fabs(flat) < 0.3);
}

TEST(shifter_moves_a_sine_by_the_shift_and_rings_at_the_carrier) {
    for (int mode = 1; mode <= 2; mode++) {                                      // 100 Hz up: 1000 -> 1100 Hz, down: 900 Hz
        FxBench b(T_SHIFTER, false, 1000.0, 0.5);
        b.set(SFT_MODE, mode); b.set(SFT_FREQ, static_cast<int>(12 * 256 * std::log2(100.0))); b.set(SFT_MIX, 32767);
        std::vector<double> l;
        b.run(0.6, &l);
        const double want = power_at(l, S(0.2), S(0.5), mode == 1 ? 1100.0 : 900.0), wrong = power_at(l, S(0.2), S(0.5), mode == 1 ? 900.0 : 1100.0), orig = power_at(l, S(0.2), S(0.5), 1000.0);
        std::printf("    shift %s 100 Hz: wanted line %.2e, mirror %.2e, original %.2e\n", mode == 1 ? "up" : "down", want, wrong, orig);
        CHECK(want > 15.0 * (wrong + 1.0));                                       // (the Hilbert network is a little less exact at low frequencies relative to the sample rate)
        CHECK(want > 30.0 * (orig + 1.0));
    }
    FxBench r(T_SHIFTER, false, 1000.0, 0.5);
    r.set(SFT_MODE, 0); r.set(SFT_FREQ, static_cast<int>(12 * 256 * std::log2(300.0))); r.set(SFT_MIX, 32767);
    std::vector<double> l;
    r.run(0.6, &l);
    const double lower = power_at(l, S(0.2), S(0.5), 700.0), upper = power_at(l, S(0.2), S(0.5), 1300.0), orig = power_at(l, S(0.2), S(0.5), 1000.0);
    CHECK(lower > 30.0 * (orig + 1.0) && upper > 30.0 * (orig + 1.0));
}

TEST(convolver_shapes_the_spectrum_like_a_cabinet_and_takes_a_custom_ir) {
    FxBench b(T_CONV, true);
    b.set(CNV_IR, CNVIR_CAB_1X12); b.set(CNV_LENGTH, 256); b.set(CNV_MIX, 32767); b.set(CNV_LEVEL, 16384);
    std::vector<double> l;
    b.run(0.6, &l);
    const double mid = band_db(l, 8192, 4096, 1500, 3000), high = band_db(l, 8192, 4096, 9000, 15000), low = band_db(l, 8192, 4096, 20, 50);
    std::printf("    1x12 cab on white noise: 1.5-3 kHz %.1f dB, above 9 kHz %.1f dB, below 50 Hz %.1f dB\n", mid, high, low);
    CHECK(mid - high > 18.0);
    CHECK(mid - low > 6.0);                                                      // (256 taps resolve about 190 Hz: the low cut is gentle)
    // custom IR: one tap of 0.5 at position 10 = the input delayed by 10 samples at half level (level 0.5 = unity of the normalised IR)
    FxBench c(T_CONV, false, 440.0, 0.5);
    ConvBlob blob{};
    blob.start = 0; blob.count = 16;
    blob.tap[10] = 16384;
    c.rig.eng.set_blob(2, &blob, sizeof blob);
    c.set(CNV_MIX, 32767); c.set(CNV_LEVEL, 16384); c.set(CNV_LENGTH, 64);
    std::vector<double> y;
    c.run(0.2, &y);
    const double expect = 0.5 * 0.5 * 32767.0 / std::sqrt(2.0);                  // half the input level (input amplitude 0.5)
    CHECK(rms_w(y, S(0.05), S(0.15)) > 0.8 * expect);
    CHECK(rms_w(y, S(0.05), S(0.15)) < 1.2 * expect);
}

TEST(comb_follows_the_key_and_rings_at_the_harmonics) {
    DspRig rig;
    GraphDesc g;
    rig.add(g, 1, T_NOTE_IN);
    rig.add(g, 2, T_OSC)->param[OSC_WAVE] = WAVE_NOISE_;
    NodeDesc *c = rig.add(g, 3, T_COMB);
    c->param[CMB_FEEDBACK] = 28000; c->param[CMB_MIX] = 32767; c->param[CMB_TUNE] = 60 * 256; c->param[CMB_DAMP] = 135 * 256;
    rig.add(g, 4, T_VOICE_OUT)->param[VO_TAIL_MS] = 500;
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
    g.connect(1, 0, 3, Dst::In, 1);
    g.connect(3, 0, 4, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.eng.note_on(57);                                                         // 220 Hz
    std::vector<double> y;
    rig.run(DspRig::blocks_for(0.5), &y);
    const size_t period = static_cast<size_t>(std::lround(kSampleRate / 220.0));
    double a = 0, e0 = 0, e1 = 0;
    for (size_t i = 6000; i + period < 14000; i++) { a += y[i] * y[i + period]; e0 += y[i] * y[i]; e1 += y[i + period] * y[i + period]; }
    const double ac = a / std::sqrt(e0 * e1 + 1e-9);
    size_t best = 0;
    double bestv = -2;
    for (size_t lag = 20; lag < 1000; lag++) {
        double a2 = 0, f0 = 0, f1 = 0;
        for (size_t i = 6000; i + lag < 14000; i++) { a2 += y[i] * y[i + lag]; f0 += y[i] * y[i]; f1 += y[i + lag] * y[i + lag]; }
        const double v = a2 / std::sqrt(f0 * f1 + 1e-9);
        if (v > bestv) { bestv = v; best = lag; }
    }
    std::printf("    comb on noise at A3: autocorrelation at one period %.2f; strongest lag %zu (period %zu), %.2f\n", ac, best, period, bestv);
    CHECK(ac > 0.3 && best + 2 >= period && best <= period + 2);                  // resonates at the played pitch
}
