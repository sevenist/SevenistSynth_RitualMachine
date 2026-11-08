// Where each LED of the second prototype sits in its chain (boards/esp32/board_esp32.h, board_led_of_control), from the PCB: the LED next to each
// switch and pot. The chain: K4, K3, K2, K1 (0..3), volume (4), the VU meter (5..10), the indicator (11), the top key row (12..15), R2, R3, R1
// (16..18), then the 4 x 4 as a serpentine. The VU meter and the indicator are under no control: HW_LED_NOT_APP. The VU meter is the board's own
// (board_leds_refresh): no app has to know it is there. The indicator stays off for now.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV2)
#include <Arduino.h>
#include "boards/esp32/board_esp32.h"
#include "boards/esp32/leds_esp32.h"
#include "boards/hwv2/hwv2_layout.h"

namespace {
const int8_t kKeyLeds[HWV2_KEY_ROWS][HWV2_KEY_COLS] = {
    {12, 13, 14, 15},                           // the function row, left to right
    {22, 21, 20, 19},
    {23, 24, 25, 26},
    {30, 29, 28, 27},
    {31, 32, 33, 34},
};
// From the bottom of the meter: as in the photo shoot, green, then orange, then red on top.
const led_color_t kVuColors[HWV2_VU_COUNT] = {{0, 128, 0}, {0, 128, 0}, {0, 128, 0}, {128, 50, 0}, {128, 50, 0}, {128, 0, 0}};
}  // namespace

int board_led_of_control(int ctl) {
    if (ctl >= HWV2_KEY_FIRST && ctl < HWV2_COL_KNOB_0) {
        const int k = ctl - HWV2_KEY_FIRST;
        return kKeyLeds[k / HWV2_KEY_COLS][k % HWV2_KEY_COLS];
    }
    switch (ctl) {
        case HWV2_VOLUME:     return 4;
        case HWV2_COL_KNOB_0: return 3;
        case HWV2_COL_KNOB_1: return 2;
        case HWV2_COL_KNOB_2: return 1;
        case HWV2_COL_KNOB_3: return 0;
        case HWV2_KNOB_R1:    return 18;
        case HWV2_KNOB_R2:    return 16;
        case HWV2_KNOB_R3:    return 17;
    }
    return -1;
}

void board_leds_refresh(void) {
    static uint32_t next_ms = 0;
    static float level = 0;
    const uint32_t now = millis();
    if ((int32_t)(now - next_ms) < 0) return;
    next_ms = now + HWV2_VU_FRAME_MS;
    level *= HWV2_VU_FALL;
    const int peak = board_audio_peak();
    if (peak > level) level = (float)peak;
    const int lit = hwv2_vu_lit((int)level);
    for (int i = 0; i < HWV2_VU_COUNT; i++)     // chain order runs from the top of the meter: bottom = the last LED
        leds_esp32_put(HWV2_VU_FIRST + HWV2_VU_COUNT - 1 - i, i < lit ? kVuColors[i] : led_color_t{0, 0, 0});
}
#endif
