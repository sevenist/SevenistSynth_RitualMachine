// The map on Shift + Menu (app/app_lists.c; ENGINE_DESIGN.md ADR-044, ADR-046 phase 3): MODES, the views' headings, SETUP >, the entries
// following the section; the page list on Menu held: the pages of the view on show.
#include "ui_test.h"
#include "core/ui_screen.h"

#define SHIFT CTL_BTN_3
#define MENU  CTL_BTN_1

static void shift_menu(void) { ev_press(SHIFT); ev_tap(MENU); ev_release(SHIFT); }
static bool on_tab(tab_t t) { return ui_app.ui.in_rack && tab_kind(&ui_app.rack, ui_app.ui.menu_tab) == t; }

// The focused entry's text.
static void focused(char *out, int n) {
    const popup_msg_t *m = &ui_app.popup.queue[0];
    m->label(m->ctx, m->focus, out, n);
}

// Every entry's text, joined with '|', headings in brackets.
static void entries(char *out, int n) {
    const popup_msg_t *m = &ui_app.popup.queue[0];
    out[0] = 0;
    for (int i = 0; i < m->n_options; i++) {
        char t[POPUP_LABEL_LEN], e[POPUP_LABEL_LEN + 4];
        const bool head = m->label(m->ctx, i, t, (int)sizeof t);
        snprintf(e, sizeof e, head ? "%s[%s]" : "%s%s", i ? "|" : "", t);
        strncat(out, e, (size_t)n - strlen(out) - 1);
    }
}

TEST(map_goes_to_the_synths_screens_grouped_by_view_and_opens_on_the_one_on_show) {
    ui_fresh();
    char t[POPUP_LABEL_LEN], all[300];
    shift_menu();
    CHECK(popup_modal(&ui_app.popup));
    CHECK_STR(ui_app.popup.queue[0].title, "MAP");
    entries(all, (int)sizeof all);
    CHECK_STR(all, "[MODES]|Synth|Pads|[EDIT]|Edit|[SEQ]|Sequencer|Seq setup|Patterns|[RACK]|Rack|Sound|[PERFORM]|Macros|Joy|"
                   "[PRESETS]|Presets|[SETUP]|Setup >");
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Edit");                                               // an EDIT page is on show
    CHECK(ui_list_pick("Rack"));
    CHECK(on_tab(TAB_RACK) && !popup_modal(&ui_app.popup));
    shift_menu();
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Rack");                                               // it opens on the screen on show
    CHECK(ui_list_pick("Sound"));
    CHECK(on_tab(TAB_SOUND));
    shift_menu(); CHECK(ui_list_pick("Macros"));
    CHECK(on_tab(TAB_MACROS));
    shift_menu(); CHECK(ui_list_pick("Joy"));                           // JOY: PERFORM, beside MACROS (ADR-046 decision 3)
    CHECK(on_tab(TAB_JOY));
    shift_menu(); CHECK(ui_list_pick("Presets"));
    CHECK(on_tab(TAB_PRESET));
    shift_menu(); CHECK(ui_list_pick("Sequencer"));
    CHECK(!ui_app.ui.in_rack && synth_ui_shows_seq(&ui_app.ui));
    shift_menu(); CHECK(ui_list_pick("Patterns"));
    CHECK(synth_ui_where(&ui_app.ui, &ui_app.rack) == SYNTH_GO_PATTERNS);
    shift_menu(); CHECK(ui_list_pick("Seq setup"));
    CHECK(synth_ui_where(&ui_app.ui, &ui_app.rack) == SYNTH_GO_SEQ_SETUP);
    shift_menu();
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Seq setup");
    // SETUP: a submenu; Back from it = the map again, on Setup >
    CHECK(ui_list_pick("Setup >"));
    CHECK(popup_modal(&ui_app.popup));
    CHECK_STR(ui_app.popup.queue[0].title, "SETUP");
    entries(all, (int)sizeof all);
    CHECK_STR(all, "Keys|LEDs|Modifiers|Curves|Machine|Storage");
    ev_tap(CTL_BTN_2);
    CHECK(popup_modal(&ui_app.popup));
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Setup >");
    CHECK(ui_list_pick("Setup >"));
    CHECK(ui_list_pick("Curves"));
    CHECK(on_tab(TAB_CURVES) && !popup_modal(&ui_app.popup));
    shift_menu();
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Setup >");                                            // a setting on show: its submenu's entry
    CHECK(ui_list_pick("Setup >"));
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Curves");
    CHECK(ui_list_pick("Machine"));
    CHECK(on_tab(TAB_MACHINE));
    shift_menu(); CHECK(ui_list_pick("Setup >")); CHECK(ui_list_pick("Keys"));
    CHECK(on_tab(TAB_KEYS));
    // Back on the map: nothing changes
    shift_menu();
    ev_tap(CTL_BTN_2);
    CHECK(!popup_modal(&ui_app.popup) && on_tab(TAB_KEYS));
    // Edit: the EDIT view as it was left; Synth in the synth: nothing moves
    const int page = ui_app.ui.view_at[VIEW_EDIT];
    shift_menu(); CHECK(ui_list_pick("Edit"));
    CHECK(!ui_app.ui.in_rack && ui_app.ui.page == page && synth_ui_view(&ui_app.ui, &ui_app.rack) == VIEW_EDIT);
    shift_menu(); CHECK(ui_list_pick("Synth"));
    CHECK(!ui_app.ui.in_rack && ui_app.ui.page == page);
}

