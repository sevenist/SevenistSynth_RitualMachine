// The SEQUENCER page's quick edit tools (core/seq.h seq_tool_apply: the Tool row, a push runs it) and its column knobs, which set the
// cursor's step once they have moved (SEQ_KNOB_HYST).
#include "ui_test.h"

extern seq_t ui_stub_seq;                    // tools/ui_stubs.c: the pattern the app handed to the engine last

static bool open_page(const char *title) {
    char t[24];
    for (int p = 0; p < ui_app.ui.page_count; p++) {
        ui_app.ui.page = p;
        ui_page_title(t, sizeof t);
        if (!strcmp(t, title)) { ui_app.ui.row = 0; ev_idle(1); return true; }
    }
    return false;
}

static void row_turn(int row, int detents) { ui_app.ui.row = row; ev_turn(CTL_ENC_B, detents); }

static void tool(int which, int scope) {                                 // the Tool row: pick, then a push
    ui_app.ui.seq_tool = (uint8_t)which; ui_app.ui.seq_scope = (uint8_t)scope;
    ui_app.ui.row = SEQ_ROW_TOOL;
    ev_tap(CTL_JOY_SW);
}

TEST(seq_tools_copy_paste_clear_and_undo) {
    ui_fresh();
    CHECK(open_page("SEQUENCER"));
    seq_t *q = &ui_app.seq;
    const seq_t demo = *q;
    row_turn(SEQ_ROW_TOOL, 2);                                           // the row picks the tool, the next one its scope
    CHECK_EQ(ui_app.ui.seq_tool, SQT_CLEAR);
    row_turn(SEQ_ROW_SCOPE, -1);
    CHECK_EQ(ui_app.ui.seq_scope, SQS_ALL);
    CHECK(!memcmp(q->step, demo.step, sizeof demo.step));                // picking changes nothing
    // a step: copy 0, paste on 1
    ui_app.ui.cursor = 0;
    tool(SQT_COPY, SQS_STEP);
    CHECK(popup_info_showing(&ui_app.popup));                            // it says what it did
    ui_app.ui.cursor = 1;
    tool(SQT_PASTE, SQS_ALL);                                            // (Paste follows what was copied, not the scope shown)
    CHECK(!memcmp(&q->step[1], &demo.step[0], sizeof demo.step[0]));
    CHECK(!memcmp(&q->step[2], &demo.step[2], sizeof demo.step[0]));
    tool(SQT_UNDO, SQS_STEP);
    CHECK(!memcmp(q->step, demo.step, sizeof demo.step));
    tool(SQT_UNDO, SQS_STEP);                                            // again: the paste is back
    CHECK(!memcmp(&q->step[1], &demo.step[0], sizeof demo.step[0]));
    tool(SQT_UNDO, SQS_STEP);
    // a page: the first one onto the second (a 32-step pattern)
    q->steps = 32;
    ui_app.ui.cursor = 3;
    tool(SQT_COPY, SQS_PAGE);
    ui_app.ui.cursor = 20;
    tool(SQT_PASTE, SQS_STEP);
    CHECK(!memcmp(&q->step[16], &demo.step[0], 16 * sizeof demo.step[0]));
    CHECK(!memcmp(&q->step[0], &demo.step[0], 16 * sizeof demo.step[0]));
    // clear: a step, a page, the pattern
    ui_app.ui.cursor = 0;
    tool(SQT_CLEAR, SQS_STEP);
    CHECK(seq_step_is_rest(&q->step[0]) && !seq_step_is_rest(&q->step[2]));
    tool(SQT_CLEAR, SQS_PAGE);
    CHECK(seq_step_is_rest(&q->step[2]) && !seq_step_is_rest(&q->step[16 + 2]));
    tool(SQT_CLEAR, SQS_ALL);
    for (int k = 0; k < SEQ_MAX_STEPS; k++) CHECK(seq_step_is_rest(&q->step[k]));
    CHECK_EQ(q->steps, 32);
    // a pattern: paste brings its length too
    *q = demo;
    tool(SQT_COPY, SQS_ALL);
    q->steps = 5;
    tool(SQT_CLEAR, SQS_ALL);
    tool(SQT_PASTE, SQS_STEP);
    CHECK_EQ(q->steps, 16);
    CHECK(!memcmp(q->step, demo.step, sizeof demo.step));
    CHECK(!memcmp(ui_stub_seq.step, demo.step, sizeof demo.step));       // the engine's sequencer has it
}

