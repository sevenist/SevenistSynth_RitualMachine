#include "core/ui_widgets.h"
#include <stdio.h>
#include <string.h>

#define SMALL u8g2_font_4x6_tr              // the ports' and chips' font

// Text drawn transparent (so a fill behind it stays), in a colour; the ink and the font mode put back.
void uw_text(u8g2_t *g, int x, int baseline, const char *s, ui_colour_t c) {
    const uint8_t was = g->font_decode.is_transparent;
    u8g2_SetFontMode(g, 1);
    ui_ink(g, c);
    u8g2_DrawStr(g, x, baseline, s);
    ui_ink(g, UC_TEXT);
    u8g2_SetFontMode(g, was);
}

/* ---------------- sliding text ---------------- */

#define SLIDE_HOLD_MS 1000
#define SLIDE_PX_S    25
#define SLIDE_GAP     16
#define SLIDES        4                     // texts sliding at once (a row's label and value, the strip ...)
static uint32_t g_now;
static struct { uint32_t key, since; bool seen; } g_slide[SLIDES];
static bool g_moving, g_moving_last;

void uw_frame_begin(uint32_t now_ms) {
    g_now = now_ms;
    g_moving_last = g_moving;
    g_moving = false;
    for (int i = 0; i < SLIDES; i++) { if (!g_slide[i].seen) g_slide[i].key = 0; g_slide[i].seen = false; }   // (gone from the screen: forgotten)
}

bool uw_marquee_moving(void) { return g_moving || g_moving_last; }

static uint32_t slide_key(int x, int base, const char *s) {
    uint32_t h = 2166136261u ^ (uint32_t)(x * 31 + base * 7919);
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h ? h : 1;
}

static uint32_t slide_since(uint32_t key) {                 // when this text started sliding (now: new)
    int free_i = -1;
    for (int i = 0; i < SLIDES; i++) {
        if (g_slide[i].key == key) { g_slide[i].seen = true; return g_slide[i].since; }
        if (!g_slide[i].key && free_i < 0) free_i = i;
    }
    if (free_i < 0) free_i = 0;
    g_slide[free_i].key = key; g_slide[free_i].since = g_now; g_slide[free_i].seen = true;
    return g_now;
}

void uw_marquee(u8g2_t *g, int x, int base, int w, const char *s, ui_colour_t c, bool moving) {
    const int tw = u8g2_GetStrWidth(g, s);
    if (tw <= w || w <= 0) { uw_text(g, x, base, s, c); return; }
    int off = 0;
    if (moving) {
        g_moving = true;
        const uint32_t t = g_now - slide_since(slide_key(x, base, s));
        const uint32_t run = (uint32_t)(tw + SLIDE_GAP) * 1000u / SLIDE_PX_S, phase = t % (SLIDE_HOLD_MS + run);
        if (phase > SLIDE_HOLD_MS) off = (int)((phase - SLIDE_HOLD_MS) * SLIDE_PX_S / 1000u);
    }
    const int asc = u8g2_GetAscent(g), desc = u8g2_GetDescent(g);
    const u8g2_uint_t cx0 = g->clip_x0, cy0 = g->clip_y0, cx1 = g->clip_x1, cy1 = g->clip_y1;     // (the window inside the clip in use)
    const int x0 = x > cx0 ? x : cx0, x1 = x + w < cx1 ? x + w : cx1, y0 = base - asc > cy0 ? base - asc : cy0, y1 = base - desc + 1 < cy1 ? base - desc + 1 : cy1;
    if (x1 <= x0 || y1 <= y0) return;
    u8g2_SetClipWindow(g, (u8g2_uint_t)x0, (u8g2_uint_t)y0, (u8g2_uint_t)x1, (u8g2_uint_t)y1);
    uw_text(g, x - off, base, s, c);
    if (moving) uw_text(g, x - off + tw + SLIDE_GAP, base, s, c);
    u8g2_SetClipWindow(g, cx0, cy0, cx1, cy1);
}

static void pair_overflow(u8g2_t *g, gui_rect_t r, int base, const char *label, const char *val, ui_colour_t lc, ui_colour_t vc, bool moving);
static bool pair_fits(u8g2_t *g, gui_rect_t r, const char *label, const char *val);

