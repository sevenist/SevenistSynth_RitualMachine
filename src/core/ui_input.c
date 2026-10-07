// What events do: editing a row, the menu tabs, knobs and macros, and synth_ui_handle() (the entry point for UI events).
#include "core/ui_screen.h"
#include "core/synth_config.h"
#include "core/curves.h"
#include "hal/hal_input.h"
#include <stdio.h>
#include <string.h>

// Sequencer rows: 1 Step (cursor), 2 Note, 3 Len, 4 Run.
static void handle_seq(synth_ui_t *ui, seq_t *seq, int row, int dir) {
    switch (row) {
        case 1: ui->cursor = (ui->cursor + dir + seq->steps) % seq->steps; break;
        case 2: seq_adjust_note(seq, ui->cursor, dir); break;
        case 3: seq_adjust_len(seq, ui->cursor, dir); break;
        case 4: seq_set_running(seq, !seq->running); break;
    }
}

// Motion sequencer rows. Page 1 (MOTION): 1 Lane, 2 Step, 3 Val, 4 On.  Page 2 (LANE): 1 Lane, 2 Tgt, 3 Mode, 4 Pol, 5 Dpth.
// Returns true when the sound changed (lane data is sent live; a new target or depth rebuilds the graph inside audio_set_params).
static bool handle_ms(synth_ui_t *ui, rack_t *rack, const seq_t *seq, int slot, graph_t graph, int row, int dir) {
    ms_lane_t *l = &rack_ms(rack, slot)->lane[ui->ms_lane];
    if (row == 1) { ui->ms_lane = (ui->ms_lane + dir + MS_LANES) % MS_LANES; return false; }
    if (graph == GRAPH_MS_STEPS) {
        const int n = seq->steps;
        switch (row) {
            case 2: ui->ms_step = (ui->ms_step + dir + n) % n; return false;
            case 3: return rack_ms_adjust_step(l, ui->ms_step, dir);
            default: rack_ms_toggle_step(l, ui->ms_step); return true;
        }
    }
    switch (row) {
        case 2: rack_ms_cycle_target(rack, slot, ui->ms_lane, dir); return true;
        case 3: l->linear ^= 1; return true;
        case 4: l->bipolar ^= 1; return true;
        default: return rack_ms_depth_adjust(rack, slot, l, dir);
    }
}

// SAMPLES tab: 1 File (highlight in the library), 2 Tgt (0 = a new sampler, k = the k-th sampler in the rack), 3 Assign, 4 Scan.
// EG page 1 rows: 1 Pt, 2 Time, 3 Lvl, 4 Crv (of the selected point).
static bool handle_eg(synth_ui_t *ui, rack_t *rack, int slot, int row, int dir) {
    if (row == 1) { ui->eg_pt = (ui->eg_pt + dir + 4) % 4; return false; }
    return rack_mparam_adjust(&rack->slot[slot], 3 * ui->eg_pt + (row - 2), dir);
}

/* ---------------- page rows: one step, absolute (knob) and macros ---------------- */

// One step (dir = +1 / -1) of the parameter on row `row` (1-based) of a page. Returns true when a sound value changed.
static bool page_row_step(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, const page_t *pg, int row, int dir) {
    if (pg->slot != GLOBAL_PAGE && pg->graph == GRAPH_EG) {
        return handle_eg(ui, rack, pg->slot, row, dir);
    } else if (pg->slot != GLOBAL_PAGE && (pg->graph == GRAPH_MS_STEPS || pg->graph == GRAPH_MS_LANE)) {
        if (ui->ms_step >= seq->steps) ui->ms_step = seq->steps - 1;
        return handle_ms(ui, rack, seq, pg->slot, pg->graph, row, dir);
    } else if (pg->slot != GLOBAL_PAGE) {
        rack_slot_t *sl = &rack->slot[pg->slot];
        const int prm = pg->params[row - 1];
        if (prm == PRM_TGT)  { rack_cycle_target(rack, pg->slot, dir); return true; }       // audio_set_params rebuilds the graph
        if (prm == PRM_TPRM) { rack_cycle_param(rack, pg->slot, dir);  return true; }
        if (prm == rack_depth_index((module_type_t)sl->type)) return rack_depth_adjust(rack, pg->slot, dir);     // Dpth: in the target's unit
        const bool changed = rack_mparam_adjust(sl, prm, dir);
        if (sl->type == MOD_SAMPLER) {                  // keep File and Slc inside what exists
            const int files = audio_sample_count();
            if (sl->v[MP_SM_FILE] > (float)files) sl->v[MP_SM_FILE] = (float)files;
            audio_sample_info_t in;
            const int f = (int)sl->v[MP_SM_FILE] - 1;
            const int slices = (f >= 0 && audio_sample_info(f, &in)) ? in.slices : 0;
            if (sl->v[MP_SM_SLICE] > (float)slices) sl->v[MP_SM_SLICE] = (float)slices;
        }
        return changed;
    } else if (graph_is_fm(pg->graph)) {                 // FM pages: the patch's values (live), the envelope point, Patch and Vol
        fm_row_t r;
        if (!fm_page_row(ui, pg, row, &r)) return false;
        if (r.selector) { ui->fm_pt = (ui->fm_pt + dir + 4) % 4; return false; }
        if (r.cfg >= 0) {
            const cfg_effect_t fx = synth_config_adjust(&rack->cfg, (cfg_param_id_t)r.cfg, dir);
            if (fx == CFG_REBUILD) ui->rebuild = true;
            return fx == CFG_LIVE;
        }
        return dx7_value_adjust(&rack->cfg.fm, r.op, r.v, dir);
    } else if (pg->graph == GRAPH_SEQ) {
        handle_seq(ui, seq, row, dir);
        return false;
    } else if (pg->graph == GRAPH_SEQ_CFG) {
        const bool changed = seq_param_adjust(seq, (seq_param_id_t)pg->params[row - 1], dir);
        if (ui->cursor >= seq->steps) ui->cursor = seq->steps - 1;
        return changed;
    }
    return param_adjust(params, (param_id_t)pg->params[row - 1], dir) != 0;
}

