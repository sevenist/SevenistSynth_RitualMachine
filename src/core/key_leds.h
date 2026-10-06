#pragma once
// The LEDs under the keys (hal_leds.h): play feedback. Each key shows what the key layout (keymap.h) gives it:
//   note keys   piano colours (C teal, other naturals dim white, sharps dim blue), bright orange while held
//   Shift       yellow, bright while Shift is on          Menu    blue, bright while the menu is open
//   Back        red                                        Play    green, bright while the sequencer runs
//   Octave +/-  violet, bright when the keyboard is shifted that way      navigation keys  dim white; unused keys off
// Colours are full-scale here; the LED driver applies the global brightness (capped on the prototype, see board_pins.h).
#include <stdbool.h>
#include <stdint.h>
#include "core/app.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called at the end of every app step. Repaints at most every KEY_LEDS_PERIOD_MS, at once after an input event.
void key_leds_update(const app_t *app, bool input_event, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
