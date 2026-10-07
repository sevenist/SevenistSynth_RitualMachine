#include "core/app.h"
#include "core/gui.h"
#include "core/key_leds.h"
#include "core/keymap.h"
#include "core/modifiers.h"
#include "core/curves.h"
#include "core/fine_step.h"
#include "core/led_roles.h"
#include "core/ui_settings.h"
#include "core/sprites.h"
#include "hal/hal_audio.h"
#include "hal/hal_display.h"
#include "hal/hal_storage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void draw_frame(app_t *app);

void app_init(app_t *app, u8g2_t *display) {
    memset(&app->in, 0, sizeof app->in);
    app->in.axis_x = app->in.axis_y = INPUT_AXIS_CENTER;
    app->dirty = false;
    app->redraw_owed = false;
    app->sd_gen = audio_sd_generation();
    app->keys_notice = false;
    app->menu_open = false;
    app->boot_ms = audio_millis();
    app->reset_held = false;
    app->status[0] = 0;
    popup_init(&app->popup);
    keymap_init();
    modifiers_init();
    curves_init();
    led_roles_init();
    keymap_load();                      // the simulator has its card at once; the board's card shows up later (check_sd)
    app->keys_notice_ms = 0;
    app->display = display;
    synth_params_default(&app->params);
    seq_init(&app->seq);
    rack_init_startup(&app->rack);
    synth_ui_init(&app->ui, &app->rack);
    curves_load();                      // before ui.cfg: its mappings name the user curves
    ui_settings_load(&app->ui, &app->rack);   // Knob mode, jump slots, the Shift / Mod layers (the board's card shows up later: check_sd)
    app->settings_pending = false;
    audio_build(&app->rack, &app->params);
    if (input_boot_reset()) {           // the reset key was held at power-on (read by the board before its key scan started)
        keymap_reset();
        keymap_save();                  // no card yet: saved when it shows up (check_sd)
        app->keys_notice = true;
        app->keys_notice_ms = app->boot_ms;
    }
    draw_frame(app);
}

/* ---------------- actions ---------------- */

static void set_status(app_t *app, const binding_t *b, int amount) {
    snprintf(app->status, sizeof app->status, "%s > %s %+d", control_name(b->ctl), action_name(b->act), amount);
}

// Set by every manual change (page, row, a value edited by hand, a macro, Shift): the column knobs are checked against what they drive
// before the next draw. A knob move itself never sets it, so a knob that just caught its value keeps it.
static bool catch_stale;

// Gives the UI one event; pushes the sound to the audio side when a value changed.
static void ui_event(app_t *app, ui_event_t ev) {
    if (synth_ui_handle(&app->ui, &app->params, &app->seq, &app->rack, ev)) audio_set_params(&app->rack, &app->params);
    app->dirty = true;
    catch_stale = true;
}

// The same event `n` times in the direction of n's sign (an encoder can report several detents at once).
static void ui_event_n(app_t *app, int n, ui_event_t down_or_next, ui_event_t up_or_prev) {
    bool changed = false;
    const ui_event_t ev = n > 0 ? down_or_next : up_or_prev;
    if (n < 0) n = -n;
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) changed |= synth_ui_handle(&app->ui, &app->params, &app->seq, &app->rack, ev);
    if (changed) audio_set_params(&app->rack, &app->params);
    app->dirty = true;
    catch_stale = true;
}

static void note_on(app_t *app, control_id_t ctl, int semitones) {
    const int note = KEYBOARD_BASE_NOTE + semitones + 12 * app->in.octave;
    if (note < 0 || note > 127 || app->in.held[ctl]) return;
    app->in.held[ctl] = (uint8_t)(note + 1);
    audio_note_on(note);
}

static void note_off(app_t *app, control_id_t ctl) {
    if (!app->in.held[ctl] || app->in.latched[ctl]) return;     // a latched note keeps sounding after its key is released
    audio_note_off(app->in.held[ctl] - 1);
    app->in.held[ctl] = 0;
}

// Shift + Back: stops every latched note (see ACT_NOTE). Returns how many.
static int release_latched(app_t *app) {
    int n = 0;
    for (int c = 0; c < CTL_COUNT; c++)
        if (app->in.latched[c]) { app->in.latched[c] = false; note_off(app, (control_id_t)c); n++; }
    return n;
}

// While a modal popup is up it gets the actions (so the key layout works as elsewhere). Notes, Shift and the master volume still act;
// everything else is ignored, so nothing edits the screen behind the popup. Returns true when the popup took the action.
static bool popup_action(app_t *app, const binding_t *b, input_event_t e) {
    const int amount = e.kind == IN_DELTA ? e.value * b->arg : b->arg;
    switch (b->act) {
        case ACT_NOTE: case ACT_SHIFT: case ACT_MOD: case ACT_MASTER_VOLUME: case ACT_VOLUME_STEP:
            return false;
        case ACT_NAV:
            if (b->arg == NAV_LEFT || b->arg == NAV_RIGHT) popup_move(&app->popup, b->arg == NAV_LEFT ? -1 : 1);
            break;
        case ACT_VALUE_ADJUST: case ACT_VALUE_FINE: case ACT_ROW_MOVE: case ACT_PAGE_MOVE:
            popup_move(&app->popup, amount);
            break;
        case ACT_LATCH: case ACT_SELECT:
            if (e.kind != IN_RELEASE) popup_confirm(&app->popup);
            break;
        case ACT_BACK:
            if (e.kind != IN_RELEASE) popup_cancel(&app->popup);
            break;
        default: break;
    }
    app->dirty = true;
    return true;
}