TEST(seq_tools_shift_reverse_and_double) {
    ui_fresh();
    CHECK(open_page("SEQUENCER"));
    seq_t *q = &ui_app.seq;
    q->steps = 8;
    for (int k = 0; k < SEQ_MAX_STEPS; k++) { seq_step_clear(&q->step[k]); q->step[k].note[0] = (uint8_t)(40 + k); }
    tool(SQT_SHIFT_L, SQS_ALL);
    CHECK_EQ(q->step[0].note[0], 41); CHECK_EQ(q->step[7].note[0], 40); CHECK_EQ(q->step[8].note[0], 48);   // around the pattern's 8 steps
    tool(SQT_SHIFT_R, SQS_STEP);                                         // (a step alone cannot move: the pattern does)
    CHECK_EQ(q->step[0].note[0], 40); CHECK_EQ(q->step[7].note[0], 47);
    tool(SQT_REVERSE, SQS_ALL);
    CHECK_EQ(q->step[0].note[0], 47); CHECK_EQ(q->step[7].note[0], 40); CHECK_EQ(q->step[8].note[0], 48);
    tool(SQT_UNDO, SQS_ALL);
    CHECK_EQ(q->step[0].note[0], 40);
    tool(SQT_DOUBLE, SQS_ALL);
    CHECK_EQ(q->steps, 16);
    CHECK_EQ(q->step[8].note[0], 40); CHECK_EQ(q->step[15].note[0], 47); CHECK_EQ(q->step[16].note[0], 56);
    tool(SQT_DOUBLE, SQS_ALL); tool(SQT_DOUBLE, SQS_ALL);
    CHECK_EQ(q->steps, 64);
    CHECK_EQ(q->step[63].note[0], 47);
    const seq_t full = *q;
    tool(SQT_DOUBLE, SQS_ALL);                                           // as long as it gets: nothing happens
    CHECK(!memcmp(q->step, full.step, sizeof full.step));
    // a page of a long pattern moves by itself
    ui_app.ui.cursor = 17;
    tool(SQT_SHIFT_L, SQS_PAGE);
    CHECK_EQ(q->step[16].note[0], full.step[17].note[0]); CHECK_EQ(q->step[31].note[0], full.step[16].note[0]);
    CHECK_EQ(q->step[15].note[0], full.step[15].note[0]); CHECK_EQ(q->step[32].note[0], full.step[32].note[0]);
    // a shorter pattern pasted under the cursor: the cursor comes back into it
    q->steps = 4;
    tool(SQT_COPY, SQS_ALL);
    q->steps = 64; ui_app.ui.cursor = 50;
    tool(SQT_PASTE, SQS_ALL);
    CHECK_EQ(q->steps, 4);
    CHECK_EQ(ui_app.ui.cursor, 3);
}

TEST(seq_tools_randomise_inside_the_pattern_and_keep_rests) {
    ui_fresh();
    CHECK(open_page("SEQUENCER"));
    seq_t *q = &ui_app.seq;
    const seq_t demo = *q;                                               // notes 48..63, rests in between
    tool(SQT_RND_NOTE, SQS_ALL);
    int moved = 0;
    for (int k = 0; k < 16; k++) {
        CHECK_EQ(seq_step_is_rest(&q->step[k]), seq_step_is_rest(&demo.step[k]));
        if (seq_step_is_rest(&q->step[k])) continue;
        CHECK(q->step[k].note[0] >= 48 && q->step[k].note[0] <= 63);     // between the pattern's lowest and highest
        moved += q->step[k].note[0] != demo.step[k].note[0];
    }
    CHECK(moved >= 5);
    tool(SQT_UNDO, SQS_ALL);
    CHECK(!memcmp(q->step, demo.step, sizeof demo.step));
    tool(SQT_HUMANISE, SQS_ALL);
    moved = 0;
    for (int k = 0; k < 16; k++) {
        if (seq_step_is_rest(&q->step[k])) { CHECK_EQ(q->step[k].vel, SEQ_VEL_DEFAULT); continue; }
        CHECK(q->step[k].vel >= 80 && q->step[k].vel <= 120);
        moved += q->step[k].vel != SEQ_VEL_DEFAULT;
    }
    CHECK(moved >= 5);
    tool(SQT_RND_PROB, SQS_PAGE);
    for (int k = 0; k < 16; k++) if (!seq_step_is_rest(&q->step[k])) CHECK(q->step[k].prob >= 50 && q->step[k].prob <= 100 && q->step[k].prob % 5 == 0);
    // one step only
    *q = demo;
    ui_app.ui.cursor = 4;
    tool(SQT_HUMANISE, SQS_STEP);
    for (int k = 0; k < 16; k++) if (k != 4) CHECK_EQ(q->step[k].vel, SEQ_VEL_DEFAULT);
    // a single pitch: an octave around it; no notes: nothing
    for (int k = 0; k < 16; k++) { seq_step_clear(&q->step[k]); q->step[k].note[0] = 60; }
    tool(SQT_RND_NOTE, SQS_ALL);
    for (int k = 0; k < 16; k++) CHECK(q->step[k].note[0] >= 54 && q->step[k].note[0] <= 66);
    tool(SQT_CLEAR, SQS_ALL);
    const seq_t empty = *q;
    tool(SQT_RND_NOTE, SQS_ALL);
    CHECK(!memcmp(q->step, empty.step, sizeof empty.step));
}

