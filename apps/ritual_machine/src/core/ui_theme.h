#pragma once
// Colour for the UI (apps/ritual_machine/docs/GUI_NODES.md section 5), on a board whose screen has it.
//
// The app draws with u8g2 as always. In colour, the platform hands the app the u8g2 of a colour canvas (framework/ui/canvas.h) instead of the
// display's own, and attaches that canvas here (ui_colour_attach): every u8g2 call then paints the canvas's ink, the theme's text colour on
// its background unless a widget says otherwise. In one colour nothing is attached: ui_colour() is false, ui_ink / ui_fill do nothing and the
// widgets (ui_widgets.h) draw their one-colour replacement. So no existing screen changes: it is drawn white on near-black.
//
// The build flag UI_COLOUR_MODE says which (platformio.ini, or the platform's default below):
//   UI_MONO     one colour (HWV1's OLED; any board, to compare)
//   UI_PALETTE  an indexed canvas: a byte a pixel, an index into the theme's colours (ui_palette); the canvas and the display's three frames
//               take 80 KB at 128 x 160
//   UI_RGB      an RGB565 canvas: 160 KB at 128 x 160
// Both colour modes look the same: the theme only uses its own colours (UC_*), so a palette holds them all.
//
// The rules: colour is never the only channel (a widget's one-colour drawing must say the same thing), and a widget that changes the ink
// puts it back to UC_TEXT before it returns.
#include <stdbool.h>
#include <stdint.h>
#include "u8g2.h"
#include "ui/canvas.h"
#include "core/rack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MONO    0
#define UI_PALETTE 1
#define UI_RGB     2

typedef enum {
    UC_BG, UC_PANEL, UC_TILE, UC_TEXT, UC_DIM, UC_CURSOR, UC_HILITE,
    UC_SRC, UC_SHP, UC_MOD, UC_FX, UC_UTL, UC_OUT, UC_GATE,          // the families (a node's band) and the gate cable
    UC_SRC_DIM, UC_SHP_DIM, UC_MOD_DIM, UC_FX_DIM, UC_UTL_DIM, UC_OUT_DIM,   // the same over a tile, dimmed (a value's bar)
    UC_COUNT
} ui_colour_t;

const uint16_t *ui_palette(void);             // the theme's colours, RGB565, UC_COUNT of them: an indexed canvas's palette

void     ui_colour_attach(canvas_t *c);       // the canvas the app draws into (RGB565 or indexed); NULL: one colour
bool     ui_colour(const u8g2_t *g);          // g draws in colour
uint16_t ui_rgb(ui_colour_t c);               // RGB565
ui_colour_t ui_dim(ui_colour_t family);       // a family's dim colour (UC_SRC -> UC_SRC_DIM); others: UC_TILE

void ui_ink(u8g2_t *g, ui_colour_t c);                           // the colour of what u8g2 draws next (colour index 1); one colour: nothing
void ui_fill(u8g2_t *g, int x, int y, int w, int h, ui_colour_t c);      // a filled rectangle in that colour; one colour: nothing

// A frame: clear it (the background colour, or u8g2's buffer), and send it (the colour canvas, or display_send).
void ui_clear(u8g2_t *g);
void ui_frame_send(u8g2_t *g);

// The family of a module type: its node's colour (sources, shapers, modulators, effects).
ui_colour_t ui_family(module_type_t t);

#ifdef __cplusplus
}
#endif
