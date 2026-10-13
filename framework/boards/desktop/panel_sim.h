#pragma once
// Simulator only: the "controls" window. It shows the front panel of the board the program was built for, drawn from the board's
// table (sim_board.h), and lets the mouse use it: click = press, wheel over a knob / pot / encoder = turn, drag a knob or a pot =
// turn, drag the joystick = deflect, right click on a joystick or a pot = its push.
#ifdef PLATFORM_SIM
#include "hal/hal_input.h"
#include "boards/desktop/sim_board.h"
#include "SDL.h"

typedef void (*panel_emit_fn)(int ctl, input_kind_t kind, int value);

const sim_panel_t *panel_layout(void);                                    // the board's parts and the size of the drawing (no window needed)
void panel_open(void);                                                    // creates the window next to the display window
int  panel_owns_window(Uint32 window_id);
// Mouse events of the panel window. Controls hit by the mouse are reported through `emit`; a turn of an endless pot is reported as
// steps (IN_DELTA), whatever the pot's reading is (input_sim.c makes the position).
void panel_handle_event(const SDL_Event *ev, panel_emit_fn emit);
// Keeps the drawn state in step with an event, wherever it came from (mouse or keyboard).
void panel_track(int ctl, input_kind_t kind, int value);
// What the application last did (a line of text), its octave and its Shift state: drawn under the panel.
void panel_set_status(const char *status, int octave, int shift);
void panel_render(void);                                                  // redraws when something changed (cheap to call every loop)

// The PC key(s) that work a control, as a short text for its part ("" = the mouse only). The keys are input_sim.c's.
const char *input_sim_key_hint(int ctl);
#endif
