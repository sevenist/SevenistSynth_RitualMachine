// The page tables: what each module / global page shows, the page list generated from the rack, and the menu tabs.
// (Types and shared declarations: ui_internal.h.)
#include "core/ui_screen.h"
#include "hal/hal_input.h"
#include <stdio.h>

/* ---- pages are generated from the rack: every module contributes its own pages, then the
 *      global pages follow (amp envelope, sequencer). ---- */

typedef struct { const char *fmt; graph_t graph; int count; int p[6]; bool needs_mod; } mpage_def_t;   // needs_mod: only while the module acts as a modulator

static const mpage_def_t oc_pages[] = {
    {"OSC %d",      GRAPH_WAVE,   4, {MP_OC_WAVE, MP_OC_PW, MP_OC_MORPH, MP_OC_LEVEL}},
    {"OSC %d TUNE", GRAPH_WAVE,   2, {MP_OC_COARSE, MP_OC_FINE}},
    {"OSC %d DEST", GRAPH_WAVE,   4, {PRM_TGT, PRM_TPRM, MP_OC_DEPTH, MP_OC_MUTE}},   // Tgt "--" = a plain oscillator
};
static const mpage_def_t fl_pages[] = {
    {"FILTER %d",   GRAPH_FILTER, 4, {MP_FL_TYPE, MP_FL_CUT, MP_FL_RES, MP_FL_ENVAMT}},
    {"FILT %d ENV", GRAPH_ENV,    4, {MP_FL_A, MP_FL_D, MP_FL_S, MP_FL_R}},
    {"FILT %d CRV", GRAPH_ENV,    3, {MP_FL_ACV, MP_FL_DCV, MP_FL_RCV}},
};
static const mpage_def_t sa_pages[] = {{"SAT %d", GRAPH_SAT, 3, {MP_SA_MODE, MP_SA_DRIVE, MP_SA_MIX}}};
static const mpage_def_t lf_pages[] = {
    {"LFO %d",      GRAPH_WAVE, 3, {MP_LF_SHAPE, MP_LF_RATE, MP_LF_DEPTH}},
    {"LFO %d DEST", GRAPH_WAVE, 3, {PRM_TGT, PRM_TPRM, MP_LF_DEPTH}},
};
static const mpage_def_t en_pages[] = {
    {"ENV %d",      GRAPH_ENV, 5, {MP_EN_A, MP_EN_D, MP_EN_S, MP_EN_R, MP_EN_DEPTH}},
    {"ENV %d CRV",  GRAPH_ENV, 5, {MP_EN_ACV, MP_EN_DCV, MP_EN_RCV, MP_EN_HOLD, MP_EN_START}},
    {"ENV %d DEST", GRAPH_ENV, 3, {PRM_TGT, PRM_TPRM, MP_EN_DEPTH}},
};
// multi-stage envelope: page 1 edits the selected point (rows Pt / Time / Lvl / Crv, drawn by hand), page 2 the rest
static const mpage_def_t eg_pages[] = {
    {"EG %d",     GRAPH_EG,     4, {0}},
    {"EG %d REL", GRAPH_EG_REL, 5, {MP_EG_SUS, MP_EG_REL, MP_EG_RCV, MP_EG_ONE, MP_EG_DEPTH}},
    {"EG %d DEST", GRAPH_EG_REL, 3, {PRM_TGT, PRM_TPRM, MP_EG_DEPTH}},
};
// motion sequencer: its rows are drawn and edited by hand (the data is not in slot.v[]): Lane / Step / Val / On, and Lane / Tgt / Mode / Pol / Dpth
static const mpage_def_t rs_pages[] = {{"RES %d", GRAPH_COMB, 5, {MP_RS_TUNE, MP_RS_FB, MP_RS_DAMP, MP_RS_INT, MP_RS_MIX}}};
static const mpage_def_t sm_pages[] = {
    {"SMP %d",       GRAPH_SAMPLE, 4, {MP_SM_FILE, MP_SM_LEVEL, MP_SM_COARSE, MP_SM_FINE}},
    {"SMP %d LOOP",  GRAPH_SAMPLE, 4, {MP_SM_LOOP, MP_SM_REV, MP_SM_START, MP_SM_TRACK}},
    {"SMP %d SLICE", GRAPH_SAMPLE, 2, {MP_SM_SLICE, MP_SM_SMODE}},
};
static const mpage_def_t ms_pages[] = {
    {"MOTION %d",  GRAPH_MS_STEPS, 4, {0}},
    {"MS %d LANE", GRAPH_MS_LANE,  5, {0}},
};

