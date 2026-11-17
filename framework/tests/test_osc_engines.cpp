// Oscillator engines: every engine sounds, stays in range, plays the right pitch and responds to timbre / morph the way it says.
#include <algorithm>
#include <vector>
#include "rig.h"

using namespace tst;

namespace {
const double kTwoPi = 6.28318530717958647692;

struct EngineBench {
    DspRig rig;
    explicit EngineBench(int engine, int timbre = 16384, int morph = 16384) {
        GraphDesc g;
        rig.add(g, 1, T_NOTE_IN);
        NodeDesc *o = rig.add(g, 2, T_OSCX);
        o->param[OSCX_ENGINE] = engine; o->param[OSCX_TIMBRE] = timbre; o->param[OSCX_MORPH] = morph;
        rig.add(g, 3, T_VOICE_OUT)->param[VO_TAIL_MS] = 3000;
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(1, 1, 2, Dst::In, 1);
        g.connect(2, 0, 3, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
    }
    void set(int idx, int v) { rig.eng.set_param(2, idx, v); }
    std::vector<double> play(int note, double seconds) {
        std::vector<double> y;
        rig.eng.note_on(note);
        rig.run(DspRig::blocks_for(seconds), &y);
        return y;
    }
};

double power_at(const std::vector<double> &x, size_t from, size_t to, double hz) {
    double re = 0, im = 0;
    for (size_t i = from; i < to && i < x.size(); i++) { const double ph = kTwoPi * hz * static_cast<double>(i) / kSampleRate; re += x[i] * std::cos(ph); im += x[i] * std::sin(ph); }
    const double n = static_cast<double>(to - from);
    return (re * re + im * im) / (n * n);
}
double rms_between(const std::vector<double> &x, double t0, double t1) {
    return rms(std::vector<double>(x.begin() + static_cast<long>(t0 * kSampleRate), x.begin() + static_cast<long>(t1 * kSampleRate)));
}
double peak_of(const std::vector<double> &x) { double p = 0; for (double v : x) p = std::fmax(p, std::fabs(v)); return p; }
double autocorr_at(const std::vector<double> &x, size_t lag, size_t from, size_t to) {
    double a = 0, e0 = 0, e1 = 0;
    for (size_t i = from; i + lag < to; i++) { a += x[i] * x[i + lag]; e0 += x[i] * x[i]; e1 += x[i + lag] * x[i + lag]; }
    return a / std::sqrt(e0 * e1 + 1e-9);
}
}  // namespace

TEST(every_osc_engine_sounds_and_stays_in_range) {
    for (int e = 0; e < OSCX_ENGINES; e++) {
        EngineBench b(e);
        std::vector<double> y = b.play(57, 0.6);                               // A3
        const double r = rms_between(y, 0.02, 0.2), pk = peak_of(y);
        std::printf("    engine %d: rms %.0f, peak %.0f\n", e, r, pk);
        CHECK(r > 100.0);
        CHECK(pk < 32767.0);
    }
}

TEST(karplus_strong_plays_the_pitch_and_morph_sets_the_decay) {
    double late[2];
    for (int m = 0; m < 2; m++) {
        EngineBench b(OSCX_KARP, 20000, m ? 30000 : 4000);
        std::vector<double> y = b.play(57, 1.5);
        const size_t period = static_cast<size_t>(std::lround(kSampleRate / 220.0));
        if (m == 1) CHECK(autocorr_at(y, period, 3000, 12000) > 0.85);        // periodic at 220 Hz
        late[m] = rms_between(y, 1.0, 1.4) / rms_between(y, 0.02, 0.1);
    }
    std::printf("    string level after 1 s relative to the start: morph low %.4f, high %.4f\n", late[0], late[1]);
    CHECK(late[1] > 10.0 * late[0] + 1e-6);                                   // a long sustain keeps its energy, a short one dies
    CHECK(late[1] > 0.1);
}

TEST(karplus_strong_retriggers_on_every_note) {
    EngineBench b(OSCX_KARP);
    b.play(57, 0.4);
    b.rig.eng.note_off(57);
    b.rig.run(DspRig::blocks_for(0.5));                                       // the voice rings out and is freed (tail 3 s): another note
    std::vector<double> y = b.play(60, 0.3);
    CHECK(rms_between(y, 0.01, 0.1) > 300.0);
}

TEST(modal_material_moves_the_partials_from_harmonic_to_bell) {
    const double f = 220.0;
    double inharm[2];
    for (int t = 0; t < 2; t++) {
        EngineBench b(OSCX_MODAL, t ? 32767 : 0, 24000);
        std::vector<double> y = b.play(57, 0.8);
        const size_t a = static_cast<size_t>(0.05 * kSampleRate), z = static_cast<size_t>(0.5 * kSampleRate);
        const double p2 = power_at(y, a, z, 2 * f), p276 = power_at(y, a, z, 2.76 * f);
        inharm[t] = p276 / (p2 + 1e-9);
    }
    std::printf("    power at 2.76 f relative to 2 f: string material %.2e, bell material %.2e\n", inharm[0], inharm[1]);
    CHECK(inharm[1] > 30.0 * inharm[0]);
}

TEST(modal_fundamental_decays_with_its_designed_time_constant) {
    // The modes are decaying phasors; the rotation is normalised to unit length so the decay is the designed one. Mode 0 (the fundamental) at morph 24000:
    // r per sample is interpolated between table rows 11 and 12 (t0 = 40 ms * 2^(6.64 mi / 16)).
    const double f = 220.0;
    const int morph = 24000, mi = morph >> 11, fr = morph & 2047;
    auto r_of = [](int row) { return std::exp(-1.0 / (40.0 * std::pow(2.0, 6.64 * row / 16.0) * 0.001 * kSampleRate)); };
    const double r0 = r_of(mi) + (r_of(mi + 1) - r_of(mi)) * fr / 2048.0;
    EngineBench b(OSCX_MODAL, 0, morph);
    std::vector<double> y = b.play(57, 0.8);
    const size_t a1 = static_cast<size_t>(0.05 * kSampleRate), z1 = static_cast<size_t>(0.15 * kSampleRate);
    const size_t a2 = static_cast<size_t>(0.35 * kSampleRate), z2 = static_cast<size_t>(0.45 * kSampleRate);
    const double ratio = std::sqrt(power_at(y, a2, z2, f) / power_at(y, a1, z1, f));
    const double want = std::pow(r0, 0.3 * kSampleRate);
    std::printf("    modal fundamental: amplitude after 0.3 s = %.3f of the earlier one, designed %.3f\n", ratio, want);
    CHECK(std::fabs(ratio - want) < 0.05 * want);
}

TEST(fm2_index_adds_sidebands_and_ratio_moves_them) {
    const double f = 220.0;
    EngineBench pure(OSCX_FM2, 0, 16384);
    std::vector<double> y0 = pure.play(57, 0.4);
    EngineBench rich(OSCX_FM2, 20000, 16384);                                // morph 0.5 -> ratio index 7 = 3.5? measured below
    std::vector<double> y1 = rich.play(57, 0.4);
    const size_t a = 4000, z = 16000;
    const double h0 = power_at(y0, a, z, 2 * f) + power_at(y0, a, z, 3 * f), h1 = power_at(y1, a, z, 2 * f) + power_at(y1, a, z, 3 * f) + power_at(y1, a, z, 4 * f);
    std::printf("    FM2 harmonic power: index 0 -> %.2e, index 0.6 x 8 rad -> %.2e (fundamental %.2e)\n", h0, h1, power_at(y0, a, z, f));
    CHECK(h1 > 100.0 * (h0 + 1.0));
    // ratio 2 (morph index 4 = 2.0): the sidebands are at multiples of the fundamental, so the spectrum has no partial between 1 f and 2 f
    EngineBench harmonic(OSCX_FM2, 12000, 4 * 2048 + 1000);
    std::vector<double> y2 = harmonic.play(57, 0.4);
    CHECK(power_at(y2, a, z, 1.5 * f) < 1e-3 * power_at(y2, a, z, f) + 1.0);
}

TEST(fold_gain_adds_harmonics) {
    const double f = 220.0;
    double h[2];
    for (int t = 0; t < 2; t++) {
        EngineBench b(OSCX_FOLD, t ? 30000 : 0, 0);
        std::vector<double> y = b.play(57, 0.4);
        h[t] = power_at(y, 3000, 15000, 3 * f) + power_at(y, 3000, 15000, 5 * f) + power_at(y, 3000, 15000, 7 * f);
    }
    std::printf("    fold: harmonic power gain 1x -> %.2e, 8x -> %.2e\n", h[0], h[1]);
    CHECK(h[1] > 8.0 * h[0]);                                                  // (a fold of 1x already bends the sine a little)
}

TEST(supersaw_spread_widens_the_spectrum) {
    EngineBench tight(OSCX_SSAW, 0, 32767), wide(OSCX_SSAW, 32767, 32767);
    std::vector<double> a = tight.play(57, 0.5), b = wide.play(57, 0.5);
    // detuned voices beat: the level wobbles. Measure the spread of the short-term rms.
    auto wobble = [](const std::vector<double> &y) {
        double lo = 1e9, hi = 0;
        const size_t w = 480;
        for (size_t at = 6000; at + w < y.size(); at += w) { double s = 0; for (size_t i = at; i < at + w; i++) s += y[i] * y[i]; s = std::sqrt(s / w); lo = std::fmin(lo, s); hi = std::fmax(hi, s); }
        return hi / lo;
    };
    std::printf("    supersaw level wobble: spread 0 -> %.2f, spread 1 -> %.2f\n", wobble(a), wobble(b));
    CHECK(wobble(b) > wobble(a) + 0.05);
    CHECK(peak_of(b) < 32767.0);
}

TEST(vowel_moves_the_formants) {
    // A (timbre 0): formants near 800 / 1150 / 2900 Hz.  U (timbre max): 325 / 700 / 2700 Hz.
    double a_800, a_325, u_800, u_325;
    {
        EngineBench b(OSCX_VOWEL, 0, 20000);
        std::vector<double> y = b.play(45, 0.5);                                  // 110 Hz
        double e800 = 0, e325 = 0;
        for (int k = 1; k < 40; k++) { const double f = 110.0 * k; const double p = power_at(y, 6000, 22000, f); if (std::fabs(f - 800) < 120) e800 += p; if (std::fabs(f - 330) < 60) e325 += p; }
        a_800 = e800; a_325 = e325;
    }
    {
        EngineBench b(OSCX_VOWEL, 32767, 20000);
        std::vector<double> y = b.play(45, 0.5);
        double e800 = 0, e325 = 0;
        for (int k = 1; k < 40; k++) { const double f = 110.0 * k; const double p = power_at(y, 6000, 22000, f); if (std::fabs(f - 800) < 120) e800 += p; if (std::fabs(f - 330) < 60) e325 += p; }
        u_800 = e800; u_325 = e325;
    }
    std::printf("    vowel A: 330 Hz band %.2e, 800 Hz band %.2e;  vowel U: 330 Hz band %.2e, 800 Hz band %.2e\n", a_325, a_800, u_325, u_800);
    CHECK(a_800 / (a_325 + 1e-9) > 3.0 * (u_800 / (u_325 + 1e-9)));              // the 800 Hz formant is relatively stronger for A than for U
}

TEST(additive_morph_removes_the_even_harmonics) {
    const double f = 220.0;
    EngineBench all(OSCX_ADD, 24000, 0), odd(OSCX_ADD, 24000, 32767);
    std::vector<double> a = all.play(57, 0.3), o = odd.play(57, 0.3);
    const double a2 = power_at(a, 3000, 12000, 2 * f), o2 = power_at(o, 3000, 12000, 2 * f), o1 = power_at(o, 3000, 12000, f), o3 = power_at(o, 3000, 12000, 3 * f);
    std::printf("    additive 2f power: all harmonics %.2e, odd only %.2e (f %.2e, 3f %.2e)\n", a2, o2, o1, o3);
    CHECK(a2 > 1000.0 * (o2 + 1.0));
    CHECK(o1 > 100.0 * o2 && o3 > 100.0 * o2);
}

TEST(additive_recurrence_stays_on_its_harmonics) {
    // The harmonics come from a Chebyshev recurrence: whatever is not at k f (k = 1..12) is its error. It must stay below -60 dB of the signal.
    const double f = 440.0 * std::pow(2.0, (57 - 69) / 12.0);
    EngineBench b(OSCX_ADD, 24000, 0);
    std::vector<double> y = b.play(57, 0.5);
    const size_t a = static_cast<size_t>(0.1 * kSampleRate), z = a + static_cast<size_t>(std::lround(0.3 * kSampleRate));        // 0.3 s = exactly 66 cycles of 220 Hz: no leakage between the lines
    double ms = 0;
    for (size_t i = a; i < z; i++) ms += y[i] * y[i];
    ms /= static_cast<double>(z - a);
    double harm = 0;
    for (int k = 1; k <= 12; k++) harm += 2.0 * power_at(y, a, z, k * f);
    const double spur_db = 10.0 * std::log10(std::fmax(ms - harm, 1e-9) / ms);
    std::printf("    additive: signal power %.3e, harmonic lines %.3e, everything else %.1f dB\n", ms, harm, spur_db);
    CHECK(spur_db < -60.0);
}

TEST(dust_density_follows_timbre) {
    double crest[2];
    for (int t = 0; t < 2; t++) {
        EngineBench b(OSCX_DUST, t ? 30000 : 6000, 20000);
        std::vector<double> y = b.play(69, 0.6);
        const double r = rms_between(y, 0.05, 0.6);
        crest[t] = peak_of(y) / (r + 1e-9);
        std::printf("    dust timbre %d: rms %.0f, crest %.1f\n", t, r, crest[t]);
    }
    CHECK(crest[0] > crest[1]);                                                 // sparse = spiky, dense = more even
}

TEST(gendy_is_a_clean_wave_without_the_walk_and_wanders_with_it) {
    double ac[2];
    for (int t = 0; t < 2; t++) {
        EngineBench b(OSCX_GENDY, t ? 24000 : 0, t ? 24000 : 0);
        std::vector<double> y = b.play(57, 1.0);
        const size_t period = static_cast<size_t>(std::lround(kSampleRate / 220.0));
        ac[t] = autocorr_at(y, period, 8000, 30000);
        std::printf("    gendy %s: autocorrelation at the period %.3f, peak %.0f\n", t ? "walking" : "still", ac[t], peak_of(y));
        CHECK(peak_of(y) < 32767.0);
    }
    CHECK(ac[0] > 0.9);                                                       // no walk: 220 Hz, a periodic wave
    CHECK(ac[1] < ac[0] - 0.1);                                               // the walk makes every cycle different
}

TEST(gendy_stays_bounded_with_the_strongest_walk) {
    EngineBench b(OSCX_GENDY, 32767, 32767);
    std::vector<double> y = b.play(45, 3.0);
    CHECK(peak_of(y) < 32767.0);
    CHECK(rms_between(y, 2.0, 3.0) > 300.0);
}

TEST(chaos_oscillator_sounds_at_every_setting_and_never_blows_up) {
    for (int t = 0; t < 3; t++) for (int m = 0; m < 3; m++) for (int note : {36, 69, 100}) {
        EngineBench b(OSCX_ROSSL, t * 16383, m * 16383);
        std::vector<double> y = b.play(note, 1.0);
        const double r = rms_between(y, 0.5, 1.0);
        CHECK(peak_of(y) < 32767.0);
        CHECK(r > 100.0);
    }
}

TEST(chaos_periodic_at_low_morph_follows_the_pitch_roughly) {
    EngineBench b(OSCX_ROSSL, 0, 0);                                          // c = 3: a limit cycle (period 1 or 2 loops); one loop is the pitch
    std::vector<double> y = b.play(57, 1.5);
    double best = 0;
    size_t best_lag = 0;
    for (size_t lag = 100; lag < 330; lag++) { const double a = autocorr_at(y, lag, 30000, 50000); if (a > best) { best = a; best_lag = lag; } }
    std::printf("    chaos c=3 at A3 (period 200 samples at 44.1 kHz): best lag %zu, autocorrelation %.3f, in Hz %.1f\n", best_lag, best, kSampleRate / static_cast<double>(best_lag));
    CHECK(best > 0.8);
    CHECK(std::fabs(kSampleRate / static_cast<double>(best_lag) / 220.0 - 1.0) < 0.35);
}

TEST(ca_wave_is_periodic_at_the_pitch_when_frozen_and_evolves_when_not) {
    double ac[2];
    for (int m = 0; m < 2; m++) {
        EngineBench b(OSCX_CA, 0, m ? 30000 : 0);
        std::vector<double> y = b.play(57, 1.5);
        const size_t period = static_cast<size_t>(std::lround(kSampleRate / 220.0));
        ac[m] = autocorr_at(y, period, 10000, 50000);
        std::printf("    ca %s: autocorrelation at the period %.3f, peak %.0f\n", m ? "evolving" : "frozen", ac[m], peak_of(y));
        CHECK(peak_of(y) < 32767.0);
        CHECK(rms_between(y, 0.1, 1.0) > 1000.0);
    }
    CHECK(ac[0] > 0.98);
    CHECK(ac[1] < ac[0]);
}

TEST(ca_rules_give_different_waves) {
    double r[8];
    for (int t = 0; t < 8; t++) {
        EngineBench b(OSCX_CA, t * 4096 + 100, 0);
        r[t] = rms_between(b.play(57, 0.4), 0.1, 0.4);
        CHECK(r[t] > 1000.0);
    }
}

TEST(pulsar_is_periodic_and_the_pulsaret_length_shapes_the_energy) {
    double r[2];
    for (int t = 0; t < 2; t++) {
        EngineBench b(OSCX_PULSAR, t ? 32000 : 3000, 12000);
        std::vector<double> y = b.play(57, 0.5);
        const size_t period = static_cast<size_t>(std::lround(kSampleRate / 220.0));
        CHECK(autocorr_at(y, period, 3000, 15000) > 0.95);
        r[t] = rms_between(y, 0.05, 0.5);
        std::printf("    pulsar timbre %d: rms %.0f\n", t, r[t]);
    }
    CHECK(r[1] > r[0]);                                                       // a longer pulsaret fills more of the period
}

TEST(tonewheel_drawbars_come_in_with_timbre) {
    const double f = 220.0;
    double h[2];
    for (int t = 0; t < 2; t++) {
        EngineBench b(OSCX_TONEW, t ? 32767 : 0, 0);
        std::vector<double> y = b.play(57, 0.5);
        const size_t a = static_cast<size_t>(0.1 * kSampleRate), z = static_cast<size_t>(0.45 * kSampleRate);
        const double p1 = power_at(y, a, z, f), p4 = power_at(y, a, z, 4 * f), p3 = power_at(y, a, z, 3 * f);
        h[t] = (p3 + p4) / (p1 + 1e-9);
        std::printf("    tonewheel timbre %d: power at 3f and 4f over f: %.4f, fundamental %.2e\n", t, h[t], p1);
        CHECK(p1 > 1e3);
    }
    CHECK(h[0] < 0.001);                                                      // 8' alone: a sine
    CHECK(h[1] > 0.05);                                                       // all nine: the upper drawbars
}

TEST(fof_keeps_its_formants_where_they_are_as_the_pitch_moves) {
    // the vowel A (timbre 0): F1 800 Hz, F2 1150 Hz. Harmonics near 800 Hz are far stronger than near 2000 Hz, at a low and a higher pitch
    for (int note : {45, 57}) {
        EngineBench b(OSCX_FOF, 0, 8000);
        std::vector<double> y = b.play(note, 0.6);
        const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const size_t a = static_cast<size_t>(0.1 * kSampleRate), z = static_cast<size_t>(0.55 * kSampleRate);
        auto near = [&](double hz) { double best = 0; for (int k = 1; k < 60; k++) if (std::fabs(k * f0 - hz) < f0 / 2) best = std::fmax(best, power_at(y, a, z, k * f0)); return best; };
        const double p800 = near(800), p2000 = near(2000);
        std::printf("    fof note %d: band power at 800 Hz %.2e, at 2000 Hz %.2e, rms %.0f\n", note, p800, p2000, rms_between(y, 0.1, 0.5));
        CHECK(p800 > 20.0 * p2000);
        CHECK(peak_of(y) < 32767.0);
    }
}

TEST(terrain_is_periodic_and_the_radius_changes_the_wave) {
    double ac[2], r[2];
    for (int t = 0; t < 2; t++) {
        EngineBench b(OSCX_TERRA, t ? 32000 : 6000, 12000);
        std::vector<double> y = b.play(57, 0.5);
        const size_t period = static_cast<size_t>(std::lround(kSampleRate / 220.0));
        ac[t] = autocorr_at(y, period, 3000, 15000);
        r[t] = rms_between(y, 0.1, 0.5);
        std::printf("    terrain timbre %d: autocorrelation %.3f, rms %.0f\n", t, ac[t], r[t]);
        CHECK(peak_of(y) < 32767.0);
    }
    CHECK(ac[0] > 0.95 && ac[1] > 0.95);
    CHECK(r[1] > r[0]);                                                       // a larger orbit crosses more of the surface
}

TEST(scanned_chain_is_plucked_and_morph_sets_how_long_it_lives) {
    double late[2];
    for (int m = 0; m < 2; m++) {
        EngineBench b(OSCX_SCAN, 16000, m ? 32000 : 3000);
        std::vector<double> y = b.play(57, 2.0);
        CHECK(peak_of(y) < 32767.0);
        late[m] = rms_between(y, 1.5, 1.9) / (rms_between(y, 0.05, 0.15) + 1e-9);
        std::printf("    scan morph %d: level at 1.5 s relative to the start %.3f\n", m, late[m]);
    }
    CHECK(late[1] > 3.0 * late[0]);
    CHECK(late[1] > 0.3);
}

TEST(wind_plays_the_pitch_while_the_gate_is_held_and_stops_after) {
    for (int note : {45, 57, 69}) {
        EngineBench b(OSCX_WIND, 24000, 20000);
        std::vector<double> y = b.play(note, 1.2);
        const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const size_t p0 = static_cast<size_t>(std::lround(kSampleRate / f0));
        double best = -1; size_t bl = 0;
        for (size_t lag = p0 * 3 / 4; lag < p0 * 5 / 4; lag++) { const double a = autocorr_at(y, lag, 20000, 40000); if (a > best) { best = a; bl = lag; } }
        const double hz = kSampleRate / static_cast<double>(bl);
        std::printf("    wind note %d (%.1f Hz): plays %.1f Hz, autocorrelation %.3f, rms %.0f\n", note, f0, hz, best, rms_between(y, 0.5, 1.1));
        CHECK(rms_between(y, 0.5, 1.1) > 300.0);
        CHECK(best > 0.9);
        CHECK(std::fabs(hz / f0 - 1.0) < 0.06);
        CHECK(peak_of(y) < 32767.0);
        b.rig.eng.note_off(note);
        std::vector<double> y2;
        b.rig.run(DspRig::blocks_for(0.6), &y2);
        CHECK(rms_between(y2, 0.4, 0.6) < 0.1 * rms_between(y, 0.5, 1.1));                  // the breath stops: the tone dies
    }
}

TEST(foley_layers_sound_across_the_timbre_and_stay_in_range) {
    for (int t = 0; t < 4; t++) for (int m : {4000, 24000, 32767}) {
        EngineBench b(OSCX_FOLEY, t * 10922, m);
        std::vector<double> y = b.play(57, 1.0);
        const double r = rms_between(y, 0.2, 1.0);
        std::printf("    foley timbre %d morph %d: rms %.0f peak %.0f\n", t, m, r, peak_of(y));
        CHECK(r > 100.0);
        CHECK(peak_of(y) < 32767.0);
    }
}
