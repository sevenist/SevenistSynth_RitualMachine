// Mutable Instruments models (MiOsc, ADR-039): every model sounds and stays in range, the pitched ones play the note's pitch, the percussive
// ones are struck by the gate. A host cost table is printed (relative ranking; the board numbers come from serial_test.py).
#include <chrono>
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
#include <string>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "rig.h"
#include "core/rack.h"

using namespace tst;

namespace {

struct MiBench {
    DspRig rig;
    explicit MiBench(int model, int timbre = 16384, int morph = 16384, int harm = 16384) {
        GraphDesc g;
        rig.add(g, 1, T_NOTE_IN);
        NodeDesc *o = rig.add(g, 2, T_MIOSC);
        o->param[MI_MODEL] = model; o->param[MI_TIMBRE] = timbre; o->param[MI_MORPH_P] = morph; o->param[MI_HARM] = harm;
        rig.add(g, 3, T_VOICE_OUT)->param[VO_TAIL_MS] = 3000;
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(1, 1, 2, Dst::In, 1);
        g.connect(2, 0, 3, Dst::In, 0);
        const Err e = rig.eng.load(g);
        if (e != Err::Ok) std::printf("    load error %s (node %p)\n", err_name(e), static_cast<void *>(o));
        CHECK(e == Err::Ok);
    }
    std::vector<double> play(int note, double seconds) {
        std::vector<double> y;
        rig.eng.note_on(note);
        rig.run(DspRig::blocks_for(seconds), &y);
        return y;
    }
};

double rms_of(const std::vector<double> &x, double t0, double t1) {
    const size_t a = static_cast<size_t>(t0 * kSampleRate), b = std::min(x.size(), static_cast<size_t>(t1 * kSampleRate));
    double s = 0;
    for (size_t i = a; i < b; i++) s += x[i] * x[i];
    return b > a ? std::sqrt(s / static_cast<double>(b - a)) : 0.0;
}
double peak_of(const std::vector<double> &x) { double p = 0; for (double v : x) p = std::fmax(p, std::fabs(v)); return p; }
bool finite_all(const std::vector<double> &x) { for (double v : x) if (!std::isfinite(v)) return false; return true; }
double autocorr_at(const std::vector<double> &x, size_t lag, size_t from, size_t to) {
    double a = 0, e0 = 0, e1 = 0;
    for (size_t i = from; i + lag < to && i + lag < x.size(); i++) { a += x[i] * x[i + lag]; e0 += x[i] * x[i]; e1 += x[i + lag] * x[i + lag]; }
    return a / std::sqrt(e0 * e1 + 1e-9);
}
bool percussive(int m) { return m == MI_KICK || m == MI_SNARE || m == MI_P_BASS_DRUM || m == MI_P_BASS_SYN || m == MI_P_SNARE ||
                                  m == MI_P_SNARE_SYN || m == MI_P_HIHAT || m == MI_P_HIHAT_2; }

}  // namespace

TEST(mi_osc_is_registered_as_a_voice_module) {
    DspRig rig;
    const ModuleType *t = rig.eng.registry().get(T_MIOSC);                       // a duplicate id is refused silently by the registry
    CHECK(t && std::string(t->info->name) == "MiOsc" && t->info->scope == Scope::Voice);
}

// The rack's Wav list (core/rack.c, C) names the models in the order of MiModel (mi_osc.h): the mapper sends Wav - OC_FIRST_MI as the model.
TEST(mi_rack_wave_names_follow_the_models) {
    CHECK_EQ(OC_WAVE_COUNT, OC_FIRST_MI + MI_MODELS);
    rack_slot_t s{};
    s.type = MOD_OSC;
    for (int m = 0; m < MI_MODELS; m++) {
        char name[24];
        s.v[MP_OC_WAVE] = static_cast<float>(OC_FIRST_MI + m);
        rack_mparam_format(&s, MP_OC_WAVE, name, sizeof name);
        if (std::string(name) != mi_model_name(m)) std::printf("    model %d: rack \"%s\", engine \"%s\"\n", m, name, mi_model_name(m));
        CHECK(std::string(name) == mi_model_name(m));
    }
}

