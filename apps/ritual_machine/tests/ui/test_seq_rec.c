// Recording into the step sequencer from the keys (app/app_seq.c): Rec armed by Shift + Play or the key function Rec (a list: Notes);
// stopped = step entry (notes and chords at the cursor, which moves on), running = live (the nearest step, the held length, added to
// the chord). And the same list's other options: the pattern's sound recorded as a sample (app/app_capture.c).
#include "ui_test.h"

#define SHIFT CTL_BTN_3
#define KEY(n) ((control_id_t)CTL_KEY(4, (n)))   // the bottom row of "Keys 8x4": note 60 + n

extern int ui_stub_seq_pos, ui_stub_seq_frac;    // tools/ui_stubs.c: the step the "engine" plays, how far into it, the steps before it
extern uint32_t ui_stub_seq_count;

// Shift + Play: Rec off when it is armed; else the list opens on the option chosen last (Notes in a fresh app) and a push takes it.
static void rec(void) {
    const bool armed = ui_app.rec.armed;
    ev_press(SHIFT); ev_tap(CTL_PLAY); ev_release(SHIFT);
    if (!armed) ev_tap(CTL_JOY_SW);
}
static void engine_at(int step, int frac, uint32_t count) { ui_stub_seq_pos = step; ui_stub_seq_frac = frac; ui_stub_seq_count = count; }

TEST(seq_rec_step_entry_writes_notes_and_chords_at_the_cursor) {
    ui_fresh();
    seq_t *q = &ui_app.seq;
    ev_tap(KEY(0));                                                      // not armed: a key only plays
    CHECK_EQ(q->step[0].note[0], 48);
    rec();
    CHECK(ui_app.rec.armed && ui_app.ui.seq_rec && !q->running);         // Shift + Play arms, it does not start the sequencer
    q->step[0].len = 3; q->step[0].vel = 70;
    ev_tap(KEY(0));
    CHECK_EQ(q->step[0].note[0], 60);                                    // the step's note is replaced, its other values stay
    CHECK_EQ(q->step[0].len, 3); CHECK_EQ(q->step[0].vel, 70);
    CHECK_EQ(ui_app.ui.cursor, 1);                                       // the cursor moved on at the release
    ev_press(KEY(0)); ev_press(KEY(4)); ev_press(KEY(7));                // keys held together: one step, a chord
    CHECK_EQ(ui_app.ui.cursor, 1);
    ev_release(KEY(4)); ev_release(KEY(0));
    CHECK_EQ(ui_app.ui.cursor, 1);                                       // one is still down
    ev_release(KEY(7));
    CHECK_EQ(ui_app.ui.cursor, 2);
    CHECK_EQ(q->step[1].note[0], 60); CHECK_EQ(q->step[1].note[1], 64); CHECK_EQ(q->step[1].note[2], 67); CHECK_EQ(q->step[1].note[3], 0);
    for (int k = 0; k < 6; k++) ev_press(KEY(k));                        // six keys: the chord holds four
    for (int k = 0; k < 6; k++) ev_release(KEY(k));
    CHECK_EQ(q->step[2].note[3], 63);
    CHECK_EQ(ui_app.ui.cursor, 3);
    // the last step: the cursor goes around
    q->steps = 4;
    ev_tap(KEY(2));
    CHECK_EQ(q->step[3].note[0], 62);
    CHECK_EQ(ui_app.ui.cursor, 0);
    // a transposed pattern: the note is stored so that it plays back as it was played
    q->transpose = 5;
    ev_tap(KEY(7));
    CHECK_EQ(q->step[0].note[0], 62);
    q->transpose = 0;
    // Rec off: keys only play again
    rec();
    CHECK(!ui_app.rec.armed && !ui_app.ui.seq_rec);
    ev_tap(KEY(1));
    CHECK_EQ(q->step[1].note[0], 60);
    CHECK_EQ(ui_app.ui.cursor, 1);
}

