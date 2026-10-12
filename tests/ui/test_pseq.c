// The Pads section's sequencer (core/pad_seq.h, app/app_pseq.c, the STEPS screen of scr_pads.c; ADR-042 stage 3): the pattern as a
// list of bit-field hits, step mode (Select + Play: the pad keys are steps), the hand-over to the engine, patterns and chain, live
// record (Select + Rec), the files.
#include "ui_test.h"
#include "app/app_internal.h"
#include "core/led_roles.h"
#include "hal/hal_leds.h"
#include "hal/hal_storage.h"

extern pseq_pattern_t ui_stub_pseq, ui_stub_pseq_next;      // tools/ui_stubs.c: what the app handed to the "engine"
extern int ui_stub_pseq_run, ui_stub_pseq_pos, ui_stub_pseq_frac, ui_stub_pseq_queued, ui_stub_pad_pushes, ui_stub_cap_state, ui_stub_clock_run;
extern uint32_t ui_stub_pseq_count;
extern led_color_t ui_stub_led[CTL_COUNT];
void ui_stub_pseq_take(void);

#define PADK(k)  ((control_id_t)CTL_KEY(1 + (k) / 4, (k) % 4))   // the Pads layout: the left 4 x 4 block
#define SELECT   ((control_id_t)CTL_KEY(2, 4))
#define REC      ((control_id_t)CTL_KEY(2, 6))
#define PLAY     ((control_id_t)CTL_KEY(0, 3))
#define BACK     ((control_id_t)CTL_KEY(0, 2))
#define BANK_B   ((control_id_t)CTL_KEY(1, 5))

