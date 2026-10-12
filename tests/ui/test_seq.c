// The step sequencer's model and pages (core/seq.h, the SEQUENCER and SEQ SETUP pages): step values, chords, long patterns, what the
// application hands to the engine's sequencer (hal_audio.h audio_seq_set; ADR-042), and the pattern in the working state's file.
#include "ui_test.h"
#include "core/patch.h"
#include "hal/hal_storage.h"

extern seq_t ui_stub_seq;                    // tools/ui_stubs.c: the pattern the app handed over last, the step the "engine" plays
extern int ui_stub_seq_pos, ui_stub_clock_run, ui_stub_clock_bpm;

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

TEST(seq_page_edits_the_values_of_a_step) {
    ui_fresh();
    CHECK(open_page("SEQUENCER"));
    seq_t *q = &ui_app.seq;
    row_turn(SEQ_ROW_STEP, 2);
    CHECK_EQ(ui_app.ui.cursor, 2);
    const int n0 = q->step[2].note[0];
    CHECK(n0 > 0);                                                       // the demo pattern has a note there
    row_turn(SEQ_ROW_NOTE, 1);
    CHECK_EQ(q->step[2].note[0], n0 + 1);
    row_turn(SEQ_ROW_LEN, 3);
    CHECK_EQ(q->step[2].len, 4);
    row_turn(SEQ_ROW_VEL, -1);
    CHECK_EQ(q->step[2].vel, 95);
    row_turn(SEQ_ROW_VEL, 8);
    CHECK_EQ(q->step[2].vel, 127);                                       // the top of the range is not on the fives
    row_turn(SEQ_ROW_VEL, -1);
    CHECK_EQ(q->step[2].vel, 125);
    row_turn(SEQ_ROW_PROB, -4);
    CHECK_EQ(q->step[2].prob, 80);
    row_turn(SEQ_ROW_PROB, 9);
    CHECK_EQ(q->step[2].prob, 100);
    row_turn(SEQ_ROW_RATCHET, 2);
    CHECK_EQ(q->step[2].ratchet, 3);
    row_turn(SEQ_ROW_RATCHET, 5);
    CHECK_EQ(q->step[2].ratchet, SEQ_RATCHET_MAX);
    // the engine's sequencer has what the page shows
    CHECK_EQ(ui_stub_seq.step[2].note[0], n0 + 1);
    CHECK_EQ(ui_stub_seq.step[2].len, 4);
    CHECK_EQ(ui_stub_seq.step[2].ratchet, SEQ_RATCHET_MAX);
    // a note lowered below the range is a rest; raised again it is the last note entered
    for (int i = 0; i < 200 && q->step[2].note[0]; i++) row_turn(SEQ_ROW_NOTE, -1);
    CHECK(seq_step_is_rest(&q->step[2]));
    row_turn(SEQ_ROW_NOTE, -1);
    CHECK(seq_step_is_rest(&q->step[2]));
    row_turn(SEQ_ROW_NOTE, 1);
    CHECK_EQ(q->step[2].note[0], SEQ_NOTE_MIN);                          // (the walk down ended on the lowest note)
}

TEST(seq_chord_notes_are_edited_by_voice) {
    ui_fresh();
    CHECK(open_page("SEQUENCER"));
    seq_t *q = &ui_app.seq;
    q->step[0].note[0] = 60;
    row_turn(SEQ_ROW_NOTE, 4);                                           // voice 1: 64, now the last note entered
    CHECK_EQ(q->step[0].note[0], 64);
    row_turn(SEQ_ROW_VOICE, 1);
    CHECK_EQ(ui_app.ui.seq_voice, 1);
    CHECK_EQ(q->step[0].note[1], 0);
    row_turn(SEQ_ROW_NOTE, 1);                                           // an empty voice starts on the last note entered ...
    CHECK_EQ(q->step[0].note[1], 64);
    row_turn(SEQ_ROW_NOTE, 3);                                           // ... and moves from there
    CHECK_EQ(q->step[0].note[1], 67);
    CHECK_EQ(q->step[0].note[0], 64);                                    // the other voice stays
    row_turn(SEQ_ROW_VOICE, 3);                                          // around: voice 1 again
    CHECK_EQ(ui_app.ui.seq_voice, 0);
    row_turn(SEQ_ROW_VOICE, -1);
    CHECK_EQ(ui_app.ui.seq_voice, SEQ_CHORD - 1);
    CHECK_EQ(ui_stub_seq.step[0].note[1], 67);
    // the highest note is the top
    q->step[0].note[3] = SEQ_NOTE_MAX;
    row_turn(SEQ_ROW_NOTE, 1);
    CHECK_EQ(q->step[0].note[3], SEQ_NOTE_MAX);
}

