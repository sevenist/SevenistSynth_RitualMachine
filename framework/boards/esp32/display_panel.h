#pragma once
// What a board gives the shared display driver (display_esp32.cpp): its panel. The frames are u8g2's full buffers: DISPLAY_WIDTH x
// DISPLAY_HEIGHT pixels, 1 bit each, a byte = 8 pixels of a column with bit 0 on top, DISPLAY_WIDTH bytes per 8 rows.
#if defined(ARDUINO_ARCH_ESP32)
#include "hal/hal_display.h"

u8g2_t *display_panel_init(void);                                      // the panel on and blank, and the u8g2 the application draws with
void display_panel_show(const uint8_t *frame, const uint8_t *shown);   // sends the tiles (8 x 8 pixels) of `frame` that differ from `shown`
// A board with a colour screen (HW_DISPLAY_RGB565 in board_pins.h) only: the same for a colour frame (hal_display.h's layout: RGB565, row
// by row, the processor's byte order; in PSRAM). shown == NULL: what the panel shows is not known, every tile is sent.
void display_panel_show_rgb565(const uint16_t *frame, const uint16_t *shown);
#endif
