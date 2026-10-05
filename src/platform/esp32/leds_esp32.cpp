// STUB: LED chain of HWV1 (36 x SK6812 on PIN_LED_DATA). Keeps a frame buffer and the control -> LED index mapping; leds_show() does
// not talk to the hardware yet.
//
// To finish: add `fastled/FastLED@^3.10.3` to lib_deps (SynthBox also runs extra_scripts/patch_fastled_dma.py and
// -DFASTLED_RMT_MAX_CHANNELS=1 so the LED output does not fight the I2S DMA), call
//   FastLED.addLeds<SK6812, PIN_LED_DATA, BGR>(fb, HW_NUM_LEDS); FastLED.setBrightness(...)
// in leds_init() and FastLED.show() in leds_show(), with `fb` a CRGB array in place of `frame` below.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <Arduino.h>
#include "hal/hal_leds.h"
#include "platform/esp32/board_pins.h"
#include "platform/esp32/hwv1_layout.h"

namespace {
led_color_t frame[HW_NUM_LEDS];
uint8_t brightness = HW_LED_BRIGHTNESS;

// Chain index of the LED under the key at physical position (row from the top, column from the left), -1 = none.
// Serpentine wiring, from SynthBox HWLayout.h / Leds.cpp: top = function keys 32..35 (columns 4..7), then notes 31..24 (reversed),
// 16..23, 15..8 (reversed), 0..7 at the bottom.
int led_index(int pr, int pc) {
    if (HWV1_FLIP_ROWS) pr = HW_KBD_ROWS - 1 - pr;
    if (HWV1_FLIP_COLS) pc = HW_KBD_COLS - 1 - pc;
    switch (pr) {
        case 0: return pc >= 4 ? 32 + (pc - 4) : -1;
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
}  // namespace

extern "C" {
void leds_init(void) { leds_clear(); }
void leds_set_brightness(uint8_t level) { brightness = level; }
void leds_clear(void) { memset(frame, 0, sizeof frame); }
void leds_set(control_id_t ctl, led_color_t color) {
    const int i = led_of_control(ctl);
    if (i >= 0 && i < HW_NUM_LEDS) frame[i] = color;
}
void leds_set_all(led_color_t color) { for (int i = 0; i < HW_NUM_LEDS; i++) frame[i] = color; }
void leds_show(void) { (void)brightness; /* TODO: push `frame` to the chain (see the top of this file) */ }
}
#endif
