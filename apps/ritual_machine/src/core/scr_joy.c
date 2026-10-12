// The JOY tab of the menu: what the joystick's X and Y drive in XY mode (synth_ui.h: synth_ui_joy_*). Axis picks X or Y; Tgt names its
// parameter (bound on the pages: a push on a row); Min / Max (% of the parameter's range, Min > Max inverts) and Curve shape how the stick's
// travel reaches it (core/curves.h); Clear unbinds the axis. XY mode itself is toggled on the pages (Shift + push: the "Joy XY" function).
// On the right: the axis' response (stick left / down to right / up, the parameter bottom to top) and whether XY mode is on.
// Saved in ui.cfg. A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include "core/curves.h"
#include <stdio.h>
#include <string.h>

static int axis(const ui_ctx_t *c) { return c->ui->joy_tab & 1; }
static mapping_t *bound(const ui_ctx_t *c) { mapping_t *m = &c->ui->joy[axis(c)]; return m->t.kind == MACRO_NONE ? NULL : m; }
static bool has_target(const ui_ctx_t *c) { return bound(c) != NULL; }

static void axis_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", axis(c) ? "Y" : "X"); }
static bool axis_adjust(const ui_ctx_t *c, int dir) { (void)dir; c->ui->joy_tab = !axis(c); return false; }

static void tgt_value(const ui_ctx_t *c, char *out, int n) {
    const mapping_t *m = bound(c);
    char name[24];
    if (!m || !synth_ui_target_describe(NULL, NULL, c->rack, &m->t, name, sizeof name, NULL, 0)) snprintf(out, (size_t)n, "--");
    else snprintf(out, (size_t)n, "%s", name);
}

// Min / Max / Curve (arg 0 / 1 / 2) of the axis shown.
static void map_value(const ui_ctx_t *c, char *out, int n) {
    const mapping_t *m = bound(c);
    if (!m) { snprintf(out, (size_t)n, "--"); return; }
    if (c->arg == 2) snprintf(out, (size_t)n, "%s", curve_name(m->curve));
    else snprintf(out, (size_t)n, "%d%%", c->arg == 0 ? m->min : mapping_max(m));
}
static bool map_adjust(const ui_ctx_t *c, int dir) {
    mapping_t *m = bound(c);
    if (!m) return false;
    if (c->arg == 2) { m->curve = (uint8_t)curve_step(m->curve, dir); return false; }
    int v = (c->arg == 0 ? m->min : mapping_max(m)) + 5 * dir;
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    if (c->arg == 0) m->min = (uint8_t)v; else m->max_off = (uint8_t)(100 - v);
    return false;
}

static void clear_activate(const ui_ctx_t *c) {
    memset(&c->ui->joy[axis(c)], 0, sizeof c->ui->joy[0]);
    c->ui->joy_fresh = -1;
    c->ui->joy_rest[axis(c)] = -1.0f;           // XY mode: the axis stops driving at once
}

static const el_def_t elements[] = {
    {"Axis",  EL_VALUE,  0, 0, 1, axis_value, axis_adjust, NULL, NULL, NULL},
    {"Tgt",   EL_VALUE,  1, 0, 1, tgt_value,  NULL,        NULL, NULL, has_target},
    {"Min",   EL_VALUE,  2, 0, 1, map_value,  map_adjust,  NULL, NULL, has_target, NULL, 0},
    {"Max",   EL_VALUE,  3, 0, 1, map_value,  map_adjust,  NULL, NULL, has_target, NULL, 1},
    {"Curve", EL_VALUE,  4, 0, 1, map_value,  map_adjust,  NULL, NULL, has_target, NULL, 2},
    {"Clear", EL_BUTTON, 5, 0, 1, NULL,       NULL,        clear_activate, NULL, has_target},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)area; (void)rect;
    const gui_rect_t box = ui_picture_box(st);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int lh = gui_text_h(g) + 1;
    u8g2_DrawStr(g, box.x + 3, box.y + lh, c->ui->joy_xy ? "XY on" : "XY off");
    const mapping_t *m = bound(c);
    if (!m) return;
    const int x0 = box.x + 3, x1 = box.x + box.w - 4, y0 = box.y + lh + 4, y1 = box.y + box.h - 4;
    if (x1 <= x0 + 4 || y1 <= y0 + 4) return;
    for (int x = x0; x <= x1; x += 3) {                                   // Min and Max levels, dotted
        u8g2_DrawPixel(g, x, y1 - (int)((float)m->min / 100.0f * (float)(y1 - y0)));
        u8g2_DrawPixel(g, x, y1 - (int)((float)mapping_max(m) / 100.0f * (float)(y1 - y0)));
    }
    int px = 0, py = 0;
    for (int x = x0; x <= x1; x++) {
        const float y = mapping_apply(m, (float)(x - x0) / (float)(x1 - x0));
        const int yy = y1 - (int)(y * (float)(y1 - y0) + 0.5f);
        if (x > x0) u8g2_DrawLine(g, px, py, x, yy);
        px = x; py = yy;
    }
}

const screen_def_t scr_joy_screen = {elements, N_ELEMENTS, false, false, layout, NULL, NULL, draw_extra};
