// The production path end to end: rack_t / synth_params_t -> mapper -> engine -> stereo samples, through the same C
// API the simulator and the firmware use. Needs -DPLATFORM_SIM and the C core files (rack, synth_config, ...).
#include <algorithm>
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

struct App {
    std::vector<uint8_t> fast, bulk;
    rack_t rack;
    synth_params_t params;
    int blocks_done = 0;
    App() : fast(6u << 20), bulk(6u << 20) {
        CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
        synth_params_default(&params);
        rack_init(&rack);
        rack.cfg.fxr.slot[1].v[2] = 0;          // the startup patch has delay and reverb on; these tests start dry
        rack.cfg.fxr.slot[2].v[0] = 0;
    }
    ~App() { engine_synth_shutdown(); }
    void build() { engine_synth_build(&rack, &params); }
    // renders `seconds` of audio; returns the left channel and optionally the right
    std::vector<double> run(double seconds, std::vector<double> *right = nullptr) {
        const int frames = static_cast<int>(seconds * engine_synth_sample_rate());
        std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
        // several odd-sized pulls, like an audio driver
        int done = 0;
        while (done < frames) {
            int n = std::min(frames - done, 173);
            engine_synth_render(&buf[static_cast<size_t>(done) * 2], n);
            done += n;
        }
        std::vector<double> l(static_cast<size_t>(frames));
        if (right) right->resize(static_cast<size_t>(frames));
        for (int i = 0; i < frames; i++) { l[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2]; if (right) (*right)[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2 + 1]; }
        return l;
    }
};

double peak_of(const std::vector<double> &x) { double p = 0; for (double v : x) p = std::fmax(p, std::fabs(v)); return p; }

// mean |x[n+1] - 2 x[n] + x[n-1]|: a brightness measure (the total variation of a ramp does not see a filter)
double roughness(const std::vector<double> &x, size_t from, size_t to) {
    double s = 0;
    for (size_t i = from + 1; i + 1 < to; i++) s += std::fabs(x[i + 1] - 2 * x[i] + x[i - 1]);
    return s / static_cast<double>(to - from);
}
double autocorr(const std::vector<double> &x, size_t lag, size_t from, size_t to) {
    double a = 0, e0 = 0, e1 = 0;
    for (size_t i = from; i + lag < to; i++) { a += x[i] * x[i + lag]; e0 += x[i] * x[i]; e1 += x[i + lag] * x[i + lag]; }
    return a / std::sqrt(e0 * e1 + 1e-9);
}
}  // namespace

TEST(demo_rack_plays_a_note_at_the_right_pitch_and_releases) {
    App a;
    a.build();
    engine_synth_note_on(48);                                             // C3, 130.81 Hz
    std::vector<double> y = a.run(0.6);
    const double r = rms(std::vector<double>(y.begin() + 6000, y.end()));
    std::printf("    demo rack C3: rms %.0f, peak %.0f\n", r, peak_of(y));
    CHECK(r > 600.0);
    CHECK(peak_of(y) < 32767.0);
    const size_t period = static_cast<size_t>(std::lround(kSampleRate / 130.8128));
    const size_t win_end = std::min<size_t>(26000, y.size());             // (the render is only 19200 samples long at 32 kHz)
    CHECK(autocorr(y, period, 8000, win_end) > 0.6);                      // periodic at the note's frequency (the LFO sweeps the filter slowly)
    CHECK(autocorr(y, period * 3 / 2, 8000, win_end) < autocorr(y, period, 8000, win_end));

    engine_synth_note_off(48);
    a.run(0.6);                                                           // release 0.2 s + tail
    std::vector<double> after = a.run(0.3);
    CHECK(rms(after) < 30.0);
}