// A knob sets a parameter by position. Most parameters only know "one step up / down", so the range is measured by walking to both
// ends (silent: the audio only sees the final value) and then walking back to the step that matches the knob position. Values that can
// be read and written as 0..1 (the FM values, dx7.h) are driven exactly instead: reading them never changes them.
typedef bool (*step_fn)(void *ctx, int dir);
#define KNOB_MAX_STEPS 512

typedef struct {
    step_fn f; void *ctx;                                   // walked one step at a time ...
    float (*get)(void *ctx); bool (*set)(void *ctx, float x); int steps;   // ... or, when get is set: 0..1 over `steps` positions
} knob_drv_t;

static int knob_index(int value, int total) {
    const int idx = (int)((long)value * (total + 1) / (INPUT_VALUE_MAX + 1));
    return idx > total ? total : idx;
}

static void knob_set(const knob_drv_t *d, int value) {
    if (d->get) { if (d->steps > 0) d->set(d->ctx, (float)knob_index(value, d->steps) / (float)d->steps); return; }
    int total = 0;
    for (int i = 0; i < KNOB_MAX_STEPS && d->f(d->ctx, -1); i++) {}
    while (total < KNOB_MAX_STEPS && d->f(d->ctx, +1)) total++;
    if (total == 0) return;
    const int idx = knob_index(value, total);
    for (int i = total; i > idx; i--) d->f(d->ctx, -1);
}

// Measures a parameter without changing it: returns its current step index (0 = min) and puts the number of steps from min to max in *total.
// A walked parameter goes to the bottom, up to the top, and back down to where it was.
static int knob_measure(const knob_drv_t *d, int *total) {
    if (d->get) { *total = d->steps; return (int)(d->get(d->ctx) * (float)d->steps + 0.5f); }
    int down = 0, up = 0;
    while (down < KNOB_MAX_STEPS && d->f(d->ctx, -1)) down++;
    while (up < KNOB_MAX_STEPS && d->f(d->ctx, +1)) up++;
    for (int i = up; i > down; i--) d->f(d->ctx, -1);
    *total = up;
    return down;
}

// An FM value (dx7.h), driven exactly.
typedef struct { dx7_patch_t *p; int op, v; } fm_ctx_t;
static float fm_get(void *c) { const fm_ctx_t *x = c; return dx7_value_get(x->p, x->op, x->v); }
static bool  fm_set(void *c, float v) { const fm_ctx_t *x = c; return dx7_value_set(x->p, x->op, x->v, v); }
static knob_drv_t fm_drv(fm_ctx_t *c) { return (knob_drv_t){NULL, c, fm_get, fm_set, dx7_value_steps(c->op, c->v)}; }

// Continuous parameters (user 2026-10-07): a linear / logarithmic module or global parameter is driven exactly over the knob's whole resolution
// (rack_mparam_set_norm / param_set_norm, rounded to the decimals shown) instead of walked in its 0.05 steps. Not the modulation depth (in its
// target's unit), not the sampler's slice (the page keeps it inside the file's slices).
#define KNOB_CONT_STEPS INPUT_VALUE_MAX
typedef struct { rack_slot_t *s; int i; synth_params_t *p; int id; } cont_ctx_t;
static float cont_get(void *c) { const cont_ctx_t *x = c; return x->s ? rack_mparam_norm(x->s, x->i) : param_norm(x->p, (param_id_t)x->id); }
static bool  cont_set(void *c, float n) { const cont_ctx_t *x = c; return x->s ? rack_mparam_set_norm(x->s, x->i, n) : param_set_norm(x->p, (param_id_t)x->id, n); }
static knob_drv_t cont_drv(cont_ctx_t *c) { return (knob_drv_t){NULL, c, cont_get, cont_set, KNOB_CONT_STEPS}; }

typedef struct { fm_ctx_t fm; cont_ctx_t cont; } drv_ctx_t;   // what an exact driver points at (one of them)

static bool module_continuous(const rack_slot_t *s, int i) {
    const module_type_t t = (module_type_t)s->type;
    return rack_mparam_is_continuous(t, i) && i != rack_depth_index(t) && !(t == MOD_SAMPLER && i == MP_SM_SLICE);
}

// Knob bookkeeping. `key` identifies what the knob drives.
// Returns true when the event should be applied. For col knobs (knob < SYNTH_UI_COL_KNOBS): catch mode — the knob is
// ignored until it crosses the current parameter value; once caught it applies normally. knob_catch_dir is 0 when caught.
static bool knob_new_position(synth_ui_t *ui, int knob, int key, int value) {
    const int pos = value;                              // the whole resolution: continuous parameters use it (the noise is filtered by the HAL)
    const bool key_changed = ui->knob_key[knob] != key;
    if (!key_changed && ui->knob_pos[knob] == pos) return false;
    ui->knob_key[knob] = key; ui->knob_pos[knob] = pos;
    if (knob >= SYNTH_UI_COL_KNOBS) return true;       // macro / volume knobs: no catch
    if (key_changed) ui->knob_catch_dir[knob] = 127;   // new parameter: force re-catch (sentinel, resolved on first step_fn call)
    return true;
}

