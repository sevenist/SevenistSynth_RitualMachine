// The knob legend (apps/ritual_machine/docs/DESIGN.md ADR-046, rules R14 / R15): a zone at the bottom of a tall screen that names what each column knob
// drives, laid out like the knobs themselves (from the places in the board's control table: 2 x 2 around the joystick on HWV1, 1 x 4 on
// the panel). Each cell: the knob's number, what it drives, and a bar of that value; while the knob has not caught the value a tick shows
// where the knob stands, so the way to turn is the way from the tick to the end of the bar.
//
//   HWV1 (cells 64 x 9, one line)          the panel (cells 32 x 12, two lines)
//   [1]Wav    ====------  [2]Timb ==|---   [1]Wav [2]Timb [3]Mrph [4]Lvl
//   [3]Mrph   =======---  [4]Lvl  ====--   ====-- ==|--   =====-  ==---
//
// The screens leave the zone free (ui_legend_h); the app fills the cells and draws it over the frame (app_draw_frame).
#include "core/synth_ui.h"
#include "core/controls.h"
#include "core/modifiers.h"
#include "core/ui_sprites_gen.h"
#include "core/ui_internal.h"
#include "ui/gui.h"
#include <stdio.h>
#include <string.h>

#define LEGEND_MIN_SCREEN_H 100      // a shorter screen has no zone (128 x 64: every row counts)
#define LEGEND_LINE_H       9        // one line of text: the 7 px font and a pixel above and below
#define LEGEND_BAR_H        3        // the bar under the label in a two-line cell
#define LEGEND_ONE_LINE_W   56       // a cell at least this wide holds its label and its bar on one line
#define LEGEND_BAR_W        22       // the bar's width in a one-line cell

typedef struct { int cols, rows; int8_t col[SYNTH_UI_COL_KNOBS], row[SYNTH_UI_COL_KNOBS]; bool any; } legend_grid_t;

// The knobs' grid from their places on the panel: distinct columns and rows, in order. A knob the board does not have keeps a cell (it drives
// nothing, and the layout stays the one of the full board).
static legend_grid_t legend_grid(void) {
    legend_grid_t lg = {0};
    int px[SYNTH_UI_COL_KNOBS], py[SYNTH_UI_COL_KNOBS];
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
        if (controls_place((control_id_t)(CTL_COL_KNOB_0 + k), &px[k], &py[k])) lg.any = true;
        else { px[k] = 200 + k; py[k] = 0; }                                // (no place: after the others, on the first row)
    }
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
        int col = 0, row = 0;
        for (int j = 0; j < SYNTH_UI_COL_KNOBS; j++) {
            bool first_x = true, first_y = true;                           // count each distinct place once
            for (int i = 0; i < j; i++) { if (px[i] == px[j]) first_x = false; if (py[i] == py[j]) first_y = false; }
            if (first_x && px[j] < px[k]) col++;
            if (first_y && py[j] < py[k]) row++;
        }
        lg.col[k] = (int8_t)col; lg.row[k] = (int8_t)row;
        if (col + 1 > lg.cols) lg.cols = col + 1;
        if (row + 1 > lg.rows) lg.rows = row + 1;
    }
    return lg;
}

static int cell_h(int cell_w) { return cell_w >= LEGEND_ONE_LINE_W ? LEGEND_LINE_H : LEGEND_LINE_H + LEGEND_BAR_H; }

int ui_legend_h(u8g2_t *g) {
    if (u8g2_GetDisplayHeight(g) < LEGEND_MIN_SCREEN_H) return 0;
    const legend_grid_t lg = legend_grid();
    if (!lg.any) return 0;
    const int ch = cell_h(u8g2_GetDisplayWidth(g) / lg.cols);
    return 2 + lg.rows * ch + (lg.rows - 1);                             // the rule and a pixel under it, the rows a pixel apart
}

bool ui_legend_shown(const synth_ui_t *ui, const rack_t *rack, bool pads) {
    if (pads) return ui->pad_fx != 1;
    return !ui->in_rack || synth_ui_on_curves_tab(ui, rack);
}

