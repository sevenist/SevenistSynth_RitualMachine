// The knob resolver (app_internal.h): what every pot drives now. One answer, read by the knob legend (ADR-046 rules R11 / R15), the
// pots' LEDs (ADR-045, key_leds.c) and the catch of the column knobs, so what the screen says, what the LED shows and what the knob is
// measured against cannot drift apart. The knob's action itself goes by the same rules (app_layer_event, then the binding table:
// ACT_PAGE_KNOB, the macros, the volume); test_knobs.c turns each pot and checks the answer followed.
#include "app/app_internal.h"
#include "core/modifiers.h"
#include "core/led_roles.h"
#include <stdio.h>
#include <string.h>

// Set by every manual change (page, row, a value edited by hand, a macro, Shift): the column knobs are checked against what they drive
// before the next draw. A knob move itself never sets it, so a knob that just caught its value keeps it.
bool app_catch_stale;

static const control_id_t pot_ctl[APP_POTS] = {CTL_COL_KNOB_0, CTL_COL_KNOB_1, CTL_COL_KNOB_2, CTL_COL_KNOB_3,
                                               CTL_KNOB_R1, CTL_KNOB_R2, CTL_KNOB_R3, CTL_VOLUME};

static bool col_caught(const app_t *app, int col) {
    return app->rack.cfg.knob_mode || (app->ui.knob_val[col] >= 0 && app->ui.knob_catch_dir[col] == 0);
}

// Macro k's value: where its knob was put, else its first destination's value; -1 with no destination.
static float macro_norm(app_t *app, int k) {
    const macro_def_t *m = &app->ui.macro[k];
    if (!m->n) return -1.0f;
    const int pos = app->ui.knob_pos[SYNTH_UI_KNOB_MACRO + k];
    if (pos >= 0) return (float)pos / (float)INPUT_VALUE_MAX;
    return synth_ui_target_peek(&app->params, &app->seq, &app->rack, &m->dest[0].t);
}

// A macro entry: the macro's index, or -1 when the entry plays none.
static int entry_macro(const mod_entry_t *me) {
    const bool macro = me->kind == ME_ACTION && (me->act == ACT_MACRO || me->act == ACT_MACRO_LEARN);
    return macro && me->arg >= 0 && me->arg < SYNTH_UI_MACROS ? me->arg : -1;
}

// What a column knob is measured against in a layer (its catch): a parameter through its mapping, a macro's first destination. False:
// nothing to catch (an action, a macro without destination, no entry).
static bool layer_target(const app_t *app, const mod_entry_t *me, mapping_t *out) {
    memset(out, 0, sizeof *out);
    if (me->kind == ME_PARAM) { *out = modifiers_mapping(me); return true; }
    const int m = entry_macro(me);
    if (m < 0 || me->act != ACT_MACRO || !app->ui.macro[m].n) return false;
    *out = app->ui.macro[m].dest[0];
    return true;
}

// Pot `ctl` with layer `layer` held (col: its column knob index, else -1). False: a Default entry on a pot that keeps its own job with the
// modifier (R1..R3, the volume); a column knob with a Default entry drives nothing (app_layer_event says "No target").
static bool layer_drive(app_t *app, int layer, control_id_t ctl, int col, knob_drive_t *d) {
    const mod_entry_t me = modifiers_get(layer, ctl);
    legend_cell_t *c = &d->cell;
    if (me.kind == ME_DEFAULT) return col >= 0;
    if (me.kind == ME_ACTION && me.act == ACT_NONE) return true;
    const bool caught = col < 0 || col_caught(app, col);
    if (col >= 0 && app->ui.knob_val[col] >= 0) c->knob = (float)app->ui.knob_val[col] / (float)INPUT_VALUE_MAX;
    const int m = entry_macro(&me);
    if (m >= 0) {                                                       // a macro, or Learn into it: no catch for Learn
        snprintf(c->label, sizeof c->label, me.act == ACT_MACRO ? "M%d" : "Lrn M%d", m + 1);
        c->state = me.act == ACT_MACRO_LEARN || caught ? 1 : 0;
        d->macro = (int8_t)m;
        if (app->ui.macro[m].n) {                                       // a column knob: its first destination, as its catch
            if (col < 0) c->value = macro_norm(app, m);
            else {
                c->value = synth_ui_target_peek(&app->params, &app->seq, &app->rack, &app->ui.macro[m].dest[0].t);
                if (c->value < 0.0f) c->value = app->ui.knob_norm[col];
            }
        }
        return true;
    }
    if (me.kind == ME_ACTION) {                                         // a setting or a step action: its name, no catch
        if (me.act == ACT_MACRO || me.act == ACT_MACRO_LEARN) return true;   // (a macro out of range: nothing)
        snprintf(c->label, sizeof c->label, "%s", action_name((action_id_t)me.act));
        c->state = 1;
        if (me.act == ACT_MASTER_VOLUME || me.act == ACT_VOLUME_STEP) { c->value = app->rack.cfg.volume / 2.0f; d->role = LR_VOLUME; }
        return true;
    }
    char name[24];                                                      // a parameter: its short name ("FL1 Cut" -> "Cut"), the catch, the value
    if (!synth_ui_target_describe(&app->params, &app->seq, &app->rack, &me.m, name, sizeof name, NULL, 0)) {
        snprintf(c->label, sizeof c->label, "gone");                    // its module was deleted
        c->state = 1;
        return true;
    }
    const char *sp = strrchr(name, ' ');
    snprintf(c->label, sizeof c->label, "%.9s", sp && strlen(name) >= sizeof c->label ? sp + 1 : name);
    c->state = caught ? 1 : 0;
    c->value = synth_ui_target_peek(&app->params, &app->seq, &app->rack, &me.m);
    if (me.m.kind == MACRO_CFG && me.m.prm == CFGP_VOLUME) { c->value = app->rack.cfg.volume / 2.0f; d->role = LR_VOLUME; }   // (walked settings: read here)
    if (me.m.kind == MACRO_CFG && me.m.prm == CFGP_SPEAKER) c->value = app->rack.cfg.speaker / 20.0f;
    if (c->value < 0.0f && col >= 0) c->value = app->ui.knob_norm[col];
    return true;
}