static int baseline_mid(u8g2_t *g, gui_rect_t r) { return r.y + (r.h - (u8g2_GetAscent(g) - u8g2_GetDescent(g))) / 2 + u8g2_GetAscent(g); }

void uw_line(u8g2_t *g, int x0, int y0, int x1, int y1, uw_cable_t k) {
    const bool col = ui_colour(g);
    int on = 1, off = 0;
    if (k == UW_CABLE_MOD) { on = col ? 3 : 1; off = col ? 2 : 1; }
    if (k == UW_CABLE_GATE) { on = 1; off = 2; }
    if (!gui_clip_line_to(g, &x0, &y0, &x1, &y1)) return;      // (off the screen: no wrap-around in u8g2's unsigned coordinates)
    ui_ink(g, k == UW_CABLE_MOD ? UC_MOD : k == UW_CABLE_GATE ? UC_GATE : UC_TEXT);
    if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { const int t = y0; y0 = y1; y1 = t; }
    if (!off) {
        if (y0 == y1) u8g2_DrawHLine(g, x0, y0, x1 - x0 + 1); else u8g2_DrawVLine(g, x0, y0, y1 - y0 + 1);
    } else if (y0 == y1) {
        for (int x = x0; x <= x1; x++) if ((x % (on + off)) < on) u8g2_DrawPixel(g, x, y0);     // (the pattern follows the screen, so crossings line up)
    } else {
        for (int y = y0; y <= y1; y++) if ((y % (on + off)) < on) u8g2_DrawPixel(g, x0, y);
    }
    ui_ink(g, UC_TEXT);
}

static void uw_tile_body(u8g2_t *g, gui_rect_t r, const char *name, ui_colour_t family, float bar, bool focused, bool modulator);
void uw_tile(u8g2_t *g, gui_rect_t r, const char *name, ui_colour_t family, float bar, bool focused, bool modulator) {
    if (r.w <= 0 || r.h <= 0) return;
    const uint8_t *font = g->font;                             // a name wider than the tile ("13 EN"): the small font (put back below)
    if (u8g2_GetStrWidth(g, name) > r.w - 6) u8g2_SetFont(g, SMALL);
    uw_tile_body(g, r, name, family, bar, focused, modulator);
    u8g2_SetFont(g, font);
}

static void uw_tile_body(u8g2_t *g, gui_rect_t r, const char *name, ui_colour_t family, float bar, bool focused, bool modulator) {
    const int base = r.h >= 14 ? r.y + 9 : baseline_mid(g, r);
    const int bw = bar >= 0 ? (int)((bar > 1 ? 1 : bar) * (float)(r.w - 10) + 0.5f) : -1;
    if (ui_colour(g)) {
        ui_fill(g, r.x, r.y, r.w, r.h, UC_TILE);
        ui_fill(g, r.x, r.y, 2, r.h, family);
        uw_text(g, r.x + 5, base, name, UC_TEXT);
        if (bw >= 0 && r.h >= 14) ui_fill(g, r.x + 5, r.y + 11, bw > 0 ? bw : 1, 2, family);
        if (focused) {
            ui_fill(g, r.x - 2, r.y - 2, r.w + 4, 2, UC_CURSOR); ui_fill(g, r.x - 2, r.y + r.h, r.w + 4, 2, UC_CURSOR);
            ui_fill(g, r.x - 2, r.y, 2, r.h, UC_CURSOR);         ui_fill(g, r.x + r.w, r.y, 2, r.h, UC_CURSOR);
        }
        return;
    }
    if (focused) {
        u8g2_DrawBox(g, r.x, r.y, r.w, r.h);
        u8g2_SetDrawColor(g, 0);
    } else {
        u8g2_DrawFrame(g, r.x, r.y, r.w, r.h);
        if (modulator) {                                       // a modulator's tile: its corners cut, so it is not mistaken for a (dotted) cable
            u8g2_SetDrawColor(g, 0);
            u8g2_DrawPixel(g, r.x, r.y); u8g2_DrawPixel(g, r.x + r.w - 1, r.y);
            u8g2_DrawPixel(g, r.x, r.y + r.h - 1); u8g2_DrawPixel(g, r.x + r.w - 1, r.y + r.h - 1);
            u8g2_SetDrawColor(g, 1);
        }
    }
    const int w = u8g2_GetStrWidth(g, name);
    const uint8_t was = g->font_decode.is_transparent;
    u8g2_SetFontMode(g, 1);
    u8g2_DrawStr(g, r.x + (r.w - w) / 2, base, name);
    u8g2_SetFontMode(g, was);
    if (bw > 0 && r.h >= 14) u8g2_DrawHLine(g, r.x + 5, r.y + 11, bw);
    u8g2_SetDrawColor(g, 1);
}

