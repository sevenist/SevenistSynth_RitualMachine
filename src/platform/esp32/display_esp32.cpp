// ESP32 display driver. HWV1: SH1107 128x128 OLED on I2C (Wire0, pins in board_pins.h), same panel and setup as
// github.com/clement-chupin/SynthBox (src/oled.cpp).
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "hal/hal_display.h"
#include "platform/esp32/board_pins.h"

// U8g2 constructor order for HW I2C with explicit pins: (rotation, reset, clock, data).
static U8G2_SH1107_128X128_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

extern "C" u8g2_t *display_init(void) {
    oled.begin();
    Wire.setClock(400000);                  // 400 kHz: about 47 ms per frame instead of 190 ms at the 100 kHz default
    oled.sendF("ca", 0xD3, -32);            // display offset, required by this panel (see SynthBox HWLayout.h)
    oled.clearBuffer();
    oled.sendBuffer();
    return oled.getU8g2();
}
#endif // ARDUINO_ARCH_ESP32 && HWV1