// For col knobs: check catch and update knob_catch_dir. Returns true if the knob is caught and knob_set should run.
// param_pos_out receives the current step index so knob_set can skip the re-walk when already caught.
static bool knob_check_catch(synth_ui_t *ui, const rack_t *rack, int knob, int value, const knob_drv_t *d) {
    if (rack->cfg.knob_mode) { ui->knob_catch_dir[knob] = 0; return true; }   // Direct mode: no catch, no arrow
    if (ui->knob_catch_dir[knob] == 0) return true;    // already caught
    int total;
    const int cur = knob_measure(d, &total);
    const int knob_idx = knob_index(value, total);
    const int diff = knob_idx - cur;                   // > 0: the knob is above the value, so it has to be turned down
    const int dir = diff > 0 ? -1 : 1;
    // Caught on an exact hit, or when the knob has crossed the value since it was last seen (a fast turn skips the exact step).
    const int prev = ui->knob_catch_dir[knob];
    if (diff == 0 || (prev != 127 && prev != 0 && prev != dir)) { ui->knob_catch_dir[knob] = 0; return true; }
    ui->knob_catch_dir[knob] = (int8_t)dir;
    return false;
}

typedef struct { synth_ui_t *ui; synth_params_t *params; seq_t *seq; rack_t *rack; const page_t *pg; int row; } row_ctx_t;
static bool row_step(void *c, int dir) { row_ctx_t *x = c; return page_row_step(x->ui, x->params, x->seq, x->rack, x->pg, x->row, dir); }

// A page row's knob driver: exact for an FM value and a continuous parameter, walking otherwise.
static knob_drv_t row_drv(row_ctx_t *rc, drv_ctx_t *dc) {
    fm_ctx_t *fc = &dc->fm;
    cont_ctx_t *cc = &dc->cont;
    fm_row_t r;
    const page_t *pg = rc->pg;
    if (graph_is_fm(pg->graph)) {
        if (fm_page_row(rc->ui, pg, rc->row, &r) && !r.selector && r.cfg < 0) { *fc = (fm_ctx_t){&rc->rack->cfg.fm, r.op, r.v}; return fm_drv(fc); }
    } else if (pg->slot != GLOBAL_PAGE && pg->graph != GRAPH_MS_STEPS && pg->graph != GRAPH_MS_LANE) {
        rack_slot_t *s = &rc->rack->slot[pg->slot];
        const int i = pg->graph == GRAPH_EG ? (rc->row >= 2 ? 3 * rc->ui->eg_pt + (rc->row - 2) : -1) : pg->params[rc->row - 1];
        if (i >= 0 && module_continuous(s, i)) { *cc = (cont_ctx_t){s, i, NULL, 0}; return cont_drv(cc); }
    } else if (pg->slot == GLOBAL_PAGE && pg->graph != GRAPH_SEQ && pg->graph != GRAPH_SEQ_CFG) {
        const int id = pg->params[rc->row - 1];
        if (param_is_continuous((param_id_t)id)) { *cc = (cont_ctx_t){NULL, 0, rc->params, id}; return cont_drv(cc); }
    }
    return (knob_drv_t){row_step, rc, NULL, NULL, 0};
}

// Rows a knob may drive: plain values. Not the ones that select or cycle (targets, motion / sequencer step editors, FM patch, sample file).
static bool row_is_knobbable(const rack_t *rack, const page_t *pg, int row) {
    if (graph_is_fm(pg->graph)) {                        // not the envelope point selector, not Patch (a walk would load every patch)
        fm_row_t r;
        return fm_page_row(NULL, pg, row, &r) && !r.selector && r.cfg != CFGP_PATCH;
    }
    if (pg->graph == GRAPH_SEQ || pg->graph == GRAPH_MS_STEPS || pg->graph == GRAPH_MS_LANE) return false;
    if (pg->graph == GRAPH_EG && row == 1) return false;
    if (pg->slot == GLOBAL_PAGE) return true;
    const int prm = pg->params[row - 1];
    return prm != PRM_TGT && prm != PRM_TPRM && !(rack->slot[pg->slot].type == MOD_SAMPLER && prm == MP_SM_FILE);
}

bool synth_ui_knob_row(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int row, int value) {
    if (row >= 1 && row <= SYNTH_UI_COL_KNOBS) ui->knob_val[row - 1] = value;
    if (ui->in_rack || row < 1 || row > SYNTH_UI_COL_KNOBS) return false;
    page_t pg;
    get_page(ui, rack, ui->page, &pg);
    if (row > pg.count || !row_is_knobbable(rack, &pg, row)) return false;
    if (!knob_new_position(ui, row - 1, ui->page * 8 + row, value)) return false;
    row_ctx_t c = {ui, params, seq, rack, &pg, row};
    drv_ctx_t fc;
    const knob_drv_t d = row_drv(&c, &fc);
    if (!knob_check_catch(ui, rack, row - 1, value, &d)) return false;
    knob_set(&d, value);
    ui->knob_cur[row - 1] = -2;
    return true;
}

static bool volume_step(void *c, int dir) { return synth_config_adjust(&((rack_t *)c)->cfg, CFGP_VOLUME, dir) != CFG_UNCHANGED; }

bool synth_ui_set_volume(synth_ui_t *ui, rack_t *rack, int value) {
    if (!knob_new_position(ui, SYNTH_UI_KNOB_VOLUME, 0, value)) return false;
    const knob_drv_t d = {volume_step, rack, NULL, NULL, 0};
    knob_set(&d, value);
    return true;
}

/* Targets: a parameter a knob drives besides the page rows (macro_t), through a mapping (mapping_t: Min / Max / curve, core/curves.h).
 * Shift / Mod + a knob can drive one (core/modifiers.h); a macro drives up to SYNTH_UI_MACRO_DESTS of them at once. A module is referred to
 * by its id, so a target survives inserting modules and does nothing once its module is gone. */

static void macro_clear(macro_t *m) { m->kind = MACRO_NONE; m->id = 0; m->prm = 0; }

