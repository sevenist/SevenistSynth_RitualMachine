#include "core/synth_params.h"
#include <stdio.h>

typedef enum { KIND_ENUM, KIND_LIN, KIND_LOG } kind_t;

typedef struct {
    const char *label;
    kind_t      kind;
    size_t      offset;         // into synth_params_t
    float       min, max;
    float       step;           // KIND_LIN: added; KIND_LOG: multiplied
    const char *unit;           // "ms", "Hz" (auto kHz), "" ...
    uint8_t     decimals;
    const char *const *names;   // KIND_ENUM
} desc_t;

static const char *const wave_names[]   = {"Sine", "Pulse", "SawDn", "SawUp", "Tri", "Noise"};
static const char *const filter_names[] = {"Off", "LP", "BP", "HP", "LP24", "Notch"};
static const char *const str_wave_names[] = {"Saw", "Pulse", "Tri"};
static const char *const str_osc_names[]  = {"Naive", "Mip"};
static const char *const off_on_names[]   = {"Off", "On"};

#define F(field) offsetof(synth_params_t, field)

static const desc_t table[P_COUNT] = {
    [P_WAVE]           = {"Wav", KIND_ENUM, F(wave),                  0, WAVE_COUNT - 1, 1, "", 0, wave_names},
    [P_PULSE_WIDTH]    = {"PW",  KIND_LIN,  F(pulse_width),           0.05f, 0.95f, 0.05f, "", 2, 0},
    [P_VOLUME]         = {"Vol", KIND_LIN,  F(volume),                0.0f, 2.0f, 0.05f, "", 2, 0},
    [P_AMP_A]          = {"Atk", KIND_LOG,  F(amp_env.attack_ms),     1, 5000, 1.2f, "ms", 0, 0},
    [P_AMP_D]          = {"Dec", KIND_LOG,  F(amp_env.decay_ms),      1, 5000, 1.2f, "ms", 0, 0},
    [P_AMP_S]          = {"Sus", KIND_LIN,  F(amp_env.sustain),       0, 1, 0.05f, "", 2, 0},
    [P_AMP_R]          = {"Rel", KIND_LOG,  F(amp_env.release_ms),    1, 5000, 1.2f, "ms", 0, 0},
    [P_FILTER_TYPE]    = {"Typ", KIND_ENUM, F(filter_type),           0, FILT_COUNT - 1, 1, "", 0, filter_names},
    [P_CUTOFF]         = {"Cut", KIND_LOG,  F(cutoff_hz),             20, 18000, 1.12f, "Hz", 0, 0},
    [P_RESONANCE]      = {"Res", KIND_LIN,  F(resonance),             0.5f, 10, 0.1f, "", 1, 0},
    [P_FILTER_ENV_AMT] = {"Env", KIND_LIN,  F(filter_env_amt),        0, 8, 0.25f, "", 2, 0},
    [P_FENV_A]         = {"Atk", KIND_LOG,  F(filter_env.attack_ms),  1, 5000, 1.2f, "ms", 0, 0},
    [P_FENV_D]         = {"Dec", KIND_LOG,  F(filter_env.decay_ms),   1, 5000, 1.2f, "ms", 0, 0},
    [P_FENV_S]         = {"Sus", KIND_LIN,  F(filter_env.sustain),    0, 1, 0.05f, "", 2, 0},
    [P_FENV_R]         = {"Rel", KIND_LOG,  F(filter_env.release_ms), 1, 5000, 1.2f, "ms", 0, 0},
    [P_AMP_HOLD]       = {"Hld", KIND_LIN,  F(amp_env.hold_ms),       0, 2000, 5, "ms", 0, 0},
    [P_AMP_ACV]        = {"ACv", KIND_LIN,  F(amp_env.a_curve),       -100, 100, 5, "%", 0, 0},
    [P_AMP_DCV]        = {"DCv", KIND_LIN,  F(amp_env.d_curve),       -100, 100, 5, "%", 0, 0},
    [P_AMP_RCV]        = {"RCv", KIND_LIN,  F(amp_env.r_curve),       -100, 100, 5, "%", 0, 0},
    [P_STR_WAVE]       = {"Wav", KIND_ENUM, F(str.wave),              0, STRW_COUNT - 1, 1, "", 0, str_wave_names},
    [P_STR_OSC]        = {"Osc", KIND_ENUM, F(str.osc),               0, 1, 1, "", 0, str_osc_names},
    [P_STR_DETUNE]     = {"Det", KIND_LIN,  F(str.detune),            0, 100, 1, "c", 0, 0},
    [P_STR_MIX]        = {"Mix", KIND_LIN,  F(str.mix),               0, 1, 0.05f, "", 2, 0},
    [P_STR_PW]         = {"PW",  KIND_LIN,  F(str.pw),                0.05f, 0.95f, 0.05f, "", 2, 0},
    [P_STR_LEVEL]      = {"Lvl", KIND_LIN,  F(str.level),             0, 1, 0.05f, "", 2, 0},
    [P_STR_LP]         = {"LP",  KIND_ENUM, F(str.lp_on),             0, 1, 1, "", 0, off_on_names},
    [P_STR_LPCUT]      = {"Cut", KIND_LOG,  F(str.lp_cut),            50, 18000, 1.12f, "Hz", 0, 0},
    [P_STR_LPENV]      = {"Env", KIND_LIN,  F(str.lp_env),            0, 8, 0.25f, "oct", 2, 0},
    [P_STR_LPKEY]      = {"Key", KIND_LIN,  F(str.lp_key),            0, 1, 0.05f, "", 2, 0},
    [P_STR_FTYPE]      = {"Typ", KIND_ENUM, F(str.ftype),             0, FILT_COUNT - 1, 1, "", 0, filter_names},
    [P_STR_FCUT]       = {"Cut", KIND_LOG,  F(str.fcut),              20, 18000, 1.12f, "Hz", 0, 0},
    [P_STR_FRES]       = {"Res", KIND_LIN,  F(str.fres),              0.5f, 10, 0.1f, "", 1, 0},
};

