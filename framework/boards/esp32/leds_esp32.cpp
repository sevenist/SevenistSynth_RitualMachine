// The LED chain on PIN_LED_DATA (HW_NUM_LEDS LEDs), driven by FastLED (RMT). The application addresses LEDs by control (hal_leds.h: the ones of
// the board's table), and the board says where each sits in the chain (board_led_of_control). FastLED 3.10.6 (pinned in platformio.ini) gives its first RMT channel DMA on the S3:
// without DMA the RMT refills its buffer from an interrupt, and an I2S audio interrupt in between stretches the signal into a reset of
// the strip (flicker; SynthBox patched FastLED 3.10.3 for this). -DFASTLED_RMT_MAX_CHANNELS=1 keeps it to one RMT channel.
// Brightness is capped at HW_LED_MAX_BRIGHTNESS: 36 LEDs at full white draw about 2 A, which browns out the board or overloads USB power.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <FastLED.h>
#include "hal/hal_leds.h"
#include "boards/esp32/board_pins.h"
#include "boards/esp32/leds_esp32.h"
#include "boards/esp32/board_esp32.h"

namespace {
CRGB frame[HW_NUM_LEDS];
bool dirty = true;
bool ready = false;                                      // leds_init has run (the chain is FastLED's)
CRGB test_frame[HW_NUM_LEDS];                           // the serial `leds` test pattern (static: the DMA may read it after show() returns)
bool disabled = false;                                   // serial `leds off`: no transfers at all (to measure their effect on the audio)
uint32_t test_until = 0;                                 // millis() until which the test owns the chain (0 = none); the application's frame keeps updating

void put(int i, led_color_t c) {
    if (i < 0 || i >= HW_NUM_LEDS) return;
    const CRGB v(c.r, c.g, c.b);
    if (frame[i] != v) { frame[i] = v; dirty = true; }
}
}  // namespace

extern "C" {
void leds_init(void) {
    if (ready) return;
    ready = true;
    FastLED.addLeds<SK6812, PIN_LED_DATA, HW_LED_ORDER>(frame, HW_NUM_LEDS);
    FastLED.setBrightness(HW_LED_BRIGHTNESS < HW_LED_MAX_BRIGHTNESS ? HW_LED_BRIGHTNESS : HW_LED_MAX_BRIGHTNESS);
    FastLED.clear(true);
    dirty = false;
}
void leds_set_brightness(uint8_t level) {
    if (level > HW_LED_MAX_BRIGHTNESS) level = HW_LED_MAX_BRIGHTNESS;
    if (FastLED.getBrightness() != level) { FastLED.setBrightness(level); dirty = true; }
}
void leds_clear(void) { for (int i = 0; i < HW_NUM_LEDS; i++) if (!(HW_LED_NOT_APP >> i & 1)) put(i, led_color_t{0, 0, 0}); }
void leds_esp32_put(int index, led_color_t color) { put(index, color); }
void leds_set(int ctl, led_color_t color) { put(board_led_of_control(ctl), color); }
void leds_set_all(led_color_t color) { for (int i = 0; i < HW_NUM_LEDS; i++) if (!(HW_LED_NOT_APP >> i & 1)) put(i, color); }
void leds_show(void) {
    if (disabled || display_esp32_loading()) return;     // dark while the loading screen is up; the frame waits (dirty stays set)
    if (test_until) {
        if ((int32_t)(millis() - test_until) < 0) return;    // the test pattern stays
        test_until = 0; dirty = true;                        // the test is over: the application's frame again
    }
    board_leds_refresh();
    if (!dirty) return;                                  // nothing changed: no transfer
    dirty = false;
    FastLED.show();
}
void leds_esp32_enable(bool on) {
    if (!on) { FastLED.clear(true); disabled = true; }
    else { disabled = false; dirty = true; }
}
// The whole chain dark now, the board's own LEDs too, before the supply goes: an LED kept powered (the USB cable feeds the chain) would otherwise keep
// its colour, and show it at the next start until leds_init. The application's frame is left as it is: a board that runs on shows it again.
void leds_esp32_blank(void) {
    if (!ready) leds_init();                             // before setup() reached it (the card reader at the start)
    static CRGB black[HW_NUM_LEDS];                      // (static: the DMA may read it after show() returns)
    FastLED[0].setLeds(black, HW_NUM_LEDS);
    FastLED.show();
    FastLED.show();                                      // the second waits for the first transfer to end: the black has reached the chain
    FastLED[0].setLeds(frame, HW_NUM_LEDS);
    dirty = true;
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