/* ---------------- learn: a Shift / Mod knob takes the parameter under the cursor (core/modifiers.h) ---------------- */

#define LEARN_INFO_MS 2500

static void layer_title(int layer, control_id_t ctl, char *out, int n) {
    char cn[8];
    modifiers_ctl_name(ctl, cn, sizeof cn);
    snprintf(out, (size_t)n, "%s+%s", modifiers_layer_name(layer), cn);
}

// The parameter under the cursor goes to layer + ctl. False (the popup says so) when the cursor is not on one.
static bool learn(app_t *app, int layer, control_id_t ctl) {
    char t[24], name[24];
    macro_t m;
    layer_title(layer, ctl, t, sizeof t);
    if (!modifiers_ctl_is_knob(ctl) || !synth_ui_target_at_cursor(&app->ui, &app->rack, &m)) {
        popup_info(&app->popup, t, "Not a parameter", 0, audio_millis(), LEARN_INFO_MS);
        return false;
    }
    modifiers_set(layer, ctl, (mod_entry_t){ME_PARAM, ACT_NONE, 0, m});
    synth_ui_target_describe(NULL, NULL, &app->rack, &m, name, sizeof name, NULL, 0);
    popup_info(&app->popup, t, name, 0, audio_millis(), LEARN_INFO_MS);
    app->dirty = true;
    catch_stale = true;
    return true;
}

// While the tab's Learn button waits: a push (Latch / Select) assigns the row under the cursor, Back gives up. True when it took the action.
static bool learn_push(app_t *app, action_id_t act) {
    if (act == ACT_BACK) {
        app->ui.learn_wait = false;
        app->ui.learn_macro = -1;
        popup_info(&app->popup, "LEARN", "Cancelled", 0, audio_millis(), LEARN_INFO_MS);
        app->dirty = true;
        return true;
    }
    if (act != ACT_LATCH && act != ACT_SELECT) return false;
    if (app->ui.learn_macro >= 0) {                     // the MACROS tab's Learn: the row becomes a destination of that macro
        const int k = app->ui.learn_macro;
        char t[24], name[24];
        macro_t m;
        snprintf(t, sizeof t, "MACRO %d", k + 1);
        if (!synth_ui_target_at_cursor(&app->ui, &app->rack, &m)) { popup_info(&app->popup, t, "Not a parameter", 0, audio_millis(), LEARN_INFO_MS); return true; }
        synth_ui_target_describe(NULL, NULL, &app->rack, &m, name, sizeof name, NULL, 0);
        if (synth_ui_macro_add(&app->ui, k, &m)) {
            char x[40];
            snprintf(x, sizeof x, "+ %s (%d of %d)", name, app->ui.macro[k].n, SYNTH_UI_MACRO_DESTS);
            popup_info(&app->popup, t, x, 0, audio_millis(), LEARN_INFO_MS);
            app->ui.macro_dest = app->ui.macro[k].n - 1;
        } else {
            popup_info(&app->popup, t, app->ui.macro[k].n >= SYNTH_UI_MACRO_DESTS ? "Full (8 destinations)" : "Already in it", 0, audio_millis(), LEARN_INFO_MS);
        }
        app->ui.learn_wait = false;
        app->ui.learn_macro = -1;
        app->dirty = true;
        return true;
    }
    if (learn(app, app->ui.learn_layer, (control_id_t)app->ui.learn_ctl)) app->ui.learn_wait = false;
    return true;
}

/* ---------------- the joystick as an XY controller (synth_ui.h: synth_ui_joy_*) ---------------- */

#define JOY_INFO_MS 1500

static void control_event(app_t *app, input_event_t e);

// A push on a page row: binds its parameter (the rules: synth_ui_joy_click). False when the row is not a parameter.
static bool joy_bind(app_t *app) {
    int axis = 0;
    const joy_click_t r = synth_ui_joy_click(&app->ui, &app->rack, &axis);
    if (r == JOY_CLICK_NONE) return false;
    char name[24], t[40];
    const mapping_t *m = &app->ui.joy[axis];
    if (!synth_ui_target_describe(NULL, NULL, &app->rack, &m->t, name, sizeof name, NULL, 0)) snprintf(name, sizeof name, "?");
    snprintf(t, sizeof t, "%s %s%s", axis ? "Y" : "X", name, m->min > mapping_max(m) ? " (inv)" : "");
    popup_info(&app->popup, r == JOY_CLICK_INVERTED ? "JOY INVERT" : "JOY", t, 0, audio_millis(), JOY_INFO_MS);
    if (app->ui.joy_xy) synth_ui_joy_mode(&app->ui, &app->params, &app->seq, &app->rack, true);   // XY mode: the new binding rests where its value is
    app->dirty = true;
    return true;
}

