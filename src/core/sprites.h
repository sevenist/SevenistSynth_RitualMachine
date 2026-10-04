#pragma once
// 1-bit sprites for gui_draw_sprite(): 8 pixels per byte, MSB = leftmost pixel, rows of
// ceil(w/8) bytes. Add a new one by drawing it in a byte grid and wrapping it in a
// gui_sprite_t. Header-only (static), so include it where it is used.
#include "core/gui.h"

static const uint8_t spr_play_data[8] = {
    0x40,   // .X......
    0x60,   // .XX.....
    0x70,   // .XXX....
    0x78,   // .XXXX...
    0x78,   // .XXXX...
    0x70,   // .XXX....
    0x60,   // .XX.....
    0x40,   // .X......
};
static const gui_sprite_t spr_play = {8, 8, spr_play_data};

static const uint8_t spr_stop_data[8] = {
    0x00,   // ........
    0x7E,   // .XXXXXX.
    0x7E,
    0x7E,
    0x7E,
    0x7E,
    0x7E,
    0x00,
};
static const gui_sprite_t spr_stop = {8, 8, spr_stop_data};

static const uint8_t spr_eq_data[4 * 8] = {
    0x00, 0x00, 0x18, 0x18, 0x18, 0xD8, 0xDB, 0xDB,   // frame 0
    0x00, 0x00, 0xC0, 0xC3, 0xC3, 0xDB, 0xDB, 0xDB,   // frame 1
    0x00, 0x18, 0x18, 0x18, 0xD8, 0xDB, 0xDB, 0xDB,   // frame 2
    0x00, 0xC0, 0xC3, 0xC3, 0xDB, 0xDB, 0xDB, 0xDB,   // frame 3
};
// 4 frames stacked vertically: use with gui_anim_add(&spr_eq, 0, 3, ...).
static const gui_sprite_t spr_eq = {8, 8, spr_eq_data, 4};
