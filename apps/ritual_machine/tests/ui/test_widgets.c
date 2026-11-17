// Widget geometry (core/ui_screen.h, ui_layout_boxes): the `box` of an element places it by data, absolute or relative, in pixels, percent, cells
// or text rows; a screen with no layout() is drawn from its boxes; a static widget is skipped by the focus. The PRESETS tab is the screen written
// this way, and it must sit exactly where the old column layout put it.
#include "ui_test.h"
#include "core/ui_screen.h"

static gui_style_t style_of(u8g2_t *g) {
    gui_style_t st;
    gui_style_init(&st, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    gui_begin(g, &st);
    return st;
}

static bool same(gui_rect_t a, gui_rect_t b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

#define LEN(v, u, a) {v, u, a}
#define PX(v) LEN(v, UN_PX, 0)
#define NO PX(0)

TEST(a_box_places_an_element_in_pixels_percent_cells_and_rows) {
    ui_fresh();
    u8g2_t *g = ui_app.display;
    const gui_style_t st = style_of(g);
    const int rh = gui_row_h(g, &st);
    const gui_rect_t area = gui_rect(0, 16, 128, 100);
    const el_def_t el[] = {
        {"a", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_IN, 0, PX(4), PX(2), PX(30), PX(10)}},                       // pixels
        {"b", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_IN, 0, LEN(50, UN_PCT, 0), LEN(25, UN_PCT, 0), LEN(25, UN_PCT, 0), LEN(1, UN_ROW, 0)}},
        {"c", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_IN, 0, LEN(2, UN_CELL, 0), NO, LEN(3, UN_CELL, 0), LEN(2, UN_ROW, 1)}},
        {"d", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_IN, 0, PX(100), PX(90), LEN(1, UN_FILL, 0), LEN(1, UN_FILL, 0)}},   // to the area's edge
    };
    gui_rect_t r[4] = {{0}};
    ui_layout_boxes(g, &st, area, el, 4, r);
    CHECK(same(r[0], gui_rect(4, 18, 30, 10)));
    CHECK(same(r[1], gui_rect(64, 16 + 25, 32, rh)));
    CHECK(same(r[2], gui_rect(16, 16, 24, 2 * rh + 1)));
    CHECK(same(r[3], gui_rect(100, 106, 27, 9)));                         // 128 - 100 - 1, 16 + 100 - 106 - 1
}

TEST(a_relative_box_follows_the_element_it_refers_to) {
    ui_fresh();
    u8g2_t *g = ui_app.display;
    const gui_style_t st = style_of(g);
    const gui_rect_t area = gui_rect(0, 16, 128, 100);
    const el_def_t el[] = {
        {"a", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_IN, 0, PX(10), PX(10), PX(40), PX(12)}},
        {"below", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_BELOW, 0, NO, LEN(2, UN_GAP, 0), LEN(0, UN_SAME, 0), PX(8)}},      // the one before it
        {"right", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_RIGHT_OF, UI_REF(0), PX(3), NO, PX(20), LEN(0, UN_SAME, 0)}},      // element 0, 3 px away
        {"above", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_ABOVE, UI_REF(0), PX(0), PX(4), LEN(0, UN_SAME, 0), PX(6)}},
        {"left", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_LEFT_OF, UI_REF(0), PX(2), NO, PX(5), PX(12)}},
    };
    gui_rect_t r[5] = {{0}};
    ui_layout_boxes(g, &st, area, el, 5, r);
    CHECK(same(r[0], gui_rect(10, 26, 40, 12)));                          // (the area starts at y 16)
    CHECK(same(r[1], gui_rect(10, 38 + 2 * st.gap, 40, 8)));
    CHECK(same(r[2], gui_rect(53, 26, 20, 12)));
    CHECK(same(r[3], gui_rect(10, 26 - 4 - 6, 40, 6)));
    CHECK(same(r[4], gui_rect(10 - 2 - 5, 26, 5, 12)));
}

TEST(an_element_without_a_box_keeps_the_rectangle_the_layout_gave_it) {
    ui_fresh();
    u8g2_t *g = ui_app.display;
    const gui_style_t st = style_of(g);
    const el_def_t el[] = {
        {"a", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {0}},
        {"b", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, {PL_IN, 0, PX(1), PX(2), PX(-5), PX(3)}},              // a negative size is 0
    };
    gui_rect_t r[2] = {gui_rect(7, 8, 9, 10), {0}};
    ui_layout_boxes(g, &st, gui_rect(0, 0, 128, 64), el, 2, r);
    CHECK(same(r[0], gui_rect(7, 8, 9, 10)));
    CHECK(same(r[1], gui_rect(1, 2, 0, 3)));
}

// The PRESETS tab has no layout(): its boxes must give what ui_layout_column and ui_picture_box gave, on every screen height.
static void preset_matches_the_old_layout(u8g2_t *g) {
    const gui_style_t st = style_of(g);
    const gui_rect_t area = gui_rect(0, GUI_BAR_H, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g) - GUI_BAR_H);
    const screen_def_t *s = &scr_preset_screen;
    CHECK(s->layout == NULL);
    gui_rect_t r[8] = {{0}}, col[8];
    ui_layout_boxes(g, &st, area, s->el, s->n, r);
    ui_layout_column(g, &st, area, 4, col);
    for (int i = 0; i < 4; i++) CHECK(same(r[i], col[i]));
    CHECK(same(r[4], ui_picture_box(&st)));
}

TEST(the_presets_tab_sits_where_its_column_layout_put_it) {
    ui_fresh();
    preset_matches_the_old_layout(ui_app.display);
    u8g2_t small;
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&small, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);
    preset_matches_the_old_layout(&small);
}

TEST(a_static_widget_is_skipped_by_the_focus) {
    ui_fresh();
    const screen_def_t *s = &scr_preset_screen;
    CHECK(s->el[s->n - 1].flags & EF_STATIC);
    const ui_ctx_t c = {&ui_app.ui, &ui_app.rack, 0, &ui_app.params, &ui_app.seq};
    ui_app.ui.row = s->n - 1;                                            // the last button
    screen_event(s, &c, UI_DOWN);                                        // encoder A forward: the static widget is not a stop, so the header
    CHECK_EQ(ui_app.ui.row, 0);
    screen_event(s, &c, UI_UP);                                          // and back from the header: the last button
    CHECK_EQ(ui_app.ui.row, s->n - 1);
    ui_app.ui.row = s->n - 1;
    screen_event(s, &c, UI_NAV_DOWN);                                    // the joystick down from the last button: nowhere to go
    CHECK_EQ(ui_app.ui.row, s->n - 1);
}
