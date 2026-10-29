// The fallback table (ADR-046 phase 5, rule R7 / R13; docs/guides/UX_REORGANIZATION.md section 5): every verb with the smallest set of
// controls, the joystick, its push and the 4 function keys (F1 Shift, F2 Menu, F3 Back, F4 Play), on HWV1's controls. A verb a mapped
// layer entry carries (Tap, BPM) uses a note key with Shift, as a user would map it.
#include "ui_test.h"
#include "app/app_internal.h"

#define F_SHIFT ((control_id_t)CTL_KEY(0, 0))
#define F_MENU  ((control_id_t)CTL_KEY(0, 1))
#define F_BACK  ((control_id_t)CTL_KEY(0, 2))
#define F_PLAY  ((control_id_t)CTL_KEY(0, 3))

extern int ui_stub_cap_state;

static void shifted(control_id_t c) { ev_press(F_SHIFT); ev_tap(c); ev_release(F_SHIFT); }
static int slot_of(module_type_t t) { for (int i = 0; i < ui_app.rack.count; i++) if (ui_app.rack.slot[i].type == t) return i; return -1; }

TEST(fallbacks_every_synth_verb_by_the_joystick_and_four_keys) {
    ui_board(true);
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    // MOVE / STEP, TOP (up from row 1, then the view selector), PAGE - / + (the header's left / right)
    ui->page = 0; ui->row = 0;
    ev_tap(CTL_JOY_DOWN);
    CHECK_EQ(ui->row, 1);
    ev_tap(CTL_JOY_UP);
    CHECK(ui->row == 0 && !ui->view_sel);
    ev_tap(CTL_JOY_UP);
    CHECK(ui->view_sel);
    ev_tap(CTL_JOY_DOWN);
    CHECK(!ui->view_sel && ui->row == 0);
    ev_tap(CTL_JOY_RIGHT);
    CHECK_EQ(ui->page, 1);
    ev_tap(CTL_JOY_LEFT);
    CHECK_EQ(ui->page, 0);
    // ADJUST (latch, then left / right), COARSE / FINE (Shift + left / right), RESET (Shift + push latched), BACK
    const int fl = slot_of(MOD_FILTER);
    CHECK(fl >= 0);
    ui->page = ui_find_page(fl, 0);
    page_t pg;
    get_page(ui, &ui_app.rack, ui->page, &pg);
    int row = 0;
    for (int i = 0; i < pg.count; i++) if (pg.params[i] == MP_FL_CUT) row = i + 1;
    ui->row = row;
    float *cut = &ui_app.rack.slot[fl].v[MP_FL_CUT];
    const float c0 = *cut;
    ev_tap(CTL_JOY_SW);
    ev_tap(CTL_JOY_RIGHT);
    const float c1 = *cut;
    CHECK(ui->latched && c1 > c0);
    shifted(CTL_JOY_RIGHT);
    CHECK(*cut > c1 && *cut - c1 < c1 - c0);                            // a fine step: smaller
    shifted(CTL_JOY_SW);
    CHECK(*cut == c0 && ui->latched);                                   // (the startup patch's cutoff is the default)
    ev_tap(F_BACK);
    CHECK(!ui->latched && ui->row == row);
    ev_tap(F_BACK);
    CHECK_EQ(ui->row, 0);
    // ENTER: the cog, and its Reset (asked)
    ui->row = pg.count + 1;
    ev_tap(CTL_JOY_SW);
    CHECK(ui->cog_page == ui->page);
    ev_tap(F_BACK);
    // TOGGLE (Menu), MAP (Shift + Menu), PAGE LIST (Menu held)
    ev_tap(F_MENU);
    CHECK(ui->in_rack && synth_ui_view(ui, &ui_app.rack) == VIEW_RACK);
    ev_tap(F_MENU);
    CHECK(!ui->in_rack);
    shifted(F_MENU);
    CHECK(popup_modal(&ui_app.popup) && !strcmp(ui_app.popup.queue[0].title, "MAP"));
    ev_tap(F_BACK);
    ev_press(F_MENU); ev_idle(100); ev_release(F_MENU);
    CHECK(popup_modal(&ui_app.popup) && !strcmp(ui_app.popup.queue[0].title, "EDIT"));
    ev_tap(F_BACK);
    CHECK(!popup_modal(&ui_app.popup));
    // OCTAVE (Shift + up / down), XY (Shift + push, nothing latched)
    const int oct = ui_app.in.octave;
    ui->row = 1;
    shifted(CTL_JOY_UP);
    CHECK_EQ(ui_app.in.octave, oct + 1);
    shifted(CTL_JOY_DOWN);
    shifted(CTL_JOY_SW);
    CHECK(ui->joy_xy);
    shifted(CTL_JOY_SW);
    CHECK(!ui->joy_xy);
    // PLAY, REC (Shift + Play: the list), CLICK (in the list)
    ev_tap(F_PLAY);
    CHECK(ui_app.seq.running);
    ev_tap(F_PLAY);
    shifted(F_PLAY);
    CHECK(ui_list_pick("Notes"));
    CHECK(ui_app.rec.armed);
    shifted(F_PLAY);
    CHECK(!ui_app.rec.armed);
    shifted(F_PLAY);
    CHECK(ui_list_pick("Click: Off"));
    CHECK(ui_app.metronome);
    // MACROS: macro 3 (no knob plays it on HWV1) from the MACROS tab's Value
    synth_ui_macro_add(ui, 2, &(macro_t){MACRO_MODULE, ui_app.rack.slot[fl].id, MP_FL_CUT});
    synth_ui_go(ui, &ui_app.rack, SYNTH_GO_MACROS);
    ui->row = 1;
    ev_tap(CTL_JOY_SW); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui->macro_cur, 2);
    ev_tap(CTL_JOY_DOWN);                                               // Value
    const float before = *cut;
    ev_tap(CTL_JOY_SW); ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_SW);
    CHECK(ui->macro_pos[2] >= 0 && *cut < before);
    ui_board(false);
}

