// The battery (hal/hal_power.h, app/app_power.c): a note when it gets low, a message to confirm when it is almost empty.
#include "ui_test.h"

extern int ui_stub_battery;                 // tools/ui_stubs.c: the charge the platform reports, -1 = no battery

TEST(battery_warns_once_when_low_and_once_when_almost_empty) {
    ui_stub_battery = 80;
    ui_fresh();
    ev_idle(600);                                                        // (the battery is looked at every 5 s)
    CHECK_EQ(ui_app.batt_warned, 0);
    ui_stub_battery = 14;
    ev_idle(600);
    CHECK_EQ(ui_app.batt_warned, 1);
    CHECK_STR(ui_app.popup.info.title, "BATTERY");
    CHECK_STR(ui_app.popup.info.text, "Low: 14 %");
    CHECK(!popup_modal(&ui_app.popup));                                  // a note: nothing to confirm
    ui_stub_battery = 12;
    ui_app.popup.info.title[0] = 0;
    ev_idle(600);
    CHECK_STR(ui_app.popup.info.title, "");                              // not said again
    ui_stub_battery = 4;
    ev_idle(600);
    CHECK_EQ(ui_app.batt_warned, 2);
    CHECK(popup_modal(&ui_app.popup));
    CHECK_STR(ui_app.popup.queue[0].title, "BATTERY");
    CHECK_EQ(ui_app.popup.count, 1);
    ev_idle(600);
    CHECK_EQ(ui_app.popup.count, 1);                                     // once
    ui_stub_battery = 60;                                                // charged: the warnings are due again
    ev_idle(600);
    CHECK_EQ(ui_app.batt_warned, 0);
    ui_stub_battery = -1;
}

TEST(battery_absent_gives_no_warning) {
    ui_stub_battery = -1;
    ui_fresh();
    ev_idle(600);
    CHECK_EQ(ui_app.batt_warned, 0);
    CHECK(!popup_modal(&ui_app.popup));
}