TEST(seq_rec_live_writes_the_nearest_step_with_the_held_length) {
    ui_fresh();
    seq_t *q = &ui_app.seq;
    rec();
    ev_tap(CTL_PLAY);                                                    // Play without Shift: the sequencer runs
    CHECK(q->running && ui_app.rec.armed);
    const seq_t before = *q;
    // pressed early in step 4, held into step 6: it goes to step 4, two steps long, next to the note that was there
    engine_at(4, 40, 4);
    ev_press(KEY(2));
    CHECK(!memcmp(q->step, before.step, sizeof before.step));            // not while it is held
    CHECK_EQ(ui_app.ui.cursor, 0);
    engine_at(6, 100, 6);
    ev_release(KEY(2));
    ev_idle(1);
    CHECK_EQ(q->step[4].note[0], before.step[4].note[0]);
    CHECK_EQ(q->step[4].note[1], 62);
    CHECK_EQ(q->step[4].len, 2);
    // pressed late in step 8: it belongs to step 9, and is written only once the playhead is there (not played twice)
    engine_at(8, 200, 24);
    ev_tap(KEY(5));
    ev_idle(3);
    CHECK(seq_step_is_rest(&q->step[9]));
    engine_at(9, 10, 25);
    ev_idle(1);
    CHECK_EQ(q->step[9].note[0], 65);
    CHECK_EQ(q->step[9].len, 1);
    // the same note again on a step: it keeps its place, the length follows
    engine_at(9, 20, 41);
    ev_press(KEY(5));
    engine_at(12, 30, 44);
    ev_release(KEY(5));
    ev_idle(1);
    CHECK_EQ(q->step[9].note[0], 65); CHECK_EQ(q->step[9].note[1], 0);
    CHECK_EQ(q->step[9].len, 3);
    // late on the last step: the first one
    engine_at(15, 250, 47);
    ev_tap(KEY(1));
    engine_at(0, 5, 48);
    ev_idle(1);
    CHECK_EQ(q->step[0].note[1], 61);
    // a full chord: its first note gives way
    q->step[2] = (seq_step_t){{50, 51, 52, 53}, 1, 100, 100, 1};
    engine_at(2, 0, 50);
    ev_tap(KEY(6));
    ev_idle(1);
    CHECK_EQ(q->step[2].note[0], 66); CHECK_EQ(q->step[2].note[1], 51);
    // going backwards there is no "next" step to round to
    q->dir = SQD_REVERSE;
    engine_at(7, 250, 60);
    ev_tap(KEY(3));
    ev_idle(1);
    CHECK_EQ(q->step[7].note[1], 63);
    q->dir = SQD_FORWARD;
    // stopped under the finger: the note is written, one step long
    engine_at(11, 10, 70);
    ev_press(KEY(4));
    ev_tap(CTL_PLAY);
    CHECK(!q->running);
    ev_idle(1);
    CHECK_EQ(q->step[11].note[0], 64);                                   // (step 12 of the demo is a rest)
    CHECK_EQ(q->step[11].len, 1);
    ev_release(KEY(4));
    CHECK_EQ(ui_app.ui.cursor, 0);                                       // (a live note never moves the cursor)
    engine_at(-1, 0, 0);
}

TEST(seq_rec_key_function_and_the_section_switch) {
    ui_fresh();
    keymap_set(KEY_COLS + 7, (key_fn_t){ACT_PAD_REC, 0});                // a key of the user's layout: Rec
    ev_tap((control_id_t)CTL_KEY(1, 7));
    CHECK(popup_modal(&ui_app.popup) && !ui_app.rec.armed);               // the list first
    ev_tap(CTL_JOY_SW);                                                  // Notes
    CHECK(ui_app.rec.armed);
    ev_tap((control_id_t)CTL_KEY(1, 7));
    CHECK(!ui_app.rec.armed && !popup_modal(&ui_app.popup));             // armed: Rec again is off at once
    ev_tap((control_id_t)CTL_KEY(1, 7));
    ev_tap(CTL_JOY_SW);
    app_set_section(&ui_app, true);                                      // the Pads section: the sequencer's Rec is off
    CHECK(!ui_app.rec.armed && !ui_app.ui.seq_rec);
    ev_press(SHIFT); ev_tap(CTL_PLAY); ev_release(SHIFT);                // there Play is the clock, with or without Shift
    CHECK(!ui_app.rec.armed);
    app_set_section(&ui_app, false);
}

