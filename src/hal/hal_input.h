#pragma once
// HAL: user input. One implementation per platform (desktop simulator, ESP32...).
//
// The HAL knows the PHYSICAL controls of the prototype board and nothing about what they do: it reports "control X was
// pressed / turned / moved" and the application decides the meaning in core/bindings.c (the binding table).
//
// Board layout (the numbers are the control ids below):
//
//   left strip (top -> bottom)     matrix keyboard (up to 8 columns x 5 rows)   right section
//     CTL_VOLUME  master knob         K1    K2    K3    K4   <- column knobs       R1  R2  R3   <- 3 knobs
//     CTL_ENC_A   encoder + switch    [k]   [k]   [k]   [k] ...  <- function row (0)  joystick (X / Y / push)
//     CTL_ENC_B   encoder + switch    [k]   [k]   [k]   [k] ...  <- main rows 1..4    B1  B2  B3  <- 3 buttons
//     CTL_PLAY    button              ...
//
// The matrix has room for 8 columns (the first prototype has 4 function keys and 4 x 8 note keys); a board reports which keys it really
// has with input_key_present(). What every key does is chosen at run time (core/keymap.h: layouts, the KEYS tab of the menu).
//
// What each kind of control reports:
//   button / key / encoder switch / joystick push : IN_PRESS and IN_RELEASE (value unused)
//   encoder                                        : IN_DELTA, value = detents since the last event (+ clockwise)
//   knob / joystick axis                           : IN_VALUE, value = position 0..INPUT_VALUE_MAX (axis centre = INPUT_AXIS_CENTER)
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INPUT_VALUE_MAX   1023                      // full travel of a knob / axis (10 bit, ESP32 ADC values are scaled to it)
#define INPUT_AXIS_CENTER 512

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
    // Virtual controls made by the application from the joystick axes (core/bindings.c): deflection past the dead zone
    // reports IN_PRESS / IN_RELEASE (with key repeat) so bindings can treat the stick as four buttons.
    CTL_JOY_LEFT = CTL_HW_COUNT, CTL_JOY_RIGHT, CTL_JOY_UP, CTL_JOY_DOWN,
    CTL_COUNT
} control_id_t;

typedef enum { IN_NONE, IN_PRESS, IN_RELEASE, IN_DELTA, IN_VALUE } input_kind_t;

typedef struct {
    control_id_t ctl;       // which control
    input_kind_t kind;      // what happened
    int          value;     // IN_DELTA: signed detents; IN_VALUE: 0..INPUT_VALUE_MAX
    bool         quit;      // window closed / platform asks to exit (ctl is CTL_NONE)
} input_event_t;

// Non-blocking poll. Returns an event with kind == IN_NONE (and quit == false) when nothing happened.
input_event_t input_poll(void);
bool input_pending(void);                           // more events are queued: the app handles them before it redraws

// Whether the board has the key CTL_KEY(row, col): the KEYS menu tab lists only the keys that exist.
bool input_key_present(int row, int col);

// The top-left function key CTL_KEY(0, 0) was held at power-on (for about 2 s, read before the key scan starts: a key that is already down
// makes no press event). The application resets the key layout. Platforms without it return false.
bool input_boot_reset(void);

#ifdef __cplusplus
}
#endif
