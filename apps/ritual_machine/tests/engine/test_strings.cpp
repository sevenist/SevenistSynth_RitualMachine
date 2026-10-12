// The Strings synth type (ADR-037): 32 thin voices, naive vs mipmap oscillators, the per-voice lowpass, the shared filter, the Ensemble.
// Through the production path (rack_t / synth_params_t -> mapper -> engine), like test_integration.cpp.
#include <algorithm>
#include <complex>
#include <vector>
#include "rig.h"
#include "rack_m.h"
#include "engine/dsp/wavetables.h"
#include "platform/engine/engine_synth.h"

extern "C" {
#include "core/rack.h"
#include "core/synth_config.h"
#include "core/synth_params.h"
}

using namespace tst;

namespace {
const double kPi = 3.14159265358979323846;

struct StrApp {
    std::vector<uint8_t> fast, bulk;
    rack_t rack;
    synth_params_t params;
    StrApp() : fast(6u << 20), bulk(6u << 20) {
        CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
        synth_params_default(&params);
        rack_init(&rack);
        rack.cfg.type = SYNTH_STRINGS;
        rack.cfg.mono = 0;
        rack_m_clear(rack);                                         // dry unless a test adds an effect (row M)
        params.str.ftype = FILT_OFF;
        params.amp_env.attack_ms = 1; params.amp_env.decay_ms = 1; params.amp_env.sustain = 1.0f; params.amp_env.release_ms = 50;
    }
    ~StrApp() { engine_synth_shutdown(); }
    void build() { engine_synth_build(&rack, &params); }
    void live() { engine_synth_set_params(&rack, &params); }
    std::vector<double> run(double seconds, std::vector<double> *right = nullptr) {
        const int frames = static_cast<int>(seconds * engine_synth_sample_rate());
        std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, 173);
            engine_synth_render(&buf[static_cast<size_t>(done) * 2], n);
            done += n;
        }
        std::vector<double> l(static_cast<size_t>(frames));
        if (right) right->resize(static_cast<size_t>(frames));
        for (int i = 0; i < frames; i++) {
            l[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2];
            if (right) (*right)[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2 + 1];
        }
        return l;
    }
};

// Power of the components that are NOT harmonics of f0 relative to the harmonic ones, in dB (Hann window, +-3 bins around each harmonic).
double inharmonic_db(const std::vector<double> &x, size_t from, size_t n, double f0) {
    const double sr = engine_synth_sample_rate();
    std::vector<double> pw(n / 2);
    for (size_t k = 1; k < n / 2; k++) {                                    // plain DFT: slow but short
        std::complex<double> acc = 0;
        for (size_t i = 0; i < n; i++) {
            const double w = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(n));
            acc += x[from + i] * w * std::polar(1.0, -2 * kPi * static_cast<double>(k * i) / static_cast<double>(n));
        }
        pw[k] = std::norm(acc);
    }
    double harm = 0, other = 0;
    for (size_t k = 1; k < n / 2; k++) {
        const double f = static_cast<double>(k) * sr / static_cast<double>(n);
        const double h = std::round(f / f0);
        const bool near = h >= 1 && std::fabs(f - h * f0) <= 3.0 * sr / static_cast<double>(n);
        (near ? harm : other) += pw[k];
    }
    return 10.0 * std::log10(other / harm);
}
}  // namespace

TEST(mip_band_keeps_every_harmonic_below_nyquist) {
    for (double hz : {20.0, 110.0, 440.0, 1000.0, 3000.0, 8000.0}) {
        const uint32_t inc = static_cast<uint32_t>(hz / engine_synth_sample_rate() * 4294967296.0);
        const int k = mip_band(inc);
        const double top = static_cast<double>(512 >> k) * hz;
        CHECK(top < engine_synth_sample_rate() / 2.0 || k == kMipBands - 1);
        if (k > 0) CHECK(static_cast<double>(512 >> (k - 1)) * hz >= engine_synth_sample_rate() / 2.0);   // the fullest band that fits
    }
}

TEST(strings_naive_vs_mipmap_aliasing) {
    StrApp a;
    a.params.str.detune = 0;
    a.params.str.mix = 0;                                                   // one oscillator: a clean spectrum
    const int note = 96;                                                    // C7, 2093 Hz: a naive saw aliases a lot up there
    const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    double db[2];
    for (int osc = 0; osc < 2; osc++) {
        a.params.str.osc = static_cast<uint8_t>(osc);
        a.build();
        engine_synth_note_on(note);
        std::vector<double> y = a.run(0.25);
        engine_synth_note_off(note);
        a.run(0.2);
        db[osc] = inharmonic_db(y, 2048, 4096, f0);
    }
    std::printf("    C7 saw, inharmonic / harmonic power: naive %.1f dB, mipmap %.1f dB\n", db[0], db[1]);
    CHECK(db[0] > -40.0);                                                   // the naive one does alias (otherwise the measure is wrong)
    CHECK(db[1] < db[0] - 15.0);
}