TEST(seq_play_runs_the_engine_sequencer_with_the_clock) {
    ui_fresh();
    CHECK(open_page("SEQUENCER"));
    CHECK(!ui_stub_seq.running && !ui_stub_clock_run);
    CHECK_EQ(ui_app.seq.pos, -1);
    ev_tap(CTL_PLAY);
    CHECK(ui_app.seq.running && ui_stub_seq.running && ui_stub_clock_run);
    CHECK_EQ(ui_stub_clock_bpm, ui_app.seq.bpm);
    ui_stub_seq_pos = 5;                                                 // the engine plays step 5: the page shows it
    ev_idle(1);
    CHECK_EQ(ui_app.seq.pos, 5);
    row_turn(SEQ_ROW_RUN, 1);                                            // the Run row stops it
    CHECK(!ui_app.seq.running && !ui_stub_seq.running && !ui_stub_clock_run);
    CHECK_EQ(ui_app.seq.pos, -1);
    ui_stub_seq_pos = -1;
}

TEST(seq_long_patterns_page_by_sixteen_steps) {
    ui_fresh();
    CHECK(open_page("SEQ SETUP"));
    row_turn(2, 16); row_turn(2, 14);                                    // Steps 16 -> 46 (an encoder turn is 16 steps at most)
    CHECK_EQ(ui_app.seq.steps, 46);
    row_turn(2, 16); row_turn(2, 16);
    CHECK_EQ(ui_app.seq.steps, SEQ_MAX_STEPS);
    CHECK_EQ(ui_stub_seq.steps, SEQ_MAX_STEPS);
    CHECK(open_page("SEQUENCER"));
    row_turn(SEQ_ROW_STEP, -1);                                          // around to the last step: the fourth page is drawn
    CHECK_EQ(ui_app.ui.cursor, SEQ_MAX_STEPS - 1);
    row_turn(SEQ_ROW_NOTE, 1);
    CHECK(ui_app.seq.step[SEQ_MAX_STEPS - 1].note[0] > 0);
    CHECK_EQ(ui_stub_seq.step[SEQ_MAX_STEPS - 1].note[0], ui_app.seq.step[SEQ_MAX_STEPS - 1].note[0]);
    row_turn(SEQ_ROW_STEP, 1);
    CHECK_EQ(ui_app.ui.cursor, 0);
    // the pattern gets shorter than the cursor: the cursor comes back into it
    ui_app.ui.cursor = 40;
    CHECK(open_page("SEQ SETUP"));
    row_turn(2, -16); row_turn(2, -16); row_turn(2, -12);
    CHECK_EQ(ui_app.seq.steps, 20);
    CHECK_EQ(ui_app.ui.cursor, 19);
    CHECK(open_page("SEQUENCER"));                                       // (drawn on its second page: steps 17..20)
    // the playhead on another page than the cursor's, and on the cursor's
    ev_tap(CTL_PLAY);
    ui_stub_seq_pos = 3; ev_idle(1);
    ui_stub_seq_pos = 18; ev_idle(1);
    CHECK_EQ(ui_app.seq.pos, 18);
    ev_tap(CTL_PLAY);
    ui_stub_seq_pos = -1;
}

TEST(seq_setup_has_the_rate_and_the_direction) {
    ui_fresh();
    CHECK(open_page("SEQ SETUP"));
    char v[16];
    seq_param_format(&ui_app.seq, SQP_RATE, v, sizeof v);
    CHECK_STR(v, "1/16");
    row_turn(5, 1);
    CHECK_EQ(ui_app.seq.rate, SQR_16T);
    row_turn(5, 5);
    CHECK_EQ(ui_app.seq.rate, SQR_32);
    seq_param_format(&ui_app.seq, SQP_RATE, v, sizeof v);
    CHECK_STR(v, "1/32");
    row_turn(5, -9);
    CHECK_EQ(ui_app.seq.rate, SQR_8);
    row_turn(6, 2);
    CHECK_EQ(ui_app.seq.dir, SQD_PINGPONG);
    seq_param_format(&ui_app.seq, SQP_DIR, v, sizeof v);
    CHECK_STR(v, "Ping");
    CHECK_EQ(ui_stub_seq.rate, SQR_8);
    CHECK_EQ(ui_stub_seq.dir, SQD_PINGPONG);
    // the old settings keep their numbers (a knob's target in ui.cfg names them)
    CHECK_EQ(SQP_BPM, 0); CHECK_EQ(SQP_STEPS, 1); CHECK_EQ(SQP_TRANSPOSE, 2); CHECK_EQ(SQP_SWING, 3);
}

