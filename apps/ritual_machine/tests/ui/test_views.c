// The views (ENGINE_DESIGN.md ADR-046 phase 3): the synth's pages and tabs grouped as EDIT, SEQ, RACK, PERFORM, PRESETS, SETUP; the header's
// view selector above the page selector; the page rings of each view; Menu = home <-> the last other view; Back climbs to home; MACHINE.
#include "ui_test.h"
#include "core/ui_screen.h"
#include "app/app_internal.h"

#define MENU CTL_BTN_1
#define BACK CTL_BTN_2

static view_t view(void) { return synth_ui_view(&ui_app.ui, &ui_app.rack); }
static bool on_tab(tab_t t) { return ui_app.ui.in_rack && tab_kind(&ui_app.rack, ui_app.ui.menu_tab) == t; }

TEST(views_the_selector_above_the_page_selector_steps_through_every_view) {
    ui_fresh();
    CHECK(view() == VIEW_EDIT && ui_app.ui.row == 0 && !ui_app.ui.view_sel);
    ev_tap(CTL_JOY_UP);                                                 // up from the page selector: the view selector
    CHECK(ui_app.ui.view_sel && ui_app.ui.row == 0);
    ev_tap(CTL_JOY_UP);                                                 // nowhere further
    CHECK(ui_app.ui.view_sel);
    static const view_t ring[] = {VIEW_SEQ, VIEW_RACK, VIEW_PERFORM, VIEW_PRESETS, VIEW_SETUP, VIEW_EDIT};
    for (int k = 0; k < 6; k++) {
        ev_tap(CTL_JOY_RIGHT);
        CHECK_EQ(view(), ring[k]);
        CHECK(ui_app.ui.view_sel && ui_app.ui.row == 0);               // the focus stays on the selector
    }
    ev_tap(CTL_JOY_LEFT);
    CHECK_EQ(view(), VIEW_SETUP);
    ev_tap(CTL_JOY_DOWN);                                               // down: the page selector (SETUP has five tabs)
    CHECK(!ui_app.ui.view_sel && ui_app.ui.row == 0 && on_tab(TAB_KEYS));
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_LEFT);                           // PRESETS has one page: down goes to its first row
    CHECK_EQ(view(), VIEW_PRESETS);
    ev_tap(CTL_JOY_DOWN);
    CHECK(!ui_app.ui.view_sel && ui_app.ui.row == 1 && on_tab(TAB_PRESET));
}

TEST(views_page_rings_stay_in_their_view) {
    ui_fresh();
    char t[32];
    const int edit = ui_app.ui.edit_pages;
    CHECK(edit > 3 && ui_app.ui.page_count == edit + 3);               // the SEQ view's three pages come last
    for (int k = 0; k < edit; k++) ev_tap(CTL_JOY_RIGHT);               // EDIT's ring: once around, never on a SEQ page
    CHECK_EQ(ui_app.ui.page, 0);
    ev_tap(CTL_JOY_LEFT);
    CHECK_EQ(ui_app.ui.page, edit - 1);
    ui_page_title(t, (int)sizeof t);
    CHECK(strcmp(t, "PATTERNS") && strcmp(t, "SEQUENCER"));
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_DOWN);    // SEQ: SEQUENCER, SEQ SETUP, PATTERNS, around
    CHECK(synth_ui_shows_seq(&ui_app.ui));
    ev_tap(CTL_JOY_RIGHT); ui_page_title(t, (int)sizeof t); CHECK_STR(t, "SEQ SETUP");
    ev_tap(CTL_JOY_RIGHT); ui_page_title(t, (int)sizeof t); CHECK_STR(t, "PATTERNS");
    ev_tap(CTL_JOY_RIGHT); CHECK(synth_ui_shows_seq(&ui_app.ui));
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_DOWN);    // RACK: RACK and SOUND
    CHECK(on_tab(TAB_RACK));
    ev_tap(CTL_JOY_RIGHT); CHECK(on_tab(TAB_SOUND));
    ev_tap(CTL_JOY_RIGHT); CHECK(on_tab(TAB_RACK));
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_DOWN);    // PERFORM: MACROS and JOY (ADR-046 decision 3)
    CHECK(on_tab(TAB_MACROS));
    ev_tap(CTL_JOY_RIGHT); CHECK(on_tab(TAB_JOY));
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_DOWN);   // SETUP: KEYS, LEDS, MODIFIERS, CURVES, MACHINE
    static const tab_t setup[] = {TAB_KEYS, TAB_LEDS, TAB_MODS, TAB_CURVES, TAB_MACHINE, TAB_KEYS};
    for (int k = 0; k < 6; k++) { CHECK(on_tab(setup[k])); ev_tap(CTL_JOY_RIGHT); }
}

