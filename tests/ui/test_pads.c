// The Pads section (core/pads.h, scr_pads.c, app.c; ADR-041 decisions 14-26): Shift + Menu, the Pads key layout, pad keys with Select /
// Mute / Shift / banks, the grid and the pad editor, the column knobs, pads.cfg, the LEDs.
#include "ui_test.h"
#include "core/led_roles.h"
#include <math.h>

extern int ui_stub_builds, ui_stub_pads_builds, ui_stub_pad_sets, ui_stub_pad_pushes, ui_stub_pad_releases, ui_stub_pads_stops, ui_stub_last_pad;
extern led_color_t ui_stub_led[CTL_COUNT];

#define SHIFT CTL_BTN_3
#define MENU  CTL_BTN_1
#define BACK  CTL_BTN_2
#define PAD_KEY(k)  CTL_KEY(1 + (k) / 4, (k) % 4)                  // the left 4 x 4 block, pad 1 top left
#define BANK_KEY(b) CTL_KEY(1, 4 + (b))
#define SELECT_KEY  CTL_KEY(2, 4)
#define MUTE_KEY    CTL_KEY(2, 5)
#define STOP_KEY    CTL_KEY(2, 7)

static void shift_menu(void) { ev_press(SHIFT); ev_tap(MENU); ev_release(SHIFT); }
static bool same_rgb(led_color_t a, led_color_t b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

TEST(pads_shift_menu_switches_the_section_and_the_keys) {
    ui_fresh();
    const int builds = ui_stub_builds, pads_builds = ui_stub_pads_builds;
    CHECK_EQ(keymap_get(1 * KEY_COLS).act, ACT_NOTE);
    shift_menu();
    CHECK_EQ(ui_app.section, SECTION_PADS);
    CHECK_EQ(ui_stub_pads_builds, pads_builds + 1);
    CHECK(!ui_app.ui.in_rack);                                           // Shift + Menu does not open the menu
    CHECK_STR(ui_app.popup.info.title, "PADS");
    CHECK(keymap_get(1 * KEY_COLS).act == ACT_PAD && keymap_get(1 * KEY_COLS).arg == 0);
    CHECK(keymap_get(4 * KEY_COLS + 3).act == ACT_PAD && keymap_get(4 * KEY_COLS + 3).arg == 15);
    CHECK_EQ(keymap_get(1 * KEY_COLS + 4).act, ACT_PAD_BANK);
    CHECK_EQ(keymap_get(0).act, ACT_SHIFT);                              // the function row stays
    ev_tap(MENU);                                                        // Menu alone does nothing here (settings: phase 6)
    CHECK(!ui_app.ui.in_rack);
    shift_menu();
    CHECK_EQ(ui_app.section, SECTION_SYNTH);
    CHECK_EQ(ui_stub_builds, builds + 1);                                // the synth comes back
    CHECK_EQ(keymap_get(1 * KEY_COLS).act, ACT_NOTE);
    CHECK_EQ(keymap_layout(), 0);                                        // the chosen layout was kept
}

TEST(pads_keys_play_select_mute_and_banks) {
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");
    shift_menu();
    const int pushes = ui_stub_pad_pushes, releases = ui_stub_pad_releases, sets = ui_stub_pad_sets;
    ev_press(PAD_KEY(1));
    CHECK_EQ(ui_stub_pad_pushes, pushes + 1);
    CHECK_EQ(ui_stub_last_pad, 1);
    CHECK_EQ(ui_app.ui.pad_cur, 1);
    ev_release(PAD_KEY(1));
    CHECK_EQ(ui_stub_pad_releases, releases + 1);
    ev_press(SHIFT); ev_tap(PAD_KEY(2)); ev_release(SHIFT);              // Shift + pad: selects, no sound
    CHECK_EQ(ui_stub_pad_pushes, pushes + 1);
    CHECK_EQ(ui_app.ui.pad_cur, 2);
    CHECK(!ui_app.ui.pad_edit);
    ev_press(SELECT_KEY); ev_tap(PAD_KEY(3)); ev_release(SELECT_KEY);    // Select + pad: its editor, no sound
    CHECK_EQ(ui_stub_pad_pushes, pushes + 1);
    CHECK_EQ(ui_app.ui.pad_cur, 3);
    CHECK(ui_app.ui.pad_edit);
    ev_tap(BACK);
    ev_press(MUTE_KEY); ev_tap(PAD_KEY(0)); ev_release(MUTE_KEY);        // Mute + pad: toggles its mute, sent to the engine
    CHECK(ui_app.pads.pad[0].mute);
    CHECK_EQ(ui_stub_pad_sets, sets + 1);
    CHECK_EQ(ui_stub_pad_pushes, pushes + 1);
    ev_press(PAD_KEY(0));                                                // held across a bank change: its release still reaches pad 0
    ev_tap(BANK_KEY(1));
    CHECK_EQ(ui_app.ui.pad_bank, 1);
    ev_release(PAD_KEY(0));
    CHECK_EQ(ui_stub_pad_releases, releases + 2);
    CHECK(!(audio_pads_playing() & 1));
    ev_tap(PAD_KEY(0));                                                  // bank B: pad 17
    CHECK_EQ(ui_stub_last_pad, 16);
    const int stops = ui_stub_pads_stops;
    ev_tap(STOP_KEY);
    ev_press(SHIFT); ev_tap(BACK); ev_release(SHIFT);                    // Shift + Back: Stop all too (the 4-column boards)
    CHECK_EQ(ui_stub_pads_stops, stops + 2);
}

TEST(pads_grid_and_editor) {
    ui_fresh();
    shift_menu();
    ev_turn(CTL_ENC_A, 5);                                               // encoder A walks the pads of the bank
    CHECK_EQ(ui_app.ui.pad_cur, 5);
    ev_turn(CTL_ENC_B, 1);                                               // encoder B changes the bank
    CHECK_EQ(ui_app.ui.pad_bank, 1);
    ev_turn(CTL_ENC_B, -2);
    CHECK_EQ(ui_app.ui.pad_bank, 3);
    ev_tap(CTL_JOY_SW);                                                  // a push opens the pad's editor on its sample row
    CHECK(ui_app.ui.pad_edit);
    CHECK_EQ(ui_app.ui.pad_row, 1);
    const int sets = ui_stub_pad_sets, pad = 3 * PADS_PER_BANK + 5;
    ev_turn(CTL_ENC_B, 1);                                               // the library's first file
    CHECK_STR(ui_app.pads.pad[pad].file, "demo0");
    CHECK_EQ(ui_stub_pad_sets, sets + 1);
    ev_turn(CTL_ENC_B, -1);                                              // back to none
    CHECK_STR(ui_app.pads.pad[pad].file, "");
    ev_turn(CTL_ENC_B, 2);                                               // demo1 has loop points: they are loaded
    CHECK_STR(ui_app.pads.pad[pad].file, "demo1");
    CHECK_EQ(ui_app.pads.pad[pad].loop, PAD_LOOP_FILE);
    ev_turn(CTL_ENC_B, 1);                                               // demo2 has none: File goes
    CHECK_STR(ui_app.pads.pad[pad].file, "demo2");
    CHECK_EQ(ui_app.pads.pad[pad].loop, PAD_LOOP_OFF);
    ev_turn(CTL_ENC_A, 4);                                               // Loop: Off -> Trim -> (no File here) Off
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.pads.pad[pad].loop, PAD_LOOP_TRIM);
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.pads.pad[pad].loop, PAD_LOOP_OFF);
    ev_turn(CTL_ENC_A, -4);
    ev_turn(CTL_ENC_A, 1);                                               // Mode
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.pads.pad[pad].mode, PAD_GATE);
    CHECK_EQ(ui_app.ui.row, 0);                                          // the synth screens' focus was not touched
    ev_turn(CTL_ENC_A, -2);                                              // the header: encoder B steps the page (PLAY, TONE, CHOP)
    CHECK_EQ(ui_app.ui.pad_row, 0);
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.ui.pad_page, 1);
    CHECK_EQ(ui_app.ui.pad_cur, 5);                                      // the pad stays (a pad key changes it)
    ev_tap(BACK);                                                        // Back: TONE -> PLAY -> the grid
    CHECK(ui_app.ui.pad_edit && ui_app.ui.pad_page == 0);
    ev_tap(BACK);
    CHECK(!ui_app.ui.pad_edit);
    ev_tap(MENU);
    CHECK_EQ(ui_app.section, SECTION_PADS);
}

