// Where an input event goes: a matrix key takes its function from the key layout (core/keymap.h), the other controls theirs from the
// binding table (core/bindings.h), after the Shift / Mod layers (app_layers.c). The joystick is four buttons, or an XY controller.
#include "app/app_internal.h"
#include "core/keymap.h"
#include "core/curves.h"
#include <stdio.h>
#include <stdlib.h>

// Looks the event up in the binding table and runs every row that matches. The table is always used as without a modifier: Shift and
// Mod are the layers (only a control whose layer entry is Default gets here).
static void app_dispatch(app_t *app, input_event_t e) {
    const uint8_t mods = MODS_NONE;
    for (int i = 0; i < bindings_count; i++) {
        const binding_t *b = &bindings[i];
        if (b->ctl != e.ctl) continue;
        if (e.kind == IN_RELEASE) {                         // only hold actions care, and the modifier state does not matter
            if (action_is_hold(b->act) && b->on == IN_PRESS) app_run_action(app, b, e);
        } else if (b->on == e.kind && (b->mods & mods)) {
            app_run_action(app, b, e);
        }
    }
}

// Every control but the matrix keys and the joystick axes.
static void control_event(app_t *app, input_event_t e) {
    if (e.kind == IN_RELEASE) { app_seq_note_up(app, e.ctl); app_note_off(app, e.ctl); app_dispatch(app, e); return; }   // a layer may have started a note on it
    if (app_curves_tab_knob(app, e) || app_mods_tab_select(app, e) || app_layer_event(app, e)) return;
    app_dispatch(app, e);
}

/* ---------------- the matrix keys: their function comes from the key layout (core/keymap.h) ---------------- */

static void key_event(app_t *app, input_event_t e, uint32_t now) {
    const int key = e.ctl - CTL_KEY_FIRST;
    if (e.kind == IN_RELEASE) {
        app_seq_note_up(app, e.ctl);                        // (the key is up, whether its note is latched or not: Rec, app_seq.c)
        app_note_off(app, e.ctl);
        app_pad_key_up(app, e.ctl);
        const key_fn_t rf = keymap_get(key);
        if (rf.act == ACT_PAD_SELECT) app->in.pad_select = false;
        if (rf.act == ACT_PAD_MUTE)   app->in.pad_mute = false;
        if (app->in.shift_key == e.ctl) { app->in.shift = false; app->in.shift_key = CTL_NONE; }
        if (app->in.mod_key == e.ctl)   { app->in.mod = false;   app->in.mod_key = CTL_NONE; }
        if (e.ctl == KEYMAP_RESET_KEY) app->reset_held = false;
        return;
    }
    if (e.kind != IN_PRESS) return;
    if (e.ctl == KEYMAP_RESET_KEY && now - app->boot_ms < KEYMAP_RESET_WINDOW_MS) { app->reset_held = true; app->reset_since = now; }
    const key_fn_t f = keymap_get(key);
    const bool modifier = f.act == ACT_SHIFT || f.act == ACT_MOD;      // a modifier key is always itself (a layer never takes it)
    if (synth_ui_on_keys_tab(&app->ui, &app->rack)) {       // the KEYS tab: a key selects itself in the list; only notes, the modifiers and Menu still act
        app->ui.key_cur = key;
        app->dirty = true;
        if (f.act != ACT_NOTE && !modifier && f.act != ACT_MENU) return;
    }
    if (!modifier && f.act != ACT_MENU && app_mods_tab_select(app, e) && f.act != ACT_NOTE) return;   // the MODIFIERS tab: the same
    if (f.act == ACT_SHIFT) app->in.shift_key = e.ctl;
    if (f.act == ACT_MOD)   app->in.mod_key = e.ctl;
    if (!modifier && app_layer_event(app, e)) return;
    const binding_t b = {e.ctl, IN_PRESS, MODS_ANY, (action_id_t)f.act, f.arg};
    app_run_action(app, &b, e);
}

// The top-left function key held for KEYMAP_RESET_HOLD_MS, pressed during the first seconds after power-on: back to the first built-in layout.
static void key_reset_check(app_t *app, uint32_t now) {
    if (!app->reset_held || now - app->reset_since < KEYMAP_RESET_HOLD_MS) return;
    app->reset_held = false;
    keymap_reset();
    keymap_save();                      // no card yet: saved when it shows up (app_check_sd)
    app->keys_notice = true;
    app->keys_notice_ms = now;
    app->dirty = true;
}

/* ---------------- the joystick as an XY controller (synth_ui.h: synth_ui_joy_*) ---------------- */

#define JOY_INFO_MS 1500

