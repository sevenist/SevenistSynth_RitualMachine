#include "core/synth_params.h"
#include "core/fine_step.h"
#include "control/param.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool g_fine_step;

// A value of synth_params_t: its descriptor (framework/control/param.h; the Ritual Machine's text style; the defaults are
// synth_params_default's) and where it is. An enumeration is a byte, the others floats.
typedef struct {
    param_desc_t d;
    size_t       offset;        // into synth_params_t
} desc_t;

static const char *const wave_names[]   = {"Sine", "Pulse", "SawDn", "SawUp", "Tri", "Noise"};
static const char *const filter_names[] = {"Off", "LP", "BP", "HP", "LP24", "Notch", "LP6", "Ladr", "ChLP", "AP"};
static const char *const str_wave_names[] = {"Saw", "Pulse", "Tri"};
static const char *const str_osc_names[]  = {"Naive", "Mip"};
static const char *const off_on_names[]   = {"Off", "On"};

#define F(field) offsetof(synth_params_t, field)

static const desc_t table[P_COUNT] = {
    [P_WAVE]           = {{"Wav", 0, WAVE_COUNT - 1, 0, "", PARAM_ENUM, wave_names, 1, 0, PARAM_TEXT_FIXED}, F(wave)},
    [P_PULSE_WIDTH]    = {{"PW", 0.05f, 0.95f, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED}, F(pulse_width)},
    [P_VOLUME]         = {{"Vol", 0.0f, 2.0f, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED}, F(volume)},
    [P_AMP_A]          = {{"Atk", 1, 5000, 0, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED}, F(amp_env.attack_ms)},
    [P_AMP_D]          = {{"Dec", 1, 5000, 0, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED}, F(amp_env.decay_ms)},
    [P_AMP_S]          = {{"Sus", 0, 1, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED}, F(amp_env.sustain)},
    [P_AMP_R]          = {{"Rel", 1, 5000, 0, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED}, F(amp_env.release_ms)},
    [P_FILTER_TYPE]    = {{"Typ", 0, FILT_COUNT - 1, 0, "", PARAM_ENUM, filter_names, 1, 0, PARAM_TEXT_FIXED}, F(filter_type)},
    [P_CUTOFF]         = {{"Cut", 20, 18000, 0, "Hz", PARAM_LOG, NULL, 1.12f, 0, PARAM_TEXT_FIXED}, F(cutoff_hz)},
    [P_RESONANCE]      = {{"Res", 0.5f, 10, 0, "", PARAM_LIN, NULL, 0.1f, 1, PARAM_TEXT_FIXED}, F(resonance)},
    [P_FILTER_ENV_AMT] = {{"Env", 0, 8, 0, "", PARAM_LIN, NULL, 0.25f, 2, PARAM_TEXT_FIXED}, F(filter_env_amt)},
    [P_FENV_A]         = {{"Atk", 1, 5000, 0, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED}, F(filter_env.attack_ms)},
    [P_FENV_D]         = {{"Dec", 1, 5000, 0, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED}, F(filter_env.decay_ms)},
    [P_FENV_S]         = {{"Sus", 0, 1, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED}, F(filter_env.sustain)},
    [P_FENV_R]         = {{"Rel", 1, 5000, 0, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED}, F(filter_env.release_ms)},
    [P_AMP_HOLD]       = {{"Hld", 0, 2000, 0, "ms", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED}, F(amp_env.hold_ms)},
    [P_AMP_ACV]        = {{"ACv", -100, 100, 0, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED}, F(amp_env.a_curve)},
    [P_AMP_DCV]        = {{"DCv", -100, 100, 0, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED}, F(amp_env.d_curve)},
    [P_AMP_RCV]        = {{"RCv", -100, 100, 0, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED}, F(amp_env.r_curve)},
    [P_STR_WAVE]       = {{"Wav", 0, STRW_COUNT - 1, 0, "", PARAM_ENUM, str_wave_names, 1, 0, PARAM_TEXT_FIXED}, F(str.wave)},
    [P_STR_OSC]        = {{"Osc", 0, 1, 0, "", PARAM_ENUM, str_osc_names, 1, 0, PARAM_TEXT_FIXED}, F(str.osc)},
    [P_STR_DETUNE]     = {{"Det", 0, 100, 0, "c", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED}, F(str.detune)},
    [P_STR_MIX]        = {{"Mix", 0, 1, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED}, F(str.mix)},
    [P_STR_PW]         = {{"PW", 0.05f, 0.95f, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED}, F(str.pw)},
    [P_STR_LEVEL]      = {{"Lvl", 0, 1, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED}, F(str.level)},
    [P_STR_LP]         = {{"LP", 0, 1, 0, "", PARAM_ENUM, off_on_names, 1, 0, PARAM_TEXT_FIXED}, F(str.lp_on)},
    [P_STR_LPCUT]      = {{"Cut", 50, 18000, 0, "Hz", PARAM_LOG, NULL, 1.12f, 0, PARAM_TEXT_FIXED}, F(str.lp_cut)},
    [P_STR_LPENV]      = {{"Env", 0, 8, 0, "oct", PARAM_LIN, NULL, 0.25f, 2, PARAM_TEXT_FIXED}, F(str.lp_env)},
    [P_STR_LPKEY]      = {{"Key", 0, 1, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED}, F(str.lp_key)},
    [P_STR_FTYPE]      = {{"Typ", 0, FILT_COUNT - 1, 0, "", PARAM_ENUM, filter_names, 1, 0, PARAM_TEXT_FIXED}, F(str.ftype)},
    [P_STR_FCUT]       = {{"Cut", 20, 18000, 0, "Hz", PARAM_LOG, NULL, 1.12f, 0, PARAM_TEXT_FIXED}, F(str.fcut)},
    [P_STR_FRES]       = {{"Res", 0.5f, 10, 0, "", PARAM_LIN, NULL, 0.1f, 1, PARAM_TEXT_FIXED}, F(str.fres)},
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

bool param_reset(synth_params_t *p, param_id_t id) {
    synth_params_t d;
    synth_params_default(&d);
    const desc_t *t = &table[id];
    const size_t n = t->d.curve == PARAM_ENUM ? 1 : sizeof(float);             // (an enumeration is a byte: param_adjust)
    uint8_t *v = (uint8_t *)p + t->offset;
    if (!memcmp(v, (const uint8_t *)&d + t->offset, n)) return false;
    memcpy(v, (const uint8_t *)&d + t->offset, n);
    return true;
}

// The value of a parameter (an enumeration's byte as a float), and its store (false: it was already that value).
static float get(const synth_params_t *p, const desc_t *t) {
    const uint8_t *base = (const uint8_t *)p + t->offset;
    return t->d.curve == PARAM_ENUM ? (float)*base : *(const float *)base;
}
static bool put(synth_params_t *p, const desc_t *t, float v) {
    if (v == get(p, t)) return false;
    uint8_t *base = (uint8_t *)p + t->offset;
    if (t->d.curve == PARAM_ENUM) *base = (uint8_t)v;
    else *(float *)base = v;
    return true;
}

int param_adjust(synth_params_t *p, param_id_t id, int dir) {
    const desc_t *t = &table[id];
    return put(p, t, pdesc_step(&t->d, get(p, t), dir, g_fine_step)) ? 1 : 0;     // (an enumeration keeps its steps)
}

// Continuous access (knobs): a linear value as 0..1 of min..max, a logarithmic one as 0..1 of log(min)..log(max). Set values are rounded to the
// decimals the parameter shows.
bool param_is_continuous(param_id_t id) { return pdesc_is_continuous(&table[id].d); }

float param_norm(const synth_params_t *p, param_id_t id) {
    const desc_t *t = &table[id];
    return pdesc_is_continuous(&t->d) ? pdesc_to_norm(&t->d, get(p, t)) : 0.0f;
}

bool param_set_norm(synth_params_t *p, param_id_t id, float n) {
    const desc_t *t = &table[id];
    return pdesc_is_continuous(&t->d) && put(p, t, pdesc_from_norm(&t->d, n));     // (to the precision shown: whole units stay whole)
}

const char *param_label(param_id_t id) { return table[id].d.name; }

void param_format(const synth_params_t *p, param_id_t id, char *out, size_t n) {
    const desc_t *t = &table[id];
    pdesc_format(&t->d, get(p, t), out, n);
}
