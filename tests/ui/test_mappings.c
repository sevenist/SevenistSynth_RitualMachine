// Mappings (Min / Max / curve), the catch in knob position, and macros with several destinations (core/curves.h, ui_input.c, scr_macros.c).
#include "ui_test.h"
#include "core/curves.h"
#include <math.h>

#define SHIFT CTL_BTN_3

static dx7_patch_t *fm(void) { return &ui_app.rack.cfg.fm; }
static mod_entry_t fm_param(int op, int v, int min, int max, int curve) {
    return (mod_entry_t){ME_PARAM, ACT_NONE, 0, {MACRO_FM, (uint8_t)op, (uint8_t)v}, (uint8_t)min, (uint8_t)(100 - max), (uint8_t)curve};
}

TEST(curves_end_points_shape_and_order) {
    for (int id = 0; id < curve_count(); id++) {
        CHECK(fabsf(curve_eval(id, 0.0f)) < 1e-6f);
        CHECK(fabsf(curve_eval(id, 1.0f) - 1.0f) < 1e-6f);
        float prev = -1.0f;
        int bad = 0;
        for (int i = 0; i <= 200; i++) { const float y = curve_eval(id, (float)i / 200.0f); if (y < prev - 1e-6f) bad++; prev = y; }
        CHECK_EQ(bad, 0);                                                  // every built-in curve rises
    }
    CHECK(curve_eval(CURVE_EXP, 0.5f) < 0.3f);
    CHECK(curve_eval(CURVE_LOG, 0.5f) > 0.7f);
    CHECK(fabsf(curve_eval(CURVE_S, 0.5f) - 0.5f) < 1e-3f);
    CHECK_EQ(curve_by_name("exp"), CURVE_EXP);
    CHECK_EQ(curve_by_name("nope"), -1);
}

TEST(mapping_range_inverts_and_finds_the_first_position) {
    mapping_t m = {{MACRO_FM, 0, 0}, 20, 100 - 80, CURVE_LIN};
    CHECK(fabsf(mapping_apply(&m, 0.0f) - 0.2f) < 1e-5f);
    CHECK(fabsf(mapping_apply(&m, 1.0f) - 0.8f) < 1e-5f);
    m.min = 100; m.max_off = 100;                                          // Min 100, Max 0: inverted
    CHECK(fabsf(mapping_apply(&m, 0.0f) - 1.0f) < 1e-5f);
    CHECK(fabsf(mapping_apply(&m, 1.0f)) < 1e-5f);
    m = (mapping_t){{MACRO_FM, 0, 0}, 0, 0, CURVE_LIN};
    CHECK(abs(mapping_inverse(&m, 0.5f) - MAPPING_POSITIONS / 2) <= 1);
    m.max_off = 50;                                                        // Max 50: a value of 0.8 is out of reach, the nearest is the top
    CHECK_EQ(mapping_inverse(&m, 0.8f), MAPPING_POSITIONS - 1);
}

TEST(mapping_shift_knob_range_and_inverted) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    ui_app.rack.cfg.knob_mode = 1;                                         // Direct
    modifiers_set(MODL_SHIFT, CTL_COL_KNOB_2, fm_param(0, DXV_LEVEL, 20, 80, CURVE_LIN));
    ev_press(SHIFT);
    ev_knob(CTL_COL_KNOB_2, 0);
    CHECK_EQ(fm()->op[0].level, 20);                                       // 20 % of 0..99
    ev_knob(CTL_COL_KNOB_2, INPUT_VALUE_MAX);
    CHECK_EQ(fm()->op[0].level, 79);
    ev_release(SHIFT);
    modifiers_set(MODL_SHIFT, CTL_COL_KNOB_2, fm_param(0, DXV_LEVEL, 100, 0, CURVE_LIN));
    ev_press(SHIFT);
    ev_knob(CTL_COL_KNOB_2, 10);
    CHECK(fm()->op[0].level >= 98);                                        // inverted: the knob at the bottom = the top of the range
    ev_release(SHIFT);
}

