#pragma once
// Fine steps (ACT_VALUE_FINE, user 2026-10-07): while g_fine_step is set, one step of a linear parameter is 1 / FINE_STEP_DIV of its normal
// step, but never finer than the decimals it shows (semitones, cents, ms stay whole: 5 % -> 1 %, 1 st stays 1 st), and one step of a
// logarithmic parameter multiplies by the FINE_STEP_DIV-th root of its factor (synth_params.c, rack.c, fxrack.c).
// Enumerations, FM values and the sequencer keep their steps. The app sets it around one fine action only (app.c).
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FINE_STEP_DIV 5
extern bool g_fine_step;

static inline float fine_lin_step(float step, int decimals) {
    if (!g_fine_step) return step;
    const float f = step / FINE_STEP_DIV, shown = powf(10.0f, (float)-decimals);
    return f > shown ? f : (shown < step ? shown : step);
}
static inline float fine_log_step(float factor) { return g_fine_step ? powf(factor, 1.0f / FINE_STEP_DIV) : factor; }
static inline int   fine_int_step(int step) { return g_fine_step ? (step / FINE_STEP_DIV > 0 ? step / FINE_STEP_DIV : 1) : step; }

#ifdef __cplusplus
}
#endif