TEST(views_each_is_shown_where_it_was_left) {
    ui_fresh();
    ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_RIGHT);                       // EDIT on its third page
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_DOWN); ev_tap(CTL_JOY_RIGHT);   // SEQ on SEQ SETUP
    const int seq_setup = ui_app.ui.page;
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_LEFT);                           // back to EDIT: its third page
    CHECK(view() == VIEW_EDIT && ui_app.ui.page == 2);
    ev_tap(CTL_JOY_RIGHT);                                              // and SEQ: SEQ SETUP
    CHECK_EQ(ui_app.ui.page, seq_setup);
}

TEST(views_menu_toggles_home_and_the_last_other_view) {
    ui_fresh();
    ev_tap(MENU);                                                       // the first time: RACK, as the menu was
    CHECK(on_tab(TAB_RACK) && ui_app.ui.row == 1);
    ev_tap(MENU);
    CHECK(view() == VIEW_EDIT && ui_app.ui.row == 0);
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_RIGHT);                          // SEQ was the last other view
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_LEFT);                           // (back on EDIT by the selector)
    ev_tap(MENU);
    CHECK(view() == VIEW_SEQ && synth_ui_shows_seq(&ui_app.ui));
    ev_tap(MENU);
    CHECK_EQ(view(), VIEW_EDIT);
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_DOWN); ev_tap(CTL_JOY_LEFT);   // SETUP on MACHINE
    CHECK(on_tab(TAB_MACHINE));
    ev_tap(MENU); ev_tap(MENU);
    CHECK(on_tab(TAB_MACHINE));
}

TEST(views_back_climbs_a_row_the_header_then_home) {
    ui_fresh();
    ev_tap(CTL_JOY_DOWN);                                               // EDIT, a row: Back to the header, then nothing (home)
    CHECK_EQ(ui_app.ui.row, 1);
    ev_tap(BACK);
    CHECK(ui_app.ui.row == 0 && view() == VIEW_EDIT);
    ev_tap(BACK);
    CHECK(ui_app.ui.row == 0 && view() == VIEW_EDIT);
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_DOWN); ev_tap(CTL_JOY_RIGHT); ev_tap(CTL_JOY_DOWN);   // SEQ SETUP, row 1
    CHECK(view() == VIEW_SEQ && ui_app.ui.row == 1);
    ev_tap(BACK);
    CHECK(view() == VIEW_SEQ && ui_app.ui.row == 0);
    ev_tap(BACK);
    CHECK(view() == VIEW_EDIT && !ui_app.ui.in_rack);
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_LEFT);                           // the view selector: Back goes home at once
    CHECK(ui_app.ui.view_sel && view() == VIEW_SETUP);
    ev_tap(BACK);
    CHECK(view() == VIEW_EDIT && !ui_app.ui.view_sel);
}

TEST(views_rack_edits_are_applied_when_the_rack_view_is_left_by_the_selector) {
    ui_fresh();
    ev_tap(MENU);
    CHECK(on_tab(TAB_RACK));
    ui_app.ui.rack_dirty = true;                                        // (as an insert would)
    ev_idle(1);
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_RIGHT);      // the selector to PERFORM: still the menu, nothing applied yet
    CHECK(view() == VIEW_PERFORM && ui_app.ui.rack_dirty);
    ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_LEFT);                         // SEQ: a page, the rack's edits applied
    CHECK(view() == VIEW_SEQ && !ui_app.ui.rack_dirty);
    CHECK(popup_info_showing(&ui_app.popup));
    CHECK_STR(ui_app.popup.info.text, "Applied");
}

extern int ui_stub_card_reader;

