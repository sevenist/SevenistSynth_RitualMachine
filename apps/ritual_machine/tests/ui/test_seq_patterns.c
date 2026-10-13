// The sequencer's patterns and chain (core/seq.h seq_bank_t, the PATTERNS page, app/app_seq.c): a pattern taken at once when stopped,
// queued to the engine while it runs and taken when the engine went on to it; the chain; the files on the card.
#include "ui_test.h"
#include "app/app_internal.h"
#include "core/card_layout.h"

extern seq_t ui_stub_seq;                    // tools/ui_stubs.c: what the app handed to the "engine" ...
extern seq_pattern_t ui_stub_seq_next;       // ... the queued pattern, its id (-1: none), how often something was queued
extern int ui_stub_seq_queued, ui_stub_seq_queues, ui_stub_seq_pos;
void ui_stub_seq_take(void);                 // the "engine" reaches the end of the playing pattern and goes on to the queued one

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

TEST(patterns_stopped_a_pattern_is_taken_at_once_and_keeps_its_own_settings) {
    ui_fresh();
    CHECK(open_page("PATTERNS"));
    seq_t *q = &ui_app.seq;
    seq_bank_t *b = &ui_app.bank;
    const seq_t demo = *q;
    CHECK_EQ(b->cur, 0);
    row_turn(PAT_ROW_PAT, 1);                                            // pattern 2: empty, 16 steps
    CHECK_EQ(b->cur, 1);
    for (int k = 0; k < SEQ_MAX_STEPS; k++) CHECK(seq_step_is_rest(&q->step[k]));
    CHECK_EQ(q->steps, 16);
    CHECK(seq_step_is_rest(&ui_stub_seq.step[0]));                       // the engine has it
    q->step[0].note[0] = 72; q->steps = 8; q->rate = SQR_8; q->swing = 30;   // edited
    q->bpm = 133;                                                        // (the tempo is the machine's)
    row_turn(PAT_ROW_PAT, -1);                                           // back to pattern 1: as it was
    CHECK(!memcmp(q->step, demo.step, sizeof demo.step));
    CHECK_EQ(q->steps, 16); CHECK_EQ(q->rate, SQR_16); CHECK_EQ(q->swing, 0);
    CHECK_EQ(q->bpm, 133);
    row_turn(PAT_ROW_PAT, 1);                                            // pattern 2 again: its edits were kept
    CHECK_EQ(q->step[0].note[0], 72); CHECK_EQ(q->steps, 8); CHECK_EQ(q->rate, SQR_8); CHECK_EQ(q->swing, 30);
    row_turn(PAT_ROW_PAT, -2);                                           // around: pattern 8
    CHECK_EQ(b->cur, SEQ_PATTERNS - 1);
    // a shorter pattern under the cursor: the cursor comes back into it
    row_turn(PAT_ROW_PAT, 1);
    ui_app.ui.cursor = 15;
    row_turn(PAT_ROW_PAT, 1);
    CHECK_EQ(b->cur, 1);
    CHECK_EQ(ui_app.ui.cursor, 7);
    CHECK_EQ(ui_stub_seq_queued, -1);                                    // nothing was ever queued: it does not run
    // the clipboard goes across patterns: pattern 1 copied onto pattern 3
    row_turn(PAT_ROW_PAT, -1);
    char msg[28];
    seq_tool_apply(q, SQT_COPY, SQS_ALL, 0, msg, sizeof msg);
    row_turn(PAT_ROW_PAT, 2);
    seq_tool_apply(q, SQT_PASTE, SQS_ALL, 0, msg, sizeof msg);
    CHECK_EQ(b->cur, 2);
    CHECK(!memcmp(q->step, demo.step, sizeof demo.step));
}

