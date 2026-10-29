#include "app/key_leds.h"
#include "app/app_internal.h"
#include "core/modifiers.h"
#include "core/keymap.h"
#include "core/led_roles.h"
#include "core/audio.h"
#include "core/controls.h"

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

/* ---------------- the pots (ADR-045): on a board with LEDs under them ---------------- */

// v 0..1: pale (white at the colour's brightness with a quarter of the colour) .. the full colour.
static led_color_t saturate(led_color_t c, float v) {
    const int m = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    const int t = 64 + (int)((v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v) * 192.0f);
    return mix((led_color_t){(uint8_t)m, (uint8_t)m, (uint8_t)m}, c, t);
}

// A pot of `role`: its Active shade once caught, else Idle; the value as saturation when the role says so (v < 0: no value).
static led_color_t pot_color(int role, bool caught, float v) {
    const led_color_t c = led_role_rgb(role, caught);
    return led_role(role)->value && v >= 0.0f ? saturate(c, v) : c;
}

// Macro k's value: where its knob was put, else its first destination's value; -1 with no destination.
static float macro_norm(app_t *app, int k) {
    const macro_def_t *m = &app->ui.macro[k];
    if (!m->n) return -1.0f;
    const int pos = app->ui.knob_pos[SYNTH_UI_KNOB_MACRO + k];
    if (pos >= 0) return (float)pos / (float)INPUT_VALUE_MAX;
    return synth_ui_target_peek(&app->params, &app->seq, &app->rack, &m->dest[0].t);
}

static const control_id_t macro_knob[3] = {CTL_KNOB_R1, CTL_KNOB_R2, CTL_KNOB_R3};

// A knob that plays macro k: the macro's colour at the Knob role's Caught brightness (R1..R3 have no catch: a macro knob sets at once;
// a col knob through a layer catches its first destination), its value as saturation when the Knob role says so; at the Away
// brightness while the macro has no destination or the knob has not caught it.
static led_color_t macro_shade(app_t *app, int k, bool caught) {
    const macro_def_t *m = &app->ui.macro[k];
    const led_role_def_t *kr = led_role(LR_KNOB);
    const led_color_t c = led_shade_rgb((led_shade_t){m->color, m->n && caught ? kr->active.pct : kr->idle.pct});
    const float v = macro_norm(app, k);
    return kr->value && m->n && v >= 0.0f ? saturate(c, v) : c;
}

// The LEDS tab: the pots of the role shown light (Idle on the Idle rows), the others are off.
static void pot_preview(const app_t *app, bool idle) {
    const int r = app->ui.led_role;
    const led_color_t knob = r == LR_KNOB ? led_role_rgb(LR_KNOB, !idle) : (led_color_t){0, 0, 0};
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) controls_led((control_id_t)(CTL_COL_KNOB_0 + k), knob);
    for (int k = 0; k < 3; k++) controls_led(macro_knob[k], knob);
    controls_led(CTL_VOLUME, r == LR_VOLUME ? led_role_rgb(LR_VOLUME, !idle) : (led_color_t){0, 0, 0});
    controls_led(CTL_ENC_A, r == LR_ENC ? led_role_rgb(LR_ENC, !idle) : (led_color_t){0, 0, 0});
}

// Encoder A (ADR-045): the section (Enc A's Idle shade in the synth, Active in Pads); a recording armed blinks it, one running lights
// it, in Back's red as the Rec key: the synth's notes (armed, then with the sequencer running), the pattern's sound, the Pads' capture
// and their sequencer's live record.
static void enc_led(const app_t *app, uint32_t now_ms) {
    const bool pads = app->section == SECTION_PADS;
    const int cap = app->ui.cap_state;
    const bool recording = (app->rec.armed && app->seq.running) || app->ui.seq_audio || cap == CAPTURE_ST_RECORDING
                           || cap == CAPTURE_ST_FINISHING || (app->ui.pseq_rec && app->pads_run);
    const bool armed = (app->rec.armed && !app->seq.running) || cap == CAPTURE_ST_ARMED || (app->ui.pseq_rec && !app->pads_run);
    if (recording || (armed && (now_ms / 150) % 2)) controls_led(CTL_ENC_A, led_role_rgb(LR_BACK, true));
    else controls_led(CTL_ENC_A, led_role_rgb(LR_ENC, pads));
}

static led_color_t macro_color(app_t *app, int k) { return macro_shade(app, k, true); }

// Shift / Mod held (ADR-045): what pot `ctl` (col: its col knob index, else -1) does in that layer, in `out`. False: its plain display
// (a Default entry on a knob that keeps its job there). A col knob's catch is already measured against its layer target (app_catch_poll).
static bool layer_pot(app_t *app, int layer, control_id_t ctl, int col, led_color_t *out) {
    const mod_entry_t me = modifiers_get(layer, ctl);
    const bool caught = col < 0 || app->rack.cfg.knob_mode || (app->ui.knob_val[col] >= 0 && app->ui.knob_catch_dir[col] == 0);
    *out = (led_color_t){0, 0, 0};
    if (me.kind == ME_DEFAULT) return col >= 0;                      // a col knob with a modifier and no entry drives nothing: dark
    if (me.kind == ME_ACTION && (me.act == ACT_MACRO || me.act == ACT_MACRO_LEARN)) {     // a macro (or Learn into it): its colour
        if (me.arg >= 0 && me.arg < SYNTH_UI_MACROS) *out = macro_shade(app, me.arg, caught);
        return true;
    }
    if (me.kind == ME_ACTION && me.act == ACT_NONE) return true;
    if (me.kind == ME_ACTION) {                                     // a setting or a step action: the Knob colour (the volume: its own)
        *out = me.act == ACT_MASTER_VOLUME || me.act == ACT_VOLUME_STEP ? pot_color(LR_VOLUME, true, app->rack.cfg.volume / 2.0f)
                                                                         : led_role_rgb(LR_KNOB, true);
        return true;
    }
    if (me.m.kind == MACRO_CFG && me.m.prm == CFGP_VOLUME) {        // the master volume as a parameter: the Volume role
        *out = pot_color(LR_VOLUME, caught, app->rack.cfg.volume / 2.0f);
        return true;
    }
    float v = synth_ui_target_peek(&app->params, &app->seq, &app->rack, &me.m);    // a parameter
    if (v < 0.0f && col >= 0) v = app->ui.knob_norm[col];
    *out = pot_color(LR_KNOB, caught, v);
    return true;
}

