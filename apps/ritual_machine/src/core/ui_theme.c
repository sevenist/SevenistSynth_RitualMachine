#include "core/ui_theme.h"
#include "hal/hal_display.h"

static canvas_t *g_canvas;

// a over b, 35 %: a family's dim colour over a tile (computed by the compiler)
#define CH(v, s) (((v) >> (s)) & 0xFF)
#define DIM(a, b) CANVAS_RGB((CH(a, 16) * 35 + CH(b, 16) * 65) / 100, (CH(a, 8) * 35 + CH(b, 8) * 65) / 100, (CH(a, 0) * 35 + CH(b, 0) * 65) / 100)
#define TILE 0x161C28
#define SRC 0x3DDC84
#define SHP 0xA87BFF
#define MOD 0xFFB020
#define FX  0x35C2E8
#define UTL 0x8A93A3
#define OUT 0xFF5A5F

static const uint16_t palette[UC_COUNT] = {
    [UC_BG] = CANVAS_HEX(0x000000), [UC_PANEL] = CANVAS_HEX(0x141A24), [UC_TILE] = CANVAS_HEX(TILE),
    [UC_TEXT] = CANVAS_HEX(0xE8ECF2), [UC_DIM] = CANVAS_HEX(0x5A6272), [UC_CURSOR] = CANVAS_HEX(0xFFFFFF), [UC_HILITE] = CANVAS_HEX(0x2A2410),
    [UC_SRC] = CANVAS_HEX(SRC), [UC_SHP] = CANVAS_HEX(SHP), [UC_MOD] = CANVAS_HEX(MOD), [UC_FX] = CANVAS_HEX(FX),
    [UC_UTL] = CANVAS_HEX(UTL), [UC_OUT] = CANVAS_HEX(OUT), [UC_GATE] = CANVAS_HEX(0xFF4FD8),
    [UC_SRC_DIM] = DIM(SRC, TILE), [UC_SHP_DIM] = DIM(SHP, TILE), [UC_MOD_DIM] = DIM(MOD, TILE), [UC_FX_DIM] = DIM(FX, TILE),
    [UC_UTL_DIM] = DIM(UTL, TILE), [UC_OUT_DIM] = DIM(OUT, TILE),
};

const uint16_t *ui_palette(void) { return palette; }

// What the canvas is given for a theme colour: its palette index on an indexed canvas, the colour itself on an RGB565 one.
static uint16_t value(ui_colour_t c) { return g_canvas->idx ? (uint16_t)c : palette[c]; }

void ui_colour_attach(canvas_t *c) {
    g_canvas = c;
    if (c) canvas_color(c, value(UC_TEXT), value(UC_BG));
}

bool ui_colour(const u8g2_t *g) { return g_canvas && g == canvas_u8g2(g_canvas); }

uint16_t ui_rgb(ui_colour_t c) { return palette[c]; }

ui_colour_t ui_dim(ui_colour_t family) {
    switch (family) {
        case UC_SRC: return UC_SRC_DIM;
        case UC_SHP: return UC_SHP_DIM;
        case UC_MOD: return UC_MOD_DIM;
        case UC_FX:  return UC_FX_DIM;
        case UC_UTL: return UC_UTL_DIM;
        case UC_OUT: return UC_OUT_DIM;
        default:     return UC_TILE;
    }
}

void ui_ink(u8g2_t *g, ui_colour_t c) { if (ui_colour(g)) canvas_color(g_canvas, value(c), value(UC_BG)); }

void ui_fill(u8g2_t *g, int x, int y, int w, int h, ui_colour_t c) { if (ui_colour(g)) canvas_fill(g_canvas, x, y, w, h, value(c)); }

void ui_clear(u8g2_t *g) {
    if (ui_colour(g)) {
        canvas_clear(g_canvas, value(UC_BG));
        canvas_color(g_canvas, value(UC_TEXT), value(UC_BG));
    }
    u8g2_ClearBuffer(g);                       // (the canvas keeps u8g2's one-bit buffer for its bookkeeping: cleared too)
}

void ui_frame_send(u8g2_t *g) {
    if (ui_colour(g)) canvas_send(g_canvas);
    else display_send(g);
}

ui_colour_t ui_family(module_type_t t) {
    switch (t) {
        case MOD_OSC: case MOD_SAMPLER: return UC_SRC;
        case MOD_FILTER: case MOD_SAT: case MOD_COMB: case MOD_EQ: return UC_SHP;
        case MOD_LFO: case MOD_MSEQ: case MOD_ENV: case MOD_EG: case MOD_VEL: return UC_MOD;
        default: return UC_FX;
    }
}
