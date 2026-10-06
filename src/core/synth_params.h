#pragma once
// Platform-independent description of the synth sound + a table that lets the
// UI edit any parameter generically. The platform audio layer renders it.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { WAVE_SINE, WAVE_PULSE, WAVE_SAW_DOWN, WAVE_SAW_UP, WAVE_TRIANGLE, WAVE_NOISE, WAVE_COUNT } wave_t;
typedef enum { FILT_OFF, FILT_LP, FILT_BP, FILT_HP, FILT_LP24, FILT_NOTCH, FILT_COUNT } filter_type_t;

typedef struct {
    float attack_ms;
    float decay_ms;
    float sustain;      // 0..1
    float release_ms;
    float hold_ms;      // time at the top between attack and decay
    float a_curve, d_curve, r_curve;   // -100..100 %: 0 = line, + = fast start / slow end, - = slow start / fast end
} env_params_t;

// The Strings synth type (ADR-037). Its amplitude envelope is amp_env (attack, decay, sustain, release; linear, the curves are not used).
typedef enum { STRW_SAW_, STRW_PULSE_, STRW_TRI_, STRW_COUNT } str_wave_t;
typedef struct {
    uint8_t wave;       // str_wave_t
    uint8_t osc;        // 0 = naive (cheapest, aliases), 1 = band-limited mipmap tables
    uint8_t lp_on;      // the per-voice one-pole lowpass
    uint8_t ftype;      // the shared filter after the voices: filter_type_t (Off = not built)
    float   detune;     // cents between the two oscillators
    float   mix;        // level of the second oscillator 0..1
    float   pw;         // pulse width 0.05..0.95
    float   level;      // voice level 0..1
    float   lp_cut;     // Hz at middle C with the envelope at 0
    float   lp_env;     // octaves added at full envelope
    float   lp_key;     // key tracking 0..1 (1 = the cutoff follows the note)
    float   fcut;       // shared filter cutoff, Hz
    float   fres;       // shared filter resonance (Q, as the rack's filter)
} str_params_t;

typedef struct {
    uint8_t      wave;          // wave_t
    float        pulse_width;   // 0.05..0.95, used by WAVE_PULSE
    float        volume;        // 0..2
    env_params_t amp_env;
    uint8_t      filter_type;   // filter_type_t
    float        cutoff_hz;
    float        resonance;     // ~Q
    float        filter_env_amt;
    env_params_t filter_env;
    str_params_t str;           // the Strings synth type
} synth_params_t;

// Every editable parameter.
typedef enum {
    P_WAVE, P_PULSE_WIDTH, P_VOLUME,
    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,
    P_FILTER_TYPE, P_CUTOFF, P_RESONANCE, P_FILTER_ENV_AMT,
    P_FENV_A, P_FENV_D, P_FENV_S, P_FENV_R,
    P_AMP_HOLD, P_AMP_ACV, P_AMP_DCV, P_AMP_RCV,
    P_STR_WAVE, P_STR_OSC, P_STR_DETUNE, P_STR_MIX, P_STR_PW, P_STR_LEVEL,
    P_STR_LP, P_STR_LPCUT, P_STR_LPENV, P_STR_LPKEY,
    P_STR_FTYPE, P_STR_FCUT, P_STR_FRES,
    P_COUNT
} param_id_t;

void synth_params_default(synth_params_t *p);

// Changes a parameter by one step (dir = +1 / -1), clamped. Returns true if it changed.
int  param_adjust(synth_params_t *p, param_id_t id, int dir);

// Short label ("Cut") and formatted value ("4.0kHz", "Saw") for display.
const char *param_label(param_id_t id);
void param_format(const synth_params_t *p, param_id_t id, char *out, size_t n);

#ifdef __cplusplus
}
#endif
