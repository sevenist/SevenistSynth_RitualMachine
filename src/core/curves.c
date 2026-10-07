#include "core/curves.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static user_curve_t users[CURVE_USER_MAX];
static unsigned     g_rev, g_saved_rev;
static bool         g_have_saved;

static const user_curve_t *user_of(int id) {
    const int k = id - CURVE_USER_FIRST;
    return k >= 0 && k < CURVE_USER_MAX && users[k].used ? &users[k] : NULL;
}

static float lut[CURVE_BUILTIN][CURVE_LUT];
static bool  lut_ready;

static void build(void) {
    const float k = 4.0f, ek = expf(k) - 1.0f;                     // the Exp / Log bend
    for (int i = 0; i < CURVE_LUT; i++) {
        const float x = (float)i / (CURVE_LUT - 1);
        lut[CURVE_LIN][i] = x;
        lut[CURVE_EXP][i] = (expf(k * x) - 1.0f) / ek;             // slow start, fast end
        lut[CURVE_LOG][i] = logf(1.0f + ek * x) / k;                // the mirror: fast start
        lut[CURVE_S][i]   = x * x * (3.0f - 2.0f * x);              // smoothstep
    }
    lut_ready = true;
}

float curve_eval(int id, float x) {
    if (x <= 0.0f) x = 0.0f;
    if (x >= 1.0f) x = 1.0f;
    const user_curve_t *u = user_of(id);
    if (u) {
        const float p = x * (CURVE_USER_LUT - 1);
        const int i = (int)p;
        if (i >= CURVE_USER_LUT - 1) return (float)u->lut[CURVE_USER_LUT - 1] / 65535.0f;
        const float a = (float)u->lut[i], b = (float)u->lut[i + 1];
        return (a + (b - a) * (p - (float)i)) / 65535.0f;
    }
    if (id <= CURVE_LIN || id >= CURVE_BUILTIN) return x;
    if (!lut_ready) build();
    const float p = x * (CURVE_LUT - 1);
    int i = (int)p;
    if (i >= CURVE_LUT - 1) return lut[id][CURVE_LUT - 1];
    const float f = p - (float)i;
    return lut[id][i] + (lut[id][i + 1] - lut[id][i]) * f;
}

const char *curve_name(int id) {
    static const char *const n[CURVE_BUILTIN] = {"Lin", "Exp", "Log", "S"};
    static const char *const u[CURVE_USER_MAX] = {"U1", "U2", "U3", "U4", "U5", "U6", "U7", "U8"};
    if (id >= 0 && id < CURVE_BUILTIN) return n[id];
    if (id >= CURVE_USER_FIRST && id < CURVE_USER_FIRST + CURVE_USER_MAX) return u[id - CURVE_USER_FIRST];
    return "?";
}

int curve_count(void) {
    int n = CURVE_BUILTIN;
    for (int k = 0; k < CURVE_USER_MAX; k++) n += users[k].used;
    return n;
}

int curve_at(int i) {
    if (i < CURVE_BUILTIN) return i < 0 ? CURVE_LIN : i;
    i -= CURVE_BUILTIN;
    for (int k = 0; k < CURVE_USER_MAX; k++) if (users[k].used && i-- == 0) return CURVE_USER_FIRST + k;
    return CURVE_LIN;
}

int curve_step(int id, int dir) {
    const int n = curve_count();
    int at = 0;
    for (int i = 0; i < n; i++) if (curve_at(i) == id) at = i;
    return curve_at((at + dir + n) % n);
}

int curve_by_name(const char *name) {
    for (int k = 0; k < CURVE_USER_MAX; k++) {
        const char *a = curve_name(CURVE_USER_FIRST + k), *b = name;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return CURVE_USER_FIRST + k;
    }
    for (int id = 0; id < CURVE_BUILTIN; id++) {
        const char *a = curve_name(id), *b = name;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return id;
    }
    return -1;
}

float mapping_apply(const mapping_t *m, float knob) {
    const float lo = (float)m->min / 100.0f, hi = (float)mapping_max(m) / 100.0f;
    return lo + (hi - lo) * curve_eval(m->curve, knob);
}

