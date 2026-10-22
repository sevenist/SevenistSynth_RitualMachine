#pragma once
// The simulator's LED frame, read by the Panel window (panel_sim.c) to draw each key's LED.
#include "hal/hal_leds.h"

led_color_t leds_sim_get(int ctl);
unsigned    leds_sim_rev(void);        // changes when a leds_show() changed a LED: the panel redraws then, with no control moving
