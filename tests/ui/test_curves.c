// User curves: points with linear or stepped segments, the table, curves.cfg, the CURVES tab and its X / Y knobs (core/curves.h, scr_curves.c).
#include "ui_test.h"
#include "core/curves.h"
#include <math.h>

#define U(k) (CURVE_USER_FIRST + (k))
#define NEAR(a, b, tol) (fabsf((float)(a) - (float)(b)) <= (tol))

TEST(curves_user_linear_and_stepped_segments) {
    curves_init();
    CHECK_EQ(curve_user_new(0), 0);
    CHECK_EQ(curve_user_new(0), -1);                                       // it exists already
    CHECK(NEAR(curve_eval(U(0), 0.37f), 0.37f, 0.01f));                     // a new curve is the straight line
    CHECK_EQ(curve_user_add_point(0, 0), 1);                                // halfway: (50, 50)
    curve_user_set_point(0, 1, 50, 80);
    CHECK(NEAR(curve_eval(U(0), 0.25f), 0.40f, 0.01f));                     // (0,0) -> (50,80), linear
    CHECK(NEAR(curve_eval(U(0), 0.75f), 0.90f, 0.01f));                     // (50,80) -> (100,100)
    curve_user_set_mode(0, 0, CURVE_PT_STEP);
    curve_user_set_mode(0, 1, CURVE_PT_STEP);
    CHECK(NEAR(curve_eval(U(0), 0.25f), 0.0f, 0.01f));                      // stepped: held at the point's y until the next point
    CHECK(NEAR(curve_eval(U(0), 0.75f), 0.80f, 0.01f));
    CHECK(NEAR(curve_eval(U(0), 1.0f), 1.0f, 0.01f));
    CHECK(curve_eval(U(0), 0.48f) < 0.05f && curve_eval(U(0), 0.52f) > 0.75f);   // the step edge is sharp (one table step wide)
}

TEST(curves_user_points_keep_their_order_and_limits) {
    curves_init();
    curve_user_new(3);
    curve_user_add_point(3, 0);                                             // 0, 50, 100
    curve_user_set_point(3, 1, 120, 50);                                    // x stays below the next point
    CHECK_EQ(curve_user(3)->pt[1].x, 100);
    curve_user_set_point(3, 1, 30, -5);
    CHECK_EQ(curve_user(3)->pt[1].x, 30);
    CHECK_EQ(curve_user(3)->pt[1].y, 0);
    while (curve_user(3)->n < CURVE_POINTS_MAX) CHECK(curve_user_add_point(3, curve_user(3)->n - 1) >= 0);
    CHECK_EQ(curve_user_add_point(3, 0), -1);                               // full at 16
    for (int i = 0; i < 20; i++) curve_user_remove_point(3, 0);
    CHECK_EQ(curve_user(3)->n, 2);                                          // two points stay
}

TEST(curves_user_in_the_mapping_list_and_after_delete) {
    curves_init();
    CHECK_EQ(curve_count(), CURVE_BUILTIN);
    curve_user_new(5);
    CHECK_EQ(curve_count(), CURVE_BUILTIN + 1);
    CHECK_EQ(curve_step(CURVE_S, 1), U(5));                                 // after the built-ins
    CHECK_EQ(curve_step(U(5), 1), CURVE_LIN);
    CHECK_EQ(curve_by_name("u6"), U(5));
    curve_user_set_point(5, 0, 0, 100);
    curve_user_set_point(5, 1, 100, 0);                                     // falling
    mapping_t m = {{MACRO_FM, 0, 0}, 0, 0, (uint8_t)U(5)};
    CHECK(NEAR(mapping_apply(&m, 0.0f), 1.0f, 0.01f));
    CHECK(NEAR(mapping_apply(&m, 1.0f), 0.0f, 0.01f));
    CHECK(mapping_inverse(&m, 1.0f) == 0);                                  // a falling curve: the first position is at the bottom
    curve_user_delete(5);
    CHECK(NEAR(mapping_apply(&m, 0.3f), 0.3f, 0.01f));                      // a deleted curve reads as linear
}

TEST(curves_cfg_round_trip) {
    curves_init();
    curve_user_new(1);
    curve_user_add_point(1, 0);
    curve_user_set_point(1, 1, 40, 90);
    curve_user_set_mode(1, 1, CURVE_PT_STEP);
    curve_user_new(7);
    static char txt[STORAGE_FILE_MAX];
    curves_to_text(txt, sizeof txt);
    CHECK(strstr(txt, "curve 2\npoint 0 0 lin\npoint 40 90 step\npoint 100 100 lin\n") != NULL);
    CHECK(strstr(txt, "curve 8\n") != NULL);
    curves_init();
    CHECK(curves_from_text(txt));
    CHECK(curve_user(1)->used && curve_user(1)->n == 3 && curve_user(1)->pt[1].mode == CURVE_PT_STEP);
    CHECK(curve_user(7)->used && !curve_user(0)->used);
    CHECK(NEAR(curve_eval(U(1), 0.6f), 0.9f, 0.01f));
    CHECK(!curves_from_text("knob catch\n"));                               // not a curve file: nothing changes
    CHECK(curve_user(1)->used);
}

TEST(curves_saved_by_themselves_and_loaded_at_start) {
    ui_stub_ramcard(true);
    ui_fresh();
    curve_user_new(2);
    curve_user_set_point(2, 1, 100, 60);
    modifiers_set(MODL_SHIFT, CTL_COL_KNOB_2, (mod_entry_t){ME_PARAM, ACT_NONE, 0, {MACRO_FM, 0, DXV_LEVEL}, 0, 0, (uint8_t)U(2)});
    ev_idle(400);                                                           // past the 2 s autosave
    ui_fresh();                                                             // power cycle with the same card
    CHECK(curve_user(2)->used && curve_user(2)->pt[1].y == 60);
    CHECK_EQ(modifiers_get(MODL_SHIFT, CTL_COL_KNOB_2).curve, U(2));        // the mapping still names it (curves load before ui.cfg)
}

TEST(curves_tab_new_knobs_and_segment) {
    ui_fresh();
    curves_init();
    ev_tap(CTL_BTN_1);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_CURVES);
    CHECK(ui_app.ui.menu_tab >= 0);
    ui_app.ui.row = 1;                                                      // Curve
    ev_turn(CTL_ENC_B, 3);
    CHECK_EQ(ui_app.ui.curve_cur, 3);
    ui_app.ui.row = 2;                                                      // New
    ev_tap(CTL_JOY_SW);
    CHECK(curve_user(3)->used);
    ui_app.ui.row = 7;                                                      // Add
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(curve_user(3)->n, 3);
    CHECK_EQ(ui_app.ui.curve_pt, 1);
    ev_knob(CURVES_KNOB_X, INPUT_VALUE_MAX * 3 / 10);                       // the X / Y knobs move the selected point
    ev_knob(CURVES_KNOB_Y, INPUT_VALUE_MAX);
    CHECK(NEAR(curve_user(3)->pt[1].x, 30, 1));
    CHECK_EQ(curve_user(3)->pt[1].y, 100);
    ui_app.ui.row = 6;                                                      // Seg
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(curve_user(3)->pt[1].mode, CURVE_PT_STEP);
    ui_app.ui.row = 4;                                                      // X with the encoder: one step
    ev_turn(CTL_ENC_B, 2);
    CHECK(NEAR(curve_user(3)->pt[1].x, 32, 1));
    ui_app.ui.row = 9;                                                      // Delete
    ev_tap(CTL_JOY_SW);
    CHECK(!curve_user(3)->used);
}
