#pragma once
// The colour canvas: a frame of RGB565 pixels that u8g2 draws into. Every u8g2 call and every call of the drawing kit (ui/gui.h) works on
// it unchanged: the canvas gives u8g2 its own line routine (u8g2's ll_hvline), which paints the canvas's colours instead of setting bits.
//
//   u8g2 draw colour 1 (set, the default)  -> the foreground colour
//   u8g2 draw colour 0 (clear)             -> the background colour (also the background of a font drawn in solid mode)
//   u8g2 draw colour 2 (XOR)               -> swaps the two: a pixel in the foreground colour becomes the background one and back
//
// So drawing in colour is: canvas_color(c, fg, bg), then the u8g2 / gui calls as on a one-colour screen. The frame is the application's
// memory; it goes to the screen with display_send_rgb565 (hal/hal_display.h) or canvas_send. A board with a one-colour screen shows it
// too (each pixel lit when it is bright), so a colour application still runs there.
//
// Not u8g2's own buffer functions: u8g2_ClearBuffer / u8g2_SendBuffer work on a one-bit buffer the canvas only keeps for u8g2's book-
// keeping. Clear with canvas_clear, send with canvas_send.
#include <stdbool.h>
#include <stdint.h>
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANVAS_MAX_W 160
#define CANVAS_MAX_H 160

// A colour in RGB565 from 8-bit red, green, blue, and from a web-style 0xRRGGBB number.
#define CANVAS_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xFF) >> 3)))
#define CANVAS_HEX(rgb)     CANVAS_RGB(((rgb) >> 16) & 0xFF, ((rgb) >> 8) & 0xFF, (rgb) & 0xFF)

typedef struct canvas {
    u8g2_t u8g2;                                    // first: u8g2's callbacks find the canvas from the u8g2 they are given
    u8x8_display_info_t info;                       // the size u8g2 sees
    uint16_t *px;                                   // w x h pixels, row by row from the top left (native byte order)
    int w, h;
    uint16_t fg, bg;
    uint8_t mono[CANVAS_MAX_W * CANVAS_MAX_H / 8];  // u8g2's one-bit buffer: nothing is drawn in it (see above)
} canvas_t;

// `pixels` holds w x h colours and stays the caller's. w and h: multiples of 8, at most CANVAS_MAX_W x CANVAS_MAX_H. False = a size
// that does not fit (the canvas is not usable). The frame is cleared to black, the colours are white on black.
bool canvas_init(canvas_t *c, uint16_t *pixels, int w, int h);
static inline u8g2_t *canvas_u8g2(canvas_t *c) { return &c->u8g2; }
static inline void canvas_color(canvas_t *c, uint16_t fg, uint16_t bg) { c->fg = fg; c->bg = bg; }

void canvas_clear(canvas_t *c, uint16_t color);                                 // the whole frame
void canvas_fill(canvas_t *c, int x, int y, int w, int h, uint16_t color);      // a rectangle, clipped to the frame
uint16_t canvas_get(const canvas_t *c, int x, int y);                           // a pixel (0 outside the frame): for tests
void canvas_send(const canvas_t *c);                                            // display_send_rgb565 of the frame

#ifdef __cplusplus
}
#endif
