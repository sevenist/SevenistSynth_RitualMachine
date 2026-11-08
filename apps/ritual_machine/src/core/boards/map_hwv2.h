#pragma once
// The Ritual Machine on HWV2 (the second prototype; its controls: framework/boards/hwv2/hwv2_controls.h): which control plays which
// role (core/controls.h). Included by core/controls.c only. The roles were written for this panel, so every control has its role, one
// to one; the keys are not listed (a matrix key takes the key role of its place). The board has 4 key columns: the key roles of
// columns 5..8 are not present on it.
#include "boards/hwv2/hwv2_controls.h"
#include "core/controls.h"

static const control_map_row_t kMapHwv2Rows[] = {
    {HWV2_VOLUME,     CTL_VOLUME,     0, 0},
    {HWV2_ENC_A,      CTL_ENC_A,      0, 0}, {HWV2_ENC_A_SW, CTL_ENC_A_SW, 0, 0},
    {HWV2_ENC_B,      CTL_ENC_B,      0, 0}, {HWV2_ENC_B_SW, CTL_ENC_B_SW, 0, 0},
    {HWV2_PLAY,       CTL_PLAY,       0, 0},
    {HWV2_COL_KNOB_0, CTL_COL_KNOB_0, 0, 0}, {HWV2_COL_KNOB_1, CTL_COL_KNOB_1, 0, 0},
    {HWV2_COL_KNOB_2, CTL_COL_KNOB_2, 0, 0}, {HWV2_COL_KNOB_3, CTL_COL_KNOB_3, 0, 0},
    {HWV2_KNOB_R1,    CTL_KNOB_R1,    0, 0}, {HWV2_KNOB_R2, CTL_KNOB_R2, 0, 0}, {HWV2_KNOB_R3, CTL_KNOB_R3, 0, 0},
    {HWV2_JOY_X,      CTL_JOY_X,      0, 0}, {HWV2_JOY_Y, CTL_JOY_Y, 0, 0}, {HWV2_JOY_SW, CTL_JOY_SW, 0, 0},
    {HWV2_BTN_1,      CTL_BTN_1,      0, 0}, {HWV2_BTN_2, CTL_BTN_2, 0, 0}, {HWV2_BTN_3, CTL_BTN_3, 0, 0},
};