typedef struct { synth_params_t *params; seq_t *seq; rack_t *rack; macro_t m; int slot; } macro_ctx_t;
static bool macro_step(void *c, int dir) {
    macro_ctx_t *x = c;
    switch (x->m.kind) {
        case MACRO_MODULE:
            if (x->m.prm == rack_depth_index((module_type_t)x->rack->slot[x->slot].type)) return rack_depth_adjust(x->rack, x->slot, dir);
            return rack_mparam_adjust(&x->rack->slot[x->slot], x->m.prm, dir);
        case MACRO_GLOBAL: return param_adjust(x->params, (param_id_t)x->m.prm, dir) != 0;
        case MACRO_SEQ:    return seq_param_adjust(x->seq, (seq_param_id_t)x->m.prm, dir);
        case MACRO_CFG:    return synth_config_adjust(&x->rack->cfg, (cfg_param_id_t)x->m.prm, dir) != CFG_UNCHANGED;
        case MACRO_FM:     return dx7_value_adjust(&x->rack->cfg.fm, x->m.id, x->m.prm, dir);
        default:           return false;
    }
}

// Ready to step: the module's slot looked up. False when there is nothing to drive.
static bool target_ctx(synth_params_t *params, seq_t *seq, rack_t *rack, const macro_t *m, macro_ctx_t *c) {
    *c = (macro_ctx_t){params, seq, rack, *m, RACK_NONE};
    if (m->kind == MACRO_NONE) return false;
    if (m->kind == MACRO_MODULE && (c->slot = rack_find(rack, m->id)) == RACK_NONE) return false;
    if (m->kind == MACRO_FM && !dx7_value_valid(m->id, m->prm)) return false;
    return true;
}

// A target's knob driver: exact for an FM value and a continuous parameter, walking otherwise.
static knob_drv_t target_drv(macro_ctx_t *c, drv_ctx_t *dc) {
    if (c->m.kind == MACRO_FM) { dc->fm = (fm_ctx_t){&c->rack->cfg.fm, c->m.id, c->m.prm}; return fm_drv(&dc->fm); }
    if (c->m.kind == MACRO_MODULE && module_continuous(&c->rack->slot[c->slot], c->m.prm)) {
        dc->cont = (cont_ctx_t){&c->rack->slot[c->slot], c->m.prm, NULL, 0};
        return cont_drv(&dc->cont);
    }
    if (c->m.kind == MACRO_GLOBAL && param_is_continuous((param_id_t)c->m.prm)) {
        dc->cont = (cont_ctx_t){NULL, 0, c->params, c->m.prm};
        return cont_drv(&dc->cont);
    }
    return (knob_drv_t){macro_step, c, NULL, NULL, 0};
}

static int target_key(const macro_t *m) { return 0x100000 | m->kind << 16 | m->id << 8 | m->prm; }   // never a page knob's key

// Sets a driven value to n (0..1 of its range): exact values directly, walked ones to the nearest step from where they are.
static void drv_set_norm(const knob_drv_t *d, float n) {
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    if (d->get) { d->set(d->ctx, n); return; }
    int total;
    const int cur = knob_measure(d, &total);
    const int idx = (int)(n * (float)total + 0.5f);
    for (int i = cur; i < idx; i++) d->f(d->ctx, +1);
    for (int i = cur; i > idx; i--) d->f(d->ctx, -1);
}

// The catch of a mapped target, in knob positions: caught when the knob's output lands on the current step, or once the knob crossed the
// first position that maps to the current value (prev: the last way to turn, 127 = unknown). Returns the way to turn, 0 = caught.
static int mapped_catch_dir(const mapping_t *mp, int value, int cur, int total, int prev) {
    if (total <= 0) return 0;
    const int out = (int)(mapping_apply(mp, (float)value / INPUT_VALUE_MAX) * (float)total + 0.5f);
    if (out == cur) return 0;
    const int kp = value * (MAPPING_POSITIONS - 1) / INPUT_VALUE_MAX;
    const int tp = mapping_inverse(mp, (float)cur / (float)total);
    const int dir = kp > tp ? -1 : 1;
    if (prev != 127 && prev != 0 && prev != dir) return 0;
    return dir;
}

// A col knob on a mapped target: the catch (Catch mode), then the value. False while not caught.
static bool col_knob_mapped(synth_ui_t *ui, const rack_t *rack, int k, int value, const knob_drv_t *d, const mapping_t *mp) {
    if (rack->cfg.knob_mode) ui->knob_catch_dir[k] = 0;
    else if (ui->knob_catch_dir[k] != 0) {
        int total;
        const int cur = knob_measure(d, &total);
        ui->knob_catch_dir[k] = (int8_t)mapped_catch_dir(mp, value, cur, total, ui->knob_catch_dir[k]);
        if (ui->knob_catch_dir[k] != 0) return false;
    }
    drv_set_norm(d, mapping_apply(mp, (float)value / INPUT_VALUE_MAX));
    ui->knob_cur[k] = -2;
    return true;
}

bool synth_ui_knob_row_target(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int row, const mapping_t *mp, int value) {
    if (row < 1 || row > SYNTH_UI_COL_KNOBS) return false;
    const int k = row - 1;
    ui->knob_val[k] = value;
    macro_ctx_t c;
    if (!target_ctx(params, seq, rack, &mp->t, &c)) return false;
    if (!knob_new_position(ui, k, target_key(&mp->t), value)) return false;
    drv_ctx_t fc;
    const knob_drv_t d = target_drv(&c, &fc);
    return col_knob_mapped(ui, rack, k, value, &d, mp);
}

bool synth_ui_knob_target(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int knob, const mapping_t *mp, int value) {
    macro_ctx_t c;
    if (knob < SYNTH_UI_COL_KNOBS || knob >= SYNTH_UI_KNOBS || !target_ctx(params, seq, rack, &mp->t, &c)) return false;
    if (!knob_new_position(ui, knob, target_key(&mp->t), value)) return false;
    drv_ctx_t fc;
    const knob_drv_t d = target_drv(&c, &fc);
    drv_set_norm(&d, mapping_apply(mp, (float)value / INPUT_VALUE_MAX));
    return true;
}

