#pragma once
// The Ritual Machine's controls: the ROLES its bindings, key layouts, layers and saved files are written against, and the map from a
// board's own controls (hal/hal_input.h: each board has its table) to them.
//
// The roles describe the panel the app was designed for (the numbers are the ids below):
//
//   left strip (top -> bottom)     matrix keyboard (up to 8 columns x 5 rows)   right section
//     CTL_VOLUME  master knob         K1    K2    K3    K4   <- column knobs       R1  R2  R3   <- 3 knobs
//     CTL_ENC_A   encoder + switch    [k]   [k]   [k]   [k] ...  <- function row (0)  joystick (X / Y / push)
//     CTL_ENC_B   encoder + switch    [k]   [k]   [k]   [k] ...  <- main rows 1..4    B1  B2  B3  <- 3 buttons
//     CTL_PLAY    button              ...
//
// A board plays the roles it can: one small file per board says which of its controls is which role (core/boards/map_<board>.h).
// A matrix key takes the key role of its place, CTL_KEY(row, col), without a line in the map; what every key does is chosen at run
// time (core/keymap.h: layouts, the KEYS tab of the menu). A board control without a role does nothing; a role no control plays is
// "not present" (the KEYS and MODIFIERS tabs list only what is present). What a role does: core/bindings.c.
//
// What each kind of role receives:
//   button / key / encoder switch / joystick push : IN_PRESS and IN_RELEASE (value unused)
//   encoder                                        : IN_DELTA, value = detents since the last event (+ clockwise)
//   knob / joystick axis                           : IN_VALUE, value = position 0..INPUT_VALUE_MAX (axis centre = INPUT_AXIS_CENTER)
// An event of the app is the HAL's input_event_t with the role in `ctl` (controls_event makes it from the board's).
#include <stdbool.h>
#include <stdint.h>
#include "hal/hal_input.h"
#include "hal/hal_leds.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_COLS 8
#define KEY_ROWS 5                                  // row 0 = the function row above the note rows
#define KEY_COUNT (KEY_ROWS * KEY_COLS)
#define CTL_KEY(row, col) (CTL_KEY_FIRST + (row) * KEY_COLS + (col))

typedef enum {
    CTL_NONE = 0,
    // left strip
    CTL_VOLUME,                                     // master volume knob
    CTL_ENC_A, CTL_ENC_A_SW,
    CTL_ENC_B, CTL_ENC_B_SW,
    CTL_PLAY,
    // matrix keyboard: CTL_KEY(row, col), row 0 = function row (top), row 4 = bottom, col 0 = left
    CTL_KEY_FIRST,
    CTL_KEY_LAST = CTL_KEY_FIRST + KEY_COLS * KEY_ROWS - 1,
    CTL_COL_KNOB_0, CTL_COL_KNOB_1, CTL_COL_KNOB_2, CTL_COL_KNOB_3,     // one knob above each column
    // right section
    CTL_KNOB_R1, CTL_KNOB_R2, CTL_KNOB_R3,
    CTL_JOY_X, CTL_JOY_Y, CTL_JOY_SW,
    CTL_BTN_1, CTL_BTN_2, CTL_BTN_3,
    CTL_HW_COUNT,
    // Virtual controls made by the application from the joystick axes (app/app_input.c): deflection past the dead zone
    // reports IN_PRESS / IN_RELEASE (with key repeat) so bindings can treat the stick as four buttons.
    CTL_JOY_LEFT = CTL_HW_COUNT, CTL_JOY_RIGHT, CTL_JOY_UP, CTL_JOY_DOWN,
    CTL_COUNT
} control_id_t;

/* ---------------- a board's map (core/controls.c; the maps themselves: core/boards/map_*.h) ---------------- */

typedef struct {
    uint8_t  board_ctl;     // the board's control: its id in the board's table
    uint8_t  role;          // control_id_t
    uint8_t  reading;       // an endless pot: IN_DELTA (steps, as an encoder role wants) or IN_VALUE (a position, as a knob role wants); 0 otherwise
    uint16_t start;         // IN_VALUE: where the position starts
} control_map_row_t;

typedef struct {
    const char              *board;     // input_board_name()
    const control_map_row_t *rows;
    int                      count;
} control_map_t;

#define CONTROLS_BOARD_MAX 96                       // the largest board table a map can bind (a control past it has no role)

const control_map_t *controls_map_of(const char *board);                // NULL: the app has no map for that board
// Binds a board's table to the roles: its matrix keys by place, the rest by the map's rows (a NULL map: the keys only).
void         controls_bind(const input_control_t *table, int count, const control_map_t *map);
control_id_t controls_role(int board_ctl);                              // CTL_NONE: that control has no role
int          controls_board_ctl(control_id_t role);                     // INPUT_NO_CONTROL: no control plays that role
bool         controls_listed(control_id_t role);                        // a control plays it and has a place on the panel
input_event_t controls_event(input_event_t board_event);                // the board's event as the app's (kind IN_NONE: no role)

/* ---------------- on the board (platform/controls_board.c; the host tests and ui_dump have their own: tools/ui_stubs.c) ---------------- */

void controls_init(void);                           // binds the board the program runs on and sets how its endless pots report; before the board's scan starts
bool controls_key_present(int row, int col);        // the board has the key CTL_KEY(row, col): the KEYS menu tab lists only the keys that exist
bool controls_present(control_id_t c);              // ... a control that is not a matrix key (the MODIFIERS menu tab lists only those)
void controls_led(control_id_t c, led_color_t color);   // the LED under the control that plays the role (hal_leds.h), if it has one

#ifdef __cplusplus
}
#endif
