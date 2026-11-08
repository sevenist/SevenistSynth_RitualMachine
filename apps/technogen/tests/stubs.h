#pragma once
// The board as the tests see it (stubs.cpp).
#include <cstdint>
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_leds.h"
#include "boards/hwv2/hwv2_controls.h"

extern int stub_frames;                                         // frames sent to the display
extern uint16_t stub_frame[DISPLAY_WIDTH * DISPLAY_HEIGHT];     // the last colour frame sent
extern led_color_t stub_leds[HWV2_CONTROL_COUNT];               // the LEDs as last set
extern int stub_led_shows;

// Writes the last colour frame as a PNG at `scale` (build/<name>.png), to look at it.
bool stub_write_png(const char *path, int scale);