TEST(patterns_running_the_next_one_is_queued_and_taken_at_the_patterns_end) {
    ui_fresh();
    CHECK(open_page("PATTERNS"));
    seq_t *q = &ui_app.seq;
    seq_bank_t *b = &ui_app.bank;
    const seq_t demo = *q;
    ui_app.bank.pat[2].step[4].note[0] = 77;                             // pattern 3 has something of its own
    ui_app.bank.pat[2].steps = 12;
    ev_tap(CTL_PLAY);
    CHECK(q->running);
    CHECK_EQ(ui_stub_seq_queued, -1);
    row_turn(PAT_ROW_PAT, 2);                                            // pattern 3 picked: it follows, pattern 1 plays on
    CHECK_EQ(b->cur, 0); CHECK_EQ(b->next, 2);
    CHECK_EQ(ui_stub_seq_queued, 2);
    CHECK_EQ(ui_stub_seq_next.step[4].note[0], 77); CHECK_EQ(ui_stub_seq_next.steps, 12);
    CHECK(!memcmp(ui_stub_seq.step, demo.step, sizeof demo.step));
    char v[16];
    seq_pat_row_text(&ui_app.ui, q, PAT_ROW_PAT, v, sizeof v);
    CHECK_STR(v, "1 >3");
    q->step[1].note[0] = 50;                                             // an edit meanwhile goes to the pattern that plays
    ev_idle(1);
    CHECK_EQ(ui_stub_seq.step[1].note[0], 50);
    const int queues = ui_stub_seq_queues;
    ev_idle(5);
    CHECK_EQ(ui_stub_seq_queues, queues);                                // handed over once, not at every step
    ui_stub_seq_take();                                                  // the engine reaches the end of pattern 1
    ev_idle(1);
    CHECK_EQ(b->cur, 2); CHECK_EQ(b->next, -1);
    CHECK_EQ(q->step[4].note[0], 77); CHECK_EQ(q->steps, 12);
    CHECK_EQ(ui_stub_seq.step[4].note[0], 77);                           // what the engine is handed from now on
    CHECK_EQ(b->pat[0].step[1].note[0], 50);                             // pattern 1 was stored with its edit
    CHECK_EQ(ui_stub_seq_queued, -1);
    // picked, then picked back: nothing follows
    row_turn(PAT_ROW_PAT, 1);
    CHECK_EQ(ui_stub_seq_queued, 3);
    row_turn(PAT_ROW_PAT, -1);
    CHECK_EQ(b->next, -1);
    CHECK_EQ(ui_stub_seq_queued, -1);
    // picked, then stopped before its turn: it is taken at the stop
    row_turn(PAT_ROW_PAT, -2);
    CHECK_EQ(b->next, 0);
    ev_tap(CTL_PLAY);
    CHECK(!q->running);
    CHECK_EQ(b->cur, 0); CHECK_EQ(b->next, -1);
    CHECK_EQ(q->step[1].note[0], 50);
    CHECK_EQ(ui_stub_seq_queued, -1);
}

TEST(patterns_chain_plays_its_entries_in_turn) {
    ui_fresh();
    CHECK(open_page("PATTERNS"));
    seq_t *q = &ui_app.seq;
    seq_bank_t *b = &ui_app.bank;
    for (int k = 0; k < SEQ_PATTERNS; k++) b->pat[k].step[0].note[0] = (uint8_t)(60 + k);     // each pattern: its own first note
    q->step[0].note[0] = 60;
    // the chain 1 1 3: Len adds entries of the pattern shown, Slot / Is edit one
    CHECK_EQ(b->chain_len, 1);
    row_turn(PAT_ROW_LEN, 2);
    CHECK_EQ(b->chain_len, 3);
    row_turn(PAT_ROW_SLOT, 2);
    CHECK_EQ(ui_app.ui.seq_chain_cur, 2);
    row_turn(PAT_ROW_IS, 2);
    CHECK_EQ(b->chain[0], 0); CHECK_EQ(b->chain[1], 0); CHECK_EQ(b->chain[2], 2);
    row_turn(PAT_ROW_SLOT, 1);                                           // around
    CHECK_EQ(ui_app.ui.seq_chain_cur, 0);
    row_turn(PAT_ROW_LEN, 20);
    CHECK_EQ(b->chain_len, SEQ_CHAIN_MAX);
    row_turn(PAT_ROW_LEN, -13);
    CHECK_EQ(b->chain_len, 3);
    row_turn(PAT_ROW_CHAIN, 1);
    CHECK(b->chain_on);
    // stopped on pattern 5; Play starts the chain at its first entry
    row_turn(PAT_ROW_PAT, 4);
    CHECK_EQ(b->cur, 4);
    ev_tap(CTL_PLAY);
    CHECK_EQ(b->cur, 0); CHECK_EQ(b->chain_pos, 0);
    CHECK_EQ(ui_stub_seq.step[0].note[0], 60);
    CHECK_EQ(ui_stub_seq_queued, 0);                                     // entry 2 is pattern 1 again
    q->step[3].note[0] = 99;                                             // an edit before its end: the repeat has it too
    ev_idle(1);
    ui_stub_seq_take(); ev_idle(1);
    CHECK_EQ(b->cur, 0); CHECK_EQ(b->chain_pos, 1);
    CHECK_EQ(q->step[3].note[0], 99);
    CHECK_EQ(ui_stub_seq.step[3].note[0], 99);
    CHECK_EQ(ui_stub_seq_queued, 2);                                     // entry 3: pattern 3
    row_turn(PAT_ROW_PAT, 3);                                            // the Pat row does nothing while the chain decides
    CHECK_EQ(b->next, -1);
    CHECK_EQ(ui_stub_seq_queued, 2);
    ui_stub_seq_take(); ev_idle(1);
    CHECK_EQ(b->cur, 2); CHECK_EQ(b->chain_pos, 2);
    CHECK_EQ(ui_stub_seq.step[0].note[0], 62);
    CHECK_EQ(ui_stub_seq_queued, 0);                                     // around: entry 1
    ui_stub_seq_take(); ev_idle(1);
    CHECK_EQ(b->cur, 0); CHECK_EQ(b->chain_pos, 0);
    // the chain switched off while it runs: the playing pattern loops, nothing follows
    row_turn(PAT_ROW_CHAIN, 1);
    CHECK(!b->chain_on);
    CHECK_EQ(ui_stub_seq_queued, -1);
    // switched on again while it runs: its first entry follows
    row_turn(PAT_ROW_CHAIN, 1);
    CHECK_EQ(ui_stub_seq_queued, 0);
    ev_tap(CTL_PLAY);
    CHECK_EQ(ui_stub_seq_queued, -1);
}

