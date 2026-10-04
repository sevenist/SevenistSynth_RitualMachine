#include "rig.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/stft.h"

using namespace sc;
using namespace tst;

namespace {
const double kPi = 3.14159265358979323846;

// Global module that emits one full-scale sample when its parameter is set to 1.
class TImpulse : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Impulse", Scope::Global, 0, 1, 1, false, {}, {"out"}, {{"trig", 0, 0, 1}}};
        return i;
    }
    void set_param(int, int32_t v) override { if (v) fire_ = true; }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        for (int i = 0; i < ctx.frames; i++) p.out[0][i] = 0;
        if (fire_) { p.out[0][0] = 32767; fire_ = false; }
    }
private:
    bool fire_ = false;
};
ModuleType impulse_type() { static TImpulse probe; return {&probe.info(), &create_module<TImpulse>}; }
constexpr int T_IMPULSE = 120;

// Global oscillator -> [more nodes] -> master helpers
NodeDesc *osc_g(DspRig &r, GraphDesc &g, int id, int wave, double hz, int level) {
    NodeDesc *n = r.add(g, id, T_OSC_G);
    n->param[OSC_WAVE] = wave;
    n->param[OSC_PITCH] = hz_to_pitch(hz);
    n->param[OSC_LEVEL] = level;
    return n;
}

int crossings(const std::vector<double> &x) {
    int c = 0;
    for (size_t i = 1; i < x.size(); i++) if (x[i - 1] < 0 && x[i] >= 0) c++;
    return c;
}
double hz_of(const std::vector<double> &x) { return crossings(x) * static_cast<double>(kSampleRate) / static_cast<double>(x.size()); }

// Normalised autocorrelation at a lag.
double autocorr(const std::vector<double> &x, size_t lag) {
    double a = 0, e0 = 0, e1 = 0;
    for (size_t i = 0; i + lag < x.size(); i++) { a += x[i] * x[i + lag]; e0 += x[i] * x[i]; e1 += x[i + lag] * x[i + lag]; }
    return a / std::sqrt(e0 * e1 + 1e-9);
}

// Power of the Hann-windowed spectrum between two frequencies (Hz).
double band_power(const std::vector<double> &x, double f0, double f1) {
    std::vector<double> w(x.size());
    for (size_t i = 0; i < x.size(); i++) w[i] = x[i] * (0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(x.size())));
    std::vector<double> p = dft_power(w);
    double s = 0;
    for (size_t k = 0; k < p.size(); k++) {
        double f = static_cast<double>(k) * kSampleRate / static_cast<double>(x.size());
        if (f >= f0 && f <= f1) s += p[k];
    }
    return s;
}
}  // namespace

