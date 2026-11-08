// The first prototype's panel (boards/esp32/display_panel.h): SH1107 128x128 OLED on I2C (Wire0, pins in board_pins.h), same panel setup as the
// SynthBox firmware (github.com/clement-chupin/SynthBox, src/oled.cpp; see board_pins.h).
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "boards/esp32/board_pins.h"
#include "boards/esp32/display_panel.h"

// U8g2 constructor order for HW I2C with explicit pins: (rotation, reset, clock, data).
static U8G2_SH1107_128X128_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

u8g2_t *display_panel_init(void) {
    oled.begin();
    Wire.setClock(400000);                  // 400 kHz (SH1107 limit); about 120 ms per full frame measured
    oled.sendF("ca", 0xD3, -32);            // display offset, required by this panel (see SynthBox HWLayout.h)
    oled.clearBuffer();
    oled.sendBuffer();
    return oled.getU8g2();
}

void display_panel_show(const uint8_t *frame, const uint8_t *shown) {
    constexpr int kTilesX = DISPLAY_WIDTH / 8, kTilesY = DISPLAY_HEIGHT / 8;
    u8x8_t *u8x8 = u8g2_GetU8x8(oled.getU8g2());
    // This panel's columns start at an offset (96): u8g2 sends column 96 + 8 x tile, which only works for a whole row (the panel's column
    // counter wraps from 127 to 0). A run starting at tile tx is sent as "tile 0" with the offset moved to (96 + 8 tx) mod 128, so it wraps
    // the same way (sending at tile tx directly gave columns past 127: garbage on the screen).
    const uint8_t base = u8x8->x_offset;
    for (int ty = 0; ty < kTilesY; ty++) {
        const uint8_t *row = frame + ty * kTilesX * 8, *old = shown + ty * kTilesX * 8;
        int tx = 0;
        while (tx < kTilesX) {
            if (!memcmp(row + tx * 8, old + tx * 8, 8)) { tx++; continue; }
            int end = tx + 1;                                     // a run of changed tiles; a single unchanged tile inside it is sent too
            while (end < kTilesX && (memcmp(row + end * 8, old + end * 8, 8) ||          // (one more tile costs less than a new command)
                                     (end + 1 < kTilesX && memcmp(row + (end + 1) * 8, old + (end + 1) * 8, 8)))) end++;
            u8x8->x_offset = (uint8_t)((base + tx * 8) & 127);
            u8x8_DrawTile(u8x8, 0, (uint8_t)ty, (uint8_t)(end - tx), const_cast<uint8_t *>(row + tx * 8));
            tx = end;
        }
    }
    u8x8->x_offset = base;
}
#endif