static const struct { const mpage_def_t *defs; int n; } mod_pages[MOD_TYPE_COUNT] = {
    [MOD_OSC]    = {oc_pages, 3},
    [MOD_FILTER] = {fl_pages, 3},
    [MOD_SAT]    = {sa_pages, 1},
    [MOD_LFO]    = {lf_pages, 2},
    [MOD_MSEQ]   = {ms_pages, 2},
    [MOD_ENV]    = {en_pages, 3},
    [MOD_EG]     = {eg_pages, 3},
    [MOD_COMB]   = {rs_pages, 1},
    [MOD_SAMPLER] = {sm_pages, 3},
};

typedef struct { const char *title; graph_t graph; int count; int params[4]; } gpage_def_t;
enum { GP_AMP_ENV, GP_SEQ, GP_SEQ_CFG, GP_FM, GP_AMP_CRV, GP_STR_OSC, GP_STR_TONE, GP_STR_LP, GP_STR_FILTER };
static const gpage_def_t global_pages[] = {
    [GP_AMP_ENV] = {"AMP ENV",   GRAPH_AMP_ENV, 4, {P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R}},
    [GP_SEQ]     = {"SEQUENCER", GRAPH_SEQ,     4, {0}},   // rows: Step, Note, Len, Run (see handle_seq)
    [GP_SEQ_CFG] = {"SEQ SETUP", GRAPH_SEQ_CFG, 4, {SQP_BPM, SQP_STEPS, SQP_TRANSPOSE, SQP_SWING}},
    [GP_AMP_CRV] = {"AMP CURVE", GRAPH_AMP_ENV, 4, {P_AMP_HOLD, P_AMP_ACV, P_AMP_DCV, P_AMP_RCV}},
    [GP_FM]      = {"FM SYNTH",  GRAPH_FM,      2, {CFGP_PATCH, CFGP_VOLUME}},       // shown instead of the module pages in FM mode
    [GP_STR_OSC]    = {"STRINGS",    GRAPH_STR_OSC,    4, {P_STR_WAVE, P_STR_OSC, P_STR_DETUNE, P_STR_MIX}},   // the Strings type (ADR-037)
    [GP_STR_TONE]   = {"STR TONE",   GRAPH_STR_OSC,    2, {P_STR_PW, P_STR_LEVEL}},
    [GP_STR_LP]     = {"VOICE LP",   GRAPH_STR_LP,     4, {P_STR_LP, P_STR_LPCUT, P_STR_LPENV, P_STR_LPKEY}},
    [GP_STR_FILTER] = {"STR FILTER", GRAPH_STR_FILTER, 3, {P_STR_FTYPE, P_STR_FCUT, P_STR_FRES}},
};

// Global pages shown after the module pages (modular synth) / the whole list (FM synth).
static const uint8_t modular_globals[] = {GP_AMP_ENV, GP_AMP_CRV, GP_SEQ, GP_SEQ_CFG};
static const uint8_t fm_globals[]      = {GP_FM, GP_SEQ, GP_SEQ_CFG};
static const uint8_t str_globals[]     = {GP_STR_OSC, GP_STR_TONE, GP_STR_LP, GP_STR_FILTER, GP_AMP_ENV, GP_SEQ, GP_SEQ_CFG};

void synth_ui_rebuild_pages(synth_ui_t *ui, const rack_t *rack) {
    int n = 0;
    const bool rack_type = synth_type_is_rack(rack->cfg.type);
    for (int i = 0; i < (rack_type ? rack->count : 0); i++) {
        int t = rack->slot[i].type;
        for (int d = 0; d < mod_pages[t].n && n < SYNTH_UI_MAX_PAGES; d++) {
            if (mod_pages[t].defs[d].needs_mod && !rack_slot_is_mod(rack, i)) continue;
            ui->pg_slot[n] = (uint8_t)i; ui->pg_def[n] = (uint8_t)d; n++;
        }
    }
    const uint8_t *gl = modular_globals;
    int n_gl = (int)sizeof modular_globals;
    if (synth_type_is_fm(rack->cfg.type)) { gl = fm_globals; n_gl = (int)sizeof fm_globals; }
    else if (synth_type_is_strings(rack->cfg.type)) { gl = str_globals; n_gl = (int)sizeof str_globals; }
    for (int d = 0; d < n_gl && n < SYNTH_UI_MAX_PAGES; d++) { ui->pg_slot[n] = GLOBAL_PAGE; ui->pg_def[n] = gl[d]; n++; }
    ui->page_count = n;
    if (ui->page >= n) ui->page = n - 1;
    if (ui->page < 0) ui->page = 0;
}