TEST(views_machine_tab_holds_out_spk_knob_and_storage) {
    ui_fresh();
    ev_tap(CTL_JOY_UP); ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_DOWN); ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_DOWN);
    CHECK(on_tab(TAB_MACHINE) && ui_app.ui.row == 1);
    const screen_def_t *s = screen_for_tab(TAB_MACHINE);
    CHECK_EQ(s->n, 4);
    CHECK_STR(s->el[0].label, "Out"); CHECK_STR(s->el[1].label, "Spk"); CHECK_STR(s->el[2].label, "Knob"); CHECK_STR(s->el[3].label, "Storage");
    const int knob = ui_app.rack.cfg.knob_mode;
    ui_app.ui.row = 3;
    ev_turn(CTL_ENC_B, 1);                                              // Knob: Catch <-> Direct
    CHECK(ui_app.rack.cfg.knob_mode != knob);
    macro_t m;                                                          // a live setting: a learnable target, as on SOUND
    ui_app.ui.row = 2;
    CHECK(synth_ui_target_at_cursor(&ui_app.ui, &ui_app.rack, &m) && m.kind == MACRO_CFG && m.prm == CFGP_SPEAKER);
    ui_app.ui.row = 4;
    CHECK(!synth_ui_target_at_cursor(&ui_app.ui, &ui_app.rack, &m));
    const int calls = ui_stub_card_reader;
    ev_tap(CTL_JOY_SW);                                                 // Storage: asked, No first
    CHECK(popup_modal(&ui_app.popup) && ui_app.popup.queue[0].kind == POPUP_ASK && !ui_app.popup.queue[0].yes);
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui_stub_card_reader, calls);
    for (int row = 1; row <= 4; row++) { ui_app.ui.row = row; ui_app.dirty = true; ev_idle(1); }   // drawn on every row without trouble
}

TEST(views_jump_slot_to_machine_and_back_to_a_seq_page) {
    ui_fresh();
    CHECK(synth_ui_go(&ui_app.ui, &ui_app.rack, SYNTH_GO_MACHINE));
    synth_ui_jump_save(&ui_app.ui, &ui_app.rack, 0);
    CHECK(synth_ui_go(&ui_app.ui, &ui_app.rack, SYNTH_GO_PATTERNS));
    CHECK(synth_ui_where(&ui_app.ui, &ui_app.rack) == SYNTH_GO_PATTERNS && view() == VIEW_SEQ);
    synth_ui_jump_save(&ui_app.ui, &ui_app.rack, 1);
    synth_ui_go(&ui_app.ui, &ui_app.rack, SYNTH_GO_MAIN);
    CHECK_EQ(view(), VIEW_EDIT);
    synth_ui_handle(&ui_app.ui, &ui_app.params, &ui_app.seq, &ui_app.rack, UI_JUMP_1);
    CHECK(on_tab(TAB_MACHINE) && view() == VIEW_SETUP);
    synth_ui_handle(&ui_app.ui, &ui_app.params, &ui_app.seq, &ui_app.rack, UI_JUMP_2);
    CHECK(synth_ui_where(&ui_app.ui, &ui_app.rack) == SYNTH_GO_PATTERNS);
    ev_tap(MENU);                                                       // from a jump too: Menu goes home, and back to the view left
    CHECK_EQ(view(), VIEW_EDIT);
    ev_tap(MENU);
    CHECK(synth_ui_where(&ui_app.ui, &ui_app.rack) == SYNTH_GO_PATTERNS);
}

/* ---------------- the Pads section's views ---------------- */

static void to_pads(void) { app_set_section(&ui_app, true); ev_idle(1); }

TEST(views_pads_ring_from_the_top_of_the_grid_keeps_the_section) {
    ui_fresh();
    to_pads();
    ui_app.pads_run = true;                                             // the pads' music: it goes on through every view
    CHECK(app_pads_view(&ui_app) == VIEW_EDIT && !ui_app.ui.view_sel);
    ui_app.ui.pad_cur = 5;
    ev_tap(CTL_JOY_UP);                                                 // the grid's second row: up moves the pad
    CHECK(ui_app.ui.pad_cur == 1 && !ui_app.ui.view_sel);
    ev_tap(CTL_JOY_UP);                                                 // its first: the view selector
    CHECK(ui_app.ui.view_sel && ui_app.ui.pad_cur == 1);
    static const view_t ring[] = {VIEW_SEQ, VIEW_RACK, VIEW_PERFORM, VIEW_SETUP, VIEW_EDIT};
    for (int k = 0; k < 5; k++) {
        ev_tap(CTL_JOY_RIGHT);
        CHECK_EQ(app_pads_view(&ui_app), ring[k]);
        CHECK(ui_app.ui.view_sel && ui_app.section == SECTION_PADS && ui_app.pads_run);
        ui_app.dirty = true; ev_idle(1);                                // drawn without trouble
    }
    CHECK(!ui_app.ui.in_rack && !ui_app.ui.pad_fx && !ui_app.ui.pad_steps);
    ev_tap(CTL_JOY_LEFT);                                               // SETUP: a synth tab over the pads
    CHECK(ui_app.ui.in_rack && view() == VIEW_SETUP && ui_app.section == SECTION_PADS);
    ev_tap(CTL_JOY_DOWN);                                               // down: its page selector, then its rows
    CHECK(!ui_app.ui.view_sel && ui_app.ui.row == 0);
    ev_tap(CTL_JOY_RIGHT);
    CHECK(on_tab(TAB_LEDS));
    ev_tap(CTL_JOY_DOWN);
    CHECK(ui_app.ui.row >= 1 && on_tab(TAB_LEDS));
    ev_tap(BACK);                                                       // Back climbs to its header, then the Pads' home
    CHECK(ui_app.ui.row == 0 && ui_app.ui.in_rack);
    ev_tap(BACK);
    CHECK(!ui_app.ui.in_rack && app_pads_view(&ui_app) == VIEW_EDIT && ui_app.section == SECTION_PADS && ui_app.pads_run);
}