#define WIDE_TOP 11                         // a wide tile: its first value row, under the name
#define WIDE_ROW 6                          // ... and the rows' pitch (the small font)

int uw_tile_wide_rows(int h) { return h > WIDE_TOP ? (h - WIDE_TOP) / WIDE_ROW : 0; }

void uw_tile_wide(u8g2_t *g, gui_rect_t r, const char *name, const char *kind, ui_colour_t family, const uw_bar_t *bar, int n, bool focused,
                  bool modulator) {
    if (r.w <= 0 || r.h <= 0) return;
    const bool col = ui_colour(g);
    const int rows = uw_tile_wide_rows(r.h) < n ? uw_tile_wide_rows(r.h) : n;
    if (col) {
        ui_fill(g, r.x, r.y, r.w, r.h, UC_TILE);
        ui_fill(g, r.x, r.y, 2, r.h, family);
    } else if (modulator) {                                    // (one colour: the corners cut, as uw_tile does)
        u8g2_DrawFrame(g, r.x, r.y, r.w, r.h);
        u8g2_SetDrawColor(g, 0);
        u8g2_DrawPixel(g, r.x, r.y); u8g2_DrawPixel(g, r.x + r.w - 1, r.y);
        u8g2_DrawPixel(g, r.x, r.y + r.h - 1); u8g2_DrawPixel(g, r.x + r.w - 1, r.y + r.h - 1);
        u8g2_SetDrawColor(g, 1);
    } else u8g2_DrawFrame(g, r.x, r.y, r.w, r.h);
    uw_text(g, r.x + 5, r.y + 8, name, UC_TEXT);
    if (kind && kind[0]) {
        const int kx = r.x + 9 + u8g2_GetStrWidth(g, name);
        uw_marquee(g, kx, r.y + 8, r.x + r.w - 3 - kx, kind, UC_DIM, focused);
    }
    const uint8_t *font = g->font;
    u8g2_SetFont(g, SMALL);
    const int bx = r.x + 22, bw = r.w - 22 - 4;
    for (int i = 0; i < rows; i++) {
        const int y = r.y + WIDE_TOP + i * WIDE_ROW;
        uw_text(g, r.x + 5, y + 5, bar[i].label, UC_TEXT);
        if (bar[i].frac < 0) continue;
        const float f = bar[i].frac > 1 ? 1 : bar[i].frac;
        const int w = (int)(f * (float)bw + 0.5f);
        if (col) {
            ui_fill(g, bx, y + 2, bw, 2, ui_dim(family));
            ui_fill(g, bx, y + 2, w > 0 ? w : 1, 2, family);
        } else {
            u8g2_DrawHLine(g, bx, y + 3, bw);
            if (w > 0) u8g2_DrawBox(g, bx, y + 2, w, 2);
        }
    }
    u8g2_SetFont(g, font);
    if (focused) {
        if (col) {
            ui_fill(g, r.x - 2, r.y - 2, r.w + 4, 2, UC_CURSOR); ui_fill(g, r.x - 2, r.y + r.h, r.w + 4, 2, UC_CURSOR);
            ui_fill(g, r.x - 2, r.y, 2, r.h, UC_CURSOR);         ui_fill(g, r.x + r.w, r.y, 2, r.h, UC_CURSOR);
        } else u8g2_DrawFrame(g, r.x - 2, r.y - 2, r.w + 4, r.h + 4);
    }
}