TEST(every_mi_model_sounds_and_stays_in_range) {
    for (int m = 0; m < MI_MODELS; m++) {
        MiBench b(m);
        std::vector<double> y = b.play(57, 0.6);                                   // A3
        const double r = percussive(m) ? rms_of(y, 0.0, 0.15) : rms_of(y, 0.05, 0.5), pk = peak_of(y);
        std::printf("    %-7s rms %6.0f  peak %6.0f\n", mi_model_name(m), r, pk);
        CHECK(finite_all(y));
        CHECK(r > 50.0);
        CHECK(pk < 32767.0);
    }
}

// The pitched models repeat at the note's period (autocorrelation), at three notes across the range.
TEST(mi_pitched_models_play_the_note) {
    static const int pitched[] = {MI_CSAW, MI_MORPH, MI_SAW_SQUARE, MI_SINE_TRI, MI_BUZZ, MI_SQUARE_SYNC, MI_SAW_SYNC, MI_ZLP, MI_VOSIM,
                                  MI_WAVETABLE, MI_P_VA_VCF, MI_P_PHASE_DIST};   // (3Sine: a chord; VA: Harm detunes its pair)
    for (int m : pitched) {
        double worst = 1.0;
        for (int note : {45, 57, 69}) {
            MiBench b(m, 8000, 8000, 0);
            std::vector<double> y = b.play(note, 0.4);
            const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const size_t lag = static_cast<size_t>(std::lround(kSampleRate / hz));
            worst = std::fmin(worst, autocorr_at(y, lag, static_cast<size_t>(0.1 * kSampleRate), static_cast<size_t>(0.35 * kSampleRate)));
        }
        std::printf("    %-7s periodic at the note: worst correlation %.3f\n", mi_model_name(m), worst);
        CHECK(worst > 0.8);
    }
}

// The percussive models are silent before the first note and struck by the gate.
TEST(mi_percussive_models_are_struck_by_the_gate) {
    for (int m : {MI_KICK, MI_SNARE, MI_P_BASS_DRUM, MI_P_BASS_SYN, MI_P_SNARE, MI_P_SNARE_SYN, MI_P_HIHAT, MI_P_HIHAT_2}) {
        MiBench b(m);
        std::vector<double> y = b.play(48, 1.2);
        const double hit = rms_of(y, 0.0, 0.1), late = rms_of(y, 1.0, 1.2);
        std::printf("    %-7s hit %6.0f, after 1 s %6.0f\n", mi_model_name(m), hit, late);
        CHECK(hit > 200.0);
        CHECK(late < hit);                                                         // it decays by itself
    }
}

// Host cost per model, 1 voice (relative ranking only; see serial_test.py for the board).
TEST(mi_model_host_cost) {
#if defined(__SSE__) || defined(__x86_64__)
    const unsigned csr = _mm_getcsr();
    if (std::getenv("MI_FTZ")) _mm_setcsr(csr | 0x8040);                          // flush denormals to zero (experiment: MI_FTZ=1)
#endif
    for (int m = 0; m < MI_MODELS; m++) {
        MiBench b(m);
        b.rig.eng.note_on(57);
        b.rig.run(8);
        const int blocks = DspRig::blocks_for(0.1);                                 // the first 100 ms: a struck model is still ringing
        const auto t0 = std::chrono::steady_clock::now();
        b.rig.run(blocks);
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        std::printf("    %-7s %6.1f ns / sample (whole graph: note in, model, voice out)\n", mi_model_name(m), ns / (blocks * kBlock));
    }
    for (int e : {OSCX_KARP, OSCX_SSAW, OSCX_ADD}) {                                // our engines, same graph, as the reference
        DspRig rig;
        GraphDesc g;
        rig.add(g, 1, T_NOTE_IN);
        rig.add(g, 2, T_OSCX)->param[OSCX_ENGINE] = e;
        rig.add(g, 3, T_VOICE_OUT);
        g.connect(1, 0, 2, Dst::In, 0); g.connect(1, 1, 2, Dst::In, 1); g.connect(2, 0, 3, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.eng.note_on(57);
        rig.run(8);
        const int blocks = DspRig::blocks_for(0.1);
        const auto t0 = std::chrono::steady_clock::now();
        rig.run(blocks);
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        std::printf("    ours:   %6.1f ns / sample (reference: OscEngines %s)\n", ns / (blocks * kBlock),
                    e == OSCX_KARP ? "Karp" : e == OSCX_SSAW ? "SSaw" : "Add");
    }
#if defined(__SSE__) || defined(__x86_64__)
    _mm_setcsr(csr);
#endif
}