TEST(delay_echoes_land_on_time_and_decay_by_the_feedback) {
    DspRig rig;
    rig.eng.registry().add(T_IMPULSE, impulse_type());
    GraphDesc g;
    rig.add(g, 1, T_IMPULSE);
    NodeDesc *d = rig.add(g, 2, T_DELAY);
    d->param[DLY_TIME] = 100 * 16; d->param[DLY_FEEDBACK] = 16384; d->param[DLY_DAMP] = 135 * 256; d->param[DLY_MIX] = kUnity;
    rig.add(g, 3, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(1, 0, 2, Dst::In, 1);
    g.connect(2, 0, 3, Dst::In, 0);
    g.connect(2, 1, 3, Dst::In, 1);
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.run(2);
    rig.eng.set_param(1, 0, 1);
    std::vector<double> y;
    rig.run(DspRig::blocks_for(0.5), &y);

    const int D = kSampleRate / 10;                                   // 100 ms
    auto peak_near = [&](int center, int span, int *at) {
        double best = 0; int bi = center;
        for (int i = center - span; i <= center + span; i++) if (i >= 0 && i < static_cast<int>(y.size()) && y[i] > best) { best = y[i]; bi = i; }
        if (at) *at = bi;
        return best;
    };
    int a1, a2, a3;
    double e1 = peak_near(D, 4, &a1);                                 // the impulse is the first sample collected
    double e2 = peak_near(2 * D, 8, &a2);
    double e3 = peak_near(3 * D, 12, &a3);
    std::printf("    echoes at %d, %d, %d samples (expect n*%d), levels %.0f / %.0f / %.0f\n", a1, a2, a3, D, e1, e2, e3);
    CHECK_NEAR(a1, D, 1);
    CHECK(e1 > 30000);                                                // first echo is the undamped input
    CHECK(e2 / e1 > 0.30 && e2 / e1 < 0.50);                          // feedback 0.5 x one-pole loss
    CHECK(e3 / e2 > 0.30 && e3 / e2 < 0.55);
}

TEST(delay_time_changes_glide_without_clicks) {
    DspRig rig;
    GraphDesc g;
    osc_g(rig, g, 1, WAVE_SINE_, 440.0, 16384);
    NodeDesc *d = rig.add(g, 2, T_DELAY);
    d->param[DLY_TIME] = 100 * 16; d->param[DLY_FEEDBACK] = 0; d->param[DLY_MIX] = kUnity;
    rig.add(g, 3, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(1, 0, 2, Dst::In, 1);
    g.connect(2, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.run(DspRig::blocks_for(0.3));
    rig.eng.set_param(2, DLY_TIME, 400 * 16);                         // a big jump: 100 ms -> 400 ms
    std::vector<double> y;
    rig.run(DspRig::blocks_for(0.8), &y);
    double nominal = 2 * kPi * 440.0 / kSampleRate * 16383.0;
    double worst = 0;
    for (size_t i = 1; i < y.size(); i++) worst = std::fmax(worst, std::fabs(y[i] - y[i - 1]));
    std::printf("    largest sample step during the glide %.0f (a plain 440 Hz sine: %.0f)\n", worst, nominal);
    CHECK(worst < nominal * 1.35);                                    // pitch moves by at most ~25 %, no discontinuity
    // and it arrives: the glide runs at 1/4 sample per sample (1.2 s for this jump), after which the sine is clean again
    rig.run(DspRig::blocks_for(0.6));
    std::vector<double> z;
    rig.run(DspRig::blocks_for(0.5), &z);
    CHECK_NEAR(hz_of(z), 440.0, 4.0);
}

TEST(delay_mix_pingpong_and_bulk_memory) {
    DspRig rig(4, true);                                              // delay lines live in the separate "PSRAM" heap
    rig.eng.registry().add(T_IMPULSE, impulse_type());
    GraphDesc g;
    rig.add(g, 1, T_IMPULSE);
    NodeDesc *d = rig.add(g, 2, T_DELAY);
    d->param[DLY_TIME] = 50 * 16; d->param[DLY_FEEDBACK] = 24000; d->param[DLY_DAMP] = 135 * 256; d->param[DLY_MIX] = kUnity; d->param[DLY_PINGPONG] = 1;
    rig.add(g, 3, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(1, 0, 2, Dst::In, 1);
    g.connect(2, 0, 3, Dst::In, 0);
    g.connect(2, 1, 3, Dst::In, 1);
    CHECK(rig.eng.load(g) == Err::Ok);
    CHECK(rig.bulk.used() >= 2 * kSampleRate * sizeof(q15));          // both lines (at least one second of samples each) live in bulk memory
    CHECK(rig.heap.used() < 64 * 1024);                               // fast memory only holds plan and module objects
    rig.run(2);
    rig.eng.set_param(1, 0, 1);
    std::vector<double> l, r;
    rig.run2(DspRig::blocks_for(0.4), &l, &r);
    const int D = kSampleRate / 20;                                   // 50 ms
    auto at = [&](const std::vector<double> &v, int center) {
        double best = 0;
        for (int i = center - 12; i <= center + 12; i++) if (i >= 0 && i < static_cast<int>(v.size())) best = std::fmax(best, std::fabs(v[i]));
        return best;
    };
    CHECK(at(l, D) > 20000);  CHECK(at(r, D) < 200);                  // echo 1 on the left only
    CHECK(at(r, 2 * D) > 8000); CHECK(at(l, 2 * D) < 200);            // echo 2 crossed to the right
    CHECK(at(l, 3 * D) > 3000); CHECK(at(r, 3 * D) < 300);            // echo 3 back on the left

    rig.eng.set_param(2, DLY_MIX, 0);                                 // mix 0: the dry signal only
    rig.run(2);
    rig.eng.set_param(1, 0, 1);
    std::vector<double> dry;
    rig.run(DspRig::blocks_for(0.1), &dry);
    CHECK(dry[0] > 32000);
    CHECK(at(dry, D) < 50);
    rig.eng.shutdown();
    CHECK_EQ(rig.bulk.used(), 0);
    CHECK_EQ(rig.heap.used(), 0);
}

TEST(spectral_thru_reconstructs_with_512_samples_of_latency) {
    auto render = [](bool with_fx, std::vector<double> *y) {
        DspRig rig;
        GraphDesc g;
        osc_g(rig, g, 1, WAVE_SAW_, 330.0, 20000);
        int out = 1;
        if (with_fx) { rig.add(g, 2, T_SPECTRAL)->param[SPX_MODE] = SPXM_THRU; g.connect(1, 0, 2, Dst::In, 0); out = 2; }
        rig.add(g, 3, T_MASTER_OUT);
        g.connect(out, 0, 3, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.run(DspRig::blocks_for(0.5), y);
    };
    std::vector<double> ref, got;
    render(false, &ref);
    render(true, &got);
    std::vector<double> a, b;
    for (size_t i = 4000; i < 16000; i++) { a.push_back(ref[i - Stft::latency()]); b.push_back(got[i]); }
    double snr = snr_db(a, b);
    std::printf("    SpectralFx thru: SNR %.1f dB vs the input delayed by %d samples\n", snr, Stft::latency());
    CHECK(snr > 45.0);
}

TEST(spectral_freeze_sustains_the_tone_after_the_input_stops) {
    if (kSampleRate > 48000) { std::printf("    skipped: N=%d bins are too wide for the phase vocoder at this rate\n", kFftN); return; }
    DspRig rig;
    GraphDesc g;
    osc_g(rig, g, 1, WAVE_SINE_, 1000.0, 16384);
    NodeDesc *s = rig.add(g, 2, T_SPECTRAL);
    s->param[SPX_MODE] = SPXM_FREEZE;
    rig.add(g, 3, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 1);
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.run(DspRig::blocks_for(0.3));
    double live = rig.rms_over(0.2);
    rig.eng.set_param(2, SPX_FREEZE, 1);
    rig.run(DspRig::blocks_for(0.05));
    rig.eng.set_param(1, OSC_LEVEL, 0);                               // the source goes silent
    rig.run(DspRig::blocks_for(0.1));
    std::vector<double> a, b;
    rig.run(DspRig::blocks_for(0.4), &a);
    rig.run(DspRig::blocks_for(0.4), &b);
    std::printf("    freeze: live %.0f rms, frozen %.0f then %.0f rms, %.0f Hz\n", live, rms(a), rms(b), hz_of(b));
    CHECK_NEAR(db(rms(a) / live), 0.0, 1.5);
    CHECK_NEAR(db(rms(b) / live), 0.0, 1.5);                          // holds, does not decay
    CHECK_NEAR(hz_of(b), 1000.0, 12.0);

    rig.eng.set_param(2, SPX_FREEZE, 0);                              // release: it falls silent
    rig.run(DspRig::blocks_for(0.3));
    CHECK(rig.rms_over(0.2) < live * 0.05);
}

TEST(spectral_pitch_shift_moves_the_frequency) {
    if (kSampleRate > 48000) { std::printf("    skipped: N=%d bins are too wide for the phase vocoder at this rate\n", kFftN); return; }
    struct Case { int semitones; double expect_hz; };
    for (Case c : {Case{12, 2000.0}, Case{-12, 500.0}, Case{7, 1498.3}, Case{-5, 749.2}}) {
        DspRig rig;
        GraphDesc g;
        osc_g(rig, g, 1, WAVE_SINE_, 1000.0, 16384);
        NodeDesc *s = rig.add(g, 2, T_SPECTRAL);
        s->param[SPX_MODE] = SPXM_PITCH; s->param[SPX_SHIFT] = c.semitones * 256;
        rig.add(g, 3, T_MASTER_OUT);
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(2, 0, 3, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.run(DspRig::blocks_for(0.4));
        std::vector<double> y;
        rig.run(DspRig::blocks_for(0.5), &y);
        std::printf("    shift %+d st: %.0f Hz (expect %.0f), rms %.0f\n", c.semitones, hz_of(y), c.expect_hz, rms(y));
        CHECK_NEAR(hz_of(y), c.expect_hz, c.expect_hz * 0.04);
        CHECK(rms(y) > 1500.0);
    }
}

TEST(spectral_robot_and_whisper_change_the_character) {
    auto render = [](int mode, int wave, double hz, std::vector<double> *y) {
        DspRig rig;
        GraphDesc g;
        osc_g(rig, g, 1, wave, hz, 12000);
        rig.add(g, 2, T_SPECTRAL)->param[SPX_MODE] = mode;
        rig.add(g, 3, T_MASTER_OUT);
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(2, 0, 3, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.run(DspRig::blocks_for(0.3));
        rig.run(DspRig::blocks_for(0.6), y);
    };
    // robot: noise becomes periodic at the hop (N/4 = 128 samples)
    std::vector<double> noise, robot;
    render(SPXM_THRU, WAVE_NOISE_, 100.0, &noise);
    render(SPXM_ROBOT, WAVE_NOISE_, 100.0, &robot);
    double c128 = autocorr(robot, Stft::H), c100 = autocorr(robot, 100), cn = autocorr(noise, Stft::H);
    std::printf("    robot autocorrelation at the hop: %.2f (lag 100: %.2f, plain noise: %.2f)\n", c128, c100, cn);
    CHECK(c128 > 0.3);
    CHECK(c128 > 10 * std::fabs(c100));
    CHECK(std::fabs(cn) < 0.1);

    // whisper: a steady tone loses its long-term phase coherence but keeps its level
    std::vector<double> tone, whisper;
    render(SPXM_THRU, WAVE_SINE_, 1000.0, &tone);
    render(SPXM_WHISPER, WAVE_SINE_, 1000.0, &whisper);
    double ct = autocorr(tone, kSampleRate / 10), cw = autocorr(whisper, kSampleRate / 10);
    std::printf("    whisper: coherence over 100 ms %.2f (tone %.2f), level %.1f dB\n", cw, ct, db(rms(whisper) / rms(tone)));
    CHECK(ct > 0.9);
    CHECK(std::fabs(cw) < 0.4);
    CHECK(db(rms(whisper) / rms(tone)) > -7.0 && db(rms(whisper) / rms(tone)) < 3.0);
}

TEST(spectral_gate_removes_noise_and_band_limits) {
    // tone + faint noise: the gate (5 % of the frame peak) removes the noise floor
    auto noise_floor = [](int mode) {
        DspRig rig;
        GraphDesc g;
        osc_g(rig, g, 1, WAVE_SINE_, 1000.0, 20000);
        osc_g(rig, g, 2, WAVE_NOISE_, 100.0, 300);
        rig.add(g, 3, T_MIX4_G)->param[0] = 16384;
        g.node[2].param[1] = 16384;
        NodeDesc *s = rig.add(g, 4, T_SPECTRAL);
        s->param[SPX_MODE] = mode; s->param[SPX_AMOUNT] = 1638;       // 5 %
        rig.add(g, 5, T_MASTER_OUT);
        g.connect(1, 0, 3, Dst::In, 0);
        g.connect(2, 0, 3, Dst::In, 1);
        g.connect(3, 0, 4, Dst::In, 0);
        g.connect(4, 0, 5, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.run(DspRig::blocks_for(0.3));
        std::vector<double> y;
        rig.run((4096 + kBlock - 1) / kBlock, &y);                         // 4320 samples
        y.resize(4096);
        return band_power(y, 3000.0, 10000.0);
    };
    double thru = noise_floor(SPXM_THRU), gated = noise_floor(SPXM_GATE);
    std::printf("    noise floor above 3 kHz: thru %.1f dB, gated %.1f dB\n", 10 * std::log10(thru + 1e-12), 10 * std::log10(gated + 1e-12));
    CHECK(10 * std::log10(thru / (gated + 1e-12)) > 15.0);

    // two tones, pass band 3..8 kHz: the 1 kHz tone is removed, the 6 kHz tone passes
    DspRig rig;
    GraphDesc g;
    osc_g(rig, g, 1, WAVE_SINE_, 1000.0, 16384);
    osc_g(rig, g, 2, WAVE_SINE_, 6000.0, 16384);
    rig.add(g, 3, T_MIX4_G)->param[0] = 16384;
    g.node[2].param[1] = 16384;
    NodeDesc *s = rig.add(g, 4, T_SPECTRAL);
    s->param[SPX_MODE] = SPXM_GATE; s->param[SPX_AMOUNT] = 0; s->param[SPX_LO] = hz_to_pitch(3000.0); s->param[SPX_HI] = hz_to_pitch(8000.0);
    rig.add(g, 5, T_MASTER_OUT);
    g.connect(1, 0, 3, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 1);
    g.connect(3, 0, 4, Dst::In, 0);
    g.connect(4, 0, 5, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.run(DspRig::blocks_for(0.3));
    std::vector<double> y;
    rig.run((4096 + kBlock - 1) / kBlock, &y);
    y.resize(4096);
    double low = band_power(y, 800.0, 1200.0), high = band_power(y, 5800.0, 6200.0);
    std::printf("    band filter: 1 kHz %.1f dB, 6 kHz %.1f dB\n", 10 * std::log10(low + 1e-12), 10 * std::log10(high + 1e-12));
    CHECK(10 * std::log10(high / (low + 1e-12)) > 30.0);
}

TEST(vocoder_imposes_the_modulator_envelope_on_the_carrier) {
    auto render = [](int mod_level, std::vector<double> *y) {
        DspRig rig;
        GraphDesc g;
        osc_g(rig, g, 1, WAVE_SINE_, 1000.0, mod_level);                // modulator: a 1 kHz tone
        osc_g(rig, g, 2, WAVE_NOISE_, 100.0, 12000);                    // carrier: white noise
        rig.add(g, 3, T_VOCODER);
        rig.add(g, 4, T_MASTER_OUT);
        g.connect(1, 0, 3, Dst::In, 0);
        g.connect(2, 0, 3, Dst::In, 1);
        g.connect(3, 0, 4, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.run(DspRig::blocks_for(0.3));
        rig.run((4096 + kBlock - 1) / kBlock, y);
        y->resize(4096);
    };
    std::vector<double> y, silent;
    render(16384, &y);
    render(0, &silent);
    double in_band = band_power(y, 750.0, 1450.0), out_band = band_power(y, 3000.0, 8000.0);
    std::printf("    vocoder: energy near the modulator tone %.1f dB, far %.1f dB (carrier is white); silent modulator rms %.0f\n",
                10 * std::log10(in_band + 1e-12), 10 * std::log10(out_band + 1e-12), rms(silent));
    CHECK(10 * std::log10(in_band / (out_band + 1e-12)) > 15.0);
    CHECK(rms(silent) < 0.1 * rms(y) + 20.0);
}