TEST(seq_motion_lanes_keep_sixteen_steps_under_a_long_pattern) {
    ui_fresh();
    ui_set_type(SYNTH_MODULAR);
    CHECK(rack_insert(&ui_app.rack, ui_app.rack.count, MOD_MSEQ));
    synth_ui_rebuild_pages(&ui_app.ui, &ui_app.rack);
    ui_app.seq.steps = 40;
    CHECK_EQ(seq_motion_steps(&ui_app.seq), SEQ_PAGE);
    bool found = false;
    char t[24];
    for (int p = 0; p < ui_app.ui.page_count && !found; p++) {
        ui_app.ui.page = p;
        ui_page_title(t, sizeof t);
        found = !strncmp(t, "MOTION", 6);
    }
    CHECK(found);
    ui_app.ui.ms_step = 0;
    row_turn(2, -1);                                                     // the lane's Step row: around its 16 steps, not the pattern's 40
    CHECK_EQ(ui_app.ui.ms_step, SEQ_PAGE - 1);
    ev_tap(CTL_PLAY);
    ui_stub_seq_pos = 37; ev_idle(1);                                    // the playhead: step 37 of the pattern is the lane's step 5
    ev_tap(CTL_PLAY);
    ui_stub_seq_pos = -1;
    ui_app.seq.steps = 5;                                                // a short pattern: the lane has its 5 steps
    row_turn(2, 1);
    CHECK(ui_app.ui.ms_step < 5);
}

/* ---------------- a pattern as text (its file on the card) ---------------- */

static char text[STORAGE_FILE_MAX];

TEST(seq_pattern_text_round_trip_and_limits) {
    ui_fresh();
    seq_t *q = &ui_app.seq;
    q->steps = 48; q->rate = SQR_8T; q->dir = SQD_RANDOM; q->swing = 25; q->transpose = -5;
    seq_step_clear(&q->step[0]);                                         // a rest where the demo has a note
    q->step[5] = (seq_step_t){{60, 64, 67, 70}, 3, 90, 75, 2};
    q->step[40] = (seq_step_t){{36, 0, 0, 0}, 1, SEQ_VEL_DEFAULT, 100, 1};
    q->step[63] = (seq_step_t){{0, 0, 72, 0}, 1, SEQ_VEL_DEFAULT, 100, 4};   // only its third voice
    seq_pattern_t want, back;
    seq_pattern_get(q, &want);
    CHECK(seq_pattern_to_text(&want, text, sizeof text) > 0);
    CHECK(strstr(text, "\npattern 48 -5 25 1 3\n") != NULL);
    CHECK(strstr(text, "\nstep 5 60 3 90 75 2 64 67 70\n") != NULL);
    CHECK(strstr(text, "\nstep 40 36\n") != NULL);                       // the defaults at the end are left out
    CHECK(strstr(text, "\nstep 0 ") == NULL);                            // a plain rest has no line
    seq_pattern_clear(&back);
    CHECK(seq_pattern_from_text(&back, text));
    CHECK(!memcmp(&back, &want, sizeof want));
    CHECK_EQ(seq_pattern_hash(&back), seq_pattern_hash(&want));
    // values out of range in a file are brought back into it; a text that is not a pattern changes nothing
    CHECK(seq_pattern_from_text(&back, "pattern 99 0 0 9 9\nstep 2 200 0 300 150 9 5\nstep 70 60\nstep -1 60\n"));
    CHECK_EQ(back.steps, SEQ_MAX_STEPS); CHECK_EQ(back.rate, SQR_COUNT - 1); CHECK_EQ(back.dir, SQD_COUNT - 1);
    CHECK_EQ(back.step[2].note[0], SEQ_NOTE_MAX); CHECK_EQ(back.step[2].len, 1); CHECK_EQ(back.step[2].vel, 127);
    CHECK_EQ(back.step[2].prob, 100); CHECK_EQ(back.step[2].ratchet, SEQ_RATCHET_MAX); CHECK_EQ(back.step[2].note[1], SEQ_NOTE_MIN);
    CHECK(seq_step_is_rest(&back.step[0]));
    const seq_pattern_t kept = back;
    CHECK(!seq_pattern_from_text(&back, "patch 1\nstep 3 60\n"));
    CHECK(!memcmp(&back, &kept, sizeof kept));
    // the longest pattern there can be fits the file buffer
    for (int k = 0; k < SEQ_MAX_STEPS; k++) want.step[k] = (seq_step_t){{100, 101, 102, 103}, 16, 127, 95, 4};
    const int n = seq_pattern_to_text(&want, text, sizeof text);
    printf("    the longest pattern: %d characters of %d\n", n, STORAGE_FILE_MAX);
    CHECK(n > 0);
    CHECK_EQ(seq_pattern_to_text(&want, text, 300), -1);                 // does not fit: said so
    // the sound's own text has no sequencer in it
    CHECK(patch_to_text(&ui_app.rack, &ui_app.params, &ui_app.ui, true, "", false, text, sizeof text) > 0);
    CHECK(strstr(text, "step ") == NULL && strstr(text, "seq ") == NULL);
}
