#pragma once
// The board the desktop program simulates, and the parts its panel window draws.
//
// The board is picked when the program is built: .\build.ps1 -Board <name> (framework/tools/sim_build.ps1, Get-SimBoard, defines
// SIM_BOARD_<NAME> and adds the board's table to the sources). Nothing said = the desktop's own board, the panel
// (panel_controls.h). The desktop's files (input_sim.c, panel_sim.c, leds_sim.c) know a board through the names below only.
// To simulate another board: its branch here and its entry in Get-SimBoard.
//
// The panel window is drawn from the board's table (hal/hal_input.h): each control has a place (px, py), the centre of its part in
// half key pitches. Controls at the same place are one part: an encoder or a pot with its push, a joystick's two axes and its push.
#include "hal/hal_input.h"

#if defined(SIM_BOARD_HWV1)
#include "boards/hwv1/hwv1_controls.h"
#define SIM_BOARD_NAME      "hwv1"
#define SIM_CONTROLS        kHwv1Controls
#define SIM_CONTROL_COUNT   HWV1_CONTROL_COUNT
#define SIM_TURN_KEYS_FIRST HWV1_POT_1, HWV1_POT_2      // the top centre and top right pots (pot 0, top left, comes third)
#else
#include "boards/desktop/panel_controls.h"
#define SIM_BOARD_NAME      "panel"
#define SIM_CONTROLS        kPanelControls
#define SIM_CONTROL_COUNT   PANEL_CONTROL_COUNT
#define SIM_TURN_KEYS_FIRST PANEL_ENC_A, PANEL_ENC_B
#endif
// SIM_TURN_KEYS_FIRST: the turning parts that take the PC's first two key pairs ([ ] and ; '); the others follow in the table's
// order (input_sim.c).

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PART_KEY, PART_BUTTON, PART_ENCODER, PART_ENDLESS, PART_KNOB, PART_JOYSTICK } sim_part_kind_t;

typedef struct {
    uint8_t kind;           // sim_part_kind_t
    int16_t ctl;            // the control (a joystick: its X axis, the first axis of the table at that place)
    int16_t ctl2;           // a joystick: its Y axis (INPUT_NO_CONTROL: none)
    int16_t sw;             // the push: a button at the place of an encoder, a pot or a joystick (INPUT_NO_CONTROL: none)
    int16_t x, y;           // the centre, in pixels of the drawing
    int16_t hw, hh;         // half the width and half the height of the body (a round part: its radius, twice)
} sim_part_t;

#define SIM_PARTS_MAX      96
#define SIM_HALF_PITCH     27       // pixels per unit of px / py
#define SIM_PANEL_TOP      26       // above the parts: the title line
#define SIM_PANEL_MARGIN   14       // left, right and under the parts
#define SIM_PANEL_STATUS   48       // under that: the app's two status lines

typedef struct {
    sim_part_t parts[SIM_PARTS_MAX];
    int        count;
    int        width, height;       // of the whole drawing
} sim_panel_t;

// The parts of a board, in the table's order, placed so that the drawing starts at the margins. Plain data in, no window: the host
// tests lay out every board.
void sim_panel_build(sim_panel_t *panel, const input_control_t *table, int count);
// The room a part takes with its texts (its name and its PC keys under a round part).
void sim_part_box(const sim_part_t *part, int *left, int *top, int *right, int *bottom);

#ifdef __cplusplus
}
#endif