TEST(live_edits_change_the_sound_without_a_rebuild_and_without_a_click) {
    App a;
    rack_clear(&a.rack);
    rack_insert(&a.rack, 0, MOD_OSC);
    a.rack.slot[0].v[MP_OC_WAVE] = 3;                                     // SawUp
    rack_insert(&a.rack, 1, MOD_FILTER);
    a.rack.slot[1].v[MP_FL_CUT] = 700.0f;
    a.build();
    engine_synth_note_on(45);
    a.run(0.4);
    std::vector<double> before = a.run(0.2);
    // open the filter: only a parameter message, the same graph
    a.rack.slot[1].v[MP_FL_CUT] = 9000.0f;
    engine_synth_set_params(&a.rack, &a.params);
    std::vector<double> after = a.run(0.5);
    std::vector<double> settled(after.begin() + 12000, after.end());
    const double rb = roughness(before, 0, before.size()), ra = roughness(settled, 0, settled.size());
    std::printf("    cutoff 700 Hz -> 9 kHz: roughness %.0f -> %.0f\n", rb, ra);
    CHECK(ra > rb * 1.5);
    double step_before = 0, step_after = 0;
    for (size_t i = 1; i < before.size(); i++) step_before = std::fmax(step_before, std::fabs(before[i] - before[i - 1]));
    for (size_t i = 1; i < 6000; i++) step_after = std::fmax(step_after, std::fabs(after[i] - after[i - 1]));   // the glide right after the edit
    std::printf("    largest sample step before the edit %.0f, during the glide %.0f\n", step_before, step_after);
    CHECK(step_after < 2.0 * peak_of(before));                            // the brighter sound has steeper edges (a saw's wrap), but the edit adds no jump of its own
}

TEST(rebuilding_the_rack_keeps_a_sounding_note_alive) {
    App a;
    a.build();
    engine_synth_note_on(52);
    a.run(0.3);
    std::vector<double> before = a.run(0.1);
    rack_insert(&a.rack, 1, MOD_OSC);                                     // a second oscillator in front of the filter (structural edit)
    a.rack.slot[1].v[MP_OC_COARSE] = 7.0f;
    a.build();
    std::vector<double> after = a.run(0.2);
    const double rb = rms(before), ra = rms(after);
    std::printf("    rms before %.0f, after adding an oscillator %.0f\n", rb, ra);
    CHECK(ra > rb * 0.5);                                                 // still sounding (a rebuild that cut the voice would be silent)
    // the first block after the rebuild is not a hole
    double first = 0;
    for (size_t i = 0; i < 400; i++) first = std::fmax(first, std::fabs(after[i]));
    CHECK(first > rb * 0.3);
}

TEST(fm_mode_plays_factory_patches_and_edits_live) {
    App a;
    a.rack.cfg.type = SYNTH_FM;
    a.build();
    engine_synth_note_on(60);
    std::vector<double> y = a.run(0.5);
    std::printf("    DX7 patch 1: rms %.0f, peak %.0f\n", rms(y), peak_of(y));
    CHECK(rms(y) > 300.0);
    CHECK(peak_of(y) < 32767.0);
    for (int p : {0, 7, 31, 63, 100, 127}) {
        a.rack.cfg.fm_patch = static_cast<uint8_t>(p);
        dx7_load_factory(&a.rack.cfg.fm, p);
        engine_synth_set_params(&a.rack, &a.params);
        engine_synth_note_off(60);
        a.run(0.4);
        engine_synth_note_on(60);
        std::vector<double> z = a.run(1.5);                                // slow-attack patches swell late
        std::printf("    patch %3d: peak %.0f\n", p + 1, peak_of(z));
        CHECK(peak_of(z) > 100.0);                                         // patch 32 is a very quiet patch (as it was under AMY)
        CHECK(peak_of(z) < 32767.0);
    }
    // an operator edit is a live message
    const double base = rms(a.run(0.1));
    a.rack.cfg.fm.op[0].level = 20;
    engine_synth_set_params(&a.rack, &a.params);
    const double edited = rms(a.run(0.3));
    CHECK(edited != base);
}

