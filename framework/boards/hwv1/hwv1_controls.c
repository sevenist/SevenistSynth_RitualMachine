// The control table of HWV1 (hwv1_controls.h). The order is the ids' order.
//
// Panel places (px, py: half key pitches from the top left) are NOT MEASURED on the board: the keys are a regular grid, the pots and
// the joystick are put above it as the notes describe them. Nothing reads them until the simulator draws a board from its table.
#include "boards/hwv1/hwv1_controls.h"

#define KEY(name, row, col) {name, CK_KEY, row, col, (col) * 2, 7 + (row) * 2, 1}
#define ROW(r) KEY("KEY " #r ".0", r, 0), KEY("KEY " #r ".1", r, 1), KEY("KEY " #r ".2", r, 2), KEY("KEY " #r ".3", r, 3), \
               KEY("KEY " #r ".4", r, 4), KEY("KEY " #r ".5", r, 5), KEY("KEY " #r ".6", r, 6), KEY("KEY " #r ".7", r, 7)
#define POT(name, px, py)   {name, CK_ENDLESS, 0, 0, px, py, 0}

const input_control_t kHwv1Controls[HWV1_CONTROL_COUNT] = {
    KEY("F1", 0, 0), KEY("F2", 0, 1), KEY("F3", 0, 2), KEY("F4", 0, 3),
    ROW(1), ROW(2), ROW(3), ROW(4),
    POT("POT 0", 0, 1), POT("POT 1", 3, 1), POT("POT 2", 6, 1),                             // the top row, from the left
    POT("POT 3", 14, 0), POT("POT 4", 14, 4), POT("POT 5", 10, 4), POT("POT 6", 10, 0),     // around the joystick: TR, BR, BL, TL
    {"JOY X",    CK_AXIS,   0, 0, 12, 2, 0},
    {"JOY Y",    CK_AXIS,   0, 0, 12, 2, 0},
    {"JOY PUSH", CK_BUTTON, 0, 0, 12, 2, 0},
};