static void joy_mode(app_t *app, bool on) {
    if (on && app->in.joy_dir != CTL_NONE) {            // a held direction is let go: the stick stops navigating
        control_event(app, (input_event_t){app->in.joy_dir, IN_RELEASE, 0, false});
        app->in.joy_dir = CTL_NONE;
    }
    synth_ui_joy_mode(&app->ui, &app->params, &app->seq, &app->rack, on);
    popup_info(&app->popup, "JOY", on ? "XY mode" : "Navigation", 0, audio_millis(), JOY_INFO_MS);
    app->dirty = true;
}

// Runs one binding for one event. `e.kind` is IN_RELEASE only for hold actions.
static void app_run_action(app_t *app, const binding_t *b, input_event_t e) {
    if (popup_modal(&app->popup) && popup_action(app, b, e)) return;
    if (app->ui.learn_wait && e.kind != IN_RELEASE && learn_push(app, b->act)) return;
    const int amount = e.kind == IN_DELTA ? e.value : 1;
    const int n = amount * b->arg;
    bool changed;
    switch (b->act) {
        case ACT_ROW_MOVE:     ui_event_n(app, n, UI_DOWN, UI_UP); set_status(app, b, n); break;
        case ACT_NAV: {
            static const ui_event_t nav[4] = {UI_NAV_LEFT, UI_NAV_RIGHT, UI_NAV_UP, UI_NAV_DOWN};
            ui_event(app, nav[b->arg & 3]);
            snprintf(app->status, sizeof app->status, "%s > Nav", control_name(b->ctl));
            break;
        }
        case ACT_LATCH:
            if (!app->ui.in_rack && joy_bind(app)) break;   // the pages: a push on a parameter row binds it to a joystick axis
            ui_event(app, UI_LATCH); snprintf(app->status, sizeof app->status, "%s > %s", control_name(b->ctl), app->ui.latched ? "Latched" : "Released");
            break;
        case ACT_JOY_MODE:
            joy_mode(app, !app->ui.joy_xy);
            break;
        case ACT_VALUE_ADJUST: ui_event_n(app, n, UI_VALUE_INC, UI_VALUE_DEC); set_status(app, b, n); break;
        case ACT_VALUE_FINE:                                // the same in fine steps (core/fine_step.h)
            g_fine_step = true;
            ui_event_n(app, n, UI_VALUE_INC, UI_VALUE_DEC);
            g_fine_step = false;
            set_status(app, b, n);
            break;
        case ACT_PAGE_MOVE:    ui_event_n(app, n, UI_PAGE_NEXT, UI_PAGE_PREV); set_status(app, b, n); break;
        case ACT_ROW_TOP:      ui_event(app, UI_ROW_TOP); break;
        case ACT_SELECT:       ui_event(app, UI_SELECT); break;
        case ACT_MENU:         ui_event(app, UI_MENU); break;
        case ACT_BACK:                                      // Shift + Back (its Shift entry Default): release all latched notes, Shift only like the latch
            if (app->in.shift && !app->in.mod) {
                snprintf(app->status, sizeof app->status, "Released %d", release_latched(app));
                popup_info(&app->popup, "LATCH", app->status, 0, audio_millis(), LEARN_INFO_MS);
                app->dirty = true;
            } else ui_event(app, UI_BACK);
            break;
        case ACT_PLAY:         ui_event(app, UI_PLAY); break;
        case ACT_SHIFT:
            app->in.shift = e.kind != IN_RELEASE;
            break;
        case ACT_MOD:
            app->in.mod = e.kind != IN_RELEASE;
            break;
        case ACT_NOTE:                                      // Shift + note (its Shift entry Default) latches the note; any next press of the key releases it
            if (e.kind == IN_RELEASE) note_off(app, b->ctl);
            else if (app->in.latched[b->ctl]) { app->in.latched[b->ctl] = false; note_off(app, b->ctl); }
            else {
                note_on(app, b->ctl, b->arg);
                app->in.latched[b->ctl] = app->in.shift && !app->in.mod && app->in.held[b->ctl];   // Shift only: Mod + note plays as without modifier
            }
            break;
        case ACT_OCTAVE:
            app->in.octave += n;
            if (app->in.octave < KEYBOARD_OCTAVE_MIN) app->in.octave = KEYBOARD_OCTAVE_MIN;
            if (app->in.octave > KEYBOARD_OCTAVE_MAX) app->in.octave = KEYBOARD_OCTAVE_MAX;
            snprintf(app->status, sizeof app->status, "Octave %+d", app->in.octave);
            break;
        case ACT_PAGE_KNOB:
            changed = synth_ui_knob_row(&app->ui, &app->params, &app->seq, &app->rack, b->arg, e.value);
            if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; }
            break;
        case ACT_JUMP: {
            const int slot = b->arg;
            if (slot < 0 || slot >= SYNTH_UI_JUMP_SLOTS) break;
            if (app->in.shift || app->in.mod) {               // Shift / Mod + jump key saves (when its layer entry is Default)
                synth_ui_jump_save(&app->ui, &app->rack, slot);
                snprintf(app->status, sizeof app->status, "Saved jump %d", slot + 1);
                app->dirty = true;
            } else {
                ui_event(app, (ui_event_t)(UI_JUMP_1 + slot));
                const bool ready = synth_ui_jump_ready(&app->ui, &app->rack, slot);
                snprintf(app->status, sizeof app->status, ready ? "Jump %d" : app->ui.jump[slot].valid ? "Jump %d: not in this synth" : "Jump %d (empty)", slot + 1);
                if (!ready && app->ui.jump[slot].valid) {           // saved, but its page or tab is not shown by this synth type (it comes back with the type)
                    char t[24];
                    snprintf(t, sizeof t, "Jump %d", slot + 1);
                    popup_info(&app->popup, t, "Not in this synth", 0, audio_millis(), POPUP_INFO_MS);
                }
            }
            break;
        }
        case ACT_MACRO:
            changed = synth_ui_macro(&app->ui, &app->params, &app->seq, &app->rack, SYNTH_UI_KNOB_MACRO + (b->arg % SYNTH_UI_MACROS), b->arg, e.value);
            if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; catch_stale = true; }
            synth_ui_macro_describe(&app->ui, &app->rack, b->arg, app->status, (int)sizeof app->status);
            break;
        case ACT_MACRO_LEARN: {                             // adds the row under the cursor to the macro
            char t[24], name[24], x[40];
            macro_t m;
            bool repeat = false;                            // a knob sends many events per turn: "Already in it" only once it was not just added
            snprintf(t, sizeof t, "MACRO %d", b->arg + 1);
            if (!synth_ui_target_at_cursor(&app->ui, &app->rack, &m)) snprintf(x, sizeof x, "Not a parameter");
            else {
                synth_ui_target_describe(NULL, NULL, &app->rack, &m, name, sizeof name, NULL, 0);
                if (synth_ui_macro_add(&app->ui, b->arg, &m)) snprintf(x, sizeof x, "+ %s (%d of %d)", name, app->ui.macro[b->arg].n, SYNTH_UI_MACRO_DESTS);
                else { snprintf(x, sizeof x, "%s", app->ui.macro[b->arg].n >= SYNTH_UI_MACRO_DESTS ? "Full (8 destinations)" : "Already in it"); repeat = e.kind == IN_VALUE; }
            }
            if (!repeat || !popup_info_showing(&app->popup)) popup_info(&app->popup, t, x, 0, audio_millis(), LEARN_INFO_MS);
            synth_ui_macro_describe(&app->ui, &app->rack, b->arg, app->status, (int)sizeof app->status);
            app->dirty = true;
            break;
        }
        case ACT_MASTER_VOLUME:
            if (synth_ui_set_volume(&app->ui, &app->rack, e.value)) { audio_set_params(&app->rack, &app->params); app->dirty = true; }
            break;
        case ACT_VOLUME_STEP: {
            changed = false;
            for (int k = 0; k < (n < 0 ? -n : n) && k < 40; k++) changed |= synth_config_adjust(&app->rack.cfg, CFGP_VOLUME, n < 0 ? -1 : 1) != CFG_UNCHANGED;
            if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; }
            snprintf(app->status, sizeof app->status, "Volume %.2f", (double)app->rack.cfg.volume);
            break;
        }
        default: break;
    }
}

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

