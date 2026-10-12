// Recording into the step sequencer from the keys (app/app_seq.c): Rec armed by Shift + Play or the key function Rec; stopped = step
// entry (notes and chords at the cursor, which moves on), running = live (the nearest step, the held length, added to the chord).
#include "ui_test.h"

#define SHIFT CTL_BTN_3
#define KEY(n) ((control_id_t)CTL_KEY(4, (n)))   // the bottom row of "Keys 8x4": note 60 + n

extern int ui_stub_seq_pos, ui_stub_seq_frac;    // tools/ui_stubs.c: the step the "engine" plays, how far into it, the steps before it
extern uint32_t ui_stub_seq_count;

static void rec(void) { ev_press(SHIFT); ev_tap(CTL_PLAY); ev_release(SHIFT); }
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
    CHECK(ui_app.rec.armed);
    ev_tap((control_id_t)CTL_KEY(1, 7));
    CHECK(!ui_app.rec.armed);
    ev_tap((control_id_t)CTL_KEY(1, 7));
    app_set_section(&ui_app, true);                                      // the Pads section: the sequencer's Rec is off
    CHECK(!ui_app.rec.armed && !ui_app.ui.seq_rec);
    ev_press(SHIFT); ev_tap(CTL_PLAY); ev_release(SHIFT);                // there Play is the clock, with or without Shift
    CHECK(!ui_app.rec.armed);
    app_set_section(&ui_app, false);
}