bool synth_ui_target_step(synth_params_t *params, seq_t *seq, rack_t *rack, const macro_t *m, int n) {
    macro_ctx_t c;
    if (!target_ctx(params, seq, rack, m, &c)) return false;
    bool changed = false;
    for (int i = 0; i < (n < 0 ? -n : n) && i < KNOB_MAX_STEPS; i++) changed |= macro_step(&c, n < 0 ? -1 : 1);
    return changed;
}

float synth_ui_target_norm(synth_params_t *params, seq_t *seq, rack_t *rack, const macro_t *m) {
    macro_ctx_t c;
    drv_ctx_t fc;
    if (!target_ctx(params, seq, rack, m, &c)) return -1.0f;
    const knob_drv_t d = target_drv(&c, &fc);
    int total;
    const int cur = knob_measure(&d, &total);
    return total > 0 ? (float)cur / (float)total : 0.0f;
}

bool synth_ui_target_describe(const synth_params_t *params, const seq_t *seq, const rack_t *rack, const macro_t *m, char *name, int nn, char *value, int nv) {
    switch (m->kind) {
        case MACRO_MODULE: {
            const int slot = rack_find(rack, m->id);
            if (slot == RACK_NONE) return false;
            const module_type_t t = (module_type_t)rack->slot[slot].type;
            char nm[8];
            rack_slot_name(rack, slot, nm, sizeof nm);
            snprintf(name, (size_t)nn, "%s %s", nm, rack_mparam_label(t, m->prm));
            if (!value) return true;
            if (m->prm == rack_depth_index(t)) rack_depth_format(rack, slot, value, nv);
            else rack_mparam_format(&rack->slot[slot], m->prm, value, nv);
            return true;
        }
        case MACRO_GLOBAL:
            snprintf(name, (size_t)nn, "%s", param_label((param_id_t)m->prm));
            if (value) param_format(params, (param_id_t)m->prm, value, (size_t)nv);
            return true;
        case MACRO_SEQ:
            snprintf(name, (size_t)nn, "%s", seq_param_label((seq_param_id_t)m->prm));
            if (value) seq_param_format(seq, (seq_param_id_t)m->prm, value, (size_t)nv);
            return true;
        case MACRO_CFG:
            snprintf(name, (size_t)nn, "%s", synth_config_label((cfg_param_id_t)m->prm));
            if (value) synth_config_format(&rack->cfg, (cfg_param_id_t)m->prm, value, (size_t)nv);
            return true;
        case MACRO_FM:
            if (!dx7_value_valid(m->id, m->prm)) return false;
            if (m->id == DX7_GLOBAL_OP) snprintf(name, (size_t)nn, "FM %s", dx7_value_label(m->id, m->prm));
            else                        snprintf(name, (size_t)nn, "OP%d %s", m->id + 1, dx7_value_label(m->id, m->prm));
            if (value) dx7_value_format(&rack->cfg.fm, m->id, m->prm, value, (size_t)nv);
            return true;
        default: return false;
    }
}

bool synth_ui_target_at_cursor(const synth_ui_t *ui, const rack_t *rack, macro_t *out) {
    macro_clear(out);
    if (ui->in_rack) {                                  // the GENERAL tab: a setting that changes live (not Type, Voices, PEnv: they rebuild)
        if (tab_kind(rack, ui->menu_tab) != TAB_GENERAL) return false;
        const int id = scr_general_setting(ui->row);
        if (id < 0 || id == CFGP_TYPE || id == CFGP_VOICES || id == CFGP_PARA_ENV) return false;
        *out = (macro_t){MACRO_CFG, 0, (uint8_t)id};
        return true;
    }
    page_t pg;
    get_page(ui, rack, ui->page, &pg);
    const int row = ui->row;
    if (row < 1 || row > pg.count || !row_is_knobbable(rack, &pg, row)) return false;
    fm_row_t r;
    if (graph_is_fm(pg.graph) && fm_page_row(ui, &pg, row, &r)) {
        *out = r.cfg >= 0 ? (macro_t){MACRO_CFG, 0, (uint8_t)r.cfg} : (macro_t){MACRO_FM, (uint8_t)r.op, (uint8_t)r.v};
    } else if (pg.slot != GLOBAL_PAGE) {
        *out = (macro_t){MACRO_MODULE, rack->slot[pg.slot].id, (uint8_t)(pg.graph == GRAPH_EG ? 3 * ui->eg_pt + (row - 2) : pg.params[row - 1])};
    } else if (pg.graph == GRAPH_SEQ_CFG) *out = (macro_t){MACRO_SEQ, 0, (uint8_t)pg.params[row - 1]};
    else                                  *out = (macro_t){MACRO_GLOBAL, 0, (uint8_t)pg.params[row - 1]};
    return true;
}

/* ---------------- macros: one knob, several destinations ---------------- */

static bool same_target(const macro_t *a, const macro_t *b) { return a->kind == b->kind && a->id == b->id && a->prm == b->prm; }

static bool macro_dest_set(synth_params_t *params, seq_t *seq, rack_t *rack, const mapping_t *mp, float knob) {
    macro_ctx_t c;
    drv_ctx_t fc;
    if (!target_ctx(params, seq, rack, &mp->t, &c)) return false;
    const knob_drv_t d = target_drv(&c, &fc);
    drv_set_norm(&d, mapping_apply(mp, knob));
    return true;
}

bool synth_ui_macro(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int knob, int k, int value) {
    if (k < 0 || k >= SYNTH_UI_MACROS || ui->macro[k].n == 0) return false;
    if (knob >= SYNTH_UI_COL_KNOBS && knob < SYNTH_UI_KNOBS && !knob_new_position(ui, knob, 0x200000 | k, value)) return false;
    bool any = false;
    for (int i = 0; i < ui->macro[k].n; i++) any |= macro_dest_set(params, seq, rack, &ui->macro[k].dest[i], (float)value / INPUT_VALUE_MAX);
    return any;
}