void uw_wire(u8g2_t *g, int x0, int y0, int x1, int y1, uw_cable_t k) {
    const bool col = ui_colour(g), dashed = k != UW_CABLE_AUDIO;
    const int on = col ? 3 : 1, off = col ? 2 : 1;
    const int sx0 = x0, sy0 = y0;
    if (!gui_clip_line_to(g, &x0, &y0, &x1, &y1)) return;      // cut where it crosses the window's borders (no wrap-around in u8g2)
    const int ox = x0 - sx0, oy = y0 - sy0, skipped = (ox < 0 ? -ox : ox) > (oy < 0 ? -oy : oy) ? (ox < 0 ? -ox : ox) : (oy < 0 ? -oy : oy);
    ui_ink(g, k == UW_CABLE_MOD ? UC_MOD : k == UW_CABLE_GATE ? UC_GATE : UC_DIM);
    if (!dashed) u8g2_DrawLine(g, (u8g2_uint_t)x0, (u8g2_uint_t)y0, (u8g2_uint_t)x1, (u8g2_uint_t)y1);
    else {                                                     // Bresenham, the dashes counted along the line from its real start
        const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, x = x0, y = y0;
        for (int i = skipped;; i++) {
            if (i % (on + off) < on) u8g2_DrawPixel(g, (u8g2_uint_t)x, (u8g2_uint_t)y);
            if (x == x1 && y == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x += sx; }
            if (e2 <= dx) { err += dx; y += sy; }
        }
    }
    ui_ink(g, UC_TEXT);
}

void uw_node(u8g2_t *g, gui_rect_t r, const char *label, ui_colour_t family, bool focused, bool small) {
    if (r.w <= 0 || r.h <= 0) return;
    const bool col = ui_colour(g);
    const int rad = r.h >= 9 ? 3 : 2;
    const uint8_t *font = g->font;
    if (small) u8g2_SetFont(g, SMALL);
    const int tw = u8g2_GetStrWidth(g, label), asc = u8g2_GetAscent(g);
    const int tx = r.x + (r.w - tw + 1) / 2, base = r.y + (r.h - asc) / 2 + asc;
    const uint8_t was = g->font_decode.is_transparent;
    u8g2_SetFontMode(g, 1);
    if (col) {
        if (focused) { ui_ink(g, UC_CURSOR); u8g2_DrawRBox(g, r.x - 2, r.y - 2, r.w + 4, r.h + 4, rad + 1); }
        ui_ink(g, family);
        u8g2_DrawRBox(g, r.x, r.y, r.w, r.h, rad);
        ui_ink(g, UC_BG);
        u8g2_DrawStr(g, tx, base, label);
        ui_ink(g, UC_TEXT);
    } else if (focused) {
        u8g2_DrawRBox(g, r.x, r.y, r.w, r.h, rad);
        u8g2_SetDrawColor(g, 0);
        u8g2_DrawStr(g, tx, base, label);
        u8g2_SetDrawColor(g, 1);
    } else {
        u8g2_SetDrawColor(g, 0);                               // (a cable under it: cleared)
        u8g2_DrawRBox(g, r.x, r.y, r.w, r.h, rad);
        u8g2_SetDrawColor(g, 1);
        u8g2_DrawRFrame(g, r.x, r.y, r.w, r.h, rad);
        u8g2_DrawStr(g, tx, base, label);
    }
    u8g2_SetFontMode(g, was);
    u8g2_SetFont(g, font);
}

int uw_chip_w(u8g2_t *g, const gui_style_t *st, const char *text) {
    u8g2_SetFont(g, SMALL);
    const int w = u8g2_GetStrWidth(g, text) + 5;
    u8g2_SetFont(g, st->font);
    return w;
}

void uw_chip(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *text, ui_colour_t c) {
    ui_ink(g, c);
    u8g2_DrawFrame(g, r.x, r.y, r.w, r.h);
    ui_ink(g, UC_TEXT);
    u8g2_SetFont(g, SMALL);
    uw_text(g, r.x + 2, r.y + r.h - 2, text, c);
    u8g2_SetFont(g, st->font);
}