TEST(pads_knobs_edit_the_selected_pad) {
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");
    shift_menu();
    const int sets = ui_stub_pad_sets;
    ev_knob(CTL_COL_KNOB_0, INPUT_VALUE_MAX / 2);                         // Start: half of demo0's 96000 frames
    CHECK(ui_app.pads.pad[0].start >= 47500 && ui_app.pads.pad[0].start <= 48500);
    ev_knob(CTL_COL_KNOB_1, 0);                                          // End: never before Start
    CHECK_EQ(pad_end(&ui_app.pads.pad[0], 96000), ui_app.pads.pad[0].start + 2);
    ev_knob(CTL_COL_KNOB_2, INPUT_VALUE_MAX);                            // Tune
    CHECK_EQ(ui_app.pads.pad[0].tune, 24);
    ev_knob(CTL_COL_KNOB_3, 0);                                          // Cut
    CHECK_EQ(ui_app.pads.pad[0].cut, 0);
    CHECK_EQ(ui_stub_pad_sets, sets + 4);
    CHECK_STR(ui_app.popup.info.title, "A1 Cut");
}

TEST(pads_cfg_text_and_autosave) {
    pads_t a, b;
    pads_init(&a);
    pad_t *p = &a.pad[PADS_PER_BANK + 2];                                // B3
    snprintf(p->file, PAD_NAME_MAX, "kick 01");
    p->mode = PAD_TOGGLE; p->loop = PAD_LOOP_TRIM; p->start = 1200; p->end = 90000; p->lstart = 4800; p->xfade_ms = 40; p->xpost = 1; p->from = 1; p->tail_ms = 50; p->tune = -2; p->fine = 10;
    p->level = 70; p->pan = -10; p->cut = 60; p->res = 20; p->choke = 3; p->mute = 1;
    snprintf(a.pad[63].file, PAD_NAME_MAX, "snare");
    static char txt[STORAGE_FILE_MAX];
    pads_to_text(&a, txt, sizeof txt);
    CHECK(strstr(txt, "pad B3 mode toggle loop 1 start 1200 end 90000 lstart 4800 xfade 40 xpos post from loop tail 50 tune -2 fine 10 level 70 pan -10 cut 60 res 20 choke 3 mute 1 file kick 01\n") != NULL);
    CHECK(strstr(txt, "pad D16 file snare\n") != NULL);
    CHECK(strstr(txt, "pad A1") == NULL);                                // an empty default pad has no line
    pads_init(&b);
    CHECK(pads_from_text(&b, txt));
    CHECK(!memcmp(&a, &b, sizeof a));
    CHECK(!pads_from_text(&b, "layout user\n"));                         // not a pads file: kept

    ui_stub_ramcard(true);
    ui_fresh();
    shift_menu();
    ev_tap(CTL_JOY_SW);
    ev_turn(CTL_ENC_B, 1);                                               // A1 takes demo0
    ev_idle(400);                                                        // a few seconds of app steps: pads.cfg is written
    pads_init(&b);
    CHECK(pads_load(&b));
    CHECK_STR(b.pad[0].file, "demo0");
    ui_fresh();                                                          // power-on: the pads come back from the card
    CHECK_STR(ui_app.pads.pad[0].file, "demo0");
}

TEST(pads_leds_show_empty_loaded_playing_and_bank) {
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");
    snprintf(ui_app.pads.pad[1].file, PAD_NAME_MAX, "demo1");
    ui_app.pads.pad[1].mute = 1;
    shift_menu();
    ev_press(PAD_KEY(0));                                                // a key event repaints at once
    ev_idle(1);
    CHECK(same_rgb(ui_stub_led[PAD_KEY(0)], led_role_rgb(LR_PAD, true)));       // sounding
    CHECK(same_rgb(ui_stub_led[PAD_KEY(1)], led_role_rgb(LR_BACK, false)));     // muted
    CHECK(same_rgb(ui_stub_led[PAD_KEY(2)], (led_color_t){0, 0, 0}));          // empty
    CHECK(same_rgb(ui_stub_led[BANK_KEY(0)], led_role_rgb(LR_BANK, true)));     // the bank shown
    CHECK(same_rgb(ui_stub_led[BANK_KEY(1)], led_role_rgb(LR_BANK, false)));
    ev_release(PAD_KEY(0));
    ev_idle(1);
    CHECK(same_rgb(ui_stub_led[PAD_KEY(0)], led_role_rgb(LR_PAD, false)));      // loaded, silent
}

extern int ui_stub_clock_bpm, ui_stub_clock_run, ui_stub_metronome;
#define TAP_KEY      CTL_KEY(3, 4)
#define CLICK_KEY    CTL_KEY(3, 5)
#define BPM_DN_KEY   CTL_KEY(3, 6)
#define BPM_UP_KEY   CTL_KEY(3, 7)

TEST(pads_play_runs_the_shared_clock) {
    ui_fresh();
    shift_menu();
    ev_idle(1);
    CHECK_EQ(ui_stub_clock_run, 0);
    ev_tap(CTL_PLAY);                                                    // Play in the Pads section: the clock, not the sequencer
    ev_idle(1);
    CHECK_EQ(ui_stub_clock_run, 1);
    CHECK(!ui_app.seq.running);
    CHECK_EQ(ui_stub_clock_bpm, ui_app.seq.bpm);                         // one tempo: the sequencer's
    ev_tap(CTL_PLAY);
    ev_idle(1);
    CHECK_EQ(ui_stub_clock_run, 0);
    ev_tap(CTL_PLAY);
    shift_menu();                                                        // leaving the section stops it
    ev_idle(1);
    CHECK_EQ(ui_stub_clock_run, 0);
    ev_tap(CTL_PLAY);                                                    // the synth: the sequencer, and the clock with it
    ev_idle(1);
    CHECK(ui_app.seq.running);
    CHECK_EQ(ui_stub_clock_run, 1);
}

TEST(pads_tempo_keys_tap_click_and_bindings) {
    ui_fresh();
    shift_menu();
    const int bpm = ui_app.seq.bpm;
    ev_tap(BPM_UP_KEY);
    CHECK_EQ(ui_app.seq.bpm, bpm + 1);
    ev_tap(BPM_DN_KEY); ev_tap(BPM_DN_KEY);
    CHECK_EQ(ui_app.seq.bpm, bpm - 1);
    CHECK(strcmp(ui_app.popup.info.title, "TEMPO") != 0);                // no popup: the Pads header shows the BPM
    ev_tap(CLICK_KEY);
    CHECK_EQ(ui_stub_metronome, 1);
    ev_tap(CLICK_KEY);
    CHECK_EQ(ui_stub_metronome, 0);
    // Shift + a pad bound to BPM +5 (a layer entry, MODIFIERS tab): the tempo moves, the pad does not play
    modifiers_set(MODL_SHIFT, PAD_KEY(0), (mod_entry_t){ME_ACTION, ACT_TEMPO, 5, {MACRO_NONE, 0, 0}});
    const int pushes = ui_stub_pad_pushes;
    ev_press(SHIFT); ev_tap(PAD_KEY(0)); ev_release(SHIFT);
    CHECK_EQ(ui_app.seq.bpm, bpm + 4);
    CHECK_EQ(ui_stub_pad_pushes, pushes);
    // an encoder with the "Tempo" step function (Mod + encoder B, say)
    CHECK(modifiers_fn_index(CTL_ENC_B, (mod_entry_t){ME_ACTION, ACT_TEMPO, 1, {MACRO_NONE, 0, 0}}) > 0);
    modifiers_set(MODL_SHIFT, CTL_ENC_B, (mod_entry_t){ME_ACTION, ACT_TEMPO, 1, {MACRO_NONE, 0, 0}});
    ev_press(SHIFT); ev_turn(CTL_ENC_B, -3); ev_release(SHIFT);
    CHECK_EQ(ui_app.seq.bpm, bpm + 1);
    ev_tap(TAP_KEY);                                                     // one tap: nothing yet
    CHECK_EQ(ui_app.seq.bpm, bpm + 1);
}

