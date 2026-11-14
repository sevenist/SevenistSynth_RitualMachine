#pragma once
// Response curves: a knob's travel 0..1 turned into 0..1, for mappings and macros (a knob onto a value's range). Portable C (header only:
// static inline), usable from C++.
//
// Built in (ids 0..CRV_BUILTIN-1): Lin, Exp (slow start, fast end), Log (its mirror), S (smoothstep), each read from a CRV_LUT-point table
// with linear interpolation (cheap on a board). User curves (ids CRV_USER_FIRST + k, named U1..U8): 2..CRV_POINTS_MAX points (x, y
// 0..100), each saying whether the segment after it is linear or stepped (held until the next point), kept as a CRV_USER_LUT table that
// is rebuilt on every edit. The user curves live in a crv_bank_t the app owns (the Ritual Machine: one, saved on the card); every call
// takes it, or NULL for the built-ins alone. An unknown id, or a user curve that is not there, is linear.
//
// The text of a bank (the Ritual Machine's curves.cfg):
//   curve 1                       a user curve (U1); its points follow, in order of x
//   point 0 0 lin                 x y (0..100) and the segment to the next point: lin or step
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRV_LUT          33
enum { CRV_LIN, CRV_EXP, CRV_LOG, CRV_S, CRV_BUILTIN, CRV_USER_FIRST = 16 };
#define CRV_USER_MAX     8
#define CRV_POINTS_MAX   16
#define CRV_USER_LUT     129
enum { CRV_PT_LIN, CRV_PT_STEP };

typedef struct { uint8_t x, y, mode; } crv_point_t;            // x, y 0..100; mode: the segment from this point to the next
typedef struct { bool used; uint8_t n; crv_point_t pt[CRV_POINTS_MAX]; uint16_t lut[CRV_USER_LUT]; } crv_user_t;
typedef struct { crv_user_t user[CRV_USER_MAX]; unsigned rev; } crv_bank_t;    // rev: changes on every edit

/* ---------------- the curves ---------------- */

// The built-in tables, made on first use.
static inline const float *crv_builtin_table(int id) {
    static float lut[CRV_BUILTIN][CRV_LUT];
    static bool ready = false;
    if (!ready) {
        const float k = 4.0f, ek = expf(k) - 1.0f;                 // the Exp / Log bend
        for (int i = 0; i < CRV_LUT; i++) {
            const float x = (float)i / (CRV_LUT - 1);
            lut[CRV_LIN][i] = x;
            lut[CRV_EXP][i] = (expf(k * x) - 1.0f) / ek;            // slow start, fast end
            lut[CRV_LOG][i] = logf(1.0f + ek * x) / k;              // the mirror: fast start
            lut[CRV_S][i]   = x * x * (3.0f - 2.0f * x);            // smoothstep
        }
        ready = true;
    }
    return lut[id];
}

static inline const crv_user_t *crv_user_of(const crv_bank_t *b, int id) {
    const int k = id - CRV_USER_FIRST;
    return b && k >= 0 && k < CRV_USER_MAX && b->user[k].used ? &b->user[k] : NULL;
}

// x 0..1 -> 0..1.
static inline float crv_eval(const crv_bank_t *b, int id, float x) {
    if (x <= 0.0f) x = 0.0f;
    if (x >= 1.0f) x = 1.0f;
    const crv_user_t *u = crv_user_of(b, id);
    if (u) {
        const float p = x * (CRV_USER_LUT - 1);
        const int i = (int)p;
        if (i >= CRV_USER_LUT - 1) return (float)u->lut[CRV_USER_LUT - 1] / 65535.0f;
        const float a = (float)u->lut[i], c = (float)u->lut[i + 1];
        return (a + (c - a) * (p - (float)i)) / 65535.0f;
    }
    if (id <= CRV_LIN || id >= CRV_BUILTIN) return x;
    const float *t = crv_builtin_table(id);
    const float p = x * (CRV_LUT - 1);
    const int i = (int)p;
    if (i >= CRV_LUT - 1) return t[CRV_LUT - 1];
    return t[i] + (t[i + 1] - t[i]) * (p - (float)i);
}

static inline const char *crv_name(int id) {                    // "Lin", "Exp", "Log", "S", "U1".."U8"; "?" for an unknown id
    static const char *const n[CRV_BUILTIN] = {"Lin", "Exp", "Log", "S"};
    static const char *const u[CRV_USER_MAX] = {"U1", "U2", "U3", "U4", "U5", "U6", "U7", "U8"};
    if (id >= 0 && id < CRV_BUILTIN) return n[id];
    if (id >= CRV_USER_FIRST && id < CRV_USER_FIRST + CRV_USER_MAX) return u[id - CRV_USER_FIRST];
    return "?";
}

