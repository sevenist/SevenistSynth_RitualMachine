#pragma once
// The simulator's LED frame, read by the Panel window (panel_sim.c) to draw each key's LED.
#include "hal/hal_leds.h"

led_color_t leds_sim_get(control_id_t ctl);