TEST(pads_tap_tempo_averages_the_last_taps) {
    seq_tap_t t;
    memset(&t, 0, sizeof t);
    CHECK_EQ(seq_tap(&t, 1000), 0);
    CHECK_EQ(seq_tap(&t, 1500), 120);
    CHECK_EQ(seq_tap(&t, 2000), 120);
    CHECK_EQ(seq_tap(&t, 2600), 113);                                    // 500 500 600 ms: 533 on average
    CHECK_EQ(seq_tap(&t, 6000), 0);                                      // a long pause starts again
    CHECK_EQ(seq_tap(&t, 6250), 240);                                    // clamped
}

TEST(pads_grid_setting_in_the_editor_and_the_file) {
    ui_fresh();
    shift_menu();
    ev_tap(CTL_JOY_SW);
    ev_turn(CTL_ENC_A, 6);                                               // Grid: Mode Strt End Loop Tail Grid (the loop rows are hidden)
    ev_turn(CTL_ENC_B, 3);                                               // Beat, 2 beats, a bar
    CHECK_EQ(ui_app.pads.pad[0].grid, PAD_GRID_BAR);
    static char txt[STORAGE_FILE_MAX];
    pads_to_text(&ui_app.pads, txt, sizeof txt);
    CHECK(strstr(txt, "pad A1 grid bar") != NULL);
    pads_t b;
    pads_init(&b);
    CHECK(pads_from_text(&b, txt));
    CHECK_EQ(b.pad[0].grid, PAD_GRID_BAR);
}

TEST(pads_loop_rows_show_while_the_pad_loops) {
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");
    shift_menu();
    ev_tap(CTL_JOY_SW);                                                  // the editor of A1, on Smp
    ev_turn(CTL_ENC_A, 4);                                               // Loop
    ev_turn(CTL_ENC_B, 1);                                               // Trim: LStr and XFad appear
    CHECK_EQ(ui_app.pads.pad[0].loop, PAD_LOOP_TRIM);
    ev_turn(CTL_ENC_A, 1);
    CHECK_EQ(ui_app.ui.pad_row, 6);                                      // LStr (element 6)
    ev_turn(CTL_ENC_B, 3);                                               // 3 steps of 1/200 of 96000 frames
    CHECK_EQ(pad_lstart(&ui_app.pads.pad[0], 96000), 3 * 480);
    ev_turn(CTL_ENC_A, 1);                                               // XFad
    ev_turn(CTL_ENC_B, 2);
    CHECK_EQ(ui_app.pads.pad[0].xfade_ms, 10);
    ev_turn(CTL_ENC_A, 1);                                               // XPos (shown once there is a crossfade)
    CHECK_EQ(ui_app.ui.pad_row, 8);
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.pads.pad[0].xpost, 1);
    ev_turn(CTL_ENC_A, 2);                                               // Tail, Grid
    ev_turn(CTL_ENC_B, 1);                                               // Beat: From appears in the right column
    CHECK_EQ(ui_app.pads.pad[0].grid, PAD_GRID_BEAT);
    ev_turn(CTL_ENC_A, 2);                                               // Sync, From
    CHECK_EQ(ui_app.ui.pad_row, pads_ui_row(0, PADP_FROM));
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.pads.pad[0].from, 1);
}


TEST(pads_chop_equal_grid_transient_manual_and_apply) {
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");               // 96000 frames at 48 kHz
    ui_app.pads.pad[0].level = 55;
    shift_menu();
    ev_tap(CTL_JOY_SW);                                                  // the editor of A1
    ev_press(SHIFT); ev_turn(CTL_ENC_A, 2); ev_release(SHIFT);           // Shift + encoder A = Page: TONE, CHOP
    CHECK_EQ(ui_app.ui.pad_page, 2);
    CHECK_EQ(ui_app.ui.chop.mode, CHOP_EQUAL);
    CHECK_EQ(ui_app.ui.chop.n_cuts, 3);                                  // Equal 4: 3 cuts
    CHECK_EQ(ui_app.ui.chop.cut[0], 24000);
    ui_app.ui.pad_row = 2;                                               // N: 8 slices
    ev_turn(CTL_ENC_B, 4);
    CHECK_EQ(ui_app.ui.chop.n_cuts, 7);
    ui_app.ui.pad_row = 1;                                               // Mode: Transients (the stub's scan: 2 strong, 2 faint)
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.ui.chop.mode, CHOP_TRANSIENT);
    CHECK_EQ(ui_app.ui.chop.n_cuts, 2);
    CHECK(ui_app.ui.chop.cut[0] == 12000 && ui_app.ui.chop.cut[1] == 48000);
    ui_app.ui.pad_row = 2;                                               // more sensitive: the faint ones too
    ev_turn(CTL_ENC_B, 10);
    CHECK_EQ(ui_app.ui.chop.n_cuts, 4);
    ui_app.ui.pad_row = 1;                                               // Grid at 120 BPM, 1/4: a cut every 24000 frames
    ev_turn(CTL_ENC_B, 1);
    ui_app.seq.bpm = 120;
    ui_app.ui.pad_row = 2;
    ev_turn(CTL_ENC_B, 0);
    ev_turn(CTL_ENC_B, 1); ev_turn(CTL_ENC_B, -1);
    CHECK_EQ(ui_app.ui.chop.mode, CHOP_GRID);
    CHECK_EQ(ui_app.ui.chop.n_cuts, 3);
    CHECK_EQ(ui_app.ui.chop.cut[1], 48000);
    ui_app.ui.pad_row = 6;                                               // Del: becomes Manual, the selected (first) cut goes
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui_app.ui.chop.mode, CHOP_MANUAL);
    CHECK_EQ(ui_app.ui.chop.n_cuts, 2);
    ui_app.ui.pad_row = 3;                                               // Pos: move it 2 steps of 1/400 of the file to the right
    ev_turn(CTL_ENC_B, 2);
    CHECK_EQ(ui_app.ui.chop.cut[0], 48000 + 480);
    ui_app.ui.pad_row = 7;                                               // Chop to pads: asks, then A2.. get the slices
    ev_tap(CTL_JOY_SW);
    CHECK(popup_modal(&ui_app.popup));
    const int sets = ui_stub_pad_sets;
    ev_tap(CTL_JOY_LEFT);                                                // the joystick moves to Yes (No is the default) ...
    ev_tap(CTL_JOY_SW);                                                  // ... and answers
    CHECK(!popup_modal(&ui_app.popup));
    CHECK_EQ(ui_stub_pad_sets, sets + 3);
    const pad_t *s1 = &ui_app.pads.pad[1], *s3 = &ui_app.pads.pad[3];
    CHECK_STR(s1->file, "demo0");
    CHECK(s1->start == 0 && s1->end == 48480);
    CHECK(s3->start == 72000 && s3->end == 0);                           // the last slice runs to the file's end
    CHECK_EQ(s1->level, 55);                                             // the source pad's settings
}