TEST(master_effects_add_a_tail_an_echo_and_stereo_width) {
    // short note on the modular synth with a fast envelope, so the tail is the effect
    auto render = [](int rvb, int dly, int chorus, std::vector<double> *l, std::vector<double> *r) {
        App a;
        a.params.amp_env.attack_ms = 2; a.params.amp_env.decay_ms = 60; a.params.amp_env.sustain = 0; a.params.amp_env.release_ms = 30;
        a.rack.cfg.fxr.slot[2].v[0] = static_cast<int16_t>(rvb);                 // slot 3 = reverb: Mix
        a.rack.cfg.fxr.slot[1].v[2] = static_cast<int16_t>(dly);                 // slot 2 = delay: Mix, Time
        a.rack.cfg.fxr.slot[1].v[0] = 250;
        a.rack.cfg.fxr.slot[0].v[0] = static_cast<int16_t>(chorus);              // slot 1 = chorus: Mode
        a.build();
        engine_synth_note_on(55);
        std::vector<double> held = a.run(0.05);                           // the note has to sound for a moment: on + off in one block never opens the gate
        engine_synth_note_off(55);
        *l = a.run(1.15, r);
        (void)held;
    };
    std::vector<double> dry_l, dry_r, rv_l, rv_r, dl_l, dl_r, ch_l, ch_r;
    render(0, 0, 0, &dry_l, &dry_r);
    render(60, 0, 0, &rv_l, &rv_r);
    render(0, 60, 0, &dl_l, &dl_r);
    render(0, 0, 1, &ch_l, &ch_r);
    auto energy = [](const std::vector<double> &x, double t0, double t1) {
        double s = 0;
        for (size_t i = static_cast<size_t>(t0 * kSampleRate); i < static_cast<size_t>(t1 * kSampleRate) && i < x.size(); i++) s += x[i] * x[i];
        return s;
    };
    const double tail_dry = energy(dry_l, 0.5, 1.2), tail_rv = energy(rv_l, 0.5, 1.2);
    std::printf("    energy 0.5..1.2 s: dry %.2e, reverb %.2e; echo window 0.25..0.4 s: dry %.2e, delay %.2e\n", tail_dry, tail_rv,
                energy(dry_l, 0.25, 0.4), energy(dl_l, 0.25, 0.4));
    CHECK(tail_rv > 50 * (tail_dry + 1.0));                               // the reverb keeps ringing after the note is gone
    CHECK(energy(dl_l, 0.26, 0.45) > 20 * (energy(dry_l, 0.26, 0.45) + 1.0));   // the echo returns after the 250 ms
    double diff = 0, tot = 0;
    for (size_t i = 0; i < 6000; i++) { diff += (ch_l[i] - ch_r[i]) * (ch_l[i] - ch_r[i]); tot += ch_l[i] * ch_l[i]; }   // while the note sounds
    double dd = 0;
    for (size_t i = 0; i < dry_l.size(); i++) dd += (dry_l[i] - dry_r[i]) * (dry_l[i] - dry_r[i]);
    CHECK(dd < 1.0);                                                      // without effects the output is mono
    CHECK(diff > 1e-4 * tot);                                             // the chorus decorrelates the channels
}

