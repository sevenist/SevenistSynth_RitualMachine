// The FM editor like the rack: operator pages in the main view, the ALGORITHM tab's operator tree, exact knob values, FM targets.
#include "ui_test.h"

#define SHIFT CTL_BTN_3

static dx7_patch_t *fm(void) { return &ui_app.rack.cfg.fm; }

TEST(fm_pages_one_per_operator_like_modules) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    char t[24];
    CHECK(ui_find_page(GLOBAL_PAGE, GP_FM_OP_BASE) >= 0);
    for (int k = 0; k < DX7_OPS; k++) {
        const int op = ui_find_page(GLOBAL_PAGE, GP_FM_OP_BASE + k), env = ui_find_page(GLOBAL_PAGE, GP_FM_ENV_BASE + k);
        CHECK(op >= 0 && env == op + 1);                                    // OPn then OPn ENV
        ui_app.ui.page = op;
        ui_page_title(t, sizeof t);
        char want[12];
        snprintf(want, sizeof want, "OP%d", k + 1);
        CHECK_STR(t, want);
    }
    ui_app.ui.page = 0;
    ui_page_title(t, sizeof t);
    CHECK_STR(t, "FM SYNTH");
    ui_set_type(SYNTH_MODULAR);
    CHECK(ui_find_page(GLOBAL_PAGE, GP_FM_OP_BASE) < 0);                    // a rack type has no operator pages
}

TEST(fm_menu_tabs_without_operator_and_envelope) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    CHECK(tab_index_of(&ui_app.rack, TAB_FM_ALGO) >= 0);
    CHECK(tab_index_of(&ui_app.rack, TAB_FM_OP) < 0);
    CHECK(tab_index_of(&ui_app.rack, TAB_FM_ENV) < 0);
    CHECK(tab_index_of(&ui_app.rack, TAB_MODS) >= 0);
}

TEST(fm_tree_push_opens_the_operator_page) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    ev_tap(CTL_BTN_1);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_FM_ALGO);
    ui_app.ui.row = 3;                                                      // Op
    ui_app.ui.fm_op = 0;
    ev_tap(CTL_JOY_RIGHT);                                                  // the selector: next operator
    CHECK_EQ(ui_app.ui.fm_op, 1);
    ev_tap(CTL_JOY_SW);                                                     // push: the operator's page
    CHECK(!ui_app.ui.in_rack);
    CHECK_EQ(ui_app.ui.page, ui_find_page(GLOBAL_PAGE, GP_FM_OP_BASE + 1));
    CHECK_EQ(ui_app.ui.row, 1);
}

TEST(fm_page_rows_edit_the_patch) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    ui_app.ui.page = ui_find_page(GLOBAL_PAGE, GP_FM_OP_BASE + 2);
    ui_app.ui.row = 1;                                                      // Lvl of operator 3
    fm()->op[2].level = 50;
    ev_turn(CTL_ENC_B, 3);
    CHECK_EQ(fm()->op[2].level, 53);
    ui_app.ui.page = ui_find_page(GLOBAL_PAGE, GP_FM_ENV_BASE + 2);
    ui_app.ui.row = 1;                                                      // Pt
    ui_app.ui.fm_pt = 0;
    ev_turn(CTL_ENC_B, 2);
    CHECK_EQ(ui_app.ui.fm_pt, 2);
    ui_app.ui.row = 2;                                                      // Lvl of point 3
    fm()->op[2].eg_l[2] = 40;
    ev_turn(CTL_ENC_B, -1);
    CHECK_EQ(fm()->op[2].eg_l[2], 39);
    ui_app.ui.page = 0; ui_app.ui.row = 2;                                  // FM SYNTH: Algo
    fm()->algorithm = 5;
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(fm()->algorithm, 6);
}

TEST(fm_values_get_set_round_trip) {
    dx7_patch_t p;
    dx7_load_factory(&p, 3);
    for (int op = 0; op <= DX7_GLOBAL_OP; op++) {
        const int n = op == DX7_GLOBAL_OP ? DXG_COUNT : DXV_COUNT;
        for (int v = 0; v < n; v++) {
            const int steps = dx7_value_steps(op, v);
            int bad = 0;
            for (int i = 0; i <= steps; i++) {
                dx7_value_set(&p, op, v, (float)i / (float)steps);
                if ((int)(dx7_value_get(&p, op, v) * (float)steps + 0.5f) != i) bad++;
            }
            if (bad) printf("    op %d value %d (%s): %d of %d positions read back wrong\n", op, v, dx7_value_label(op, v), bad, steps + 1);
            CHECK_EQ(bad, 0);
        }
    }
}