TEST(pads_next_setting_and_file) {
    ui_fresh();
    shift_menu();
    ev_tap(CTL_JOY_SW);
    ui_app.ui.pad_row = pads_ui_row(0, PADP_NEXT);
    ev_turn(CTL_ENC_B, 4);
    CHECK_EQ(ui_app.pads.pad[0].next, PAD_NEXT_RIGHT);
    static char txt[STORAGE_FILE_MAX];
    pads_to_text(&ui_app.pads, txt, sizeof txt);
    CHECK(strstr(txt, "pad A1 next right") != NULL);
}

TEST(pads_chop_page_back_path_and_stop_popup) {
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");
    shift_menu();
    ev_tap(CTL_JOY_SW);                                                  // the pad page
    ui_app.ui.pad_row = 0;                                               // the header: PLAY -> TONE -> CHOP
    ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_RIGHT);
    CHECK_EQ(ui_app.ui.pad_page, 2);
    CHECK_EQ(ui_app.ui.chop.n_cuts, 3);
    ev_tap(BACK);                                                        // CHOP -> the pad page
    CHECK(ui_app.ui.pad_edit && ui_app.ui.pad_page == 0);
    ev_tap(BACK);                                                        // -> the grid
    CHECK(!ui_app.ui.pad_edit);
    ev_tap(STOP_KEY);                                                    // the Stop key says so, like Shift + Back
    CHECK_STR(ui_app.popup.info.text, "Stop all");
}


TEST(pads_snap_moves_the_trim_to_beats_sixteenths_attacks_and_zeros) {
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");               // 96000 frames at 48 kHz
    ui_app.seq.bpm = 120;                                                // a beat = 24000 frames
    shift_menu();
    ev_tap(CTL_JOY_SW);
    pad_t *p = &ui_app.pads.pad[0];
    const int strt = pads_ui_row(0, PADP_START), snap = pads_ui_row(0, PADP_SNAP), end = pads_ui_row(0, PADP_END);
    ui_app.ui.pad_row = snap; ev_turn(CTL_ENC_B, 1);                      // Beat
    ui_app.ui.pad_row = strt; ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(p->start, 24000);
    ui_app.ui.pad_row = end; ev_turn(CTL_ENC_B, -1);                      // End counts from Start: 72000 (2 beats), then 48000
    CHECK_EQ(pad_end(p, 96000), 72000);
    ev_turn(CTL_ENC_B, -1);
    CHECK_EQ(pad_end(p, 96000), 48000);
    ui_app.ui.pad_row = snap; ev_turn(CTL_ENC_B, 1);                      // 1/16 of the sample: 6000 frames
    ui_app.ui.pad_row = strt; ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(p->start, 30000);
    ui_app.ui.pad_row = snap; ev_turn(CTL_ENC_B, 1);                      // Attack (the stub's onsets: 12000 30000 48000 72000)
    ui_app.ui.pad_row = strt; ev_turn(CTL_ENC_B, -1);
    CHECK_EQ(p->start, 12000);
    ui_app.ui.pad_row = snap; ev_turn(CTL_ENC_B, 1);                      // Zero (the stub's: every 1000 frames)
    ui_app.ui.pad_row = strt; ev_turn(CTL_ENC_B, 1);                      // a step of 480, then the next crossing
    CHECK_EQ(p->start, 13000);
    ev_knob(CTL_COL_KNOB_0, 5);                                          // a knob: the nearest crossing to where it points
    CHECK_EQ(p->start % 1000, 0);
}

TEST(pads_sync_and_grid_units_in_the_file) {
    pads_t a, b;
    pads_init(&a);
    a.pad[0].sync = 1; a.pad[0].grid = PAD_GRID_3BAR; a.pad[0].snap = PAD_SNAP_ZERO;
    static char txt[STORAGE_FILE_MAX];
    pads_to_text(&a, txt, sizeof txt);
    CHECK(strstr(txt, " grid 3bar") && strstr(txt, " sync 1") && strstr(txt, " snap zero"));
    pads_init(&b);
    CHECK(pads_from_text(&b, txt));
    CHECK(!memcmp(&a, &b, sizeof a));
    CHECK_EQ(pad_grid_beats(PAD_GRID_BEAT), 1);
    CHECK_EQ(pad_grid_beats(PAD_GRID_2BEAT), 2);
    CHECK_EQ(pad_grid_beats(PAD_GRID_BAR), 4);
    CHECK_EQ(pad_grid_beats(PAD_GRID_8BAR), 32);
}


#include "core/history.h"
extern int ui_stub_renders, ui_stub_render_op, ui_stub_render_amount, ui_stub_render_amount2, ui_stub_removed;
extern char ui_stub_render_src[24], ui_stub_removed_last[64];

TEST(history_tree_names_undo_redo_and_trim) {
    history_t h;
    history_init(&h);
    char n[HISTORY_NAME], r[HISTORY_NAME];
    history_new_name(&h, "kick", n);
    CHECK_STR(n, "kick~1");
    CHECK(history_add(&h, "kick~1", "kick", "Norm -0.3dB"));
    CHECK(history_add(&h, "kick~2", "kick~1", "Reverse"));
    CHECK(history_add(&h, "kick~3", "kick~1", "Gain +6.0dB"));         // a branch from ~1
    history_new_name(&h, "kick", n);
    CHECK_STR(n, "kick~4");
    history_root(&h, "kick~3", r);
    CHECK_STR(r, "kick");
    history_parent(&h, "kick~2", n);
    CHECK_STR(n, "kick~1");
    history_newest_child(&h, "kick~1", n);                               // Redo from ~1: the newest branch
    CHECK_STR(n, "kick~3");
    history_newest_child(&h, "kick~3", n);
    CHECK_STR(n, "");
    static char txt[STORAGE_FILE_MAX];
    history_to_text(&h, txt, sizeof txt);
    history_t b;
    CHECK(history_from_text(&b, txt));
    CHECK(b.n == 3 && !strcmp(b.v[1].what, "Reverse") && !strcmp(b.v[2].parent, "kick~1"));
    // the trimming: 12 versions in a chain, a pad uses ~2: the last 8 and ~2 stay, the others go and ~2's child re-links
    history_init(&h);
    char prev[HISTORY_NAME] = "pad";
    for (int k = 1; k <= 12; k++) { history_new_name(&h, "pad", n); history_add(&h, n, prev, "Gain"); snprintf(prev, sizeof prev, "%s", n); }
    const char *used[1] = {"pad~2"};
    char gone[HISTORY_MAX][HISTORY_NAME];
    const int g = history_trim(&h, "pad", used, 1, gone, HISTORY_MAX);
    CHECK_EQ(g, 3);                                                      // ~1, ~3, ~4 (12 - 8 = 4 old ones, ~2 is used)
    CHECK(!strcmp(gone[0], "pad~1") && !strcmp(gone[1], "pad~3") && !strcmp(gone[2], "pad~4"));
    history_parent(&h, "pad~2", n);
    CHECK_STR(n, "pad");                                                 // its parent ~1 went: now from the root
    history_parent(&h, "pad~5", n);
    CHECK_STR(n, "pad~2");                                               // ~4 and ~3 went: the nearest kept ancestor
    CHECK(!history_find(&h, "pad~1") && history_find(&h, "pad~12"));
}