void synth_params_default(synth_params_t *p) {
    p->wave = WAVE_SAW_DOWN;
    p->pulse_width = 0.5f;
    p->volume = 1.0f;
    p->amp_env    = (env_params_t){5, 200, 0.7f, 200, 0, 55, 60, 60};
    p->filter_type = FILT_LP;
    p->cutoff_hz = 4000;
    p->resonance = 0.7f;
    p->filter_env_amt = 0;
    p->filter_env = (env_params_t){5, 300, 0.0f, 200, 0, 55, 60, 60};
    p->str = (str_params_t){STRW_SAW_, 1, 0, FILT_LP, 12, 1.0f, 0.5f, 0.5f, 1500, 2.0f, 0.5f, 6000, 0.7f};
}

int param_adjust(synth_params_t *p, param_id_t id, int dir) {
    const desc_t *d = &table[id];
    uint8_t *base = (uint8_t *)p + d->offset;
    if (d->kind == KIND_ENUM) {
        int v = *base, nv = v + dir;
        if (nv < (int)d->min || nv > (int)d->max) return 0;
        *base = (uint8_t)nv;
        return 1;
    }
    float *f = (float *)base, v = *f, nv;
    nv = d->kind == KIND_LOG ? (dir > 0 ? v * d->step : v / d->step) : v + dir * d->step;
    if (nv < d->min) nv = d->min;
    if (nv > d->max) nv = d->max;
    if (nv == v) return 0;
    *f = nv;
    return 1;
}

const char *param_label(param_id_t id) { return table[id].label; }

void param_format(const synth_params_t *p, param_id_t id, char *out, size_t n) {
    const desc_t *d = &table[id];
    const uint8_t *base = (const uint8_t *)p + d->offset;
    if (d->kind == KIND_ENUM) { snprintf(out, n, "%s", d->names[*base]); return; }
    float v = *(const float *)base;
    if (d->unit[0] == 'H' && v >= 1000) snprintf(out, n, "%.1fkHz", v / 1000.0f);
    else                                snprintf(out, n, "%.*f%s", d->decimals, v, d->unit);
}
