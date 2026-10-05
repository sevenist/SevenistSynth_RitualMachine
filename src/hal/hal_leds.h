#pragma once
// HAL: the LEDs under the keys. One implementation per platform; the simulator's is a no-op.
//
// STUB: the API is final enough to code against, the ESP32 side keeps a frame buffer but does not drive the chain yet (see
// platform/esp32/leds_esp32.cpp for where FastLED goes). LEDs are addressed by the physical control they sit under, like input is,
// so the application never sees the chain order.
#include <stdint.h>
#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t r, g, b; } led_color_t;

void leds_init(void);
void leds_set_brightness(uint8_t level);                   // 0..255, global
void leds_clear(void);                                     // all black (not shown until leds_show)
void leds_set(control_id_t ctl, led_color_t color);        // the LED under a key or button; ignored if that control has none
void leds_set_all(led_color_t color);
void leds_show(void);                                      // pushes the frame buffer to the hardware

#ifdef __cplusplus
}
#endif