TEST(strings_has_32_real_voices) {
    StrApp a;
    a.params.str.level = 0.3f;
    a.params.str.detune = 7;
    a.build();
    auto play = [&](int n) {
        for (int i = 0; i < n; i++) engine_synth_note_on(36 + i * 2);
        std::vector<double> y = a.run(0.3);
        for (int i = 0; i < n; i++) engine_synth_note_off(36 + i * 2);
        a.run(0.3);
        return rms(std::vector<double>(y.begin() + 4000, y.end()));
    };
    const double r8 = play(8), r32 = play(32);
    a.rack.cfg.str_voices = 8;
    a.build();
    const double r32_on_8 = play(32);
    std::printf("    rms: 8 notes %.0f, 32 notes %.0f, 32 notes on 8 voices %.0f\n", r8, r32, r32_on_8);
    CHECK(r32 > r8 * 1.5);                                                 // uncorrelated voices: about sqrt(4) = 2x
    CHECK(r32_on_8 < r32 * 0.75);                                          // with 8 voices the extra notes steal
    CHECK_EQ(synth_config_voices(&a.rack.cfg), 8);
}

TEST(strings_envelope_attack_and_release_times) {
    StrApp a;
    a.params.amp_env.attack_ms = 200;
    a.params.amp_env.release_ms = 300;
    a.params.str.mix = 0;                                                   // one oscillator: two detuned ones beat, and their peak is not the envelope
    a.build();
    engine_synth_note_on(60);
    std::vector<double> y = a.run(0.4);
    const double sr = engine_synth_sample_rate();
    auto win_peak = [&](const std::vector<double> &v, double t) {
        double p = 0;
        for (size_t i = static_cast<size_t>(t * sr); i < static_cast<size_t>((t + 0.01) * sr) && i < v.size(); i++) p = std::fmax(p, std::fabs(v[i]));
        return p;
    };
    const double full = win_peak(y, 0.3), half = win_peak(y, 0.095);
    std::printf("    attack 200 ms: level at 100 ms %.2f of full\n", half / full);
    CHECK_NEAR(half / full, 0.5, 0.12);                                    // linear: half way at half the time
    engine_synth_note_off(60);
    std::vector<double> r = a.run(0.5);
    const double mid = win_peak(r, 0.145) / full, end = win_peak(r, 0.35);
    std::printf("    release 300 ms: %.2f at 150 ms, peak %.0f after 350 ms\n", mid, end);
    CHECK_NEAR(mid, 0.5, 0.12);
    CHECK(end == 0.0);                                                      // silent, and the voice is freed
}

TEST(strings_voice_lowpass_darkens_and_switches_live) {
    StrApp a;
    a.build();
    engine_synth_note_on(48);
    std::vector<double> y = a.run(0.3);
    const double open = rms(y);
    double rough_open = 0, rough_lp = 0;
    for (size_t i = 5001; i + 1 < y.size(); i++) rough_open += std::fabs(y[i + 1] - 2 * y[i] + y[i - 1]);
    a.params.str.lp_on = 1;
    a.params.str.lp_cut = 300;
    a.params.str.lp_env = 0;
    a.live();                                                               // a parameter of the voice module: no rebuild
    std::vector<double> z = a.run(0.3);
    for (size_t i = 5001; i + 1 < z.size(); i++) rough_lp += std::fabs(z[i + 1] - 2 * z[i] + z[i - 1]);
    std::printf("    roughness open %.0f, lowpass 300 Hz %.0f (rms %.0f -> %.0f)\n", rough_open, rough_lp, open, rms(z));
    CHECK(rough_lp < rough_open * 0.3);
}

TEST(strings_shared_filter_and_ensemble) {
    StrApp a;
    a.params.str.ftype = FILT_LP;
    a.params.str.fcut = 400;
    a.build();
    for (int n : {48, 55, 60, 64}) engine_synth_note_on(n);
    std::vector<double> r, l = a.run(0.4, &r);
    double diff = 0;
    for (size_t i = 0; i < l.size(); i++) diff = std::fmax(diff, std::fabs(l[i] - r[i]));
    CHECK(rms(l) > 100.0);
    CHECK(diff == 0.0);                                                     // no ensemble: both channels identical
    CHECK(rack_add_m(&a.rack, MOD_ENSEMBLE) != RACK_NONE);
    a.build();
    std::vector<double> r2, l2 = a.run(0.6, &r2);
    double c = 0, el = 0, er = 0;
    for (size_t i = 8000; i < l2.size(); i++) { c += l2[i] * r2[i]; el += l2[i] * l2[i]; er += r2[i] * r2[i]; }
    const double corr = c / std::sqrt(el * er);
    std::printf("    ensemble: L/R correlation %.3f, rms %.0f\n", corr, rms(l2));
    CHECK(corr < 0.98);                                                     // the three taps make the channels differ
    CHECK(corr > 0.3);
}
