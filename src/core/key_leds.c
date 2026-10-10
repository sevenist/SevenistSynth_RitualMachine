#include "core/key_leds.h"
#include "core/keymap.h"
#include "core/led_roles.h"
#include "hal/hal_audio.h"
#include "hal/hal_leds.h"

#define KEY_LEDS_PERIOD_MS 20                                // repaint rate without input (the Play LED follows the sequencer)
#define MUTE_PULSE_MS      800                               // a muted pad that sounds: red <-> its playing colour, one round trip

static led_color_t mix(led_color_t a, led_color_t b, int t256) {    // t256 = 0 (a) .. 256 (b)
    return (led_color_t){(uint8_t)(a.r + ((b.r - a.r) * t256 >> 8)), (uint8_t)(a.g + ((b.g - a.g) * t256 >> 8)), (uint8_t)(a.b + ((b.b - a.b) * t256 >> 8))};
}

// Whether a key's role is in its active state now (see led_roles.h).
static bool key_active(const app_t *app, control_id_t ctl, key_fn_t f) {
    switch (f.act) {
        case ACT_NOTE:   return app->in.held[ctl] != 0;
        case ACT_SHIFT:  return app->in.shift;
        case ACT_MOD:    return app->in.mod;
        case ACT_MENU:   return app->ui.in_rack;
        case ACT_PLAY: {                                      // the clock runs: a flash on every beat, a longer one on beat 1
            int bar, beat, frac;
            if (!audio_transport(&bar, &beat, &frac)) return false;
            return frac < (beat == 1 ? 128 : 64);
        }
        case ACT_OCTAVE: return f.arg > 0 ? app->in.octave > 0 : app->in.octave < 0;
        case ACT_JUMP:   return synth_ui_jump_ready(&app->ui, &app->rack, f.arg & 7);     // bright: a jump goes somewhere now
        case ACT_PAD:    return (app->pads_shown >> (app->ui.pad_bank * PADS_PER_BANK + (f.arg & 15))) & 1;    // the pad sounds
        case ACT_PAD_BANK: return app->ui.pad_bank == f.arg;
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
    // The LEDS tab: only the keys of the role shown light, in the shade being edited (Idle on the Idle rows, else Active); the others are off
    // so no other colour hides it.
    const bool preview = synth_ui_on_leds_tab(&app->ui, &app->rack);
    const bool preview_idle = app->ui.row == 2 || app->ui.row == 3;   // scr_leds.c rows: 1 Role, 2 Idle, 3 Idle %, 4 Active, 5 Act %
    for (int r = 0; r < KEY_ROWS; r++)
        for (int c = 0; c < KEY_COLS; c++) {
            if (!input_key_present(r, c)) continue;
            const control_id_t ctl = (control_id_t)CTL_KEY(r, c);
            const key_fn_t f = keymap_get(r * KEY_COLS + c);
            const int role = led_role_of(f);
            if (preview) { leds_set(ctl, role == app->ui.led_role ? led_role_rgb(role, !preview_idle) : (led_color_t){0, 0, 0}); continue; }
            if (f.act == ACT_PAD) {                          // an empty pad is dark, a muted one shows Back's idle colour (unless it sounds)
                const pad_t *p = &app->pads.pad[app->ui.pad_bank * PADS_PER_BANK + (f.arg & 15)];
                if (pad_empty(p)) { leds_set(ctl, (led_color_t){0, 0, 0}); continue; }
                if (p->mute && !key_active(app, ctl, f)) { leds_set(ctl, led_role_rgb(LR_BACK, false)); continue; }
                if (p->mute) {                               // muted but sounding (user, 2026-10-10): a smooth pulse red <-> the pad colour
                    const int ph = (int)(now_ms % MUTE_PULSE_MS) * 512 / MUTE_PULSE_MS;     // 0..511: up, then down
                    leds_set(ctl, mix(led_role_rgb(LR_BACK, true), led_role_rgb(LR_PAD, true), ph < 256 ? ph : 511 - ph));
                    continue;
                }
            }
            leds_set(ctl, led_role_rgb(role, key_active(app, ctl, f)));
        }
    leds_show();                                             // the driver sends only when something changed
}
