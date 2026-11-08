// The knob resolver (app_knobs.c): what every pot drives now, one answer for the knob legend, the pots' LEDs and the catch. These tests
// walk the screens and the layers of both sections and check that the legend, the LEDs and the knob's action agree.
#include "ui_test.h"
#include "app/app_internal.h"
#include "core/controls.h"
#include "core/led_roles.h"
#include "core/modifiers.h"
#include "boards/hwv1/hwv1_controls.h"

extern led_color_t ui_stub_led[];           // tools/ui_stubs.c

static void bind_hwv1(void) { controls_bind(kHwv1Controls, HWV1_CONTROL_COUNT, controls_map_of("hwv1")); ui_board(true); }

static bool dark(led_color_t c) { return c.r == 0 && c.g == 0 && c.b == 0; }
static bool same(led_color_t a, led_color_t b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static void resolve(knob_drive_t pot[APP_POTS]) { app_knobs_resolve(&ui_app, pot); }

// Every pot: its LED is dark exactly when the resolver says it drives nothing, and it shows the role's Away shade exactly while the
// column knob has not caught its value.
static void check_leds_agree(const char *where) {
    knob_drive_t pot[APP_POTS];
    ev_idle(3);                                                         // (the LEDs are repainted every 20 ms)
    resolve(pot);
    for (int i = 0; i < APP_POTS; i++) {
        const led_color_t led = ui_stub_led[pot[i].ctl];
        if (pot[i].cell.state < 0 ? !dark(led) : dark(led)) {
            printf("    %s: pot %d (%s) state %d, LED %d,%d,%d\n", where, i, pot[i].cell.label, pot[i].cell.state, led.r, led.g, led.b);
            CHECK(false);
        }
    }
}

TEST(knobs_leds_and_legend_agree_on_every_screen) {
    bind_hwv1();
    ui_fresh();
    led_roles_init();
    check_leds_agree("OSC 1");
    ui_app.ui.page = ui_find_page(GLOBAL_PAGE, 1);                      // the SEQUENCER: the step's Note / Len / Vel / Prob
    check_leds_agree("SEQUENCER");
    ui_app.ui.page = 0;
    ev_tap(CTL_BTN_1);                                                  // the menu (RACK): the column knobs drive nothing
    check_leds_agree("RACK");
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_CURVES);
    check_leds_agree("CURVES");
    ev_tap(CTL_BTN_1);
    ev_press(CTL_BTN_3);                                                // Shift: its layer
    check_leds_agree("Shift");
    ev_release(CTL_BTN_3);
    app_set_section(&ui_app, true);                                     // Pads: the pad's Start / End / Tune / Cut
    check_leds_agree("Pads");
    ev_press(CTL_BTN_3);
    check_leds_agree("Pads + Shift");
    ev_release(CTL_BTN_3);
    app_pads_view_go(&ui_app, VIEW_SETUP);                              // a synth tab over the pads: the knobs drive nothing
    check_leds_agree("SETUP over the pads");
}

// The user, 2026-11-08: the layers act in Pads too, and the legend, the LEDs and the header show the layer held there.
TEST(knobs_the_layer_held_in_pads_is_what_the_knobs_drive) {
    bind_hwv1();                                                        // the prototype's Shift layer: K1 speaker, K2 volume, K3 / K4 macros 1 / 2
    ui_fresh();
    led_roles_init();
    ui_app.rack.cfg.knob_mode = 1;                                      // (Direct: no catch to wait for)
    app_set_section(&ui_app, true);
    ev_idle(2);
    const int end = ui_app.pads.pad[pads_ui_pad(&ui_app.ui)].end;
    ev_press(CTL_BTN_3);
    ev_idle(1);
    knob_drive_t pot[APP_POTS];
    resolve(pot);
    CHECK_STR(pot[0].cell.label, "Spk");
    CHECK_STR(pot[1].cell.label, "Vol");
    CHECK_STR(pot[2].cell.label, "M1");
    CHECK_EQ(pot[2].macro, 0);
    CHECK_EQ(pot[1].role, LR_VOLUME);
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.layer_held, MODL_SHIFT);                         // the Pads header's "S"
    ev_knob(CTL_COL_KNOB_1, 0);                                         // K2: the volume, not the pad's End
    ev_idle(1);
    resolve(pot);
    CHECK(pot[1].cell.value < 0.05f);
    CHECK_EQ(ui_app.pads.pad[pads_ui_pad(&ui_app.ui)].end, end);
    ev_release(CTL_BTN_3);
    ev_idle(1);
    resolve(pot);
    CHECK_STR(pot[1].cell.label, "End");
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.layer_held, -1);
}