// Showing a page measures every knob row (the catch refresh): with the walking measure this changed off-grid FM values (fine, times).
TEST(fm_catch_refresh_never_changes_the_patch) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    dx7_load_factory(fm(), 10);
    fm()->op[0].fine = 0.123f; fm()->op[1].eg_t[1] = 777; fm()->op[2].fixed_hz = 123.4f;
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) ui_app.ui.knob_val[k] = 300 + 100 * k;   // the knobs have been touched
    dx7_patch_t before;
    memcpy(&before, fm(), sizeof before);
    for (int p = 0; p < ui_app.ui.page_count; p++) {
        ui_app.ui.page = p;
        for (int pt = 0; pt < 4; pt++) {
            ui_app.ui.fm_pt = pt;
            synth_ui_catch_refresh(&ui_app.ui, &ui_app.params, &ui_app.seq, &ui_app.rack, NULL);
        }
    }
    CHECK(memcmp(&before, fm(), sizeof before) == 0);
}

TEST(fm_knob_sets_exact_values_with_catch) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    ui_app.ui.page = ui_find_page(GLOBAL_PAGE, GP_FM_OP_BASE);
    ui_app.ui.row = 1;
    ui_app.rack.cfg.knob_mode = 1;                                          // Direct
    ev_knob(CTL_COL_KNOB_0, INPUT_VALUE_MAX);
    CHECK_EQ(fm()->op[0].level, 99);
    ev_knob(CTL_COL_KNOB_0, 0);
    CHECK_EQ(fm()->op[0].level, 0);
    ui_app.rack.cfg.knob_mode = 0;                                          // Catch: far from the value, nothing moves
    fm()->op[0].level = 80;
    ui_app.ui.knob_catch_dir[0] = 127; ui_app.ui.knob_key[0] = -1;
    ev_knob(CTL_COL_KNOB_0, 100);
    CHECK_EQ(fm()->op[0].level, 80);
    for (int v = 100; v <= INPUT_VALUE_MAX; v += 40) ev_knob(CTL_COL_KNOB_0, v);   // sweeping across catches it
    CHECK_EQ(fm()->op[0].level, 99);
}

TEST(fm_row_learned_as_a_shift_target) {
    ui_fresh();
    keymap_set(1 * KEY_COLS + 0, (key_fn_t){ACT_MOD, 0});
    ui_set_type(SYNTH_FM);
    ui_app.ui.page = ui_find_page(GLOBAL_PAGE, GP_FM_ENV_BASE + 3);
    ui_app.ui.fm_pt = 2;
    ui_app.ui.row = 3;                                                      // Time of point 3 of operator 4
    ui_app.ui.mods_layer = MODL_SHIFT;
    ev_press(SHIFT); ev_press(CTL_KEY(1, 0)); ev_knob(CTL_COL_KNOB_2, 200); ev_release(CTL_KEY(1, 0));
    const mod_entry_t e = modifiers_get(MODL_SHIFT, CTL_COL_KNOB_2);
    CHECK(e.kind == ME_PARAM && e.m.kind == MACRO_FM && e.m.id == 3 && e.m.prm == DXV_EG_T3);
    ui_app.rack.cfg.knob_mode = 1;
    ev_knob(CTL_COL_KNOB_2, INPUT_VALUE_MAX);
    CHECK_EQ(fm()->op[3].eg_t[2], 60000);
    CHECK_STR(ui_app.popup.info.title, "OP4 T3");
    ev_release(SHIFT);
    ui_app.ui.page = 0; ui_app.ui.row = 3;                                  // FM SYNTH Fb: a patch-wide value
    macro_t m;
    CHECK(synth_ui_target_at_cursor(&ui_app.ui, &ui_app.rack, &m));
    CHECK(m.kind == MACRO_FM && m.id == DX7_GLOBAL_OP && m.prm == DXG_FB);
    ui_app.ui.row = 1;                                                      // Patch: not a knob target
    CHECK(!synth_ui_target_at_cursor(&ui_app.ui, &ui_app.rack, &m));
}

TEST(fm_jump_slot_to_an_operator_page) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    ui_app.ui.page = ui_find_page(GLOBAL_PAGE, GP_FM_ENV_BASE + 4);
    ui_app.ui.row = 2;
    synth_ui_jump_save(&ui_app.ui, &ui_app.rack, 0);
    ui_app.ui.page = 0;
    synth_ui_rebuild_pages(&ui_app.ui, &ui_app.rack);
    ev_idle(1);
    synth_ui_handle(&ui_app.ui, &ui_app.params, &ui_app.seq, &ui_app.rack, UI_JUMP_1);
    CHECK_EQ(ui_app.ui.page, ui_find_page(GLOBAL_PAGE, GP_FM_ENV_BASE + 4));
    CHECK_EQ(ui_app.ui.row, 2);
}