bool synth_ui_macro_row(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int row, int k, int value) {
    if (row < 1 || row > SYNTH_UI_COL_KNOBS || k < 0 || k >= SYNTH_UI_MACROS) return false;
    const int kk = row - 1;
    ui->knob_val[kk] = value;
    const macro_def_t *md = &ui->macro[k];
    if (md->n == 0) return false;
    macro_ctx_t c;
    drv_ctx_t fc;
    if (!target_ctx(params, seq, rack, &md->dest[0].t, &c)) return false;
    if (!knob_new_position(ui, kk, target_key(&md->dest[0].t), value)) return false;
    const knob_drv_t d = target_drv(&c, &fc);
    if (!col_knob_mapped(ui, rack, kk, value, &d, &md->dest[0])) return false;    // the catch follows the first destination
    for (int i = 1; i < md->n; i++) macro_dest_set(params, seq, rack, &md->dest[i], (float)value / INPUT_VALUE_MAX);
    return true;
}

bool synth_ui_macro_step(synth_params_t *params, seq_t *seq, rack_t *rack, const macro_def_t *m, int n) {
    bool any = false;
    for (int i = 0; i < m->n; i++) any |= synth_ui_target_step(params, seq, rack, &m->dest[i].t, n);
    return any;
}

bool synth_ui_macro_add(synth_ui_t *ui, int k, const macro_t *t) {
    if (k < 0 || k >= SYNTH_UI_MACROS || t->kind == MACRO_NONE) return false;
    macro_def_t *md = &ui->macro[k];
    for (int i = 0; i < md->n; i++) if (same_target(&md->dest[i].t, t)) return false;
    if (md->n >= SYNTH_UI_MACRO_DESTS) return false;
    md->dest[md->n++] = (mapping_t){*t, 0, 0, 0};
    ui->knob_key[SYNTH_UI_KNOB_MACRO + k] = -1;         // the next movement applies at once
    return true;
}

bool synth_ui_macro_learn(synth_ui_t *ui, const rack_t *rack, int k) {
    macro_t m;
    return synth_ui_target_at_cursor(ui, rack, &m) && synth_ui_macro_add(ui, k, &m);
}

void synth_ui_macro_remove(synth_ui_t *ui, int k, int dest) {
    if (k < 0 || k >= SYNTH_UI_MACROS) return;
    macro_def_t *md = &ui->macro[k];
    if (dest < 0 || dest >= md->n) return;
    for (int i = dest; i + 1 < md->n; i++) md->dest[i] = md->dest[i + 1];
    md->n--;
    memset(&md->dest[md->n], 0, sizeof md->dest[md->n]);
}

void synth_ui_macro_describe(const synth_ui_t *ui, const rack_t *rack, int k, char *out, int n) {
    char name[24];
    const macro_def_t *md = &ui->macro[k];
    if (md->n == 0 || !synth_ui_target_describe(NULL, NULL, rack, &md->dest[0].t, name, sizeof name, NULL, 0)) {
        snprintf(out, (size_t)n, "M%d (unassigned)", k + 1);
        return;
    }
    if (md->n > 1) snprintf(out, (size_t)n, "M%d > %s +%d", k + 1, name, md->n - 1);
    else           snprintf(out, (size_t)n, "M%d > %s", k + 1, name);
}

void synth_ui_macros_prune(synth_ui_t *ui, const rack_t *rack) {
    for (int k = 0; k < SYNTH_UI_MACROS; k++)
        for (int i = ui->macro[k].n - 1; i >= 0; i--) {
            const macro_t *t = &ui->macro[k].dest[i].t;
            if (t->kind == MACRO_MODULE && rack_find(rack, t->id) == RACK_NONE) synth_ui_macro_remove(ui, k, i);
        }
    for (int a = 0; a < 2; a++) {                       // the joystick axes too
        const macro_t *t = &ui->joy[a].t;
        if (t->kind == MACRO_MODULE && rack_find(rack, t->id) == RACK_NONE) { memset(&ui->joy[a], 0, sizeof ui->joy[a]); ui->joy_fresh = -1; }
    }
}

/* ---------------- the joystick as an XY controller ---------------- */

static void mapping_invert(mapping_t *m) {              // Min <-> Max (Min > Max inverts, synth_ui.h)
    const int lo = m->min, hi = mapping_max(m);
    m->min = (uint8_t)hi; m->max_off = (uint8_t)(100 - lo);
}

joy_click_t synth_ui_joy_click(synth_ui_t *ui, const rack_t *rack, int *axis) {
    macro_t t;
    if (!synth_ui_target_at_cursor(ui, rack, &t)) return JOY_CLICK_NONE;
    const int f = ui->joy_fresh;
    if (f >= 0 && same_target(&ui->joy[f].t, &t)) {    // the parameter just bound, again: it goes to the other axis, the one it replaced comes back
        const mapping_t m = ui->joy[f];
        ui->joy[0] = ui->joy_prev[0]; ui->joy[1] = ui->joy_prev[1];
        ui->joy[1 - f] = m;
        ui->joy_fresh = (int8_t)(1 - f); ui->joy_next = (uint8_t)f;
        *axis = 1 - f;
        return JOY_CLICK_MOVED;
    }
    for (int a = 0; a < 2; a++)
        if (same_target(&ui->joy[a].t, &t)) {           // already bound (not just now): its axis is inverted
            mapping_invert(&ui->joy[a]);
            ui->joy_fresh = -1;
            *axis = a;
            return JOY_CLICK_INVERTED;
        }
    const int a = ui->joy_next % 2;                     // a new one replaces the older binding: X, Y, X, Y ...
    ui->joy_prev[0] = ui->joy[0]; ui->joy_prev[1] = ui->joy[1];
    ui->joy[a] = (mapping_t){t, 0, 0, 0};
    ui->joy_fresh = (int8_t)a; ui->joy_next = (uint8_t)(1 - a);
    *axis = a;
    return JOY_CLICK_BOUND;
}

