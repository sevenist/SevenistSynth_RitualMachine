#include <chrono>
#include "rig.h"
#include "engine/dsp/osc.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/svf.h"

using namespace sc;
using namespace tst;

namespace {

const double kPi = 3.14159265358979323846;

double q_hz(int32_t pitch) { return 440.0 * std::exp2((pitch - 69 * 256) / (12.0 * 256.0)); }

// Oscillator -> filter -> voice out, with a held note. Used to measure the filter's frequency response.
struct FilterBench {
    DspRig rig;
    FilterBench() {
        GraphDesc g;
        rig.add(g, 1, T_OSC)->param[OSC_WAVE] = WAVE_SINE_;
        rig.add(g, 2, T_FILTER_V);
        rig.add(g, 3, T_VOICE_OUT);
        g.node[0].param[OSC_LEVEL] = 16384;
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(2, 0, 3, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.eng.note_on(60);
    }
    // gain in dB of the filter at input frequency f (cutoff already set)
    double gain_db(double f, double in_rms = 16384.0 / 1.41421356) {
        rig.eng.set_param(1, OSC_PITCH, hz_to_pitch(f));
        rig.run(DspRig::blocks_for(0.05));
        return db(rig.rms_over(0.1) / in_rms);
    }
    void filter(int mode, int sections, double fc, int res = 0) {
        rig.eng.set_param(2, FLT_MODE, mode);
        rig.eng.set_param(2, FLT_SECTIONS, sections);
        rig.eng.set_param(2, FLT_CUTOFF, hz_to_pitch(fc));
        rig.eng.set_param(2, FLT_RES, res);
        rig.run(DspRig::blocks_for(0.1));        // let the control-rate smoothing settle
    }
};

// Analytic response of the cascaded Butterworth sections through the bilinear transform: Omega = tan(pi f/fs) / g.
double omega(double f, double fc) { return std::tan(kPi * f / kSampleRate) / std::tan(kPi * fc / kSampleRate); }
double lp_db(double f, double fc, int sections) { return -10.0 * std::log10(1.0 + std::pow(omega(f, fc), 4.0 * sections)); }
double hp_db(double f, double fc, int sections) { return -10.0 * std::log10(1.0 + std::pow(1.0 / omega(f, fc), 4.0 * sections)); }

}  // namespace

TEST(svf_reciprocal_is_accurate) {
    double worst = 0;                                                  // in LSB of the Q31 result
    for (uint64_t d = (1u << 26); d < 55ull * (1u << 26); d += 987653) {
        double D = static_cast<double>(d) / (1u << 26);
        double want = std::fmin(2147483647.0, 2147483648.0 / D);
        double got = svf_recip_q26(static_cast<uint32_t>(d));
        worst = std::fmax(worst, std::fabs(got - want));
    }
    std::printf("    reciprocal worst error %.2f LSB (Q31)\n", worst);
    CHECK(worst <= 4.0);
}

TEST(svf_batch_float_coefficients_match_the_integer_ones) {
    double worst = 0;
    for (int n = 1; n <= 4; n++) {
        for (double fc = 30.0; fc < 0.45 * kSampleRate; fc *= 1.31) {
            const uint32_t g_q28 = svf_g(hz_to_inc(static_cast<float>(fc)));
            const float g = static_cast<float>(g_q28) * (1.0f / 268435456.0f);
            float k[4];
            for (int s = 0; s < n; s++) k[s] = 0.5f + 0.4f * static_cast<float>(s);
            SvfCoefF c[4];
            svf_coef_batch(g, k, n, c);
            for (int s = 0; s < n; s++) {
                const SvfCoefF ref = svf_to_float(svf_coef(g_q28, static_cast<int32_t>(k[s] * 536870912.0f)));
                worst = std::fmax(worst, std::fabs(static_cast<double>(c[s].a1) - ref.a1));
                worst = std::fmax(worst, std::fabs(static_cast<double>(c[s].a2) - ref.a2));
                worst = std::fmax(worst, std::fabs(static_cast<double>(c[s].a3) - ref.a3));
            }
        }
    }
    std::printf("    batch float coefficients: worst difference to the integer ones %.2e\n", worst);
    CHECK(worst < 2e-6);
}

TEST(svf_prewarp_matches_tan) {
    double worst = 0;
    for (double fc = 20.0; fc < 0.45 * kSampleRate; fc *= 1.05) {
        double g = svf_g(hz_to_inc(static_cast<float>(fc))) / 268435456.0;
        worst = std::fmax(worst, std::fabs(g / std::tan(kPi * fc / kSampleRate) - 1.0));
    }
    CHECK(worst < 1e-3);
}

TEST(filter_lowpass_response_matches_butterworth) {
    FilterBench b;
    const double fc = 1000.0;
    for (int sections = 1; sections <= 4; sections++) {
        b.filter(FLTM_LP, sections, fc);
        double fcq = q_hz(hz_to_pitch(fc));
        for (double f : {250.0, 500.0, 1000.0, 2000.0}) {
            double fq = q_hz(hz_to_pitch(f));
            double want = lp_db(fq, fcq, sections);
            if (want < -45) continue;
            CHECK_NEAR(b.gain_db(f), want, 0.4);
        }
        CHECK_NEAR(b.gain_db(fc), -3.01, 0.4);                         // -3 dB at the cutoff for every order
    }
    b.filter(FLTM_LP, 4, fc);
    CHECK(b.gain_db(4000.0) < -40.0);                                  // 8 poles: steep skirt
}

TEST(filter_highpass_bandpass_notch) {
    FilterBench b;
    const double fc = 2000.0;
    double fcq = q_hz(hz_to_pitch(fc));
    b.filter(FLTM_HP, 2, fc);
    for (double f : {500.0, 1000.0, 2000.0, 4000.0}) {
        double want = hp_db(q_hz(hz_to_pitch(f)), fcq, 2);
        if (want > -45) CHECK_NEAR(b.gain_db(f), want, 0.4);
    }
    b.filter(FLTM_BP, 1, fc);
    CHECK_NEAR(b.gain_db(fc), 0.0, 0.3);                               // normalised band-pass: unity at the centre
    const double k = 1.0 / 0.70711;                                    // single section Q
    for (double f : {fc / 4, fc / 2, fc * 2, fc * 4}) {
        double w = omega(q_hz(hz_to_pitch(f)), fcq);
        double want = db(k * w / std::sqrt((1 - w * w) * (1 - w * w) + k * k * w * w));
        CHECK_NEAR(b.gain_db(f), want, 0.4);
    }
    b.filter(FLTM_NOTCH, 1, fc);
    CHECK(b.gain_db(fc) < -35.0);
    CHECK_NEAR(b.gain_db(fc / 8), 0.0, 0.5);
}

TEST(filter_resonance_peaks_and_stays_stable) {
    FilterBench b;
    b.rig.eng.set_param(1, OSC_LEVEL, 2048);
    const double in_rms = 2048.0 / 1.41421356;
    b.filter(FLTM_LP, 1, 1000.0, 0);
    double flat = b.gain_db(1000.0, in_rms);
    b.filter(FLTM_LP, 1, 1000.0, 32767);                              // Q boost 16x on the last section
    double peak = b.gain_db(1000.0, in_rms);
    CHECK_NEAR(peak - flat, 20.0 * std::log10(16.0), 1.0);

    // noise through the most extreme settings, then silence: the filter must ring out, not latch
    b.rig.eng.set_param(1, OSC_WAVE, WAVE_NOISE_);
    b.rig.eng.set_param(1, OSC_LEVEL, 30000);
    for (int sections = 1; sections <= 4; sections++)
        for (int32_t cut : {0, 40 * 256, 100 * 256, 135 * 256}) {
            b.rig.eng.set_param(2, FLT_SECTIONS, sections);
            b.rig.eng.set_param(2, FLT_CUTOFF, cut);
            b.rig.eng.set_param(2, FLT_RES, 32767);
            b.rig.run(DspRig::blocks_for(0.05));
        }
    b.rig.eng.set_param(1, OSC_LEVEL, 0);
    b.rig.run(DspRig::blocks_for(1.0));
    CHECK(b.rig.rms_over(0.1) < 8.0);
}

TEST(oscillator_frequencies) {
    DspRig rig;
    for (int w : {WAVE_SINE_, WAVE_SAW_, WAVE_PULSE_, WAVE_TRI_}) {
        for (double f : {110.0, 440.0, 3000.0}) {
            GraphDesc g;
            rig.add(g, 1, T_OSC)->param[OSC_WAVE] = w;
            g.node[0].param[OSC_PITCH] = hz_to_pitch(f);
            g.node[0].param[OSC_PW] = 16384;
            rig.add(g, 2, T_VOICE_OUT);
            g.connect(1, 0, 2, Dst::In, 0);
            CHECK(rig.eng.load(g) == Err::Ok);
            rig.eng.all_notes_off();
            rig.eng.note_on(60);
            rig.run(4);
            std::vector<double> x;
            rig.run(DspRig::blocks_for(1.0), &x);
            int cross = 0;
            for (size_t i = 1; i < x.size(); i++) if (x[i - 1] < 0 && x[i] >= 0) cross++;
            CHECK_NEAR(cross * static_cast<double>(kSampleRate) / static_cast<double>(x.size()), q_hz(hz_to_pitch(f)), 1.5);
        }
    }
}

namespace {
// Non-harmonic power relative to the fundamental for a coherent waveform (N samples, m cycles).
template <typename F>
double alias_db(F wave, int N, int m) {
    std::vector<double> x(N);
    const uint32_t inc = static_cast<uint32_t>(static_cast<uint64_t>(m) * 4294967296ull / N);
    uint32_t ph = 0;
    for (int i = 0; i < N; i++, ph += inc) x[i] = wave(ph, inc);
    std::vector<double> p = dft_power(x);
    double alias = 0;
    for (int k = 1; k <= N / 2; k++) if (k % m != 0) alias += p[k];
    return 10.0 * std::log10(alias / p[m]);
}
}  // namespace

TEST(band_limited_waveforms_alias_less_than_naive) {
    const int N = 2048;
    auto saw_n = [](uint32_t t, uint32_t) { return static_cast<double>(static_cast<int32_t>(t >> 16) - 32768); };
    auto saw_b = [](uint32_t t, uint32_t dt) { return static_cast<double>(osc_saw(t, dt)); };
    const uint32_t pw = 0x4CCCCCCDu;                                   // 30 % duty
    auto pulse_n = [pw](uint32_t t, uint32_t) { return t < pw ? 32767.0 : -32768.0; };
    auto pulse_b = [pw](uint32_t t, uint32_t dt) { return static_cast<double>(osc_pulse(t, dt, pw)); };
    auto tri_n = [](uint32_t t, uint32_t) {
        return static_cast<double>(t < 0x80000000u ? 32767 - static_cast<int32_t>(t >> 15) : static_cast<int32_t>(t >> 15) - 98303);
    };
    auto tri_b = [](uint32_t t, uint32_t dt) { return static_cast<double>(osc_tri(t, dt)); };

    // mid-range fundamental (about 1.2 kHz at 48 kHz): the corrections must clearly win
    {
        const int m = 53;
        double sn = alias_db(saw_n, N, m), sb = alias_db(saw_b, N, m);
        double pn = alias_db(pulse_n, N, m), pb = alias_db(pulse_b, N, m);
        double tn = alias_db(tri_n, N, m), tb = alias_db(tri_b, N, m);
        std::printf("    1.2 kHz alias: saw naive %.1f / blep %.1f, pulse %.1f / %.1f, triangle %.1f / %.1f dB\n", sn, sb, pn, pb, tn, tb);
        CHECK(sb < sn - 15.0);
        CHECK(pb < pn - 15.0);
        CHECK(tb < tn - 8.0);
    }
    // 5 kHz is hard for a 2-sample correction: it must still help
    {
        const int m = 213;
        double sn = alias_db(saw_n, N, m), sb = alias_db(saw_b, N, m);
        double pn = alias_db(pulse_n, N, m), pb = alias_db(pulse_b, N, m);
        double tn = alias_db(tri_n, N, m), tb = alias_db(tri_b, N, m);
        std::printf("    5 kHz alias:   saw naive %.1f / blep %.1f, pulse %.1f / %.1f, triangle %.1f / %.1f dB\n", sn, sb, pn, pb, tn, tb);
        CHECK(sb < sn - 4.0);
        CHECK(pb < pn - 4.0);
        CHECK(tb < tn - 3.0);
    }
}

TEST(osc_waveforms_use_the_full_range_and_pulse_keeps_its_duty) {
    const int N = 2048, m = 37;
    const uint32_t inc = static_cast<uint32_t>(static_cast<uint64_t>(m) * 4294967296ull / N);
    double mn = 1e9, mx = -1e9;
    uint32_t ph = 0;
    for (int i = 0; i < N; i++, ph += inc) { double s = osc_saw(ph, inc); mn = std::fmin(mn, s); mx = std::fmax(mx, s); }
    CHECK(mn < -30000 && mx > 30000);                                  // saw spans -1..+1 (samples inside the correction window are pulled to 0)
    for (uint32_t duty : {0x40000000u, 0x80000000u, 0xC0000000u}) {
        double sum = 0;
        ph = 0;
        for (int i = 0; i < N; i++, ph += inc) sum += osc_pulse(ph, inc, duty);
        double expect = (static_cast<double>(duty) / 4294967296.0 * 2.0 - 1.0) * 32768.0;   // mean level = 2*duty - 1
        CHECK(std::fabs(sum / N - expect) < 400.0);
    }
}

TEST(adsr_timing) {
    DspRig rig;
    GraphDesc g;
    rig.add(g, 1, T_NOTE_IN);
    NodeDesc *e = rig.add(g, 2, T_ENV);
    e->param[ENV_ATTACK] = 20; e->param[ENV_DECAY] = 100; e->param[ENV_SUSTAIN] = 16384; e->param[ENV_RELEASE] = 100;
    rig.add(g, 3, T_VOICE_OUT);
    g.connect(1, 1, 2, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);

    rig.eng.note_on(60);
    std::vector<double> y;
    rig.run(DspRig::blocks_for(1.0), &y);
    size_t reach = 0;
    while (reach < y.size() && y[reach] < 32700) reach++;
    double t_ms = 1000.0 * static_cast<double>(reach) / kSampleRate;
    std::printf("    attack reached full scale at %.1f ms (set 20)\n", t_ms);
    CHECK(t_ms > 14.0 && t_ms < 21.0);                                 // segments have an exact length: the attack ends at its time
    CHECK_NEAR(y.back(), 16384, 250);                                  // sustain

    rig.eng.note_off(60);
    std::vector<double> r;
    rig.run(DspRig::blocks_for(0.4), &r);
    size_t at_rel = static_cast<size_t>(0.100 * kSampleRate);
    std::printf("    level one release-time after note-off: %.0f (start 16384)\n", r[at_rel]);
    CHECK(r[at_rel] >= 0 && r[at_rel] < 30);                           // the release reaches zero at its time
    CHECK(r[at_rel / 2] > 300 && r[at_rel / 2] < 8000);                // and is on its way before
    bool monotone = true;
    for (size_t i = 1; i < r.size(); i++) if (r[i] > r[i - 1] + 1) monotone = false;
    CHECK(monotone);

    // retrigger from the middle of a release continues from the current level (no click to zero)
    rig.eng.note_on(60);
    rig.eng.note_off(60);
    rig.run(DspRig::blocks_for(0.05));
    rig.eng.note_on(60);
    std::vector<double> z;
    rig.run(2, &z);
    CHECK(z[0] > 0 || z[kBlock] > 0);
}

TEST(lfo_rate_shapes_and_unipolar) {
    DspRig rig;
    GraphDesc g;
    NodeDesc *l = rig.add(g, 1, T_LFO_G);
    l->param[LFO_RATE] = static_cast<int32_t>(std::lround(12 * 256 * std::log2(5.0)));     // 5 Hz
    rig.add(g, 2, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    std::vector<double> x;
    rig.run(DspRig::blocks_for(4.0), &x);
    int cross = 0;
    for (size_t i = 1; i < x.size(); i++) if (x[i - 1] < 0 && x[i] >= 0) cross++;
    CHECK_NEAR(cross, 20, 1);

    rig.eng.set_param(1, LFO_UNIPOLAR, 1);
    x.clear();
    rig.run(DspRig::blocks_for(1.0), &x);
    double mn = 1e9, mx = -1e9;
    for (double v : x) { mn = std::fmin(mn, v); mx = std::fmax(mx, v); }
    CHECK(mn >= -1.0);
    CHECK(mx > 32000);

    rig.eng.set_param(1, LFO_SHAPE, LFOS_SH);
    rig.eng.set_param(1, LFO_UNIPOLAR, 0);
    x.clear();
    rig.run(DspRig::blocks_for(1.0), &x);
    int changes = 0;
    for (size_t i = 1; i < x.size(); i++) if (x[i] != x[i - 1]) changes++;
    CHECK(changes >= 3 && changes <= 8);                               // 5 held values per second
}

namespace {
// Global Const -> unit under test -> master; returns the output for a given input value.
struct ShaperBench {
    DspRig rig;
    ShaperBench() {
        GraphDesc g;
        rig.add(g, 1, T_CONST_G);
        rig.add(g, 2, T_SHAPER_G);
        rig.add(g, 3, T_MASTER_OUT);
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(2, 0, 3, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
    }
    double at(double x) {
        rig.eng.set_param(1, CONST_VALUE, static_cast<int32_t>(std::lround(x * 32768.0 > 32767 ? 32767 : x * 32768.0)));
        std::vector<double> y;
        rig.run(2, &y);
        return y.back() / 32768.0;
    }
};
}  // namespace

TEST(shaper_curves) {
    ShaperBench b;
    b.rig.eng.set_param(2, SHP_MODE, SHPM_TANH);
    b.rig.eng.set_param(2, SHP_DRIVE, 0);
    CHECK_NEAR(b.at(0.5), std::tanh(0.5), 0.002);
    CHECK_NEAR(b.at(-0.5), -std::tanh(0.5), 0.002);
    CHECK_NEAR(b.at(0.0), 0.0, 0.001);
    b.rig.eng.set_param(2, SHP_DRIVE, 256);                            // 2x
    CHECK_NEAR(b.at(0.5), std::tanh(1.0), 0.002);

    b.rig.eng.set_param(2, SHP_MODE, SHPM_CLIP);
    CHECK_NEAR(b.at(0.4), 0.8, 0.001);
    CHECK_NEAR(b.at(0.6), 1.0, 0.001);
    CHECK_NEAR(b.at(-0.6), -1.0, 0.001);

    b.rig.eng.set_param(2, SHP_MODE, SHPM_FOLD);
    b.rig.eng.set_param(2, SHP_DRIVE, 0);
    CHECK_NEAR(b.at(0.5), std::sin(kPi / 4), 0.002);
    b.rig.eng.set_param(2, SHP_DRIVE, 256);
    CHECK_NEAR(b.at(0.75), std::sin(kPi / 2 * 1.5), 0.003);            // folds back past 1.0

    b.rig.eng.set_param(2, SHP_MODE, SHPM_CRUSH);
    b.rig.eng.set_param(2, SHP_DRIVE, 0);
    b.rig.eng.set_param(2, SHP_BITS, 3);
    std::vector<double> levels;
    for (int i = -100; i <= 100; i++) {
        double v = b.at(i / 100.0 * 0.999);
        bool seen = false;
        for (double l : levels) if (l == v) seen = true;
        if (!seen) levels.push_back(v);
    }
    CHECK(levels.size() <= 9);

    b.rig.eng.set_param(2, SHP_MODE, SHPM_CLIP);
    b.rig.eng.set_param(2, SHP_DRIVE, 4 * 256);
    b.rig.eng.set_param(2, SHP_MIX, 0);                                // fully dry
    CHECK_NEAR(b.at(0.3), 0.3, 0.001);
    b.rig.eng.set_param(2, SHP_MIX, 16384);                            // half wet
    CHECK_NEAR(b.at(0.3), 0.3 + 0.5 * (1.0 - 0.3), 0.002);
}

TEST(vca_mult_mix4) {
    DspRig rig;
    GraphDesc g;
    rig.add(g, 1, T_CONST_G)->param[CONST_VALUE] = 16384;             // 0.5
    rig.add(g, 2, T_CONST_G)->param[CONST_VALUE] = 16384;
    rig.add(g, 3, T_MULT_G);
    rig.add(g, 4, T_VCA_G)->param[VCA_LEVEL] = 0;
    rig.add(g, 5, T_MIX4_G);
    rig.add(g, 6, T_MASTER_OUT);
    g.connect(1, 0, 3, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 1);
    g.connect(1, 0, 4, Dst::In, 0);
    g.connect(2, 0, 4, Dst::Param, VCA_LEVEL);                         // gain = 0 + 0.5
    for (int i = 0; i < 4; i++) g.connect(1, 0, 5, Dst::In, i);
    // three separate outputs into the master through a Mix4 so each is observable in turn
    g.connect(3, 0, 6, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    std::vector<double> y;
    rig.run(2, &y);
    CHECK_NEAR(y.back(), 8192, 3);                                     // ring mod: 0.5 * 0.5

    GraphDesc h;
    rig.add(h, 1, T_CONST_G)->param[CONST_VALUE] = 16384;
    rig.add(h, 2, T_VCA_G)->param[VCA_LEVEL] = 0;
    rig.add(h, 3, T_MASTER_OUT);
    h.connect(1, 0, 2, Dst::In, 0);
    h.connect(1, 0, 2, Dst::Param, VCA_LEVEL);
    h.connect(2, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(h) == Err::Ok);
    y.clear();
    rig.run(2, &y);
    CHECK_NEAR(y.back(), 8192, 3);                                     // 0.5 * (0 + 0.5)

    GraphDesc m;
    rig.add(m, 1, T_CONST_G)->param[CONST_VALUE] = 16384;
    rig.add(m, 2, T_MIX4_G);
    rig.add(m, 3, T_MASTER_OUT);
    for (int i = 0; i < 4; i++) m.connect(1, 0, 2, Dst::In, i);
    m.connect(2, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(m) == Err::Ok);
    y.clear();
    rig.run(2, &y);
    CHECK_NEAR(y.back(), 16384, 6);                                    // 4 x 0.5 x 0.25
}

TEST(audio_rate_fm_produces_the_expected_sidebands) {
    DspRig rig;
    GraphDesc g;
    NodeDesc *c = rig.add(g, 1, T_OSC);                                // carrier 1000 Hz
    c->param[OSC_WAVE] = WAVE_SINE_; c->param[OSC_PITCH] = hz_to_pitch(1000.0);
    NodeDesc *m = rig.add(g, 2, T_OSC);                                // modulator 100 Hz
    m->param[OSC_WAVE] = WAVE_SINE_; m->param[OSC_PITCH] = hz_to_pitch(100.0);
    rig.add(g, 3, T_VOICE_OUT);
    g.connect(2, 0, 1, Dst::Param, OSC_PITCH, 3277);                   // depth 0.1 x 12 semitones of range
    g.connect(1, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.eng.note_on(60);
    rig.run(DspRig::blocks_for(0.05));

    const int N = 4800;                                                // 10 Hz bins at 48 kHz
    if (kSampleRate != 48000) return;
    std::vector<double> x;
    rig.run(N / kBlock + 1, &x);
    x.resize(N);
    for (int i = 0; i < N; i++) x[i] *= 0.5 - 0.5 * std::cos(2 * kPi * i / N);          // Hann
    std::vector<double> p = dft_power(x);
    auto bin_db = [&](int k) { return 10.0 * std::log10(p[k - 1] + p[k] + p[k + 1] + 1e-12); };
    double carrier = bin_db(100);
    std::printf("    sidebands: -100Hz %.1f dB, +100Hz %.1f dB, +-200Hz %.1f dB, off-grid %.1f dB (re carrier)\n",
                bin_db(90) - carrier, bin_db(110) - carrier, bin_db(120) - carrier, bin_db(105) - carrier);
    CHECK(bin_db(90) - carrier > -14 && bin_db(90) - carrier < -4);
    CHECK(bin_db(110) - carrier > -14 && bin_db(110) - carrier < -4);
    CHECK(bin_db(120) - carrier < -15);
    CHECK(bin_db(105) - carrier < -30);
}

TEST(full_subtractive_voice_and_cost) {
    DspRig rig(8);
    GraphDesc g;
    rig.add(g, 1, T_NOTE_IN);
    rig.add(g, 2, T_OSC)->param[OSC_WAVE] = WAVE_SAW_;
    NodeDesc *env = rig.add(g, 3, T_ENV);
    env->param[ENV_ATTACK] = 5; env->param[ENV_DECAY] = 300; env->param[ENV_SUSTAIN] = 16384; env->param[ENV_RELEASE] = 150;
    NodeDesc *f = rig.add(g, 4, T_FILTER_V);
    f->param[FLT_CUTOFF] = 60 * 256; f->param[FLT_RES] = 10000; f->param[FLT_SECTIONS] = 2;
    rig.add(g, 5, T_VCA_V)->param[VCA_LEVEL] = 0;
    rig.add(g, 6, T_VOICE_OUT)->param[VO_TAIL_MS] = 2000;
    rig.add(g, 7, T_BUS_IN);
    rig.add(g, 8, T_MASTER_OUT);
    NodeDesc *lfo = rig.add(g, 9, T_LFO_G);
    lfo->param[LFO_RATE] = static_cast<int32_t>(std::lround(12 * 256 * std::log2(5.0)));
    g.connect(1, 0, 2, Dst::In, 0);                                    // pitch
    g.connect(1, 1, 3, Dst::In, 0);                                    // gate -> env
    g.connect(2, 0, 4, Dst::In, 0);
    g.connect(3, 0, 4, Dst::Param, FLT_CUTOFF, 20000);                 // env opens the filter
    g.connect(9, 0, 4, Dst::Param, FLT_CUTOFF, 4000);                  // global LFO wobbles it
    g.connect(4, 0, 5, Dst::In, 0);
    g.connect(3, 0, 5, Dst::Param, VCA_LEVEL);                         // env -> VCA
    g.connect(5, 0, 6, Dst::In, 0);
    g.connect(7, 0, 8, Dst::In, 0);
    g.connect(7, 1, 8, Dst::In, 1);
    CHECK(rig.eng.load(g) == Err::Ok);

    for (int n : {48, 55, 60, 64, 67, 71, 72, 76}) rig.eng.note_on(n);   // all 8 voices
    rig.run(1);                                                          // commands are applied at the start of the next block
    CHECK_EQ(rig.eng.active_voices(), 8);
    std::vector<double> y;
    rig.run(DspRig::blocks_for(0.5), &y);
    double peak = 0;
    for (double v : y) peak = std::fmax(peak, std::fabs(v));
    CHECK(rms(y) > 500.0);
    CHECK(peak <= 32767.0);

    auto t0 = std::chrono::steady_clock::now();
    const int blocks = 2000;
    rig.run(blocks);
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / blocks;
    double budget_us = 1e6 * kBlock / kSampleRate;
    std::printf("    host cost: %.1f us per block with 8 voices (block = %.0f us, %.1f %% of real time on this PC)\n", us, budget_us, 100.0 * us / budget_us);

    for (int n : {48, 55, 60, 64, 67, 71, 72, 76}) rig.eng.note_off(n);
    rig.run(DspRig::blocks_for(1.0));
    CHECK_EQ(rig.eng.active_voices(), 0);
}