// A push on a page row: binds its parameter (the rules: synth_ui_joy_click). False when the row is not a parameter.
bool app_joy_bind(app_t *app) {
    int axis = 0;
    const joy_click_t r = synth_ui_joy_click(&app->ui, &app->rack, &axis);
    if (r == JOY_CLICK_NONE) return false;
    char name[24], t[40];
    const mapping_t *m = &app->ui.joy[axis];
    if (!synth_ui_target_describe(NULL, NULL, &app->rack, &m->t, name, sizeof name, NULL, 0)) snprintf(name, sizeof name, "?");
    snprintf(t, sizeof t, "%s %s%s", axis ? "Y" : "X", name, m->min > mapping_max(m) ? " (inv)" : "");
    popup_info(&app->popup, r == JOY_CLICK_SWAPPED ? "JOY SWAP" : "JOY", t, 0, audio_millis(), JOY_INFO_MS);
    if (app->ui.joy_xy) synth_ui_joy_mode(&app->ui, &app->params, &app->seq, &app->rack, true);   // XY mode: the new binding rests where its value is
    app->dirty = true;
    return true;
}

void app_joy_xy_off(app_t *app) {                 // leaving the synth: XY mode off, its parameters back where they were
    if (app->ui.joy_xy) synth_ui_joy_mode(&app->ui, &app->params, &app->seq, &app->rack, false);
}

void app_joy_mode(app_t *app, bool on) {
    if (on && app->in.joy_dir != CTL_NONE) {            // a held direction is let go: the stick stops navigating
        control_event(app, (input_event_t){app->in.joy_dir, IN_RELEASE, 0, false});
        app->in.joy_dir = CTL_NONE;
    }
    synth_ui_joy_mode(&app->ui, &app->params, &app->seq, &app->rack, on);
    popup_info(&app->popup, "JOY", on ? "XY mode" : "Navigation", 0, audio_millis(), JOY_INFO_MS);
    app->dirty = true;
}

/* ---------------- joystick as four buttons ---------------- */

static control_id_t joy_direction(const input_state_t *in) {
    const int dx = in->axis_x - INPUT_AXIS_CENTER, dy = in->axis_y - INPUT_AXIS_CENTER;
    const bool x_out = in->axis_x < JOY_THRESHOLD_LOW || in->axis_x > JOY_THRESHOLD_HIGH;
    const bool y_out = in->axis_y < JOY_THRESHOLD_LOW || in->axis_y > JOY_THRESHOLD_HIGH;
    if (!x_out && !y_out) return CTL_NONE;
    if (x_out && (!y_out || abs(dx) >= abs(dy))) return dx < 0 ? CTL_JOY_LEFT : CTL_JOY_RIGHT;
    return dy < 0 ? CTL_JOY_UP : CTL_JOY_DOWN;                  // low axis value = pushed up
}

static void joy_update(app_t *app, uint32_t now) {
    if (app->ui.joy_xy && !in_pads(app)) {              // XY mode: the axes drive the bound parameters, no navigation
        bool changed = synth_ui_joy_axis(&app->ui, &app->params, &app->seq, &app->rack, 0, app->in.axis_x);
        changed |= synth_ui_joy_axis(&app->ui, &app->params, &app->seq, &app->rack, 1, app->in.axis_y);
        if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; app_catch_stale = true; }
        return;
    }
    const control_id_t dir = joy_direction(&app->in);
    if (dir == app->in.joy_dir) return;
    if (app->in.joy_dir != CTL_NONE) control_event(app, (input_event_t){app->in.joy_dir, IN_RELEASE, 0, false});
    app->in.joy_dir = dir;
    if (dir != CTL_NONE) {
        control_event(app, (input_event_t){dir, IN_PRESS, 0, false});
        app->in.joy_next_ms = now + JOY_REPEAT_FIRST_MS;
    }
}

static void joy_repeat(app_t *app, uint32_t now) {
    if (app->in.joy_dir == CTL_NONE || (int32_t)(now - app->in.joy_next_ms) < 0) return;
    control_event(app, (input_event_t){app->in.joy_dir, IN_PRESS, 0, false});
    app->in.joy_next_ms = now + JOY_REPEAT_MS;
}

/* ---------------- the step's side ---------------- */

// One input event: a joystick axis, a matrix key, or any other control.
void app_input_event(app_t *app, input_event_t e, uint32_t now) {
    if (e.ctl == CTL_JOY_X || e.ctl == CTL_JOY_Y) {
        if (e.ctl == CTL_JOY_X) app->in.axis_x = e.value; else app->in.axis_y = e.value;
        joy_update(app, now);
    } else if (e.ctl >= CTL_KEY_FIRST && e.ctl <= CTL_KEY_LAST) {
        key_event(app, e, now);
    } else {
        control_event(app, e);
    }
}

// Every step: the held joystick direction repeats, the key that resets the layout is watched.
void app_input_poll(app_t *app, uint32_t now) {
    joy_repeat(app, now);
    key_reset_check(app, now);
}
