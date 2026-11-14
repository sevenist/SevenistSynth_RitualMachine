#pragma once
// Macros: one control that moves several targets at once, each through a mapping, and a macro can drive other macros.
// Portable C (header only: static inline), usable from C++.
//
// A mapping (ctl_map_t): the control's travel 0..1 goes through a response curve (control/curve.h), then onto Min..Max of the target's
// range (0..1 of it; Min > Max inverts). A macro (mac_t) has up to MAC_DESTS of them, its destinations.
//
// A destination can be another macro of the same set (MAC_KIND_MACRO): it then sets that macro's position, which moves that macro's own
// destinations, and so on down the chain (M1 -> M2 -> M3: M1 sweeps M2 over part of its travel). Loops are refused when a destination is
// added (mac_add), so the macros and their links never form a cycle and a chain always ends.
//
// The app says what a target is: ctl_target_t is its key (kind: the app's own, 1..0xFD; id; prm), except kind MAC_KIND_MACRO, the
// library's (prm: the macro's index). The app owns its macros (an array of MAC_MAX) and sets its own targets: mac_apply calls the app's
// function for every destination that is not a macro, with the value as 0..1 of the target's range.
// The Ritual Machine's macros, joystick and Shift / Mod mappings (core/synth_ui.h) and SynthLab Studio's macros (macros.h) use it.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "control/curve.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAC_MAX        8                // macros in a set
#define MAC_DESTS      8                // destinations of a macro
#define MAC_NAME       16
#define MAC_KIND_NONE  0                // no target
#define MAC_KIND_MACRO 0xFE             // another macro of the set (prm: its index)

// (C++: default values, so `ctl_map_t{}` is the plain mapping and `mac_t{}` an empty macro in the middle; in C they are zero.)
#ifdef __cplusplus
#define MAC_DEFAULT(x) = x
#else
#define MAC_DEFAULT(x)
#endif

// What a mapping drives: the app's key (no padding: the structs below compare with memcmp).
typedef struct { uint8_t kind, id; uint16_t prm; } ctl_target_t;

typedef struct {
    ctl_target_t t;
    float   min MAC_DEFAULT(0.0f), max MAC_DEFAULT(1.0f);      // 0..1 of the target's range
    int32_t curve MAC_DEFAULT(CRV_LIN);                       // control/curve.h
} ctl_map_t;

typedef struct {
    ctl_map_t d[MAC_DESTS];
    float   pos MAC_DEFAULT(0.5f);                           // where it is, 0..1; < 0: not known (the app may measure it)
    int32_t n MAC_DEFAULT(0);                                // destinations in d[]
    int32_t color MAC_DEFAULT(0);                            // the app's (an LED colour, ...)
    char    name[MAC_NAME] MAC_DEFAULT({});                  // the app's ("" when it names them itself: "M1")
} mac_t;

// The function that sets one of the app's targets to v (0..1 of its range). True when the target exists.
typedef bool (*mac_set_fn)(void *ctx, const ctl_target_t *t, float v);

/* ---------------- targets and mappings ---------------- */

static inline ctl_target_t ctl_target(int kind, int id, int prm) {
    ctl_target_t t;
    t.kind = (uint8_t)kind; t.id = (uint8_t)id; t.prm = (uint16_t)prm;
    return t;
}
static inline ctl_target_t mac_target(int k) { return ctl_target(MAC_KIND_MACRO, 0, k); }
static inline bool ctl_target_same(const ctl_target_t *a, const ctl_target_t *b) { return a->kind == b->kind && a->id == b->id && a->prm == b->prm; }
// The macro a target is (MAC_KIND_MACRO), -1: not a macro.
static inline int mac_of(const ctl_target_t *t) { return t->kind == MAC_KIND_MACRO && t->prm < MAC_MAX ? (int)t->prm : -1; }

// The whole range, linear.
static inline ctl_map_t ctl_map_plain(ctl_target_t t) {
    ctl_map_t m;
    m.t = t; m.min = 0.0f; m.max = 1.0f; m.curve = CRV_LIN;
    return m;
}
static inline bool ctl_map_is_plain(const ctl_map_t *m) { return m->min == 0.0f && m->max == 1.0f && m->curve == CRV_LIN; }
// The target's position 0..1 for the control at x (0..1). `curves`: the user curves (NULL: the built-ins only).
static inline float ctl_map_apply(const crv_bank_t *curves, const ctl_map_t *m, float x) {
    return m->min + (m->max - m->min) * crv_eval(curves, m->curve, x);
}
// The first of `positions` control positions (0 .. positions-1, evenly over 0..1) whose output is nearest v.
static inline int ctl_map_inverse(const crv_bank_t *curves, const ctl_map_t *m, float v, int positions) {
    int best = 0;
    float best_d = 2.0f;
    for (int p = 0; p < positions; p++) {
        float d = ctl_map_apply(curves, m, (float)p / (float)(positions - 1)) - v;
        if (d < 0) d = -d;
        if (d < best_d - 1e-6f) { best_d = d; best = p; }
    }
    return best;
}
// A 0..1 position in whole percent (what the screens show and the Ritual Machine's files keep).
static inline int ctl_pct(float x) { return (int)(x * 100.0f + (x < 0.0f ? -0.5f : 0.5f)); }

