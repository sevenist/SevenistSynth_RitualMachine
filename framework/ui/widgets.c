// Widgets for a pointer and a keyboard (widgets.h).
#include "ui/widgets.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WG_PI 3.14159265f
#define DRAG_START_PX 4         // the pointer moves this far with the press before a drag and drop starts
#define ACTIVE_DND (-1)         // `active` while a payload travels: no widget takes the pointer

const wg_theme_t wg_default_theme = {
    .bg = CANVAS_HEX(0x14181D), .panel = CANVAS_HEX(0x1D232A), .panel2 = CANVAS_HEX(0x2A323C), .line = CANVAS_HEX(0x3C4855),
    .text = CANVAS_HEX(0xDCE2E8), .dim = CANVAS_HEX(0x7D8996), .accent = CANVAS_HEX(0x6CC3FF), .warn = CANVAS_HEX(0xFF6B5E),
    .font = u8g2_font_6x10_tr, .font_small = u8g2_font_5x7_tr,
};

bool wg_key_pressed(const wg_input_t *in, int key) {
    for (int i = 0; i < in->nkeys; i++)
        if (in->keys[i].key == key && in->keys[i].down) return true;
    return false;
}

static bool inside(gui_rect_t r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
static bool shift(const wg_ctx_t *c) { return (c->in->mods & WG_MOD_SHIFT) != 0; }
static float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

void wg_init(wg_ctx_t *c, canvas_t *cv) {
    memset(c, 0, sizeof *c);
    c->cv = cv;
    c->g = canvas_u8g2(cv);
    c->theme = wg_default_theme;
}

void wg_begin(wg_ctx_t *c, const wg_input_t *in) {
    c->in = in;
    c->hot = 0;
    c->focus_seen = false;
    memcpy(c->prev_order, c->order, sizeof c->order);
    c->prev_norder = c->norder;
    c->norder = 0;
    c->tab_move = 0;
    c->hint[0] = 0;
    for (int i = 0; i < in->nkeys; i++)
        if (in->keys[i].down && in->keys[i].key == WG_KEY_TAB) c->tab_move += (in->mods & WG_MOD_SHIFT) ? -1 : 1;
    if (wg_key_pressed(in, WG_KEY_ESC)) c->focus = 0;
    c->dnd_dropped = c->dnd_kind != WG_DND_NONE && (in->released & WG_BTN_LEFT);
    u8g2_SetFontMode(c->g, 1);
    u8g2_SetDrawColor(c->g, 1);
}

static void draw_dnd(wg_ctx_t *c) {
    const wg_theme_t *t = &c->theme;
    wg_line(c, c->dnd_x, c->dnd_y, c->in->x, c->in->y, t->accent);
    const int w = wg_text_w(c, c->dnd_label, true) + 6;
    const gui_rect_t box = gui_rect(c->in->x + 8, c->in->y + 6, w, 11);
    wg_fill(c, box, t->accent);
    wg_text_small(c, box.x + 3, box.y + 8, c->dnd_label, t->bg);
}

void wg_end(wg_ctx_t *c) {
    const wg_input_t *in = c->in;
    if (c->dnd_kind != WG_DND_NONE && !c->dnd_dropped) draw_dnd(c);
    if (c->tab_move && c->norder > 0) {                     // Tab: the next / previous widget of this frame's order
        int at = -1;
        for (int i = 0; i < c->norder; i++) if (c->order[i] == c->focus) at = i;
        if (at < 0) at = c->tab_move > 0 ? -1 : c->norder;
        at = ((at + c->tab_move) % c->norder + c->norder) % c->norder;
        c->focus = c->order[at];
    } else if (c->focus && !c->focus_seen) {
        c->focus = 0;                                       // (the focused widget is no longer shown)
    }
    if ((in->pressed & WG_BTN_LEFT) && c->active == 0) c->focus = 0;   // a press on nothing
    if (in->released & WG_BTN_LEFT) c->active = 0;
    if (c->dnd_dropped) { c->dnd_kind = WG_DND_NONE; c->dnd_dropped = false; if (c->active == ACTIVE_DND) c->active = 0; }
}

/* ---------------- drawing ---------------- */

static void ink(wg_ctx_t *c, uint16_t color) {
    canvas_color(c->cv, color, c->theme.bg);
    u8g2_SetDrawColor(c->g, 1);
}

void wg_fill(wg_ctx_t *c, gui_rect_t r, uint16_t color) { canvas_fill(c->cv, r.x, r.y, r.w, r.h, color); }

void wg_frame(wg_ctx_t *c, gui_rect_t r, uint16_t color) {
    canvas_fill(c->cv, r.x, r.y, r.w, 1, color);
    canvas_fill(c->cv, r.x, r.y + r.h - 1, r.w, 1, color);
    canvas_fill(c->cv, r.x, r.y, 1, r.h, color);
    canvas_fill(c->cv, r.x + r.w - 1, r.y, 1, r.h, color);
}

void wg_text(wg_ctx_t *c, int x, int baseline, const char *s, uint16_t color) {
    ink(c, color);
    u8g2_SetFont(c->g, c->theme.font);
    u8g2_DrawStr(c->g, (u8g2_uint_t)x, (u8g2_uint_t)baseline, s);
}

void wg_text_small(wg_ctx_t *c, int x, int baseline, const char *s, uint16_t color) {
    ink(c, color);
    u8g2_SetFont(c->g, c->theme.font_small);
    u8g2_DrawStr(c->g, (u8g2_uint_t)x, (u8g2_uint_t)baseline, s);
}

int wg_text_w(wg_ctx_t *c, const char *s, bool small) {
    u8g2_SetFont(c->g, small ? c->theme.font_small : c->theme.font);
    return u8g2_GetStrWidth(c->g, s);
}

void wg_text_center(wg_ctx_t *c, gui_rect_t r, const char *s, uint16_t color, bool small) {
    ink(c, color);
    u8g2_SetFont(c->g, small ? c->theme.font_small : c->theme.font);
    const int w = u8g2_GetStrWidth(c->g, s), asc = u8g2_GetAscent(c->g);
    u8g2_DrawStr(c->g, (u8g2_uint_t)(r.x + (r.w - w) / 2), (u8g2_uint_t)(r.y + (r.h + asc) / 2), s);
}

// Where a point lies against the canvas (Cohen-Sutherland's outcode): 0 inside.
static int outcode(const wg_ctx_t *c, int x, int y) {
    return (x < 0 ? 1 : 0) | (x >= c->cv->w ? 2 : 0) | (y < 0 ? 4 : 0) | (y >= c->cv->h ? 8 : 0);
}

// The line clipped to the canvas first: u8g2 takes unsigned coordinates, so an end above or left of the canvas (negative) would wrap
// to a far-away point and draw long horizontal or vertical strokes (a cable whose module is scrolled off the top or the left).
void wg_line(wg_ctx_t *c, int x0, int y0, int x1, int y1, uint16_t color) {
    const int xmax = c->cv->w - 1, ymax = c->cv->h - 1;
    int a = outcode(c, x0, y0), b = outcode(c, x1, y1);
    while (a | b) {
        if (a & b) return;                                  // both ends on the same outer side: nothing to draw
        const int out = a ? a : b;
        long long x = 0, y = 0;
        const long long dx = (long long)x1 - x0, dy = (long long)y1 - y0;
        if (out & 8)      { y = ymax; x = x0 + dx * (ymax - y0) / dy; }
        else if (out & 4) { y = 0;    x = x0 + dx * (0 - y0) / dy; }
        else if (out & 2) { x = xmax; y = y0 + dy * (xmax - x0) / dx; }
        else              { x = 0;    y = y0 + dy * (0 - x0) / dx; }
        if (out == a) { x0 = (int)x; y0 = (int)y; a = outcode(c, x0, y0); }
        else          { x1 = (int)x; y1 = (int)y; b = outcode(c, x1, y1); }
    }
    ink(c, color);
    u8g2_DrawLine(c->g, (u8g2_uint_t)x0, (u8g2_uint_t)y0, (u8g2_uint_t)x1, (u8g2_uint_t)y1);
}

static void pixel(wg_ctx_t *c, int x, int y, uint16_t color) {
    if (x >= 0 && y >= 0 && x < c->cv->w && y < c->cv->h) c->cv->px[y * c->cv->w + x] = color;
}

void wg_arc(wg_ctx_t *c, int cx, int cy, int radius, float a0, float a1, int thick, uint16_t color) {
    if (a1 < a0) { const float t = a0; a0 = a1; a1 = t; }
    for (int k = 0; k < thick; k++) {
        const float r = (float)(radius - k);
        if (r <= 0.f) break;
        const float step = 0.5f / r;
        for (float a = a0; a <= a1 + step * 0.5f; a += step)
            pixel(c, cx + (int)lroundf(r * cosf(a)), cy - (int)lroundf(r * sinf(a)), color);
    }
}

void wg_focus_ring(wg_ctx_t *c, gui_rect_t r) {
    for (int x = r.x; x < r.x + r.w; x += 2) { pixel(c, x, r.y, c->theme.accent); pixel(c, x, r.y + r.h - 1, c->theme.accent); }
    for (int y = r.y; y < r.y + r.h; y += 2) { pixel(c, r.x, y, c->theme.accent); pixel(c, r.x + r.w - 1, y, c->theme.accent); }
}

uint16_t wg_mix(uint16_t a, uint16_t b, int t) {
    const int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31, rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
    const int r = ra + (rb - ra) * t / 256, g = ga + (gb - ga) * t / 256, bl = ba + (bb - ba) * t / 256;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* ---------------- state ---------------- */

bool wg_hover(const wg_ctx_t *c, gui_rect_t r) {
    return c->active == 0 && c->dnd_kind == WG_DND_NONE && inside(r, c->in->x, c->in->y);
}

void wg_focusable(wg_ctx_t *c, int id) {
    if (c->norder < WG_ORDER_MAX) c->order[c->norder++] = id;
    if (id == c->focus) c->focus_seen = true;
}

bool wg_has_focus(const wg_ctx_t *c, int id) { return id != 0 && c->focus == id; }
void wg_set_focus(wg_ctx_t *c, int id) { c->focus = id; c->focus_seen = true; }

bool wg_behavior(wg_ctx_t *c, int id, gui_rect_t r, bool focusable) {
    if (focusable) wg_focusable(c, id);
    const wg_input_t *in = c->in;
    const bool over = inside(r, in->x, in->y) && c->dnd_kind == WG_DND_NONE;
    if (over && (c->active == 0 || c->active == id)) c->hot = id;
    if (over && c->active == 0 && (in->pressed & WG_BTN_LEFT)) {
        c->active = id;
        if (focusable) wg_set_focus(c, id);
        c->press_x = in->x;
        c->press_y = in->y;
        c->press_part = 0;
    }
    return c->active == id;
}

wg_keys_t wg_focus_keys(wg_ctx_t *c, int id) {
    wg_keys_t k = {0, false, false, false, false};
    if (!wg_has_focus(c, id)) return k;
    const wg_input_t *in = c->in;
    for (int i = 0; i < in->nkeys; i++) {
        if (!in->keys[i].down) continue;
        switch (in->keys[i].key) {
        case WG_KEY_UP: case WG_KEY_RIGHT: k.steps++; break;
        case WG_KEY_DOWN: case WG_KEY_LEFT: k.steps--; break;
        case WG_KEY_PGUP: k.steps += 10; break;
        case WG_KEY_PGDN: k.steps -= 10; break;
        case WG_KEY_HOME: k.home = true; break;
        case WG_KEY_END: k.end = true; break;
        case WG_KEY_DELETE: case WG_KEY_BACKSPACE: k.reset = true; break;
        case WG_KEY_ENTER: k.enter = true; break;
        default: break;
        }
    }
    return k;
}

void wg_set_hint(wg_ctx_t *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->hint, sizeof c->hint, fmt, ap);
    va_end(ap);
}

/* ---------------- widgets ---------------- */

static bool clicked(wg_ctx_t *c, int id, gui_rect_t r) {
    return c->active == id && (c->in->released & WG_BTN_LEFT) && inside(r, c->in->x, c->in->y);
}

bool wg_button(wg_ctx_t *c, int id, gui_rect_t r, const char *label, bool selected) {
    const wg_theme_t *t = &c->theme;
    wg_behavior(c, id, r, true);
    bool hit = clicked(c, id, r) || wg_focus_keys(c, id).enter;
    const bool hot = c->hot == id;
    uint16_t bg = selected ? t->accent : (hot ? t->line : t->panel2);
    if (c->active == id && !selected) bg = t->dim;
    wg_fill(c, r, bg);
    wg_text_center(c, r, label, selected ? t->bg : t->text, false);
    if (wg_has_focus(c, id)) wg_focus_ring(c, gui_inset(r, -2));
    return hit;
}

bool wg_tab(wg_ctx_t *c, int id, gui_rect_t r, const char *label, bool selected) {
    const wg_theme_t *t = &c->theme;
    wg_behavior(c, id, r, true);
    const bool hit = clicked(c, id, r) || wg_focus_keys(c, id).enter;
    wg_fill(c, r, selected ? t->panel2 : (c->hot == id ? t->panel : t->bg));
    if (selected) wg_fill(c, gui_rect(r.x, r.y + r.h - 2, r.w, 2), t->accent);
    wg_text_center(c, r, label, selected ? t->text : t->dim, false);
    if (wg_has_focus(c, id)) wg_focus_ring(c, gui_inset(r, -1));
    return hit;
}

int wg_stepper(wg_ctx_t *c, int id, gui_rect_t r, const char *text) {
    const wg_theme_t *t = &c->theme;
    wg_behavior(c, id, r, true);
    int step = 0;
    if (clicked(c, id, r)) step = c->in->x < r.x + r.w / 2 ? -1 : 1;
    if (c->hot == id && c->in->wheel) step += c->in->wheel > 0 ? 1 : -1;
    step += wg_focus_keys(c, id).steps;
    wg_fill(c, r, c->hot == id ? t->line : t->panel2);
    wg_text_small(c, r.x + 3, r.y + (r.h + 6) / 2, "<", t->dim);
    wg_text_small(c, r.x + r.w - 8, r.y + (r.h + 6) / 2, ">", t->dim);
    wg_text_center(c, gui_inset(r, 8), text, t->text, false);
    if (wg_has_focus(c, id)) wg_focus_ring(c, gui_inset(r, -2));
    return step;
}

static float knob_angle(float v) { return 1.25f * WG_PI - v * 1.5f * WG_PI; }   // 0 at the bottom left, 1 at the bottom right

static float quantize(float v, int steps) {
    v = clamp01(v);
    if (steps > 1) v = floorf(v * (float)(steps - 1) + 0.5f) / (float)(steps - 1);
    return v;
}

int wg_knob(wg_ctx_t *c, int id, gui_rect_t r, float *v, const wg_knob_t *k) {
    const wg_theme_t *t = &c->theme;
    const wg_input_t *in = c->in;
    int R = (r.w < r.h - 16 ? r.w : r.h - 16) / 2 - 4;
    if (R < 4) R = 4;
    const int cx = r.x + r.w / 2, cy = r.y + R + 5;
    const float step = k->steps > 1 ? 1.f / (float)(k->steps - 1) : (shift(c) ? 0.002f : 0.01f);
    int changed = 0;
    const float before = *v;
    const bool was_active = c->active == id;

    const bool active = wg_behavior(c, id, r, true);
    float *ring_v = k->ring ? (shift(c) ? k->ring_a : k->ring_b) : NULL;
    if (active && !was_active) {                            // the press: on the ring or on the knob
        const float dx = (float)(in->x - cx), dy = (float)(in->y - cy), d = sqrtf(dx * dx + dy * dy);
        c->press_part = (k->ring && ring_v && d >= (float)R + 0.5f && d <= (float)R + 6.f) ? 1 : 0;
        c->press_v = c->press_part ? *ring_v : *v;
        if (in->double_click && c->press_part == 0) { *v = k->def; c->press_v = k->def; }
    }
    if (active && (in->buttons & WG_BTN_LEFT)) {            // dragging: up = more (relative, Shift finer)
        const float sens = shift(c) ? 0.001f : 0.005f;
        c->press_v += (float)(c->press_y - in->y) * sens;
        c->press_v = clamp01(c->press_v);
        c->press_y = in->y;
        if (c->press_part == 1 && ring_v) {
            const float nv = clamp01(c->press_v);
            if (nv != *ring_v) { *ring_v = nv; changed |= WG_RING_CHANGED; }
        } else {
            *v = quantize(c->press_v, k->steps);
        }
    }
    if (c->hot == id && in->wheel && c->active == 0) *v = quantize(*v + (float)in->wheel * step, k->steps);
    const wg_keys_t keys = wg_focus_keys(c, id);
    if (keys.steps) *v = quantize(*v + (float)keys.steps * step, k->steps);
    if (keys.home) *v = 0.f;
    if (keys.end) *v = 1.f;
    if (keys.reset) *v = k->def;
    if (*v != before) changed |= WG_CHANGED;

    // drawing: the track, the value's arc, the ring, the pointer, the label and the value
    const bool hot = c->hot == id, focus = wg_has_focus(c, id);
    const uint16_t col = k->color ? k->color : t->accent;
    const float a0 = knob_angle(0.f), av = knob_angle(*v), a1 = knob_angle(1.f);
    u8g2_t *g = c->g;
    ink(c, hot || active ? t->line : t->panel2);
    u8g2_DrawDisc(g, (u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)(R - 3), U8G2_DRAW_ALL);
    wg_arc(c, cx, cy, R, a1, a0, 2, t->line);
    wg_arc(c, cx, cy, R, av, a0, 2, col);
    if (k->ring && k->ring_a && k->ring_b) {
        const bool ring_hot = hot && c->active == 0 && c->press_part == 0;
        wg_arc(c, cx, cy, R + 3, knob_angle(*k->ring_a), knob_angle(*k->ring_b), (ring_hot || (active && c->press_part == 1)) ? 3 : 2, k->ring_color);
        const float ab = knob_angle(*k->ring_b);            // the end the drag moves: a dot
        pixel(c, cx + (int)lroundf((float)(R + 5) * cosf(ab)), cy - (int)lroundf((float)(R + 5) * sinf(ab)), t->text);
    }
    wg_line(c, cx, cy, cx + (int)lroundf((float)(R - 2) * cosf(av)), cy - (int)lroundf((float)(R - 2) * sinf(av)), t->text);
    for (int i = 0; i < k->ndots && i < 8; i++)
        canvas_fill(c->cv, r.x + r.w - 4 - i * 4, r.y, 3, 3, k->dots[i]);
    const int ly = cy + R + 9;
    if (k->label) wg_text_center(c, gui_rect(r.x, ly - 7, r.w, 8), k->label, focus || active ? t->text : t->dim, true);
    if (k->value) wg_text_center(c, gui_rect(r.x, ly + 1, r.w, 8), k->value, active || hot ? col : t->text, true);
    if (focus) wg_focus_ring(c, r);
    if (hot && k->label) wg_set_hint(c, "%s  %s", k->label, k->value ? k->value : "");
    return changed;
}

bool wg_slider(wg_ctx_t *c, int id, gui_rect_t r, float *v, float def, const char *text, uint16_t color) {
    const wg_theme_t *t = &c->theme;
    const wg_input_t *in = c->in;
    const float before = *v;
    const bool was_active = c->active == id;
    const bool active = wg_behavior(c, id, r, true);
    if (active && !was_active) {
        c->press_v = *v;
        if (in->double_click) { *v = def; c->press_v = def; }
    }
    if (active && (in->buttons & WG_BTN_LEFT)) {
        c->press_v += (float)(in->x - c->press_x) / (float)(r.w > 1 ? r.w : 1) * (shift(c) ? 0.2f : 1.f);
        c->press_x = in->x;
        *v = clamp01(c->press_v);
    }
    const float step = shift(c) ? 0.002f : 0.01f;
    if (c->hot == id && in->wheel && c->active == 0) *v = clamp01(*v + (float)in->wheel * step);
    const wg_keys_t keys = wg_focus_keys(c, id);
    if (keys.steps) *v = clamp01(*v + (float)keys.steps * step);
    if (keys.home) *v = 0.f;
    if (keys.end) *v = 1.f;
    if (keys.reset) *v = def;
    wg_fill(c, r, c->hot == id ? t->line : t->panel2);
    wg_fill(c, gui_rect(r.x, r.y, (int)lroundf(*v * (float)r.w), r.h), wg_mix(t->panel2, color ? color : t->accent, 110));
    wg_fill(c, gui_rect(r.x + (int)lroundf(*v * (float)(r.w - 1)), r.y, 1, r.h), color ? color : t->accent);
    wg_text_center(c, r, text, t->text, true);
    if (wg_has_focus(c, id)) wg_focus_ring(c, gui_inset(r, -2));
    return *v != before;
}

/* ---------------- drag and drop ---------------- */

void wg_drag_source(wg_ctx_t *c, int id, int kind, int value, const char *label) {
    const wg_input_t *in = c->in;
    if (c->active != id || !(in->buttons & WG_BTN_LEFT) || c->dnd_kind != WG_DND_NONE) return;
    if (abs(in->x - c->press_x) + abs(in->y - c->press_y) < DRAG_START_PX) return;
    c->dnd_kind = kind;
    c->dnd_value = value;
    c->dnd_from = id;
    c->dnd_x = c->press_x;
    c->dnd_y = c->press_y;
    snprintf(c->dnd_label, sizeof c->dnd_label, "%s", label ? label : "");
    c->active = ACTIVE_DND;
}

bool wg_drop_target(wg_ctx_t *c, gui_rect_t r, int kind, int *value) {
    if (c->dnd_kind != kind || !inside(r, c->in->x, c->in->y)) return false;
    if (c->dnd_dropped) { if (value) *value = c->dnd_value; return true; }
    wg_frame(c, r, c->theme.accent);
    wg_frame(c, gui_inset(r, 1), c->theme.accent);
    return false;
}

bool wg_dragging(const wg_ctx_t *c, int kind) { return c->dnd_kind == kind; }
