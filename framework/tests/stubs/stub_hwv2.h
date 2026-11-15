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

// Another board's control table from now on (an app tried on HWV1's: link framework/boards/hwv1/hwv1_controls.c); NULL: HWV2's again.
void stub_use_controls(const input_control_t *table, int count);

// Its card (hal/hal_storage.h): files in memory. Out at the start; stub_card_insert queues the INSERTED event (its folders all there),
// stub_card_remove the REMOVED one; stub_card_put writes a file (a card out keeps its files), stub_card_clear empties it.
void stub_card_put(const char *name, const char *text);
void stub_card_clear();
void stub_card_insert();
void stub_card_remove();
extern int stub_card_reads;                                     // storage_read calls
