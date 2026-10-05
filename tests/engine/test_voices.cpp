#include "rig.h"

using namespace sc;
using namespace tst;

// ADR-036 stage 1: the voice count is chosen per graph load, and the Mono mode of the voice manager.

namespace {

// NoteIn -> sine Osc -> VoiceOut -> bus -> MasterOut: the smallest patch that makes a pitch you can count.
void sine_patch(DspRig &rig, GraphDesc &g, int tail_ms = 60) {
    rig.add(g, 1, T_NOTE_IN);
    rig.add(g, 2, T_OSC)->param[OSC_WAVE] = WAVE_SINE_;
    rig.add(g, 3, T_VOICE_OUT)->param[VO_TAIL_MS] = tail_ms;
    rig.add(g, 4, T_BUS_IN);
    rig.add(g, 5, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
    g.connect(4, 0, 5, Dst::In, 0);
    g.connect(4, 1, 5, Dst::In, 1);
}

// A heavier voice (filter + two more modules) so the per-voice memory is the bulk of the graph.
void voice_heavy_patch(DspRig &rig, GraphDesc &g) {
    rig.add(g, 1, T_NOTE_IN);
    rig.add(g, 2, T_OSC)->param[OSC_WAVE] = WAVE_SAW_;
    rig.add(g, 6, T_FILTER_V);
    rig.add(g, 7, T_ENV);
    rig.add(g, 8, T_VCA_V);
    rig.add(g, 3, T_VOICE_OUT)->param[VO_TAIL_MS] = 60;
    rig.add(g, 4, T_BUS_IN);
    rig.add(g, 5, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(2, 0, 6, Dst::In, 0);
    g.connect(6, 0, 8, Dst::In, 0);
    g.connect(1, 1, 7, Dst::In, 0);
    g.connect(7, 0, 8, Dst::Param, 0);
    g.connect(8, 0, 3, Dst::In, 0);
    g.connect(4, 0, 5, Dst::In, 0);
    g.connect(4, 1, 5, Dst::In, 1);
}

// Counts the rising zero crossings of a block-by-block render: pitch in Hz over `seconds`.
double hz_over(DspRig &rig, double seconds) {
    std::vector<double> y;
    rig.run(DspRig::blocks_for(seconds), &y);
    int c = 0;
    for (size_t i = 1; i < y.size(); i++) if (y[i - 1] < 0 && y[i] >= 0) c++;
    return c / seconds;
}

double note_hz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

void mono_rig(DspRig &rig, bool legato, int glide_ms) {
    GraphDesc g;
    sine_patch(rig, g);
    CHECK(rig.eng.load(g, 1) == Err::Ok);
    CHECK(rig.eng.set_voice_mode(VoiceMode::Mono, legato, glide_ms));
    rig.run(2);
}

}  // namespace

TEST(the_voice_count_is_chosen_per_load_and_the_unused_voices_cost_nothing) {
    DspRig rig(8);
    GraphDesc g;
    voice_heavy_patch(rig, g);

    CHECK(rig.eng.load(g, 8) == Err::Ok);
    rig.run(2);
    rig.eng.gc();
    const size_t used8 = rig.heap.used();
    for (int n : {60, 62, 64, 65, 67}) rig.eng.note_on(n);
    rig.run(4);
    CHECK_EQ(rig.eng.active_voices(), 5);
    rig.eng.all_notes_off();
    rig.run(2);

    CHECK(rig.eng.load(g, 1) == Err::Ok);                                   // one voice: the 8 copies are replaced by 1, and freed by gc()
    rig.run(2);
    rig.eng.gc();
    const size_t used1 = rig.heap.used();
    for (int n : {60, 62, 64, 65, 67}) rig.eng.note_on(n);
    rig.run(4);
    CHECK_EQ(rig.eng.active_voices(), 1);                                   // the poly allocator steals within the one voice
    std::printf("    module memory: 8 voices %zu bytes, 1 voice %zu bytes\n", used8, used1);
    CHECK(used1 < used8 * 7 / 10);                                          // these voice modules are small: the saving is larger with real patches (measured on the board)

    size_t back = 0;
    for (int round = 0; round < 3; round++) {                               // 1 -> 8 -> 1 -> 8 ...: the heap must not grow from one round trip to the next
        rig.eng.all_notes_off();
        rig.run(2);
        CHECK(rig.eng.load(g, 8) == Err::Ok);
        rig.run(2);
        rig.eng.gc();
        const size_t u8 = rig.heap.used();
        if (round > 0) CHECK_EQ(u8, back);
        back = u8;
        CHECK(rig.eng.load(g, 1) == Err::Ok);
        rig.run(2);
        rig.eng.gc();
    }
    CHECK(back <= used8 + 256);
    rig.eng.shutdown();
    CHECK_EQ(rig.heap.used(), 0);
}

TEST(mono_plays_the_last_key_and_returns_to_the_held_one) {
    DspRig rig(4);
    mono_rig(rig, false, 0);
    rig.eng.note_on(60);
    CHECK_NEAR(hz_over(rig, 0.5), note_hz(60), 6);
    rig.eng.note_on(67);                                                    // a second key: the voice moves to it, no second voice
    CHECK_EQ(rig.eng.active_voices(), 1);
    rig.run(2);
    CHECK_NEAR(hz_over(rig, 0.5), note_hz(67), 6);
    rig.eng.note_off(67);                                                   // the top key up: back to the one still held
    rig.run(2);
    CHECK_NEAR(hz_over(rig, 0.5), note_hz(60), 6);
    CHECK_EQ(rig.eng.active_voices(), 1);
    rig.eng.note_off(60);                                                   // the last key up: release, then the voice frees itself
    rig.run(DspRig::blocks_for(0.3));
    CHECK_EQ(rig.eng.active_voices(), 0);
}

TEST(mono_releasing_a_lower_key_keeps_the_sounding_one) {
    DspRig rig(4);
    mono_rig(rig, false, 0);
    rig.eng.note_on(60);
    rig.eng.note_on(64);
    rig.eng.note_on(67);
    rig.eng.note_off(60);                                                   // not the sounding key: nothing changes
    rig.run(2);
    CHECK_NEAR(hz_over(rig, 0.5), note_hz(67), 6);
    rig.eng.note_off(67);                                                   // back to 64 (60 is gone)
    rig.run(2);
    CHECK_NEAR(hz_over(rig, 0.5), note_hz(64), 6);
    rig.eng.note_on(64);                                                    // the same key again moves to the top, it is not stacked twice
    rig.eng.note_off(64);
    rig.run(DspRig::blocks_for(0.3));
    CHECK_EQ(rig.eng.active_voices(), 0);
}

TEST(mono_legato_keeps_the_envelopes_running_and_retrigger_restarts_them) {
    for (bool legato : {true, false}) {
        DspRig rig(4);
        mono_rig(rig, legato, 0);
        rig.eng.note_on(60);
        rig.run(200);
        const uint32_t age_before = rig.eng.voice(0).age;
        CHECK(age_before > 150);
        rig.eng.note_on(64);                                                // a key is held: legato only changes the pitch
        rig.run(1);
        const uint32_t age_after = rig.eng.voice(0).age;
        if (legato) CHECK(age_after > age_before);                          // the voice was not restarted
        else CHECK(age_after < 5);                                          // restarted: its envelopes begin again
        CHECK_EQ(rig.eng.voice(0).note, 64);
        rig.eng.note_off(64);                                               // back to 60: the same rule
        rig.run(1);
        CHECK_EQ(rig.eng.voice(0).note, 60);
        if (legato) CHECK(rig.eng.voice(0).age > 150); else CHECK(rig.eng.voice(0).age < 5);
    }
}

TEST(mono_glide_slides_the_pitch_with_the_set_time_constant) {
    DspRig rig(4);
    mono_rig(rig, true, 100);
    rig.eng.note_on(60);
    rig.run(4);
    CHECK_EQ(rig.eng.voice(0).pitch, 60 * 256);                             // the first note does not glide from anywhere
    rig.eng.note_on(72);                                                    // an octave up while 60 is held
    const double from = 60 * 256, to = 72 * 256;
    int reached = -1;
    int32_t prev = rig.eng.voice(0).pitch;
    for (int b = 0; b < 2000; b++) {
        rig.run(1);
        const int32_t p = rig.eng.voice(0).pitch;
        CHECK(p >= prev);                                                   // monotone, never overshoots
        CHECK(p <= 72 * 256);
        prev = p;
        if (reached < 0 && p >= from + 0.632 * (to - from)) reached = b + 1;
    }
    const double ms = reached * 1000.0 * kBlock / kSampleRate;
    std::printf("    glide 100 ms: 63 %% of the way after %.0f ms\n", ms);
    CHECK_NEAR(ms, 100.0, 15.0);
    CHECK_EQ(rig.eng.voice(0).pitch, 72 * 256);                             // and it arrives
}

TEST(mono_retrigger_glides_from_the_pitch_it_has_even_in_the_release_tail) {
    DspRig rig(4);
    GraphDesc g;
    sine_patch(rig, g, 2000);                                               // a long tail: the voice is still sounding after the key went up
    CHECK(rig.eng.load(g, 1) == Err::Ok);
    CHECK(rig.eng.set_voice_mode(VoiceMode::Mono, false, 80));
    rig.run(2);
    rig.eng.note_on(60);
    rig.run(4);
    rig.eng.note_off(60);
    rig.run(10);
    CHECK_EQ(rig.eng.active_voices(), 1);
    rig.eng.note_on(72);
    rig.run(1);
    const int32_t p = rig.eng.voice(0).pitch;
    CHECK(p >= 60 * 256 && p < 72 * 256);                                   // it starts at 60 and slides, it does not jump to 72
    CHECK(p < 62 * 256);
}

TEST(mono_stack_survives_more_keys_than_it_holds_and_all_notes_off_clears_it) {
    DspRig rig(4);
    mono_rig(rig, true, 0);
    for (int n = 40; n < 60; n++) rig.eng.note_on(n);                       // 20 keys: the stack holds 16, the oldest are dropped
    rig.run(2);
    CHECK_EQ(rig.eng.voice(0).note, 59);
    for (int n = 59; n >= 44; n--) rig.eng.note_off(n);                     // releasing from the top walks down the kept keys
    rig.run(2);
    CHECK(rig.eng.voice(0).gate == false || rig.eng.voice(0).note < 44);
    rig.eng.all_notes_off();
    rig.run(2);
    CHECK_EQ(rig.eng.active_voices(), 0);
    rig.eng.note_on(65);                                                    // a clean start after all-notes-off
    rig.run(2);
    CHECK_EQ(rig.eng.voice(0).note, 65);
    CHECK(rig.eng.voice(0).gate);
}

TEST(back_to_poly_after_mono_gives_every_key_its_own_voice) {
    DspRig rig(4);
    mono_rig(rig, false, 0);
    GraphDesc g;
    sine_patch(rig, g);
    CHECK(rig.eng.load(g, 4) == Err::Ok);
    CHECK(rig.eng.set_voice_mode(VoiceMode::Poly, false, 0));
    rig.run(3);
    for (int n : {60, 64, 67}) rig.eng.note_on(n);
    rig.run(2);
    CHECK_EQ(rig.eng.active_voices(), 3);
}