TEST(modulators_move_the_targets) {
    App a;
    rack_clear(&a.rack);
    rack_insert(&a.rack, 0, MOD_OSC);
    a.rack.slot[0].v[MP_OC_WAVE] = 0;                                     // sine
    rack_insert(&a.rack, 1, MOD_LFO);
    a.rack.slot[1].v[MP_LF_RATE] = 5.0f;
    a.rack.slot[1].v[MP_LF_DEPTH] = 2.0f;                                 // +-2 semitones
    a.rack.slot[1].tgt_id = a.rack.slot[0].id;
    a.rack.slot[1].tgt_param = 0;                                         // pitch
    a.build();
    engine_synth_note_on(69);
    a.run(0.2);
    std::vector<double> y = a.run(1.0);
    // frequency in 25 ms windows
    double lo = 1e9, hi = 0;
    const size_t w = static_cast<size_t>(0.04 * kSampleRate);
    for (size_t at = 0; at + w < y.size(); at += w / 2) {
        int cross = 0;
        double first = 0, last = 0;
        for (size_t i = at + 1; i < at + w; i++)
            if (y[i - 1] < 0 && y[i] >= 0) { double t = static_cast<double>(i) - y[i] / (y[i] - y[i - 1]); if (!cross) first = t; last = t; cross++; }
        if (cross < 4) continue;
        double f = (cross - 1) * kSampleRate / (last - first);            // from the first to the last crossing: far finer than counting
        lo = std::fmin(lo, f); hi = std::fmax(hi, f);
    }
    std::printf("    vibrato: frequency swings between %.0f and %.0f Hz around 440\n", lo, hi);
    CHECK(hi - lo > 50.0);                                                // +-2 semitones = about +-52 Hz
    CHECK(lo > 380.0 && hi < 500.0);
}

namespace {
double tone_power(const std::vector<double> &x, double hz) {          // Goertzel-style single-bin DFT power
    double re = 0, im = 0;
    for (size_t i = 0; i < x.size(); i++) { const double ph = 2 * kPi * hz * static_cast<double>(i) / kSampleRate; re += x[i] * std::cos(ph); im += x[i] * std::sin(ph); }
    return (re * re + im * im) / static_cast<double>(x.size() * x.size());
}
}  // namespace

TEST(an_oscillator_with_a_target_modulates_at_its_own_pitch_and_can_be_muted) {
    // carrier OC1 (sine) on A3 = 220 Hz, modulator OC2 one octave up (440 Hz) aimed at the carrier's pitch: odd sidebands at
    // 220 * (1, 3, 5, ...) appear, and 440 Hz itself is only heard while OC2 is not muted
    auto render = [](bool with_mod, bool mute, double *p440, double *p880, double *p1320) {
        App a;
        rack_clear(&a.rack);
        rack_insert(&a.rack, 0, MOD_OSC);
        a.rack.slot[0].v[MP_OC_WAVE] = 0;
        if (with_mod) {
            rack_insert(&a.rack, 1, MOD_OSC);
            rack_slot_t &m = a.rack.slot[1];
            m.v[MP_OC_WAVE] = 0; m.v[MP_OC_COARSE] = 12.0f; m.v[MP_OC_DEPTH] = 6.0f; m.v[MP_OC_MUTE] = mute ? 1.0f : 0.0f;
            m.tgt_id = a.rack.slot[0].id; m.tgt_param = 0;
        }
        a.params.amp_env.attack_ms = 2; a.params.amp_env.sustain = 1.0f;
        a.build();
        engine_synth_note_on(57);
        a.run(0.3);
        std::vector<double> y = a.run(0.5);
        *p440 = tone_power(y, 220.0); *p880 = tone_power(y, 440.0); *p1320 = tone_power(y, 660.0);
        return rms(y);
    };
    double a440, a880, a1320, b440, b880, b1320, c440, c880, c1320;
    const double ra = render(false, false, &a440, &a880, &a1320);
    const double rb = render(true, true, &b440, &b880, &b1320);
    const double rc = render(true, false, &c440, &c880, &c1320);
    std::printf("    rms alone %.0f, modulated+muted %.0f, modulated+audible %.0f\n", ra, rb, rc);
    std::printf("    f / 2f / 3f power (220 Hz): alone %.2e %.2e %.2e   muted %.2e %.2e %.2e   audible %.2e %.2e %.2e\n", a440, a880, a1320, b440, b880, b1320, c440, c880, c1320);
    CHECK(rb > 200.0);
    CHECK(b1320 > 100 * (a1320 + 1.0));                                   // the modulation is there while muted (FM sidebands)
    CHECK(c880 > 100 * (b880 + 1.0));                                     // the modulator's own tone only when audible
    const double carrier_c = rc * rc - 2.0 * c880;                         // what is left of the audible mix once the modulator's own tone is taken out
    CHECK(carrier_c > 0.5 * rb * rb && carrier_c < 2.0 * rb * rb);        // the modulated carrier is the same either way
}