TEST(map_in_pads_has_grid_steps_pad_fx_and_takes_the_synths_tabs_to_the_synth) {
    ui_fresh();
    char t[POPUP_LABEL_LEN], all[300];
    shift_menu(); CHECK(ui_list_pick("Pads"));
    CHECK_EQ(ui_app.section, SECTION_PADS);
    shift_menu();
    entries(all, (int)sizeof all);
    CHECK_STR(all, "[MODES]|Synth|Pads|[EDIT]|Grid|[SEQ]|Steps|[RACK]|Pad FX|[PERFORM]|Macros|Joy|[SETUP]|Setup >");   // no PRESETS heading
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Grid");
    CHECK(ui_list_pick("Pad FX"));
    CHECK(ui_app.ui.pad_fx && ui_app.section == SECTION_PADS);
    shift_menu(); CHECK(ui_list_pick("Steps"));
    CHECK(ui_app.ui.pad_steps && !ui_app.ui.pad_fx);
    shift_menu();
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Steps");
    CHECK(ui_list_pick("Grid"));                                        // the grid: steps and FX closed
    CHECK(!ui_app.ui.pad_steps && !ui_app.ui.pad_fx && ui_app.section == SECTION_PADS);
    shift_menu(); CHECK(ui_list_pick("Steps"));
    shift_menu(); CHECK(ui_list_pick("Pads"));                          // MODES Pads: the grid too
    CHECK(!ui_app.ui.pad_steps && ui_app.section == SECTION_PADS);
    shift_menu(); CHECK(ui_list_pick("Macros"));                        // a synth tab: the synth section
    CHECK_EQ(ui_app.section, SECTION_SYNTH);
    CHECK(on_tab(TAB_MACROS));
    shift_menu(); CHECK(ui_list_pick("Pads"));
    shift_menu(); CHECK(ui_list_pick("Setup >")); CHECK(ui_list_pick("LEDs"));
    CHECK(ui_app.section == SECTION_SYNTH && on_tab(TAB_LEDS));
    shift_menu(); CHECK(ui_list_pick("Pads"));
    shift_menu(); CHECK(ui_list_pick("Synth"));                         // the synth as it was left: LEDS
    CHECK(ui_app.section == SECTION_SYNTH && on_tab(TAB_LEDS));
}

TEST(map_leaving_the_rack_with_edits_rebuilds_as_menu_does) {
    ui_fresh();
    shift_menu(); CHECK(ui_list_pick("Rack"));
    ui_app.ui.rack_dirty = true;                                        // (as an insert would)
    ev_idle(1);
    shift_menu(); CHECK(ui_list_pick("Edit"));
    CHECK(!ui_app.ui.in_rack && !ui_app.ui.rack_dirty);
    shift_menu(); CHECK(ui_list_pick("Rack"));
    ui_app.ui.rack_dirty = true;
    shift_menu(); CHECK(ui_list_pick("Sequencer"));                     // a page: the same
    CHECK(!ui_app.ui.in_rack && !ui_app.ui.rack_dirty && synth_ui_shows_seq(&ui_app.ui));
}

/* ---------------- the page list: Menu held ---------------- */

