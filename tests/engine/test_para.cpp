// Mod Para (ADR-036 stage 2): the voices play the sources, one shared filter / amp chain after their sum, three envelope policies.
// And the rack oscillator's table qualities (ADR-037): Mip / Naive on the plain waves, the Strng engine. Through the production path.
#include <algorithm>
#include <cmath>
#include <vector>
#include "rig.h"
#include "platform/engine/engine_synth.h"

extern "C" {
#include "core/rack.h"
#include "core/synth_config.h"
#include "core/synth_params.h"
}

using namespace tst;

namespace {
const double kPi = 3.14159265358979323846;

struct ParaApp {
    std::vector<uint8_t> fast, bulk;
    rack_t rack;
    synth_params_t params;
    explicit ParaApp(int policy) : fast(6u << 20), bulk(6u << 20) {
        CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
        synth_params_default(&params);
        rack_init(&rack);                                           // OSC -> FILTER -> SAT, LFO -> cutoff
        rack.slot[3].v[MP_LF_DEPTH] = 0;                            // no LFO wobble in the measurements
        rack.slot[0].v[MP_OC_WAVE] = 0;                             // a sine: one clear line per key
        rack.slot[1].v[MP_FL_CUT] = 12000;
        rack.slot[1].v[MP_FL_ENVAMT] = 0;
        rack.cfg.type = SYNTH_MOD_PARA;
        rack.cfg.voices = 4;
        rack.cfg.para_env = static_cast<uint8_t>(policy);
        for (int k = 0; k < FXR_SLOTS; k++) fxr_set_type(&rack.cfg.fxr.slot[k], FX_NONE);
        params.amp_env.attack_ms = 1; params.amp_env.decay_ms = 1; params.amp_env.sustain = 1.0f; params.amp_env.release_ms = 400;
        params.amp_env.a_curve = params.amp_env.d_curve = params.amp_env.r_curve = 0;
    }
    ~ParaApp() { engine_synth_shutdown(); }
    void build() { engine_synth_build(&rack, &params); }
    std::vector<double> run(double seconds) {
        const int frames = static_cast<int>(seconds * engine_synth_sample_rate());
        std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, 173);
            engine_synth_render(&buf[static_cast<size_t>(done) * 2], n);
            done += n;
        }
        std::vector<double> l(static_cast<size_t>(frames));
        for (int i = 0; i < frames; i++) l[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2];
        return l;
    }
};

double power_at(const std::vector<double> &x, size_t from, size_t n, double hz) {     // Goertzel, Hann window
    CHECK(from + n <= x.size());                                      // a window past the end is a test bug (renders are shorter at 32 kHz)
    if (from + n > x.size()) n = from < x.size() ? x.size() - from : 0;
    const double w = 2 * kPi * hz / engine_synth_sample_rate();
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) {
        const double win = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(n));
        const double s0 = x[from + i] * win + 2 * std::cos(w) * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - 2 * std::cos(w) * s1 * s2;
}
double hz_of(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }
double rms_of(const std::vector<double> &x, size_t from, size_t to) {
    double s = 0;
    for (size_t i = from; i < to; i++) s += x[i] * x[i];
    return std::sqrt(s / static_cast<double>(to - from));
}
}  // namespace

TEST(para_plays_every_key_through_one_shared_filter) {
    ParaApp a(PARA_ENV_LEGATO);
    a.build();
    engine_synth_note_on(60);
    engine_synth_note_on(67);
    std::vector<double> y = a.run(0.5);
    const double c4 = power_at(y, 4000, 8192, hz_of(60)), g4 = power_at(y, 4000, 8192, hz_of(67));
    std::printf("    Mod Para, two keys: power at C4 %.3g, at G4 %.3g\n", c4, g4);
    CHECK(c4 > 1e6 && g4 > 1e6);
    CHECK(c4 / g4 < 4.0 && g4 / c4 < 4.0);
}

TEST(para_released_key_stops_while_others_hold_and_the_last_chord_rings_through_the_release) {
    ParaApp a(PARA_ENV_LEGATO);
    a.build();
    engine_synth_note_on(60);
    engine_synth_note_on(67);
    a.run(0.2);
    engine_synth_note_off(60);                                       // G4 still held: C4 must stop
    std::vector<double> y = a.run(0.2);
    const double c4 = power_at(y, 2000, 4096, hz_of(60)), g4 = power_at(y, 2000, 4096, hz_of(67));
    std::printf("    C4 released, G4 held: C4 %.3g, G4 %.3g\n", c4, g4);
    CHECK(c4 < g4 * 1e-3);
    engine_synth_note_on(64);
    a.run(0.1);
    engine_synth_note_off(64);
    engine_synth_note_off(67);                                       // the last keys go up together: both keep sounding in the shared release
    std::vector<double> r = a.run(0.6);
    const double sr = engine_synth_sample_rate();
    const double e4 = power_at(r, static_cast<size_t>(0.02 * sr), 4096, hz_of(64)), g4r = power_at(r, static_cast<size_t>(0.02 * sr), 4096, hz_of(67));
    const double early = rms_of(r, 0, static_cast<size_t>(0.05 * sr)), late = rms_of(r, static_cast<size_t>(0.45 * sr), static_cast<size_t>(0.6 * sr));
    std::printf("    release: E4 %.3g, G4 %.3g, rms 0..50 ms %.0f, 450..600 ms %.0f\n", e4, g4r, early, late);
    CHECK(e4 > 1e5 && g4r > 1e5);
    CHECK(late < early * 0.05);                                      // the shared 400 ms release has finished
}

