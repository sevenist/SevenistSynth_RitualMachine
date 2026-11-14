#pragma once
// A parameter as a control sees it: its range, how it moves, how it is written. One descriptor for every app's values (the Ritual Machine's
// sound, rack modules and sequencer; SynthLab's synths, which SynthLab Studio edits; TechnoGen's track values), and the functions that
// move, place and write a value from it (pdesc_*). Portable C (header only: static inline), usable from C++.
//
//   static const param_desc_t cutoff = {"Cutoff", 40, 16000, 1200, "Hz", PARAM_LOG};        // the fields after `curve` are optional
//
// curve:
//   PARAM_LIN    a float, moved by `step` (added)
//   PARAM_LOG    a float on a ratio scale (Hz, ms: min > 0), moved by `step` (multiplied); its 0..1 position is along its logarithm
//   PARAM_INT    a whole number, moved by `step` (default 1); `labels` may name min..max
//   PARAM_ENUM   a choice among `labels` (min..max); not continuous: a knob does not sweep it
//   PARAM_HEAD   not a value: a section's title in a list (`name` is the title)
// step: pdesc_step's step. 0: a hundredth of the range (LIN), a hundredth of the range's ratio (LOG), 1 (INT, ENUM).
// The text (pdesc_format), in one of two styles, each an app's own:
//   PARAM_TEXT_AUTO   (0, SynthLab's) as many digits as fit, kilo as "k", ms as s from 1000: "1.20 s", "4.00k Hz", "12.5 Hz", "3 st"
//   PARAM_TEXT_FIXED  (the Ritual Machine's) `decimals` digits, the unit stuck to the number, Hz as kHz from 1000: "350ms", "4.0kHz"
// A value set from a 0..1 position is rounded to the precision the text shows (FIXED: `decimals`; INT and ENUM: whole).
// Fine steps (the Ritual Machine's ACT_VALUE_FINE): one step of a LIN value is a fifth of its step but never finer than its decimals
// show, one step of a LOG value multiplies by the fifth root of its factor; INT and ENUM keep their steps.
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PARAM_LIN, PARAM_LOG, PARAM_INT, PARAM_HEAD, PARAM_ENUM } param_curve_t;
typedef enum { PARAM_TEXT_AUTO, PARAM_TEXT_FIXED } param_text_t;

// (C++: the fields after `def` have default values, so a short initializer is complete; in C they are zero, the same.)
#ifdef __cplusplus
#define PARAM_DEFAULT(x) = x
#else
#define PARAM_DEFAULT(x)
#endif
typedef struct {
    const char *name;                                   // what the screen shows beside the value ("Cut", "Cutoff")
    float min, max, def;
    const char *unit PARAM_DEFAULT(NULL);               // written after the value ("Hz", "ms", "%"); NULL or "": none
    uint8_t curve PARAM_DEFAULT(PARAM_LIN);             // param_curve_t
    const char *const *labels PARAM_DEFAULT(NULL);      // INT (optional), ENUM: the names of min .. max
    float step PARAM_DEFAULT(0.0f);                     // pdesc_step: added (LIN, INT), multiplied (LOG); 0: the default (above)
    uint8_t decimals PARAM_DEFAULT(0);                  // PARAM_TEXT_FIXED: the digits after the point
    uint8_t text PARAM_DEFAULT(PARAM_TEXT_AUTO);        // param_text_t
} param_desc_t;

#define PARAM_FINE_DIV 5                // a fine step: a fifth of a step

static inline float pdesc_clamp(const param_desc_t *d, float v) { return v < d->min ? d->min : v > d->max ? d->max : v; }
static inline bool pdesc_is_value(const param_desc_t *d) { return d->curve != PARAM_HEAD; }
// A knob can sweep it: every value but a choice (and a title).
static inline bool pdesc_is_continuous(const param_desc_t *d) { return d->curve != PARAM_ENUM && d->curve != PARAM_HEAD; }
// How many values it has: INT and ENUM, max - min + 1; the others 0.
static inline int pdesc_choices(const param_desc_t *d) {
    return d->curve == PARAM_INT || d->curve == PARAM_ENUM ? (int)(d->max - d->min) + 1 : 0;
}
static inline bool pdesc_log_scale(const param_desc_t *d) { return d->curve == PARAM_LOG && d->min > 0.0f && d->max > d->min; }

// The fine step of a LIN step (never finer than the `decimals` shown, nor coarser than the step) and of a LOG factor.
static inline float pdesc_fine_lin(float step, int decimals) {
    const float f = step / PARAM_FINE_DIV, shown = powf(10.0f, (float)-decimals);
    return f > shown ? f : (shown < step ? shown : step);
}
static inline float pdesc_fine_log(float factor) { return powf(factor, 1.0f / PARAM_FINE_DIV); }

