// The control table of the desktop's own board (panel_controls.h). The order is the ids' order. Panel places (px, py) are the centres
// of the parts in half key pitches: the simulator's panel window is drawn from them (sim_board.c, panel_sim.c). An encoder's push
// and the joystick's push sit at the place of their part.
#include "boards/desktop/panel_controls.h"

#define KEY(row, col)  {"KEY " #row "." #col, CK_KEY, row, col, 5 + (col) * 2, 4 + (row) * 2, 1}
#define ROW(r)         KEY(r, 0), KEY(r, 1), KEY(r, 2), KEY(r, 3)
#define PART(name, kind, px, py) {name, kind, 0, 0, px, py, 0}

const input_control_t kPanelControls[PANEL_CONTROL_COUNT] = {
    PART("VOLUME",   CK_KNOB,    2, 2),
    PART("ENC A",    CK_ENCODER, 2, 5),  PART("ENC A SW", CK_BUTTON, 2, 5),
    PART("ENC B",    CK_ENCODER, 2, 9),  PART("ENC B SW", CK_BUTTON, 2, 9),
    PART("PLAY",     CK_BUTTON,  2, 12),
    ROW(0), ROW(1), ROW(2), ROW(3), ROW(4),
    PART("K1",       CK_KNOB,    5, 2),  PART("K2", CK_KNOB, 7, 2),  PART("K3", CK_KNOB, 9, 2),  PART("K4", CK_KNOB, 11, 2),
    PART("R1",       CK_KNOB,   15, 2),  PART("R2", CK_KNOB, 17, 2), PART("R3", CK_KNOB, 19, 2),
    PART("JOY X",    CK_AXIS,   17, 6),  PART("JOY Y", CK_AXIS, 17, 6),  PART("JOY PUSH", CK_BUTTON, 17, 6),
    PART("B1",       CK_BUTTON, 14, 12), PART("B2", CK_BUTTON, 16, 12),  PART("B3", CK_BUTTON, 18, 12),
};