TEST(pads_edit_page_renders_a_version_and_undo_goes_back) {
    ui_stub_ramcard(true);
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");
    shift_menu();
    ev_tap(CTL_JOY_SW);
    ui_app.ui.pad_row = 0;
    ev_tap(CTL_JOY_LEFT);                                                // the header: PLAY <- EDIT
    CHECK_EQ(ui_app.ui.pad_page, 3);
    const int renders = ui_stub_renders;
    ui_app.ui.pad_row = 4;                                               // Apply (Normalize to -0.3 dB by default)
    ev_tap(CTL_JOY_SW);
    ev_idle(3);                                                          // rendered, listed, adopted
    CHECK_EQ(ui_stub_renders, renders + 1);
    CHECK_EQ(ui_stub_render_op, 0);
    CHECK_EQ(ui_stub_render_amount, -3);
    CHECK_STR(ui_stub_render_src, "demo0");
    CHECK_STR(ui_app.pads.pad[0].file, "demo0~1");                       // the pad plays the new version
    CHECK_EQ(ui_app.history.n, 1);
    CHECK_STR(ui_app.history.v[0].what, "Norm -0.3dB");
    CHECK_EQ(ui_app.ui.render_pct, -1);
    ui_app.ui.pad_row = 1; ev_turn(CTL_ENC_B, 6);                         // Op: Reverse
    CHECK_STR(pads_ui_op_name(ui_app.ui.edit_op), "Reverse");
    ui_app.pads.pad[0].start = 5000;
    ui_app.ui.pad_row = 4; ev_tap(CTL_JOY_SW); ev_idle(3);
    CHECK_STR(ui_app.pads.pad[0].file, "demo0~2");
    CHECK_EQ(ui_app.pads.pad[0].start, 0);                               // reversed: the old trim means nothing
    ui_app.ui.pad_row = 6; ev_tap(CTL_JOY_SW);                           // Undo: the parent
    CHECK_STR(ui_app.pads.pad[0].file, "demo0~1");
    ev_tap(CTL_JOY_SW);
    CHECK_STR(ui_app.pads.pad[0].file, "demo0");
    ui_app.ui.pad_row = 7; ev_tap(CTL_JOY_SW);                           // Redo: the newest child
    CHECK_STR(ui_app.pads.pad[0].file, "demo0~1");
    ui_app.ui.pad_row = 5; ev_turn(CTL_ENC_B, 1);                         // Ver: the next version
    CHECK_STR(ui_app.pads.pad[0].file, "demo0~2");
    history_t h;                                                         // history.cfg was written
    CHECK(history_load(&h) && h.n == 2);
    ev_tap(BACK);                                                        // the Smp row does not offer the versions
    ui_app.ui.pad_row = 1;
    ev_turn(CTL_ENC_B, 1);
    CHECK(strchr(ui_app.pads.pad[0].file, '~') == NULL);
}

TEST(pads_edit_trims_old_versions_and_deletes_their_files) {
    ui_stub_ramcard(true);
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");
    shift_menu();
    ev_tap(CTL_JOY_SW);
    ui_app.ui.pad_page = 3;
    const int removed = ui_stub_removed;
    for (int k = 0; k < 10; k++) { ui_app.ui.pad_row = 4; ev_tap(CTL_JOY_SW); ev_idle(3); }
    CHECK_STR(ui_app.pads.pad[0].file, "demo0~10");
    CHECK_EQ(ui_app.history.n, HISTORY_KEEP);                            // the last 8 of 10
    CHECK_EQ(ui_stub_removed, removed + 2);                              // ~1 and ~2: their files deleted
    CHECK_STR(ui_stub_removed_last, STORAGE_DIR_SAMPLES "/demo0~2.smp");
    audio_sample_info_t in;
    bool listed = false;
    for (int i = 0; i < audio_sample_count(); i++) if (audio_sample_info(i, &in) && !strcmp(in.name, "demo0~1")) listed = true;
    CHECK(!listed);                                                      // and gone from the library's lists
}

TEST(pads_grid_chop_pads_the_sample_to_whole_grid_units) {
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");               // 96000 frames = 2 s
    ui_app.seq.bpm = 100;                                                // a beat = 28800 frames: 2 s is 3.33 beats
    shift_menu();
    ev_tap(CTL_JOY_SW);
    ui_app.ui.pad_page = 2;                                              // CHOP
    ui_app.ui.pad_row = 1; ev_turn(CTL_ENC_B, 2);                         // Mode: Grid (1/4 note)
    CHECK_EQ(ui_app.ui.chop.mode, CHOP_GRID);
    const int renders = ui_stub_renders;
    ui_app.ui.pad_row = 7; ev_tap(CTL_JOY_SW);                           // Chop to pads
    ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_SW);                            // Yes
    ev_idle(3);
    CHECK_EQ(ui_stub_renders, renders + 1);                              // a padded version: 4 beats = 115200 frames
    CHECK_EQ(ui_stub_render_op, 7);
    CHECK_EQ(ui_stub_render_amount, 115200);
    CHECK_STR(ui_app.pads.pad[0].file, "demo0~1");
    const pad_t *s1 = &ui_app.pads.pad[1], *s4 = &ui_app.pads.pad[4];
    CHECK_STR(s1->file, "demo0~1");                                      // 4 slices of one beat on A2..A5
    CHECK(s1->start == 0 && s1->end == 28800);
    CHECK(s4->start == 86400 && s4->end == 0);
    CHECK(pad_empty(&ui_app.pads.pad[5]));
    // a trim that the file can fill: End moves to the grid line, no render
    ui_fresh();
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");
    ui_app.seq.bpm = 120;                                                // a beat = 24000
    ui_app.pads.pad[0].end = 40000;
    shift_menu();
    ev_tap(CTL_JOY_SW);
    ui_app.ui.pad_page = 2;
    ui_app.ui.pad_row = 1; ev_turn(CTL_ENC_B, 2);
    const int r2 = ui_stub_renders;
    ui_app.ui.pad_row = 7; ev_tap(CTL_JOY_SW);
    ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_SW);
    ev_idle(2);
    CHECK_EQ(ui_stub_renders, r2);
    CHECK_EQ(ui_app.pads.pad[2].end, 48000);                             // the second slice ends on the beat, with the file's own audio
}

// The EDIT page's Apply on pad A1 with operation `op` and amount `amt` (the stub renders at once).
static void edit_apply(int op, int32_t amt) {
    ui_app.ui.pad_page = 3;
    ui_app.ui.edit_op = (uint8_t)op;
    ui_app.ui.edit_amt[op] = amt;
    ui_app.ui.pad_row = 4;
    ev_tap(CTL_JOY_SW);
    ev_idle(3);
}

