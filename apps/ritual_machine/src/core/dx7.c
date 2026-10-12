#include "core/dx7.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void dx7_load_factory(dx7_patch_t *p, int index) {
    if (index < 0 || index >= DX7_FACTORY_COUNT) index = 0;
    *p = dx7_factory[index];
}

float dx7_amp(int level)       { return 2.0f * exp2f((float)(level - 99) / 8.0f); }
float dx7_env_value(int level) { return exp2f((float)(level - 99) / 8.0f); }

/* ---------------- operator parameters ---------------- */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

bool dx7_op_adjust(dx7_patch_t *p, int op, dx7_op_param_t id, int dir) {
    dx7_op_t *o = &p->op[op];
    switch (id) {
    case DXP_LEVEL: {
        int v = clampi(o->level + dir, 0, 99);
        bool ch = v != o->level;
        o->level = (uint8_t)v;
        return ch;
    }
    case DXP_COARSE: {
        float v = o->coarse + 0.5f * (float)dir;
        if (v < 0.5f) v = 0.5f;
        if (v > 31.0f) v = 31.0f;
        bool ch = v != o->coarse;
        o->coarse = v;
        return ch;
    }
    case DXP_FINE: {
        float v = o->fine + 0.005f * (float)dir;
        if (v < -0.5f) v = -0.5f;
        if (v > 0.5f) v = 0.5f;
        if (fabsf(v) < 0.0025f) v = 0.0f;       // snap back to exactly 0
        bool ch = v != o->fine;
        o->fine = v;
        return ch;
    }
    case DXP_FIXED: {                           // Off <-> 1 Hz .. 9.9 kHz (x1.12 per step)
        float v = o->fixed_hz;
        if (v <= 0.0f) { if (dir > 0) v = 110.0f; else return false; }
        else v = dir > 0 ? v * 1.12f : v / 1.12f;
        if (v < 1.0f) v = 0.0f;
        if (v > 9900.0f) v = 9900.0f;
        bool ch = v != o->fixed_hz;
        o->fixed_hz = v;
        return ch;
    }
    default: return false;
    }
}

const char *dx7_op_label(dx7_op_param_t id) {
    static const char *const l[DXP_COUNT] = {"Lvl", "Crs", "Fine", "Fix"};
    return l[id];
}

void dx7_op_format(const dx7_patch_t *p, int op, dx7_op_param_t id, char *out, size_t n) {
    const dx7_op_t *o = &p->op[op];
    switch (id) {
    case DXP_LEVEL:  snprintf(out, n, "%d", o->level); break;
    case DXP_COARSE: snprintf(out, n, "%.1f", o->coarse); break;
    case DXP_FINE:   snprintf(out, n, "%+.3f", o->fine); break;
    case DXP_FIXED:
        if (o->fixed_hz <= 0.0f) snprintf(out, n, "Off");
        else if (o->fixed_hz >= 1000.0f) snprintf(out, n, "%.1fkHz", o->fixed_hz / 1000.0f);
        else snprintf(out, n, "%.0fHz", o->fixed_hz);
        break;
    default: out[0] = 0;
    }
}

/* ---------------- envelope points ---------------- */

bool dx7_eg_adjust(dx7_patch_t *p, int op, int point, dx7_eg_field_t field, int dir) {
    dx7_op_t *o = &p->op[op];
    if (field == DXE_LEVEL) {
        int v = clampi(o->eg_l[point] + dir, 0, 99);
        bool ch = v != o->eg_l[point];
        o->eg_l[point] = (uint8_t)v;
        return ch;
    }
    // time: 0 <-> 1 ms ... 60 s, x1.2 per step (a stage of 0 ms is allowed, as in the factory patches)
    uint32_t t = o->eg_t[point], nt;
    if (dir > 0) nt = t < 1 ? 1 : (uint32_t)((float)t * 1.2f + 1.0f);
    else         nt = t <= 1 ? 0 : (uint32_t)((float)t / 1.2f);
    if (nt > 60000) nt = 60000;
    bool ch = nt != t;
    o->eg_t[point] = nt;
    return ch;
}

