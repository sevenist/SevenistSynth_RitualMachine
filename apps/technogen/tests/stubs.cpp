// What the tests stand in for: the board's display (a frame sent is kept, so a test can look at it) and its control table (HWV2's own,
// linked from framework/boards/hwv2/hwv2_controls.c).
#include "tests/stubs.h"
#include <cstring>
#include "boards/hwv2/hwv2_controls.h"

int stub_frames = 0;
uint16_t stub_frame[DISPLAY_WIDTH * DISPLAY_HEIGHT];

extern "C" void display_send_rgb565(const uint16_t *frame) {
    std::memcpy(stub_frame, frame, sizeof stub_frame);
    stub_frames++;
}
extern "C" void display_send(u8g2_t *) { stub_frames++; }
extern "C" const input_control_t *input_controls(int *count) { if (count) *count = HWV2_CONTROL_COUNT; return kHwv2Controls; }

led_color_t stub_leds[HWV2_CONTROL_COUNT];
int stub_led_shows = 0;
extern "C" void leds_init(void) { std::memset(stub_leds, 0, sizeof stub_leds); }
extern "C" void leds_set_brightness(uint8_t) {}
extern "C" void leds_clear(void) { std::memset(stub_leds, 0, sizeof stub_leds); }
extern "C" void leds_set(int ctl, led_color_t c) { if (ctl >= 0 && ctl < HWV2_CONTROL_COUNT) stub_leds[ctl] = c; }
extern "C" void leds_set_all(led_color_t c) { for (led_color_t &l : stub_leds) l = c; }
extern "C" void leds_show(void) { stub_led_shows++; }
