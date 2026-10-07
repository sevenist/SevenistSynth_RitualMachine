#include "core/fxrack.h"
#include "core/fine_step.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef enum { PK_PCT, PK_SPCT, PK_MS, PK_DB, PK_ENUM, PK_RATE, PK_NOTE, PK_OCT4, PK_TAPS, PK_SEMI } fxp_kind_t;

typedef struct {
    const char *label;
    fxp_kind_t  kind;
    int16_t     min, max, step, def;
    const char *const *names;           // PK_ENUM
} fxp_t;

typedef struct {
    const char *name, *code;
    int n;
    fxp_t p[FXR_PARAMS];
} fxd_t;

static const char *const drive_modes[] = {"Tanh", "Clip", "Fold", "Crush", "Tube", "Tape", "Diode", "Cheb", "Rect", "Decim"};
static const char *const chorus_modes[] = {"Off", "I", "II", "I+II"};
static const char *const trem_shapes[]  = {"Sine", "Tri", "Square"};
static const char *const trem_modes[]   = {"Trem", "Pan"};
static const char *const shift_modes[]  = {"Ring", "Up", "Down"};
static const char *const off_on[]       = {"Off", "On"};
static const char *const cab_names[]    = {"1x12", "4x12", "Bright", "Dark", "Acoust", "Violin", "Drum", "Phone"};
static const char *const spec_modes[]   = {"Thru", "Freeze", "Gate", "Robot", "Whisp", "Pitch"};

// PK_RATE: hundredths of Hz (5 = 0.05 Hz, 2000 = 20 Hz; for the shifter up to 5000 Hz); PK_NOTE: a MIDI note shown as a frequency;
// PK_OCT4: quarter octaves of gain (16 = 4 octaves); PK_TAPS: convolution length.
static const fxd_t defs[FX_TYPE_COUNT] = {
    [FX_NONE]    = {"None",       "--", 0, {{0}}},
    [FX_DRIVE]   = {"Drive",      "DR", 4, {{"Mod", PK_ENUM, 0, 9, 1, 0, drive_modes}, {"Drv", PK_OCT4, 0, 16, 1, 8, 0}, {"Mix", PK_PCT, 0, 100, 5, 100, 0}, {"Bit", PK_TAPS, 1, 15, 1, 8, 0}}},
    [FX_CHORUS]  = {"Chorus",     "CH", 2, {{"Mode", PK_ENUM, 0, 3, 1, 0, chorus_modes}, {"Mix", PK_PCT, 0, 100, 5, 73, 0}}},
    [FX_PHASER]  = {"Phaser",     "PH", 4, {{"Rate", PK_RATE, 5, 2000, 1, 40, 0}, {"Dpth", PK_PCT, 0, 100, 5, 60, 0}, {"Fb", PK_SPCT, -95, 95, 5, 40, 0}, {"Mix", PK_PCT, 0, 100, 5, 50, 0}}},
    [FX_FLANGER] = {"Flanger",    "FL", 4, {{"Rate", PK_RATE, 5, 2000, 1, 30, 0}, {"Dpth", PK_PCT, 0, 100, 5, 60, 0}, {"Fb", PK_SPCT, -95, 95, 5, 40, 0}, {"Mix", PK_PCT, 0, 100, 5, 60, 0}}},
    [FX_TREMOLO] = {"Trem / Pan", "TR", 4, {{"Rate", PK_RATE, 5, 2000, 1, 400, 0}, {"Dpth", PK_PCT, 0, 100, 5, 60, 0}, {"Shp", PK_ENUM, 0, 2, 1, 0, trem_shapes}, {"Mode", PK_ENUM, 0, 1, 1, 0, trem_modes}}},
    [FX_COMP]    = {"Compressor", "CP", 4, {{"Thr", PK_DB, -60, 0, 2, -18, 0}, {"Rat", PK_TAPS, 1, 20, 1, 4, 0}, {"Rel", PK_MS, 10, 1000, 10, 150, 0}, {"Gain", PK_DB, 0, 24, 1, 0, 0}}},
    [FX_EQ]      = {"EQ 3-band",  "EQ", 4, {{"Low", PK_DB, -15, 15, 1, 0, 0}, {"Mid", PK_DB, -15, 15, 1, 0, 0}, {"MidF", PK_NOTE, 36, 120, 1, 84, 0}, {"High", PK_DB, -15, 15, 1, 0, 0}}},
    [FX_SHIFT]   = {"Ring / Shift", "RS", 3, {{"Mode", PK_ENUM, 0, 2, 1, 0, shift_modes}, {"Freq", PK_RATE, 100, 5000, 1, 440, 0}, {"Mix", PK_PCT, 0, 100, 5, 50, 0}}},
    [FX_DELAY]   = {"Delay",      "DL", 4, {{"Time", PK_MS, 20, 1000, 10, 350, 0}, {"Fb", PK_PCT, 0, 95, 5, 40, 0}, {"Mix", PK_PCT, 0, 100, 5, 0, 0}, {"Pong", PK_ENUM, 0, 1, 1, 0, off_on}}},
    [FX_REVERB]  = {"Reverb",     "RV", 4, {{"Mix", PK_PCT, 0, 100, 5, 0, 0}, {"Dec", PK_PCT, 0, 98, 2, 60, 0}, {"Size", PK_PCT, 0, 100, 5, 60, 0}, {"Damp", PK_PCT, 0, 100, 5, 50, 0}}},
    [FX_CAB]     = {"Cab / Body", "CB", 4, {{"IR", PK_ENUM, 0, 7, 1, 0, cab_names}, {"Len", PK_TAPS, 64, 512, 32, 256, 0}, {"Mix", PK_PCT, 0, 100, 5, 100, 0}, {"Lvl", PK_PCT, 0, 100, 5, 50, 0}}},
    [FX_ENSEMBLE] = {"Ensemble",  "EN", 4, {{"Rate", PK_RATE, 5, 500, 1, 60, 0}, {"Dpth", PK_PCT, 0, 100, 5, 70, 0}, {"Shim", PK_PCT, 0, 100, 5, 35, 0}, {"Mix", PK_PCT, 0, 100, 5, 70, 0}}},
    // Spectral: Shft = pitch (Pitch, Freeze), Amt = the Gate threshold (% of the frame peak); behind the cog: Hold (Freeze holds), the Gate band Lo / Hi
    [FX_SPECTRAL] = {"Spectral",  "SP", 7, {{"Mode", PK_ENUM, 0, 5, 1, 0, spec_modes}, {"Shft", PK_SEMI, -24, 24, 1, 0, 0}, {"Amt", PK_PCT, 0, 100, 1, 12, 0},
                                             {"Mix", PK_PCT, 0, 100, 5, 100, 0}, {"Hold", PK_ENUM, 0, 1, 1, 0, off_on}, {"Lo", PK_NOTE, 0, 135, 1, 20, 0},
                                             {"Hi", PK_NOTE, 0, 135, 1, 135, 0}}},
};