void dx7_eg_format(const dx7_patch_t *p, int op, int point, dx7_eg_field_t field, char *out, size_t n) {
    const dx7_op_t *o = &p->op[op];
    if (field == DXE_LEVEL) snprintf(out, n, "%d", o->eg_l[point]);
    else if (o->eg_t[point] >= 1000) snprintf(out, n, "%.2fs", (double)o->eg_t[point] / 1000.0);
    else snprintf(out, n, "%ums", (unsigned)o->eg_t[point]);
}

/* ---------------- patch-wide ---------------- */

bool dx7_algorithm_adjust(dx7_patch_t *p, int dir) {
    int v = clampi(p->algorithm + dir, 1, DX7_ALGORITHMS);
    bool ch = v != p->algorithm;
    p->algorithm = (uint8_t)v;
    return ch;
}

bool dx7_feedback_adjust(dx7_patch_t *p, int dir) {
    float v = p->feedback + 0.02f * (float)dir;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (fabsf(v) < 0.01f) v = 0.0f;
    bool ch = v != p->feedback;
    p->feedback = v;
    return ch;
}

void dx7_feedback_format(const dx7_patch_t *p, char *out, size_t n) { snprintf(out, n, "%.2f", p->feedback); }

/* ---------------- one code for every value (dx7.h) ---------------- */

#define FIXED_MAX 9900.0f
#define TIME_MAX  60000.0f

bool dx7_value_valid(int op, int v) {
    if (op == DX7_GLOBAL_OP) return v >= 0 && v < DXG_COUNT;
    return op >= 0 && op < DX7_OPS && v >= 0 && v < DXV_COUNT;
}

bool dx7_value_adjust(dx7_patch_t *p, int op, int v, int dir) {
    if (!dx7_value_valid(op, v)) return false;
    if (op == DX7_GLOBAL_OP) return v == DXG_ALGO ? dx7_algorithm_adjust(p, dir) : dx7_feedback_adjust(p, dir);
    if (v <= DXV_FIXED) return dx7_op_adjust(p, op, (dx7_op_param_t)v, dir);
    if (v <= DXV_EG_L4) return dx7_eg_adjust(p, op, v - DXV_EG_L1, DXE_LEVEL, dir);
    return dx7_eg_adjust(p, op, v - DXV_EG_T1, DXE_TIME, dir);
}

void dx7_value_format(const dx7_patch_t *p, int op, int v, char *out, size_t n) {
    if (!dx7_value_valid(op, v)) { snprintf(out, n, "?"); return; }
    if (op == DX7_GLOBAL_OP) { if (v == DXG_ALGO) snprintf(out, n, "%d", p->algorithm); else dx7_feedback_format(p, out, n); return; }
    if (v <= DXV_FIXED) dx7_op_format(p, op, (dx7_op_param_t)v, out, n);
    else if (v <= DXV_EG_L4) dx7_eg_format(p, op, v - DXV_EG_L1, DXE_LEVEL, out, n);
    else dx7_eg_format(p, op, v - DXV_EG_T1, DXE_TIME, out, n);
}

const char *dx7_value_label(int op, int v) {
    static const char *const g[DXG_COUNT] = {"Algo", "Fb"};
    static const char *const o[DXV_COUNT] = {"Lvl", "Crs", "Fine", "Fix", "L1", "L2", "L3", "L4", "T1", "T2", "T3", "T4"};
    if (!dx7_value_valid(op, v)) return "?";
    return op == DX7_GLOBAL_OP ? g[v] : o[v];
}

