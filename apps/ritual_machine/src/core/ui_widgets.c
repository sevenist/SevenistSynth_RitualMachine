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

static int baseline_mid(u8g2_t *g, gui_rect_t r) { return r.y + (r.h - (u8g2_GetAscent(g) - u8g2_GetDescent(g))) / 2 + u8g2_GetAscent(g); }

void uw_line(u8g2_t *g, int x0, int y0, int x1, int y1, uw_cable_t k) {
    const bool col = ui_colour(g);
    int on = 1, off = 0;
    if (k == UW_CABLE_MOD) { on = col ? 3 : 1; off = col ? 2 : 1; }
    if (k == UW_CABLE_GATE) { on = 1; off = 2; }
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

void uw_tile(u8g2_t *g, gui_rect_t r, const char *name, ui_colour_t family, float bar, bool focused, bool modulator) {
    if (r.w <= 0 || r.h <= 0) return;
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
    if (kind && kind[0]) uw_text(g, r.x + 9 + u8g2_GetStrWidth(g, name), r.y + 8, kind, UC_DIM);
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
    ui_ink(g, k == UW_CABLE_MOD ? UC_MOD : k == UW_CABLE_GATE ? UC_GATE : UC_DIM);
    if (!dashed) u8g2_DrawLine(g, (u8g2_uint_t)x0, (u8g2_uint_t)y0, (u8g2_uint_t)x1, (u8g2_uint_t)y1);
    else {                                                     // Bresenham, the dashes counted along the line
        const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, x = x0, y = y0;
        for (int i = 0;; i++) {
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
    uw_text(g, r.x + 3, base, lab, UC_TEXT);
    char val[28];
    snprintf(val, sizeof val, state == GUI_LATCHED ? "<%s>" : "%s", value);
    uw_text(g, r.x + r.w - 2 - u8g2_GetStrWidth(g, val), base, val, state == GUI_LATCHED ? family : UC_TEXT);
    if (state != GUI_PLAIN) { ui_ink(g, state == GUI_LATCHED ? family : UC_CURSOR); u8g2_DrawFrame(g, r.x, r.y, r.w, r.h); ui_ink(g, UC_TEXT); }
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
        uw_text(g, r.x + 7, base, name, UC_TEXT);
        u8g2_SetFont(g, SMALL);
        uw_text(g, r.x + r.w - 2 - u8g2_GetStrWidth(g, info), base, info, UC_DIM);
        u8g2_SetFont(g, st->font);
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

void uw_panel(u8g2_t *g, gui_rect_t r) {
    if (ui_colour(g)) { ui_fill(g, r.x, r.y, r.w, r.h, UC_PANEL); return; }
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawBox(g, r.x, r.y, r.w, r.h);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawHLine(g, r.x, r.y, r.w);
}