#define KNOB(k) ((control_id_t)(CTL_COL_KNOB_0 + (k)))

TEST(seq_knobs_set_the_step_once_they_have_moved) {
    ui_fresh();
    CHECK(open_page("SEQUENCER"));
    seq_t *q = &ui_app.seq;
    const seq_step_t s0 = q->step[0];
    ev_knob(KNOB(2), 500);                                               // first seen: this is where the Vel knob stands
    ev_knob(KNOB(2), 500 + SEQ_KNOB_HYST - 1);                           // noise: nothing
    CHECK(!memcmp(&q->step[0], &s0, sizeof s0));
    ev_knob(KNOB(2), 500 + SEQ_KNOB_HYST);                               // moved: it writes, and goes on writing
    CHECK_EQ(q->step[0].vel, 1 + (int)((500 + SEQ_KNOB_HYST) * 126.0f / INPUT_VALUE_MAX + 0.5f));
    ev_knob(KNOB(2), INPUT_VALUE_MAX);
    CHECK_EQ(q->step[0].vel, 127);
    ev_knob(KNOB(2), 0);
    CHECK_EQ(q->step[0].vel, 1);
    ev_knob(KNOB(2), 512);
    const int vel = q->step[0].vel;
    // the cursor moves to another step: the knob must move again before it writes there
    row_turn(SEQ_ROW_STEP, 3);
    const seq_step_t s3 = q->step[3];
    ev_knob(KNOB(2), 512 + 5);                                           // where it reports from: its reference on this step
    ev_knob(KNOB(2), 512 + 5 - SEQ_KNOB_HYST + 1);
    CHECK(!memcmp(&q->step[3], &s3, sizeof s3));
    CHECK_EQ(q->step[0].vel, vel);
    ev_knob(KNOB(2), 512 + 5 - SEQ_KNOB_HYST);
    CHECK(q->step[3].vel != s3.vel);
    // the other knobs: Note (the bottom = a rest), Len, Prob
    ev_knob(KNOB(0), 300); ev_knob(KNOB(0), INPUT_VALUE_MAX);
    CHECK_EQ(q->step[3].note[0], SEQ_NOTE_MAX);
    ev_knob(KNOB(0), 0);
    CHECK_EQ(q->step[3].note[0], 0);
    ev_knob(KNOB(1), 300); ev_knob(KNOB(1), INPUT_VALUE_MAX);
    CHECK_EQ(q->step[3].len, SEQ_PAGE);
    ev_knob(KNOB(1), 0);
    CHECK_EQ(q->step[3].len, 1);
    ev_knob(KNOB(3), 300); ev_knob(KNOB(3), INPUT_VALUE_MAX / 2);
    CHECK_EQ(q->step[3].prob, 50);
    // another voice of the chord: the Note knob must move again
    row_turn(SEQ_ROW_VOICE, 1);
    ev_knob(KNOB(0), 4);
    CHECK_EQ(q->step[3].note[1], 0);
    ev_knob(KNOB(0), 600);
    CHECK(q->step[3].note[1] > 0);
    CHECK_EQ(q->step[3].note[0], 0);
    // back from another page: the same step, but the knobs must move again
    const seq_step_t kept = q->step[3];
    ui_app.ui.row = 0; ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_LEFT);      // SEQ SETUP and back
    ev_knob(KNOB(2), 512 - SEQ_KNOB_HYST - 6);
    CHECK(!memcmp(&q->step[3], &kept, sizeof kept));
    CHECK_EQ(ui_stub_seq.step[3].prob, 50);                              // the engine's sequencer has what the knobs wrote
}
