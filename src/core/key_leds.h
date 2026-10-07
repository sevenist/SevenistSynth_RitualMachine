#pragma once
// The LEDs under the keys (hal_leds.h): play feedback. Each key shows the colour of its role (core/led_roles.h: from its function in the key
// layout): the Idle colour, or the Active one while a note is held, Shift / Mod is held, the menu is open, the sequencer runs, the keyboard
// is shifted that way (Octave) or a jump slot is saved. The colours are edited in the LEDS tab; while it is on screen the keys of the role
// shown light in its Active colour. The LED driver applies the global brightness (capped on the prototype, see board_pins.h).
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
