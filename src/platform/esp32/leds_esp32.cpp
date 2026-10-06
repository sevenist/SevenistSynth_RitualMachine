// LED chain of HWV1: 36 x SK6812 on PIN_LED_DATA, driven by FastLED (RMT). The application addresses LEDs by control (hal_leds.h); this
// file maps a control to its place in the chain. FastLED 3.10.6 (pinned in platformio.ini) gives its first RMT channel DMA on the S3:
// without DMA the RMT refills its buffer from an interrupt, and an I2S audio interrupt in between stretches the signal into a reset of
// the strip (flicker; SynthBox patched FastLED 3.10.3 for this). -DFASTLED_RMT_MAX_CHANNELS=1 keeps it to one RMT channel.
// Brightness is capped at HW_LED_MAX_BRIGHTNESS: 36 LEDs at full white draw about 2 A, which browns out the board or overloads USB power.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <Arduino.h>
#include <FastLED.h>
#include "hal/hal_leds.h"
#include "platform/esp32/board_pins.h"
#include "platform/esp32/hwv1_layout.h"
#include "platform/esp32/leds_esp32.h"

namespace {
CRGB frame[HW_NUM_LEDS];
bool dirty = true;
CRGB test_frame[HW_NUM_LEDS];                           // the serial `leds` test pattern (static: the DMA may read it after show() returns)
bool disabled = false;                                   // serial `leds off`: no transfers at all (to measure their effect on the audio)
uint32_t test_until = 0;                                 // millis() until which the test owns the chain (0 = none); the application's frame keeps updating

// Chain index of the LED under the key at physical position (row from the top, column from the left), -1 = none. Serpentine, read left to
// right: function keys 32..35 on the top row, then the note rows from the top: 31..24, 16..23, 15..8, 0..7 (the user saw every row mirrored
// with the column flip, 2026-10-06). SynthBox's table (crdToIdx, OOPSIE_LED_FLAG) is in TCA column order, which runs
// right to left on this board; kHwv1KeyMap is already physical, so no HWV1_FLIP_COLS here (applying it mirrored every row).
int led_index(int pr, int pc) {
    if (HWV1_FLIP_ROWS) pr = HW_KBD_ROWS - 1 - pr;
    switch (pr) {
        case 0: return pc < 4 ? 32 + pc : -1;
        case 1: return 31 - pc;
        case 2: return 16 + pc;
        case 3: return 15 - pc;
        case 4: return pc;
    }
    return -1;
}

int led_of_control(control_id_t ctl) {
    for (int r = 0; r < HW_KBD_ROWS; r++)
        for (int c = 0; c < HW_KBD_COLS; c++)
            if (kHwv1KeyMap[r][c] == ctl) return led_index(r, c);
    return -1;
}

void put(int i, led_color_t c) {
    if (i < 0 || i >= HW_NUM_LEDS) return;
    const CRGB v(c.r, c.g, c.b);
    if (frame[i] != v) { frame[i] = v; dirty = true; }
}
}  // namespace

extern "C" {
void leds_init(void) {
    FastLED.addLeds<SK6812, PIN_LED_DATA, RGB>(frame, HW_NUM_LEDS);
    FastLED.setBrightness(HW_LED_BRIGHTNESS < HW_LED_MAX_BRIGHTNESS ? HW_LED_BRIGHTNESS : HW_LED_MAX_BRIGHTNESS);
    FastLED.clear(true);
    dirty = false;
}
void leds_set_brightness(uint8_t level) {
    if (level > HW_LED_MAX_BRIGHTNESS) level = HW_LED_MAX_BRIGHTNESS;
    if (FastLED.getBrightness() != level) { FastLED.setBrightness(level); dirty = true; }
}
void leds_clear(void) { for (int i = 0; i < HW_NUM_LEDS; i++) put(i, led_color_t{0, 0, 0}); }
void leds_set(control_id_t ctl, led_color_t color) { put(led_of_control(ctl), color); }
void leds_set_all(led_color_t color) { for (int i = 0; i < HW_NUM_LEDS; i++) put(i, color); }
void leds_show(void) {
    if (disabled) return;
    if (test_until) {
        if ((int32_t)(millis() - test_until) < 0) return;    // the test pattern stays
        test_until = 0; dirty = true;                        // the test is over: the application's frame again
    }
    if (!dirty) return;                                  // nothing changed: no transfer
    dirty = false;
    FastLED.show();
}
void leds_esp32_enable(bool on) {
    if (!on) { FastLED.clear(true); disabled = true; }
    else { disabled = false; dirty = true; }
}
void leds_esp32_test(int index, uint32_t ms) {
    for (int i = 0; i < HW_NUM_LEDS; i++) test_frame[i] = index < 0 ? CRGB(80, 80, 80) : i == index ? CRGB(255, 0, 0) : CRGB::Black;
    FastLED[0].setLeds(test_frame, HW_NUM_LEDS);         // show the test pattern without touching the application's frame
    FastLED.show();
    FastLED[0].setLeds(frame, HW_NUM_LEDS);
    test_until = millis() + (ms ? ms : 1);
}
}
#endif