/* ---------------- the Shift / Mod layers (core/modifiers.h) ---------------- */

static int active_layer(const app_t *app) { return app->in.mod ? MODL_MOD : app->in.shift ? MODL_SHIFT : -1; }

static bool is_col_knob(control_id_t c) { return c >= CTL_COL_KNOB_0 && c <= CTL_COL_KNOB_3; }

// A Param entry: the knob drives its target; a popup says what it is, its value and, for a col knob that has not caught it, the way to turn.
// The knob slot of an absolute knob that is not a col knob (the volume knob or R1..R3) for the bookkeeping of synth_ui_knob_target.
static int other_knob_slot(control_id_t c) { return c == CTL_VOLUME ? SYNTH_UI_KNOB_VOLUME : SYNTH_UI_KNOB_MACRO + (c - CTL_KNOB_R1); }

// A Param entry (through its mapping) or a macro (macro >= 0: all its destinations; the popup shows the first). A popup says what it
// drives, its value and, for a col knob that has not caught it, the way to turn.
static void run_param(app_t *app, int layer, const mod_entry_t *me, int macro, input_event_t e) {
    bool changed = false;
    int arrow = 0;
    const mapping_t mp = macro >= 0 ? app->ui.macro[macro].dest[0] : modifiers_mapping(me);
    if (is_col_knob(e.ctl) && e.kind == IN_VALUE) {
        const int row = e.ctl - CTL_COL_KNOB_0 + 1;
        changed = macro >= 0 ? synth_ui_macro_row(&app->ui, &app->params, &app->seq, &app->rack, row, macro, e.value)
                             : synth_ui_knob_row_target(&app->ui, &app->params, &app->seq, &app->rack, row, &mp, e.value);
        const int cd = app->ui.knob_catch_dir[row - 1];
        arrow = cd == 1 || cd == -1 ? cd : 0;               // 127: not measured yet
    } else if (e.kind == IN_VALUE) {
        changed = macro >= 0 ? synth_ui_macro(&app->ui, &app->params, &app->seq, &app->rack, other_knob_slot(e.ctl), macro, e.value)
                             : synth_ui_knob_target(&app->ui, &app->params, &app->seq, &app->rack, other_knob_slot(e.ctl), &mp, e.value);
        catch_stale |= changed;
    } else if (e.kind == IN_DELTA) {
        changed = macro >= 0 ? synth_ui_macro_step(&app->params, &app->seq, &app->rack, &app->ui.macro[macro], e.value)
                             : synth_ui_target_step(&app->params, &app->seq, &app->rack, &mp.t, e.value);
        catch_stale |= changed;
    }
    if (changed) audio_set_params(&app->rack, &app->params);
    char name[24], val[16], t[24];
    if (synth_ui_target_describe(&app->params, &app->seq, &app->rack, &mp.t, name, sizeof name, val, sizeof val)) {
        popup_info(&app->popup, name, val, arrow, audio_millis(), POPUP_INFO_MS);
    } else {
        layer_title(layer, e.ctl, t, sizeof t);
        popup_info(&app->popup, t, "Its module is gone", 0, audio_millis(), POPUP_INFO_MS);
    }
    app->dirty = true;
}