void ui_legend_cell_none(legend_cell_t *c) { memset(c, 0, sizeof *c); c->value = -1.0f; c->knob = -1.0f; c->state = -1; }

// The bar: the value filled, a thin track for the rest; the knob's place as a tick across it while it has not caught the value.
static void draw_bar(u8g2_t *g, gui_rect_t b, const legend_cell_t *c) {
    if (c->value < 0.0f || b.w < 4) return;
    const int mid = b.y + b.h / 2;
    const float v = c->value > 1.0f ? 1.0f : c->value;
    const int fill = (int)(v * (float)(b.w - 1) + 0.5f);
    u8g2_DrawHLine(g, b.x, mid, b.w);
    if (fill > 0) u8g2_DrawBox(g, b.x, b.y, fill + 1, b.h);
    else u8g2_DrawVLine(g, b.x, b.y, b.h);                                // (empty: a mark at the start so the bar reads as a bar)
    if (c->state == 0 && c->knob >= 0.0f) {
        const float k = c->knob > 1.0f ? 1.0f : c->knob;
        const int x = b.x + (int)(k * (float)(b.w - 1) + 0.5f);
        u8g2_SetDrawColor(g, 2);                                          // XOR: seen over the fill and over the track
        u8g2_DrawVLine(g, x, b.y - 2, b.h + 4);
        u8g2_SetDrawColor(g, 1);
    }
}

static void draw_cell(u8g2_t *g, gui_rect_t r, int k, const legend_cell_t *c) {
    const bool none = c->state < 0;
    const gui_rect_t line = gui_rect(r.x, r.y, r.w, LEGEND_LINE_H);
    char num[4];
    snprintf(num, sizeof num, "%d", k + 1);
    const int bw = u8g2_GetStrWidth(g, num) + 3, base = line.y + LEGEND_LINE_H - 2;
    if (none) u8g2_DrawFrame(g, line.x, line.y, bw, LEGEND_LINE_H);       // the number: filled when the knob drives something here
    else { u8g2_DrawBox(g, line.x, line.y, bw, LEGEND_LINE_H); u8g2_SetDrawColor(g, 0); }
    u8g2_DrawStr(g, line.x + 2, base, num);
    u8g2_SetDrawColor(g, 1);
    const bool one_line = r.w >= LEGEND_ONE_LINE_W;
    const int lx = line.x + bw + 2;
    const int lw = (one_line ? r.w - LEGEND_BAR_W - 2 : r.w) - (lx - r.x);
    u8g2_SetClipWindow(g, lx, line.y, lx + lw, line.y + LEGEND_LINE_H);
    u8g2_DrawStr(g, lx, base, none ? "-" : c->label);
    u8g2_SetMaxClipWindow(g);
    if (none) return;
    if (one_line) draw_bar(g, gui_rect(r.x + r.w - LEGEND_BAR_W, line.y + 3, LEGEND_BAR_W, LEGEND_BAR_H), c);
    else          draw_bar(g, gui_rect(r.x, line.y + LEGEND_LINE_H, r.w, LEGEND_BAR_H - 1), c);
}

void ui_legend_draw(u8g2_t *g, const legend_cell_t cell[SYNTH_UI_COL_KNOBS]) {
    const int h = ui_legend_h(g);
    if (!h) return;
    const int W = u8g2_GetDisplayWidth(g), top = u8g2_GetDisplayHeight(g) - h;
    const legend_grid_t lg = legend_grid();
    const int cw = W / lg.cols, ch = cell_h(cw);
    u8g2_SetDrawColor(g, 0);                                              // the zone is the legend's, whatever was drawn there
    u8g2_DrawBox(g, 0, top, W, h);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawHLine(g, 0, top, W);
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
        const gui_rect_t r = gui_rect(lg.col[k] * cw, top + 2 + lg.row[k] * (ch + 1), cw - 3, ch);
        draw_cell(g, r, k, &cell[k]);
    }
}

/* ---------------- the header's marks ---------------- */

#define BATT_W 12                    // the battery: a 10 x 7 body and its 2 px nub
#define BATT_H 7