static inline bool crv_name_is(const char *a, const char *b) {   // (case-insensitive)
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return !*a && !*b;
}

// The id of a name, case-insensitive (a user curve's name whether it exists or not); -1 when unknown.
static inline int crv_by_name(const char *name) {
    for (int k = 0; k < CRV_USER_MAX; k++) if (crv_name_is(crv_name(CRV_USER_FIRST + k), name)) return CRV_USER_FIRST + k;
    for (int id = 0; id < CRV_BUILTIN; id++) if (crv_name_is(crv_name(id), name)) return id;
    return -1;
}

// The curves to choose from: the built-ins, then the user curves that exist. crv_step: the next / previous one from `id`, round.
static inline int crv_count(const crv_bank_t *b) {
    int n = CRV_BUILTIN;
    if (b) for (int k = 0; k < CRV_USER_MAX; k++) n += b->user[k].used;
    return n;
}
static inline int crv_at(const crv_bank_t *b, int i) {
    if (i < CRV_BUILTIN) return i < 0 ? CRV_LIN : i;
    i -= CRV_BUILTIN;
    if (b) for (int k = 0; k < CRV_USER_MAX; k++) if (b->user[k].used && i-- == 0) return CRV_USER_FIRST + k;
    return CRV_LIN;
}
static inline int crv_step(const crv_bank_t *b, int id, int dir) {
    const int n = crv_count(b);
    int at = 0;
    for (int i = 0; i < n; i++) if (crv_at(b, i) == id) at = i;
    return crv_at(b, (at + dir + n) % n);
}

/* ---------------- user curves ---------------- */

// The table: for each position, the segment it falls in; a stepped segment holds its start point's y, a linear one goes to the next point.
static inline void crv_user_rebuild(crv_user_t *u) {
    for (int i = 0; i < CRV_USER_LUT; i++) {
        const float x = 100.0f * (float)i / (CRV_USER_LUT - 1);
        float y;
        if (x <= u->pt[0].x) y = u->pt[0].y;
        else if (x >= u->pt[u->n - 1].x) y = u->pt[u->n - 1].y;
        else {
            int j = 0;
            while (j + 1 < u->n && u->pt[j + 1].x <= x) j++;
            const crv_point_t *a = &u->pt[j], *c = &u->pt[j + 1];
            if (a->mode == CRV_PT_STEP || c->x == a->x) y = a->y;
            else y = a->y + (c->y - a->y) * (x - a->x) / (float)(c->x - a->x);
        }
        u->lut[i] = (uint16_t)(y / 100.0f * 65535.0f + 0.5f);
    }
}

static inline void crv_init(crv_bank_t *b) { memset(b->user, 0, sizeof b->user); b->rev++; }    // no user curves
static inline crv_user_t *crv_editable(crv_bank_t *b, int k) { return k >= 0 && k < CRV_USER_MAX && b->user[k].used ? &b->user[k] : NULL; }
static inline void crv_changed(crv_bank_t *b, crv_user_t *u) { crv_user_rebuild(u); b->rev++; }

// Makes slot k a curve (0,0) -> (100,100), linear; returns k, -1 when it exists.
static inline int crv_user_new(crv_bank_t *b, int k) {
    if (k < 0 || k >= CRV_USER_MAX || b->user[k].used) return -1;
    crv_user_t *u = &b->user[k];
    memset(u, 0, sizeof *u);
    u->used = true;
    u->n = 2;
    u->pt[0].x = 0;   u->pt[0].y = 0;   u->pt[0].mode = CRV_PT_LIN;
    u->pt[1].x = 100; u->pt[1].y = 100; u->pt[1].mode = CRV_PT_LIN;
    crv_changed(b, u);
    return k;
}

static inline void crv_user_delete(crv_bank_t *b, int k) {
    if (!crv_editable(b, k)) return;
    memset(&b->user[k], 0, sizeof b->user[k]);
    b->rev++;
}

// A point halfway to the next one (or after the last); returns its index, -1 when full.
static inline int crv_user_add_point(crv_bank_t *b, int k, int after) {
    crv_user_t *u = crv_editable(b, k);
    if (!u || u->n < 1 || u->n >= CRV_POINTS_MAX) return -1;            // (a curve has 2 points at least)
    if (after < 0) after = 0;
    if (after >= u->n) after = u->n - 1;
    crv_point_t p = u->pt[after];
    if (after + 1 < u->n) {
        p.x = (uint8_t)((u->pt[after].x + u->pt[after + 1].x) / 2);
        p.y = (uint8_t)((u->pt[after].y + u->pt[after + 1].y) / 2);
    } else if (p.x < 100) p.x = (uint8_t)(p.x + (100 - p.x) / 2);
    for (int i = u->n; i > after + 1; i--) u->pt[i] = u->pt[i - 1];
    u->pt[after + 1] = p;
    u->n++;
    crv_changed(b, u);
    return after + 1;
}

