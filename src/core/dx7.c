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