static char buf[STORAGE_FILE_MAX];

TEST(patterns_are_saved_by_themselves_and_come_back) {
    ui_stub_ramcard(true);
    ui_fresh();
    CHECK(storage_read("system/config/seq.cfg", buf, sizeof buf) < 0);    // nothing changed yet: nothing written
    CHECK(open_page("PATTERNS"));
    seq_t *q = &ui_app.seq;
    seq_bank_t *b = &ui_app.bank;
    q->step[2].ratchet = 3;                                              // pattern 1 edited
    row_turn(PAT_ROW_PAT, 3);                                            // pattern 4: written from scratch
    q->step[0] = (seq_step_t){{40, 47, 0, 0}, 4, 110, 60, 2};
    q->steps = 24; q->dir = SQD_PINGPONG;
    q->bpm = 97;
    row_turn(PAT_ROW_LEN, 2); row_turn(PAT_ROW_SLOT, 1); row_turn(PAT_ROW_IS, -3);    // the chain 1 1 4 (its first entry was there)
    row_turn(PAT_ROW_CHAIN, 1);
    ev_idle(20);
    CHECK(storage_read("system/config/pattern4.cfg", buf, sizeof buf) < 0);   // not at once: a moment after the last change
    ev_idle(900);
    CHECK(storage_read("system/config/seq.cfg", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "seq 97 4 1\n") != NULL && strstr(buf, "chain 1 1 4\n") != NULL);
    CHECK(storage_read("system/config/pattern4.cfg", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "pattern 24 0 0 2 2\n") != NULL && strstr(buf, "step 0 40 4 110 60 2 47\n") != NULL);
    CHECK(storage_read("system/config/pattern1.cfg", buf, sizeof buf) > 0);
    CHECK(storage_read("system/config/pattern2.cfg", buf, sizeof buf) < 0);   // never touched: no file
    CHECK(!ui_app.seq_pending && !app_seq_unsaved(&ui_app));
    ui_fresh();                                                          // power off, power on
    q = &ui_app.seq; b = &ui_app.bank;
    CHECK_EQ(b->cur, 3); CHECK(b->chain_on); CHECK_EQ(b->chain_len, 3);
    CHECK_EQ(b->chain[0], 0); CHECK_EQ(b->chain[1], 0); CHECK_EQ(b->chain[2], 3);
    CHECK_EQ(q->bpm, 97); CHECK_EQ(q->steps, 24); CHECK_EQ(q->dir, SQD_PINGPONG);
    CHECK_EQ(q->step[0].note[0], 40); CHECK_EQ(q->step[0].note[1], 47); CHECK_EQ(q->step[0].len, 4); CHECK_EQ(q->step[0].prob, 60);
    CHECK_EQ(b->pat[0].step[2].ratchet, 3);
    CHECK(!app_seq_unsaved(&ui_app));
    ev_idle(900);
    CHECK(!ui_app.seq_pending);                                          // what was read is what the card holds: no write follows
    // switching off with an unsaved edit: written at once
    q->step[9].note[0] = 55;
    CHECK(app_seq_unsaved(&ui_app));
    app_save_now(&ui_app);
    CHECK(!app_seq_unsaved(&ui_app));
    CHECK(storage_read("system/config/pattern4.cfg", buf, sizeof buf) > 0 && strstr(buf, "step 9 55\n") != NULL);
    ui_stub_ramcard(false);
}