// Menu held until the page list is up (or 200 steps); the release follows.
static void menu_hold(void) {
    ev_press(MENU);
    for (int k = 0; k < 200 && !popup_modal(&ui_app.popup); k++) ev_idle(1);
    ev_release(MENU);
}

TEST(page_list_on_a_long_menu_press_lists_the_pages_of_the_view_on_show) {
    ui_fresh();
    char t[POPUP_LABEL_LEN], want[24];
    ev_tap(MENU);                                                       // a short press: the last other view (RACK), at the release
    CHECK(ui_app.ui.in_rack && !popup_modal(&ui_app.popup));
    ev_tap(MENU);
    CHECK(!ui_app.ui.in_rack);
    ev_press(MENU);
    ev_idle(2);
    CHECK(!ui_app.ui.in_rack && !popup_modal(&ui_app.popup));           // held: nothing yet
    ev_release(MENU);
    CHECK(ui_app.ui.in_rack);
    ev_tap(MENU);
    ui_app.ui.page = 2;
    menu_hold();                                                        // EDIT: its pages, none of SEQ's
    CHECK(popup_modal(&ui_app.popup) && !ui_app.ui.in_rack);           // the release after the list did nothing
    CHECK_STR(ui_app.popup.queue[0].title, "EDIT");
    CHECK_EQ(ui_app.popup.queue[0].n_options, ui_app.ui.edit_pages);
    CHECK_EQ(ui_app.popup.queue[0].focus, 2);                           // on the page on show
    focused(t, (int)sizeof t);
    synth_ui_page_title(&ui_app.ui, &ui_app.rack, 2, want, (int)sizeof want);
    CHECK_STR(t, want);
    CHECK(!ui_list_pick("SEQUENCER"));
    CHECK(ui_list_pick("AMP ENV"));
    CHECK(!popup_modal(&ui_app.popup) && ui_app.ui.row == 1);
    ui_page_title(t, (int)sizeof t);
    CHECK_STR(t, "AMP ENV");
    // Back: nothing changes
    const int page = ui_app.ui.page;
    menu_hold();
    ev_tap(CTL_BTN_2);
    CHECK(!popup_modal(&ui_app.popup) && ui_app.ui.page == page);
    // SEQ: its three pages
    shift_menu(); CHECK(ui_list_pick("Sequencer"));
    menu_hold();
    CHECK_STR(ui_app.popup.queue[0].title, "SEQ");
    CHECK_EQ(ui_app.popup.queue[0].n_options, 3);
    CHECK(ui_list_pick("PATTERNS"));
    CHECK(synth_ui_where(&ui_app.ui, &ui_app.rack) == SYNTH_GO_PATTERNS);
    // a view of tabs: its tabs, the pick stays in it
    shift_menu(); CHECK(ui_list_pick("Rack"));
    menu_hold();
    CHECK_STR(ui_app.popup.queue[0].title, "RACK");
    CHECK_EQ(ui_app.popup.queue[0].n_options, 2);
    CHECK_EQ(ui_app.popup.queue[0].focus, 0);
    CHECK(ui_list_pick("SOUND"));
    CHECK(on_tab(TAB_SOUND) && ui_app.ui.row == 1);
    shift_menu(); CHECK(ui_list_pick("Setup >")); CHECK(ui_list_pick("Keys"));
    menu_hold();
    CHECK_STR(ui_app.popup.queue[0].title, "SETUP");
    CHECK_EQ(ui_app.popup.queue[0].n_options, 5);
    CHECK(ui_list_pick("MACHINE"));
    CHECK(on_tab(TAB_MACHINE));
}

// In Pads there is no jump list: a long press is a short one, the FX chains (user, 2026-10-29: "the long press on menu shouldn't
// show synth parts in pad mode").
TEST(no_jump_list_in_pads_a_long_press_is_the_fx_as_a_short_one) {
    ui_fresh();
    shift_menu(); CHECK(ui_list_pick("Pads"));
    ev_tap(MENU);
    CHECK(ui_app.ui.pad_fx);                                            // short: the FX chains, as before
    ev_tap(MENU);
    CHECK(!ui_app.ui.pad_fx);
    ev_press(MENU);
    ev_idle(100);                                                       // well past MENU_LONG_MS
    CHECK(!popup_modal(&ui_app.popup) && !ui_app.ui.pad_fx);
    ev_release(MENU);
    CHECK(!popup_modal(&ui_app.popup) && ui_app.ui.pad_fx && ui_app.section == SECTION_PADS);
}