TEST(pads_edit_stretch_pitch_speed_and_the_trim_points_follow) {
    ui_stub_ramcard(false); ui_stub_ramcard(true);                       // an empty card
    ui_fresh();
    pad_t *p = &ui_app.pads.pad[0];
    snprintf(p->file, PAD_NAME_MAX, "demo0");                            // 96000 frames at 48 kHz = 2 s
    p->start = 10000; p->end = 40000; p->lstart = 20000;
    shift_menu();
    ev_tap(CTL_JOY_SW);
    ui_app.ui.pad_page = 3;
    ui_app.ui.pad_row = 1; ev_turn(CTL_ENC_B, 8);                         // Op: the 9th is Stretch
    CHECK_STR(pads_ui_op_name(ui_app.ui.edit_op), "Stretch");
    ui_app.ui.pad_row = 2; ev_turn(CTL_ENC_B, 1);                         // its amount: 100 % -> 105 %
    CHECK_EQ(ui_app.ui.edit_amt[EDIT_STRETCH], 105);
    const int renders = ui_stub_renders;
    edit_apply(EDIT_STRETCH, 100);                                       // 100 %: nothing to do, no file written
    CHECK_EQ(ui_stub_renders, renders);
    CHECK_STR(p->file, "demo0");
    edit_apply(EDIT_STRETCH, 150);
    CHECK_EQ(ui_stub_renders, renders + 1);
    CHECK_EQ(ui_stub_render_op, PROC_STRETCH);
    CHECK_EQ(ui_stub_render_amount, 1500000);
    CHECK_STR(p->file, "demo0~1");
    CHECK_STR(ui_app.history.v[0].what, "Stretch 150%");
    CHECK(p->start == 15000 && p->end == 60000 && p->lstart == 30000);   // the trim points went with the audio
    edit_apply(EDIT_SPEED, 200);                                         // twice the speed: half the length
    CHECK_EQ(ui_stub_render_op, PROC_RESAMPLE);
    CHECK_EQ(ui_stub_render_amount, 500000);
    CHECK(p->start == 7500 && p->end == 30000 && p->lstart == 15000);
    CHECK_STR(ui_app.history.v[1].what, "Speed 200%");
    edit_apply(EDIT_PITCH, -350);                                        // the length stays, the points too
    CHECK_EQ(ui_stub_render_op, PROC_PITCH);
    CHECK_EQ(ui_stub_render_amount, -350);
    CHECK(p->start == 7500 && p->end == 30000);
    CHECK_STR(ui_app.history.v[2].what, "Pitch -3.50st");
    CHECK_STR(p->file, "demo0~3");
    edit_apply(EDIT_PITCH, 0);                                           // 0 cents: nothing
    CHECK_STR(p->file, "demo0~3");
}

TEST(pads_edit_fit_makes_the_loop_last_whole_beats) {
    ui_stub_ramcard(false); ui_stub_ramcard(true);                       // an empty card
    ui_fresh();
    pad_t *p = &ui_app.pads.pad[0];
    snprintf(p->file, PAD_NAME_MAX, "demo0");                            // 2 s
    ui_app.seq.bpm = 120;                                                // a beat = 24000 frames
    shift_menu();
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(pad_fit_ppm(96000, 48000, 4, 120), 1000000u);
    CHECK_EQ(pad_fit_ppm(96000, 48000, 5, 120), 1250000u);
    CHECK_EQ(pad_fit_ppm(96000, 44100, 4, 120), 918750u);                // (at the file's own rate: 4 beats = 88200 of its frames)
    const int renders = ui_stub_renders;
    edit_apply(EDIT_FIT, 4);                                             // 2 s are 4 beats already
    CHECK_EQ(ui_stub_renders, renders);
    edit_apply(EDIT_FIT, 128);                                           // 64 s out of 2: refused
    CHECK_EQ(ui_stub_renders, renders);
    edit_apply(EDIT_FIT, 5);                                             // the whole pad to 5 beats
    CHECK_EQ(ui_stub_render_op, PROC_STRETCH);
    CHECK_EQ(ui_stub_render_amount, 1250000);
    CHECK_STR(ui_app.history.v[0].what, "Fit 5b@120");
    // a looping pad: the loop (loop start .. End) is what must fit, the lead-in before it stretches along
    ui_stub_ramcard(false); ui_stub_ramcard(true);                       // (an empty card again: no history)
    ui_fresh();
    p = &ui_app.pads.pad[0];
    snprintf(p->file, PAD_NAME_MAX, "demo0");
    ui_app.seq.bpm = 100;                                                // a beat = 28800 frames
    p->loop = PAD_LOOP_TRIM; p->lstart = 36000; p->end = 90000;           // a loop of 54000 frames
    shift_menu();
    ev_tap(CTL_JOY_SW);
    edit_apply(EDIT_FIT_SPEED, 2);                                       // to 2 beats = 57600 frames, by the speed
    CHECK_EQ(ui_stub_render_op, PROC_RESAMPLE);
    CHECK_EQ(ui_stub_render_amount, 1066667);
    CHECK_STR(ui_app.history.v[0].what, "FitSpd 2b@100");
    audio_sample_info_t in;
    bool found = false;
    for (int i = 0; i < audio_sample_count() && !found; i++) found = audio_sample_info(i, &in) && !strcmp(in.name, p->file);
    CHECK(found);
    const uint32_t loop = pad_end(p, in.frames) - pad_lstart(p, in.frames);
    CHECK(loop >= 57599 && loop <= 57601);                               // the loop is 2 beats long now
}

TEST(pads_edit_crop_and_undo_puts_the_trim_points_back) {
    ui_stub_ramcard(false); ui_stub_ramcard(true);                       // an empty card
    ui_fresh();
    pad_t *p = &ui_app.pads.pad[0];
    snprintf(p->file, PAD_NAME_MAX, "demo0");                            // 96000 frames
    shift_menu();
    ev_tap(CTL_JOY_SW);
    const int renders = ui_stub_renders;
    edit_apply(EDIT_CROP, 0);                                            // not trimmed: nothing to crop
    CHECK_EQ(ui_stub_renders, renders);
    p->start = 10000; p->end = 40000; p->lstart = 20000;
    edit_apply(EDIT_CROP, 0);
    CHECK_EQ(ui_stub_render_op, PROC_CROP);
    CHECK(ui_stub_render_amount == 10000 && ui_stub_render_amount2 == 40000);
    CHECK_STR(p->file, "demo0~1");
    CHECK_STR(ui_app.history.v[0].what, "Crop");
    CHECK(p->start == 0 && p->end == 0 && p->lstart == 10000);           // the new file is the old region; the loop start moved with it
    edit_apply(EDIT_STRETCH, 200);                                       // twice as long
    CHECK_STR(p->file, "demo0~2");
    CHECK_EQ(p->lstart, 20000u);
    ui_app.ui.pad_row = 6; ev_tap(CTL_JOY_SW);                           // Undo: the crop, with its own loop start
    CHECK_STR(p->file, "demo0~1");
    CHECK(p->start == 0 && p->end == 0 && p->lstart == 10000);
    ev_tap(CTL_JOY_SW);                                                  // Undo: the original, trimmed as it was
    CHECK_STR(p->file, "demo0");
    CHECK(p->start == 10000 && p->end == 40000 && p->lstart == 20000);
    ui_app.ui.pad_row = 7; ev_tap(CTL_JOY_SW);                           // Redo
    CHECK_STR(p->file, "demo0~1");
    CHECK(p->start == 0 && p->end == 0 && p->lstart == 10000);
    ui_app.ui.pad_row = 5; ev_turn(CTL_ENC_B, 1);                         // Ver: on to the stretched one
    CHECK_STR(p->file, "demo0~2");
    CHECK_EQ(p->lstart, 20000u);
    ev_turn(CTL_ENC_B, 1);                                               // Ver wraps to the original: two steps up the chain
    CHECK_STR(p->file, "demo0");
    CHECK(p->start == 10000 && p->end == 40000 && p->lstart == 20000);
    history_t h;                                                         // and the card remembers them
    CHECK(history_load(&h) && h.n == 2);
    CHECK(h.v[0].ptrim[0] == 10000 && h.v[0].ptrim[1] == 40000 && h.v[0].ctrim[2] == 10000);
    CHECK_STR(h.v[1].what, "Stretch 200%");
}

