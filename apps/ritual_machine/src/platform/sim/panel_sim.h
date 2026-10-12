#pragma once
// Simulator only: the "controls" window that shows the prototype's front panel (see hal/hal_input.h for the layout) and lets the
// mouse use it: click = press, wheel over a knob / encoder = turn, drag a knob = turn, drag the joystick pad = deflect.
#ifdef PLATFORM_SIM
#include "hal/hal_input.h"
#include "SDL.h"

#define PANEL_DRAW_HEIGHT 440     // height of the panel drawing; the display window picks its scale to be at least this tall

typedef void (*panel_emit_fn)(control_id_t ctl, input_kind_t kind, int value);

void panel_open(void);                                                    // creates the window next to the display window
int  panel_owns_window(Uint32 window_id);
// Mouse events of the panel window. Controls hit by the mouse are reported through `emit`.
void panel_handle_event(const SDL_Event *ev, panel_emit_fn emit);
// Keeps the drawn state in step with an event, wherever it came from (mouse or keyboard).
void panel_track(control_id_t ctl, input_kind_t kind, int value);
// What the application last did (app_t.status), the octave and the Shift state: drawn under the panel.
void panel_set_status(const char *status, int octave, int shift);
void panel_render(void);                                                  // redraws when something changed (cheap to call every loop)
#endif
