// The Pads section (core/pads.h, scr_pads.c, app.c; ADR-041 decisions 14-26): Shift + Menu, the Pads key layout, pad keys with Select /
// Mute / Shift / banks, the grid and the pad editor, the column knobs, pads.cfg, the LEDs.
#include "ui_test.h"
#include "core/led_roles.h"

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
    ev_turn(CTL_ENC_A, -2);                                              // the header: encoder B steps to the next pad
    CHECK_EQ(ui_app.ui.pad_row, 0);
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.ui.pad_cur, 6);
    ev_tap(BACK);                                                        // Back: the grid
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
    CHECK_STR(ui_app.popup.info.title, "TEMPO");
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
    ev_turn(CTL_ENC_B, 2);
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
    ev_turn(CTL_ENC_A, 9);                                               // Lvl Pan Tune Fine Cut Res Chok Mute From
    CHECK_EQ(ui_app.ui.pad_row, 19);
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.pads.pad[0].from, 1);
}
