#include "core/modifiers.h"
#include "core/keymap.h"
#include "core/dx7.h"
#include "core/synth_config.h"
#include "core/curves.h"
#include "hal/hal_input.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static mod_entry_t g_e[MODL_COUNT][CTL_COUNT];
static unsigned    g_rev;

/* ---------------- the controls ---------------- */

// Every control that is not a matrix key, in the order the tab lists them, with its short name (the file word is the name in lower
// case without spaces). The keys follow: "F1".."F8" for the function row, "A1".."D8" for the note rows from the top (as in the KEYS tab).
static const struct { control_id_t ctl; const char *name; } others[] = {
    {CTL_COL_KNOB_0, "K1"}, {CTL_COL_KNOB_1, "K2"}, {CTL_COL_KNOB_2, "K3"}, {CTL_COL_KNOB_3, "K4"},
    {CTL_VOLUME, "Vol"}, {CTL_ENC_A, "EncA"}, {CTL_ENC_A_SW, "EncA sw"}, {CTL_ENC_B, "EncB"}, {CTL_ENC_B_SW, "EncB sw"},
    {CTL_KNOB_R1, "R1"}, {CTL_KNOB_R2, "R2"}, {CTL_KNOB_R3, "R3"},
    {CTL_JOY_UP, "Joy U"}, {CTL_JOY_DOWN, "Joy D"}, {CTL_JOY_LEFT, "Joy L"}, {CTL_JOY_RIGHT, "Joy R"}, {CTL_JOY_SW, "Joy sw"},
    {CTL_PLAY, "Play"}, {CTL_BTN_1, "B1"}, {CTL_BTN_2, "B2"}, {CTL_BTN_3, "B3"},
};
#define N_OTHERS ((int)(sizeof others / sizeof others[0]))

static bool is_key(control_id_t c) { return c >= CTL_KEY_FIRST && c <= CTL_KEY_LAST; }

static bool present(control_id_t c) {
    if (is_key(c)) return input_key_present((c - CTL_KEY_FIRST) / KEY_COLS, (c - CTL_KEY_FIRST) % KEY_COLS);
    return input_control_present(c);
}

// All candidates, present or not (the file may name a control another board has: its entry is kept).
static int          cand_count(void) { return N_OTHERS + KEY_COUNT; }
static control_id_t cand_at(int i)   { return i < N_OTHERS ? others[i].ctl : (control_id_t)(CTL_KEY_FIRST + i - N_OTHERS); }

int modifiers_ctl_count(void) {
    int n = 0;
    for (int i = 0; i < cand_count(); i++) if (present(cand_at(i))) n++;
    return n;
}

control_id_t modifiers_ctl_at(int i) {
    for (int k = 0; k < cand_count(); k++) if (present(cand_at(k)) && i-- == 0) return cand_at(k);
    return CTL_NONE;
}

int modifiers_ctl_index(control_id_t c) {
    int n = 0;
    for (int k = 0; k < cand_count(); k++) {
        if (!present(cand_at(k))) continue;
        if (cand_at(k) == c) return n;
        n++;
    }
    return -1;
}

bool modifiers_ctl_is_knob(control_id_t c) {
    return c == CTL_VOLUME || c == CTL_ENC_A || c == CTL_ENC_B || (c >= CTL_COL_KNOB_0 && c <= CTL_COL_KNOB_3) || (c >= CTL_KNOB_R1 && c <= CTL_KNOB_R3);
}

void modifiers_ctl_name(control_id_t c, char *out, int n) {
    if (is_key(c)) {
        const int k = c - CTL_KEY_FIRST, r = k / KEY_COLS, col = k % KEY_COLS;
        if (r == 0) snprintf(out, (size_t)n, "F%d", col + 1);
        else        snprintf(out, (size_t)n, "%c%d", 'A' + r - 1, col + 1);
        return;
    }
    for (int i = 0; i < N_OTHERS; i++) if (others[i].ctl == c) { snprintf(out, (size_t)n, "%s", others[i].name); return; }
    snprintf(out, (size_t)n, "?");
}

static void ctl_token(control_id_t c, char *out, int n) {
    char name[16];
    modifiers_ctl_name(c, name, sizeof name);
    int j = 0;
    for (const char *s = name; *s && j < n - 1; s++) if (*s != ' ') out[j++] = (char)tolower((unsigned char)*s);
    out[j] = 0;
}

/* ---------------- the functions ---------------- */