void synth_ui_joy_mode(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, bool on) {
    if (ui->joy_xy)                                     // leaving (or restarting) XY mode: an axis away from its centre goes back to its value
        for (int a = 0; a < 2; a++) synth_ui_joy_axis(ui, params, seq, rack, a, INPUT_AXIS_CENTER);
    ui->joy_xy = on;
    ui->joy_fresh = -1;                                 // after a mode change a click on a bound parameter inverts it
    for (int a = 0; a < 2; a++) { ui->joy_last[a] = JOY_POS_CENTRE; ui->joy_rest[a] = -1.0f; }
}

// The stick's rest value follows the parameter: it is read again each time the stick leaves the centre, so a value set meanwhile by a knob,
// an encoder or the navigation is the one the stick moves around. Back in the centre the stick puts that value back, unless something
// else changed the parameter during the push (it is then not where the stick put it): that change is kept.
bool synth_ui_joy_axis(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int axis, int value) {
    if (!ui->joy_xy || axis < 0 || axis > 1) return false;
    int d = axis == 0 ? value - INPUT_AXIS_CENTER : INPUT_AXIS_CENTER - value;     // right / up = positive (a low Y value is pushed up)
    const int span = INPUT_AXIS_CENTER - JOY_XY_DEADZONE;
    d = d > JOY_XY_DEADZONE ? d - JOY_XY_DEADZONE : d < -JOY_XY_DEADZONE ? d + JOY_XY_DEADZONE : 0;
    const float x = (float)(d > span ? span : d < -span ? -span : d) / (float)span;
    const int pos = d == 0 ? JOY_POS_CENTRE : (int)(x * 127.0f) + JOY_POS_CENTRE;   // 1..255: nothing to do while the stick stays on the same position
    const int last = ui->joy_last[axis];
    if (pos == last) return false;
    ui->joy_last[axis] = (int16_t)pos;
    macro_ctx_t c;
    drv_ctx_t fc;
    if (!target_ctx(params, seq, rack, &ui->joy[axis].t, &c)) return false;
    const knob_drv_t drv = target_drv(&c, &fc);
    int total, cur;
    if (pos == JOY_POS_CENTRE) {                        // let go
        if (ui->joy_rest[axis] < 0.0f) return false;
        cur = knob_measure(&drv, &total);
        const bool untouched = cur == (int)(ui->joy_wrote[axis] * (float)total + 0.5f);
        if (untouched) drv_set_norm(&drv, ui->joy_rest[axis]);   // exactly the value it had
        ui->joy_rest[axis] = -1.0f;
        return untouched;
    }
    if (last == JOY_POS_CENTRE || ui->joy_rest[axis] < 0.0f) {   // leaving the centre: the value now is the one to move around
        cur = knob_measure(&drv, &total);
        ui->joy_rest[axis] = total > 0 ? (float)cur / (float)total : 0.0f;
        ui->joy_centre[axis] = (float)mapping_inverse(&ui->joy[axis], ui->joy_rest[axis]) / (float)(MAPPING_POSITIONS - 1);
    }
    const float ctr = ui->joy_centre[axis];
    float n = mapping_apply(&ui->joy[axis], ctr + x * (x > 0.0f ? 1.0f - ctr : ctr));
    n = n < 0.0f ? 0.0f : n > 1.0f ? 1.0f : n;
    drv_set_norm(&drv, n);
    ui->joy_wrote[axis] = n;
    return true;
}

void synth_ui_catch_refresh(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, const mapping_t *tgt) {
    page_t pg;
    if (!ui->in_rack) get_page(ui, rack, ui->page, &pg);
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
        int8_t dir = 0;
        int key = -1;
        bool have = false;
        const mapping_t *mp = NULL;
        knob_drv_t d;
        row_ctx_t rc;
        macro_ctx_t mc;
        drv_ctx_t fc;
        if (!ui->in_rack && ui->knob_val[k] >= 0 && !rack->cfg.knob_mode) {
            if (tgt && tgt[k].t.kind != MACRO_PAGE_ROW) {
                if (target_ctx(params, seq, rack, &tgt[k].t, &mc)) { d = target_drv(&mc, &fc); key = target_key(&tgt[k].t); mp = &tgt[k]; have = true; }
            } else if (k + 1 <= pg.count && row_is_knobbable(rack, &pg, k + 1)) {
                rc = (row_ctx_t){ui, params, seq, rack, &pg, k + 1}; d = row_drv(&rc, &fc); key = ui->page * 8 + k + 1; have = true;
            }
        }
        if (have) {
            int total;
            const int cur = knob_measure(&d, &total);
            // A knob that set the value itself stays caught until something else changes the parameter (walking can measure it one step off).
            // Only for the same target: a modifier swaps the target, and a knob that set the page value has not caught the modifier's target.
            const bool same = ui->knob_key[k] == key;
            const bool kept = same && (ui->knob_cur[k] == -2 || (ui->knob_catch_dir[k] == 0 && ui->knob_cur[k] == cur));
            if (kept) dir = 0;
            else if (mp) dir = (int8_t)mapped_catch_dir(mp, ui->knob_val[k], cur, total, 127);
            else {
                const int idx = knob_index(ui->knob_val[k], total);
                dir = (int8_t)(idx == cur ? 0 : idx > cur ? -1 : 1);
            }
            ui->knob_cur[k] = cur;
            ui->knob_key[k] = key;
        } else {
            ui->knob_cur[k] = -1;
        }
        ui->knob_catch_dir[k] = dir;
    }
}