// The value `dir` steps away (+ up, - down; fine: fine steps), clamped to the range.
static inline float pdesc_step(const param_desc_t *d, float v, int dir, bool fine) {
    float r = v;
    switch (d->curve) {
    case PARAM_LOG: {
        float st = d->step > 0.0f ? d->step : (pdesc_log_scale(d) ? powf(d->max / d->min, 0.01f) : 1.0f);
        if (fine) st = pdesc_fine_log(st);
        for (int k = 0; k < dir; k++) r *= st;
        for (int k = 0; k > dir; k--) r /= st;
        break;
    }
    case PARAM_INT: case PARAM_ENUM:
        r = v + (float)dir * (d->step > 0.0f ? d->step : 1.0f);
        break;
    case PARAM_HEAD:
        return v;
    default: {
        float st = d->step > 0.0f ? d->step : (d->max - d->min) / 100.0f;
        if (fine) st = pdesc_fine_lin(st, d->decimals);
        r = v + (float)dir * st;
    }
    }
    return pdesc_clamp(d, r);
}

// The value `detents` of an encoder away, the range in 100 detents (SynthLab's encoder B): LIN in equal steps, snapped to them; LOG by
// equal ratios; INT by 1. Clamped.
static inline float pdesc_detents(const param_desc_t *d, float v, int detents) {
    float r;
    if (d->curve == PARAM_HEAD) return v;
    if (d->curve == PARAM_INT || d->curve == PARAM_ENUM) r = floorf(v + 0.5f) + (float)detents;
    else if (d->curve == PARAM_LOG && d->min > 0.0f) r = v * powf(d->max / d->min, (float)detents / 100.0f);
    else {                                                  // (LIN, and a LOG range from 0)
        const float step = (d->max - d->min) / 100.0f;
        r = d->min + step * floorf((v - d->min) / step + 0.5f + (float)detents);
    }
    return pdesc_clamp(d, r);
}

// Its 0..1 position along the range (LOG: along its logarithm).
static inline float pdesc_to_norm(const param_desc_t *d, float v) {
    if (d->max <= d->min) return 0.0f;
    const float n = pdesc_log_scale(d) ? logf(v / d->min) / logf(d->max / d->min) : (v - d->min) / (d->max - d->min);
    return n < 0.0f ? 0.0f : n > 1.0f ? 1.0f : n;
}

// The value at a 0..1 position, rounded to the precision its text shows, clamped.
static inline float pdesc_from_norm(const param_desc_t *d, float n) {
    n = n < 0.0f ? 0.0f : n > 1.0f ? 1.0f : n;
    float v = pdesc_log_scale(d) ? d->min * powf(d->max / d->min, n) : d->min + n * (d->max - d->min);
    if (d->curve == PARAM_INT || d->curve == PARAM_ENUM) v = floorf(v + 0.5f);
    else if (d->text == PARAM_TEXT_FIXED) {
        const float q = powf(10.0f, (float)d->decimals);
        v = roundf(v * q) / q;
    }
    return pdesc_clamp(d, v);
}

// The value as text, in the descriptor's style (above).
static inline void pdesc_format(const param_desc_t *d, float v, char *out, size_t size) {
    const char *unit = d->unit && d->unit[0] ? d->unit : NULL;
    if (d->curve == PARAM_ENUM || (d->curve == PARAM_INT && d->labels)) {
        const int i = (int)floorf(v + 0.5f) - (int)d->min;
        snprintf(out, size, "%s", d->labels && i >= 0 && i <= (int)d->max - (int)d->min ? d->labels[i] : "?");
        return;
    }
    if (d->text == PARAM_TEXT_FIXED) {
        if (unit && unit[0] == 'H' && v >= 1000) snprintf(out, size, "%.1fkHz", v / 1000.0f);
        else snprintf(out, size, "%.*f%s", d->curve == PARAM_INT ? 0 : d->decimals, v, unit ? unit : "");
        return;
    }
    if (d->curve == PARAM_INT) {
        snprintf(out, size, "%d%s%s", (int)floorf(v + 0.5f), unit ? " " : "", unit ? unit : "");
        return;
    }
    if (unit && strcmp(unit, "ms") == 0 && fabsf(v) >= 1000.0f) { v /= 1000.0f; unit = "s"; }      // 1.20 s, not 1.20k ms
    const float a = fabsf(v);
    char num[16];
    if (a < 1000.0f && fabsf(v - floorf(v + 0.5f)) < 0.005f) snprintf(num, sizeof num, "%d", (int)floorf(v + 0.5f));
    else if (a >= 10000.0f) snprintf(num, sizeof num, "%.1fk", v / 1000.0f);
    else if (a >= 1000.0f) snprintf(num, sizeof num, "%.2fk", v / 1000.0f);
    else if (a >= 100.0f) snprintf(num, sizeof num, "%.0f", v);
    else if (a >= 10.0f) snprintf(num, sizeof num, "%.1f", v);
    else snprintf(num, sizeof num, "%.2f", v);
    snprintf(out, size, "%s%s%s", num, unit ? " " : "", unit ? unit : "");
}

#ifdef __cplusplus
}
#endif