static void pads_section(void) { ui_fresh(); app_set_section(&ui_app, true); ev_idle(1); }
static void steps_mode(void) { ev_press(SELECT); ev_tap(PLAY); ev_release(SELECT); }
static pseq_pattern_t *pat(void) { return &ui_app.pbank.pat[ui_app.pbank.cur]; }
static void field_turn(int field, int detents) { ui_app.ui.pseq_row = (uint8_t)field; ev_turn(CTL_ENC_B, detents); }
static bool same(led_color_t a, led_color_t b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

TEST(pseq_pattern_is_a_compact_list_of_hits) {
    pseq_pattern_t p;
    CHECK_EQ(sizeof(pseq_hit_t), 4);                                     // one 32-bit bit-field a hit
    CHECK(sizeof p <= 648);
    pseq_clear(&p);
    CHECK_EQ(p.n, 0); CHECK_EQ(p.steps, 16);
    CHECK(pseq_toggle(&p, 3, 0) && pseq_toggle(&p, 3, 4) && pseq_toggle(&p, 63, 63));
    CHECK_EQ(p.n, 3);
    CHECK(pseq_find(&p, 3, 4) >= 0 && pseq_find(&p, 3, 5) < 0 && pseq_find(&p, 4, 4) < 0);
    CHECK(pseq_track(&p, 3) == ((1ull << 0) | (1ull << 4)));
    CHECK(pseq_used(&p) == (1ull << 3));                                 // pad 64's hit is past the pattern's 16 steps
    const pseq_hit_t *h = &p.hit[pseq_find(&p, 3, 4)];
    CHECK_EQ(h->level, PSEQ_LEVEL_MAX); CHECK_EQ(h->prob, PSEQ_PROB_MAX); CHECK_EQ(h->ratchet, 0); CHECK_EQ(h->len, 0);
    CHECK(pseq_put(&p, 3, 4, 5, 60, 3, 12));                             // the same hit: its values replaced
    CHECK_EQ(p.n, 3);
    h = &p.hit[pseq_find(&p, 3, 4)];
    CHECK_EQ(h->level, 4); CHECK_EQ(h->prob, 6); CHECK_EQ(h->ratchet, 2); CHECK_EQ(h->len, 11);
    char v[12];
    pseq_hit_format(h, PSH_LEVEL, v, sizeof v); CHECK_STR(v, "5/16");
    pseq_hit_format(h, PSH_PROB, v, sizeof v);  CHECK_STR(v, "60%");
    pseq_hit_format(h, PSH_RATCHET, v, sizeof v); CHECK_STR(v, "x3");
    pseq_hit_format(h, PSH_LEN, v, sizeof v);   CHECK_STR(v, "12");
    CHECK(pseq_toggle(&p, 3, 0));                                        // again: taken away
    CHECK_EQ(p.n, 2);
    CHECK(pseq_find(&p, 3, 0) < 0 && pseq_find(&p, 63, 63) >= 0);
    CHECK_EQ(pseq_clear_pad(&p, 3), 1);
    CHECK_EQ(p.n, 1);
    // full: 160 hits, the next one is refused, the pattern still fits its file
    pseq_clear(&p);
    p.steps = 64;
    for (int i = 0; i < PSEQ_HITS; i++) CHECK(pseq_put(&p, i % 64, i / 64 * 7 + i % 5, 1 + i % 16, 50, 1 + i % 4, 1 + i % 64));
    CHECK_EQ(p.n, PSEQ_HITS);
    CHECK(!pseq_toggle(&p, 0, 63) || p.n == PSEQ_HITS);
    static char txt[STORAGE_FILE_MAX];
    const int n = pseq_to_text(&p, txt, sizeof txt);
    printf("    a full pads pattern (%d hits): %d bytes in memory, %d characters of %d on the card\n", PSEQ_HITS, (int)sizeof p, n, STORAGE_FILE_MAX);
    CHECK(n > 0);
    pseq_pattern_t back;
    CHECK(pseq_from_text(&back, txt));
    CHECK_EQ(pseq_hash(&back), pseq_hash(&p));
    CHECK(!pseq_from_text(&back, "pattern 16 0 0 2 0\nstep 0 60\n"));    // the synth sequencer's file is not one
}

TEST(pseq_select_play_turns_the_pad_keys_into_steps) {
    pads_section();
    CHECK(ui_app.pbank.pat != NULL);
    const int pushes = ui_stub_pad_pushes;
    steps_mode();
    CHECK(ui_app.ui.pad_steps);
    CHECK(!ui_app.pads_run);                                             // Select + Play is not Play
    ev_tap(PADK(5));                                                     // pad A1 (selected): its step 6
    ev_tap(PADK(0));
    CHECK_EQ(pat()->n, 2);
    CHECK(pseq_find(pat(), 0, 5) >= 0 && pseq_find(pat(), 0, 0) >= 0);
    CHECK_EQ(ui_stub_pad_pushes, pushes);                                // no pad sounded
    CHECK_EQ(ui_app.ui.pad_cur, 0);                                      // and the selected pad did not move
    CHECK_EQ(ui_app.ui.pseq_step, 0);                                    // the cursor is on the step last pushed
    ev_tap(PADK(5));                                                     // again: off
    CHECK(pseq_find(pat(), 0, 5) < 0);
    // Select + pad: another track (pad A3); its steps are its own
    ev_press(SELECT); ev_tap(PADK(2)); ev_release(SELECT);
    CHECK_EQ(ui_app.ui.pad_cur, 2);
    CHECK(ui_app.ui.pad_steps && !ui_app.ui.pad_edit);
    ev_tap(PADK(4));
    CHECK(pseq_find(pat(), 2, 4) >= 0 && pseq_find(pat(), 0, 4) < 0);
    // another bank: the track of that bank's pad
    ev_tap(BANK_B);
    ev_tap(PADK(1));
    CHECK(pseq_find(pat(), 16 + 2, 1) >= 0);
    CHECK_EQ(ui_stub_pseq.n, pat()->n);                                  // the engine has the pattern as it is
    // the LEDs: a hit bright, a free step dim, the playhead in Play's colour
    ev_idle(3);
    CHECK(same(ui_stub_led[PADK(1)], led_role_rgb(LR_PAD, true)));
    CHECK(same(ui_stub_led[PADK(3)], led_role_rgb(LR_PAD, false)));
    ev_tap(PLAY);
    CHECK(ui_app.pads_run && ui_stub_pseq_run && ui_stub_clock_run);
    ui_stub_pseq_pos = 3;
    ev_idle(3);
    CHECK_EQ(ui_app.ui.pseq_pos, 3);
    CHECK(same(ui_stub_led[PADK(3)], led_role_rgb(LR_PLAY, true)));
    ev_tap(PLAY);
    ui_stub_pseq_pos = -1;
    // Back leaves step mode: the keys are pads again
    ev_tap(BACK);
    CHECK(!ui_app.ui.pad_steps);
    // Shift + Play does the same as Select + Play (a board without the right block, the simulator), and Shift + pad picks the track
    ev_press(CTL_BTN_3); ev_tap(PLAY); ev_release(CTL_BTN_3);
    CHECK(ui_app.ui.pad_steps && !ui_app.pads_run);
    ev_press(CTL_BTN_3); ev_tap(PADK(7)); ev_release(CTL_BTN_3);
    CHECK_EQ(ui_app.ui.pad_cur, 7);
    field_turn(14, 1);                                                   // the Rec field arms the live record (no Rec key needed)
    CHECK(ui_app.prec.armed && ui_app.ui.pseq_rec);
    field_turn(14, 1);
    CHECK(!ui_app.prec.armed);
    ev_press(CTL_BTN_3); ev_tap(PLAY); ev_release(CTL_BTN_3);
    CHECK(!ui_app.ui.pad_steps);
    ev_press(CTL_BTN_3); ev_tap(PADK(0)); ev_release(CTL_BTN_3);
    ev_tap(PADK(5));
    CHECK_EQ(ui_stub_pad_pushes, pushes + 1);
    CHECK(pseq_find(pat(), 16 + 5, 5) < 0);
}

TEST(pseq_steps_screen_edits_a_hit_and_the_pattern) {
    pads_section();
    steps_mode();
    ev_tap(PADK(2));                                                     // a hit on step 3: the cursor is there
    const pseq_hit_t *h = &pat()->hit[pseq_find(pat(), 0, 2)];
    field_turn(0, -3);                                                   // Lvl
    CHECK_EQ(h->level, PSEQ_LEVEL_MAX - 3);
    field_turn(1, -4);                                                   // Prob
    CHECK_EQ(h->prob, 6);
    field_turn(2, 2);                                                    // Rtch
    CHECK_EQ(h->ratchet, 2);
    field_turn(3, 5);                                                    // Len
    CHECK_EQ(h->len, 5);
    ev_tap(CTL_JOY_RIGHT);                                               // the cursor on step 4: no hit there, the values have nothing to move
    CHECK_EQ(ui_app.ui.pseq_step, 3);
    const int n = pat()->n;
    field_turn(0, -3);
    CHECK_EQ(pat()->n, n);
    ev_tap(CTL_JOY_SW);                                                  // the joystick's push: a hit there
    CHECK(pseq_find(pat(), 0, 3) >= 0);
    ev_tap(CTL_JOY_SW);
    CHECK(pseq_find(pat(), 0, 3) < 0);
    ev_tap(CTL_JOY_DOWN);
    CHECK_EQ(ui_app.ui.pseq_step, 7);
    ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_UP);
    CHECK_EQ(ui_app.ui.pseq_step, 2);
    // the pattern's settings: 40 steps, three pages; the keys follow the page of the cursor
    field_turn(5, 16); field_turn(5, 8);                                 // Steps 16 -> 40
    CHECK_EQ(pat()->steps, 40);
    field_turn(6, 1);                                                    // Rate
    CHECK_EQ(pat()->rate, SQR_16T);
    field_turn(7, 2);                                                    // Swing
    CHECK_EQ(pat()->swing, 10);
    field_turn(8, 1);                                                    // Dir
    CHECK_EQ(pat()->dir, SQD_REVERSE);
    ev_press(CTL_BTN_3); ev_turn(CTL_ENC_A, 1); ev_release(CTL_BTN_3);   // Page + (Shift + encoder A): steps 17..32
    CHECK_EQ(ui_app.ui.pseq_step, 18);
    ev_tap(PADK(0));
    CHECK(pseq_find(pat(), 0, 16) >= 0);
    ev_press(CTL_BTN_3); ev_turn(CTL_ENC_A, 1); ev_release(CTL_BTN_3);   // the last page: steps 33..40
    ev_tap(PADK(7));
    CHECK(pseq_find(pat(), 0, 39) >= 0);
    const int before = pat()->n;
    ev_tap(PADK(8));                                                     // step 41: past the pattern's end
    CHECK_EQ(pat()->n, before);
    CHECK_EQ(ui_stub_pseq.steps, 40);
    CHECK_EQ(ui_stub_pseq.dir, SQD_REVERSE);
    // the sequencer switched off: the clock runs without it
    field_turn(9, 1);
    CHECK(!ui_app.pbank.on);
    ev_tap(PLAY);
    CHECK(ui_app.pads_run && !ui_stub_pseq_run && ui_stub_clock_run);
    ev_tap(PLAY);
}

