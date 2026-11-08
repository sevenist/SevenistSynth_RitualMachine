#pragma once
// The board as the tests see it (stubs.cpp).
#include <cstdint>
#include "hal/hal_display.h"
#include "hal/hal_input.h"

extern int stub_frames;                                         // frames sent to the display
extern uint16_t stub_frame[DISPLAY_WIDTH * DISPLAY_HEIGHT];     // the last colour frame sent
