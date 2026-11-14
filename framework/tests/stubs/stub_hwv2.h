#pragma once
// The second prototype's board (HWV2) as an app's host tests see it (stub_hwv2.cpp): its display keeps the last frame sent, so a test can
// look at it or write it as a PNG; its control table is HWV2's own (link framework/boards/hwv2/hwv2_controls.c); its LEDs keep what was
// set. Build with -DDISPLAY_HEIGHT=160 (its 128 x 160 screen) and u8g2 on the include path. (In a folder of its own: the library's
// tests, every framework/tests/*.cpp, are built without u8g2.)
#include <cstdint>
#include "boards/hwv2/hwv2_controls.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_leds.h"

extern int stub_frames;                                         // frames sent to the display (colour or one-colour)
extern uint16_t stub_frame[DISPLAY_WIDTH * DISPLAY_HEIGHT];     // the last colour frame sent
extern led_color_t stub_leds[HWV2_CONTROL_COUNT];               // the LEDs as last set
extern int stub_led_shows;

// Writes the last colour frame as a PNG at `scale` (host/png.h), to look at it.
bool stub_write_png(const char *path, int scale);