TEST(pseq_patterns_and_chain_as_on_the_synth) {
    pads_section();
    steps_mode();
    pseq_bank_t *b = &ui_app.pbank;
    ev_tap(PADK(0));                                                     // pattern 1: a hit
    field_turn(4, 1);                                                    // Pat: stopped, taken at once
    CHECK_EQ(b->cur, 1);
    CHECK_EQ(pat()->n, 0);
    ev_tap(PADK(3));                                                     // pattern 2: its own hit
    CHECK_EQ(b->pat[0].n, 1); CHECK_EQ(b->pat[1].n, 1);
    CHECK_EQ(ui_stub_pseq.hit[0].step, 3);
    field_turn(4, -1);
    CHECK_EQ(b->cur, 0);
    // running: the picked pattern is handed over as the next one and taken when the engine went on to it
    ev_tap(PLAY);
    field_turn(4, 1);
    CHECK_EQ(b->cur, 0); CHECK_EQ(b->next, 1);
    CHECK_EQ(ui_stub_pseq_queued, 1);
    CHECK_EQ(ui_stub_pseq_next.hit[0].step, 3);
    ui_stub_pseq_take(); ev_idle(1);
    CHECK_EQ(b->cur, 1); CHECK_EQ(b->next, -1);
    CHECK_EQ(ui_stub_pseq.hit[0].step, 3);
    CHECK_EQ(ui_stub_pseq_queued, -1);
    ev_tap(PLAY);
    // the chain 2 2 1: started by Play at its first entry, each entry queued when the one before starts
    field_turn(11, 2);                                                   // CLen 3 (new entries: the pattern shown, 2)
    field_turn(12, 2); field_turn(13, -1);                               // Slot 3: Is 1
    CHECK_EQ(b->chain[0], 0); CHECK_EQ(b->chain[1], 1); CHECK_EQ(b->chain[2], 0);
    field_turn(12, 1); field_turn(13, 1);                                // Slot 1: Is 2
    CHECK_EQ(b->chain[0], 1);
    field_turn(10, 1);                                                   // Chain On
    field_turn(4, -1);                                                   // (stopped on pattern 1)
    ev_tap(PLAY);
    CHECK_EQ(b->cur, 1); CHECK_EQ(b->chain_pos, 0);
    CHECK_EQ(ui_stub_pseq_queued, 1);
    ui_stub_pseq_take(); ev_idle(1);
    CHECK_EQ(b->cur, 1); CHECK_EQ(b->chain_pos, 1);
    CHECK_EQ(ui_stub_pseq_queued, 0);
    ui_stub_pseq_take(); ev_idle(1);
    CHECK_EQ(b->cur, 0); CHECK_EQ(b->chain_pos, 2);
    CHECK_EQ(ui_stub_pseq.hit[0].step, 0);
    ev_tap(PLAY);
    CHECK_EQ(ui_stub_pseq_queued, -1);
}

