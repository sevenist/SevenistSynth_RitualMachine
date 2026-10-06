#include "core/rack.h"
#include "core/synth_params.h"
#include <stdio.h>
#include <string.h>

typedef enum { K_ENUM, K_LIN, K_LOG } mp_kind_t;

typedef struct {                // descriptor of one module parameter
    const char *label;
    mp_kind_t   kind;
    float       min, max, step;     // K_LIN: added; K_LOG: multiplied
    float       def;
    const char *unit;               // "Hz" prints as kHz above 1000
    uint8_t     decimals;
    const char *const *names;       // K_ENUM
} mp_t;

static const char *const wave_names[]   = {"Sine", "Pulse", "SawDn", "SawUp", "Tri", "Noise", "Karp", "Modal", "FM2", "Fold", "SSaw", "Vowel", "Add", "Dust", "Strng"};
static const char *const qual_names[]   = {"Blep", "Mip", "Naive"};
static const char *const filter_names[] = {"Off", "LP", "BP", "HP", "LP24", "Notch", "LP6", "Ladr", "ChLP"};
static const char *const sat_names[]    = {"Tanh", "Clip", "Fold", "Crush", "Tube", "Tape", "Diode", "Cheb", "Rect", "Decim"};
static const char *const loop_names[]   = {"File", "Off", "Fwd", "Ping"};
static const char *const dir_names[]    = {"Fwd", "Rev"};
static const char *const track_names[]  = {"Key", "Drum"};
static const char *const trig_names[]   = {"Gate", "Shot"};
static const char *const smode_names[]  = {"On", "Stop"};
static const char *const out_names[]    = {"On", "Mute"};
static const char *const lfo_names[]    = {"Sine", "Tri", "SawDn", "Pulse"};

