#pragma once
// The controls of HWV2 (the second prototype, KiCad project GRV-mini): their ids and the board's table (hal/hal_input.h). Plain data, no hardware
// code: it also compiles on the desktop (the host tests read it; the simulator built with -Board hwv2 draws the board from it).
//
// The panel the desktop draws, in hardware: the same parts, the same order, the same places (boards/desktop/panel_controls.h).
//
//   left strip (top -> bottom)     matrix keyboard (4 columns x 5 rows)          right section
//     VOLUME   master knob           K1    K2    K3    K4   <- column knobs        R1  R2  R3   <- 3 knobs
//     ENC A    SW24                  [k]   [k]   [k]   [k]  <- function row (0)    joystick (X / Y / push)
//     ENC B    SW25                  [k]   [k]   [k]   [k]  <- main rows 1..4      B1  B2  B3   <- SW22, SW19, SW21
//     PLAY     SW23                  ...
#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HWV2_KEY_ROWS 5                 // row 0 = the function row above the note rows
#define HWV2_KEY_COLS 4

enum {
    HWV2_VOLUME,
    HWV2_ENC_A, HWV2_ENC_A_SW,
    HWV2_ENC_B, HWV2_ENC_B_SW,
    HWV2_PLAY,
    HWV2_KEY_FIRST,                                         // HWV2_KEY(row, col), row by row from the top left
    HWV2_COL_KNOB_0 = HWV2_KEY_FIRST + HWV2_KEY_ROWS * HWV2_KEY_COLS,
    HWV2_COL_KNOB_1, HWV2_COL_KNOB_2, HWV2_COL_KNOB_3,      // one knob above each column
    HWV2_KNOB_R1, HWV2_KNOB_R2, HWV2_KNOB_R3,
    HWV2_JOY_X, HWV2_JOY_Y, HWV2_JOY_SW,
    HWV2_BTN_1, HWV2_BTN_2, HWV2_BTN_3,
    HWV2_CONTROL_COUNT
};

#define HWV2_KEY(row, col) (HWV2_KEY_FIRST + (row) * HWV2_KEY_COLS + (col))

extern const input_control_t kHwv2Controls[HWV2_CONTROL_COUNT];

#ifdef __cplusplus
}
#endif