TEST(pseq_live_record_writes_the_pads_played_to_the_nearest_step) {
    pads_section();
    ui_app.pads.pad[1].mode = PAD_GATE;
    ev_press(SELECT); ev_tap(REC); ev_release(SELECT);                   // Select + Rec: the pattern's record, not the audio capture
    CHECK(ui_app.prec.armed && ui_app.ui.pseq_rec);
    CHECK_EQ(ui_stub_cap_state, CAPTURE_ST_IDLE);
    ev_tap(PADK(0));                                                     // the clock does not run: a pad only plays
    CHECK_EQ(pat()->n, 0);
    ev_tap(PLAY);
    const int pushes = ui_stub_pad_pushes;
    ui_stub_pseq_pos = 4; ui_stub_pseq_frac = 40; ui_stub_pseq_count = 4;
    ev_tap(PADK(0));                                                     // early in step 5: step 5
    ev_idle(1);
    CHECK_EQ(ui_stub_pad_pushes, pushes + 1);                            // it sounded under the finger
    CHECK(pseq_find(pat(), 0, 4) >= 0);
    ui_stub_pseq_pos = 8; ui_stub_pseq_frac = 220; ui_stub_pseq_count = 8;
    ev_press(PADK(1));                                                   // the gate pad, late in step 9: step 10 ...
    ev_idle(2);
    CHECK(pseq_find(pat(), 1, 9) < 0);                                   // ... written only once the playhead is there
    ui_stub_pseq_pos = 12; ui_stub_pseq_frac = 30; ui_stub_pseq_count = 12;
    ev_release(PADK(1));
    ev_idle(1);
    const int at = pseq_find(pat(), 1, 9);
    CHECK(at >= 0);
    if (at >= 0) CHECK_EQ(pat()->hit[at].len, 3 - 1);                    // held from step 10 into step 13: three steps
    // late on the last step: the first one
    ui_stub_pseq_pos = 15; ui_stub_pseq_frac = 250; ui_stub_pseq_count = 31;
    ev_tap(PADK(2));
    ui_stub_pseq_pos = 0; ui_stub_pseq_frac = 5; ui_stub_pseq_count = 32;
    ev_idle(1);
    CHECK(pseq_find(pat(), 2, 0) >= 0);
    ev_idle(1);                                                          // (the engine gets it at the next step)
    CHECK_EQ(ui_stub_pseq.n, pat()->n);
    ev_press(SELECT); ev_tap(REC); ev_release(SELECT);
    CHECK(!ui_app.prec.armed);
    const int n = pat()->n;
    ev_tap(PADK(3));
    ev_idle(1);
    CHECK_EQ(pat()->n, n);
    ev_tap(REC);                                                         // Rec alone is still the capture
    CHECK(ui_stub_cap_state != CAPTURE_ST_IDLE);
    ev_tap(REC);
    ev_tap(PLAY);
    ui_stub_pseq_pos = -1;
}