void app_knobs_resolve(app_t *app, knob_drive_t pot[APP_POTS]) {
    const int layer = app_active_layer(app);
    const bool pads_screen = in_pads(app) && !app->ui.in_rack;        // (a synth tab over the pads: the synth's screen)
    legend_cell_t page[SYNTH_UI_COL_KNOBS];
    if (layer < 0 && !pads_screen) synth_ui_legend(&app->ui, &app->params, &app->seq, &app->rack, page);
    for (int i = 0; i < APP_POTS; i++) {
        knob_drive_t *d = &pot[i];
        d->ctl = pot_ctl[i];
        d->macro = -1;
        d->role = LR_KNOB;
        ui_legend_cell_none(&d->cell);
        const int col = i < SYNTH_UI_COL_KNOBS ? i : -1;
        if (layer >= 0 && layer_drive(app, layer, d->ctl, col, d)) continue;
        if (col >= 0) {                                                 // the screen's: the page row, the step, the point, the pad
            if (pads_screen) app_pads_knob_cell(app, col, &d->cell);
            else d->cell = page[col];
        } else if (d->ctl == CTL_VOLUME) {                              // the master volume (0..2), no catch
            snprintf(d->cell.label, sizeof d->cell.label, "Vol");
            d->cell.state = 1;
            d->cell.value = app->rack.cfg.volume / 2.0f;
            d->role = LR_VOLUME;
        } else {                                                        // R1..R3: macros 1..3, set at once (no catch)
            const int k = i - SYNTH_UI_COL_KNOBS;
            snprintf(d->cell.label, sizeof d->cell.label, "M%d", k + 1);
            d->cell.state = 1;
            d->cell.value = macro_norm(app, k);
            d->macro = (int8_t)k;
        }
    }
}

/* ---------------- the catch of the column knobs ---------------- */

// Every step, before the draw: the column knobs are checked against what they drive when that may have changed.
void app_catch_poll(app_t *app) {
    // A modifier swaps what the col knobs drive: the catch is measured again against the new targets.
    static int last_layer = -1;
    static unsigned last_rev;
    const int layer = app_active_layer(app);
    if (layer != last_layer || (layer >= 0 && modifiers_rev() != last_rev)) {
        last_layer = layer; last_rev = modifiers_rev();
        app->dirty = true; app_catch_stale = true;
    }
    if (!app_catch_stale) return;
    app_catch_stale = false;
    mapping_t tgt[SYNTH_UI_COL_KNOBS];
    app->ui.knob_away = 0;
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
        if (layer < 0) { memset(&tgt[k], 0, sizeof tgt[k]); tgt[k].t.kind = MACRO_PAGE_ROW; continue; }
        const mod_entry_t me = modifiers_get(layer, (control_id_t)(CTL_COL_KNOB_0 + k));
        layer_target(app, &me, &tgt[k]);
        app->ui.knob_away |= (uint8_t)(1 << k);
    }
    synth_ui_catch_refresh(&app->ui, &app->params, &app->seq, &app->rack, tgt);
}
