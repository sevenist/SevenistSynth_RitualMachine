#pragma once
// LED chain extras beyond hal_leds.h: dev tools, the board's own LEDs, the chain dark before the power goes.
#include "hal/hal_leds.h"

#ifdef __cplusplus
extern "C" {
#endif

// Dev test (serial `leds`): for `ms` milliseconds only chain LED `index` is lit (red), or every LED (white) when index < 0; the application's
// LED updates are ignored meanwhile.
void leds_esp32_test(int index, uint32_t ms);
void leds_esp32_put(int index, led_color_t color);         // a board's own LED, by its place in the chain (board_leds_refresh)
void leds_esp32_enable(bool on);                           // serial `leds off` / `leds on`: stop / resume every transfer to the chain
void leds_esp32_blank(void);                               // the whole chain black now (power off, card reader); the next leds_show sends the frame again

#ifdef __cplusplus
}
#endif