TEST(mapping_catch_in_knob_position_with_a_curve) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    ui_app.rack.cfg.knob_mode = 0;                                         // Catch
    modifiers_set(MODL_SHIFT, CTL_COL_KNOB_2, fm_param(0, DXV_LEVEL, 0, 100, CURVE_EXP));
    fm()->op[0].level = 50;
    ev_press(SHIFT);
    ev_idle(1);
    ev_knob(CTL_COL_KNOB_2, 100);                                          // far below where Exp reaches 50
    CHECK_EQ(fm()->op[0].level, 50);
    CHECK_EQ(ui_app.ui.knob_catch_dir[2], 1);                              // turn right to catch
    int v = 100;
    for (; v <= INPUT_VALUE_MAX && fm()->op[0].level == 50; v += 8) ev_knob(CTL_COL_KNOB_2, v);
    const float knob = (float)(v - 8) / INPUT_VALUE_MAX;
    CHECK(fabsf(curve_eval(CURVE_EXP, knob) * 99.0f - 50.0f) < 6.0f);      // caught where the curve gives about 50
    ev_knob(CTL_COL_KNOB_2, INPUT_VALUE_MAX);
    CHECK_EQ(fm()->op[0].level, 99);
    ev_release(SHIFT);
}

TEST(macro_drives_every_destination_through_its_mapping) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    memset(ui_app.ui.macro, 0, sizeof ui_app.ui.macro);
    CHECK(synth_ui_macro_add(&ui_app.ui, 0, &(macro_t){MACRO_FM, 0, DXV_LEVEL}));
    CHECK(synth_ui_macro_add(&ui_app.ui, 0, &(macro_t){MACRO_FM, 1, DXV_LEVEL}));
    ui_app.ui.macro[0].dest[1].min = 100; ui_app.ui.macro[0].dest[1].max_off = 100;     // the second goes the other way
    ev_knob(CTL_KNOB_R1, INPUT_VALUE_MAX);
    CHECK_EQ(fm()->op[0].level, 99);
    CHECK_EQ(fm()->op[1].level, 0);
    ev_knob(CTL_KNOB_R1, 0);
    CHECK_EQ(fm()->op[0].level, 0);
    CHECK_EQ(fm()->op[1].level, 99);
}

TEST(macro_learn_adds_dedups_fills_and_removes) {
    ui_fresh();
    memset(ui_app.ui.macro, 0, sizeof ui_app.ui.macro);
    for (int i = 0; i < SYNTH_UI_MACRO_DESTS; i++) CHECK(synth_ui_macro_add(&ui_app.ui, 4, &(macro_t){MACRO_GLOBAL, 0, (uint8_t)i}));
    CHECK(!synth_ui_macro_add(&ui_app.ui, 4, &(macro_t){MACRO_GLOBAL, 0, 20}));          // full
    CHECK(!synth_ui_macro_add(&ui_app.ui, 3, &(macro_t){MACRO_NONE, 0, 0}));
    CHECK(synth_ui_macro_add(&ui_app.ui, 3, &(macro_t){MACRO_GLOBAL, 0, 2}));
    CHECK(!synth_ui_macro_add(&ui_app.ui, 3, &(macro_t){MACRO_GLOBAL, 0, 2}));           // already in it
    synth_ui_macro_remove(&ui_app.ui, 4, 0);
    CHECK_EQ(ui_app.ui.macro[4].n, SYNTH_UI_MACRO_DESTS - 1);
    CHECK_EQ(ui_app.ui.macro[4].dest[0].t.prm, 1);                                        // the others moved up
    // Shift + R1 adds the row under the cursor to macro 1 (it does not replace it)
    ui_fresh();
    const int n0 = ui_app.ui.macro[0].n;
    ui_app.ui.page = 0; ui_app.ui.row = 2;
    ev_press(SHIFT); ev_knob(CTL_KNOB_R1, 300); ev_knob(CTL_KNOB_R1, 320); ev_release(SHIFT);
    CHECK_EQ(ui_app.ui.macro[0].n, n0 + 1);
}

