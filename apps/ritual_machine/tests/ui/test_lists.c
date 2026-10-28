// The page list on Shift + Menu (app/app_lists.c; ENGINE_DESIGN.md ADR-044): MODES, MAIN, ADVANCED >, the entries following the mode.
#include "ui_test.h"

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

TEST(page_list_goes_to_the_synths_screens_and_opens_on_the_one_on_show) {
    ui_fresh();
    char t[POPUP_LABEL_LEN], all[200];
    shift_menu();
    CHECK(popup_modal(&ui_app.popup));
    entries(all, (int)sizeof all);
    CHECK_STR(all, "[MODES]|Synth|Pads|[MAIN]|Rack|Presets|Sound|Sequencer|Macros|Advanced >");
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Synth");                                              // a main view page is on show
    CHECK(ui_list_pick("Rack"));
    CHECK(on_tab(TAB_RACK) && !popup_modal(&ui_app.popup));
    shift_menu();
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Rack");                                               // it opens on the screen on show
    CHECK(ui_list_pick("Sound"));
    CHECK(on_tab(TAB_GENERAL));
    shift_menu(); CHECK(ui_list_pick("Macros"));
    CHECK(on_tab(TAB_MACROS));
    shift_menu(); CHECK(ui_list_pick("Presets"));
    CHECK(on_tab(TAB_PRESET));
    shift_menu(); CHECK(ui_list_pick("Sequencer"));
    CHECK(!ui_app.ui.in_rack && synth_ui_shows_seq(&ui_app.ui));
    shift_menu();
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Sequencer");
    // ADVANCED: a submenu; Back from it = the list again, on Advanced
    CHECK(ui_list_pick("Advanced >"));
    CHECK(popup_modal(&ui_app.popup));
    entries(all, (int)sizeof all);
    CHECK_STR(all, "Keys|LEDs|Modifiers|Joy|Curves");
    ev_tap(CTL_BTN_2);
    CHECK(popup_modal(&ui_app.popup));
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Advanced >");
    CHECK(ui_list_pick("Advanced >"));
    CHECK(ui_list_pick("Curves"));
    CHECK(on_tab(TAB_CURVES) && !popup_modal(&ui_app.popup));
    shift_menu();
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Advanced >");                                         // a settings tab on show: its submenu's entry
    CHECK(ui_list_pick("Advanced >"));
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Curves");
    CHECK(ui_list_pick("Keys"));
    CHECK(on_tab(TAB_KEYS));
    // Back on the list: nothing changes
    shift_menu();
    ev_tap(CTL_BTN_2);
    CHECK(!popup_modal(&ui_app.popup) && on_tab(TAB_KEYS));
    // Synth: the main view as it was left
    const int page = ui_app.ui.page;
    shift_menu(); CHECK(ui_list_pick("Synth"));
    CHECK(!ui_app.ui.in_rack && ui_app.ui.page == page);
}

TEST(page_list_in_pads_has_pad_fx_and_steps_and_takes_the_synths_tabs_to_the_synth) {
    ui_fresh();
    char t[POPUP_LABEL_LEN], all[200];
    shift_menu(); CHECK(ui_list_pick("Pads"));
    CHECK_EQ(ui_app.section, SECTION_PADS);
    shift_menu();
    entries(all, (int)sizeof all);
    CHECK_STR(all, "[MODES]|Synth|Pads|[MAIN]|Pad FX|Presets|Steps|Macros|Advanced >");
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Pads");
    CHECK(ui_list_pick("Pad FX"));
    CHECK(ui_app.ui.pad_fx && ui_app.section == SECTION_PADS);
    shift_menu(); CHECK(ui_list_pick("Steps"));
    CHECK(ui_app.ui.pad_steps && !ui_app.ui.pad_fx);
    shift_menu();
    focused(t, (int)sizeof t);
    CHECK_STR(t, "Steps");
    CHECK(ui_list_pick("Pads"));                                        // the grid: steps and FX closed
    CHECK(!ui_app.ui.pad_steps && !ui_app.ui.pad_fx && ui_app.section == SECTION_PADS);
    shift_menu(); CHECK(ui_list_pick("Macros"));                        // a synth tab: the synth section
    CHECK_EQ(ui_app.section, SECTION_SYNTH);
    CHECK(on_tab(TAB_MACROS));
    shift_menu(); CHECK(ui_list_pick("Pads"));
    shift_menu(); CHECK(ui_list_pick("Advanced >")); CHECK(ui_list_pick("LEDs"));
    CHECK(ui_app.section == SECTION_SYNTH && on_tab(TAB_LEDS));
    shift_menu(); CHECK(ui_list_pick("Synth"));
    CHECK(!ui_app.ui.in_rack);
}

TEST(page_list_leaving_the_rack_with_edits_rebuilds_as_menu_does) {
    ui_fresh();
    shift_menu(); CHECK(ui_list_pick("Rack"));
    ui_app.ui.rack_dirty = true;                                        // (as an insert would)
    ev_idle(1);
    shift_menu(); CHECK(ui_list_pick("Synth"));
    CHECK(!ui_app.ui.in_rack && !ui_app.ui.rack_dirty);
    shift_menu(); CHECK(ui_list_pick("Rack"));
    ui_app.ui.rack_dirty = true;
    shift_menu(); CHECK(ui_list_pick("Sequencer"));                     // a page: the same
    CHECK(!ui_app.ui.in_rack && !ui_app.ui.rack_dirty && synth_ui_shows_seq(&ui_app.ui));
}
