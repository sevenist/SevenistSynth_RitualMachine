#pragma once
// The controls of HWV1 (the first prototype): their ids and the board's table (hal/hal_input.h). Plain data, no hardware code: it
// also compiles on the desktop (the host tests read it; the simulator built with -Board hwv1 draws the board from it).
//
//   36 keys      the 4 function keys (the 4 leftmost places of the top row) and 4 rows of 8, each with an LED
//   7 pots       without end stops (SynthBox numbering: 0, 1, 2 on the top row, 3..6 around the joystick: TL = 6, TR = 3, BL = 5, BR = 4)
//   a joystick   two axes and its push
//
// How the parts are wired (the key controller's rows and columns, each pot's mux channels and direction): hwv1_layout.h.
#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HWV1_KEY_ROWS 5                 // row 0 = the function keys
#define HWV1_KEY_COLS 8
#define HWV1_FN_KEYS  4

enum {
    HWV1_KEY_F1, HWV1_KEY_F2, HWV1_KEY_F3, HWV1_KEY_F4,
    HWV1_KEY_NOTES,                                         // rows 1..4, 8 keys each, from the top left: see HWV1_KEY
    HWV1_POT_0 = HWV1_KEY_NOTES + (HWV1_KEY_ROWS - 1) * HWV1_KEY_COLS,
    HWV1_POT_1, HWV1_POT_2, HWV1_POT_3, HWV1_POT_4, HWV1_POT_5, HWV1_POT_6,
    HWV1_JOY_X, HWV1_JOY_Y, HWV1_JOY_SW,
    HWV1_CONTROL_COUNT
};

// The key at (row from the top, column from the left); INPUT_NO_CONTROL where the top row has no key.
#define HWV1_KEY(row, col) ((row) > 0 ? HWV1_KEY_NOTES + ((row) - 1) * HWV1_KEY_COLS + (col) : (col) < HWV1_FN_KEYS ? HWV1_KEY_F1 + (col) : INPUT_NO_CONTROL)

extern const input_control_t kHwv1Controls[HWV1_CONTROL_COUNT];

#ifdef __cplusplus
}
#endif
