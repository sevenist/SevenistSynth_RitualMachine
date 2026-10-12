// The Shift / Mod layers (core/modifiers.h): what a control does while a modifier is held, Learn, and the catch of the column knobs.
#include "app/app_internal.h"
#include "core/modifiers.h"
#include "core/curves.h"
#include <stdio.h>
#include <string.h>

// Set by every manual change (page, row, a value edited by hand, a macro, Shift): the column knobs are checked against what they drive
// before the next draw. A knob move itself never sets it, so a knob that just caught its value keeps it.
bool app_catch_stale;

/* ---------------- learn: a Shift / Mod knob takes the parameter under the cursor (core/modifiers.h) ---------------- */

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
    app_catch_stale = true;
    return true;
}

// While the tab's Learn button waits: a push (Latch / Select) assigns the row under the cursor, Back gives up. True when it took the action.
bool app_learn_push(app_t *app, action_id_t act) {
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

// A Learn button (MODIFIERS, or MACROS with learn_macro set): back to the pages, the next push on a row assigns it.
void app_learn_poll(app_t *app, uint32_t now) {
    if (!app->ui.learn_req) return;
    app->ui.learn_req = false;
    app->ui.learn_wait = true;
    app->ui.learn_layer = (uint8_t)app->ui.mods_layer;
    app->ui.learn_ctl = (uint8_t)app->ui.mods_ctl;
    if (app->ui.in_rack) app_ui_event(app, UI_MENU);
    char t[24];
    if (app->ui.learn_macro >= 0) snprintf(t, sizeof t, "MACRO %d", app->ui.learn_macro + 1);
    else layer_title(app->ui.learn_layer, (control_id_t)app->ui.learn_ctl, t, sizeof t);
    popup_info(&app->popup, t, "Push on a row (Back: cancel)", 0, now, 3 * LEARN_INFO_MS);
}

/* ---------------- the Shift / Mod layers (core/modifiers.h) ---------------- */

static int active_layer(const app_t *app) { return app->in.mod ? MODL_MOD : app->in.shift ? MODL_SHIFT : -1; }

static bool is_col_knob(control_id_t c) { return c >= CTL_COL_KNOB_0 && c <= CTL_COL_KNOB_3; }

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
        app_catch_stale |= changed;
    } else if (e.kind == IN_DELTA) {
        changed = macro >= 0 ? synth_ui_macro_step(&app->params, &app->seq, &app->rack, &app->ui.macro[macro], e.value)
                             : synth_ui_target_step(&app->params, &app->seq, &app->rack, &mp.t, e.value);
        app_catch_stale |= changed;
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
bool app_layer_event(app_t *app, input_event_t e) {
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
bool app_mods_tab_select(app_t *app, input_event_t e) {
    if (!synth_ui_on_mods_tab(&app->ui, &app->rack) || modifiers_ctl_index(e.ctl) < 0) return false;
    const bool key = e.ctl >= CTL_KEY_FIRST && e.ctl <= CTL_KEY_LAST;
    const bool knob = (is_col_knob(e.ctl) || (e.ctl >= CTL_KNOB_R1 && e.ctl <= CTL_KNOB_R3)) && e.kind == IN_VALUE;
    if (!(key && e.kind == IN_PRESS) && !knob) return false;
    app->ui.mods_ctl = e.ctl;
    if (active_layer(app) >= 0) app->ui.mods_layer = active_layer(app);
    app->dirty = true;
    return true;
}

// On the CURVES tab the knobs CURVES_KNOB_X / _Y (bindings.h) move the selected point, absolute over their travel.
bool app_curves_tab_knob(app_t *app, input_event_t e) {
    if (e.kind != IN_VALUE || (e.ctl != CURVES_KNOB_X && e.ctl != CURVES_KNOB_Y) || !synth_ui_on_curves_tab(&app->ui, &app->rack)) return false;
    const int k = app->ui.curve_cur % CURVE_USER_MAX, i = app->ui.curve_pt;
    const user_curve_t *u = curve_user(k);
    if (!u || !u->used || i < 0 || i >= u->n) return true;
    const int v = (e.value * 100 + INPUT_VALUE_MAX / 2) / INPUT_VALUE_MAX;
    curve_user_set_point(k, i, e.ctl == CURVES_KNOB_X ? v : u->pt[i].x, e.ctl == CURVES_KNOB_Y ? v : u->pt[i].y);
    app->dirty = true;
    return true;
}

/* ---------------- the catch of the column knobs ---------------- */

// Every step, before the draw: the column knobs are checked against what they drive when that may have changed.
void app_catch_poll(app_t *app) {
    // A modifier swaps what the col knobs drive: the catch is measured again against the new targets.
    static int last_layer = -1;
    static unsigned last_rev;
    const int layer = active_layer(app);
    if (layer != last_layer || (layer >= 0 && modifiers_rev() != last_rev)) {
        last_layer = layer; last_rev = modifiers_rev();
        app->dirty = true; app_catch_stale = true;
    }
    if (!app_catch_stale) return;
    app_catch_stale = false;
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