// Back held while a live recording runs: the playhead erases, one Undo brings it all back (user, 2026-10-14).
TEST(seq_rec_back_held_erases_under_the_playhead_and_undo_brings_it_back) {
    ui_fresh();
    seq_t *q = &ui_app.seq;
    char msg[28];
    q->step[4] = (seq_step_t){{60, 64, 0, 0}, 2, 90, 50, 2};
    q->step[5] = (seq_step_t){{62, 0, 0, 0}, 1, 100, 100, 1};
    q->step[6] = (seq_step_t){{65, 0, 0, 0}, 1, 100, 100, 1};
    const seq_t before = *q;
    seq_step_t rest;
    seq_step_clear(&rest);
    // it runs, Rec is not armed: Back is Back
    ev_tap(CTL_PLAY);
    engine_at(4, 10, 4);
    ev_press(CTL_BTN_2); ev_idle(1);
    CHECK(!ui_app.ui.seq_erase);
    ev_release(CTL_BTN_2);
    ev_tap(CTL_PLAY);
    // armed and stopped (step entry): Back is Back too
    rec();
    ev_press(CTL_BTN_2); ev_idle(1);
    CHECK(!ui_app.ui.seq_erase);
    ev_release(CTL_BTN_2);
    CHECK(!memcmp(q->step, before.step, sizeof before.step));
    // armed and running: the step that plays when Back goes down, then every step the playhead reaches while it is held
    ev_tap(CTL_PLAY);
    CHECK(q->running && ui_app.rec.armed);
    engine_at(4, 10, 4);
    ev_press(CTL_BTN_2);
    CHECK(ui_app.ui.seq_erase);
    CHECK(!memcmp(&q->step[4], &rest, sizeof rest));                     // a rest with the default values
    CHECK_EQ(q->step[5].note[0], 62);
    engine_at(5, 0, 5);
    ev_idle(1);
    CHECK(!memcmp(&q->step[5], &rest, sizeof rest));
    ev_release(CTL_BTN_2);
    CHECK(!ui_app.ui.seq_erase);
    engine_at(6, 0, 6);
    ev_idle(1);
    CHECK_EQ(q->step[6].note[0], 65);                                    // let go: nothing more goes
    // one Undo: everything that hold erased
    CHECK(seq_tool_apply(q, SQT_UNDO, SQS_ALL, 0, msg, (int)sizeof msg));
    CHECK(!memcmp(q->step, before.step, sizeof before.step));
    // a hold over steps that are rests already changes nothing, and leaves the Undo as it was
    engine_at(12, 0, 12);                                                // (step 12 of the demo is a rest)
    q->step[12] = rest;
    const seq_t rested = *q;
    ev_press(CTL_BTN_2); ev_idle(1); ev_release(CTL_BTN_2);
    CHECK(!memcmp(q->step, rested.step, sizeof rested.step));
    // the Back key of the layout (F3) erases as well; a note played into the step and not written yet goes with it
    engine_at(5, 240, 13);
    ev_tap(KEY(3));                                                      // late in step 5: it waits for step 6
    engine_at(6, 0, 14);
    ev_press((control_id_t)CTL_KEY(0, 2));
    CHECK(ui_app.ui.seq_erase);
    ev_idle(2);
    CHECK(!memcmp(&q->step[6], &rest, sizeof rest));
    ev_release((control_id_t)CTL_KEY(0, 2));
    CHECK(!ui_app.ui.seq_erase);
    // Rec switched off under the finger: the erasing ends there
    engine_at(3, 0, 19);
    ev_press(CTL_BTN_2);
    rec();
    CHECK(!ui_app.rec.armed && !ui_app.ui.seq_erase);
    const seq_t after = *q;
    engine_at(4, 0, 20);
    ev_idle(1);
    CHECK(!memcmp(q->step, after.step, sizeof after.step));
    ev_release(CTL_BTN_2);
    // the menu open: Back keeps its own work there
    rec();
    ev_tap(CTL_BTN_1);
    CHECK(ui_app.ui.in_rack);
    engine_at(4, 0, 36);
    ev_press(CTL_BTN_2);
    CHECK(!ui_app.ui.seq_erase);
    ev_idle(1);
    CHECK(!memcmp(&q->step[4], &after.step[4], sizeof rest));
    ev_release(CTL_BTN_2);
    ev_tap(CTL_BTN_1);
    ev_tap(CTL_PLAY);
    engine_at(-1, 0, 0);
}

/* ---------------- the pattern's sound recorded as a sample (app/app_capture.c; user, 2026-10-14) ---------------- */
extern int ui_stub_cap_state, ui_stub_cap_seq, ui_stub_cap_tail, ui_stub_cap_listening, ui_stub_seq_rec_skip, ui_stub_seq_rec_stop;
extern uint32_t ui_stub_seq_halts;
extern char ui_stub_cap_name[AUDIO_NAME_MAX];
void ui_stub_capture_sound(uint32_t frames);
enum { OPT_NOTES, OPT_AUDIO_STOP, OPT_AUDIO_ON, OPT_AUDIO_TAIL, OPT_LOOP_STOP, OPT_LOOP_ON };

// Shift + Play, then the joystick down to the option and its push.
static void rec_choose(int option) {
    ev_press(SHIFT); ev_tap(CTL_PLAY); ev_release(SHIFT);
    CHECK(popup_modal(&ui_app.popup));
    for (int k = ui_app.rec_choice; k != option; k = (k + 1) % 6) ev_tap(CTL_JOY_DOWN);
    ev_tap(CTL_JOY_SW);
}

