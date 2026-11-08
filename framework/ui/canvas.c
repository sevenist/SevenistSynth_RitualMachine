// The colour canvas (canvas.h): u8g2 set up with a display of the canvas's size and a line routine that paints RGB565.
#include "ui/canvas.h"
#include <string.h>
#include "hal/hal_display.h"

// u8g2's line routine: `len` pixels from (x, y), along x (dir 0) or y (dir 1). u8g2 has clipped the line to the frame already (a full
// buffer: no page offset). The draw colour picks what is painted (canvas.h).
static void canvas_hvline(u8g2_t *g, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t len, uint8_t dir) {
    canvas_t *c = (canvas_t *)g;
    if (x >= (u8g2_uint_t)c->w || y >= (u8g2_uint_t)c->h || len == 0) return;
    uint16_t *p = c->px + (size_t)y * (size_t)c->w + x;
    const int step = dir ? c->w : 1;
    const u8g2_uint_t room = dir ? (u8g2_uint_t)(c->h - y) : (u8g2_uint_t)(c->w - x);
    if (len > room) len = room;
    if (g->draw_color == 2) {
        const uint16_t swap = (uint16_t)(c->fg ^ c->bg);
        for (; len; len--, p += step) *p ^= swap;
    } else {
        const uint16_t v = g->draw_color ? c->fg : c->bg;
        for (; len; len--, p += step) *p = v;
    }
}

// The display u8g2 thinks it has: only its size matters, nothing is ever sent through it.
static uint8_t canvas_display_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    (void)arg_int; (void)arg_ptr;
    if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) u8x8_d_helper_display_setup_memory(u8x8, &((canvas_t *)u8x8)->info);
    return 1;
}

bool canvas_init(canvas_t *c, uint16_t *pixels, int w, int h) {
    memset(c, 0, sizeof *c);
    if (!pixels || w <= 0 || h <= 0 || (w & 7) || (h & 7) || w > CANVAS_MAX_W || h > CANVAS_MAX_H) return false;
    c->px = pixels;
    c->w = w;
    c->h = h;
    c->info.chip_disable_level = 1;
    c->info.tile_width = (uint8_t)(w / 8);
    c->info.tile_height = (uint8_t)(h / 8);
    c->info.pixel_width = (uint16_t)w;
    c->info.pixel_height = (uint16_t)h;
    // (the u8x8 struct is the first member of u8g2_t, so the display callback's u8x8 is the canvas too)
    u8g2_SetupDisplay(&c->u8g2, canvas_display_cb, u8x8_cad_empty, u8x8_byte_empty, u8x8_dummy_cb);
    u8g2_SetupBuffer(&c->u8g2, c->mono, (uint8_t)(h / 8), canvas_hvline, &u8g2_cb_r0);
    canvas_color(c, 0xFFFF, 0x0000);
    canvas_clear(c, 0x0000);
    return true;
}

void canvas_clear(canvas_t *c, uint16_t color) {
    const size_t n = (size_t)c->w * (size_t)c->h;
    for (size_t i = 0; i < n; i++) c->px[i] = color;
}

void canvas_fill(canvas_t *c, int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > c->w) w = c->w - x;
    if (y + h > c->h) h = c->h - y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; r++) {
        uint16_t *p = c->px + (size_t)(y + r) * (size_t)c->w + x;
        for (int i = 0; i < w; i++) p[i] = color;
    }
}

uint16_t canvas_get(const canvas_t *c, int x, int y) {
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return 0;
    return c->px[(size_t)y * (size_t)c->w + x];
}

void canvas_send(const canvas_t *c) {
    if (c->w == DISPLAY_WIDTH && c->h == DISPLAY_HEIGHT) display_send_rgb565(c->px);   // (a frame of another size is not the screen's)
}