int mapping_inverse(const mapping_t *m, float v) {
    int best = 0;
    float best_d = 2.0f;
    for (int p = 0; p < MAPPING_POSITIONS; p++) {
        const float d = fabsf(mapping_apply(m, (float)p / (MAPPING_POSITIONS - 1)) - v);
        if (d < best_d - 1e-6f) { best_d = d; best = p; }
    }
    return best;
}

bool mapping_is_plain(const mapping_t *m) { return m->min == 0 && m->max_off == 0 && m->curve == CURVE_LIN; }

/* ---------------- user curves ---------------- */

// The table: for each position, the segment it falls in; a stepped segment holds its start point's y, a linear one goes to the next point.
static void rebuild(user_curve_t *u) {
    for (int i = 0; i < CURVE_USER_LUT; i++) {
        const float x = 100.0f * (float)i / (CURVE_USER_LUT - 1);
        float y;
        if (x <= u->pt[0].x) y = u->pt[0].y;
        else if (x >= u->pt[u->n - 1].x) y = u->pt[u->n - 1].y;
        else {
            int j = 0;
            while (j + 1 < u->n && u->pt[j + 1].x <= x) j++;
            const curve_point_t *a = &u->pt[j], *b = &u->pt[j + 1];
            if (a->mode == CURVE_PT_STEP || b->x == a->x) y = a->y;
            else y = a->y + (b->y - a->y) * (x - a->x) / (float)(b->x - a->x);
        }
        u->lut[i] = (uint16_t)(y / 100.0f * 65535.0f + 0.5f);
    }
}

static user_curve_t *editable(int k) { return k >= 0 && k < CURVE_USER_MAX && users[k].used ? &users[k] : NULL; }
static void changed(user_curve_t *u) { rebuild(u); g_rev++; }

void curves_init(void) { memset(users, 0, sizeof users); g_rev++; }

const user_curve_t *curve_user(int k) { return k >= 0 && k < CURVE_USER_MAX ? &users[k] : NULL; }

int curve_user_new(int k) {
    if (k < 0 || k >= CURVE_USER_MAX || users[k].used) return -1;
    user_curve_t *u = &users[k];
    memset(u, 0, sizeof *u);
    u->used = true;
    u->n = 2;
    u->pt[0] = (curve_point_t){0, 0, CURVE_PT_LIN};
    u->pt[1] = (curve_point_t){100, 100, CURVE_PT_LIN};
    changed(u);
    return k;
}

void curve_user_delete(int k) {
    if (!editable(k)) return;
    memset(&users[k], 0, sizeof users[k]);
    g_rev++;
}

int curve_user_add_point(int k, int after) {
    user_curve_t *u = editable(k);
    if (!u || u->n >= CURVE_POINTS_MAX) return -1;
    if (after < 0) after = 0;
    if (after >= u->n) after = u->n - 1;
    curve_point_t p = u->pt[after];
    if (after + 1 < u->n) {                                     // halfway to the next point
        p.x = (uint8_t)((u->pt[after].x + u->pt[after + 1].x) / 2);
        p.y = (uint8_t)((u->pt[after].y + u->pt[after + 1].y) / 2);
    } else if (p.x < 100) p.x = (uint8_t)(p.x + (100 - p.x) / 2);
    for (int i = u->n; i > after + 1; i--) u->pt[i] = u->pt[i - 1];
    u->pt[after + 1] = p;
    u->n++;
    changed(u);
    return after + 1;
}

void curve_user_remove_point(int k, int i) {
    user_curve_t *u = editable(k);
    if (!u || u->n <= 2 || i < 0 || i >= u->n) return;
    for (int j = i; j + 1 < u->n; j++) u->pt[j] = u->pt[j + 1];
    u->n--;
    changed(u);
}

void curve_user_set_point(int k, int i, int x, int y) {
    user_curve_t *u = editable(k);
    if (!u || i < 0 || i >= u->n) return;
    const int lo = i > 0 ? u->pt[i - 1].x : 0, hi = i + 1 < u->n ? u->pt[i + 1].x : 100;
    x = x < lo ? lo : x > hi ? hi : x;
    y = y < 0 ? 0 : y > 100 ? 100 : y;
    if (u->pt[i].x == x && u->pt[i].y == y) return;
    u->pt[i].x = (uint8_t)x; u->pt[i].y = (uint8_t)y;
    changed(u);
}