// Knobs and encoders: steps. On an absolute knob a step is INPUT_VALUE_MAX / 32 of travel (the app turns positions into steps).
static const struct { uint8_t act; int8_t arg; const char *name; const char *tok; } knob_fns[] = {
    {ACT_ROW_MOVE,      1, "Rows",     "rows"},
    {ACT_PAGE_MOVE,     1, "Pages",    "pages"},
    {ACT_VALUE_ADJUST,  1, "Value",    "value"},
    {ACT_VALUE_ADJUST,  4, "Value x4", "value4"},
    {ACT_OCTAVE,        1, "Octave",   "octave"},
    {ACT_VOLUME_STEP,   1, "Volume",   "volume"},
    {ACT_MACRO,         0, "Macro 1",  "macro1"},               // plays macro 1..8 (what it drives: learned with "Learn M1..8")
    {ACT_MACRO,         1, "Macro 2",  "macro2"},
    {ACT_MACRO,         2, "Macro 3",  "macro3"},
    {ACT_MACRO,         3, "Macro 4",  "macro4"},
    {ACT_MACRO,         4, "Macro 5",  "macro5"},
    {ACT_MACRO,         5, "Macro 6",  "macro6"},
    {ACT_MACRO,         6, "Macro 7",  "macro7"},
    {ACT_MACRO,         7, "Macro 8",  "macro8"},
    {ACT_MACRO_LEARN,   0, "Learn M1", "learnm1"},              // adds the parameter under the cursor to macro 1..8
    {ACT_MACRO_LEARN,   1, "Learn M2", "learnm2"},
    {ACT_MACRO_LEARN,   2, "Learn M3", "learnm3"},
    {ACT_MACRO_LEARN,   3, "Learn M4", "learnm4"},
    {ACT_MACRO_LEARN,   4, "Learn M5", "learnm5"},
    {ACT_MACRO_LEARN,   5, "Learn M6", "learnm6"},
    {ACT_MACRO_LEARN,   6, "Learn M7", "learnm7"},
    {ACT_MACRO_LEARN,   7, "Learn M8", "learnm8"},
};
#define N_KNOB_FNS ((int)(sizeof knob_fns / sizeof knob_fns[0]))

// The key functions a layer entry may take: all but the modifiers themselves (a modifier inside a layer would never be released).
static bool key_fn_ok(key_fn_t f) { return f.act != ACT_SHIFT && f.act != ACT_MOD; }

// Knobs can also drive these GENERAL settings straight from the list (the ones that change live; any other parameter needs learn).
static const uint8_t knob_cfgs[] = {CFGP_VOLUME, CFGP_SPEAKER, CFGP_OUTPUT, CFGP_GLIDE, CFGP_LEGATO, CFGP_PATCH, CFGP_KNOB_MODE};
#define N_KNOB_CFGS ((int)(sizeof knob_cfgs / sizeof knob_cfgs[0]))

int modifiers_fn_count(control_id_t c) {
    if (modifiers_ctl_is_knob(c)) return 2 + N_KNOB_CFGS + N_KNOB_FNS;
    int n = 1;                                                   // Default, then the key functions (None is the first of them)
    for (int i = 0; i < keymap_fn_count(); i++) if (key_fn_ok(keymap_fn_at(i))) n++;
    return n;
}

mod_entry_t modifiers_fn_at(control_id_t c, int i) {
    mod_entry_t e = {ME_DEFAULT, ACT_NONE, 0, {MACRO_NONE, 0, 0}};
    if (i <= 0) return e;
    e.kind = ME_ACTION;
    if (modifiers_ctl_is_knob(c)) {                              // i == 1: None, then the settings, then the steps
        const int k = i - 2;
        if (k >= 0 && k < N_KNOB_CFGS) return (mod_entry_t){ME_PARAM, ACT_NONE, 0, {MACRO_CFG, 0, knob_cfgs[k]}};
        if (k >= N_KNOB_CFGS && k - N_KNOB_CFGS < N_KNOB_FNS) { e.act = knob_fns[k - N_KNOB_CFGS].act; e.arg = knob_fns[k - N_KNOB_CFGS].arg; }
        return e;
    }
    for (int k = 0; k < keymap_fn_count(); k++) {
        const key_fn_t f = keymap_fn_at(k);
        if (key_fn_ok(f) && --i == 0) { e.act = f.act; e.arg = f.arg; return e; }
    }
    return e;
}

int modifiers_fn_index(control_id_t c, mod_entry_t e) {
    for (int i = 0; i < modifiers_fn_count(c); i++) {
        const mod_entry_t f = modifiers_fn_at(c, i);
        if (f.kind != e.kind) continue;
        if (e.kind == ME_DEFAULT) return i;
        if (e.kind == ME_ACTION && f.act == e.act && f.arg == e.arg) return i;
        if (e.kind == ME_PARAM && f.m.kind == e.m.kind && f.m.prm == e.m.prm) return i;
    }
    return e.kind == ME_PARAM ? -1 : 0;                           // a learned parameter that is not in the list
}

