#pragma once
// HWV1 LED chain extras beyond hal_leds.h (dev tools only).
#include "hal/hal_leds.h"

#ifdef __cplusplus
extern "C" {
#endif

// Dev test (serial `leds`): for `ms` milliseconds only chain LED `index` is lit (red), or every LED (white) when index < 0; the application's
// LED updates are ignored meanwhile.
void leds_esp32_test(int index, uint32_t ms);
void leds_esp32_enable(bool on);                           // serial `leds off` / `leds on`: stop / resume every transfer to the chain

#ifdef __cplusplus
}
#endif