// An Action entry. An absolute knob turns its travel into steps of INPUT_VALUE_MAX / 32 for the step actions.
static void run_layer_action(app_t *app, const mod_entry_t *me, input_event_t e) {
    if (me->act == ACT_NONE) return;
    if (e.kind == IN_VALUE && me->act != ACT_MACRO_LEARN) {
        int16_t *last = &app->in.knob_last[e.ctl];
        const int was = *last - 1;
        *last = (int16_t)(e.value + 1);
        if (was < 0) return;                                 // the first position seen: nothing to step from
        const int d = e.value / 32 - was / 32;
        if (d == 0) return;
        e = (input_event_t){e.ctl, IN_DELTA, d, false};
    }
    const binding_t b = {e.ctl, e.kind, MODS_ANY, (action_id_t)me->act, me->arg};
    app_run_action(app, &b, e);
}

// Shift / Mod + a control. Returns true when the layer took the event; false: the base (table or key layout) handles it.
static bool layer_event(app_t *app, input_event_t e) {
    const bool turn = e.kind == IN_VALUE || e.kind == IN_DELTA;
    if (is_col_knob(e.ctl) && e.kind == IN_VALUE) app->ui.knob_val[e.ctl - CTL_COL_KNOB_0] = e.value;
    if (app->in.shift && app->in.mod && turn && modifiers_ctl_is_knob(e.ctl)) {      // the learn chord, for the layer the MODIFIERS tab shows
        learn(app, app->ui.mods_layer, e.ctl);
        return true;
    }
    const int layer = active_layer(app);
    if (layer < 0) return false;
    const mod_entry_t me = modifiers_get(layer, e.ctl);
    if (me.kind == ME_PARAM) { if (turn) run_param(app, layer, &me, -1, e); return true; }
    if (me.kind == ME_ACTION && me.act == ACT_MACRO && me.arg >= 0 && me.arg < SYNTH_UI_MACROS) {   // a macro: its destinations, like a Param
        if (turn) {
            if (app->ui.macro[me.arg].n == 0) {
                char t[24], x[24];
                layer_title(layer, e.ctl, t, sizeof t);
                snprintf(x, sizeof x, "Macro %d: no target", me.arg + 1);
                popup_info(&app->popup, t, x, 0, audio_millis(), POPUP_INFO_MS);
                app->dirty = true;
            } else run_param(app, layer, &me, me.arg, e);
        }
        return true;
    }
    if (me.kind == ME_ACTION) { run_layer_action(app, &me, e); return true; }
    if (is_col_knob(e.ctl) && e.kind == IN_VALUE) {                          // Default on a col knob with a modifier: nothing to drive
        char t[24];
        layer_title(layer, e.ctl, t, sizeof t);
        popup_info(&app->popup, t, "No target", 0, audio_millis(), POPUP_INFO_MS);
        app->dirty = true;
        return true;
    }
    return false;
}