TEST(pads_edit_color_operations_and_their_amounts) {
    ui_stub_ramcard(false); ui_stub_ramcard(true);
    ui_fresh();
    pad_t *p = &ui_app.pads.pad[0];
    snprintf(p->file, PAD_NAME_MAX, "demo0");
    shift_menu();
    ev_tap(CTL_JOY_SW);
    char t[16];
    CHECK_STR(pads_ui_op_name(EDIT_RATE), "Lo-fi rate");
    pads_ui_op_amount(EDIT_RATE, ui_app.ui.edit_amt[EDIT_RATE], t, sizeof t);
    CHECK_STR(t, "12000Hz");
    edit_apply(EDIT_RATE, 8000);
    CHECK(ui_stub_render_op == PROC_DECIMATE && ui_stub_render_amount == 8000);
    CHECK_STR(ui_app.history.v[0].what, "Rate 8000Hz");
    edit_apply(EDIT_BITS, 8);
    CHECK(ui_stub_render_op == PROC_BITS && ui_stub_render_amount == 8);
    CHECK_STR(ui_app.history.v[1].what, "Bits 8");
    edit_apply(EDIT_DRIVE, 120);
    CHECK(ui_stub_render_op == PROC_DRIVE && ui_stub_render_amount == 120);
    CHECK_STR(ui_app.history.v[2].what, "Drive +12.0dB");
    edit_apply(EDIT_LOWPASS, 4000);
    CHECK(ui_stub_render_op == PROC_LOWPASS && ui_stub_render_amount == 4000);
    CHECK_STR(ui_app.history.v[3].what, "LP 4000Hz");
    edit_apply(EDIT_HIGHPASS, 200);
    CHECK(ui_stub_render_op == PROC_HIGHPASS && ui_stub_render_amount == 200);
    CHECK_STR(p->file, "demo0~5");
    ui_app.ui.pad_page = 3;                                              // the cutoff moves by about a semitone a step, and stays in range
    ui_app.ui.edit_op = EDIT_LOWPASS;
    ui_app.ui.edit_amt[EDIT_LOWPASS] = 1000;
    ui_app.ui.pad_row = 2; ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.ui.edit_amt[EDIT_LOWPASS], 1060);
    ui_app.ui.edit_amt[EDIT_LOWPASS] = 20000; ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.ui.edit_amt[EDIT_LOWPASS], 20000);
    ui_app.ui.edit_op = EDIT_BITS; ui_app.ui.edit_amt[EDIT_BITS] = 2; ev_turn(CTL_ENC_B, -1);
    CHECK_EQ(ui_app.ui.edit_amt[EDIT_BITS], 2);
}

TEST(pads_edit_tonal_algo_and_paulstretch) {
    ui_stub_ramcard(false); ui_stub_ramcard(true);
    ui_fresh();
    pad_t *p = &ui_app.pads.pad[0];
    snprintf(p->file, PAD_NAME_MAX, "demo0");                            // 96000 frames
    p->start = 10000; p->end = 40000;
    shift_menu();
    ev_tap(CTL_JOY_SW);
    ui_app.ui.pad_page = 3;
    ui_app.ui.edit_op = EDIT_STRETCH;
    ui_app.ui.pad_row = 3; ev_turn(CTL_ENC_B, 1);                         // the Algo row: Beat -> Tonal
    CHECK_EQ(ui_app.ui.edit_tonal, 1);
    edit_apply(EDIT_STRETCH, 150);
    CHECK(ui_stub_render_op == PROC_STRETCH_TONAL && ui_stub_render_amount == 1500000);
    CHECK_STR(ui_app.history.v[0].what, "StretchT 150%");
    CHECK(p->start == 15000 && p->end == 60000);
    edit_apply(EDIT_PITCH, 700);
    CHECK(ui_stub_render_op == PROC_PITCH_TONAL && ui_stub_render_amount == 700);
    CHECK_STR(ui_app.history.v[1].what, "PitchT +7.00st");
    edit_apply(EDIT_SPEED, 50);                                          // Speed has no Algo: the resampler as before
    CHECK_EQ(ui_stub_render_op, PROC_RESAMPLE);
    ui_app.ui.edit_tonal = 0;
    edit_apply(EDIT_PITCH, 100);
    CHECK_EQ(ui_stub_render_op, PROC_PITCH);
    // paulstretch: x8 by default, the trim points go along, a result past the limit is refused
    ui_stub_ramcard(false); ui_stub_ramcard(true);
    ui_fresh();
    p = &ui_app.pads.pad[0];
    snprintf(p->file, PAD_NAME_MAX, "demo0");
    p->start = 10000; p->end = 40000;
    shift_menu();
    ev_tap(CTL_JOY_SW);
    char t[16];
    pads_ui_op_amount(EDIT_PAUL, ui_app.ui.edit_amt[EDIT_PAUL], t, sizeof t);
    CHECK_STR(t, "x8.0");
    edit_apply(EDIT_PAUL, 80);
    CHECK(ui_stub_render_op == PROC_PAUL && ui_stub_render_amount == 8000000);
    CHECK_STR(ui_app.history.v[0].what, "Paul x8.0");
    CHECK(p->start == 80000 && p->end == 320000);
    const int renders = ui_stub_renders;
    edit_apply(EDIT_PAUL, 640);                                          // 768000 frames x64 = 49 million: too long
    CHECK_EQ(ui_stub_renders, renders);
}

/* ---------------- the FX chains (ADR-041 decisions 23, 47-49) ---------------- */
extern int ui_stub_pads_fx;

TEST(pads_fx_page_chains_a_module_and_its_values) {
    ui_stub_ramcard(false); ui_stub_ramcard(true);                       // an empty card
    ui_fresh();
    rack_t *fx = &ui_app.pads.fx;
    CHECK(fx->fx && fx->count == 0);
    shift_menu();                                                        // the Pads section
    CHECK_EQ(ui_app.ui.pad_fx, 0);
    ev_tap(MENU);                                                        // Menu: the FX chains
    CHECK_EQ(ui_app.ui.pad_fx, 1);
    CHECK_EQ(ui_app.ui.fx_row, 1);                                       // chain A, its + cell
    const int sent = ui_stub_pads_fx;
    ev_tap(CTL_JOY_SW);                                                  // push: the menu (Type, Insert)
    CHECK(ui_app.ui.rack_menu);
    ui_app.ui.rack_type = MOD_OSC;                                       // Type only offers what a chain takes
    ev_turn(CTL_ENC_B, 1);
    CHECK(rack_is_processor((module_type_t)ui_app.ui.rack_type));
    ui_app.ui.rack_type = MOD_REVERB;
    ui_app.ui.fx_row = 6;                                                // Insert
    ev_tap(CTL_JOY_SW);
    CHECK(fx->count == 1 && fx->slot[0].type == MOD_REVERB && fx->slot[0].row == 0);
    CHECK(ui_stub_pads_fx > sent);                                       // the engine got the chains
    CHECK(!ui_app.ui.rack_menu);
    ui_app.ui.fx_row = 1; ui_app.ui.rack_col[0] = 0;                     // the reverb's cell: push, Edit is the menu's first row
    ev_tap(CTL_JOY_SW);
    CHECK(ui_app.ui.rack_menu && ui_app.ui.fx_row == 4);
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui_app.ui.pad_fx, 2);                                       // its values
    const float mix = fx->slot[0].v[MP_RV_MIX], dec = fx->slot[0].v[MP_RV_DEC];
    const int sent2 = ui_stub_pads_fx;
    ui_app.ui.fx_row = 1;
    ev_turn(CTL_ENC_B, 1);
    CHECK(fx->slot[0].v[MP_RV_MIX] > mix);
    CHECK(ui_stub_pads_fx > sent2);
    ev_knob(CTL_COL_KNOB_0 + 1, INPUT_VALUE_MAX);                         // the second column knob: the second value, to its top
    CHECK(fx->slot[0].v[MP_RV_DEC] > dec);
    ev_idle(1);
    ev_tap(BACK);                                                        // back to the chains
    CHECK_EQ(ui_app.ui.pad_fx, 1);
    ui_app.ui.fx_row = 0;                                                // the header: the routing
    ev_tap(CTL_JOY_RIGHT);
    CHECK_EQ(fx->fx_route, RACK_ROUTE_AB_C);
    ev_idle(1);
    ev_tap(BACK);                                                        // and back to the pads
    CHECK_EQ(ui_app.ui.pad_fx, 0);
    CHECK(ui_app.section == SECTION_PADS);
    ev_idle(400);                                                        // a few seconds: padsfx.cfg is written
    rack_t back;
    rack_init_fx(&back);
    CHECK(pads_fx_load(&back));
    CHECK(back.fx && back.count == 1 && back.slot[0].type == MOD_REVERB && back.fx_route == RACK_ROUTE_AB_C);
    CHECK(fabs((double)back.slot[0].v[MP_RV_DEC] - (double)fx->slot[0].v[MP_RV_DEC]) < 1e-3);
    pads_t loaded;                                                       // pads.cfg does not touch the chains, and carries a pad's bus
    pads_init(&loaded);
    loaded.fx = *fx;
    CHECK(pads_from_text(&loaded, "pads\npad A1 bus b file demo0\n"));
    CHECK(loaded.fx.count == 1 && loaded.pad[0].bus == 2);
    static char txt[STORAGE_FILE_MAX];
    pads_to_text(&loaded, txt, sizeof txt);
    CHECK(strstr(txt, " bus b") != NULL);
}

