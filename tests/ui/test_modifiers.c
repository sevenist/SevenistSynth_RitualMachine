// The Shift / Mod layers and the MODIFIERS tab (core/modifiers.h, scr_mods.c, app.c).
#include "ui_test.h"

#define SHIFT CTL_BTN_3
#define MOD   CTL_KEY(1, 0)                 // A1, given the Mod function by mod_key()

static void mod_key(void) { keymap_set(1 * KEY_COLS + 0, (key_fn_t){ACT_MOD, 0}); }

static bool same_entry(mod_entry_t a, mod_entry_t b) {
    if (a.kind != b.kind) return false;
    if (a.kind == ME_ACTION) return a.act == b.act && a.arg == b.arg;
    if (a.kind == ME_PARAM) return a.m.kind == b.m.kind && a.m.id == b.m.id && a.m.prm == b.m.prm;
    return true;
}

TEST(modifiers_shift_starting_entries) {
    ui_fresh();
    const mod_entry_t k1 = modifiers_get(MODL_SHIFT, CTL_COL_KNOB_0), k2 = modifiers_get(MODL_SHIFT, CTL_COL_KNOB_1);
    CHECK(k1.kind == ME_PARAM && k1.m.kind == MACRO_CFG && k1.m.prm == CFGP_SPEAKER);
    CHECK(k2.kind == ME_PARAM && k2.m.kind == MACRO_CFG && k2.m.prm == CFGP_VOLUME);
    CHECK(modifiers_get(MODL_SHIFT, CTL_ENC_A).act == ACT_PAGE_MOVE);
    CHECK(modifiers_get(MODL_SHIFT, CTL_JOY_UP).act == ACT_OCTAVE);
    CHECK(modifiers_get(MODL_SHIFT, CTL_KNOB_R1).act == ACT_MACRO_LEARN);
    CHECK(modifiers_get(MODL_SHIFT, CTL_COL_KNOB_2).kind == ME_DEFAULT);           // the simulator has R knobs: K3 stays free
    for (int c = 1; c < CTL_COUNT; c++) CHECK(modifiers_get(MODL_MOD, (control_id_t)c).kind == ME_DEFAULT);
}

TEST(modifiers_shift_knobs_drive_volume_and_speaker) {
    ui_fresh();
    ui_app.rack.cfg.knob_mode = 1;                                   // Direct: no catch
    ev_press(SHIFT);
    const float vol0 = ui_app.rack.cfg.volume;
    ev_knob(CTL_COL_KNOB_1, INPUT_VALUE_MAX);
    CHECK(ui_app.rack.cfg.volume > vol0);
    ev_knob(CTL_COL_KNOB_0, 0);
    CHECK_EQ(ui_app.rack.cfg.speaker, 0);
    ev_knob(CTL_COL_KNOB_2, 500);
    CHECK_STR(ui_app.popup.info.text, "No target");
    ev_release(SHIFT);
}

TEST(modifiers_mod_matches_shift) {
    ui_fresh();
    mod_key();
    ev_press(MOD);
    CHECK(ui_app.in.mod);
    ev_knob(CTL_COL_KNOB_2, 700);                                     // Default on a col knob: the same popup as Shift
    CHECK_STR(ui_app.popup.info.title, "Mod+K3");
    CHECK_STR(ui_app.popup.info.text, "No target");
    ev_release(MOD);
    CHECK(!ui_app.in.mod);
    // a jump key saves with Mod as with Shift
    keymap_set(4 * KEY_COLS + 3, (key_fn_t){ACT_JUMP, 2});
    ui_app.ui.page = 1; ui_app.ui.row = 1;
    ev_press(MOD); ev_tap(CTL_KEY(4, 3)); ev_release(MOD);
    CHECK(ui_app.ui.jump[2].valid);
}

TEST(modifiers_layer_action_overrides_the_table) {
    ui_fresh();
    mod_key();
    modifiers_set(MODL_MOD, CTL_ENC_A, (mod_entry_t){ME_ACTION, ACT_PAGE_MOVE, 1, {0, 0, 0}});
    const int page0 = ui_app.ui.page, row0 = ui_app.ui.row;
    ev_press(MOD); ev_turn(CTL_ENC_A, 1); ev_release(MOD);
    CHECK_EQ(ui_app.ui.page, page0 + 1);
    CHECK_EQ(ui_app.ui.row, row0);
    ev_turn(CTL_ENC_A, 1);                                            // without a modifier: rows
    CHECK_EQ(ui_app.ui.row, row0 + 1);
    ev_press(SHIFT); ev_turn(CTL_ENC_A, 1); ev_release(SHIFT);        // Shift's starting entry: pages
    CHECK_EQ(ui_app.ui.page, page0 + 2);
}