TEST(fallbacks_every_pads_verb_by_the_joystick_and_four_keys) {
    ui_board(true);
    ui_stub_ramcard(false); ui_stub_ramcard(true);
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    shifted(F_MENU);
    CHECK(ui_list_pick("Pads"));
    CHECK(ui_app.section == SECTION_PADS);
    // BANK (Shift + left / right on the grid), pads by the joystick, ENTER = the pad's editor, BACK
    shifted(CTL_JOY_RIGHT);
    CHECK_EQ(ui->pad_bank, 1);
    shifted(CTL_JOY_LEFT);
    CHECK_EQ(ui->pad_bank, 0);
    ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_DOWN);
    CHECK_EQ(ui->pad_cur, 5);
    ev_tap(CTL_JOY_SW);
    CHECK(ui->pad_edit);
    ev_tap(F_BACK);
    CHECK(!ui->pad_edit);
    // PLAY, the Pads Rec list: capture, steps, click; Stop all (Shift + Back)
    ev_tap(F_PLAY);
    CHECK(ui_app.pads_run);
    ev_tap(F_PLAY);
    shifted(F_PLAY);
    CHECK(ui_list_pick("Capture"));
    CHECK_EQ(ui_stub_cap_state, CAPTURE_ST_ARMED);
    shifted(F_PLAY);
    ev_idle(3);
    CHECK_EQ(ui_app.capture.stage, 0);
    shifted(F_PLAY);
    CHECK(ui_list_pick("Steps on"));
    CHECK(ui->pad_steps);
    shifted(F_PLAY);
    CHECK(ui_list_pick("Steps off"));
    shifted(F_BACK);
    CHECK(popup_info_showing(&ui_app.popup));
    // Tap and BPM: actions a layer entry carries (MODIFIERS), here Shift + two note keys
    const control_id_t tap = (control_id_t)CTL_KEY(4, 6), up = (control_id_t)CTL_KEY(4, 7);
    modifiers_set(MODL_SHIFT, tap, (mod_entry_t){ME_ACTION, ACT_TAP, 0, {MACRO_NONE, 0, 0}});
    modifiers_set(MODL_SHIFT, up, (mod_entry_t){ME_ACTION, ACT_TEMPO, 1, {MACRO_NONE, 0, 0}});
    const int bpm = ui_app.seq.bpm;
    shifted(up);
    CHECK_EQ(ui_app.seq.bpm, bpm + 1);
    shifted(tap);
    CHECK(popup_info_showing(&ui_app.popup) && !strcmp(ui_app.popup.info.title, "TAP"));
    modifiers_reset();
    ui_board(false);
}