extern int ui_stub_card_reader;

TEST(map_storage_asks_then_saves_and_restarts_as_the_card_reader) {
    ui_fresh();
    const int calls = ui_stub_card_reader;
    shift_menu(); CHECK(ui_list_pick("Setup >")); CHECK(ui_list_pick("Storage"));
    CHECK(popup_modal(&ui_app.popup) && ui_app.popup.queue[0].kind == POPUP_ASK && !ui_app.popup.queue[0].yes);   // asked, No first
    ev_tap(CTL_JOY_SW);
    CHECK(!popup_modal(&ui_app.popup));
    CHECK_EQ(ui_stub_card_reader, calls);                              // No: nothing
    shift_menu(); CHECK(ui_list_pick("Setup >")); CHECK(ui_list_pick("Storage"));
    ev_tap(CTL_JOY_LEFT);                                               // Yes
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui_stub_card_reader, calls + 1);
    CHECK(popup_info_showing(&ui_app.popup));                           // the tests' platform has none: it says so
    CHECK_STR(ui_app.popup.info.text, "No card reader on this platform");
}

/* ---------------- the SOUND tab: GENERAL + SAMPLES or ALGORITHM (ADR-044) ---------------- */

// The labels of the SOUND tab's rows the focus reaches, from the first, joined with '|' (encoder A down, as by hand).
static void sound_rows(char *out, int n) {
    out[0] = 0;
    ui_app.ui.row = 1;
    const screen_def_t *s = screen_for_tab(TAB_SOUND);
    for (int k = 0; k < 20; k++) {
        const el_def_t *e = &s->el[ui_app.ui.row - 1];
        const ui_ctx_t c = {&ui_app.ui, &ui_app.rack, e->arg};
        const char *l = e->label_dyn ? e->label_dyn(&c) : e->label;
        if (out[0]) strncat(out, "|", (size_t)n - strlen(out) - 1);
        strncat(out, l, (size_t)n - strlen(out) - 1);
        const int was = ui_app.ui.row;
        ev_turn(CTL_ENC_A, 1);
        if (ui_app.ui.row <= was) break;                                // around: the list is done
    }
}

TEST(sound_tab_holds_the_settings_then_the_sample_or_the_algorithm_by_synth_type) {
    ui_fresh();
    char all[200];
    CHECK_EQ(tab_count(&ui_app.rack), 10);
    CHECK_EQ(tab_kind(&ui_app.rack, 1), TAB_SOUND);                    // the RACK view's second (ADR-046 phase 3)
    CHECK_EQ(tab_index_of(&ui_app.rack, TAB_GENERAL), 1);              // a jump slot saved on GENERAL, SAMPLES or ALGORITHM lands on SOUND
    CHECK_EQ(tab_index_of(&ui_app.rack, TAB_SAMPLES), 1);
    CHECK_EQ(tab_index_of(&ui_app.rack, TAB_FM_ALGO), 1);
    CHECK_STR(tab_name(TAB_SOUND), "SOUND");
    shift_menu(); CHECK(ui_list_pick("Sound"));
    CHECK(on_tab(TAB_SOUND));
    sound_rows(all, (int)sizeof all);                                   // Modular: the sample rows
    CHECK(strstr(all, "Vol|File|Tgt|Assign") != NULL);                // (Out, Spk, Knob: SETUP > MACHINE)
    CHECK(strstr(all, "Knob") == NULL && strstr(all, "Spk") == NULL);
    CHECK(strstr(all, "Algo") == NULL);
    ui_set_type(SYNTH_FM);
    ev_tap(MENU);
    shift_menu(); CHECK(ui_list_pick("Sound"));
    sound_rows(all, (int)sizeof all);                                   // FM: the algorithm's
    CHECK(strstr(all, "Vol|Algo|Fb|Op") != NULL);
    CHECK(strstr(all, "File") == NULL);
    ui_set_type(SYNTH_STRINGS);
    sound_rows(all, (int)sizeof all);                                   // Strings: the settings only
    CHECK(strstr(all, "Vol") != NULL && strstr(all, "File") == NULL && strstr(all, "Algo") == NULL);
    // the picture follows the part: drawn on each without trouble
    ui_set_type(SYNTH_MODULAR);
    for (int row = 1; row <= 12; row++) { ui_app.ui.row = row; ui_app.dirty = true; ev_idle(1); }
}
