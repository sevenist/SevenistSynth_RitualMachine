// The Ritual Machine's curves (curves.h): one bank of user curves, the library's (framework/control/curve.h), and its file on the card.
#include "core/curves.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static crv_bank_t g_bank;
static unsigned   g_saved_rev;
static bool       g_have_saved;

float       curve_eval(int id, float x) { return crv_eval(&g_bank, id, x); }
const char *curve_name(int id) { return crv_name(id); }
int         curve_by_name(const char *name) { return crv_by_name(name); }
int         curve_count(void) { return crv_count(&g_bank); }
int         curve_at(int i) { return crv_at(&g_bank, i); }
int         curve_step(int id, int dir) { return crv_step(&g_bank, id, dir); }

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

void                curves_init(void) { crv_init(&g_bank); }
const user_curve_t *curve_user(int k) { return k >= 0 && k < CURVE_USER_MAX ? &g_bank.user[k] : NULL; }
int      curve_user_new(int k) { return crv_user_new(&g_bank, k); }
void     curve_user_delete(int k) { crv_user_delete(&g_bank, k); }
int      curve_user_add_point(int k, int after) { return crv_user_add_point(&g_bank, k, after); }
void     curve_user_remove_point(int k, int i) { crv_user_remove_point(&g_bank, k, i); }
void     curve_user_set_point(int k, int i, int x, int y) { crv_user_set_point(&g_bank, k, i, x, y); }
void     curve_user_set_mode(int k, int i, int mode) { crv_user_set_mode(&g_bank, k, i, mode); }
unsigned curves_rev(void) { return g_bank.rev; }

/* ---------------- curves.cfg (the library's text, framework/control/curve.h) ---------------- */

int  curves_to_text(char *buf, int cap) { return crv_to_text(&g_bank, "# SynthCore user curves (curves.cfg)", buf, cap); }
bool curves_from_text(const char *txt) { return crv_from_text(&g_bank, txt); }

#define g_buf g_storage_buf                      // the shared settings buffer (hal_storage.h)

bool curves_load(void) {
    const bool ok = storage_read(CURVES_FILE, g_buf, STORAGE_FILE_MAX) >= 0 && curves_from_text(g_buf);
    g_saved_rev = g_bank.rev;                                          // loaded, or nothing to load: the current state is the reference
    g_have_saved = true;
    return ok;
}

bool curves_save(void) {
    const int n = curves_to_text(g_buf, STORAGE_FILE_MAX);
    if (!storage_write(CURVES_FILE, g_buf, n)) return false;
    g_saved_rev = g_bank.rev;
    g_have_saved = true;
    return true;
}

bool curves_changed(void) { return g_have_saved && g_bank.rev != g_saved_rev; }