TEST(modifiers_key_entry_overrides_its_layout_function) {
    ui_fresh();
    keymap_set(4 * KEY_COLS + 3, (key_fn_t){ACT_JUMP, 2});
    ev_press(SHIFT); ev_tap(CTL_KEY(4, 3)); ev_release(SHIFT);
    CHECK(ui_app.ui.jump[2].valid);                                    // Default: Shift + jump key saves
    modifiers_set(MODL_SHIFT, CTL_KEY(4, 3), (mod_entry_t){ME_ACTION, ACT_OCTAVE, 1, {0, 0, 0}});
    const int oct = ui_app.in.octave;
    ev_press(SHIFT); ev_tap(CTL_KEY(4, 3)); ev_release(SHIFT);
    CHECK_EQ(ui_app.in.octave, oct + 1);
}

TEST(modifiers_learn_chord_assigns_the_row_under_the_cursor) {
    ui_fresh();
    mod_key();
    ui_app.ui.page = 0; ui_app.ui.row = 1; ui_app.ui.mods_layer = MODL_SHIFT;
    page_t pg;
    get_page(&ui_app.ui, &ui_app.rack, 0, &pg);
    ev_press(SHIFT); ev_press(MOD);
    ev_knob(CTL_COL_KNOB_2, 300);
    ev_release(MOD);
    const mod_entry_t e = modifiers_get(MODL_SHIFT, CTL_COL_KNOB_2);
    CHECK(e.kind == ME_PARAM && e.m.kind == MACRO_MODULE && e.m.id == ui_app.rack.slot[pg.slot].id && e.m.prm == pg.params[0]);
    const float before = ui_app.rack.slot[pg.slot].v[pg.params[0]];
    ev_knob(CTL_COL_KNOB_2, 0);
    ev_knob(CTL_COL_KNOB_2, INPUT_VALUE_MAX);
    CHECK(ui_app.rack.slot[pg.slot].v[pg.params[0]] != before);     // Shift + K3 drives it now (caught on the way)
    ev_release(SHIFT);
    ev_idle(2);
    CHECK_EQ(ui_app.ui.knob_away, 0);
}

TEST(modifiers_shift_r1_learns_macro_1) {
    ui_fresh();
    ui_app.ui.page = 0; ui_app.ui.row = 2;
    page_t pg;
    get_page(&ui_app.ui, &ui_app.rack, 0, &pg);
    ev_press(SHIFT); ev_knob(CTL_KNOB_R1, 400); ev_release(SHIFT);
    CHECK(ui_app.ui.macro[0].n == 2 && ui_app.ui.macro[0].dest[1].t.kind == MACRO_MODULE && ui_app.ui.macro[0].dest[1].t.prm == pg.params[1]);
}

TEST(modifiers_tab_selects_and_learn_button_assigns_on_push) {
    ui_fresh();
    ev_tap(CTL_BTN_1);
    CHECK(ui_app.ui.in_rack);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_MODS);
    ui_app.ui.row = 0;
    ev_idle(1);
    CHECK(synth_ui_on_mods_tab(&ui_app.ui, &ui_app.rack));
    ev_tap(CTL_KEY(2, 1));
    CHECK_EQ(ui_app.ui.mods_ctl, CTL_KEY(2, 1));                       // a key press selects the key
    ev_knob(CTL_COL_KNOB_3, 100);
    CHECK_EQ(ui_app.ui.mods_ctl, CTL_COL_KNOB_3);                      // a K4 turn selects K4
    ui_app.ui.row = 7;                                                  // Learn
    ev_tap(CTL_JOY_SW);
    ev_idle(1);
    CHECK(!ui_app.ui.in_rack && ui_app.ui.learn_wait);
    ui_app.ui.page = 0; ui_app.ui.row = 2;
    page_t pg;
    get_page(&ui_app.ui, &ui_app.rack, 0, &pg);
    ev_tap(CTL_JOY_SW);
    const mod_entry_t e = modifiers_get(MODL_SHIFT, CTL_COL_KNOB_3);
    CHECK(!ui_app.ui.learn_wait && e.kind == ME_PARAM && e.m.prm == pg.params[1]);
    CHECK(!ui_app.ui.latched);
}