// On the MODIFIERS tab a key press or a turn of a knob that does not navigate the tab (col knobs, R1..R3) selects that control in the
// tab; with Shift or Mod held, the tab shows that layer. Returns true when the event was used for that.
static bool mods_tab_select(app_t *app, input_event_t e) {
    if (!synth_ui_on_mods_tab(&app->ui, &app->rack) || modifiers_ctl_index(e.ctl) < 0) return false;
    const bool key = e.ctl >= CTL_KEY_FIRST && e.ctl <= CTL_KEY_LAST;
    const bool knob = (is_col_knob(e.ctl) || (e.ctl >= CTL_KNOB_R1 && e.ctl <= CTL_KNOB_R3)) && e.kind == IN_VALUE;
    if (!(key && e.kind == IN_PRESS) && !knob) return false;
    app->ui.mods_ctl = e.ctl;
    if (active_layer(app) >= 0) app->ui.mods_layer = active_layer(app);
    app->dirty = true;
    return true;
}

// Every control but the matrix keys and the joystick axes.
// On the CURVES tab the knobs CURVES_KNOB_X / _Y (bindings.h) move the selected point, absolute over their travel.
static bool curves_tab_knob(app_t *app, input_event_t e) {
    if (e.kind != IN_VALUE || (e.ctl != CURVES_KNOB_X && e.ctl != CURVES_KNOB_Y) || !synth_ui_on_curves_tab(&app->ui, &app->rack)) return false;
    const int k = app->ui.curve_cur % CURVE_USER_MAX, i = app->ui.curve_pt;
    const user_curve_t *u = curve_user(k);
    if (!u || !u->used || i < 0 || i >= u->n) return true;
    const int v = (e.value * 100 + INPUT_VALUE_MAX / 2) / INPUT_VALUE_MAX;
    curve_user_set_point(k, i, e.ctl == CURVES_KNOB_X ? v : u->pt[i].x, e.ctl == CURVES_KNOB_Y ? v : u->pt[i].y);
    app->dirty = true;
    return true;
}

static void control_event(app_t *app, input_event_t e) {
    if (e.kind == IN_RELEASE) { note_off(app, e.ctl); app_dispatch(app, e); return; }   // a layer may have started a note on it
    if (curves_tab_knob(app, e) || mods_tab_select(app, e) || layer_event(app, e)) return;
    app_dispatch(app, e);
}

/* ---------------- the matrix keys: their function comes from the key layout (core/keymap.h) ---------------- */

