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
