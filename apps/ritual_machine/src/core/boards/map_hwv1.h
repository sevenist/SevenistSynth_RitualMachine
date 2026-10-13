#pragma once
// The Ritual Machine on HWV1 (the first prototype; its controls: framework/boards/hwv1/hwv1_controls.h): which control plays which
// role (core/controls.h). Included by core/controls.c only. Edit the rows to change the correspondence.
//
// The 36 keys are not listed: a matrix key takes the key role of its place, so the 4 function keys are the roles' function row 0 and
// the 4 x 8 note keys its rows 1..4 (what they do: the key layout, core/keymap.h; by default the function keys are Shift, Menu, Back,
// Play). The board has no encoder push, no Play / B1..B3 button and no right-hand knob: those roles are not present on it.
#include "boards/hwv1/hwv1_controls.h"
#include "core/controls.h"

// An endless pot plays an encoder role in steps, or a knob role as a position that starts at the middle: the value then jumps to the
// pot's counter on the first move (no pick-up).
static const control_map_row_t kMapHwv1Rows[] = {
    {HWV1_POT_0,  CTL_VOLUME,     IN_DELTA, 0},     // top left: steps from the current volume (a position jumped to the pot's own count)
    {HWV1_POT_1,  CTL_ENC_A,      IN_DELTA, 0},     // top centre: rows
    {HWV1_POT_2,  CTL_ENC_B,      IN_DELTA, 0},     // top right: value
    {HWV1_POT_3,  CTL_COL_KNOB_1, IN_VALUE, 512},   // around the joystick: top right
    {HWV1_POT_4,  CTL_COL_KNOB_3, IN_VALUE, 512},   // bottom right
    {HWV1_POT_5,  CTL_COL_KNOB_2, IN_VALUE, 512},   // bottom left
    {HWV1_POT_6,  CTL_COL_KNOB_0, IN_VALUE, 512},   // top left
    {HWV1_JOY_X,  CTL_JOY_X,      0, 0},
    {HWV1_JOY_Y,  CTL_JOY_Y,      0, 0},
    {HWV1_JOY_SW, CTL_JOY_SW,     0, 0},
};
