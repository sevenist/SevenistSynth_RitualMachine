#pragma once
// The project's controls and its screen, portable: the library's HAL reports the controls (hal/hal_input.h) and gives the display
// (hal/hal_display.h, a u8g2 instance); ui/gui.h draws. The platform files (main_sim.cpp, main_esp32.cpp) call these three.
#include "hal/hal_display.h"
#include "hal/hal_input.h"

void app_start(u8g2_t *display);            // once, after synth_init()
void app_event(const input_event_t &e);     // a control was used
void app_draw();                            // call it often: redraws and sends the screen only when something changed