TEST(para_envelope_policies_legato_retrigger_and_per_voice) {
    const double sr = engine_synth_sample_rate();
    double bump[2] = {0, 0};
    for (int policy : {PARA_ENV_LEGATO, PARA_ENV_RETRIG}) {
        ParaApp a(policy);
        a.params.amp_env.attack_ms = 100;
        a.params.amp_env.decay_ms = 50;
        a.params.amp_env.sustain = 0.3f;
        a.build();
        engine_synth_note_on(60);
        std::vector<double> y1 = a.run(0.4);                             // at sustain
        const double sus = rms_of(y1, static_cast<size_t>(0.3 * sr), static_cast<size_t>(0.4 * sr));
        engine_synth_note_on(72);                                        // a second key
        std::vector<double> y2 = a.run(0.15);
        const double after = rms_of(y2, static_cast<size_t>(0.06 * sr), static_cast<size_t>(0.1 * sr));
        // two keys: the level rises by the second line anyway (about sqrt 2); a retrigger climbs further towards the peak
        bump[policy] = after / sus;
        std::printf("    policy %s: rms at sustain %.0f, after a second key %.0f (x %.2f)\n", policy == PARA_ENV_LEGATO ? "Legato" : "Retrig", sus, after, after / sus);
    }
    CHECK(bump[PARA_ENV_LEGATO] < 1.8);
    CHECK(bump[PARA_ENV_RETRIG] > bump[PARA_ENV_LEGATO] * 1.5);

    ParaApp v(PARA_ENV_VOICE);                                            // every voice its own amp envelope: a released key decays alone
    v.params.amp_env.release_ms = 200;
    v.build();
    engine_synth_note_on(60);
    engine_synth_note_on(67);
    v.run(0.2);
    engine_synth_note_off(60);
    std::vector<double> y = v.run(0.6);                                  // (the 4096-sample window at 300 ms must fit at 32 kHz too)
    const double c_early = power_at(y, 0, 2048, hz_of(60)), c_late = power_at(y, static_cast<size_t>(0.3 * sr), 4096, hz_of(60));
    const double g_late = power_at(y, static_cast<size_t>(0.3 * sr), 4096, hz_of(67));
    std::printf("    policy Voice: C4 just after its release %.3g, 300 ms later %.3g; G4 held %.3g\n", c_early, c_late, g_late);
    CHECK(c_early > c_late * 100);
    CHECK(g_late > 1e6);
}

TEST(rack_oscillator_quality_and_the_strings_engine) {
    // plain saw: Blep vs Mip vs Naive, high note, through the poly modular rack (filter off, saturator dry: they would add their own content)
    double inh[3];
    for (int qual = 0; qual < 3; qual++) {
        ParaApp a(PARA_ENV_LEGATO);
        a.rack.cfg.type = SYNTH_MODULAR;
        a.rack.slot[0].v[MP_OC_WAVE] = 3;                                 // SawUp
        a.rack.slot[0].v[MP_OC_QUAL] = static_cast<float>(qual);
        a.rack.slot[1].v[MP_FL_TYPE] = FILT_OFF;
        a.rack.slot[2].v[MP_SA_MIX] = 0;
        a.build();
        const int note = 96;
        engine_synth_note_on(note);
        std::vector<double> y = a.run(0.3);
        // power away from the harmonics relative to the harmonics (plain DFT, Hann window, +-3 bins around each harmonic)
        const size_t n = 4096, from = 4096;
        const double sr = engine_synth_sample_rate(), f0 = hz_of(note);
        double harm = 0, other = 0;
        for (size_t k = 1; k < n / 2; k++) {
            const double f = static_cast<double>(k) * sr / static_cast<double>(n);
            const double p = power_at(y, from, n, f);
            const double h = std::round(f / f0);
            (h >= 1 && std::fabs(f - h * f0) <= 3.0 * sr / static_cast<double>(n) ? harm : other) += p;
        }
        inh[qual] = 10 * std::log10(other / harm);
    }
    std::printf("    C7 saw, inharmonic / harmonic power: Blep %.1f dB, Mip %.1f dB, Naive %.1f dB\n", inh[0], inh[1], inh[2]);
    CHECK(inh[1] < inh[2] - 15.0);                                       // the tables remove most of the aliasing
    CHECK(inh[0] < inh[2] - 10.0);

    // the Strng engine: plays at the key, Morph picks the wave, Naive / Mip both sound
    for (int qual : {1, 2}) {
        ParaApp a(PARA_ENV_LEGATO);
        a.rack.cfg.type = SYNTH_MODULAR;
        a.rack.slot[0].v[MP_OC_WAVE] = static_cast<float>(OC_FIRST_ENGINE + 8);
        a.rack.slot[0].v[MP_OC_PW] = 0;                                   // Timbre 0: no detune
        a.rack.slot[0].v[MP_OC_MORPH] = 0;                                // saw
        a.rack.slot[0].v[MP_OC_QUAL] = static_cast<float>(qual);
        a.build();
        engine_synth_note_on(57);
        std::vector<double> y = a.run(0.5);
        const double at = power_at(y, 4096, 8192, hz_of(57)), off = power_at(y, 4096, 8192, hz_of(57) * 1.5);
        std::printf("    Strng engine (%s): power at A3 %.3g, between harmonics %.3g, rms %.0f\n", qual == 1 ? "Mip" : "Naive", at, off, rms(y));
        CHECK(at > off * 1e3);
    }
}
