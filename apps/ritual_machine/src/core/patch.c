// A patch as text (patch.h): the working state synth.cfg and the presets.
#include "core/patch.h"
#include "core/modifiers.h"
#include "core/audio.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define g_buf g_storage_buf                      // the shared settings buffer (hal_storage.h)

/* ---------------- writing ---------------- */

// Appends to buf at *n; once the text does not fit, *n stays at cap (patch_to_text then answers -1).
static void add(char *buf, int cap, int *n, const char *fmt, ...) {
    if (*n >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(buf + *n, (size_t)(cap - *n), fmt, ap);
    va_end(ap);
    *n = w < 0 || *n + w >= cap ? cap : *n + w;
}

// Numbers read back as they were: the short form (%g) when it does that, 9 digits when it does not (0.857142866 for 6 / 7).
static void add_f(char *buf, int cap, int *n, float v) {
    char t[24];
    snprintf(t, sizeof t, "%g", (double)v);
    if (strtof(t, NULL) != v) snprintf(t, sizeof t, "%.9g", (double)v);
    add(buf, cap, n, " %s", t);
}

static void add_line(char *buf, int cap, int *n, const char *word, const float *v, int count) {
    add(buf, cap, n, "%s", word);
    for (int i = 0; i < count; i++) add_f(buf, cap, n, v[i]);
    add(buf, cap, n, "\n");
}

static void add_env(char *buf, int cap, int *n, const char *word, const env_params_t *e) {
    const float v[8] = {e->attack_ms, e->decay_ms, e->sustain, e->release_ms, e->hold_ms, e->a_curve, e->d_curve, e->r_curve};
    add_line(buf, cap, n, word, v, 8);
}

// A sampler's file: its name in the library ("" when it has none, or the catalog no longer holds it).
static void sampler_name(const rack_slot_t *s, char *out, int n) {
    audio_sample_info_t in;
    const int index = (int)s->v[MP_SM_FILE] - 1;
    out[0] = 0;
    if (s->type == MOD_SAMPLER && index >= 0 && audio_sample_info(index, &in)) snprintf(out, (size_t)n, "%s", in.name);
}

int patch_to_text(const rack_t *rack, const synth_params_t *params, const synth_ui_t *ui, bool state, const char *preset,
                  bool edited, char *buf, int cap) {
    int n = 0;
    const synth_config_t *c = &rack->cfg;
    add(buf, cap, &n, "# Ritual Machine %s\npatch 1\n", state ? "working state (synth.cfg): the sound as it was left, saved by itself" : "preset");
    if (state) add(buf, cap, &n, "from %d %s\n", edited ? 1 : 0, preset ? preset : "");
    add(buf, cap, &n, "type %d voices %d strvoices %d glide %d legato %d fmpatch %d\n", c->type, c->voices, c->str_voices, c->glide, c->legato, c->fm_patch);
    {
        const str_params_t *st = &params->str;
        const float mix[4] = {rack->br_lvl[0], rack->br_pan[0], rack->br_lvl[1], rack->br_pan[1]};
        const float par[7] = {(float)params->wave, params->pulse_width, params->volume, (float)params->filter_type, params->cutoff_hz, params->resonance,
                              params->filter_env_amt};
        const float str[13] = {(float)st->wave, (float)st->osc, (float)st->lp_on, (float)st->ftype, st->detune, st->mix, st->pw, st->level, st->lp_cut,
                               st->lp_env, st->lp_key, st->fcut, st->fres};
        add_line(buf, cap, &n, "mix", mix, 4);
        add_line(buf, cap, &n, "par", par, 7);
        add_env(buf, cap, &n, "amp", &params->amp_env);
        add_env(buf, cap, &n, "fenv", &params->filter_env);
        add_line(buf, cap, &n, "str", str, 13);
    }
    if (synth_type_is_fm(c->type)) {
        add(buf, cap, &n, "fm %d", c->fm.algorithm);
        add_f(buf, cap, &n, c->fm.feedback);
        add(buf, cap, &n, " %u %.*s\n", (unsigned)c->fm.rel_ms, DX7_NAME_LEN, c->fm.name);
        for (int k = 0; k < DX7_OPS; k++) {
            const dx7_op_t *o = &c->fm.op[k];
            const float f[3] = {o->coarse, o->fine, o->fixed_hz};
            add(buf, cap, &n, "op %d %d", k + 1, o->level);
            for (int i = 0; i < 3; i++) add_f(buf, cap, &n, f[i]);
            add(buf, cap, &n, " %d %d %d %d %u %u %u %u\n", o->eg_l[0], o->eg_l[1], o->eg_l[2], o->eg_l[3], (unsigned)o->eg_t[0], (unsigned)o->eg_t[1],
                (unsigned)o->eg_t[2], (unsigned)o->eg_t[3]);
        }
    }
    for (int i = 0; i < rack->count; i++) {
        const rack_slot_t *s = &rack->slot[i];
        int used = rack_mparam_count((module_type_t)s->type);           // its values: what it shows, and whatever else it keeps (not the zeros at the end)
        for (int k = MOD_PARAM_MAX - 1; k >= used; k--) if (s->v[k] != 0.0f) { used = k + 1; break; }
        if (s->type == MOD_MSEQ && used <= MP_MS_POOL) used = MP_MS_POOL + 1;   // (its pattern's place: the ms lines name it)
        add(buf, cap, &n, "mod %d %s %d %d %d %d %d :", s->id, rack_type_code((module_type_t)s->type), s->row, s->para, s->penv, s->tgt_id, s->tgt_param);
        for (int k = 0; k < used && k < MOD_PARAM_MAX; k++) add_f(buf, cap, &n, s->type == MOD_SAMPLER && k == MP_SM_FILE ? 0.0f : s->v[k]);   // (the file: the smp line)
        add(buf, cap, &n, "\n");
        char name[AUDIO_NAME_MAX];
        sampler_name(s, name, sizeof name);
        if (name[0]) add(buf, cap, &n, "smp %d %s\n", s->id, name);
        if (s->type != MOD_MSEQ) continue;
        const int pool = (int)s->v[MP_MS_POOL];
        if (pool < 0 || pool >= MS_POOL) continue;
        for (int l = 0; l < MS_LANES; l++) {
            const ms_lane_t *ln = &rack->ms[pool].lane[l];
            add(buf, cap, &n, "ms %d %d %d %d %d %d", pool, l, ln->tgt_id, ln->tgt_param, ln->linear, ln->bipolar);
            add_f(buf, cap, &n, ln->depth);
            add(buf, cap, &n, " %x :", ln->active);
            for (int k = 0; k < MS_STEPS; k++) add(buf, cap, &n, " %d", ln->val[k]);
            add(buf, cap, &n, "\n");
        }
    }
    for (int k = 0; k < SYNTH_UI_MACROS; k++)
        for (int i = 0; i < ui->macro[k].n && i < SYNTH_UI_MACRO_DESTS; i++) {
            char t[64];
            if (mapping_to_text(&ui->macro[k].dest[i], t, sizeof t)) add(buf, cap, &n, "macro %d %s\n", k + 1, t);
        }
    for (int a = 0; a < 2; a++) {
        char t[64];
        if (mapping_to_text(&ui->joy[a], t, sizeof t)) add(buf, cap, &n, "joy %s %s\n", a ? "y" : "x", t);
    }
    return n < cap ? n : -1;
}

/* ---------------- reading ---------------- */

static int clampi_(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Up to `max` numbers from `s` into out; how many were there. A number that is not one (nan, inf) ends the list.
static int floats(const char *s, float *out, int max) {
    int n = 0;
    while (n < max) {
        char *end = NULL;
        const float v = strtof(s, &end);
        if (end == s || !(v == v) || v > 1e12f || v < -1e12f) break;
        out[n++] = v;
        s = end;
    }
    return n;
}

static void read_env(const char *s, env_params_t *e) {
    float v[8];
    if (floats(s, v, 8) != 8) return;
    *e = (env_params_t){v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]};
}

static bool word(const char *line, const char *w) {
    const size_t n = strlen(w);
    return !strncmp(line, w, n) && (line[n] == ' ' || line[n] == '\n' || line[n] == '\r' || !line[n]);
}

// The rest of a line (a name, spaces allowed) into out, without the line end.
static void rest(const char *s, char *out, int n) {
    int k = 0;
    while (*s == ' ') s++;
    while (*s && *s != '\n' && *s != '\r' && k < n - 1) out[k++] = *s++;
    while (k > 0 && out[k - 1] == ' ') k--;
    out[k] = 0;
}

#define EACH_LINE(line, txt) for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL)

bool patch_from_text(rack_t *rack, synth_params_t *params, synth_ui_t *ui, char *preset, bool *edited, const char *txt) {
    bool is_patch = false;
    EACH_LINE(line, txt) if (word(line, "patch")) { is_patch = true; break; }
    if (!is_patch) return false;

    rack_t r;
    rack_clear(&r);
    r.cfg.volume = rack->cfg.volume; r.cfg.mono = rack->cfg.mono; r.cfg.speaker = rack->cfg.speaker; r.cfg.knob_mode = rack->cfg.knob_mode;
    synth_params_t p;
    synth_params_default(&p);
    macro_def_t macro[SYNTH_UI_MACROS];
    mapping_t joy[2];
    memset(macro, 0, sizeof macro);
    memset(joy, 0, sizeof joy);
    char from[PRESET_NAME_MAX] = "";
    int from_edited = 0;

    int type_now = SYNTH_MODULAR;
    EACH_LINE(line, txt) {
        int t = 0, v = 1, sv = SYNTH_STR_MAX_VOICES, g = 0, l = 0, f = 0;
        if (sscanf(line, "type %d voices %d strvoices %d glide %d legato %d fmpatch %d", &t, &v, &sv, &g, &l, &f) < 1) continue;
        type_now = clampi_(t, 0, SYNTH_TYPE_COUNT - 1);
        r.cfg.voices = (uint8_t)clampi_(v, 1, SYNTH_MAX_VOICES);
        r.cfg.str_voices = (uint8_t)clampi_(sv, 1, SYNTH_STR_MAX_VOICES);
        r.cfg.glide = (uint8_t)clampi_(g, 0, 255);            // (an index past the list reads as Off: synth_config_glide_ms)
        r.cfg.legato = l ? 1 : 0;
        r.cfg.fm_patch = (uint8_t)clampi_(f, 0, FM_PATCH_COUNT - 1);
        break;
    }
    dx7_load_factory(&r.cfg.fm, r.cfg.fm_patch);              // (the FM lines below replace it)
    // The modules are put back as in a modular rack, whatever the type: an FM or Strings sound keeps the branches it does not play
    // (they come back with the Modular type), and the row rules are the modular ones.

    int pool_of[MS_POOL];                                     // a motion sequencer's place in the file -> its place now
    uint8_t para[RACK_MAX];
    for (int i = 0; i < MS_POOL; i++) pool_of[i] = -1;
    memset(para, 0, sizeof para);
    EACH_LINE(line, txt) {
        float v[MOD_PARAM_MAX];
        int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, at = 0;
        char w[16];
        if (word(line, "mix")) {
            if (floats(line + 3, v, 4) == 4) { r.br_lvl[0] = v[0]; r.br_pan[0] = v[1]; r.br_lvl[1] = v[2]; r.br_pan[1] = v[3]; }
        } else if (word(line, "par")) {
            if (floats(line + 3, v, 7) == 7) {
                p.wave = (uint8_t)v[0]; p.pulse_width = v[1]; p.volume = v[2]; p.filter_type = (uint8_t)v[3]; p.cutoff_hz = v[4]; p.resonance = v[5];
                p.filter_env_amt = v[6];
            }
        } else if (word(line, "amp")) {
            read_env(line + 3, &p.amp_env);
        } else if (word(line, "fenv")) {
            read_env(line + 4, &p.filter_env);
        } else if (word(line, "str")) {
            if (floats(line + 3, v, 13) == 13)
                p.str = (str_params_t){(uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12]};
        } else if (word(line, "fm")) {
            float fb = 0;
            unsigned rel = 0;
            if (sscanf(line, "fm %d %f %u%n", &a, &fb, &rel, &at) < 3 || !at) continue;
            r.cfg.fm.algorithm = (uint8_t)clampi_(a, 1, DX7_ALGORITHMS);
            r.cfg.fm.feedback = fb < 0 ? 0 : fb > 1 ? 1 : fb;
            r.cfg.fm.rel_ms = rel;
            memset(r.cfg.fm.name, 0, sizeof r.cfg.fm.name);
            rest(line + at, r.cfg.fm.name, DX7_NAME_LEN);
        } else if (word(line, "op")) {
            if (sscanf(line, "op %d%n", &a, &at) != 1 || a < 1 || a > DX7_OPS || floats(line + at, v, 12) != 12) continue;
            dx7_op_t *o = &r.cfg.fm.op[a - 1];
            o->level = (uint8_t)clampi_((int)v[0], 0, 99); o->coarse = v[1]; o->fine = v[2]; o->fixed_hz = v[3];
            for (int k = 0; k < 4; k++) { o->eg_l[k] = (uint8_t)clampi_((int)v[4 + k], 0, 99); o->eg_t[k] = v[8 + k] > 0 ? (uint32_t)v[8 + k] : 0; }
        } else if (word(line, "mod")) {
            if (sscanf(line, "mod %d %15s %d %d %d %d %d :%n", &a, w, &b, &c, &d, &e, &f, &at) != 7 || !at) continue;
            int type = MOD_TYPE_COUNT;
            for (int t = 0; t < MOD_TYPE_COUNT; t++) if (!strcmp(w, rack_type_code((module_type_t)t))) type = t;
            int top = 0;                                      // (the id it gets until its own is set: above every id so far)
            for (int i = 0; i < r.count; i++) if (r.slot[i].id > top) top = r.slot[i].id;
            r.next_id = (uint8_t)(top >= 255 ? 1 : top + 1);
            if (type == MOD_TYPE_COUNT || b < 0 || b >= RACK_ROWS || !rack_insert_row(&r, r.count, b, (module_type_t)type)) continue;
            rack_slot_t *s = &r.slot[r.count - 1];
            if (a >= 1 && a <= 255 && rack_find(&r, a) == RACK_NONE) s->id = (uint8_t)a;       // its own id (the targets, the macros and the jumps name it)
            s->penv = (uint8_t)clampi_(d, 0, PARA_ENV_COUNT - 1);
            s->tgt_id = (uint8_t)clampi_(e, 0, 255);
            s->tgt_param = (uint8_t)clampi_(f, 0, 255);
            para[r.count - 1] = c ? 1 : 0;
            const float pool = s->v[MP_MS_POOL];
            const int got = floats(line + at, v, MOD_PARAM_MAX);
            const int file_pool = got > MP_MS_POOL ? (int)v[MP_MS_POOL] : 0;
            if (type == MOD_MSEQ && file_pool >= 0 && file_pool < MS_POOL) pool_of[file_pool] = (int)pool;
            for (int k = 0; k < got; k++) s->v[k] = v[k];
            if (type == MOD_MSEQ) s->v[MP_MS_POOL] = pool;
            if (type == MOD_SAMPLER) s->v[MP_SM_FILE] = 0;
        } else if (word(line, "smp")) {
            char name[AUDIO_NAME_MAX];
            if (sscanf(line, "smp %d%n", &a, &at) != 1 || !at) continue;
            rest(line + at, name, sizeof name);
            const int slot = rack_find(&r, a);
            if (slot == RACK_NONE || r.slot[slot].type != MOD_SAMPLER || !name[0]) continue;
            const int index = audio_sample_use(name);
            r.slot[slot].v[MP_SM_FILE] = index >= 0 ? (float)(index + 1) : 0.0f;
        } else if (word(line, "ms")) {
            float depth = 0;
            unsigned active = 0;
            if (sscanf(line, "ms %d %d %d %d %d %d %f %x :%n", &a, &b, &c, &d, &e, &g, &depth, &active, &at) != 8 || !at) continue;
            if (a < 0 || a >= MS_POOL || pool_of[a] < 0 || b < 0 || b >= MS_LANES) continue;
            ms_lane_t *ln = &r.ms[pool_of[a]].lane[b];
            ln->tgt_id = (uint8_t)clampi_(c, 0, 255); ln->tgt_param = (uint8_t)clampi_(d, 0, 255);
            ln->linear = e ? 1 : 0; ln->bipolar = g ? 1 : 0; ln->depth = depth; ln->active = (uint16_t)active;
            if (floats(line + at, v, MS_STEPS) == MS_STEPS) for (int k = 0; k < MS_STEPS; k++) ln->val[k] = (uint8_t)clampi_((int)v[k], 0, 100);
        } else if (word(line, "macro")) {
            mapping_t mp;
            if (sscanf(line, "macro %d %n", &a, &at) != 1 || !at || a < 1 || a > SYNTH_UI_MACROS || !mapping_parse(line + at, &mp)) continue;
            macro_def_t *md = &macro[a - 1];
            if (md->n < SYNTH_UI_MACRO_DESTS) md->dest[md->n++] = mp;
        } else if (word(line, "joy")) {
            mapping_t mp;
            if (sscanf(line, "joy %3s %n", w, &at) != 1 || !at || (strcmp(w, "x") && strcmp(w, "y")) || !mapping_parse(line + at, &mp)) continue;
            joy[w[0] == 'y'] = mp;
        } else if (word(line, "from")) {
            if (sscanf(line, "from %d%n", &from_edited, &at) == 1 && at) rest(line + at, from, sizeof from);
        }
    }
    int top = 0;
    for (int i = 0; i < r.count; i++) {
        rack_slot_t *s = &r.slot[i];
        if (s->id > top) top = s->id;
        if (s->tgt_id && rack_find(&r, s->tgt_id) == RACK_NONE) s->tgt_id = 0;       // a target that is not in the file
        if (para[i]) rack_set_para(&r, i, true);
    }
    r.next_id = (uint8_t)(top >= 255 ? 1 : top + 1);
    r.cfg.type = (uint8_t)type_now;

    *rack = r;
    *params = p;
    memcpy(ui->macro, macro, sizeof macro);
    memcpy(ui->joy, joy, sizeof joy);
    memset(ui->joy_prev, 0, sizeof ui->joy_prev);
    ui->joy_fresh = -1;
    if (preset) snprintf(preset, PRESET_NAME_MAX, "%s", from);
    if (edited) *edited = from_edited != 0;
    return true;
}

/* ---------------- the hash ---------------- */

static uint32_t fnv(uint32_t h, const void *data, size_t n) {
    const uint8_t *b = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}
#define H(x) h = fnv(h, &(x), sizeof(x))

uint32_t patch_hash(const rack_t *rack, const synth_params_t *params, const synth_ui_t *ui) {
    uint32_t h = 2166136261u;
    const synth_config_t *c = &rack->cfg;
    H(c->type); H(c->voices); H(c->str_voices); H(c->glide); H(c->legato); H(c->fm_patch);
    H(rack->br_lvl); H(rack->br_pan);
    H(params->wave); H(params->pulse_width); H(params->volume); H(params->filter_type); H(params->cutoff_hz); H(params->resonance);
    H(params->filter_env_amt); H(params->amp_env); H(params->filter_env); H(params->str);
    if (synth_type_is_fm(c->type)) {
        H(c->fm.algorithm); H(c->fm.feedback); H(c->fm.rel_ms);
        h = fnv(h, c->fm.name, strnlen(c->fm.name, DX7_NAME_LEN));
        for (int k = 0; k < DX7_OPS; k++) { const dx7_op_t *o = &c->fm.op[k]; H(o->level); H(o->coarse); H(o->fine); H(o->fixed_hz); H(o->eg_l); H(o->eg_t); }
    }
    for (int i = 0; i < rack->count; i++) {
        const rack_slot_t *s = &rack->slot[i];
        H(s->type); H(s->id); H(s->tgt_id); H(s->tgt_param); H(s->row); H(s->para); H(s->penv);
        if (s->type == MOD_SAMPLER) {                           // its file by name: the catalog index is not the same at the next start
            char name[AUDIO_NAME_MAX];
            sampler_name(s, name, sizeof name);
            h = fnv(h, name, strlen(name));
            h = fnv(h, &s->v[MP_SM_FILE + 1], sizeof s->v - (MP_SM_FILE + 1) * sizeof s->v[0]);
            continue;
        }
        H(s->v);
        if (s->type != MOD_MSEQ || s->v[MP_MS_POOL] < 0 || s->v[MP_MS_POOL] >= MS_POOL) continue;
        const ms_pattern_t *m = &rack->ms[(int)s->v[MP_MS_POOL]];
        for (int l = 0; l < MS_LANES; l++) { const ms_lane_t *ln = &m->lane[l]; H(ln->val); H(ln->active); H(ln->tgt_id); H(ln->tgt_param); H(ln->linear); H(ln->bipolar); H(ln->depth); }
    }
    for (int k = 0; k < SYNTH_UI_MACROS; k++) { H(ui->macro[k].n); h = fnv(h, ui->macro[k].dest, sizeof ui->macro[k].dest[0] * (ui->macro[k].n < SYNTH_UI_MACRO_DESTS ? ui->macro[k].n : SYNTH_UI_MACRO_DESTS)); }
    H(ui->joy);
    return h;
}

/* ---------------- files ---------------- */

bool preset_file(const char *name, char *out, int n) {
    const int len = snprintf(out, (size_t)n, STORAGE_DIR_PRESETS "/%s" PRESET_EXT, name);
    return len > 0 && len < n;
}

bool patch_save(const char *file, const rack_t *rack, const synth_params_t *params, const synth_ui_t *ui, bool state, const char *preset,
                bool edited, bool *too_big) {
    const int n = patch_to_text(rack, params, ui, state, preset, edited, g_buf, STORAGE_FILE_MAX);
    if (too_big) *too_big = n < 0;
    return n >= 0 && storage_write(file, g_buf, n);
}

bool patch_load(const char *file, rack_t *rack, synth_params_t *params, synth_ui_t *ui, char *preset, bool *edited) {
    return storage_read(file, g_buf, STORAGE_FILE_MAX) >= 0 && patch_from_text(rack, params, ui, preset, edited, g_buf);
}
