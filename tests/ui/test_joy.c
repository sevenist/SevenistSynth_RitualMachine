// The joystick as an XY controller (synth_ui.h: synth_ui_joy_*, app.c: joy_bind / joy_mode / joy_update, scr_joy.c, ui.cfg "joy" lines).
#include "ui_test.h"
#include "core/curves.h"

#define SHIFT CTL_BTN_3

static bool same_t(macro_t a, macro_t b) { return a.kind == b.kind && a.id == b.id && a.prm == b.prm; }

// The target of row `row` of page 0 (rows 1..3 are parameters there).
static macro_t row_target(int row) {
    macro_t m;
    ui_app.ui.page = 0; ui_app.ui.row = row;
    CHECK(synth_ui_target_at_cursor(&ui_app.ui, &ui_app.rack, &m));
    return m;
}
static void push_on(int row) { ui_app.ui.page = 0; ui_app.ui.row = row; ev_tap(CTL_JOY_SW); }
static float norm(macro_t m) { return synth_ui_target_norm(&ui_app.params, &ui_app.seq, &ui_app.rack, &m); }

TEST(joy_push_binds_x_then_y_moves_and_inverts) {
    ui_fresh();
    const macro_t a = row_target(1), b = row_target(2), c = row_target(3);
    push_on(1);
    CHECK(same_t(ui_app.ui.joy[0].t, a));
    CHECK_EQ(ui_app.ui.joy[1].t.kind, MACRO_NONE);
    CHECK_STR(ui_app.popup.info.title, "JOY");
    push_on(2);
    CHECK(same_t(ui_app.ui.joy[1].t, b));
    push_on(3);                                                          // a third one replaces the older binding: X
    CHECK(same_t(ui_app.ui.joy[0].t, c) && same_t(ui_app.ui.joy[1].t, b));
    push_on(3);                                                          // again at once: C goes to Y, A comes back on X
    CHECK(same_t(ui_app.ui.joy[0].t, a) && same_t(ui_app.ui.joy[1].t, c));
    push_on(3);                                                          // and back
    CHECK(same_t(ui_app.ui.joy[0].t, c) && same_t(ui_app.ui.joy[1].t, b));
    push_on(2);                                                          // B is bound and not the one just bound: its axis is inverted
    CHECK(same_t(ui_app.ui.joy[1].t, b));
    CHECK(ui_app.ui.joy[1].min == 100 && mapping_max(&ui_app.ui.joy[1]) == 0);
    CHECK_STR(ui_app.popup.info.title, "JOY INVERT");
    push_on(2);                                                          // inverted again: back to normal
    CHECK(ui_app.ui.joy[1].min == 0 && mapping_max(&ui_app.ui.joy[1]) == 100);
    push_on(1);                                                          // a new one: replaces the older binding (B; C came back on X last)
    CHECK(same_t(ui_app.ui.joy[0].t, c) && same_t(ui_app.ui.joy[1].t, a));
    ui_app.ui.row = 0;                                                   // not a parameter row: the push keeps its old meaning
    ev_tap(CTL_JOY_SW);
    CHECK(same_t(ui_app.ui.joy[0].t, c) && same_t(ui_app.ui.joy[1].t, a));
}

TEST(joy_xy_mode_moves_around_the_value_and_springs_back) {
    ui_fresh();
    const macro_t a = row_target(1);
    push_on(1);
    synth_ui_target_step(&ui_app.params, &ui_app.seq, &ui_app.rack, &a, 3);       // away from both ends
    const float v0 = norm(a);
    CHECK(v0 > 0.0f && v0 < 1.0f);
    ev_press(SHIFT); ev_tap(CTL_JOY_SW); ev_release(SHIFT);              // Shift + push: XY mode
    CHECK(ui_app.ui.joy_xy);
    CHECK_STR(ui_app.popup.info.text, "XY mode");
    const int page = ui_app.ui.page, row = ui_app.ui.row;
    ev_knob(CTL_JOY_X, INPUT_VALUE_MAX);                                 // full right: the top of the range
    CHECK(norm(a) > 0.99f);
    CHECK(ui_app.ui.page == page && ui_app.ui.row == row);               // no navigation in XY mode
    ev_knob(CTL_JOY_X, INPUT_AXIS_CENTER);                               // let go: exactly the value again
    CHECK(norm(a) == v0);
    ev_knob(CTL_JOY_X, 0);
    CHECK(norm(a) < 0.01f);
    ev_knob(CTL_JOY_X, INPUT_AXIS_CENTER + JOY_XY_DEADZONE / 2);         // inside the dead zone: the centre
    CHECK(norm(a) == v0);
    ev_knob(CTL_JOY_X, 300);
    ev_press(SHIFT); ev_tap(CTL_JOY_SW); ev_release(SHIFT);              // leaving XY mode away from the centre: the value comes back
    CHECK(!ui_app.ui.joy_xy);
    CHECK(norm(a) == v0);
    ev_knob(CTL_JOY_X, INPUT_AXIS_CENTER);
    ev_knob(CTL_JOY_X, INPUT_VALUE_MAX);                                 // navigation again: Right steps the row's value once, no XY drive
    CHECK(norm(a) < 0.99f);
}

