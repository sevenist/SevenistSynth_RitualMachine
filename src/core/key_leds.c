#include "core/key_leds.h"
#include "core/keymap.h"
#include "core/led_roles.h"
#include "hal/hal_leds.h"

#define KEY_LEDS_PERIOD_MS 20                                // repaint rate without input (the Play LED follows the sequencer)

// Whether a key's role is in its active state now (see led_roles.h).
static bool key_active(const app_t *app, control_id_t ctl, key_fn_t f) {
    switch (f.act) {
        case ACT_NOTE:   return app->in.held[ctl] != 0;
        case ACT_SHIFT:  return app->in.shift;
        case ACT_MOD:    return app->in.mod;
        case ACT_MENU:   return app->ui.in_rack;
        case ACT_PLAY:   return app->seq.running;
        case ACT_OCTAVE: return f.arg > 0 ? app->in.octave > 0 : app->in.octave < 0;
        case ACT_JUMP:   return synth_ui_jump_ready(&app->ui, &app->rack, f.arg & 7);     // bright: a jump goes somewhere now
        default:         return false;
    }
}

void key_leds_update(const app_t *app, bool input_event, uint32_t now_ms) {
    static uint32_t last_ms;
    static unsigned last_rev;
    const bool edited = led_roles_rev() != last_rev;          // a colour edited in the LEDS tab shows at once
    if (!input_event && !edited && now_ms - last_ms < KEY_LEDS_PERIOD_MS) return;
    last_ms = now_ms;
    last_rev = led_roles_rev();
    const bool preview = synth_ui_on_leds_tab(&app->ui, &app->rack);   // the LEDS tab: the keys of the role shown light in its Active colour
    for (int r = 0; r < KEY_ROWS; r++)
        for (int c = 0; c < KEY_COLS; c++) {
            if (!input_key_present(r, c)) continue;
            const control_id_t ctl = (control_id_t)CTL_KEY(r, c);
            const key_fn_t f = keymap_get(r * KEY_COLS + c);
            const int role = led_role_of(f);
            const bool active = key_active(app, ctl, f) || (preview && role == app->ui.led_role);
            leds_set(ctl, led_role_rgb(role, active));
        }
    leds_show();                                             // the driver sends only when something changed
}
