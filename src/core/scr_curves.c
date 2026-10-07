// The CURVES tab of the menu: the user curves U1..U8 (core/curves.h), chosen by any mapping's Curve row (MODIFIERS, MACROS).
// Curve picks a slot; New makes an empty slot a straight line; Point picks a point; X / Y move it (or the two knobs CURVES_KNOB_X / _Y of
// bindings.h, absolute 0..100); Seg says whether the segment after the point is linear or stepped (held until the next point); Add puts a
// point halfway to the next one; Del pt removes the point (two stay); Delete removes the curve (mappings using it then read it as linear).
// On the right: the curve, its points, the selected one as a box. Saved in curves.cfg by itself (app.c).
// A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include "core/curves.h"
#include <stdio.h>

static int k_of(const ui_ctx_t *c) { return c->ui->curve_cur % CURVE_USER_MAX; }
static const user_curve_t *cu(const ui_ctx_t *c) { const user_curve_t *u = curve_user(k_of(c)); return u && u->used ? u : NULL; }
static int pt_of(const ui_ctx_t *c) {
    const user_curve_t *u = cu(c);
    if (!u) return 0;
    if (c->ui->curve_pt >= u->n) c->ui->curve_pt = u->n - 1;
    if (c->ui->curve_pt < 0) c->ui->curve_pt = 0;
    return c->ui->curve_pt;
}

static bool exists(const ui_ctx_t *c) { return cu(c) != NULL; }
static bool empty(const ui_ctx_t *c)  { return cu(c) == NULL; }

static void curve_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s%s", curve_name(CURVE_USER_FIRST + k_of(c)), exists(c) ? "" : " --"); }
static bool curve_adjust(const ui_ctx_t *c, int dir) { c->ui->curve_cur = (c->ui->curve_cur + dir + CURVE_USER_MAX) % CURVE_USER_MAX; c->ui->curve_pt = 0; return false; }
static void new_activate(const ui_ctx_t *c) { curve_user_new(k_of(c)); c->ui->curve_pt = 0; }

static void pt_value(const ui_ctx_t *c, char *out, int n) { const user_curve_t *u = cu(c); if (u) snprintf(out, (size_t)n, "%d/%d", pt_of(c) + 1, u->n); else snprintf(out, (size_t)n, "--"); }
static bool pt_adjust(const ui_ctx_t *c, int dir) { const user_curve_t *u = cu(c); if (u) c->ui->curve_pt = (pt_of(c) + dir + u->n) % u->n; return false; }

// X / Y (arg 0 / 1) of the selected point, in steps of 1.
static void xy_value(const ui_ctx_t *c, char *out, int n) {
    const user_curve_t *u = cu(c);
    if (!u) { snprintf(out, (size_t)n, "--"); return; }
    snprintf(out, (size_t)n, "%d", c->arg == 0 ? u->pt[pt_of(c)].x : u->pt[pt_of(c)].y);
}
static bool xy_adjust(const ui_ctx_t *c, int dir) {
    const user_curve_t *u = cu(c);
    if (!u) return false;
    const curve_point_t p = u->pt[pt_of(c)];
    curve_user_set_point(k_of(c), pt_of(c), p.x + (c->arg == 0 ? dir : 0), p.y + (c->arg == 1 ? dir : 0));
    return false;
}

static void seg_value(const ui_ctx_t *c, char *out, int n) {
    const user_curve_t *u = cu(c);
    snprintf(out, (size_t)n, "%s", !u ? "--" : u->pt[pt_of(c)].mode == CURVE_PT_STEP ? "Step" : "Lin");
}
static bool seg_adjust(const ui_ctx_t *c, int dir) {
    (void)dir;
    const user_curve_t *u = cu(c);
    if (u) curve_user_set_mode(k_of(c), pt_of(c), u->pt[pt_of(c)].mode == CURVE_PT_STEP ? CURVE_PT_LIN : CURVE_PT_STEP);
    return false;
}

static bool can_add(const ui_ctx_t *c) { const user_curve_t *u = cu(c); return u && u->n < CURVE_POINTS_MAX; }
static void add_activate(const ui_ctx_t *c) { const int i = curve_user_add_point(k_of(c), pt_of(c)); if (i >= 0) c->ui->curve_pt = i; }
static bool can_del_pt(const ui_ctx_t *c) { const user_curve_t *u = cu(c); return u && u->n > 2; }
static void del_pt_activate(const ui_ctx_t *c) { curve_user_remove_point(k_of(c), pt_of(c)); pt_of(c); }
static void delete_activate(const ui_ctx_t *c) { curve_user_delete(k_of(c)); c->ui->curve_pt = 0; }

static const el_def_t elements[] = {
    {"Curve",  EL_VALUE,  0, 0, 1, curve_value, curve_adjust, NULL,            NULL, NULL},
    {"New",    EL_BUTTON, 1, 0, 1, NULL,        NULL,         new_activate,    NULL, empty},
    {"Point",  EL_VALUE,  2, 0, 1, pt_value,    pt_adjust,    NULL,            NULL, exists},
    {"X",      EL_VALUE,  3, 0, 1, xy_value,    xy_adjust,    NULL,            NULL, exists, NULL, 0},
    {"Y",      EL_VALUE,  4, 0, 1, xy_value,    xy_adjust,    NULL,            NULL, exists, NULL, 1},
    {"Seg",    EL_VALUE,  5, 0, 1, seg_value,   seg_adjust,   NULL,            NULL, exists},
    {"Add",    EL_BUTTON, 6, 0, 1, NULL,        NULL,         add_activate,    NULL, can_add},
    {"Del pt", EL_BUTTON, 7, 0, 1, NULL,        NULL,         del_pt_activate, NULL, can_del_pt},
    {"Delete", EL_BUTTON, 8, 0, 1, NULL,        NULL,         delete_activate, NULL, exists},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

// The curve (as a mapping reads it: through its table), the points as dots, the selected one as a box.
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)area; (void)rect;
    const gui_rect_t box = ui_picture_box(st);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const user_curve_t *u = cu(c);
    if (!u) { gui_draw_text_centered(g, gui_rect(box.x, box.y + box.h / 2 - 4, box.w, 9), "empty: New"); return; }
    const int x0 = box.x + 3, x1 = box.x + box.w - 4, y0 = box.y + 3, y1 = box.y + box.h - 4;
    if (x1 <= x0 + 4 || y1 <= y0 + 4) return;
    const int id = CURVE_USER_FIRST + k_of(c);
    int px = 0, py = 0;
    for (int x = x0; x <= x1; x++) {
        const int yy = y1 - (int)(curve_eval(id, (float)(x - x0) / (float)(x1 - x0)) * (float)(y1 - y0) + 0.5f);
        if (x > x0) u8g2_DrawLine(g, px, py, x, yy);
        px = x; py = yy;
    }
    for (int i = 0; i < u->n; i++) {
        const int x = x0 + u->pt[i].x * (x1 - x0) / 100, y = y1 - u->pt[i].y * (y1 - y0) / 100;
        if (i == pt_of(c)) u8g2_DrawFrame(g, x - 2, y - 2, 5, 5);
        else               u8g2_DrawBox(g, x - 1, y - 1, 3, 3);
    }
}

const screen_def_t scr_curves_screen = {elements, N_ELEMENTS, false, false, layout, NULL, NULL, draw_extra};