// The user, 2026-11-08: the SEQUENCER's and the CURVES tab's knobs light as the legend names them.
TEST(knobs_sequencer_and_curves_light_their_pots) {
    bind_hwv1();
    ui_fresh();
    led_roles_init();
    ui_app.ui.page = ui_find_page(GLOBAL_PAGE, 1);
    ev_idle(3);
    knob_drive_t pot[APP_POTS];
    resolve(pot);
    CHECK_STR(pot[0].cell.label, "Note");
    CHECK_EQ(pot[0].cell.state, 0);                                     // the step's value: the knob must move first
    CHECK(same(ui_stub_led[CTL_COL_KNOB_0], led_role_rgb(LR_KNOB, false)));
    ui_app.ui.page = 0;
    ev_tap(CTL_BTN_1);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_CURVES);
    ev_idle(3);
    CHECK(same(ui_stub_led[CURVES_KNOB_X], led_role_rgb(LR_KNOB, true)));
    CHECK(same(ui_stub_led[CURVES_KNOB_Y], led_role_rgb(LR_KNOB, true)));
}

// The user, 2026-11-08: in Pads the pots show the pad's values as saturation (the Knob role's Value switch on), as on the synth's pages.
TEST(knobs_pads_pots_show_the_pad_value) {
    bind_hwv1();
    ui_fresh();
    led_roles_init();
    app_set_section(&ui_app, true);
    ev_knob(CTL_COL_KNOB_3, 0);                                         // Cut at 0: pale
    ev_idle(3);
    const led_color_t low = ui_stub_led[CTL_COL_KNOB_3];
    knob_drive_t pot[APP_POTS];
    resolve(pot);
    CHECK_STR(pot[3].cell.label, "Cut");
    CHECK(pot[3].cell.value < 0.05f);                                   // the knob set it, the resolver reads it back
    ev_knob(CTL_COL_KNOB_3, INPUT_VALUE_MAX);                           // Cut at 100: the full colour
    ev_idle(3);
    resolve(pot);
    CHECK(pot[3].cell.value > 0.95f);
    CHECK(!same(low, ui_stub_led[CTL_COL_KNOB_3]));
    CHECK(same(ui_stub_led[CTL_COL_KNOB_3], led_role_rgb(LR_KNOB, true)));
}

// The user, 2026-11-08: a synth tab over the pads (PERFORM, SETUP) leaves the column knobs idle: their pots are dark; CURVES lights its two.
TEST(knobs_a_tab_over_the_pads_darkens_the_column_knobs) {
    bind_hwv1();
    ui_fresh();
    led_roles_init();
    app_set_section(&ui_app, true);
    const int cut = ui_app.pads.pad[pads_ui_pad(&ui_app.ui)].cut;
    app_pads_view_go(&ui_app, VIEW_SETUP);
    ev_idle(3);
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) CHECK(dark(ui_stub_led[CTL_COL_KNOB_0 + k]));
    ev_knob(CTL_COL_KNOB_3, cut > 50 ? 0 : INPUT_VALUE_MAX);            // and they do nothing to the pad
    ev_idle(1);
    CHECK_EQ(ui_app.pads.pad[pads_ui_pad(&ui_app.ui)].cut, cut);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_CURVES);
    ev_idle(3);
    CHECK(!dark(ui_stub_led[CURVES_KNOB_X]));
}

// A column knob's action follows the resolver's answer: a page row turned to its end reads back at that end.
TEST(knobs_turning_a_page_row_moves_what_the_resolver_reads) {
    bind_hwv1();
    ui_fresh();
    ui_app.rack.cfg.knob_mode = 1;                                      // (Direct: no catch to wait for)
    knob_drive_t pot[APP_POTS];
    resolve(pot);
    CHECK_STR(pot[1].cell.label, "Timb");
    ev_knob(CTL_COL_KNOB_1, INPUT_VALUE_MAX);
    ev_idle(2);
    resolve(pot);
    CHECK(pot[1].cell.value > 0.95f);
    CHECK_EQ(pot[1].cell.state, 1);
    ev_knob(CTL_COL_KNOB_1, 0);
    ev_idle(2);
    resolve(pot);
    CHECK(pot[1].cell.value < 0.05f);
}
