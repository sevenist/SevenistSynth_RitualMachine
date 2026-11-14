#pragma once
// Fine steps (ACT_VALUE_FINE, user 2026-10-07): while g_fine_step is set, one step of a linear parameter is 1 / FINE_STEP_DIV of its normal
// step, but never finer than the decimals it shows (semitones, cents, ms stay whole: 5 % -> 1 %, 1 st stays 1 st), and one step of a
// logarithmic parameter multiplies by the FINE_STEP_DIV-th root of its factor (synth_params.c, rack.c).
// Enumerations, FM values and the sequencer keep their steps. The app sets it around one fine action only (app/app_actions.c).
#include <stdbool.h>
#include <math.h>
#include "control/param.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FINE_STEP_DIV PARAM_FINE_DIV
extern bool g_fine_step;

// (the formulas are the library's: framework/control/param.h, pdesc_fine_lin / pdesc_fine_log; the tables' values step through pdesc_step)
static inline float fine_lin_step(float step, int decimals) { return g_fine_step ? pdesc_fine_lin(step, decimals) : step; }
static inline float fine_log_step(float factor) { return g_fine_step ? pdesc_fine_log(factor) : factor; }
static inline int   fine_int_step(int step) { return g_fine_step ? (step / FINE_STEP_DIV > 0 ? step / FINE_STEP_DIV : 1) : step; }

#ifdef __cplusplus
}
#endif