void modifiers_entry_name(mod_entry_t e, const rack_t *rack, char *out, int n) {
    if (e.kind == ME_DEFAULT) { snprintf(out, (size_t)n, "Default"); return; }
    if (e.kind == ME_PARAM) {
        char name[24];
        if (!synth_ui_target_describe(NULL, NULL, rack, &e.m, name, sizeof name, NULL, 0)) snprintf(name, sizeof name, "(gone)");
        snprintf(out, (size_t)n, "%s", name);
        return;
    }
    for (int i = 0; i < N_KNOB_FNS; i++) if (knob_fns[i].act == e.act && knob_fns[i].arg == e.arg) { snprintf(out, (size_t)n, "%s", knob_fns[i].name); return; }
    keymap_fn_name((key_fn_t){e.act, e.arg}, out, n);
}

/* ---------------- state ---------------- */

static void clear_all(void) {
    for (int l = 0; l < MODL_COUNT; l++)
        for (int c = 0; c < CTL_COUNT; c++) g_e[l][c] = (mod_entry_t){ME_DEFAULT, ACT_NONE, 0, {MACRO_NONE, 0, 0}};
}

static mod_entry_t g_def[MODL_COUNT][CTL_COUNT];        // the defaults: the file only lists what differs from them

static mod_entry_t act(uint8_t a, int8_t arg) { return (mod_entry_t){ME_ACTION, a, arg, {MACRO_NONE, 0, 0}}; }

// The Shift layer starts with what Shift always did (it used to be fixed in bindings.c); the Mod layer starts empty. A board without the
// right-hand macro knobs (the prototype) gets macros 1 and 2 on Shift + column knobs 3 and 4 instead.
static void set_defaults(void) {
    clear_all();
    g_e[MODL_SHIFT][CTL_COL_KNOB_0] = (mod_entry_t){ME_PARAM, ACT_NONE, 0, {MACRO_CFG, 0, CFGP_SPEAKER}};
    g_e[MODL_SHIFT][CTL_COL_KNOB_1] = (mod_entry_t){ME_PARAM, ACT_NONE, 0, {MACRO_CFG, 0, CFGP_VOLUME}};
    g_e[MODL_SHIFT][CTL_ENC_A]      = act(ACT_PAGE_MOVE, 1);
    g_e[MODL_SHIFT][CTL_ENC_B]      = act(ACT_VALUE_ADJUST, 4);
    g_e[MODL_SHIFT][CTL_KNOB_R1]    = act(ACT_MACRO_LEARN, 0);
    g_e[MODL_SHIFT][CTL_KNOB_R2]    = act(ACT_MACRO_LEARN, 1);
    g_e[MODL_SHIFT][CTL_KNOB_R3]    = act(ACT_MACRO_LEARN, 2);
    g_e[MODL_SHIFT][CTL_JOY_UP]     = act(ACT_OCTAVE, 1);
    g_e[MODL_SHIFT][CTL_JOY_DOWN]   = act(ACT_OCTAVE, -1);
    g_e[MODL_SHIFT][CTL_JOY_LEFT]   = act(ACT_PAGE_MOVE, -1);
    g_e[MODL_SHIFT][CTL_JOY_RIGHT]  = act(ACT_PAGE_MOVE, 1);
    if (!input_control_present(CTL_KNOB_R1)) {
        g_e[MODL_SHIFT][CTL_COL_KNOB_2] = act(ACT_MACRO, 0);
        g_e[MODL_SHIFT][CTL_COL_KNOB_3] = act(ACT_MACRO, 1);
    }
}

void modifiers_init(void) {
    set_defaults();
    memcpy(g_def, g_e, sizeof g_def);
    g_rev++;
}

void modifiers_reset(void) { modifiers_init(); }

mod_entry_t modifiers_get(int layer, control_id_t c) {
    if (layer < 0 || layer >= MODL_COUNT || c <= CTL_NONE || c >= CTL_COUNT) return (mod_entry_t){ME_DEFAULT, ACT_NONE, 0, {MACRO_NONE, 0, 0}};
    return g_e[layer][c];
}

void modifiers_set(int layer, control_id_t c, mod_entry_t e) {
    if (layer < 0 || layer >= MODL_COUNT || c <= CTL_NONE || c >= CTL_COUNT) return;
    g_e[layer][c] = e;
    g_rev++;
}

