// The battery (hal_power.h): a note when it gets low, a message to confirm when it is almost empty. Each is given once; they come
// again after the battery was charged.
#include "app/app_internal.h"
#include "hal/hal_power.h"
#include <stdio.h>

#define BATTERY_POLL_MS   5000
#define BATTERY_LOW_PCT   15
#define BATTERY_EMPTY_PCT 5
#define BATTERY_REARM_PCT 25             // above this the warnings are due again
#define BATTERY_INFO_MS   3000

void app_power_poll(app_t *app, uint32_t now) {
    if (now - app->batt_ms < BATTERY_POLL_MS) return;
    app->batt_ms = now;
    const int pct = power_battery_percent();
    if (pct < 0) return;
    if (pct >= BATTERY_REARM_PCT) { app->batt_warned = 0; return; }
    const int level = pct <= BATTERY_EMPTY_PCT ? 2 : pct <= BATTERY_LOW_PCT ? 1 : 0;
    if (level <= app->batt_warned) return;
    app->batt_warned = (uint8_t)level;
    if (level == 2) {
        popup_error(&app->popup, "BATTERY", "The battery is almost empty. Plug in the cable or switch off.", NULL, NULL);
    } else {
        char t[24];
        snprintf(t, sizeof t, "Low: %d %%", pct);
        popup_info(&app->popup, "BATTERY", t, 0, now, BATTERY_INFO_MS);
    }
    app->dirty = true;
}