static const mp_t osc_mp[] = {
    [MP_OC_WAVE]   = {"Wav",  K_ENUM, 0, 14, 1, 2, "", 0, wave_names},
    [MP_OC_PW]     = {"PW",   K_LIN, 0.05f, 0.95f, 0.05f, 0.5f, "", 2, 0},
    [MP_OC_LEVEL]  = {"Lvl",  K_LIN, 0, 1, 0.05f, 1, "", 2, 0},
    [MP_OC_COARSE] = {"Crs",  K_LIN, -24, 24, 1, 0, "st", 0, 0},
    [MP_OC_FINE]   = {"Fine", K_LIN, -50, 50, 1, 0, "ct", 0, 0},
    [MP_OC_MUTE]   = {"Out",  K_ENUM, 0, 1, 1, 0, "", 0, out_names},       // with a target: Mute silences the chain output, not the modulation
    [MP_OC_DEPTH]  = {"Dpth", K_LIN, 0, 2, 0.05f, 1, "", 2, 0},            // with a target: modulation depth (in the target's unit, see rack_depth_*)
    [MP_OC_MORPH]  = {"Mrph", K_LIN, 0, 1, 0.05f, 0.5f, "", 2, 0},         // engines only
    [MP_OC_QUAL]   = {"Q",    K_ENUM, 0, 2, 1, 0, "", 0, qual_names},     // Saw / Pulse / Tri and Strng: PolyBLEP, mipmap table, naive
};
static const mp_t flt_mp[] = {
    [MP_FL_TYPE]   = {"Typ", K_ENUM, 0, FILT_COUNT - 1, 1, 1, "", 0, filter_names},
    [MP_FL_CUT]    = {"Cut", K_LOG, 20, 18000, 1.12f, 4000, "Hz", 0, 0},
    [MP_FL_RES]    = {"Res", K_LIN, 0.5f, 10, 0.1f, 0.7f, "", 1, 0},
    [MP_FL_ENVAMT] = {"Env", K_LIN, 0, 8, 0.25f, 0, "", 2, 0},
    [MP_FL_A]      = {"Atk", K_LOG, 1, 5000, 1.2f, 5, "ms", 0, 0},
    [MP_FL_D]      = {"Dec", K_LOG, 1, 5000, 1.2f, 300, "ms", 0, 0},
    [MP_FL_S]      = {"Sus", K_LIN, 0, 1, 0.05f, 0, "", 2, 0},
    [MP_FL_R]      = {"Rel", K_LOG, 1, 5000, 1.2f, 200, "ms", 0, 0},
    [MP_FL_ACV]    = {"ACv", K_LIN, -100, 100, 5, 55, "%", 0, 0},
    [MP_FL_DCV]    = {"DCv", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [MP_FL_RCV]    = {"RCv", K_LIN, -100, 100, 5, 60, "%", 0, 0},
};
static const mp_t sat_mp[] = {
    [MP_SA_MODE]  = {"Mod", K_ENUM, 0, 9, 1, 1, "", 0, sat_names},
    [MP_SA_DRIVE] = {"Drv", K_LOG, 1, 16, 1.25f, 4, "", 1, 0},
    [MP_SA_MIX]   = {"Mix", K_LIN, 0, 1, 0.05f, 1, "", 2, 0},
};
static const mp_t lfo_mp[] = {
    [MP_LF_SHAPE] = {"Shp", K_ENUM, 0, 3, 1, 0, "", 0, lfo_names},
    [MP_LF_RATE]  = {"Rate", K_LOG, 0.05f, 30, 1.2f, 4, "Hz", 2, 0},
    [MP_LF_DEPTH] = {"Dpth", K_LIN, 0, 2, 0.05f, 1, "", 2, 0},
};
static const mp_t rs_mp[] = {
    [MP_RS_TUNE] = {"Crs",  K_LIN, -24, 24, 1, 0, "st", 0, 0},
    [MP_RS_FB]   = {"Fb",   K_LIN, -95, 95, 5, 80, "%", 0, 0},
    [MP_RS_DAMP] = {"Dmp",  K_LOG, 200, 18000, 1.25f, 8000, "Hz", 0, 0},
    [MP_RS_INT]  = {"Int",  K_LIN, 0, 24, 1, 0, "st", 0, 0},
    [MP_RS_MIX]  = {"Mix",  K_LIN, 0, 1, 0.05f, 0.5f, "", 2, 0},
};
static const mp_t env_mp[] = {
    [MP_EN_A]     = {"Atk", K_LOG, 1, 5000, 1.2f, 5, "ms", 0, 0},
    [MP_EN_D]     = {"Dec", K_LOG, 1, 5000, 1.2f, 300, "ms", 0, 0},
    [MP_EN_S]     = {"Sus", K_LIN, 0, 1, 0.05f, 0, "", 2, 0},
    [MP_EN_DEPTH] = {"Dpth", K_LIN, 0, 2, 0.05f, 1, "", 2, 0},
    [MP_EN_R]     = {"Rel", K_LOG, 1, 5000, 1.2f, 200, "ms", 0, 0},
    [MP_EN_HOLD]  = {"Hld", K_LIN, 0, 2000, 5, 0, "ms", 0, 0},
    [MP_EN_START] = {"Str", K_LIN, 0, 1, 0.05f, 0, "", 2, 0},
    [MP_EN_ACV]   = {"ACv", K_LIN, -100, 100, 5, 55, "%", 0, 0},
    [MP_EN_DCV]   = {"DCv", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [MP_EN_RCV]   = {"RCv", K_LIN, -100, 100, 5, 60, "%", 0, 0},
};
static const mp_t eg_mp[] = {            // labels are generic: the EG pages show the selected point (see ui_pages.c / ui_input.c)
    [0]  = {"T1", K_LIN, 0, 2000, 1, 1, "ms", 0, 0},   [1]  = {"L1", K_LIN, 0, 100, 5, 100, "%", 0, 0}, [2]  = {"C1", K_LIN, -100, 100, 5, 0, "%", 0, 0},
    [3]  = {"T2", K_LIN, 0, 2000, 1, 100, "ms", 0, 0}, [4]  = {"L2", K_LIN, 0, 100, 5, 20, "%", 0, 0},  [5]  = {"C2", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [6]  = {"T3", K_LIN, 0, 2000, 1, 0, "ms", 0, 0},   [7]  = {"L3", K_LIN, 0, 100, 5, 0, "%", 0, 0},   [8]  = {"C3", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [9]  = {"T4", K_LIN, 0, 2000, 1, 0, "ms", 0, 0},   [10] = {"L4", K_LIN, 0, 100, 5, 0, "%", 0, 0},   [11] = {"C4", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [MP_EG_SUS]   = {"Sus",  K_LIN, 1, 4, 1, 2, "", 0, 0},
    [MP_EG_REL]   = {"Rel",  K_LOG, 1, 5000, 1.2f, 200, "ms", 0, 0},
    [MP_EG_RCV]   = {"RCv",  K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [MP_EG_ONE]   = {"Trig", K_ENUM, 0, 1, 1, 0, "", 0, trig_names},
    [MP_EG_DEPTH] = {"Dpth", K_LIN, 0, 2, 0.05f, 1, "", 2, 0},
};

static const mp_t sm_mp[] = {
    [MP_SM_FILE]   = {"File",  K_LIN, 0, 32, 1, 0, "", 0, 0},                  // shown by name (ui_draw.c); 0 = none
    [MP_SM_LEVEL]  = {"Lvl",   K_LIN, 0, 1, 0.05f, 1, "", 2, 0},
    [MP_SM_COARSE] = {"Crs",   K_LIN, -24, 24, 1, 0, "st", 0, 0},
    [MP_SM_FINE]   = {"Fine",  K_LIN, -50, 50, 1, 0, "ct", 0, 0},
    [MP_SM_LOOP]   = {"Loop",  K_ENUM, 0, 3, 1, 0, "", 0, loop_names},
    [MP_SM_REV]    = {"Dir",   K_ENUM, 0, 1, 1, 0, "", 0, dir_names},
    [MP_SM_START]  = {"Start", K_LIN, 0, 5000, 10, 0, "ms", 0, 0},
    [MP_SM_TRACK]  = {"Trk",   K_ENUM, 0, 1, 1, 0, "", 0, track_names},
    [MP_SM_SLICE]  = {"Slc",   K_LIN, 0, 16, 1, 0, "", 0, 0},                  // 0 = all
    [MP_SM_SMODE]  = {"Mode",  K_ENUM, 0, 1, 1, 0, "", 0, smode_names},
};

typedef struct {
    const char *code, *name;
    bool audio, modulator;
    int nparams;
    const char *pshort[4], *plong[4];   // modulation targets (see rack_param_name)
    const mp_t *mp; int nmp;            // module's own parameters
} type_info_t;

static const type_info_t info[MOD_TYPE_COUNT] = {
    [MOD_OSC]    = {"OC", "Oscillator", true,  false, 4, {"Pit", "Lvl", "PW", "Mrph"},   {"Pitch", "Level", "Pulse width", "Morph"}, osc_mp, 8},
    [MOD_FILTER] = {"FL", "Filter",     true,  false, 2, {"Cut", "Res"},         {"Cutoff", "Resonance"}, flt_mp, 11},
    [MOD_SAT]    = {"SA", "Saturation", true,  false, 2, {"Drv", "Mix"},         {"Drive", "Mix"}, sat_mp, 3},
    [MOD_LFO]    = {"LF", "LFO",        false, true,  2, {"Rate", "Dpth"},       {"Rate", "Depth"}, lfo_mp, 3},
    [MOD_MSEQ]   = {"MS", "Motion Seq", false, true,  1, {"Dpth"},              {"Depth"}, NULL, 0},
    [MOD_ENV]    = {"EN", "Envelope",   false, true,  1, {"Dpth"},              {"Depth"}, env_mp, 10},
    [MOD_SAMPLER]= {"SM", "Sampler",    true,  false, 1, {"Pit"},               {"Pitch"}, sm_mp, 10},
    [MOD_COMB]   = {"RS", "Resonator",  true,  false, 1, {"Mix"},               {"Mix"}, rs_mp, 5},
    [MOD_EG]     = {"EG", "Multi Env",  false, true,  1, {"Dpth"},              {"Depth"}, eg_mp, 17},
};

static void depth_reset(rack_t *r, int slot);

const char *rack_type_code(module_type_t t) { return info[t].code; }
const char *rack_type_name(module_type_t t) { return info[t].name; }
bool rack_is_audio(module_type_t t)         { return info[t].audio; }
bool rack_is_modulator(module_type_t t)     { return info[t].modulator; }
bool rack_can_target(module_type_t t)       { return (info[t].modulator && t != MOD_MSEQ) || t == MOD_OSC; }
int  rack_param_count(module_type_t t)      { return info[t].nparams; }
const char *rack_param_name(module_type_t t, int p) { return info[t].pshort[p]; }
const char *rack_param_long(module_type_t t, int p) { return info[t].plong[p]; }

void rack_clear(rack_t *r) {
    memset(r, 0, sizeof *r);
    r->next_id = 1;
    synth_config_init(&r->cfg);
}

void rack_init(rack_t *r) {
    rack_clear(r);
    rack_insert(r, 0, MOD_OSC);
    rack_insert(r, 1, MOD_FILTER);
    rack_insert(r, 2, MOD_SAT);
    rack_insert(r, 3, MOD_LFO);
    r->slot[3].tgt_id = r->slot[1].id;      // LFO -> filter cutoff
    r->slot[3].tgt_param = 0;
    r->slot[3].v[MP_LF_DEPTH] = 1.5f;       // octaves of cutoff
    r->cfg.fxr.slot[1].v[0] = 1000;         // startup patch: delay 1000 ms, 40 % mix; reverb 40 % mix (the FX type defaults stay dry)
    r->cfg.fxr.slot[1].v[2] = 40;
    r->cfg.fxr.slot[2].v[0] = 40;
}

// The startup patch: four oscillators, each a different engine (Karplus string, Modal, Supersaw, Additive), into one filter; the delay and the
// reverb are on. Eight voices of this is the heaviest thing the default synth does, which is the point: it is the load the engines are tuned for.
void rack_init_startup(rack_t *r) {
    rack_clear(r);
    static const int engine[4] = {0, 1, 4, 6};                       // OSCX_KARP, OSCX_MODAL, OSCX_SSAW, OSCX_ADD
    static const int coarse[4] = {0, 0, 0, 12};
    for (int i = 0; i < 4; i++) {
        rack_insert(r, i, MOD_OSC);
        r->slot[i].v[MP_OC_WAVE] = (float)(OC_FIRST_ENGINE + engine[i]);
        r->slot[i].v[MP_OC_COARSE] = (float)coarse[i];
    }
    rack_insert(r, 4, MOD_FILTER);
    r->cfg.fxr.slot[1].v[0] = 1000;         // delay 1000 ms, 40 % mix; reverb 40 % mix (the FX type defaults stay dry)
    r->cfg.fxr.slot[1].v[2] = 40;
    r->cfg.fxr.slot[2].v[0] = 40;
}

// Measurement patch for the sampler: nothing but a sampler and one filter (wide open), every master effect dry, so what is measured or heard is the sampler path.
void rack_init_sampler(rack_t *r, int file, int loop) {
    rack_clear(r);
    rack_insert(r, 0, MOD_SAMPLER);
    r->slot[0].v[MP_SM_FILE] = (float)(file + 1);
    r->slot[0].v[MP_SM_LOOP] = (float)loop;
    rack_insert(r, 1, MOD_FILTER);
}

static void ms_pattern_default(ms_pattern_t *p) {
    memset(p, 0, sizeof *p);
    for (int l = 0; l < MS_LANES; l++) {
        for (int i = 0; i < MS_STEPS; i++) p->lane[l].val[i] = 50;
        p->lane[l].bipolar = 1;
        p->lane[l].depth = 1.0f;                                 // (replaced by the unit's default once a target is chosen)
    }
    static const uint8_t demo[4] = {100, 30, 70, 0};            // lane 1: a rising and falling shape on every 4th step
    for (int i = 0; i < 4; i++) { p->lane[0].val[i * 4] = demo[i]; p->lane[0].active |= (uint16_t)(1u << (i * 4)); }
}

static int ms_pool_free(const rack_t *r) {
    for (int p = 0; p < MS_POOL; p++) {
        bool used = false;
        for (int i = 0; i < r->count; i++) if (r->slot[i].type == MOD_MSEQ && (int)r->slot[i].v[MP_MS_POOL] == p) used = true;
        if (!used) return p;
    }
    return -1;
}

bool rack_insert(rack_t *r, int pos, module_type_t type) {
    if (r->count >= RACK_MAX || pos < 0 || pos > r->count || type >= MOD_TYPE_COUNT) return false;
    int pool = -1;
    if (type == MOD_MSEQ && (pool = ms_pool_free(r)) < 0) return false;      // both motion sequencers are in use
    for (int i = r->count; i > pos; i--) r->slot[i] = r->slot[i - 1];
    r->slot[pos] = (rack_slot_t){.type = (uint8_t)type, .id = r->next_id++};
    for (int i = 0; i < info[type].nmp; i++) r->slot[pos].v[i] = info[type].mp[i].def;
    if (pool >= 0) { r->slot[pos].v[MP_MS_POOL] = (float)pool; ms_pattern_default(&r->ms[pool]); }
    if (r->next_id == 0) r->next_id = 1;
    r->count++;
    return true;
}

bool rack_delete(rack_t *r, int pos) {
    if (pos < 0 || pos >= r->count) return false;
    uint8_t id = r->slot[pos].id;
    for (int i = pos; i < r->count - 1; i++) r->slot[i] = r->slot[i + 1];
    r->count--;
    for (int i = 0; i < r->count; i++)
        if (r->slot[i].tgt_id == id) { r->slot[i].tgt_id = 0; r->slot[i].tgt_param = 0; }
    for (int p = 0; p < MS_POOL; p++)
        for (int l = 0; l < MS_LANES; l++)
            if (r->ms[p].lane[l].tgt_id == id) { r->ms[p].lane[l].tgt_id = 0; r->ms[p].lane[l].tgt_param = 0; }
    return true;
}

int rack_find(const rack_t *r, int id) {
    for (int i = 0; i < r->count; i++) if (r->slot[i].id == id) return i;
    return RACK_NONE;
}

int rack_instance(const rack_t *r, int slot) {
    int n = 0;
    for (int i = 0; i <= slot; i++) if (r->slot[i].type == r->slot[slot].type) n++;
    return n;
}

bool rack_slot_is_mod(const rack_t *r, int slot) {
    const rack_slot_t *s = &r->slot[slot];
    return info[s->type].modulator || (s->type == MOD_OSC && s->tgt_id != 0);
}

bool rack_slot_is_audio(const rack_t *r, int slot) {
    return info[r->slot[slot].type].audio;                       // an OSC with a target stays in the chain (it can be muted there)
}

int rack_audio_prev(const rack_t *r, int slot) {
    for (int i = slot - 1; i >= 0; i--) if (rack_slot_is_audio(r, i)) return i;
    return RACK_NONE;
}

int rack_audio_next(const rack_t *r, int slot) {
    for (int i = slot + 1; i < r->count; i++) if (rack_slot_is_audio(r, i)) return i;
    return RACK_OUT;
}

bool rack_has_signal(const rack_t *r, int slot) {
    for (int i = 0; i <= slot; i++) {
        if (r->slot[i].type == MOD_OSC && !(r->slot[i].tgt_id && r->slot[i].v[MP_OC_MUTE] > 0.5f)) return true;
        if (r->slot[i].type == MOD_SAMPLER && r->slot[i].v[MP_SM_FILE] > 0.5f) return true;
    }
    return false;
}

void rack_cycle_target(rack_t *r, int slot, int dir) {
    rack_slot_t *s = &r->slot[slot];
    if (!rack_can_target((module_type_t)s->type)) return;
    // candidates: 0 = none, then every other module in slot order
    int cur = s->tgt_id ? rack_find(r, s->tgt_id) : RACK_NONE;   // slot index or none
    int n = r->count;
    int pos = cur + 1;                     // 0 = none, 1..n = slot+1
    for (int k = 0; k <= n; k++) {
        pos = (pos + dir + n + 1) % (n + 1);
        if (pos == 0) { s->tgt_id = 0; s->tgt_param = 0; depth_reset(r, slot); return; }
        if (pos - 1 != slot) { s->tgt_id = r->slot[pos - 1].id; s->tgt_param = 0; depth_reset(r, slot); return; }
    }
}

void rack_cycle_param(rack_t *r, int slot, int dir) {
    rack_slot_t *s = &r->slot[slot];
    int ti = s->tgt_id ? rack_find(r, s->tgt_id) : RACK_NONE;
    if (!rack_can_target((module_type_t)s->type) || ti == RACK_NONE) return;
    int n = info[r->slot[ti].type].nparams;
    s->tgt_param = (uint8_t)((s->tgt_param + dir + n) % n);
    depth_reset(r, slot);
}

void rack_slot_name(const rack_t *r, int slot, char *buf, int n) {
    snprintf(buf, n, "%s%d", info[r->slot[slot].type].code, rack_instance(r, slot));
}

void rack_describe(const rack_t *r, int slot, char *buf, int n) {
    const rack_slot_t *s = &r->slot[slot];
    char me[8];
    rack_slot_name(r, slot, me, sizeof me);
    if (!rack_can_target((module_type_t)s->type)) { snprintf(buf, n, "%s %s", me, info[s->type].name); return; }
    if (s->type == MOD_OSC && !s->tgt_id) { snprintf(buf, n, "%s %s", me, info[s->type].name); return; }
    int ti = s->tgt_id ? rack_find(r, s->tgt_id) : RACK_NONE;
    if (ti == RACK_NONE) { snprintf(buf, n, "%s > (no target)", me); return; }
    char tg[8];
    rack_slot_name(r, ti, tg, sizeof tg);
    snprintf(buf, n, "%s > %s %s", me, tg, info[r->slot[ti].type].plong[s->tgt_param]);
}

/* ---------------- modulation depth units ---------------- */

static const depth_unit_t du_pitch = {"st",  -96, 96, 1,    12, 0};
static const depth_unit_t du_pct   = {"%",  -100, 100, 5,   50, 0};
static const depth_unit_t du_oct   = {"oct",  -8, 8, 0.25f, 2,  2};
static const depth_unit_t du_drive = {"oct",  -4, 4, 0.25f, 1,  2};
static const depth_unit_t du_plain = {"",     -2, 2, 0.05f, 1,  2};      // a target the mapper does not realise

const depth_unit_t *rack_depth_unit(module_type_t t, int param) {
    switch (t) {
        case MOD_OSC:     return param == 0 ? &du_pitch : &du_pct;     // (Mrph only exists on the engines: the mapper checks)
        case MOD_SAMPLER: return param == 0 ? &du_pitch : NULL;
        case MOD_FILTER:  return param == 0 ? &du_oct : NULL;
        case MOD_SAT:     return param == 0 ? &du_drive : NULL;
        default:          return NULL;
    }
}

int rack_depth_index(module_type_t t) {
    switch (t) {
        case MOD_LFO: return MP_LF_DEPTH;
        case MOD_ENV: return MP_EN_DEPTH;
        case MOD_OSC: return MP_OC_DEPTH;
        case MOD_EG:  return MP_EG_DEPTH;
        default:      return -1;
    }
}

static const depth_unit_t *unit_of(const rack_t *r, int slot) {
    const rack_slot_t *s = &r->slot[slot];
    int ti = s->tgt_id ? rack_find(r, s->tgt_id) : RACK_NONE;
    const depth_unit_t *u = ti == RACK_NONE ? NULL : rack_depth_unit((module_type_t)r->slot[ti].type, s->tgt_param);
    return u ? u : &du_plain;
}

static bool adjust_in(float *v, const depth_unit_t *u, int dir) {
    float nv = *v + (float)dir * u->step;
    if (nv < u->min) nv = u->min;
    if (nv > u->max) nv = u->max;
    if (nv > -u->step * 0.5f && nv < u->step * 0.5f) nv = 0;           // no float drift around zero
    if (nv == *v) return false;
    *v = nv;
    return true;
}

static void format_in(float v, const depth_unit_t *u, char *out, int n) { snprintf(out, n, "%+.*f%s", u->decimals, (double)v, u->unit); }

bool rack_depth_adjust(rack_t *r, int slot, int dir) {
    const int i = rack_depth_index((module_type_t)r->slot[slot].type);
    return i >= 0 && adjust_in(&r->slot[slot].v[i], unit_of(r, slot), dir);
}

void rack_depth_format(const rack_t *r, int slot, char *out, int n) {
    const int i = rack_depth_index((module_type_t)r->slot[slot].type);
    format_in(i >= 0 ? r->slot[slot].v[i] : 0, unit_of(r, slot), out, n);
}

static void depth_reset(rack_t *r, int slot) {
    const int i = rack_depth_index((module_type_t)r->slot[slot].type);
    if (i >= 0) r->slot[slot].v[i] = unit_of(r, slot)->def;
}

bool rack_ms_depth_adjust(const rack_t *r, int slot, ms_lane_t *l, int dir) {
    (void)slot;
    int ti = l->tgt_id ? rack_find(r, l->tgt_id) : RACK_NONE;
    const depth_unit_t *u = ti == RACK_NONE ? NULL : rack_depth_unit((module_type_t)r->slot[ti].type, l->tgt_param);
    return adjust_in(&l->depth, u ? u : &du_plain, dir);
}

void rack_ms_depth_format(const rack_t *r, ms_lane_t *l, char *out, int n) {
    int ti = l->tgt_id ? rack_find(r, l->tgt_id) : RACK_NONE;
    const depth_unit_t *u = ti == RACK_NONE ? NULL : rack_depth_unit((module_type_t)r->slot[ti].type, l->tgt_param);
    format_in(l->depth, u ? u : &du_plain, out, n);
}

/* ---------------- motion sequencer lanes ---------------- */

ms_pattern_t *rack_ms(rack_t *r, int slot) { return &r->ms[(int)r->slot[slot].v[MP_MS_POOL] % MS_POOL]; }
const ms_pattern_t *rack_ms_const(const rack_t *r, int slot) { return &r->ms[(int)r->slot[slot].v[MP_MS_POOL] % MS_POOL]; }

// What the engine mapper can realise (keep in step with rack_graph.cpp): oscillator pitch / level / PW, filter cutoff, saturation drive.
bool rack_ms_target_ok(module_type_t t, int param) {
    switch (t) {
        case MOD_OSC:    return param < 4;
        case MOD_SAMPLER: return param == 0;
        case MOD_FILTER: return param == 0;
        case MOD_SAT:    return param == 0;
        default:         return false;
    }
}

void rack_ms_cycle_target(rack_t *r, int slot, int lane, int dir) {
    ms_lane_t *l = &rack_ms(r, slot)->lane[lane];
    // candidates: 0 = none, then (module, parameter) pairs in slot order
    int cand[1 + RACK_MAX * 3][2], n = 1, cur = 0;
    cand[0][0] = 0; cand[0][1] = 0;
    for (int i = 0; i < r->count; i++) {
        if (i == slot) continue;
        for (int p = 0; p < info[r->slot[i].type].nparams; p++) {
            if (!rack_ms_target_ok((module_type_t)r->slot[i].type, p)) continue;
            cand[n][0] = r->slot[i].id; cand[n][1] = p;
            if (l->tgt_id == r->slot[i].id && l->tgt_param == p) cur = n;
            n++;
        }
    }
    cur = (cur + dir + n) % n;
    l->tgt_id = (uint8_t)cand[cur][0];
    l->tgt_param = (uint8_t)cand[cur][1];
    {
        const int ti = l->tgt_id ? rack_find(r, l->tgt_id) : RACK_NONE;
        const depth_unit_t *u = ti == RACK_NONE ? NULL : rack_depth_unit((module_type_t)r->slot[ti].type, l->tgt_param);
        l->depth = (u ? u : &du_plain)->def;
    }
}

void rack_ms_target_name(const rack_t *r, const ms_lane_t *l, char *buf, int n) {
    int ti = l->tgt_id ? rack_find(r, l->tgt_id) : RACK_NONE;
    if (ti == RACK_NONE) { snprintf(buf, n, "--"); return; }
    char nm[8];
    rack_slot_name(r, ti, nm, sizeof nm);
    snprintf(buf, n, "%s %s", nm, info[r->slot[ti].type].pshort[l->tgt_param]);
}

bool rack_ms_adjust_step(ms_lane_t *l, int step, int dir) {
    int v = l->val[step] + dir * 5;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    bool changed = v != l->val[step];
    l->val[step] = (uint8_t)v;
    return changed;
}

void rack_ms_toggle_step(ms_lane_t *l, int step) { l->active ^= (uint16_t)(1u << step); }

/* ---------------- module parameters ---------------- */

int rack_mparam_count(module_type_t t) { return info[t].nmp; }
const char *rack_mparam_label(module_type_t t, int i) { return info[t].mp[i].label; }

bool rack_mparam_adjust(rack_slot_t *s, int i, int dir) {
    const mp_t *d = &info[s->type].mp[i];
    float v = s->v[i], nv;
    if (d->kind == K_ENUM)     nv = v + (float)dir;
    else if (d->kind == K_LOG) nv = dir > 0 ? v * d->step : v / d->step;
    else                       nv = v + (float)dir * d->step;
    if (nv < d->min) nv = d->min;
    if (nv > d->max) nv = d->max;
    if (nv == v) return false;
    s->v[i] = nv;
    return true;
}

void rack_mparam_format(const rack_slot_t *s, int i, char *out, int n) {
    const mp_t *d = &info[s->type].mp[i];
    float v = s->v[i];
    if (d->kind == K_ENUM) { snprintf(out, n, "%s", d->names[(int)v]); return; }
    if (s->type == MOD_SAMPLER && i == MP_SM_SLICE && v < 0.5f) { snprintf(out, n, "All"); return; }
    if (d->unit[0] == 'H' && v >= 1000) snprintf(out, n, "%.1fkHz", v / 1000.0f);
    else snprintf(out, n, "%.*f%s", d->decimals, v, d->unit);
}