namespace {
// frequency from zero crossings in [t0, t1) seconds
double freq_between(const std::vector<double> &y, double t0, double t1) {
    const size_t a = static_cast<size_t>(t0 * kSampleRate), b = static_cast<size_t>(t1 * kSampleRate);
    int cross = 0;
    double first = 0, last = 0;
    for (size_t i = a + 1; i < b && i < y.size(); i++)
        if (y[i - 1] < 0 && y[i] >= 0) { double t = static_cast<double>(i) - y[i] / (y[i] - y[i - 1]); if (!cross) first = t; last = t; cross++; }
    return cross < 2 ? 0.0 : (cross - 1) * kSampleRate / (last - first);
}
}  // namespace

TEST(changing_a_modulation_depth_does_not_rebuild_the_graph) {
    App a;
    rack_init(&a.rack);                                                     // demo rack: osc, filter, saturator, LFO -> cutoff
    a.rack.cfg.fxr.slot[1].v[2] = 0; a.rack.cfg.fxr.slot[2].v[0] = 0;
    rack_insert(&a.rack, 4, MOD_ENV);
    rack_slot_t &env = a.rack.slot[4];
    env.tgt_id = a.rack.slot[1].id; env.tgt_param = 0;                      // envelope -> filter cutoff
    env.v[MP_EN_DEPTH] = 0.0f;
    a.build();
    a.run(0.01);                                                            // the plan becomes active at the next block
    const float steps[] ={0.25f, 0.5f, 3.0f, 7.75f, 8.0f, 7.75f, 0.0f, 0.25f, -0.25f, 0.0f, 0.5f, -8.0f, -7.75f};
    for (float d : steps) {
        const unsigned before = engine_synth_build_count();
        env.v[MP_EN_DEPTH] = d;
        engine_synth_set_params(&a.rack, &a.params);
        std::printf("    depth %.2f oct: %s\n", d, engine_synth_build_count() == before ? "live" : "REBUILD");
        CHECK_EQ(engine_synth_build_count(), before);
    }
}

TEST(the_filters_own_envelope_amount_never_rebuilds_the_graph) {
    App a;
    rack_init(&a.rack);                                                     // demo rack: slot 1 is the filter
    a.rack.cfg.fxr.slot[1].v[2] = 0; a.rack.cfg.fxr.slot[2].v[0] = 0;
    a.rack.slot[1].v[MP_FL_ENVAMT] = 0.0f;
    a.build();
    a.run(0.01);
    const float steps[] = {0.25f, 3.0f, 7.75f, 8.0f, 7.75f, 0.25f, 0.0f, 0.25f};
    for (float d : steps) {
        const unsigned before = engine_synth_build_count();
        a.rack.slot[1].v[MP_FL_ENVAMT] = d;
        engine_synth_set_params(&a.rack, &a.params);
        a.run(0.01);
        std::printf("    filter envelope amount %.2f: %s\n", d, engine_synth_build_count() == before ? "live" : "REBUILD");
        CHECK_EQ(engine_synth_build_count(), before);
    }
}