TEST(modifiers_learn_button_back_cancels) {
    ui_fresh();
    ui_app.ui.mods_ctl = CTL_COL_KNOB_3;
    ui_app.ui.learn_req = true;
    ev_idle(1);
    CHECK(ui_app.ui.learn_wait);
    ev_tap(CTL_BTN_2);
    CHECK(!ui_app.ui.learn_wait);
    CHECK(modifiers_get(MODL_SHIFT, CTL_COL_KNOB_3).kind == ME_DEFAULT);
}

TEST(modifiers_func_list_keeps_the_settings) {
    ui_fresh();
    const mod_entry_t k1 = modifiers_get(MODL_SHIFT, CTL_COL_KNOB_0);
    const int i = modifiers_fn_index(CTL_COL_KNOB_0, k1);
    CHECK(i > 0);                                                       // Spk is in K1's list: stepping away and back finds it
    CHECK(same_entry(modifiers_fn_at(CTL_COL_KNOB_0, i), k1));
    for (int j = 0; j < modifiers_fn_count(CTL_ENC_A); j++)             // every entry of the list is found at its own index
        CHECK_EQ(modifiers_fn_index(CTL_ENC_A, modifiers_fn_at(CTL_ENC_A, j)), j);
    for (int j = 0; j < modifiers_fn_count(CTL_KEY(2, 2)); j++)
        CHECK_EQ(modifiers_fn_index(CTL_KEY(2, 2), modifiers_fn_at(CTL_KEY(2, 2), j)), j);
}

TEST(modifiers_ui_cfg_round_trip) {
    ui_fresh();
    modifiers_set(MODL_SHIFT, CTL_JOY_UP, (mod_entry_t){ME_DEFAULT, ACT_NONE, 0, {0, 0, 0}});            // a cleared starting entry
    modifiers_set(MODL_MOD, CTL_ENC_A, (mod_entry_t){ME_ACTION, ACT_PAGE_MOVE, 1, {0, 0, 0}});
    modifiers_set(MODL_MOD, CTL_KEY(3, 2), (mod_entry_t){ME_ACTION, ACT_NOTE, 7, {0, 0, 0}});
    modifiers_set(MODL_SHIFT, CTL_COL_KNOB_3, (mod_entry_t){ME_PARAM, ACT_NONE, 0, {MACRO_MODULE, ui_app.rack.slot[0].id, 1}});
    modifiers_set(MODL_MOD, CTL_COL_KNOB_0, (mod_entry_t){ME_PARAM, ACT_NONE, 0, {MACRO_FM, 2, DXV_EG_T3}});
    mod_entry_t before[MODL_COUNT][CTL_COUNT];
    for (int l = 0; l < MODL_COUNT; l++) for (int c = 1; c < CTL_COUNT; c++) before[l][c] = modifiers_get(l, (control_id_t)c);
    static char txt[STORAGE_FILE_MAX];
    ui_settings_to_text(&ui_app.ui, &ui_app.rack, txt, sizeof txt);
    CHECK(strstr(txt, "shift joyu act default") != NULL);
    CHECK(strstr(txt, "mod k1 param fm 2 10") != NULL);
    modifiers_init();
    CHECK(ui_settings_from_text(&ui_app.ui, &ui_app.rack, txt));
    int diff = 0;
    for (int l = 0; l < MODL_COUNT; l++) for (int c = 1; c < CTL_COUNT; c++) diff += !same_entry(before[l][c], modifiers_get(l, (control_id_t)c));
    CHECK_EQ(diff, 0);
}

TEST(modifiers_ui_cfg_first_format_still_loads) {
    ui_fresh();
    modifiers_set(MODL_SHIFT, CTL_COL_KNOB_2, (mod_entry_t){ME_ACTION, ACT_ROW_MOVE, 1, {0, 0, 0}});
    CHECK(ui_settings_from_text(&ui_app.ui, &ui_app.rack, "knob catch\nshift 1 none\nshift 2 cfg spk\n"));
    const mod_entry_t k1 = modifiers_get(MODL_SHIFT, CTL_COL_KNOB_0), k2 = modifiers_get(MODL_SHIFT, CTL_COL_KNOB_1);
    CHECK(k1.kind == ME_DEFAULT);
    CHECK(k2.kind == ME_PARAM && k2.m.kind == MACRO_CFG && k2.m.prm == CFGP_SPEAKER);
    CHECK(modifiers_get(MODL_SHIFT, CTL_COL_KNOB_2).kind == ME_ACTION);   // the other entries are kept
}

