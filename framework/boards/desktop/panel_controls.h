#pragma once
// The controls of the desktop's own board (the panel: a drawing of the future prototype): their ids and the board's table
// (hal/hal_input.h). Plain data: the host tests read it too. The simulator draws its panel window from this table, as it draws any
// other board it is built for (sim_board.h).
//
//   left strip (top -> bottom)     matrix keyboard (4 columns x 5 rows)          right section
//     VOLUME   master knob           K1    K2    K3    K4   <- column knobs        R1  R2  R3   <- 3 knobs
//     ENC A    encoder + switch      [k]   [k]   [k]   [k]  <- function row (0)    joystick (X / Y / push)
//     ENC B    encoder + switch      [k]   [k]   [k]   [k]  <- main rows 1..4      B1  B2  B3   <- 3 buttons
//     PLAY     button                ...
#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_KEY_ROWS 5                // row 0 = the function row above the note rows
#define PANEL_KEY_COLS 4

enum {
    PANEL_VOLUME,
    PANEL_ENC_A, PANEL_ENC_A_SW,
    PANEL_ENC_B, PANEL_ENC_B_SW,
    PANEL_PLAY,
    PANEL_KEY_FIRST,                                        // PANEL_KEY(row, col), row by row from the top left
    PANEL_COL_KNOB_0 = PANEL_KEY_FIRST + PANEL_KEY_ROWS * PANEL_KEY_COLS,
    PANEL_COL_KNOB_1, PANEL_COL_KNOB_2, PANEL_COL_KNOB_3,   // one knob above each column
    PANEL_KNOB_R1, PANEL_KNOB_R2, PANEL_KNOB_R3,
    PANEL_JOY_X, PANEL_JOY_Y, PANEL_JOY_SW,
    PANEL_BTN_1, PANEL_BTN_2, PANEL_BTN_3,
    PANEL_CONTROL_COUNT
};

#define PANEL_KEY(row, col) (PANEL_KEY_FIRST + (row) * PANEL_KEY_COLS + (col))

extern const input_control_t kPanelControls[PANEL_CONTROL_COUNT];

#ifdef __cplusplus
}
#endif
