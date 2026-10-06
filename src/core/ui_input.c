// What events do: editing a row, the menu tabs, knobs and macros, and synth_ui_handle() (the entry point for UI events).
#include "core/ui_screen.h"
#include "core/synth_config.h"
#include "hal/hal_input.h"
#include <stdio.h>

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
    } else if (pg->graph == GRAPH_FM) {                  // FM page: patch (reloads the voices) and volume
        cfg_effect_t fx = synth_config_adjust(&rack->cfg, (cfg_param_id_t)pg->params[row - 1], dir);
        if (fx == CFG_REBUILD) ui->rebuild = true;
        return fx == CFG_LIVE;
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

// A knob sets a parameter by position. Parameters only know "one step up / down", so the range is measured by walking to both
// ends (silent: the audio only sees the final value) and then walking back to the step that matches the knob position.
typedef bool (*step_fn)(void *ctx, int dir);
#define KNOB_MAX_STEPS 512

static void knob_set(step_fn f, void *ctx, int value) {
    int total = 0;
    for (int i = 0; i < KNOB_MAX_STEPS && f(ctx, -1); i++) {}
    while (total < KNOB_MAX_STEPS && f(ctx, +1)) total++;
    if (total == 0) return;
    int idx = (int)((long)value * (total + 1) / (INPUT_VALUE_MAX + 1));
    if (idx > total) idx = total;
    for (int i = total; i > idx; i--) f(ctx, -1);
}

// Measures a parameter without changing it: returns its current step index (0 = min) and puts the number of steps from min to max in *total.
// It walks to the bottom, up to the top, and back down to where it was.
static int knob_measure(step_fn f, void *ctx, int *total) {
    int down = 0, up = 0;
    while (down < KNOB_MAX_STEPS && f(ctx, -1)) down++;
    while (up < KNOB_MAX_STEPS && f(ctx, +1)) up++;
    for (int i = up; i > down; i--) f(ctx, -1);
    *total = up;
    return down;
}

// Knob bookkeeping. `key` identifies what the knob drives.
// Returns true when the event should be applied. For col knobs (knob < SYNTH_UI_COL_KNOBS): catch mode — the knob is
// ignored until it crosses the current parameter value; once caught it applies normally. knob_catch_dir is 0 when caught.
static bool knob_new_position(synth_ui_t *ui, int knob, int key, int value) {
    const int pos = value >> 3;                         // 128 positions are enough to tell steps apart
    const bool key_changed = ui->knob_key[knob] != key;
    if (!key_changed && ui->knob_pos[knob] == pos) return false;
    ui->knob_key[knob] = key; ui->knob_pos[knob] = pos;
    if (knob >= SYNTH_UI_COL_KNOBS) return true;       // macro / volume knobs: no catch
    if (key_changed) ui->knob_catch_dir[knob] = 127;   // new parameter: force re-catch (sentinel, resolved on first step_fn call)
    return true;
}

// For col knobs: check catch and update knob_catch_dir. Returns true if the knob is caught and knob_set should run.
// param_pos_out receives the current step index so knob_set can skip the re-walk when already caught.
static bool knob_check_catch(synth_ui_t *ui, const rack_t *rack, int knob, int value, step_fn f, void *ctx) {
    if (rack->cfg.knob_mode) { ui->knob_catch_dir[knob] = 0; return true; }   // Direct mode: no catch, no arrow
    if (ui->knob_catch_dir[knob] == 0) return true;    // already caught
    int total;
    const int cur = knob_measure(f, ctx, &total);
    int knob_idx = (int)((long)value * (total + 1) / (INPUT_VALUE_MAX + 1));
    if (knob_idx > total) knob_idx = total;
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

// Rows a knob may drive: plain values. Not the ones that select or cycle (targets, motion / sequencer step editors, FM patch, sample file).
static bool row_is_knobbable(const rack_t *rack, const page_t *pg, int row) {
    if (pg->graph == GRAPH_SEQ || pg->graph == GRAPH_FM || pg->graph == GRAPH_MS_STEPS || pg->graph == GRAPH_MS_LANE) return false;
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
    if (!knob_check_catch(ui, rack, row - 1, value, row_step, &c)) return false;
    knob_set(row_step, &c, value);
    ui->knob_cur[row - 1] = -2;
    return true;
}

static bool volume_step(void *c, int dir) { return synth_config_adjust(&((rack_t *)c)->cfg, CFGP_VOLUME, dir) != CFG_UNCHANGED; }

bool synth_ui_set_volume(synth_ui_t *ui, rack_t *rack, int value) {
    if (!knob_new_position(ui, SYNTH_UI_KNOB_VOLUME, 0, value)) return false;
    knob_set(volume_step, rack, value);
    return true;
}

/* Macros: the three right-hand knobs each drive one parameter chosen by the user. Shift + knob assigns the parameter under the
 * cursor ("learn"). A module is referred to by its id, so a macro survives inserting modules and is cleared when its module goes. */

static void macro_clear(macro_t *m) { m->kind = MACRO_NONE; m->id = 0; m->prm = 0; }

static int macro_slot(const synth_ui_t *ui, const rack_t *rack, int k) {
    return ui->macro[k].kind == MACRO_MODULE ? rack_find(rack, ui->macro[k].id) : RACK_NONE;
}

typedef struct { synth_params_t *params; seq_t *seq; rack_t *rack; macro_t m; int slot; } macro_ctx_t;
static bool macro_step(void *c, int dir) {
    macro_ctx_t *x = c;
    switch (x->m.kind) {
        case MACRO_MODULE:
            if (x->m.prm == rack_depth_index((module_type_t)x->rack->slot[x->slot].type)) return rack_depth_adjust(x->rack, x->slot, dir);
            return rack_mparam_adjust(&x->rack->slot[x->slot], x->m.prm, dir);
        case MACRO_GLOBAL: return param_adjust(x->params, (param_id_t)x->m.prm, dir) != 0;
        case MACRO_SEQ:    return seq_param_adjust(x->seq, (seq_param_id_t)x->m.prm, dir);
        default:           return false;
    }
}

bool synth_ui_macro(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int k, int value) {
    if (k < 0 || k >= SYNTH_UI_MACROS || ui->macro[k].kind == MACRO_NONE) return false;
    int slot = RACK_NONE;
    if (ui->macro[k].kind == MACRO_MODULE && (slot = macro_slot(ui, rack, k)) == RACK_NONE) { macro_clear(&ui->macro[k]); return false; }
    if (!knob_new_position(ui, SYNTH_UI_KNOB_MACRO + k, 0, value)) return false;
    macro_ctx_t c = {params, seq, rack, ui->macro[k], slot};
    knob_set(macro_step, &c, value);
    return true;
}

bool synth_ui_macro_learn(synth_ui_t *ui, const rack_t *rack, int k) {
    if (k < 0 || k >= SYNTH_UI_MACROS || ui->in_rack) return false;
    page_t pg;
    get_page(ui, rack, ui->page, &pg);
    const int row = ui->row;
    if (row < 1 || row > pg.count || !row_is_knobbable(rack, &pg, row)) return false;
    macro_t m;
    macro_clear(&m);
    if (pg.slot != GLOBAL_PAGE) {
        m.kind = MACRO_MODULE; m.id = rack->slot[pg.slot].id;
        m.prm = (uint8_t)(pg.graph == GRAPH_EG ? 3 * ui->eg_pt + (row - 2) : pg.params[row - 1]);
    } else if (pg.graph == GRAPH_SEQ_CFG) { m.kind = MACRO_SEQ; m.prm = (uint8_t)pg.params[row - 1]; }
    else { m.kind = MACRO_GLOBAL; m.prm = (uint8_t)pg.params[row - 1]; }
    ui->macro[k] = m;
    ui->knob_key[SYNTH_UI_KNOB_MACRO + k] = -1;         // the next movement applies at once
    return true;
}

void synth_ui_macro_describe(const synth_ui_t *ui, const rack_t *rack, int k, char *out, int n) {
    const macro_t *m = &ui->macro[k];
    const int slot = macro_slot(ui, rack, k);
    if (m->kind == MACRO_MODULE && slot != RACK_NONE) {
        char nm[8];
        rack_slot_name(rack, slot, nm, sizeof nm);
        snprintf(out, (size_t)n, "R%d > %s %s", k + 1, nm, rack_mparam_label((module_type_t)rack->slot[slot].type, m->prm));
    } else if (m->kind == MACRO_GLOBAL) snprintf(out, (size_t)n, "R%d > %s", k + 1, param_label((param_id_t)m->prm));
    else if (m->kind == MACRO_SEQ)      snprintf(out, (size_t)n, "R%d > %s", k + 1, seq_param_label((seq_param_id_t)m->prm));
    else                                snprintf(out, (size_t)n, "R%d (unassigned)", k + 1);
}

typedef struct { rack_t *rack; cfg_param_id_t id; } cfg_ctx_t;
static bool cfg_step(void *c, int dir) { cfg_ctx_t *x = c; return synth_config_adjust(&x->rack->cfg, x->id, dir) != CFG_UNCHANGED; }

bool synth_ui_knob_row_shift(synth_ui_t *ui, rack_t *rack, int row, int value) {
    if (row < 1 || row > SYNTH_UI_COL_KNOBS) return false;
    ui->knob_val[row - 1] = value;
    const macro_t *m = &ui->knob_shift[row - 1];
    if (m->kind != MACRO_GLOBAL) return false;           // only global (cfg) params wired for now
    const int knob = row - 1;
    const int key = 0x200 + m->prm;                     // distinct key from page-knob keys
    if (!knob_new_position(ui, knob, key, value)) return false;
    cfg_ctx_t c = {rack, (cfg_param_id_t)m->prm};
    if (!knob_check_catch(ui, rack, knob, value, cfg_step, &c)) return false;
    knob_set(cfg_step, &c, value);
    ui->knob_cur[knob] = -2;
    return true;
}

bool synth_ui_knob_shift_describe(const synth_ui_t *ui, const rack_t *rack, int row, char *name, int nn, char *value, int nv, int *arrow) {
    if (row < 1 || row > SYNTH_UI_COL_KNOBS || ui->knob_shift[row - 1].kind != MACRO_GLOBAL) return false;
    const cfg_param_id_t id = (cfg_param_id_t)ui->knob_shift[row - 1].prm;
    snprintf(name, (size_t)nn, "%s", synth_config_label(id));
    synth_config_format(&rack->cfg, id, value, (size_t)nv);
    const int cd = ui->knob_catch_dir[row - 1];
    *arrow = cd == 1 || cd == -1 ? cd : 0;              // 127: not measured yet
    return true;
}

void synth_ui_catch_refresh(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, bool shift) {
    page_t pg;
    if (!ui->in_rack) get_page(ui, rack, ui->page, &pg);
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
        int8_t dir = 0;
        int key = -1;
        step_fn f = NULL;
        row_ctx_t rc;
        cfg_ctx_t cc;
        void *ctx = NULL;
        if (!ui->in_rack && ui->knob_val[k] >= 0 && !rack->cfg.knob_mode) {
            if (shift) {
                const macro_t *m = &ui->knob_shift[k];
                if (m->kind == MACRO_GLOBAL) { cc = (cfg_ctx_t){rack, (cfg_param_id_t)m->prm}; f = cfg_step; ctx = &cc; key = 0x200 + m->prm; }
            } else if (k + 1 <= pg.count && row_is_knobbable(rack, &pg, k + 1)) {
                rc = (row_ctx_t){ui, params, seq, rack, &pg, k + 1}; f = row_step; ctx = &rc; key = ui->page * 8 + k + 1;
            }
        }
        if (f) {
            int total;
            const int cur = knob_measure(f, ctx, &total);
            int idx = (int)((long)ui->knob_val[k] * (total + 1) / (INPUT_VALUE_MAX + 1));
            if (idx > total) idx = total;
            // A knob that set the value itself stays caught until something else changes the parameter (walking can measure it one step off).
            // Only for the same target: Shift swaps the target, and a knob that set the page value has not caught the Shift target.
            const bool same = ui->knob_key[k] == key;
            const bool kept = same && (ui->knob_cur[k] == -2 || (ui->knob_catch_dir[k] == 0 && ui->knob_cur[k] == cur));
            dir = (int8_t)(kept || idx == cur ? 0 : idx > cur ? -1 : 1);
            ui->knob_cur[k] = cur;
            ui->knob_key[k] = key;
        } else {
            ui->knob_cur[k] = -1;
        }
        ui->knob_catch_dir[k] = dir;
    }
}

// The macros start on the first filter's cutoff and resonance and the first LFO's rate, when the rack has them.
// Shift-knob defaults: knob 0 = speaker level, knob 1 = master volume, knobs 2/3 unassigned.
void macros_default(synth_ui_t *ui, const rack_t *rack) {
    for (int k = 0; k < SYNTH_UI_MACROS; k++) macro_clear(&ui->macro[k]);
    int fl = RACK_NONE, lf = RACK_NONE;
    for (int i = 0; i < rack->count; i++) {
        if (rack->slot[i].type == MOD_FILTER && fl == RACK_NONE) fl = i;
        if (rack->slot[i].type == MOD_LFO && lf == RACK_NONE) lf = i;
    }
    if (fl != RACK_NONE) {
        ui->macro[0] = (macro_t){MACRO_MODULE, rack->slot[fl].id, MP_FL_CUT};
        ui->macro[1] = (macro_t){MACRO_MODULE, rack->slot[fl].id, MP_FL_RES};
    }
    if (lf != RACK_NONE) ui->macro[2] = (macro_t){MACRO_MODULE, rack->slot[lf].id, MP_LF_RATE};
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) macro_clear(&ui->knob_shift[k]);
    ui->knob_shift[0] = (macro_t){MACRO_GLOBAL, 0, CFGP_SPEAKER};
    ui->knob_shift[1] = (macro_t){MACRO_GLOBAL, 0, CFGP_VOLUME};
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