static const fxp_t *param(int type, int i) { return &defs[type].p[i]; }

void fxr_set_type(fx_slot_t *s, int type) {
    if (type < 0 || type >= FX_TYPE_COUNT) type = FX_NONE;
    s->type = (uint8_t)type;
    for (int i = 0; i < FXR_PARAMS; i++) s->v[i] = i < defs[type].n ? param(type, i)->def : 0;
}

void fxr_init(fxrack_t *r) {
    memset(r, 0, sizeof *r);
    fxr_set_type(&r->slot[0], FX_CHORUS);
    fxr_set_type(&r->slot[1], FX_DELAY);
    fxr_set_type(&r->slot[2], FX_REVERB);
    fxr_set_type(&r->slot[3], FX_NONE);
}

bool fxr_cycle_type(fx_slot_t *s, int dir) {
    int t = s->type + dir;
    if (t < 0 || t >= FX_TYPE_COUNT) return false;
    fxr_set_type(s, t);
    return true;
}

const char *fxr_type_name(int type) { return defs[type].name; }
const char *fxr_type_code(int type) { return defs[type].code; }
int fxr_param_count(int type) { return defs[type].n; }
const char *fxr_label(int type, int i) { return i < defs[type].n ? param(type, i)->label : ""; }

bool fxr_adjust(fx_slot_t *s, int i, int dir) {
    if (i < 0 || i >= defs[s->type].n) return false;
    const fxp_t *p = param(s->type, i);
    int v = s->v[i], nv;
    if (p->kind == PK_RATE) nv = dir > 0 ? (v + 1 > v * 6 / 5 ? v + 1 : v * 6 / 5) : (v - 1 < v * 5 / 6 ? v - 1 : v * 5 / 6);   // 20 % steps
    else nv = v + dir * fine_int_step(p->step);
    if (nv < p->min) nv = p->min;
    if (nv > p->max) nv = p->max;
    if (nv == v) return false;
    s->v[i] = (int16_t)nv;
    return true;
}

void fxr_format(const fx_slot_t *s, int i, char *out, size_t n) {
    if (i < 0 || i >= defs[s->type].n) { out[0] = 0; return; }
    const fxp_t *p = param(s->type, i);
    const int v = s->v[i];
    switch (p->kind) {
        case PK_ENUM: snprintf(out, n, "%s", p->names[v]); break;
        case PK_PCT:  snprintf(out, n, "%d%%", v); break;
        case PK_SPCT: snprintf(out, n, "%+d%%", v); break;
        case PK_MS:   snprintf(out, n, "%dms", v); break;
        case PK_DB:   snprintf(out, n, "%+ddB", v); break;
        case PK_RATE: if (v >= 1000) snprintf(out, n, "%.1fkHz", v / 100000.0); else if (v >= 100) snprintf(out, n, "%.0fHz", v / 100.0); else snprintf(out, n, "%.2fHz", v / 100.0); break;
        case PK_NOTE: {
            const double hz = 440.0 * pow(2.0, (v - 69) / 12.0);
            if (hz >= 1000) snprintf(out, n, "%.1fkHz", hz / 1000.0); else snprintf(out, n, "%.0fHz", hz);
            break;
        }
        case PK_OCT4: snprintf(out, n, "%.2foct", v / 4.0); break;
        case PK_TAPS: snprintf(out, n, "%d", v); break;
        case PK_SEMI: snprintf(out, n, "%+dst", v); break;
    }
}