static void pot_leds(app_t *app) {
    const int layer = app->section == SECTION_PADS ? -1 : app_active_layer(app);
    if (layer >= 0) {
        led_color_t c;
        for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
            const control_id_t ctl = (control_id_t)(CTL_COL_KNOB_0 + k);
            if (layer_pot(app, layer, ctl, k, &c)) controls_led(ctl, c);
        }
        for (int k = 0; k < 3; k++) controls_led(macro_knob[k], layer_pot(app, layer, macro_knob[k], -1, &c) ? c : macro_color(app, k));
        controls_led(CTL_VOLUME, layer_pot(app, layer, CTL_VOLUME, -1, &c) ? c : pot_color(LR_VOLUME, true, app->rack.cfg.volume / 2.0f));
        return;
    }
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {                 // the page's rows 1..4 (Pads: the editor's, not measured: caught)
        const control_id_t ctl = (control_id_t)(CTL_COL_KNOB_0 + k);
        if (app->section == SECTION_PADS) { controls_led(ctl, pot_color(LR_KNOB, true, -1.0f)); continue; }
        float v;
        const int st = synth_ui_col_knob_state(&app->ui, &app->params, &app->seq, &app->rack, k, &v);
        controls_led(ctl, st < 0 ? (led_color_t){0, 0, 0} : pot_color(LR_KNOB, st == 1, v));
    }
    for (int k = 0; k < 3; k++) controls_led(macro_knob[k], macro_color(app, k));
    controls_led(CTL_VOLUME, pot_color(LR_VOLUME, true, app->rack.cfg.volume / 2.0f));      // (the master volume is 0..2)
}

void key_leds_update(app_t *app, bool input_event, uint32_t now_ms) {
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
            if (!controls_key_present(r, c)) continue;
            const control_id_t ctl = (control_id_t)CTL_KEY(r, c);
            const key_fn_t f = keymap_get(r * KEY_COLS + c);
            const int role = led_role_of(f);
            if (preview) { controls_led(ctl, role == app->ui.led_role ? led_role_rgb(role, !preview_idle) : (led_color_t){0, 0, 0}); continue; }
            if (f.act == ACT_PAD && app->ui.pad_steps && app->pbank.pat) {   // step mode: the selected pad's steps of the page shown
                const pseq_pattern_t *pt = &app->pbank.pat[app->pbank.cur];
                const int cursor = app->ui.pseq_step < pt->steps ? app->ui.pseq_step : pt->steps - 1;
                const int step = cursor / SEQ_PAGE * SEQ_PAGE + (f.arg & 15);
                const bool hit = step < pt->steps && pseq_find(pt, pads_ui_pad(&app->ui), step) >= 0;
                if (step >= pt->steps) controls_led(ctl, (led_color_t){0, 0, 0});                      // past the pattern's end
                else if (step == app->ui.pseq_pos) controls_led(ctl, led_role_rgb(LR_PLAY, true));     // the playhead
                else if (hit) controls_led(ctl, led_role_rgb(LR_PAD, true));
                else controls_led(ctl, led_role_rgb(LR_PAD, false));
                continue;
            }
            if (f.act == ACT_PAD) {                          // an empty pad is dark, a muted one shows Back's idle colour (unless it sounds)
                const pad_t *p = &app->pads.pad[app->ui.pad_bank * PADS_PER_BANK + (f.arg & 15)];
                if (pad_empty(p)) { controls_led(ctl, (led_color_t){0, 0, 0}); continue; }
                if (p->mute && !key_active(app, ctl, f)) { controls_led(ctl, led_role_rgb(LR_BACK, false)); continue; }
                if (p->mute) {                               // muted but sounding (user, 2026-10-10): a smooth pulse red <-> the pad colour
                    const int ph = (int)(now_ms % MUTE_PULSE_MS) * 512 / MUTE_PULSE_MS;     // 0..511: up, then down
                    controls_led(ctl, mix(led_role_rgb(LR_BACK, true), led_role_rgb(LR_PAD, true), ph < 256 ? ph : 511 - ph));
                    continue;
                }
            }
            if (f.act == ACT_PAD_REC) {                      // Rec: dim, blinking while armed, lit while it records (Back's red)
                const int st = app->ui.cap_state;
                const bool lit = st == CAPTURE_ST_RECORDING || st == CAPTURE_ST_FINISHING || (st == CAPTURE_ST_ARMED && (now_ms / 150) % 2);
                controls_led(ctl, led_role_rgb(LR_BACK, lit));
                continue;
            }
            controls_led(ctl, led_role_rgb(role, key_active(app, ctl, f)));
        }
    if (preview) pot_preview(app, preview_idle);
    else { pot_leds(app); enc_led(app, now_ms); }
    leds_show();                                             // the driver sends only when something changed
}
