// TechnoGen's controls, screen and LEDs (src/app.cpp), with the real synth behind them. The main view and the engine view are written to
// build/main_view.png and build/engine_view.png (4x), to look at them against the design.
#include <cstdlib>
#include <cstring>
#include <vector>
#include "app.h"
#include "synth.h"
#include "tests/stubs.h"
#include "tests/test.h"
#include "tg_app.h"
#include "ui/canvas.h"

using namespace tg;

namespace {
constexpr size_t kFast = 1u << 20, kBulk = 1u << 20;
struct Rig {
    void *fast = std::malloc(kFast), *bulk = std::malloc(kBulk);
    bool ok = synth_init(fast, kFast, bulk, kBulk);
    Rig() { app_start(nullptr); app_draw(); }
    ~Rig() { synth_shutdown(); std::free(fast); std::free(bulk); }
};
void ev(int ctl, input_kind_t kind, int value = 0) { input_event_t e{}; e.ctl = ctl; e.kind = kind; e.value = value; app_event(e); app_draw(); }
void tap(int ctl) { ev(ctl, IN_PRESS); ev(ctl, IN_RELEASE); }
void turn(int ctl, int d) { ev(ctl, IN_DELTA, d); }
void knob(int k, int pct) {
    static const int ids[kKnobs] = {HWV2_COL_KNOB_0, HWV2_COL_KNOB_1, HWV2_COL_KNOB_2, HWV2_COL_KNOB_3, HWV2_KNOB_R1, HWV2_KNOB_R2, HWV2_KNOB_R3};
    ev(ids[k], IN_VALUE, pct * INPUT_VALUE_MAX / 100);
}
int key(int i) { return HWV2_KEY(1 + i / 4, i % 4); }
uint16_t px(int x, int y) { return stub_frame[y * DISPLAY_WIDTH + x]; }
uint16_t col(int t) { return CANVAS_HEX(track_def(t).color); }
void render(int frames) {
    std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
    for (int at = 0; at < frames; at += 256) { synth_render(buf.data(), frames - at < 256 ? frames - at : 256); app_draw(); }
}
}  // namespace

TEST(app_the_main_view_shows_the_seven_tracks_in_their_colours) {
    Rig r;
    CHECK(r.ok);
    AppState &s = app_state();
    CHECK_EQ(s.selected, 0);
    CHECK_EQ(s.view, V_MAIN);
    CHECK(px(127, 12 + 8) == col(T_KICK));                         // the selected row: its colour as the paper
    for (int t = 1; t < kTracks; t++) {
        CHECK(px(127, 12 + t * 16 + 8) == 0);                       // the others: black paper ...
        int ink = 0;
        for (int x = 0; x < 22; x++) for (int y = 0; y < 16; y++) ink += px(x, 12 + t * 16 + y) == col(t);
        CHECK(ink > 10);                                            // ... and their name in their colour
    }
    // KICK's steps 1, 5, 9, 13 are accents: full-height marks, in black on the selected row
    for (int p = 0; p < kSteps; p += 4) CHECK(px(23 + (p / 4) * 21, 12 + 2) == 0);
    CHECK(px(23 + 5, 12 + 2) == col(T_KICK));                      // step 2: off, only the 1-pixel floor at the bottom
    CHECK(px(23 + 5, 12 + 13) == 0);
    CHECK(stub_write_png("build/main_view.png", 4));
}

TEST(app_encoder_a_selects_and_its_push_opens_the_engine) {
    Rig r;
    AppState &s = app_state();
    turn(HWV2_ENC_A, 1);
    CHECK_EQ(s.selected, 1);
    turn(HWV2_ENC_A, -1);
    turn(HWV2_ENC_A, -1);
    CHECK_EQ(s.selected, kTracks - 1);                              // round the list
    tap(HWV2_ENC_A_SW);
    CHECK_EQ(s.view, V_ENGINE);
    CHECK(stub_write_png("build/engine_view.png", 4));
    tap(HWV2_ENC_A_SW);
    CHECK_EQ(s.view, V_MAIN);
}

TEST(app_step_keys_cycle_off_hit_accent) {
    Rig r;
    AppState &s = app_state();
    const int before = synth_hits();
    CHECK_EQ(s.sel().step[0][1], S_OFF);
    tap(key(1));
    CHECK_EQ(s.sel().step[0][1], S_HIT);
    CHECK_EQ(synth_hits(), before + 1);
    tap(key(1));
    CHECK_EQ(s.sel().step[0][1], S_ACCENT);
    tap(key(1));
    CHECK_EQ(s.sel().step[0][1], S_OFF);
    CHECK_EQ(synth_hits(), before);
    CHECK(stub_leds[key(0)].r > 0);                                 // an accent's LED in the track's colour
}

TEST(app_shift_picks_the_lane_of_a_drum_track) {
    Rig r;
    AppState &s = app_state();
    turn(HWV2_ENC_A, 1);
    turn(HWV2_ENC_A, 1);
    CHECK_EQ(s.selected, T_DRUM);
    tap(HWV2_KEY(0, 3));                                            // SHFT tapped alone: latched
    CHECK(s.shift.on());
    tap(key(3));                                                    // SHFT + key 4: the fourth lane, HAT
    CHECK_EQ(s.lane[T_DRUM], 3);
    tap(HWV2_KEY(0, 3));                                            // tapped again: off
    CHECK(!s.shift.on());
    const uint8_t was = s.sel().step[3][5];
    tap(key(5));
    CHECK(s.sel().step[3][5] != was);                               // the key edits the HAT lane
    ev(HWV2_KEY(0, 3), IN_PRESS);                                   // held while encoder A turns: the lane moves, and SHFT does not latch
    turn(HWV2_ENC_A, 1);
    ev(HWV2_KEY(0, 3), IN_RELEASE);
    CHECK_EQ(s.lane[T_DRUM], 4);
    CHECK(!s.shift.on());
}