TEST(pads_fx_rack_rules_and_the_synth_rack_keeps_its_own) {
    rack_t fx;
    rack_init_fx(&fx);
    CHECK_EQ(rack_fx_load_pct(&fx), 27);                                 // the 8 pad voices alone
    CHECK(!rack_can_insert(&fx, 0, 0, MOD_OSC) && !rack_can_insert(&fx, 0, 0, MOD_LFO) && !rack_can_insert(&fx, 0, 1, MOD_COMB));
    for (int k = 0; k < RACK_FX_SLOTS; k++) CHECK(rack_insert_row(&fx, fx.count, 1, k & 1 ? MOD_DELAY : MOD_FILTER));
    CHECK(!rack_can_insert(&fx, fx.count, 1, MOD_EQ));                    // chain B is full
    CHECK(rack_insert_row(&fx, fx.count, 2, MOD_EQ));
    CHECK(rack_set_row(&fx, fx.count - 1, 0));                           // the EQ moves from C to A
    CHECK(!rack_set_row(&fx, fx.count - 1, 1));                          // not into the full chain
    CHECK(!rack_can_para(&fx, 0) && rack_shared_start(&fx, 1) == RACK_NONE && rack_slot_is_shared(&fx, 0));
    CHECK_EQ(rack_fx_param_count(MOD_FILTER), 3);
    CHECK(rack_fx_load_pct(&fx) > 45 && rack_fx_load_pct(&fx) < 55);     // 2 filters, 2 delays, an EQ next to the 8 pads: about 50 %
    CHECK_STR(rack_route_name(RACK_ROUTE_AC_AB), "A+C/A+B");
    static char txt[STORAGE_FILE_MAX];
    pads_fx_to_text(&fx, txt, sizeof txt);
    rack_t b;
    rack_init_fx(&b);
    CHECK(pads_fx_from_text(&b, txt));
    CHECK_EQ(b.count, fx.count);
    for (int i = 0; i < fx.count; i++) CHECK(b.slot[i].type == fx.slot[i].type && b.slot[i].row == fx.slot[i].row);
    CHECK(!pads_fx_from_text(&b, "keys\n"));                             // not that file: kept
    CHECK_EQ(b.count, fx.count);
    rack_t synth;                                                        // the synth's rack: sources and modulators as before
    rack_init_startup(&synth);
    CHECK(!synth.fx && rack_can_insert(&synth, 0, 0, MOD_OSC));
}

/* ---------------- capture / resampling: the Rec key ---------------- */
extern int ui_stub_cap_state, ui_stub_cap_bar;
extern uint32_t ui_stub_cap_limit, ui_stub_cap_frames;
extern char ui_stub_cap_name[24];
void ui_stub_capture_sound(uint32_t frames);
#define REC_KEY CTL_KEY(2, 6)                                            // the Pads layout: the right block's second row, third key

TEST(pads_rec_key_records_onto_an_empty_pad) {
    ui_stub_ramcard(false); ui_stub_ramcard(true);
    ui_fresh();
    shift_menu();
    CHECK_EQ(keymap_get(2 * KEY_COLS + 6).act, ACT_PAD_REC);
    snprintf(ui_app.pads.pad[0].file, PAD_NAME_MAX, "demo0");            // A1 is taken: the recording goes to A2
    ev_tap(REC_KEY);
    CHECK_EQ(ui_stub_cap_state, CAPTURE_ST_ARMED);
    CHECK_STR(ui_stub_cap_name, "rec1");
    CHECK(ui_stub_cap_bar == 0 && ui_stub_cap_limit == 300u * 48000u);   // no length: from the first sound, 5 minutes at most
    ev_idle(2);
    CHECK_EQ(ui_app.ui.cap_state, CAPTURE_ST_ARMED);
    ui_stub_capture_sound(3 * 48000);
    ev_idle(2);
    CHECK(ui_app.ui.cap_state == CAPTURE_ST_RECORDING && ui_app.ui.cap_secs == 3);
    ev_tap(REC_KEY);                                                     // Rec again: stop
    ev_idle(4);
    CHECK_EQ(ui_app.ui.cap_state, CAPTURE_ST_IDLE);
    const pad_t *p = &ui_app.pads.pad[1];
    CHECK_STR(p->file, "rec1");
    CHECK(p->level == 100 && p->loop == PAD_LOOP_OFF && p->bus == 0);
    CHECK(ui_app.ui.pad_cur == 1 && ui_app.ui.pad_bank == 0);            // and it is the selected pad now
    ev_tap(REC_KEY);                                                     // the next one: rec2 on A3; stopped before any sound: nothing
    CHECK_STR(ui_stub_cap_name, "rec2");
    ev_tap(REC_KEY);
    ev_idle(3);
    CHECK(pad_empty(&ui_app.pads.pad[2]));
    CHECK_EQ(ui_app.capture.stage, 0);
}

TEST(pads_rec_with_a_length_runs_on_the_clock_and_makes_a_loop) {
    ui_stub_ramcard(false); ui_stub_ramcard(true);
    ui_fresh();
    shift_menu();
    ui_app.seq.bpm = 120;
    ev_press(SHIFT); ev_tap(REC_KEY); ev_tap(REC_KEY); ev_release(SHIFT);   // Shift + Rec twice: 1 bar, then 2
    CHECK_EQ(ui_app.capture.bars, 2);
    CHECK_EQ(ui_stub_cap_state, CAPTURE_ST_IDLE);
    ui_app.pads_run = true;                                              // the clock runs
    ev_tap(REC_KEY);
    CHECK(ui_stub_cap_bar == 1 && ui_stub_cap_limit == 2u * 96000u);     // from the next bar line, 2 bars of 2 s
    ui_stub_capture_sound(250000);                                       // more than its length: it stopped by itself
    ev_idle(4);
    const pad_t *p = &ui_app.pads.pad[0];
    CHECK_STR(p->file, "rec1");
    CHECK(p->loop == PAD_LOOP_TRIM && p->grid == PAD_GRID_BAR);
    audio_sample_info_t in;
    bool found = false;
    for (int i = 0; i < audio_sample_count() && !found; i++) found = audio_sample_info(i, &in) && !strcmp(in.name, "rec1");
    CHECK(found && in.frames == 192000u);
    // no empty pad left: refused
    for (int i = 0; i < PAD_COUNT; i++) snprintf(ui_app.pads.pad[i].file, PAD_NAME_MAX, "demo0");
    ev_tap(REC_KEY);
    CHECK_EQ(ui_stub_cap_state, CAPTURE_ST_IDLE);
}