unsigned modifiers_rev(void) { return g_rev; }

void modifiers_prune(const rack_t *rack) {
    for (int l = 0; l < MODL_COUNT; l++)
        for (int c = 0; c < CTL_COUNT; c++) {
            mod_entry_t *e = &g_e[l][c];
            if (e->kind == ME_PARAM && e->m.kind == MACRO_MODULE && rack_find(rack, e->m.id) == RACK_NONE) {
                *e = (mod_entry_t){ME_DEFAULT, ACT_NONE, 0, {MACRO_NONE, 0, 0}};
                g_rev++;
            }
        }
}

const char *modifiers_layer_name(int layer) { return layer == MODL_MOD ? "Mod" : "Shift"; }

/* ---------------- ui.cfg ----------------
 *   modifiers                     this file holds the layers (every entry not listed keeps its default, modifiers_init)
 *   shift enca act default        Default (written when the default entry is something else)
 *   shift k1 param cfg spk        a Param: cfg <GENERAL setting>, mod <module id> <param index>, glob <param id>, seq <param id>
 *   mod a3 act page+              an Action: a key function word (keys.cfg's words), or a knob word (rows pages value value4 octave volume)
 *   shift joyu act none           None (the control does nothing with this modifier)                                                   */

static void cfg_token(int id, char *out, int n) {
    snprintf(out, (size_t)n, "%s", synth_config_label((cfg_param_id_t)id));
    for (char *c = out; *c; c++) *c = (char)tolower((unsigned char)*c);
}

static bool same(const mod_entry_t *a, const mod_entry_t *b) {
    if (a->kind != b->kind) return false;
    if (a->kind == ME_ACTION) return a->act == b->act && a->arg == b->arg;
    if (a->kind == ME_PARAM) return a->m.kind == b->m.kind && a->m.id == b->m.id && a->m.prm == b->m.prm &&
                                    a->min == b->min && a->max_off == b->max_off && a->curve == b->curve;
    return true;
}

int mapping_to_text(const mapping_t *m, char *out, int n) {
    int k;
    switch (m->t.kind) {
        case MACRO_MODULE: k = snprintf(out, (size_t)n, "mod %d %d", m->t.id, m->t.prm); break;
        case MACRO_GLOBAL: k = snprintf(out, (size_t)n, "glob %d", m->t.prm); break;
        case MACRO_SEQ:    k = snprintf(out, (size_t)n, "seq %d", m->t.prm); break;
        case MACRO_FM:     k = snprintf(out, (size_t)n, "fm %d %d", m->t.id, m->t.prm); break;
        case MACRO_CFG: { char t[16]; cfg_token(m->t.prm, t, sizeof t); k = snprintf(out, (size_t)n, "cfg %s", t); break; }
        default: if (n > 0) out[0] = 0; return 0;
    }
    if (k < n && (m->min || m->max_off)) k += snprintf(out + k, (size_t)(n - k), " range %d %d", m->min, mapping_max(m));
    if (k < n && m->curve) { char c[16]; snprintf(c, sizeof c, "%s", curve_name(m->curve)); for (char *p = c; *p; p++) *p = (char)tolower((unsigned char)*p);
                             k += snprintf(out + k, (size_t)(n - k), " curve %s", c); }
    return k < n ? k : n - 1;
}

bool mapping_parse(const char *line, mapping_t *out) {
    char s[96], kind[8], tok[16];                   // this line only: the searches below must not run into the next lines of the file
    int a = 0, b = 0, n = 0;
    while (line[n] && line[n] != '\n' && line[n] != '\r' && n < (int)sizeof s - 1) { s[n] = line[n]; n++; }
    s[n] = 0;
    mapping_t m = {{MACRO_NONE, 0, 0}, 0, 0, 0};
    if (sscanf(s, "%7s", kind) != 1) return false;
    if (!strcmp(kind, "mod") && sscanf(s, "%*s %d %d", &a, &b) == 2 && a > 0 && a < 256 && b >= 0 && b < 256) m.t = (macro_t){MACRO_MODULE, (uint8_t)a, (uint8_t)b};
    else if (!strcmp(kind, "glob") && sscanf(s, "%*s %d", &a) == 1 && a >= 0 && a < P_COUNT) m.t = (macro_t){MACRO_GLOBAL, 0, (uint8_t)a};
    else if (!strcmp(kind, "seq") && sscanf(s, "%*s %d", &a) == 1 && a >= 0 && a < SQP_COUNT) m.t = (macro_t){MACRO_SEQ, 0, (uint8_t)a};
    else if (!strcmp(kind, "fm") && sscanf(s, "%*s %d %d", &a, &b) == 2 && dx7_value_valid(a, b)) m.t = (macro_t){MACRO_FM, (uint8_t)a, (uint8_t)b};
    else if (!strcmp(kind, "cfg") && sscanf(s, "%*s %15s", tok) == 1) {
        for (int id = 0; id < CFGP_COUNT; id++) { char t[16]; cfg_token(id, t, sizeof t); if (!strcmp(t, tok)) m.t = (macro_t){MACRO_CFG, 0, (uint8_t)id}; }
        if (m.t.kind == MACRO_NONE) return false;
    } else return false;
    const char *r = strstr(s, " range ");
    if (r && sscanf(r, " range %d %d", &a, &b) == 2 && a >= 0 && a <= 100 && b >= 0 && b <= 100) { m.min = (uint8_t)a; m.max_off = (uint8_t)(100 - b); }
    const char *c = strstr(s, " curve ");
    if (c && sscanf(c, " curve %15s", tok) == 1 && curve_by_name(tok) >= 0) m.curve = (uint8_t)curve_by_name(tok);
    *out = m;
    return true;
}

