#pragma once
// The project's controls and its screen, portable: app_name, app_start, app_event and app_draw (declared in the library's app/app_main.h, with
// the sound's calls of synth.h), called by the library's entry points (framework/app: main_esp32.cpp on the board, main_sim.cpp on the
// desktop). The library's HAL reports the board's controls (hal/hal_input.h) and gives the display (hal/hal_display.h, a u8g2 instance);
// ui/gui.h draws.
#include "app/app_main.h"