// The macros start on the first filter's cutoff and resonance and the first LFO's rate (one destination each), when the rack has them.
void macros_default(synth_ui_t *ui, const rack_t *rack) {
    memset(ui->macro, 0, sizeof ui->macro);
    int fl = RACK_NONE, lf = RACK_NONE;
    for (int i = 0; i < rack->count; i++) {
        if (rack->slot[i].type == MOD_FILTER && fl == RACK_NONE) fl = i;
        if (rack->slot[i].type == MOD_LFO && lf == RACK_NONE) lf = i;
    }
    if (fl != RACK_NONE) {
        synth_ui_macro_add(ui, 0, &(macro_t){MACRO_MODULE, rack->slot[fl].id, MP_FL_CUT});
        synth_ui_macro_add(ui, 1, &(macro_t){MACRO_MODULE, rack->slot[fl].id, MP_FL_RES});
    }
    if (lf != RACK_NONE) synth_ui_macro_add(ui, 2, &(macro_t){MACRO_MODULE, rack->slot[lf].id, MP_LF_RATE});
    for (int i = 0; i < SYNTH_UI_KNOBS; i++) { ui->knob_key[i] = -1; ui->knob_pos[i] = -1; }
    for (int i = 0; i < SYNTH_UI_COL_KNOBS; i++) { ui->knob_catch_dir[i] = 127; ui->knob_val[i] = -1; ui->knob_cur[i] = -1; }   // force catch on first touch
}

/* ---------------- events ---------------- */

// Number of rows of what is on screen (row 0, the page / tab selector, not counted).
static int rows_on_screen(const synth_ui_t *ui, const rack_t *rack) {
    if (ui->in_rack) return tab_rows(tab_kind(rack, ui->menu_tab));
    page_t pg;
    get_page(ui, rack, ui->page, &pg);
    return pg.count;
}

bool synth_ui_handle(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, ui_event_t e) {
    if (e == UI_PLAY) { seq_set_running(seq, !seq->running); return false; }
    if (e == UI_ROW_TOP) { ui->row = 0; ui->latched = false; return false; }

    if (e >= UI_JUMP_1 && e <= UI_JUMP_8) {
        const int slot = e - UI_JUMP_1;
        const jump_slot_t *j = &ui->jump[slot];
        if (!j->valid) return false;
        if (ui->in_rack && !j->in_rack && ui->rack_dirty) {      // leaving the rack editor: same as closing it with MENU (the slots are resolved again)
            ui->rebuild = true; ui->rack_dirty = false; synth_ui_rebuild_pages(ui, rack);
        }
        if (!synth_ui_jump_ready(ui, rack, slot)) return false;  // cleared (its module was deleted) or not shown in this synth type: stay
        ui->latched = false;
        ui->in_rack = j->in_rack;
        if (j->in_rack) ui->menu_tab = tab_index_of(rack, (tab_t)j->tab);
        else            ui->page = j->at;
        const int rows = rows_on_screen(ui, rack);
        ui->row      = j->row > rows ? rows : j->row;
        return false;
    }

    // Every tab of the menu is a declarative screen (ui_screen.h) and gets the events as they are. The main view (pages) is not converted yet:
    // it understands only the old events, so the joystick moves the row / changes the value, encoder B changes the value, a push of the joystick activates.
    if (!ui->in_rack) {
        switch (e) {
            case UI_NAV_UP: e = UI_UP; break;
            case UI_NAV_DOWN: e = UI_DOWN; break;
            case UI_NAV_LEFT: e = UI_LEFT; break;
            case UI_NAV_RIGHT: e = UI_RIGHT; break;
            case UI_VALUE_INC: e = UI_RIGHT; break;
            case UI_VALUE_DEC: e = UI_LEFT; break;
            case UI_LATCH: e = UI_SELECT; break;
            default: break;
        }
        if (e == UI_SELECT) { if (ui->row == 0) return false; e = UI_RIGHT; }   // push-to-activate: Insert, Delete, Run... act like Right
    }
    if (e == UI_PAGE_PREV || e == UI_PAGE_NEXT) {         // change page (or menu tab) from any row, keeping the row when the new one has it
        const int saved = ui->row;
        ui->row = 0;
        synth_ui_handle(ui, params, seq, rack, e == UI_PAGE_NEXT ? UI_RIGHT : UI_LEFT);
        const int rows = rows_on_screen(ui, rack);
        ui->row = saved > rows ? rows : saved;
        ui->latched = false;
        return false;
    }

    // MENU toggles the rack editor. Closing it with edits asks the app to rebuild the synth
    // and regenerates the pages from the new rack.
    if (e == UI_MENU) {
        ui->latched = false;
        if (!ui->in_rack) { ui->in_rack = true; ui->row = 1; if (ui->menu_tab >= tab_count(rack)) ui->menu_tab = 0; }
        else {
            ui->in_rack = false; ui->row = 0;
            if (ui->rack_dirty) { ui->rebuild = true; ui->rack_dirty = false; synth_ui_rebuild_pages(ui, rack); }
        }
        return false;
    }

    if (ui->in_rack) {
        const ui_ctx_t ctx = {ui, rack, 0};
        return screen_event(screen_for_tab(tab_kind(rack, ui->menu_tab)), &ctx, e);
    }

    page_t pg;
    get_page(ui, rack, ui->page, &pg);
    int n = pg.count;
    switch (e) {
        case UI_DOWN: ui->row = (ui->row + 1) % (n + 1); break;
        case UI_UP:   ui->row = (ui->row + n) % (n + 1); break;
        case UI_LEFT:
        case UI_RIGHT: {
            int dir = e == UI_RIGHT ? 1 : -1;
            if (ui->row == 0) ui->page = (ui->page + dir + ui->page_count) % ui->page_count;
            else return page_row_step(ui, params, seq, rack, &pg, ui->row, dir);
            break;
        }
        default: break;
    }
    return false;
}