TEST(macros_saved_in_ui_cfg_with_range_and_curve) {
    ui_fresh();
    memset(ui_app.ui.macro, 0, sizeof ui_app.ui.macro);
    synth_ui_macro_add(&ui_app.ui, 6, &(macro_t){MACRO_FM, 2, DXV_EG_T2});
    synth_ui_macro_add(&ui_app.ui, 6, &(macro_t){MACRO_MODULE, ui_app.rack.slot[0].id, 1});
    ui_app.ui.macro[6].dest[1] = (mapping_t){ui_app.ui.macro[6].dest[1].t, 10, 100 - 70, CURVE_S};
    macro_def_t before[SYNTH_UI_MACROS];
    memcpy(before, ui_app.ui.macro, sizeof before);
    static char txt[STORAGE_FILE_MAX];
    ui_settings_to_text(&ui_app.ui, &ui_app.rack, txt, sizeof txt);
    CHECK(strstr(txt, "macro 7 fm 2 9\n") != NULL);
    CHECK(strstr(txt, " range 10 70 curve s\n") != NULL);
    memset(ui_app.ui.macro, 0, sizeof ui_app.ui.macro);
    CHECK(ui_settings_from_text(&ui_app.ui, &ui_app.rack, txt));
    CHECK(memcmp(before, ui_app.ui.macro, sizeof before) == 0);
    // a file of the first format (no "macros" line) keeps the default macros
    ui_fresh();
    const int n0 = ui_app.ui.macro[0].n;
    CHECK(ui_settings_from_text(&ui_app.ui, &ui_app.rack, "knob direct\n"));
    CHECK_EQ(ui_app.ui.macro[0].n, n0);
}

TEST(mapping_saved_on_a_layer_entry) {
    ui_fresh();
    modifiers_set(MODL_MOD, CTL_COL_KNOB_1, fm_param(3, DXV_FINE, 25, 75, CURVE_LOG));
    static char txt[STORAGE_FILE_MAX];
    ui_settings_to_text(&ui_app.ui, &ui_app.rack, txt, sizeof txt);
    CHECK(strstr(txt, "mod k2 param fm 3 2 range 25 75 curve log\n") != NULL);
    modifiers_init();
    CHECK(ui_settings_from_text(&ui_app.ui, &ui_app.rack, txt));
    const mod_entry_t e = modifiers_get(MODL_MOD, CTL_COL_KNOB_1);
    CHECK(e.kind == ME_PARAM && e.min == 25 && e.max_off == 25 && e.curve == CURVE_LOG);
}

TEST(macros_tab_learn_and_remove) {
    ui_fresh();
    memset(ui_app.ui.macro, 0, sizeof ui_app.ui.macro);
    ev_tap(CTL_BTN_1);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_MACROS);
    CHECK(ui_app.ui.menu_tab >= 0);
    ui_app.ui.row = 1;                                                     // Macro
    ev_turn(CTL_ENC_B, 2);
    CHECK_EQ(ui_app.ui.macro_cur, 2);
    ui_app.ui.row = 7;                                                     // Learn
    ev_tap(CTL_JOY_SW);
    ev_idle(1);
    CHECK(!ui_app.ui.in_rack && ui_app.ui.learn_wait && ui_app.ui.learn_macro == 2);
    ui_app.ui.page = 0; ui_app.ui.row = 1;
    ev_tap(CTL_JOY_SW);                                                    // the push adds the row
    CHECK_EQ(ui_app.ui.macro[2].n, 1);
    CHECK(!ui_app.ui.learn_wait && ui_app.ui.learn_macro == -1);
    ev_tap(CTL_BTN_1);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_MACROS);
    ui_app.ui.row = 4;                                                     // Min
    ev_turn(CTL_ENC_B, 3);
    CHECK_EQ(ui_app.ui.macro[2].dest[0].min, 15);
    ui_app.ui.row = 6;                                                     // Curve
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.ui.macro[2].dest[0].curve, CURVE_EXP);
    ui_app.ui.row = 8;                                                     // Remove
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui_app.ui.macro[2].n, 0);
}

TEST(macro_on_a_prototype_col_knob_moves_every_destination) {
    ui_board(true);
    ui_fresh();
    ui_set_type(SYNTH_FM);
    memset(ui_app.ui.macro, 0, sizeof ui_app.ui.macro);
    synth_ui_macro_add(&ui_app.ui, 0, &(macro_t){MACRO_FM, 0, DXV_LEVEL});
    synth_ui_macro_add(&ui_app.ui, 0, &(macro_t){MACRO_FM, 4, DXV_LEVEL});
    ui_app.rack.cfg.knob_mode = 1;
    ev_press(SHIFT);                                                       // Shift + K3 = Macro 1 on the prototype
    ev_knob(CTL_COL_KNOB_2, INPUT_VALUE_MAX);
    ev_release(SHIFT);
    CHECK_EQ(fm()->op[0].level, 99);
    CHECK_EQ(fm()->op[4].level, 99);
    ui_board(false);
}