static char buf[STORAGE_FILE_MAX];

TEST(pseq_patterns_are_saved_by_themselves_and_come_back) {
    ui_stub_ramcard(true);
    pads_section();
    CHECK(storage_read("system/config/padseq.cfg", buf, sizeof buf) < 0);
    steps_mode();
    ev_tap(PADK(0)); ev_tap(PADK(6));
    field_turn(2, 1);                                                    // step 7: a ratchet of 2
    field_turn(4, 2);                                                    // pattern 3
    ev_tap(PADK(9));
    field_turn(5, -4);
    ev_idle(20);
    CHECK(storage_read("system/config/padpat1.cfg", buf, sizeof buf) < 0);    // not at once
    ev_idle(900);
    CHECK(storage_read("system/config/padseq.cfg", buf, sizeof buf) > 0 && strstr(buf, "padseq 1 3 0\n") != NULL);
    CHECK(storage_read("system/config/padpat1.cfg", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "hit 0 0\n") != NULL && strstr(buf, "hit 0 6 16 100 2\n") != NULL);
    CHECK(storage_read("system/config/padpat3.cfg", buf, sizeof buf) > 0 && strstr(buf, "padpattern 12 0 2 0\n") != NULL);
    CHECK(storage_read("system/config/padpat2.cfg", buf, sizeof buf) < 0);
    CHECK(!app_pseq_unsaved(&ui_app));
    ui_fresh();                                                          // power off, power on
    CHECK_EQ(ui_app.pbank.cur, 2);
    CHECK_EQ(pat()->steps, 12);
    CHECK(pseq_find(pat(), 0, 9) >= 0);
    CHECK_EQ(ui_app.pbank.pat[0].n, 2);
    CHECK(pseq_find(&ui_app.pbank.pat[0], 0, 6) >= 0 && ui_app.pbank.pat[0].hit[pseq_find(&ui_app.pbank.pat[0], 0, 6)].ratchet == 1);
    CHECK(!app_pseq_unsaved(&ui_app));
    ui_stub_ramcard(false);
}