TEST(seq_rec_list_records_the_patterns_sound_and_asks_where_it_goes) {
    ui_stub_ramcard(false); ui_stub_ramcard(true);
    ui_fresh();
    seq_t *q = &ui_app.seq;
    // the list closed with Back: nothing
    ev_press(SHIFT); ev_tap(CTL_PLAY); ev_release(SHIFT);
    CHECK(popup_modal(&ui_app.popup));
    ev_tap(CTL_BTN_2);
    CHECK(!popup_modal(&ui_app.popup) && !ui_app.rec.armed && ui_stub_cap_state == CAPTURE_ST_IDLE);
    // Audio + tail, stopped: the file opens first, then the pass is asked for and the sequencer starts
    ui_stub_cap_listening = 0;
    rec_choose(OPT_AUDIO_TAIL);
    CHECK(ui_stub_cap_state == CAPTURE_ST_ARMED && ui_stub_cap_seq == 1 && ui_stub_cap_tail == 1);
    CHECK_STR(ui_stub_cap_name, "rec1");
    CHECK(ui_app.ui.seq_audio && !ui_app.rec.armed);
    ev_idle(2);
    CHECK(ui_stub_seq_rec_skip == -2 && !q->running);                    // not before the file listens
    ui_stub_cap_listening = 1;
    ev_idle(1);
    CHECK(ui_stub_seq_rec_skip == 0 && ui_stub_seq_rec_stop == 1);       // the next pass, and stop at its end
    CHECK(q->running);
    ui_stub_capture_sound(48000);
    ui_stub_seq_halts = 1;                                               // the engine stopped the run at the pass's end ...
    ev_idle(1);
    CHECK(!q->running);
    CHECK_EQ(ui_stub_cap_state, CAPTURE_ST_RECORDING);                   // ... the tail goes on
    audio_capture_stop();                                                // (the tap ends by itself: here the test ends it)
    ev_idle(3);
    CHECK(popup_modal(&ui_app.popup) && !ui_app.ui.seq_audio);            // where does it go? A1 (empty), the library
    ev_tap(CTL_JOY_SW);
    CHECK_STR(ui_app.pads.pad[0].file, "rec1");
    CHECK(ui_app.pads.pad[0].loop == PAD_LOOP_OFF && ui_app.pads.pad[0].level == 100);
    CHECK_EQ(ui_app.capture.stage, 0);
    CHECK_EQ(ui_app.rec_choice, OPT_AUDIO_TAIL);                          // the list opens there next time
    // Loop, play on, while it runs: two passes, the second kept; the sequencer goes on; it goes on the next empty pad, looping
    ev_tap(CTL_PLAY);
    CHECK(q->running);
    rec_choose(OPT_LOOP_ON);
    ev_idle(1);
    CHECK(ui_stub_seq_rec_skip == 1 && ui_stub_seq_rec_stop == 0);
    ui_stub_capture_sound(96000);
    audio_capture_stop();
    ev_idle(3);
    CHECK(q->running);
    ev_tap(CTL_JOY_DOWN);                                                // "Pad A1 (replace)", "Next empty: A2", "Library only"
    ev_tap(CTL_JOY_SW);
    const pad_t *p = &ui_app.pads.pad[1];
    CHECK_STR(p->file, "rec2");
    CHECK(p->loop == PAD_LOOP_TRIM && p->mode == PAD_TOGGLE && p->grid == PAD_GRID_BAR);   // (16 steps of 1/16: a bar)
    CHECK_STR(ui_app.pads.pad[0].file, "rec1");
    // Audio, stop, stopped by hand halfway: what it has is kept; Back on the second list: the library only
    rec_choose(OPT_AUDIO_STOP);
    ev_idle(1);
    ui_stub_capture_sound(20000);
    ev_tap(CTL_PLAY);                                                    // the sequencer stopped by hand
    ev_idle(3);
    CHECK_EQ(ui_stub_seq_rec_skip, -1);                                  // the pass asked for is forgotten
    CHECK(popup_modal(&ui_app.popup));
    ev_tap(CTL_BTN_2);
    CHECK(pad_empty(&ui_app.pads.pad[2]) && ui_app.capture.stage == 0);
    // Shift + Play while it waits: stopped, nothing came, no file
    rec_choose(OPT_AUDIO_ON);
    ev_idle(1);
    ev_press(SHIFT); ev_tap(CTL_PLAY); ev_release(SHIFT);
    ev_idle(2);
    CHECK(ui_app.capture.stage == 0 && !popup_modal(&ui_app.popup) && !ui_app.ui.seq_audio);
    // an empty pattern: refused
    ev_tap(CTL_PLAY);
    for (int k = 0; k < SEQ_MAX_STEPS; k++) seq_step_clear(&q->step[k]);
    rec_choose(OPT_AUDIO_STOP);
    CHECK_EQ(ui_stub_cap_state, CAPTURE_ST_IDLE);
    ui_stub_ramcard(false);
}
