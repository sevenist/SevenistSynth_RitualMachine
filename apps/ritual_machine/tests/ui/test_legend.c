// The knob legend and the header's marks (ENGINE_DESIGN.md ADR-046, phase 1; core/ui_legend.c): what each column knob drives, laid out
// like the knobs of the board bound, and the battery / the modifier held at the right of the header.
#include "ui_test.h"
#include "app/app_internal.h"
#include "core/controls.h"
#include "boards/hwv1/hwv1_controls.h"
#include "boards/desktop/panel_controls.h"

extern int ui_stub_battery;                 // tools/ui_stubs.c

static void bind_hwv1(void)  { controls_bind(kHwv1Controls, HWV1_CONTROL_COUNT, controls_map_of("hwv1")); ui_board(true); }
static void bind_panel(void) { controls_bind(kPanelControls, PANEL_CONTROL_COUNT, controls_map_of("panel")); ui_board(false); }

static bool px(int x, int y) {
    const uint8_t *b = u8g2_GetBufferPtr(ui_app.display);
    return (b[(y / 8) * u8g2_GetDisplayWidth(ui_app.display) + x] >> (y % 8)) & 1;
}

static void cells(legend_cell_t c[SYNTH_UI_COL_KNOBS]) {
    const int layer = app_active_layer(&ui_app);
    if (ui_app.section == SECTION_PADS) app_pads_legend(&ui_app, c);
    else if (layer >= 0) app_layer_legend(&ui_app, layer, c);
    else synth_ui_legend(&ui_app.ui, &ui_app.params, &ui_app.seq, &ui_app.rack, c);
}

TEST(legend_takes_the_layout_of_the_knobs) {
    bind_hwv1();
    ui_fresh();
    CHECK_EQ(ui_legend_h(ui_app.display), 21);                          // 2 x 2 cells of 9 px (the pots around the joystick), the rule
    bind_panel();
    CHECK_EQ(ui_legend_h(ui_app.display), 14);                          // 1 x 4 cells of 12 px: the label, the bar under it
    u8g2_t small;                                                       // 128 x 64: no zone
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&small, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);
    CHECK_EQ(ui_legend_h(&small), 0);
    controls_bind(NULL, 0, NULL);                                       // no column knob on the board: no zone
    CHECK_EQ(ui_legend_h(ui_app.display), 0);
}

TEST(legend_names_the_page_rows_with_their_values) {
    bind_hwv1();
    ui_fresh();
    legend_cell_t c[SYNTH_UI_COL_KNOBS];
    cells(c);                                                           // OSC 1: Wav (a list: no bar), Timb, Mrph, Lvl
    CHECK_STR(c[0].label, "Wav");
    CHECK_STR(c[1].label, "Timb");
    CHECK_STR(c[2].label, "Mrph");
    CHECK_STR(c[3].label, "Lvl");
    CHECK(c[1].value > 0.45f && c[1].value < 0.55f);
    CHECK(c[3].value > 0.95f);
    CHECK_EQ(c[1].state, 0);                                            // not moved since the start: nothing caught yet
    ev_knob(CTL_COL_KNOB_1, 200);                                       // away from 0.5: the tick shows where the knob is
    ev_idle(2);
    cells(c);
    CHECK_EQ(c[1].state, 0);
    CHECK(c[1].knob > 0.15f && c[1].knob < 0.25f);
    app_draw_frame(&ui_app);
    for (int x = 0; x < 128; x += 8) CHECK(px(x, 128 - 21));            // the zone's rule across the screen
}

TEST(legend_shows_the_sequencer_step_and_the_curve_point) {
    bind_hwv1();
    ui_fresh();
    ui_app.ui.page = ui_find_page(GLOBAL_PAGE, 1);                      // GP_SEQ (ui_pages.c)
    legend_cell_t c[SYNTH_UI_COL_KNOBS];
    cells(c);
    CHECK_STR(c[0].label, "Note");
    CHECK_STR(c[3].label, "Prob");
    CHECK(ui_legend_shown(&ui_app.ui, &ui_app.rack, false));
    ui_app.ui.page = 0;
    ev_tap(CTL_BTN_1);                                                  // the menu (RACK): its rows need the height, the knobs drive nothing
    CHECK(ui_app.ui.in_rack);
    CHECK(!ui_legend_shown(&ui_app.ui, &ui_app.rack, false));
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_CURVES);
    CHECK(ui_legend_shown(&ui_app.ui, &ui_app.rack, false));
    cells(c);
    CHECK_STR(c[CURVES_KNOB_X - CTL_COL_KNOB_0].label, "Pt X");
    CHECK_STR(c[CURVES_KNOB_Y - CTL_COL_KNOB_0].label, "Pt Y");
    CHECK_EQ(c[0].state, -1);
}

TEST(legend_previews_the_layer_held) {
    bind_hwv1();                                                        // the prototype's Shift layer: K1 speaker, K2 volume, K3 / K4 macros 1 / 2
    ui_fresh();
    ev_press(CTL_BTN_3);                                                // Shift
    ev_idle(1);
    legend_cell_t c[SYNTH_UI_COL_KNOBS];
    cells(c);
    CHECK_STR(c[0].label, "Spk");
    CHECK_STR(c[1].label, "Vol");
    CHECK_STR(c[2].label, "M1");
    CHECK_STR(c[3].label, "M2");
    CHECK(c[1].value > 0.0f);                                           // the volume, read from the setting
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.layer_held, MODL_SHIFT);                         // the header's "S"
    ev_release(CTL_BTN_3);
    ev_idle(1);
    cells(c);
    CHECK_STR(c[0].label, "Wav");
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.layer_held, -1);
}

TEST(legend_in_pads_names_the_pad_knobs) {
    bind_hwv1();
    ui_fresh();
    app_set_section(&ui_app, true);
    legend_cell_t c[SYNTH_UI_COL_KNOBS];
    cells(c);
    CHECK_STR(c[0].label, "Strt");
    CHECK_STR(c[3].label, "Cut");
    CHECK(c[2].value > 0.45f && c[2].value < 0.55f);                    // Tune +0
    CHECK(ui_legend_shown(&ui_app.ui, &ui_app.rack, true));
    pads_ui_fx_open(&ui_app.ui, true);                                  // the FX chains: the rack's rows need the height
    CHECK(!ui_legend_shown(&ui_app.ui, &ui_app.rack, true));
}

TEST(header_shows_the_battery) {
    bind_hwv1();
    ui_stub_battery = -1;
    ui_fresh();
    ev_idle(600);
    CHECK_EQ(ui_app.ui.batt_pct, -1);                                   // none measured: no icon
    ui_stub_battery = 50;
    ev_idle(600);                                                       // (looked at every 5 s)
    CHECK_EQ(ui_app.ui.batt_pct, 50);
    ui_app.ui.row = 1;                                                  // (a plain header: on a focused one the marks are drawn inverted)
    app_draw_frame(&ui_app);
    CHECK(px(127, 7) || px(126, 7));                                    // the nub at the right end of the header
    CHECK(!px(127, 1) && !px(126, 1));
    ui_stub_battery = -1;
}
