#pragma once
// The Ritual Machine on the desktop board (the simulator's panel; its controls: framework/boards/desktop/panel_controls.h): which
// control plays which role (core/controls.h). Included by core/controls.c only. The roles were written for this panel, so every
// control has its role, one to one; the keys are not listed (a matrix key takes the key role of its place).
#include "boards/desktop/panel_controls.h"
#include "core/controls.h"

static const control_map_row_t kMapPanelRows[] = {
    {PANEL_VOLUME,     CTL_VOLUME,     0, 0},
    {PANEL_ENC_A,      CTL_ENC_A,      0, 0}, {PANEL_ENC_A_SW, CTL_ENC_A_SW, 0, 0},
    {PANEL_ENC_B,      CTL_ENC_B,      0, 0}, {PANEL_ENC_B_SW, CTL_ENC_B_SW, 0, 0},
    {PANEL_PLAY,       CTL_PLAY,       0, 0},
    {PANEL_COL_KNOB_0, CTL_COL_KNOB_0, 0, 0}, {PANEL_COL_KNOB_1, CTL_COL_KNOB_1, 0, 0},
    {PANEL_COL_KNOB_2, CTL_COL_KNOB_2, 0, 0}, {PANEL_COL_KNOB_3, CTL_COL_KNOB_3, 0, 0},
    {PANEL_KNOB_R1,    CTL_KNOB_R1,    0, 0}, {PANEL_KNOB_R2, CTL_KNOB_R2, 0, 0}, {PANEL_KNOB_R3, CTL_KNOB_R3, 0, 0},
    {PANEL_JOY_X,      CTL_JOY_X,      0, 0}, {PANEL_JOY_Y, CTL_JOY_Y, 0, 0}, {PANEL_JOY_SW, CTL_JOY_SW, 0, 0},
    {PANEL_BTN_1,      CTL_BTN_1,      0, 0}, {PANEL_BTN_2, CTL_BTN_2, 0, 0}, {PANEL_BTN_3, CTL_BTN_3, 0, 0},
};