int modifiers_to_text(char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "modifiers\n");
    for (int l = 0; l < MODL_COUNT; l++)
        for (int i = 0; i < cand_count() && n < cap; i++) {
            const control_id_t c = cand_at(i);
            const mod_entry_t *e = &g_e[l][c];
            if (same(e, &g_def[l][c])) continue;
            char ct[16], what[64];
            ctl_token(c, ct, sizeof ct);
            if (e->kind == ME_DEFAULT) {
                snprintf(what, sizeof what, "act default");
            } else if (e->kind == ME_PARAM) {
                const mapping_t mp = modifiers_mapping(e);
                char t[56];
                if (!mapping_to_text(&mp, t, sizeof t)) continue;
                snprintf(what, sizeof what, "param %s", t);
            } else {
                const char *tok = NULL;
                for (int k = 0; k < N_KNOB_FNS; k++) if (modifiers_ctl_is_knob(c) && knob_fns[k].act == e->act && knob_fns[k].arg == e->arg) tok = knob_fns[k].tok;
                char kt[16];
                if (!tok) { keymap_fn_token((key_fn_t){e->act, e->arg}, kt, sizeof kt); tok = kt; }
                snprintf(what, sizeof what, "act %s", tok);
            }
            n += snprintf(buf + n, (size_t)(cap - n), "%s %s %s\n", l == MODL_MOD ? "mod" : "shift", ct, what);
        }
    return n < cap ? n : cap - 1;
}

void modifiers_from_text_begin(void) { set_defaults(); g_rev++; }

bool modifiers_from_line(const char *line) {
    char w1[16], w2[16], w3[16];
    int at = 0;
    if (sscanf(line, "%15s %15s %15s %n", w1, w2, w3, &at) != 3 || at == 0) return false;
    const int layer = !strcmp(w1, "shift") ? MODL_SHIFT : !strcmp(w1, "mod") ? MODL_MOD : -1;
    if (layer < 0) return false;
    control_id_t c = CTL_NONE;
    for (int i = 0; i < cand_count(); i++) {
        char t[16];
        ctl_token(cand_at(i), t, sizeof t);
        if (!strcmp(t, w2)) c = cand_at(i);
    }
    if (c == CTL_NONE) return false;
    const char *rest = line + at;
    mod_entry_t e = {ME_ACTION, ACT_NONE, 0, {MACRO_NONE, 0, 0}};
    if (!strcmp(w3, "act")) {
        char tok[16];
        key_fn_t f;
        if (sscanf(rest, "%15s", tok) != 1) return false;
        bool ok = false;
        if (!strcmp(tok, "default")) { e.kind = ME_DEFAULT; ok = true; }
        for (int k = 0; k < N_KNOB_FNS; k++) if (modifiers_ctl_is_knob(c) && !strcmp(tok, knob_fns[k].tok)) { e.act = knob_fns[k].act; e.arg = knob_fns[k].arg; ok = true; }
        if (!ok && keymap_fn_parse(rest, &f) && key_fn_ok(f)) { e.act = f.act; e.arg = f.arg; ok = true; }
        if (!ok) return false;
    } else if (!strcmp(w3, "param")) {
        mapping_t mp;
        if (!modifiers_ctl_is_knob(c) || !mapping_parse(rest, &mp)) return false;
        e = (mod_entry_t){ME_PARAM, ACT_NONE, 0, mp.t, mp.min, mp.max_off, mp.curve};
    } else return false;
    g_e[layer][c] = e;
    g_rev++;
    return true;
}