TEST(an_envelope_in_real_units_sweeps_the_pitch_like_a_kick) {
    App a;
    rack_clear(&a.rack);
    rack_insert(&a.rack, 0, MOD_OSC);
    a.rack.slot[0].v[MP_OC_WAVE] = 0;                                      // sine
    rack_insert(&a.rack, 1, MOD_ENV);
    rack_slot_t &e = a.rack.slot[1];
    e.tgt_id = a.rack.slot[0].id; e.tgt_param = 0;                          // pitch
    e.v[MP_EN_A] = 1; e.v[MP_EN_D] = 120; e.v[MP_EN_S] = 0; e.v[MP_EN_DCV] = 60;
    e.v[MP_EN_DEPTH] = 48.0f;                                               // +48 semitones at the top of the envelope (units: st)
    a.params.amp_env.attack_ms = 1; a.params.amp_env.sustain = 1.0f;
    a.build();
    engine_synth_note_on(36);                                              // C2 = 65.4 Hz
    std::vector<double> y = a.run(0.6);
    const double f_early = freq_between(y, 0.004, 0.016), f_late = freq_between(y, 0.40, 0.55);
    std::printf("    pitch envelope +48 st: %.0f Hz at 4..16 ms, %.1f Hz after the decay (note 65.4 Hz)\n", f_early, f_late);
    CHECK(f_late > 60.0 && f_late < 70.0);                                  // back at the note
    CHECK(f_early > 4.0 * f_late);                                          // and well above it at the start (more than two octaves)
}

TEST(rack_oscillator_engines_play_and_follow_the_keyboard) {
    for (int wave = OC_FIRST_ENGINE; wave < OC_FIRST_ENGINE + 8; wave++) {
        App a;
        rack_clear(&a.rack);
        rack_insert(&a.rack, 0, MOD_OSC);
        a.rack.slot[0].v[MP_OC_WAVE] = static_cast<float>(wave);
        a.params.amp_env.attack_ms = 2; a.params.amp_env.sustain = 1.0f;
        a.build();
        engine_synth_note_on(57);
        std::vector<double> y = a.run(0.4);
        const double r = rms(std::vector<double>(y.begin() + 2000, y.begin() + 12000));
        std::printf("    engine %d through the rack: rms %.0f, peak %.0f\n", wave - OC_FIRST_ENGINE, r, peak_of(y));
        CHECK(r > 60.0);
        CHECK(peak_of(y) < 32767.0);
        if (wave == OC_FIRST_ENGINE) {                                      // the string keeps the played pitch: 220 Hz
            const size_t period = static_cast<size_t>(std::lround(kSampleRate / 220.0));
            CHECK(autocorr(y, period, 3000, 12000) > 0.8);
        }
    }
    // the morph control can be a modulation target (an engine has four targets: Pit Lvl PW Mrph)
    App a;
    rack_clear(&a.rack);
    rack_insert(&a.rack, 0, MOD_OSC);
    a.rack.slot[0].v[MP_OC_WAVE] = OC_FIRST_ENGINE + 6;                     // additive
    rack_insert(&a.rack, 1, MOD_LFO);
    a.rack.slot[1].tgt_id = a.rack.slot[0].id; a.rack.slot[1].tgt_param = 3;
    a.rack.slot[1].v[MP_LF_DEPTH] = 80.0f;
    a.build();
    engine_synth_note_on(57);
    std::vector<double> y = a.run(0.5);
    CHECK(rms(y) > 200.0);
}