void uw_title(u8g2_t *g, gui_rect_t r, const char *left, const char *right, ui_colour_t family) {
    const int base = baseline_mid(g, r);
    const int rw = u8g2_GetStrWidth(g, right);
    if (ui_colour(g)) {
        ui_fill(g, r.x, r.y, r.w, r.h, family);
        uw_text(g, r.x + 3, base, left, UC_BG);
        uw_text(g, r.x + r.w - 2 - rw, base, right, UC_BG);
        return;
    }
    u8g2_DrawBox(g, r.x, r.y, r.w, r.h);
    u8g2_SetDrawColor(g, 0);
    const uint8_t was = g->font_decode.is_transparent;
    u8g2_SetFontMode(g, 1);
    u8g2_DrawStr(g, r.x + 3, base, left);
    u8g2_DrawStr(g, r.x + r.w - 2 - rw, base, right);
    u8g2_SetFontMode(g, was);
    u8g2_SetDrawColor(g, 1);
}

void uw_tabs(u8g2_t *g, gui_rect_t r, const char *const *names, int n, int sel, bool focused) {
    const int cw = r.w / n, base = baseline_mid(g, r);
    for (int i = 0; i < n; i++) {
        const gui_rect_t c = gui_rect(r.x + i * cw, r.y, i == n - 1 ? r.w - i * cw : cw, r.h);
        const int w = u8g2_GetStrWidth(g, names[i]);
        if (i == sel) {
            if (ui_colour(g)) { ui_fill(g, c.x, c.y, c.w, c.h, UC_TILE); ui_fill(g, c.x, c.y + c.h - 2, c.w, 2, UC_TEXT); uw_text(g, c.x + (c.w - w) / 2, base, names[i], UC_TEXT); }
            else { u8g2_DrawBox(g, c.x, c.y, c.w, c.h); u8g2_SetDrawColor(g, 0); u8g2_DrawStr(g, c.x + (c.w - w) / 2, base, names[i]); u8g2_SetDrawColor(g, 1); }
        } else uw_text(g, c.x + (c.w - w) / 2, base, names[i], UC_DIM);
    }
    if (focused) { ui_ink(g, UC_CURSOR); u8g2_DrawFrame(g, r.x, r.y, r.w, r.h); ui_ink(g, UC_TEXT); }
}

void uw_value_row(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *label, const char *value, float frac, ui_colour_t family,
                  gui_state_t state, bool modulated) {
    char lab[16];
    snprintf(lab, sizeof lab, "%s%s", label, modulated ? "~" : "");
    if (frac > 1) frac = 1;
    if (!ui_colour(g)) {
        gui_draw_field_state(g, st, r, lab, value, state);
        if (state == GUI_PLAIN && frac > 0) u8g2_DrawHLine(g, r.x, r.y + r.h - 1, (int)(frac * (float)r.w + 0.5f));
        return;
    }
    const int base = baseline_mid(g, r);
    ui_fill(g, r.x, r.y, r.w, r.h, UC_TILE);
    if (frac > 0) ui_fill(g, r.x, r.y, (int)(frac * (float)r.w + 0.5f), r.h, ui_dim(family));
    if (modulated) ui_fill(g, r.x, r.y + r.h - 2, r.w, 2, UC_MOD);
    char val[28];
    snprintf(val, sizeof val, state == GUI_LATCHED ? "<%s>" : "%s", value);
    if (!pair_fits(g, r, lab, val)) pair_overflow(g, r, base, lab, val, UC_TEXT, state == GUI_LATCHED ? family : UC_TEXT, state != GUI_PLAIN);
    else {
        uw_text(g, r.x + 3, base, lab, UC_TEXT);
        uw_text(g, r.x + r.w - 2 - u8g2_GetStrWidth(g, val), base, val, state == GUI_LATCHED ? family : UC_TEXT);
    }
    if (state != GUI_PLAIN) { ui_ink(g, state == GUI_LATCHED ? family : UC_CURSOR); u8g2_DrawFrame(g, r.x, r.y, r.w, r.h); ui_ink(g, UC_TEXT); }
}