static inline void crv_user_remove_point(crv_bank_t *b, int k, int i) {     // a curve keeps at least 2 points
    crv_user_t *u = crv_editable(b, k);
    if (!u || u->n <= 2 || i < 0 || i >= u->n) return;
    for (int j = i; j + 1 < u->n; j++) u->pt[j] = u->pt[j + 1];
    u->n--;
    crv_changed(b, u);
}

static inline void crv_user_set_point(crv_bank_t *b, int k, int i, int x, int y) {   // x stays between the neighbours
    crv_user_t *u = crv_editable(b, k);
    if (!u || i < 0 || i >= u->n) return;
    const int lo = i > 0 ? u->pt[i - 1].x : 0, hi = i + 1 < u->n ? u->pt[i + 1].x : 100;
    x = x < lo ? lo : x > hi ? hi : x;
    y = y < 0 ? 0 : y > 100 ? 100 : y;
    if (u->pt[i].x == x && u->pt[i].y == y) return;
    u->pt[i].x = (uint8_t)x; u->pt[i].y = (uint8_t)y;
    crv_changed(b, u);
}

static inline void crv_user_set_mode(crv_bank_t *b, int k, int i, int mode) {
    crv_user_t *u = crv_editable(b, k);
    if (!u || i < 0 || i >= u->n || u->pt[i].mode == mode) return;
    u->pt[i].mode = (uint8_t)(mode == CRV_PT_STEP ? CRV_PT_STEP : CRV_PT_LIN);
    crv_changed(b, u);
}

/* ---------------- the text ---------------- */

// The user curves as text (header line `title`, NULL: none); returns its length (cut at cap - 1).
static inline int crv_to_text(const crv_bank_t *b, const char *title, char *buf, int cap) {
    int n = title ? snprintf(buf, (size_t)cap, "%s\n", title) : 0;
    if (!title && cap > 0) buf[0] = 0;
    for (int k = 0; k < CRV_USER_MAX && n < cap; k++) {
        const crv_user_t *u = &b->user[k];
        if (!u->used) continue;
        n += snprintf(buf + n, (size_t)(cap - n), "curve %d\n", k + 1);
        for (int i = 0; i < u->n && n < cap; i++)
            n += snprintf(buf + n, (size_t)(cap - n), "point %d %d %s\n", u->pt[i].x, u->pt[i].y, u->pt[i].mode == CRV_PT_STEP ? "step" : "lin");
    }
    return n < cap ? n : cap - 1;
}

// Replaces every user curve with the text's (a curve with fewer than 2 points is completed). False: no curve in it, nothing changed.
static inline bool crv_from_text(crv_bank_t *b, const char *txt) {
    static crv_user_t got[CRV_USER_MAX];                            // (2.5 KB: not on the stack)
    memset(got, 0, sizeof got);
    bool any = false;
    int cur = -1;
    for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        int a, x, y;
        char mode[8];
        if (sscanf(line, "curve %d", &a) == 1) {
            cur = a >= 1 && a <= CRV_USER_MAX ? a - 1 : -1;
            if (cur >= 0) { memset(&got[cur], 0, sizeof got[cur]); got[cur].used = true; any = true; }
        } else if (cur >= 0 && sscanf(line, "point %d %d %7s", &x, &y, mode) == 3 && got[cur].n < CRV_POINTS_MAX) {
            crv_user_t *u = &got[cur];
            const int lo = u->n ? u->pt[u->n - 1].x : 0;           // keep the order of x
            x = x < lo ? lo : x > 100 ? 100 : x;
            y = y < 0 ? 0 : y > 100 ? 100 : y;
            crv_point_t *p = &u->pt[u->n++];
            p->x = (uint8_t)x; p->y = (uint8_t)y; p->mode = (uint8_t)(!strcmp(mode, "step") ? CRV_PT_STEP : CRV_PT_LIN);
        }
    }
    if (!any) return false;
    for (int k = 0; k < CRV_USER_MAX; k++) {
        crv_user_t *u = &got[k];
        if (u->used && u->n < 2) {
            if (u->n == 0) { u->pt[0].x = 0; u->pt[0].y = 0; u->pt[0].mode = CRV_PT_LIN; }
            u->pt[1].x = 100; u->pt[1].y = 100; u->pt[1].mode = CRV_PT_LIN;
            u->n = 2;
        }
        if (u->used) crv_user_rebuild(u);
    }
    memcpy(b->user, got, sizeof b->user);
    b->rev++;
    return true;
}

#ifdef __cplusplus
}
#endif