TEST(app_dice_rolls_the_track_and_back_undoes_it) {
    Rig r;
    AppState &s = app_state();
    const Track before = s.sel();
    tap(HWV2_BTN_2);
    CHECK(s.sel().seed != before.seed);
    CHECK_EQ(s.undo.n, 1);
    const Track bass = s.track(T_BASS);
    tap(HWV2_BTN_3);
    CHECK_EQ(s.sel().seed, before.seed);
    CHECK(std::memcmp(s.sel().value, before.value, sizeof before.value) == 0);
    CHECK_EQ(s.undo.n, 0);
    CHECK(s.track(T_BASS).seed == bass.seed);                      // only the selected track rolled
    tap(HWV2_BTN_3);
    CHECK(std::strcmp(s.message, "NO ROLL TO UNDO") == 0);
}

TEST(app_shift_dice_rolls_all_seven_and_keeps_the_locks) {
    Rig r;
    AppState &s = app_state();
    s.track(T_BASS).lock[0] = true;
    const uint8_t cutoff = s.track(T_BASS).value[0];
    uint16_t seeds[kTracks];
    for (int t = 0; t < kTracks; t++) seeds[t] = s.track(t).seed;
    ev(HWV2_KEY(0, 3), IN_PRESS);
    tap(HWV2_BTN_2);
    ev(HWV2_KEY(0, 3), IN_RELEASE);
    for (int t = 0; t < kTracks; t++) CHECK(s.track(t).seed != seeds[t]);
    CHECK_EQ(s.track(T_BASS).value[0], cutoff);
}

TEST(app_a_knob_takes_over_once_it_reaches_its_value) {
    Rig r;
    AppState &s = app_state();
    tap(HWV2_ENC_A_SW);                                             // the engine view: the knobs are KICK's values
    const uint8_t v = s.sel().value[0];
    const int far = v > 50 ? 5 : 95;
    knob(0, far);                                                   // somewhere else: nothing changes yet
    CHECK_EQ(s.sel().value[0], v);
    CHECK(!s.knob_caught[0]);
    knob(0, v > 50 ? 95 : 5);                                       // swept past the value: it is caught, and follows
    CHECK(s.knob_caught[0]);
    CHECK_EQ(s.sel().value[0], v > 50 ? 95 : 5);
    knob(0, 50);
    CHECK_EQ(s.sel().value[0], 50);
    turn(HWV2_ENC_A, 1);                                            // another track: the knob lets go
    CHECK(!s.knob_caught[0]);
}

TEST(app_mod_and_a_knob_lock_a_value) {
    Rig r;
    AppState &s = app_state();
    tap(HWV2_ENC_A_SW);
    tap(HWV2_BTN_1);                                                // MOD latched
    CHECK(s.mod.on());
    const uint8_t v = s.sel().value[2];
    knob(2, 10);
    knob(2, 90);                                                    // (one toggle a knob while MOD is on)
    CHECK(s.sel().lock[2]);
    CHECK_EQ(s.sel().value[2], v);                                  // the value stays
    tap(HWV2_BTN_1);
    CHECK(!s.mod.on());
    tap(HWV2_BTN_2);                                                // the dice leaves it
    CHECK_EQ(s.sel().value[2], v);
}

TEST(app_track_page_selects_and_mutes) {
    Rig r;
    AppState &s = app_state();
    tap(HWV2_KEY(0, 1));                                            // TRK
    CHECK_EQ(s.keys, K_TRACK);
    tap(key(4));
    CHECK_EQ(s.selected, T_PAD);
    const int before = synth_hits();
    tap(key(8 + T_KICK));                                           // mute the kick
    CHECK(s.track(T_KICK).muted);
    CHECK(synth_hits() < before);
    tap(key(15));                                                   // ALL
    CHECK(!s.track(T_KICK).muted);
    CHECK_EQ(synth_hits(), before);
}

TEST(app_patterns_switch_at_once_when_stopped_and_at_the_bar_when_playing) {
    Rig r;
    AppState &s = app_state();
    tap(HWV2_BTN_2);                                                // A1 gets a new kick
    const uint16_t a1 = s.track(T_KICK).seed;
    tap(HWV2_KEY(0, 2));                                            // PERF
    tap(key(8 + 1));                                                // A2, stopped: at once
    CHECK_EQ(s.cur, 1);
    CHECK(s.track(T_KICK).seed != a1);                              // A2 is still the first roll
    tap(key(8 + 0));
    CHECK_EQ(s.cur, 0);
    tap(HWV2_PLAY);
    render(4096);
    tap(key(8 + 1));                                                // playing: queued for the end of the bar
    CHECK_EQ(s.queued, 1);
    CHECK_EQ(s.cur, 0);
    render(synth_sample_rate() * 60 / s.bpm * 4 + 4096);            // a bar later
    CHECK_EQ(s.cur, 1);
    CHECK_EQ(s.queued, -1);
}

TEST(app_play_moves_the_playhead) {
    Rig r;
    AppState &s = app_state();
    tap(HWV2_PLAY);
    CHECK(s.playing);
    render(synth_sample_rate() * 60 / s.bpm / 4 * 3 + 100);         // into the fourth step
    CHECK_EQ(s.step, 3);
    tap(HWV2_PLAY);
    CHECK(!s.playing);
    CHECK_EQ(s.step, -1);
}