// A label and a value that do not fit side by side: each its window (the value at most half, the label the rest), sliding while focused.
static void pair_overflow(u8g2_t *g, gui_rect_t r, int base, const char *label, const char *val, ui_colour_t lc, ui_colour_t vc, bool moving) {
    const int avail = r.w - 5, lw = u8g2_GetStrWidth(g, label), vw = u8g2_GetStrWidth(g, val);
    int vwin = vw < avail / 2 ? vw : avail / 2;
    if (lw + 4 + vw <= avail) vwin = vw;
    else if (lw + 4 < avail - vwin) vwin = avail - lw - 4;             // (a short label: the value has the rest)
    const int lwin = val[0] ? avail - vwin - 4 : avail;
    uw_marquee(g, r.x + 3, base, lwin, label, lc, moving);
    if (val[0]) uw_marquee(g, r.x + r.w - 2 - vwin, base, vwin, val, vc, moving);
}

static bool pair_fits(u8g2_t *g, gui_rect_t r, const char *label, const char *val) {
    return u8g2_GetStrWidth(g, label) + 4 + (val ? u8g2_GetStrWidth(g, val) : 0) <= r.w - 5;
}

void uw_field(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *label, const char *value, ui_colour_t accent, gui_state_t state,
              bool dim) {
    char shown[40];
    snprintf(shown, sizeof shown, state == GUI_LATCHED && value && value[0] ? "<%s>" : "%s", value ? value : "");
    if (!ui_colour(g)) {
        if (pair_fits(g, r, label, shown) || state == GUI_PLAIN) { gui_draw_field_state(g, st, r, label, value, state); return; }
        const bool filled = state == GUI_LATCHED;                     // one colour, too long and focused: the same frame / fill, the texts slide
        if (filled) { u8g2_DrawBox(g, r.x, r.y, r.w, r.h); u8g2_SetDrawColor(g, 0); }
        else u8g2_DrawFrame(g, r.x, r.y, r.w, r.h);
        pair_overflow(g, r, baseline_mid(g, r), label, shown, UC_TEXT, UC_TEXT, true);
        u8g2_SetDrawColor(g, 1);
        return;
    }
    const int base = baseline_mid(g, r);
    if (!pair_fits(g, r, label, shown)) {
        ui_fill(g, r.x, r.y, r.w, r.h, state == GUI_LATCHED ? UC_HILITE : UC_TILE);
        pair_overflow(g, r, base, label, shown, state == GUI_PLAIN ? UC_TEXT : UC_CURSOR, dim ? UC_DIM : state == GUI_LATCHED ? accent : UC_TEXT,
                      state != GUI_PLAIN);
        if (state != GUI_PLAIN) { ui_ink(g, state == GUI_LATCHED ? accent : UC_CURSOR); u8g2_DrawFrame(g, r.x, r.y, r.w, r.h); ui_ink(g, UC_TEXT); }
        return;
    }
    ui_fill(g, r.x, r.y, r.w, r.h, state == GUI_LATCHED ? UC_HILITE : UC_TILE);
    uw_text(g, r.x + 3, base, label, state == GUI_PLAIN ? UC_TEXT : UC_CURSOR);
    if (value && value[0]) {
        char val[40];
        snprintf(val, sizeof val, state == GUI_LATCHED ? "<%s>" : "%s", value);
        const int vx = r.x + r.w - 2 - u8g2_GetStrWidth(g, val);
        ui_fill(g, vx - 2, r.y, r.x + r.w - vx + 2, r.h, state == GUI_LATCHED ? UC_HILITE : UC_TILE);   // (a long label: cut under the value)
        uw_text(g, vx, base, val, dim ? UC_DIM : state == GUI_LATCHED ? accent : UC_TEXT);
    }
    if (state != GUI_PLAIN) { ui_ink(g, state == GUI_LATCHED ? accent : UC_CURSOR); u8g2_DrawFrame(g, r.x, r.y, r.w, r.h); ui_ink(g, UC_TEXT); }
}