TEST(joy_follows_a_value_set_by_a_knob) {
    ui_fresh();
    ui_app.rack.cfg.knob_mode = 1;                                       // Direct: the knob sets the value at once
    const macro_t a = row_target(1);
    push_on(1);
    ev_press(SHIFT); ev_tap(CTL_JOY_SW); ev_release(SHIFT);
    ev_knob(CTL_COL_KNOB_0, 300);                                        // the knob of row 1 sets the value while the stick is centred
    const float v1 = norm(a);
    ev_knob(CTL_JOY_X, 800);
    CHECK(norm(a) > v1);                                                 // the stick moves around the knob's value ...
    ev_knob(CTL_JOY_X, INPUT_AXIS_CENTER);
    CHECK(norm(a) == v1);                                                // ... and comes back to it
    ev_knob(CTL_JOY_X, 200);
    CHECK(norm(a) < v1);
    ev_knob(CTL_COL_KNOB_0, 900);                                        // the knob changes it during a push
    const float v2 = norm(a);
    CHECK(v2 > v1);
    ev_knob(CTL_JOY_X, INPUT_AXIS_CENTER);
    CHECK(norm(a) == v2);                                                // let go: the knob's change is kept
    ev_knob(CTL_JOY_X, 0);
    ev_knob(CTL_JOY_X, INPUT_AXIS_CENTER);
    CHECK(norm(a) == v2);                                                // and the next push is around it
}

TEST(joy_range_curve_tab_and_ui_cfg) {
    ui_fresh();
    const macro_t a = row_target(1), b = row_target(2);
    push_on(1); push_on(2);
    ui_app.ui.joy[0] = (mapping_t){a, 20, 100 - 60, CURVE_S};
    ev_tap(CTL_BTN_1);                                                   // the menu, JOY tab: Y shown, then cleared
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_JOY);
    CHECK(ui_app.ui.menu_tab >= 0);
    ui_app.ui.row = 1;                                                   // Axis: X -> Y
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.ui.joy_tab, 1);
    ui_app.ui.row = 6;                                                   // Clear
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui_app.ui.joy[1].t.kind, MACRO_NONE);
    CHECK(same_t(ui_app.ui.joy[0].t, a));
    static char txt[STORAGE_FILE_MAX];
    ui_settings_to_text(&ui_app.ui, &ui_app.rack, txt, sizeof txt);
    CHECK(strstr(txt, "joy\njoy x ") != NULL);
    CHECK(strstr(txt, "joy y") == NULL);
    CHECK(strstr(txt, " range 20 60 curve s\n") != NULL);
    memset(ui_app.ui.joy, 0, sizeof ui_app.ui.joy);
    ui_app.ui.joy[1] = (mapping_t){b, 0, 0, 0};                          // the file has no Y: it is unbound after loading
    CHECK(ui_settings_from_text(&ui_app.ui, &ui_app.rack, txt));
    CHECK(same_t(ui_app.ui.joy[0].t, a) && ui_app.ui.joy[0].min == 20 && mapping_max(&ui_app.ui.joy[0]) == 60 && ui_app.ui.joy[0].curve == CURVE_S);
    CHECK_EQ(ui_app.ui.joy[1].t.kind, MACRO_NONE);
}

TEST(joy_shift_push_is_a_layer_entry) {
    ui_fresh();
    CHECK(modifiers_get(MODL_SHIFT, CTL_JOY_SW).kind == ME_ACTION && modifiers_get(MODL_SHIFT, CTL_JOY_SW).act == ACT_JOY_MODE);
    key_fn_t f;
    CHECK(keymap_fn_parse("joyxy", &f) && f.act == ACT_JOY_MODE);       // also a key function (KEYS tab, "Joy XY")
}