void curve_user_set_mode(int k, int i, int mode) {
    user_curve_t *u = editable(k);
    if (!u || i < 0 || i >= u->n || u->pt[i].mode == mode) return;
    u->pt[i].mode = (uint8_t)(mode == CURVE_PT_STEP ? CURVE_PT_STEP : CURVE_PT_LIN);
    changed(u);
}

unsigned curves_rev(void) { return g_rev; }

/* ---------------- curves.cfg ----------------
 *   # SynthCore user curves (curves.cfg)
 *   curve 1                       a user curve (U1); its points follow, in order of x
 *   point 0 0 lin                 x y (0..100) and the segment to the next point: lin or step                                      */

int curves_to_text(char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "# SynthCore user curves (curves.cfg)\n");
    for (int k = 0; k < CURVE_USER_MAX && n < cap; k++) {
        const user_curve_t *u = &users[k];
        if (!u->used) continue;
        n += snprintf(buf + n, (size_t)(cap - n), "curve %d\n", k + 1);
        for (int i = 0; i < u->n && n < cap; i++)
            n += snprintf(buf + n, (size_t)(cap - n), "point %d %d %s\n", u->pt[i].x, u->pt[i].y, u->pt[i].mode == CURVE_PT_STEP ? "step" : "lin");
    }
    return n < cap ? n : cap - 1;
}

bool curves_from_text(const char *txt) {
    static user_curve_t got[CURVE_USER_MAX];
    memset(got, 0, sizeof got);
    bool any = false;
    int cur = -1;
    for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        int a, x, y;
        char mode[8];
        if (sscanf(line, "curve %d", &a) == 1) {
            cur = a >= 1 && a <= CURVE_USER_MAX ? a - 1 : -1;
            if (cur >= 0) { memset(&got[cur], 0, sizeof got[cur]); got[cur].used = true; any = true; }
        } else if (cur >= 0 && sscanf(line, "point %d %d %7s", &x, &y, mode) == 3 && got[cur].n < CURVE_POINTS_MAX) {
            user_curve_t *u = &got[cur];
            const int lo = u->n ? u->pt[u->n - 1].x : 0;           // keep the order of x
            x = x < lo ? lo : x > 100 ? 100 : x;
            y = y < 0 ? 0 : y > 100 ? 100 : y;
            u->pt[u->n++] = (curve_point_t){(uint8_t)x, (uint8_t)y, (uint8_t)(!strcmp(mode, "step") ? CURVE_PT_STEP : CURVE_PT_LIN)};
        }
    }
    if (!any) return false;
    for (int k = 0; k < CURVE_USER_MAX; k++) {
        if (got[k].used && got[k].n < 2) {                          // a curve needs two points: complete it
            if (got[k].n == 0) got[k].pt[0] = (curve_point_t){0, 0, CURVE_PT_LIN};
            got[k].pt[1] = (curve_point_t){100, 100, CURVE_PT_LIN};
            got[k].n = 2;
        }
        if (got[k].used) rebuild(&got[k]);
    }
    memcpy(users, got, sizeof users);
    g_rev++;
    return true;
}

static char g_buf[STORAGE_FILE_MAX];

bool curves_load(void) {
    const bool ok = storage_read(CURVES_FILE, g_buf, sizeof g_buf) >= 0 && curves_from_text(g_buf);
    g_saved_rev = g_rev;                                          // loaded, or nothing to load: the current state is the reference
    g_have_saved = true;
    return ok;
}

bool curves_save(void) {
    const int n = curves_to_text(g_buf, sizeof g_buf);
    if (!storage_write(CURVES_FILE, g_buf, n)) return false;
    g_saved_rev = g_rev;
    g_have_saved = true;
    return true;
}

bool curves_changed(void) { return g_have_saved && g_rev != g_saved_rev; }