void uw_button(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *label, ui_colour_t accent, gui_state_t state) {
    if (!ui_colour(g)) { gui_draw_button(g, st, r, label, state); return; }
    const int base = baseline_mid(g, r), w = u8g2_GetStrWidth(g, label);
    ui_fill(g, r.x, r.y, r.w, r.h, state == GUI_PLAIN ? UC_TILE : accent);
    uw_text(g, r.x + (r.w - w) / 2, base, label, state == GUI_PLAIN ? UC_TEXT : UC_BG);
    if (state != GUI_PLAIN) { ui_ink(g, UC_CURSOR); u8g2_DrawFrame(g, r.x, r.y, r.w, r.h); ui_ink(g, UC_TEXT); }
}

void uw_port_row(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *left, const char *right, ui_colour_t right_c, bool focused) {
    u8g2_SetFont(g, SMALL);
    const int base = baseline_mid(g, r), rw = u8g2_GetStrWidth(g, right);
    if (ui_colour(g)) {
        if (focused) ui_fill(g, r.x, r.y, r.w, r.h, UC_HILITE);
        uw_text(g, r.x + 3, base, left, UC_TEXT);
        uw_text(g, r.x + r.w - 2 - rw, base, right, right_c);
        if (focused) { ui_ink(g, right_c == UC_TEXT ? UC_CURSOR : right_c); u8g2_DrawFrame(g, r.x, r.y, r.w, r.h); ui_ink(g, UC_TEXT); }
    } else {
        if (focused) { u8g2_DrawBox(g, r.x, r.y, r.w, r.h); u8g2_SetDrawColor(g, 0); }
        const uint8_t was = g->font_decode.is_transparent;
        u8g2_SetFontMode(g, 1);
        u8g2_DrawStr(g, r.x + 3, base, left);
        u8g2_DrawStr(g, r.x + r.w - 2 - rw, base, right);
        u8g2_SetFontMode(g, was);
        u8g2_SetDrawColor(g, 1);
    }
    u8g2_SetFont(g, st->font);
}

void uw_list_row(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *name, const char *info, ui_colour_t family, bool focused) {
    const int base = baseline_mid(g, r);
    if (ui_colour(g)) {
        ui_fill(g, r.x, r.y, r.w, r.h, focused ? UC_HILITE : UC_TILE);
        ui_fill(g, r.x, r.y, 3, r.h, family);
        u8g2_SetFont(g, SMALL);
        const int iw = u8g2_GetStrWidth(g, info);
        uw_text(g, r.x + r.w - 2 - iw, base, info, UC_DIM);
        u8g2_SetFont(g, st->font);
        uw_marquee(g, r.x + 7, base, r.w - 13 - iw, name, UC_TEXT, focused);
        if (focused) { ui_ink(g, family); u8g2_DrawFrame(g, r.x, r.y, r.w, r.h); ui_ink(g, UC_TEXT); }
        return;
    }
    if (focused) u8g2_DrawBox(g, r.x, r.y, r.w, r.h); else u8g2_DrawBox(g, r.x, r.y, 2, r.h);
    if (focused) u8g2_SetDrawColor(g, 0);
    const uint8_t was = g->font_decode.is_transparent;
    u8g2_SetFontMode(g, 1);
    u8g2_DrawStr(g, r.x + 5, base, name);
    u8g2_SetFont(g, SMALL);
    u8g2_DrawStr(g, r.x + r.w - 2 - u8g2_GetStrWidth(g, info), base, info);
    u8g2_SetFont(g, st->font);
    u8g2_SetFontMode(g, was);
    u8g2_SetDrawColor(g, 1);
}

void uw_box(u8g2_t *g, gui_rect_t r) {
    ui_ink(g, UC_DIM);
    u8g2_DrawFrame(g, r.x, r.y, r.w, r.h);
    ui_ink(g, UC_TEXT);
}

void uw_panel(u8g2_t *g, gui_rect_t r) {
    if (ui_colour(g)) { ui_fill(g, r.x, r.y, r.w, r.h, UC_PANEL); return; }
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawBox(g, r.x, r.y, r.w, r.h);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawHLine(g, r.x, r.y, r.w);
}