void get_page(const synth_ui_t *ui, const rack_t *rack, int idx, page_t *out) {
    int slot = ui->pg_slot[idx], def = ui->pg_def[idx];
    out->slot = slot; out->def = def;
    if (slot == GLOBAL_PAGE) {
        const gpage_def_t *g = &global_pages[def];
        snprintf(out->title, sizeof out->title, "%s", g->title);
        out->graph = g->graph; out->count = g->count;
        for (int i = 0; i < 4; i++) out->params[i] = g->params[i];
        for (int i = 4; i < 6; i++) out->params[i] = 0;
    } else {
        const mpage_def_t *m = &mod_pages[rack->slot[slot].type].defs[def];
        snprintf(out->title, sizeof out->title, m->fmt, rack_instance(rack, slot));
        out->graph = m->graph; out->count = m->count;
        for (int i = 0; i < 6; i++) out->params[i] = m->p[i];
    }
}

void synth_ui_init(synth_ui_t *ui, const rack_t *rack) {
    ui->page = 0; ui->row = 0; ui->cursor = 0; ui->rack_cur = 0; ui->rack_scroll = 0; ui->rack_type = MOD_OSC;
    ui->rack_dirty = false; ui->rebuild = false; ui->in_rack = false; ui->menu_tab = 0; ui->fm_op = 0; ui->fm_pt = 0;
    ui->run_anim = GUI_ANIM_INVALID; ui->ms_lane = 0; ui->ms_step = 0; ui->smp_cur = 0; ui->smp_tgt = 0; ui->key_cur = KEY_COLS; ui->eg_pt = 0; ui->fx_slot = 0;
    synth_ui_rebuild_pages(ui, rack);
    macros_default(ui, rack);
}

bool synth_ui_shows_seq(const synth_ui_t *ui) {
    if (ui->in_rack) return false;
    if (ui->pg_slot[ui->page] == GLOBAL_PAGE) return ui->pg_def[ui->page] == GP_SEQ;
    return false;       // (the motion page's playhead is drawn from the same ticks: see synth_ui_shows_playhead)
}

bool synth_ui_shows_playhead(const synth_ui_t *ui, const rack_t *rack) {
    if (synth_ui_shows_seq(ui)) return true;
    if (ui->in_rack || ui->pg_slot[ui->page] == GLOBAL_PAGE) return false;
    return mod_pages[rack->slot[ui->pg_slot[ui->page]].type].defs[ui->pg_def[ui->page]].graph == GRAPH_MS_STEPS;
}

/* ---------------- menu tabs ----------------
 * Modular synth: RACK, GENERAL, SAMPLES, FX RACK, KEYS.  FM synth: GENERAL, ALGORITHM, OPERATOR, ENVELOPE (the DX7 editor), FX RACK, KEYS.
 * Strings: GENERAL, FX RACK, KEYS (its sound is edited on the pages). */

int tab_count(const rack_t *r) {
    if (synth_type_is_fm(r->cfg.type)) return 6;
    return synth_type_is_strings(r->cfg.type) ? 3 : 5;
}

tab_t tab_kind(const rack_t *r, int idx) {
    static const tab_t modular[5] = {TAB_RACK, TAB_GENERAL, TAB_SAMPLES, TAB_FX, TAB_KEYS};
    static const tab_t fm[6] = {TAB_GENERAL, TAB_FM_ALGO, TAB_FM_OP, TAB_FM_ENV, TAB_FX, TAB_KEYS};
    static const tab_t strings[3] = {TAB_GENERAL, TAB_FX, TAB_KEYS};
    if (synth_type_is_fm(r->cfg.type)) return fm[idx % 6];
    return synth_type_is_strings(r->cfg.type) ? strings[idx % 3] : modular[idx % 5];
}

const char *tab_name(tab_t t) {
    static const char *const n[] = {"RACK", "GENERAL", "ALGORITHM", "OPERATOR", "ENVELOPE", "FX RACK", "SAMPLES", "KEYS"};
    return n[t];
}

bool synth_ui_on_keys_tab(const synth_ui_t *ui, const rack_t *rack) { return ui->in_rack && tab_kind(rack, ui->menu_tab) == TAB_KEYS; }

int tab_rows(tab_t t) { return screen_for_tab(t)->n; }      // every tab is a declarative screen: its rows are its elements

int tab_index_of(const rack_t *r, tab_t t) {
    for (int i = 0; i < tab_count(r); i++) if (tab_kind(r, i) == t) return i;
    return 0;
}