static void key_event(app_t *app, input_event_t e, uint32_t now) {
    const int key = e.ctl - CTL_KEY_FIRST;
    if (e.kind == IN_RELEASE) {
        note_off(app, e.ctl);
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
    if (!modifier && f.act != ACT_MENU && mods_tab_select(app, e) && f.act != ACT_NOTE) return;   // the MODIFIERS tab: the same
    if (f.act == ACT_SHIFT) app->in.shift_key = e.ctl;
    if (f.act == ACT_MOD)   app->in.mod_key = e.ctl;
    if (!modifier && layer_event(app, e)) return;
    const binding_t b = {e.ctl, IN_PRESS, MODS_ANY, (action_id_t)f.act, f.arg};
    app_run_action(app, &b, e);
}

// The top-left function key held for KEYMAP_RESET_HOLD_MS, pressed during the first seconds after power-on: back to the first built-in layout.
static void key_reset_check(app_t *app, uint32_t now) {
    if (!app->reset_held || now - app->reset_since < KEYMAP_RESET_HOLD_MS) return;
    app->reset_held = false;
    keymap_reset();
    keymap_save();                      // no card yet: saved when it shows up (check_sd)
    app->keys_notice = true;
    app->keys_notice_ms = now;
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
    if (app->ui.joy_xy) {                               // XY mode: the axes drive the bound parameters, no navigation
        bool changed = synth_ui_joy_axis(&app->ui, &app->params, &app->seq, &app->rack, 0, app->in.axis_x);
        changed |= synth_ui_joy_axis(&app->ui, &app->params, &app->seq, &app->rack, 1, app->in.axis_y);
        if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; catch_stale = true; }
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

/* ---------------- notices: the key layout was reset ---------------- */

#define KEYS_NOTICE_MS 3000

static void draw_keys_notice(app_t *app) {
    u8g2_t *g = app->display;
    u8g2_ClearBuffer(g);
    u8g2_SetFont(g, u8g2_font_5x7_tr);
    u8g2_DrawFrame(g, 0, 0, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    u8g2_DrawBox(g, 0, 0, u8g2_GetDisplayWidth(g), 10);
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawStr(g, 4, 8, "KEYS RESET");
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawStr(g, 4, 21, "The keys are back to");
    char l[32];
    snprintf(l, sizeof l, "the layout %s.", keymap_layout_name(0));
    u8g2_DrawStr(g, 4, 30, l);
    u8g2_DrawStr(g, 4, 42, "Release the key.");
}


// One frame: the full-screen notice or the UI, the popup on top, then the buffer goes to the display.
static void draw_frame(app_t *app) {
    u8g2_t *g = app->display;
    if (app->keys_notice) draw_keys_notice(app);   // the full-screen notice hides the popups until it goes (they wait in their queue)
    else {
        synth_ui_draw(&app->ui, &app->params, &app->seq, &app->rack, g);
        gui_style_t st;
        gui_style_init(&st, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
        popup_draw(&app->popup, g, &st);
    }
    display_send(g);
}

// The card changed (inserted, removed, or its files were listed): the sampler modules look their file up again.
// The key layout follows the card: a card that shows up gives its keys.cfg, unless the keys were changed meanwhile (then they are written to it).
static void check_sd(app_t *app) {
    const uint32_t gen = audio_sd_generation();
    if (gen == app->sd_gen) return;
    app->sd_gen = gen;
    if (audio_sd_state() != SD_NONE) {
        if (keymap_dirty()) keymap_save(); else keymap_load();
        if (curves_changed()) curves_save(); else curves_load();
        if (ui_settings_changed(&app->ui, &app->rack)) ui_settings_save(&app->ui, &app->rack); else ui_settings_load(&app->ui, &app->rack);
    }
    audio_build(&app->rack, &app->params);
    app->dirty = true;
}

/* ---------------- UI settings (ui.cfg): saved by themselves a moment after they change ---------------- */

#define SETTINGS_SAVE_MS 2000           // after the first change: a few edits in a row make one write

static void settings_autosave(app_t *app, uint32_t now) {
    const bool ui = ui_settings_changed(&app->ui, &app->rack), cv = curves_changed();      // ui.cfg and curves.cfg
    if (!ui && !cv) { app->settings_pending = false; return; }
    if (!app->settings_pending) { app->settings_pending = true; app->settings_ms = now; return; }
    if (now - app->settings_ms < SETTINGS_SAVE_MS || audio_sd_state() == SD_NONE) return;   // no card: saved when one shows up (check_sd)
    const bool ok = (!cv || curves_save()) & (!ui || ui_settings_save(&app->ui, &app->rack));
    if (ok) app->settings_pending = false;
    else app->settings_ms = now;                                    // try again a bit later
}

/* ---------------- card events (hal_storage.h): what the platform found, told with popups ---------------- */

#define CARD_INFO_MS 2000
#define CARD_QUIET_MS 5000              // no "Card inserted" in the first seconds after start (the boot-time mount); errors and questions still show

static void answer_folders(void *ctx, bool yes) {
    (void)ctx;
    if (yes) storage_make_folders();                    // STORAGE_EV_FOLDERS_DONE follows
}

static void card_events(app_t *app) {
    storage_event_t ev;
    char t[POPUP_TEXT_LEN];
    while (storage_poll_event(&ev)) {
        app->dirty = true;
        switch (ev.kind) {
        case STORAGE_EV_INSERTED:
            if (ev.slow) {
                snprintf(t, sizeof t, "A read takes %u ms (max %u). Samples are off for this card.",
                         (unsigned)((ev.read_us + 500) / 1000), (unsigned)(ev.read_limit_us / 1000));
                popup_error(&app->popup, "SD CARD TOO SLOW", t, NULL, NULL);
            } else if (audio_millis() - app->boot_ms >= CARD_QUIET_MS) {   // a card that was in at power-on is no news
                popup_info(&app->popup, "SD CARD", "Card inserted", 0, audio_millis(), CARD_INFO_MS);
            }
            if (ev.missing) {                           // the card is not set up (or only partly): ask before writing to it
                static const char *const dirs[STORAGE_FOLDER_COUNT] = STORAGE_FOLDERS;
                int n = 0, all = (1 << STORAGE_FOLDER_COUNT) - 1;
                if ((ev.missing & all) == all) n = snprintf(t, sizeof t, "No SynthCore folders on this card. Create them?");
                else {
                    n = snprintf(t, sizeof t, "Missing:");
                    for (int i = 0; i < STORAGE_FOLDER_COUNT && n < (int)sizeof t; i++)
                        if (ev.missing & (1 << i)) n += snprintf(t + n, sizeof t - (size_t)n, " %s", dirs[i]);
                    if (n < (int)sizeof t) snprintf(t + n, sizeof t - (size_t)n, ". Create?");
                }
                popup_ask(&app->popup, "SD CARD", t, false, answer_folders, app);
            }
            break;
        case STORAGE_EV_REMOVED:
            popup_info(&app->popup, "SD CARD", "Card removed", 0, audio_millis(), CARD_INFO_MS);
            break;
        case STORAGE_EV_FOLDERS_DONE:
            if (ev.ok) popup_info(&app->popup, "SD CARD", ev.moved ? "Folders created, old files moved to /system" : "Folders created", 0, audio_millis(), CARD_INFO_MS);
            else popup_error(&app->popup, NULL, "The folders could not be created on the card.", NULL, NULL);
            break;
        default: break;
        }
    }
}

/* ---------------- the step ---------------- */

bool app_step(app_t *app, input_event_t e) {
    if (e.quit) return false;
    app->dirty = false;
    const uint32_t now = audio_millis();

    check_sd(app);
    card_events(app);
    if (app->keys_notice && (e.kind == IN_PRESS || e.kind == IN_RELEASE || now - app->keys_notice_ms > KEYS_NOTICE_MS)) {
        app->keys_notice = false;                                       // a key (the reset key let go) or a few seconds
        app->dirty = true;
    }

    if (e.kind != IN_NONE) {
        if (e.ctl == CTL_JOY_X || e.ctl == CTL_JOY_Y) {
            if (e.ctl == CTL_JOY_X) app->in.axis_x = e.value; else app->in.axis_y = e.value;
            joy_update(app, now);
        } else if (e.ctl >= CTL_KEY_FIRST && e.ctl <= CTL_KEY_LAST) {
            key_event(app, e, now);
        } else {
            control_event(app, e);
        }
    }
    joy_repeat(app, now);
    key_reset_check(app, now);
    settings_autosave(app, now);

    if (app->menu_open && !app->ui.in_rack && keymap_dirty() && !keymap_save())        // the menu was closed: the key layout goes to the card
        snprintf(app->status, sizeof app->status, "Keys not saved (no card)");
    app->menu_open = app->ui.in_rack;

    if (app->ui.rebuild) {              // the rack editor was just left: rebuild the synth from it
        app->ui.rebuild = false;
        audio_build(&app->rack, &app->params);
    }

    if (app->ui.learn_req) {            // a Learn button (MODIFIERS, or MACROS with learn_macro set): back to the pages, the next push on a row assigns it
        app->ui.learn_req = false;
        app->ui.learn_wait = true;
        app->ui.learn_layer = (uint8_t)app->ui.mods_layer;
        app->ui.learn_ctl = (uint8_t)app->ui.mods_ctl;
        if (app->ui.in_rack) ui_event(app, UI_MENU);
        char t[24];
        if (app->ui.learn_macro >= 0) snprintf(t, sizeof t, "MACRO %d", app->ui.learn_macro + 1);
        else layer_title(app->ui.learn_layer, (control_id_t)app->ui.learn_ctl, t, sizeof t);
        popup_info(&app->popup, t, "Push on a row (Back: cancel)", 0, now, 3 * LEARN_INFO_MS);
    }

    // A modifier swaps what the col knobs drive: the catch is measured again against the new targets.
    static int last_layer = -1;
    static unsigned last_rev;
    const int layer = active_layer(app);
    if (layer != last_layer || (layer >= 0 && modifiers_rev() != last_rev)) {
        last_layer = layer; last_rev = modifiers_rev();
        app->dirty = true; catch_stale = true;
    }
    if (popup_tick(&app->popup, now)) app->dirty = true;
    if (catch_stale) {
        catch_stale = false;
        mapping_t tgt[SYNTH_UI_COL_KNOBS];
        app->ui.knob_away = 0;
        for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
            const mod_entry_t me = modifiers_get(layer, (control_id_t)(CTL_COL_KNOB_0 + k));
            const bool page_row = layer < 0;
            const bool macro = me.kind == ME_ACTION && me.act == ACT_MACRO && me.arg >= 0 && me.arg < SYNTH_UI_MACROS;
            memset(&tgt[k], 0, sizeof tgt[k]);
            if (page_row) tgt[k].t.kind = MACRO_PAGE_ROW;
            else if (me.kind == ME_PARAM) tgt[k] = modifiers_mapping(&me);
            else if (macro && app->ui.macro[me.arg].n) tgt[k] = app->ui.macro[me.arg].dest[0];     // the catch follows the first destination
            if (!page_row) app->ui.knob_away |= (uint8_t)(1 << k);
        }
        synth_ui_catch_refresh(&app->ui, &app->params, &app->seq, &app->rack, tgt);
    }

    audio_update();

    // Run indicator animation: created when the sequencer starts, deleted when it stops.
    if (app->seq.running && app->ui.run_anim == GUI_ANIM_INVALID)
        app->ui.run_anim = gui_anim_add(&spr_eq, 0, 3, 120, true);
    else if (!app->seq.running && app->ui.run_anim != GUI_ANIM_INVALID) {
        gui_anim_remove(app->ui.run_anim);
        app->ui.run_anim = GUI_ANIM_INVALID;
    }
    bool animated = gui_anim_tick(audio_millis());

    audio_set_clock(app->seq.bpm, app->seq.steps, app->seq.swing, app->seq.running);   // the motion sequencers follow the note sequencer's timing
    if (app->seq.running && app->seq.fresh) audio_motion_restart();                     // a run starts on this tick: the lanes start with it
    seq_event_t se;
    bool stepped = seq_tick(&app->seq, audio_millis(), &se);
    for (int i = 0; i < se.n_off; i++) audio_note_off(se.off[i]);
    if (se.on)  audio_note_on(se.on);

    // Redraw only when something visible changed. Notes and the modifier do not change the screen. While more input is queued the redraw
    // is owed, not done: every pending note and knob turn is handled first and the screen is drawn once after them.
    const bool want = app->dirty || app->redraw_owed || ((stepped || animated) && synth_ui_shows_playhead(&app->ui, &app->rack));
    if (want && input_pending()) {
        app->redraw_owed = true;
    } else if (want) {
        app->redraw_owed = false;
        draw_frame(app);
    }
    key_leds_update(app, e.kind != IN_NONE, now);
    return true;
}