TEST(fx_rack_slots_run_in_order_and_edit_live) {
    auto render = [](int t0, int t1, int t2, std::vector<double> *out) {
        App a;
        rack_clear(&a.rack);
        rack_insert(&a.rack, 0, MOD_OSC);
        a.rack.slot[0].v[MP_OC_WAVE] = 3;                                     // SawUp
        a.params.amp_env.attack_ms = 2; a.params.amp_env.sustain = 1.0f;
        fxr_set_type(&a.rack.cfg.fxr.slot[0], t0);
        fxr_set_type(&a.rack.cfg.fxr.slot[1], t1);
        fxr_set_type(&a.rack.cfg.fxr.slot[2], t2);
        fxr_set_type(&a.rack.cfg.fxr.slot[3], FX_NONE);
        a.build();
        engine_synth_note_on(48);
        *out = a.run(0.5);
        return rms(std::vector<double>(out->begin() + 8000, out->end()));
    };
    std::vector<double> clean, driven, cab, both;
    const double r0 = render(FX_NONE, FX_NONE, FX_NONE, &clean);
    const double r1 = render(FX_DRIVE, FX_NONE, FX_NONE, &driven);
    const double r2 = render(FX_CAB, FX_NONE, FX_NONE, &cab);
    const double r3 = render(FX_DRIVE, FX_CAB, FX_COMP, &both);
    std::printf("    rms: clean %.0f, drive %.0f, cab %.0f, drive + cab + comp %.0f\n", r0, r1, r2, r3);
    CHECK(r0 > 500.0 && r1 > 500.0 && r2 > 200.0 && r3 > 200.0);
    CHECK(roughness(driven, 8000, driven.size()) != roughness(clean, 8000, clean.size()));
    CHECK(roughness(cab, 8000, cab.size()) / r2 < 0.9 * roughness(clean, 8000, clean.size()) / r0);   // the cabinet rolls off the top of the saw (relative to the level)
    CHECK(peak_of(both) <= 32767.0);
    // an effect parameter is a live message (no rebuild): the delay mix opens an echo
    App a;
    rack_clear(&a.rack);
    rack_insert(&a.rack, 0, MOD_OSC);
    a.params.amp_env.attack_ms = 2; a.params.amp_env.decay_ms = 40; a.params.amp_env.sustain = 0; a.params.amp_env.release_ms = 20;
    fxr_set_type(&a.rack.cfg.fxr.slot[0], FX_DELAY);
    a.rack.cfg.fxr.slot[0].v[0] = 200; a.rack.cfg.fxr.slot[0].v[2] = 0;
    a.build();
    engine_synth_note_on(60);
    a.run(0.15);
    engine_synth_note_off(60);
    std::vector<double> before = a.run(0.5);
    a.rack.cfg.fxr.slot[0].v[2] = 80;
    engine_synth_set_params(&a.rack, &a.params);
    engine_synth_note_on(60);
    a.run(0.15);
    engine_synth_note_off(60);
    std::vector<double> after = a.run(0.5);
    // the note was played 150 ms before `after` starts, so its 200 ms echo lands 50 ms into it (the delay does not record while its mix is 0)
    CHECK(rms(std::vector<double>(after.begin() + 2400, after.begin() + 7200)) > 20.0 * (rms(std::vector<double>(before.begin() + 2400, before.begin() + 7200)) + 1.0));
}

TEST(resonator_module_rings_at_the_played_pitch) {
    App a;
    rack_clear(&a.rack);
    rack_insert(&a.rack, 0, MOD_OSC);
    a.rack.slot[0].v[MP_OC_WAVE] = 5;                                       // noise
    rack_insert(&a.rack, 1, MOD_COMB);
    a.rack.slot[1].v[MP_RS_FB] = 85.0f; a.rack.slot[1].v[MP_RS_MIX] = 1.0f; a.rack.slot[1].v[MP_RS_DAMP] = 18000.0f;
    a.params.amp_env.attack_ms = 2; a.params.amp_env.sustain = 1.0f;
    a.build();
    engine_synth_note_on(57);                                               // 220 Hz
    std::vector<double> y = a.run(0.5);
    const size_t period = static_cast<size_t>(std::lround(kSampleRate / 220.0));
    double best = -2;
    size_t best_lag = 0;
    for (size_t lag = period - 3; lag <= period + 3; lag++) { const double c = autocorr(y, lag, 6000, 14000); if (c > best) { best = c; best_lag = lag; } }
    std::printf("    resonator on noise at A3: autocorrelation %.2f at lag %zu (period %zu)\n", best, best_lag, period);
    CHECK(best > 0.3);
    CHECK(autocorr(y, period * 3 / 2, 6000, 14000) < best - 0.15);
}