TEST(modifiers_saved_by_themselves_and_loaded_at_start) {
    ui_stub_ramcard(true);
    ui_fresh();
    modifiers_set(MODL_MOD, CTL_ENC_B, (mod_entry_t){ME_ACTION, ACT_OCTAVE, 1, {0, 0, 0}});
    ev_idle(400);                                                          // the stub clock runs 10 ms per step: past the 2 s autosave
    ui_fresh();                                                             // "power cycle" with the same card
    const mod_entry_t e = modifiers_get(MODL_MOD, CTL_ENC_B);
    CHECK(e.kind == ME_ACTION && e.act == ACT_OCTAVE);
}

TEST(modifiers_deleted_module_target_is_pruned) {
    ui_fresh();
    modifiers_set(MODL_MOD, CTL_KNOB_R1, (mod_entry_t){ME_PARAM, ACT_NONE, 0, {MACRO_MODULE, 200, 1}});   // no module 200
    synth_ui_rebuild_pages(&ui_app.ui, &ui_app.rack);
    CHECK(modifiers_get(MODL_MOD, CTL_KNOB_R1).kind == ME_DEFAULT);
}

TEST(modifiers_prototype_gets_macros_on_k3_k4) {
    ui_board(true);
    ui_fresh();
    const mod_entry_t k3 = modifiers_get(MODL_SHIFT, CTL_COL_KNOB_2);
    CHECK(k3.kind == ME_ACTION && k3.act == ACT_MACRO && k3.arg == 0);
    CHECK(modifiers_get(MODL_SHIFT, CTL_COL_KNOB_3).act == ACT_MACRO);
    CHECK(modifiers_ctl_index(CTL_KNOB_R1) < 0);
    CHECK(modifiers_ctl_index(CTL_COL_KNOB_0) >= 0);
    const int slot = rack_find(&ui_app.rack, ui_app.ui.macro[0].dest[0].t.id);
    CHECK(slot != RACK_NONE);
    const float before = ui_app.rack.slot[slot].v[ui_app.ui.macro[0].dest[0].t.prm];
    ev_press(SHIFT); ev_idle(1);
    CHECK(ui_app.ui.knob_away & 4);
    for (int v = 0; v <= INPUT_VALUE_MAX; v += 64) ev_knob(CTL_COL_KNOB_2, v);
    ev_knob(CTL_COL_KNOB_2, INPUT_VALUE_MAX);
    CHECK(ui_app.rack.slot[slot].v[ui_app.ui.macro[0].dest[0].t.prm] != before);
    ev_release(SHIFT);
    ui_board(false);
}

TEST(modifiers_shift_note_latches_until_pressed_again) {
    ui_fresh();
    const control_id_t f1 = CTL_KEY(0, 0), note = CTL_KEY(4, 0), other = CTL_KEY(4, 1);   // F1 = Shift in the built-in layouts
    ev_press(f1); ev_press(note); ev_release(note); ev_release(f1);
    CHECK(ui_app.in.held[note] != 0);                                           // Shift + note: still sounding after both are released
    ev_press(other); ev_release(other);
    CHECK(ui_app.in.held[other] == 0);                                          // a plain note is not latched
    CHECK(ui_app.in.held[note] != 0);
    ev_press(note);
    CHECK(ui_app.in.held[note] == 0);                                           // the next press releases the latched note
    ev_release(note);
    CHECK(ui_app.in.held[note] == 0);
    ev_press(note);
    CHECK(ui_app.in.held[note] != 0);                                           // and the key plays normally again
    ev_release(note);
    CHECK(ui_app.in.held[note] == 0);
    ev_press(f1); ev_tap(note); ev_tap(other); ev_release(f1);                  // two latched notes
    CHECK(ui_app.in.held[note] != 0 && ui_app.in.held[other] != 0);
    const bool in_rack = ui_app.ui.in_rack;
    ev_press(SHIFT); ev_tap(CTL_BTN_2); ev_release(SHIFT);                      // Shift + Back: release all, and no Back on the screen
    CHECK(ui_app.in.held[note] == 0 && ui_app.in.held[other] == 0);
    CHECK_STR(ui_app.popup.info.text, "Released 2");
    CHECK(ui_app.ui.in_rack == in_rack);
    ev_tap(note);
    CHECK(ui_app.in.held[note] == 0);                                           // no latch left on the key
    mod_key();                                                                  // Mod + note: no latch (Shift only, the user)
    ev_press(MOD); ev_press(note); ev_release(note); ev_release(MOD);
    CHECK(ui_app.in.held[note] == 0);
}
