#include "core/key_leds.h"
#include "core/keymap.h"
#include "hal/hal_leds.h"

#define KEY_LEDS_PERIOD_MS 20                                // repaint rate without input (the Play LED follows the sequencer)

static led_color_t rgb(uint8_t r, uint8_t g, uint8_t b) { led_color_t c = {r, g, b}; return c; }

static led_color_t key_color(const app_t *app, control_id_t ctl, key_fn_t f) {
    switch (f.act) {
        case ACT_NOTE: {
            if (app->in.held[ctl]) return rgb(255, 90, 0);
            const int pc = ((KEYBOARD_BASE_NOTE + f.arg) % 12 + 12) % 12;      // the octave shift moves every key by 12: same colour
            if (pc == 0) return rgb(0, 110, 140);
            const bool sharp = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            return sharp ? rgb(0, 0, 40) : rgb(45, 45, 45);
        }
        case ACT_SHIFT:  return app->in.shift ? rgb(255, 200, 0) : rgb(80, 60, 0);
        case ACT_MENU:   return app->ui.in_rack ? rgb(60, 120, 255) : rgb(0, 0, 90);
        case ACT_BACK:   return rgb(90, 0, 0);
        case ACT_PLAY:   return app->seq.running ? rgb(0, 255, 0) : rgb(0, 50, 0);
        case ACT_OCTAVE: return (f.arg > 0 ? app->in.octave > 0 : app->in.octave < 0) ? rgb(170, 0, 255) : rgb(50, 0, 80);
        case ACT_JUMP:   return app->ui.jump[f.arg & 7].valid ? rgb(0, 160, 120) : rgb(0, 40, 30);
        case ACT_NONE:   return rgb(0, 0, 0);
        default:         return rgb(30, 30, 30);                              // navigation and the other control actions
    }
}

void key_leds_update(const app_t *app, bool input_event, uint32_t now_ms) {
    static uint32_t last_ms;
    if (!input_event && now_ms - last_ms < KEY_LEDS_PERIOD_MS) return;
    last_ms = now_ms;
    for (int r = 0; r < KEY_ROWS; r++)
        for (int c = 0; c < KEY_COLS; c++) {
            if (!input_key_present(r, c)) continue;
            const control_id_t ctl = (control_id_t)CTL_KEY(r, c);
            leds_set(ctl, key_color(app, ctl, keymap_get(r * KEY_COLS + c)));
        }
    leds_show();                                             // the driver sends only when something changed
}