TEST(views_pads_menu_toggles_home_and_the_last_other_view) {
    ui_fresh();
    to_pads();
    ev_tap(MENU);                                                       // at first PAD FX, as Menu in Pads was
    CHECK(ui_app.ui.pad_fx && app_pads_view(&ui_app) == VIEW_RACK);
    ev_tap(MENU);
    CHECK(!ui_app.ui.pad_fx && app_pads_view(&ui_app) == VIEW_EDIT);
    ev_tap(CTL_JOY_UP);                                                 // (the first pad: the top row)
    ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_LEFT);                         // PERFORM by the selector
    CHECK(on_tab(TAB_MACROS) && ui_app.section == SECTION_PADS);
    ev_tap(CTL_JOY_DOWN); ev_tap(CTL_JOY_RIGHT);                        // its JOY tab
    CHECK(on_tab(TAB_JOY));
    ev_tap(MENU);
    CHECK(app_pads_view(&ui_app) == VIEW_EDIT && !ui_app.ui.in_rack);
    ev_tap(MENU);                                                       // back to it, on JOY
    CHECK(on_tab(TAB_JOY) && ui_app.section == SECTION_PADS);
    ev_tap(MENU);
    ui_app.ui.pad_edit = true;                                          // a pad's editor open: EDIT keeps it
    ev_tap(MENU); ev_tap(MENU);
    CHECK(ui_app.ui.pad_edit && app_pads_view(&ui_app) == VIEW_EDIT);
}

TEST(views_each_section_keeps_its_own_tab) {
    ui_fresh();
    synth_ui_go(&ui_app.ui, &ui_app.rack, SYNTH_GO_SOUND);              // the synth on SOUND
    to_pads();
    CHECK(!ui_app.ui.in_rack && app_pads_view(&ui_app) == VIEW_EDIT);  // Pads: its grid, not the synth's tab
    app_pads_tab(&ui_app, SYNTH_GO_MACHINE);                            // MACHINE over the pads
    CHECK(on_tab(TAB_MACHINE));
    app_set_section(&ui_app, false);
    CHECK(on_tab(TAB_SOUND));                                           // the synth: SOUND again
    to_pads();
    CHECK(on_tab(TAB_MACHINE) && ui_app.section == SECTION_PADS);       // Pads: MACHINE again
}

TEST(views_learn_from_a_tab_over_the_pads_goes_to_the_synths_pages) {
    ui_fresh();
    to_pads();
    app_pads_tab(&ui_app, SYNTH_GO_MACROS);
    ui_app.ui.learn_macro = 0;                                          // (as the MACROS tab's Learn button)
    ui_app.ui.learn_req = true;
    ev_idle(1);
    CHECK(ui_app.section == SECTION_SYNTH && !ui_app.ui.in_rack && view() == VIEW_EDIT && ui_app.ui.learn_wait);
}

TEST(views_knobs_over_the_pads_leave_the_pads_alone) {
    ui_fresh();
    to_pads();
    const pad_t before = ui_app.pads.pad[pads_ui_pad(&ui_app.ui)];
    app_pads_tab(&ui_app, SYNTH_GO_KEYS);
    for (int k = 0; k < 4; k++) ev_knob((control_id_t)(CTL_COL_KNOB_0 + k), 900);
    CHECK(!memcmp(&before, &ui_app.pads.pad[pads_ui_pad(&ui_app.ui)], sizeof before));
}