/* ---------------- macros ---------------- */

// The destination on `t`, -1: none.
static inline int mac_find(const mac_t *m, const ctl_target_t *t) {
    for (int i = 0; i < m->n && i < MAC_DESTS; i++)
        if (ctl_target_same(&m->d[i].t, t)) return i;
    return -1;
}

// True when macro `from` moves macro `to` through its links (directly or down a chain), or from == to.
static inline bool mac_reaches(const mac_t *set, int from, int to) {
    if (from < 0 || from >= MAC_MAX || to < 0 || to >= MAC_MAX) return false;
    unsigned seen = 0, todo = 1u << from;
    while (todo) {
        int k = 0;
        while (!(todo >> k & 1u)) k++;
        todo &= ~(1u << k);
        if (k == to) return true;
        seen |= 1u << k;
        for (int i = 0; i < set[k].n && i < MAC_DESTS; i++) {
            const int j = mac_of(&set[k].d[i].t);
            if (j >= 0 && !(seen >> j & 1u)) todo |= 1u << j;
        }
    }
    return false;
}

// Macro k can take a link to macro j: another macro, not one that moves k (that would loop), not there already, room for it.
static inline bool mac_can_link(const mac_t *set, int k, int j) {
    if (k < 0 || k >= MAC_MAX || j < 0 || j >= MAC_MAX || mac_reaches(set, j, k)) return false;
    const ctl_target_t t = mac_target(j);
    return set[k].n < MAC_DESTS && mac_find(&set[k], &t) < 0;
}

// Adds `map` to macro k. Returns its index; -1 when it has no target, it is there already, the macro is full, or it would loop.
static inline int mac_add(mac_t *set, int k, ctl_map_t map) {
    if (k < 0 || k >= MAC_MAX || map.t.kind == MAC_KIND_NONE) return -1;
    mac_t *m = &set[k];
    if (map.t.kind == MAC_KIND_MACRO) { if (!mac_can_link(set, k, (int)map.t.prm)) return -1; }
    else if (m->n >= MAC_DESTS || mac_find(m, &map.t) >= 0) return -1;
    m->d[m->n] = map;
    return m->n++;
}

// Removes destination i (the next ones move up).
static inline void mac_remove(mac_t *m, int i) {
    if (i < 0 || i >= m->n) return;
    for (int j = i; j + 1 < m->n; j++) m->d[j] = m->d[j + 1];
    m->n--;
    memset((void *)&m->d[m->n], 0, sizeof m->d[m->n]);    // (void *: a C++ ctl_map_t has default values)
}

static inline bool mac__apply(mac_t *set, const crv_bank_t *curves, int k, int first, float pos, mac_set_fn fn, void *ctx, int depth) {
    bool any = false;
    const mac_t *m = &set[k];
    for (int i = first < 0 ? 0 : first; i < m->n && i < MAC_DESTS; i++) {
        const ctl_map_t *d = &m->d[i];
        float v = ctl_map_apply(curves, d, pos);
        v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
        const int j = mac_of(&d->t);
        if (j < 0) { any |= fn(ctx, &d->t, v); continue; }
        if (depth >= MAC_MAX) continue;                       // (a loop: never made by mac_add)
        set[j].pos = v;
        mac__apply(set, curves, j, 0, v, fn, ctx, depth + 1);
        any = true;
    }
    return any;
}

// Puts macro k at `pos` (0..1) and moves its destinations, the macros it drives with theirs, in order. True when anything was set.
static inline bool mac_apply(mac_t *set, const crv_bank_t *curves, int k, float pos, mac_set_fn fn, void *ctx) {
    if (k < 0 || k >= MAC_MAX) return false;
    pos = pos < 0.0f ? 0.0f : pos > 1.0f ? 1.0f : pos;
    set[k].pos = pos;
    return mac__apply(set, curves, k, 0, pos, fn, ctx, 0);
}
// The same from destination `first` on, the position left alone (the app set the ones before itself: the Ritual Machine's catch).
static inline bool mac_apply_from(mac_t *set, const crv_bank_t *curves, int k, int first, float pos, mac_set_fn fn, void *ctx) {
    if (k < 0 || k >= MAC_MAX) return false;
    return mac__apply(set, curves, k, first, pos, fn, ctx, 0);
}

#ifdef __cplusplus
}
#endif