int ui_header_marks(u8g2_t *g, int right, int y, int h, const synth_ui_t *ui) {
    const uint8_t color = g->draw_color;                                  // XOR: seen on a plain header and on a focused (filled) one
    u8g2_SetDrawColor(g, 2);
    int x = right;
    if (ui->batt_pct >= 0) {                                              // the battery: filled as it is charged, hollow and cut when nearly empty
        x -= BATT_W;
        const int by = y + (h - BATT_H) / 2;
        u8g2_DrawFrame(g, x, by, BATT_W - 2, BATT_H);
        u8g2_DrawBox(g, x + BATT_W - 2, by + 2, 2, BATT_H - 4);
        const int fill = (ui->batt_pct * (BATT_W - 6) + 50) / 100;
        if (fill > 0) u8g2_DrawBox(g, x + 2, by + 2, fill, BATT_H - 4);
        else u8g2_DrawLine(g, x + 2, by + BATT_H - 3, x + BATT_W - 5, by + 2);   // empty: a stroke across
        x -= 2;
    }
    if (ui->rec_mark != REC_MARK_NONE) {                                   // recording: armed (framed) or running (inverted)
        const char *m = ui->rec_mark == REC_MARK_ERASE ? "ERS" : "REC";
        const int w = u8g2_GetStrWidth(g, m) + 3, my = y + (h - LEGEND_LINE_H) / 2;
        x -= w;
        if (ui->rec_mark == REC_MARK_ARMED) u8g2_DrawFrame(g, x, my, w, LEGEND_LINE_H);
        else u8g2_DrawBox(g, x, my, w, LEGEND_LINE_H);
        u8g2_DrawStr(g, x + 2, my + LEGEND_LINE_H - 2, m);
        x -= 2;
    }
    if (ui->layer_held >= 0) {                                            // a modifier held: "S" or "M" inverted (the legend shows its layer)
        const char *m = ui->layer_held == MODL_MOD ? "M" : "S";
        const int w = u8g2_GetStrWidth(g, m) + 3, my = y + (h - LEGEND_LINE_H) / 2;
        x -= w;
        u8g2_DrawBox(g, x, my, w, LEGEND_LINE_H);
        u8g2_DrawStr(g, x + 2, my + LEGEND_LINE_H - 2, m);
        x -= 2;
    }
    u8g2_SetDrawColor(g, color);
    return right - x;
}

/* ---------------- the header's view badge and title (ADR-046 phase 3) ---------------- */

#define BADGE_W 16

int ui_view_badge(u8g2_t *g, int x, int y, int h, view_t v, bool focused) {
    char name[24];
    snprintf(name, sizeof name, "16/view_%s", synth_ui_view_name(v));
    for (char *p = name; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    const uint8_t color = g->draw_color;
    if (focused) { u8g2_SetDrawColor(g, 1); u8g2_DrawBox(g, x, y, BADGE_W, h); u8g2_SetDrawColor(g, 0); }
    const gui_sprite_t *sp = ui_sprite(name);                          // a painted icon when there is one, else the tab icons' look:
    if (sp) gui_draw_sprite(g, sp, x, y);                              // two letters in a frame
    else {
        if (!focused) u8g2_DrawFrame(g, x, y, BADGE_W, h);
        gui_draw_text_centered(g, gui_rect(x, y, BADGE_W, h), synth_ui_view_abbr(v));
    }
    u8g2_SetDrawColor(g, focused ? 1 : color);
    return BADGE_W;
}

void ui_header_title(u8g2_t *g, gui_rect_t room, const char *title, bool arrows) {
    char t[32];
    snprintf(t, sizeof t, arrows ? "< %s >" : "%s", title);
    if (arrows && u8g2_GetStrWidth(g, t) > room.w) snprintf(t, sizeof t, "<%s>", title);   // tighter
    if (u8g2_GetStrWidth(g, t) > room.w) snprintf(t, sizeof t, "%s", title);
    u8g2_SetClipWindow(g, room.x, room.y, room.x + room.w, room.y + room.h);
    gui_draw_text_centered(g, room, t);
    u8g2_SetMaxClipWindow(g);
}