static float clampf(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

// Envelope time at knob position i of TIME_STEPS: log-like up to 60 s, but at least 1 ms more per position (times are whole ms, so a pure
// log scale gave the same time to the first ~18 positions and the knob read back the wrong position).
#define TIME_STEPS 127
static uint32_t time_at(int i) {
    return (uint32_t)i + (uint32_t)(expf((float)i / TIME_STEPS * logf(1.0f + TIME_MAX - TIME_STEPS)) - 1.0f + 0.5f);
}
static float time_pos(uint32_t t) {                         // the position whose time is nearest to t, as 0..1
    int lo = 0, hi = TIME_STEPS;
    while (lo < hi) { const int mid = (lo + hi) / 2; if (time_at(mid) < t) lo = mid + 1; else hi = mid; }
    if (lo > 0 && t - time_at(lo - 1) < time_at(lo) - t) lo--;
    return (float)lo / TIME_STEPS;
}

// Fixed frequency: 0 = Off, then 1 Hz .. 9.9 kHz on a log scale. Envelope time: 0 .. 60 s (time_at).
float dx7_value_get(const dx7_patch_t *p, int op, int v) {
    if (!dx7_value_valid(op, v)) return 0.0f;
    if (op == DX7_GLOBAL_OP) return v == DXG_ALGO ? (float)(p->algorithm - 1) / (DX7_ALGORITHMS - 1) : clampf(p->feedback);
    const dx7_op_t *o = &p->op[op];
    switch (v) {
        case DXV_LEVEL:  return (float)o->level / 99.0f;
        case DXV_COARSE: return clampf((o->coarse - 0.5f) / 30.5f);
        case DXV_FINE:   return clampf(o->fine + 0.5f);
        case DXV_FIXED:  return o->fixed_hz < 1.0f ? 0.0f : clampf(logf(o->fixed_hz) / logf(FIXED_MAX));
        default:
            if (v <= DXV_EG_L4) return (float)o->eg_l[v - DXV_EG_L1] / 99.0f;
            return time_pos(o->eg_t[v - DXV_EG_T1]);
    }
}

bool dx7_value_set(dx7_patch_t *p, int op, int v, float x) {
    if (!dx7_value_valid(op, v)) return false;
    x = clampf(x);
    if (op == DX7_GLOBAL_OP) {
        if (v == DXG_ALGO) { const uint8_t a = (uint8_t)(1 + (int)(x * (DX7_ALGORITHMS - 1) + 0.5f)); const bool ch = a != p->algorithm; p->algorithm = a; return ch; }
        const float f = roundf(x * 50.0f) / 50.0f; const bool ch = f != p->feedback; p->feedback = f; return ch;
    }
    dx7_op_t *o = &p->op[op];
    switch (v) {
        case DXV_LEVEL:  { const uint8_t l = (uint8_t)(x * 99.0f + 0.5f); const bool ch = l != o->level; o->level = l; return ch; }
        case DXV_COARSE: { const float c = 0.5f + roundf(x * 61.0f) * 0.5f; const bool ch = c != o->coarse; o->coarse = c; return ch; }
        case DXV_FINE:   { const float f = roundf((x - 0.5f) * 200.0f) / 200.0f; const bool ch = f != o->fine; o->fine = f; return ch; }
        case DXV_FIXED:  { const float h = x <= 0.0f ? 0.0f : expf(x * logf(FIXED_MAX)); const float hz = h < 1.0f && x > 0.0f ? 1.0f : h; const bool ch = hz != o->fixed_hz; o->fixed_hz = hz; return ch; }
        default:
            if (v <= DXV_EG_L4) { uint8_t *l = &o->eg_l[v - DXV_EG_L1]; const uint8_t n = (uint8_t)(x * 99.0f + 0.5f); const bool ch = n != *l; *l = n; return ch; }
            uint32_t *t = &o->eg_t[v - DXV_EG_T1];
            const uint32_t n = time_at((int)(x * TIME_STEPS + 0.5f));
            const bool ch = n != *t; *t = n; return ch;
    }
}

int dx7_value_steps(int op, int v) {
    if (op == DX7_GLOBAL_OP) return v == DXG_ALGO ? DX7_ALGORITHMS - 1 : 50;
    switch (v) {
        case DXV_COARSE: return 61;
        case DXV_FINE:   return 127;
        case DXV_FIXED:  return 127;
        case DXV_EG_T1: case DXV_EG_T2: case DXV_EG_T3: case DXV_EG_T4: return TIME_STEPS;
        default:         return 99;
    }
}
