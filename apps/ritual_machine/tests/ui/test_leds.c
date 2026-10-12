// Key LED colours by role (core/led_roles.h, key_leds.c) and the LEDS tab (scr_leds.c). The stubs record the colour of every LED.
#include "ui_test.h"
#include "core/led_roles.h"
#include <stdlib.h>

extern led_color_t ui_stub_led[CTL_COUNT];

static bool near_rgb(led_color_t c, int r, int g, int b) { return abs(c.r - r) <= 3 && abs(c.g - g) <= 3 && abs(c.b - b) <= 3; }
#define SHIFT_KEY CTL_KEY(0, 0)                     // F1 is Shift in the built-in layouts

TEST(leds_default_roles_match_the_first_colours) {
    led_roles_init();
    CHECK(near_rgb(led_role_rgb(LR_NOTE, false), 45, 45, 45));
    CHECK(near_rgb(led_role_rgb(LR_SHARP, false), 0, 0, 41));
    CHECK(near_rgb(led_role_rgb(LR_ROOT, false), 94, 0, 140));
    CHECK(near_rgb(led_role_rgb(LR_NOTE, true), 140, 255, 0));                  // a held note
    CHECK(near_rgb(led_role_rgb(LR_SHIFT, false), 43, 79, 0));
    CHECK(near_rgb(led_role_rgb(LR_SHIFT, true), 140, 255, 0));
    CHECK(near_rgb(led_role_rgb(LR_MENU, true), 80, 0, 255));
    CHECK(near_rgb(led_role_rgb(LR_JUMP, false), 40, 0, 22));
    CHECK(near_rgb(led_role_rgb(LR_JUMP, true), 161, 0, 88));
    CHECK(near_rgb(led_role_rgb(LR_BACK, true), 89, 0, 0));                    // no active state: the idle colour
    CHECK(near_rgb(led_role_rgb(LR_PLAY, true), 0, 255, 0));                    // Play stays green (the user)
    CHECK(near_rgb(led_role_rgb(LR_NONE, false), 0, 0, 0));
}

TEST(leds_role_of_each_key_function) {
    CHECK_EQ(led_role_of((key_fn_t){ACT_NOTE, 0}), LR_ROOT);                     // the base note is a C
    CHECK_EQ(led_role_of((key_fn_t){ACT_NOTE, 13}), LR_SHARP);                   // C#
    CHECK_EQ(led_role_of((key_fn_t){ACT_NOTE, 4}), LR_NOTE);                     // E
    CHECK_EQ(led_role_of((key_fn_t){ACT_MOD, 0}), LR_MOD);
    CHECK_EQ(led_role_of((key_fn_t){ACT_JUMP, 3}), LR_JUMP);
    CHECK_EQ(led_role_of((key_fn_t){ACT_PAGE_MOVE, 1}), LR_NAV);
    CHECK_EQ(led_role_of((key_fn_t){ACT_NONE, 0}), LR_NONE);
}

TEST(leds_keys_follow_their_state) {
    ui_fresh();
    ev_idle(5);
    CHECK(near_rgb(ui_stub_led[SHIFT_KEY], 43, 79, 0));
    ev_press(SHIFT_KEY);
    CHECK(near_rgb(ui_stub_led[SHIFT_KEY], 140, 255, 0));                       // Shift held: Active
    ev_release(SHIFT_KEY);
    CHECK(near_rgb(ui_stub_led[SHIFT_KEY], 43, 79, 0));
    const control_id_t note = CTL_KEY(4, 0);                                    // bottom-left: the base note (Root)
    ev_press(note);
    CHECK(near_rgb(ui_stub_led[note], 140, 255, 0));
    ev_release(note);
    CHECK(near_rgb(ui_stub_led[note], 94, 0, 140));
}

TEST(leds_tab_edits_and_previews_a_role) {
    ui_fresh();
    ev_tap(CTL_BTN_1);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_LEDS);
    CHECK(ui_app.ui.menu_tab >= 0);
    ui_app.ui.row = 1;                                                          // Role
    ev_turn(CTL_ENC_B, LR_SHIFT);
    CHECK_EQ(ui_app.ui.led_role, LR_SHIFT);
    ev_idle(5);
    CHECK(near_rgb(ui_stub_led[SHIFT_KEY], 140, 255, 0));                       // the preview: the role's Active colour
    CHECK(near_rgb(ui_stub_led[CTL_KEY(4, 0)], 0, 0, 0));                       // every other role is off (a Root key here)
    ui_app.ui.row = 2;                                                          // on the Idle rows the preview shows the Idle colour
    ev_idle(5);
    CHECK(near_rgb(ui_stub_led[SHIFT_KEY], 43, 79, 0));
    ui_app.ui.row = 4;                                                         // Active colour: Lime -> Green
    ev_turn(CTL_ENC_B, 1);
    ev_idle(1);
    CHECK(near_rgb(ui_stub_led[SHIFT_KEY], 0, 255, 0));
    ui_app.ui.row = 5;                                                          // Active %: 100 -> 50
    ev_turn(CTL_ENC_B, -10);
    ev_idle(1);
    CHECK(near_rgb(ui_stub_led[SHIFT_KEY], 0, 128, 0));
    ev_tap(CTL_BTN_1);                                                          // leave the menu: the key shows its idle colour again
    ev_idle(5);
    CHECK(near_rgb(ui_stub_led[SHIFT_KEY], 43, 79, 0));
    ev_tap(CTL_BTN_1);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_LEDS);
    ui_app.ui.row = 6;                                                          // Reset
    ev_tap(CTL_JOY_SW);
    CHECK(near_rgb(led_role_rgb(LR_SHIFT, true), 140, 255, 0));
}

TEST(leds_saved_in_ui_cfg) {
    ui_fresh();
    led_role_set(LR_MOD, (led_role_def_t){{2, 50}, {6, 100}});                  // Red 50 %, Lime 100 %
    static char txt[STORAGE_FILE_MAX];
    ui_settings_to_text(&ui_app.ui, &ui_app.rack, txt, sizeof txt);
    CHECK(strstr(txt, "leds\nled mod red 50 lime 100\n") != NULL);
    CHECK(strstr(txt, "led shift") == NULL);                                    // only what differs from the defaults
    led_roles_init();
    CHECK(ui_settings_from_text(&ui_app.ui, &ui_app.rack, txt));
    CHECK(near_rgb(led_role_rgb(LR_MOD, false), 128, 0, 0));
    CHECK(near_rgb(led_role_rgb(LR_MOD, true), 140, 255, 0));
}
