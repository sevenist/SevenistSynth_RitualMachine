#include "core/bindings.h"
#include <stdio.h>

// A key of the matrix plays the note `arg` semitones above the base note (bottom-left key = 0, +1 to the right, +4 per row up,
// the extra row on top = 16..19).
#define NOTE_KEY(r, c) {CTL_KEY(r, c), IN_PRESS, MODS_ANY, ACT_NOTE, (KEY_ROWS - 1 - (r)) * KEY_COLS + (c)}

/* ===========================================================================================================================
 * THE BINDING TABLE. One row = one link:   { control, event kind, modifier state, action, argument }
 *
 *   Shift is the hold-action on BTN 3: rows marked MODS_SHIFT are active only while it is held, MODS_NONE only while it is not.
 *   To rebind a control, edit its row (or add one: a control can trigger several actions). The list of actions and what their
 *   argument means is in bindings.h.
 * ======================================================================================================================== */
const binding_t bindings[] = {
    /* ---- left strip ---- */
    {CTL_VOLUME,     IN_VALUE, MODS_ANY,   ACT_MASTER_VOLUME,  0},
    {CTL_ENC_A,      IN_DELTA, MODS_NONE,  ACT_ROW_MOVE,       +1},       // encoder A: rows ...
    {CTL_ENC_A,      IN_DELTA, MODS_SHIFT, ACT_PAGE_MOVE,      +1},       //            ... with Shift: pages
    {CTL_ENC_A_SW,   IN_PRESS, MODS_ANY,   ACT_ROW_TOP,         0},       // push: back to the page selector
    {CTL_ENC_B,      IN_DELTA, MODS_NONE,  ACT_VALUE_ADJUST,   +1},       // encoder B: the value ...
    {CTL_ENC_B,      IN_DELTA, MODS_SHIFT, ACT_VALUE_ADJUST,   +4},       //            ... with Shift: coarse (4 steps per detent)
    {CTL_ENC_B_SW,   IN_PRESS, MODS_ANY,   ACT_SELECT,          0},       // push: activate (Insert, Delete, Run ...)
    {CTL_PLAY,       IN_PRESS, MODS_ANY,   ACT_PLAY,            0},

    /* ---- matrix keyboard: notes ---- */
    NOTE_KEY(0, 0), NOTE_KEY(0, 1), NOTE_KEY(0, 2), NOTE_KEY(0, 3),       // extra row
    NOTE_KEY(1, 0), NOTE_KEY(1, 1), NOTE_KEY(1, 2), NOTE_KEY(1, 3),
    NOTE_KEY(2, 0), NOTE_KEY(2, 1), NOTE_KEY(2, 2), NOTE_KEY(2, 3),
    NOTE_KEY(3, 0), NOTE_KEY(3, 1), NOTE_KEY(3, 2), NOTE_KEY(3, 3),
    NOTE_KEY(4, 0), NOTE_KEY(4, 1), NOTE_KEY(4, 2), NOTE_KEY(4, 3),       // bottom row: the lowest notes

    /* ---- matrix keyboard: the knob above each column edits row 1..4 of the current page ---- */
    {CTL_COL_KNOB_0, IN_VALUE, MODS_ANY,   ACT_PAGE_KNOB,       1},
    {CTL_COL_KNOB_1, IN_VALUE, MODS_ANY,   ACT_PAGE_KNOB,       2},
    {CTL_COL_KNOB_2, IN_VALUE, MODS_ANY,   ACT_PAGE_KNOB,       3},
    {CTL_COL_KNOB_3, IN_VALUE, MODS_ANY,   ACT_PAGE_KNOB,       4},

    /* ---- right section ---- */
    {CTL_KNOB_R1,    IN_VALUE, MODS_NONE,  ACT_MACRO,           0},       // macros ...
    {CTL_KNOB_R2,    IN_VALUE, MODS_NONE,  ACT_MACRO,           1},
    {CTL_KNOB_R3,    IN_VALUE, MODS_NONE,  ACT_MACRO,           2},
    {CTL_KNOB_R1,    IN_VALUE, MODS_SHIFT, ACT_MACRO_LEARN,     0},       // ... with Shift: assign the parameter under the cursor
    {CTL_KNOB_R2,    IN_VALUE, MODS_SHIFT, ACT_MACRO_LEARN,     1},
    {CTL_KNOB_R3,    IN_VALUE, MODS_SHIFT, ACT_MACRO_LEARN,     2},

    {CTL_JOY_UP,     IN_PRESS, MODS_NONE,  ACT_NAV,            NAV_UP},   // joystick: moves the focus (latched: edits the value)
    {CTL_JOY_DOWN,   IN_PRESS, MODS_NONE,  ACT_NAV,            NAV_DOWN},
    {CTL_JOY_LEFT,   IN_PRESS, MODS_NONE,  ACT_NAV,            NAV_LEFT},
    {CTL_JOY_RIGHT,  IN_PRESS, MODS_NONE,  ACT_NAV,            NAV_RIGHT},
    {CTL_JOY_UP,     IN_PRESS, MODS_SHIFT, ACT_OCTAVE,         +1},       // with Shift: octave up / down, pages
    {CTL_JOY_DOWN,   IN_PRESS, MODS_SHIFT, ACT_OCTAVE,         -1},
    {CTL_JOY_LEFT,   IN_PRESS, MODS_SHIFT, ACT_PAGE_MOVE,      -1},
    {CTL_JOY_RIGHT,  IN_PRESS, MODS_SHIFT, ACT_PAGE_MOVE,      +1},
    {CTL_JOY_SW,     IN_PRESS, MODS_ANY,   ACT_LATCH,           0},       // push: latch the focused value / activate a button

    {CTL_BTN_1,      IN_PRESS, MODS_ANY,   ACT_MENU,            0},
    {CTL_BTN_2,      IN_PRESS, MODS_ANY,   ACT_BACK,            0},
    {CTL_BTN_3,      IN_PRESS, MODS_ANY,   ACT_SHIFT,           0},       // hold = Shift
};
const int bindings_count = (int)(sizeof bindings / sizeof bindings[0]);

int action_is_hold(action_id_t a) { return a == ACT_SHIFT || a == ACT_NOTE; }

const char *action_name(action_id_t a) {
    static const char *const n[ACT_COUNT] = {
        [ACT_NONE] = "-", [ACT_ROW_MOVE] = "Row", [ACT_NAV] = "Nav", [ACT_LATCH] = "Latch", [ACT_VALUE_ADJUST] = "Value", [ACT_PAGE_MOVE] = "Page", [ACT_ROW_TOP] = "Top",
        [ACT_SELECT] = "Select", [ACT_MENU] = "Menu", [ACT_BACK] = "Back", [ACT_PLAY] = "Play", [ACT_SHIFT] = "Shift",
        [ACT_NOTE] = "Note", [ACT_OCTAVE] = "Octave", [ACT_PAGE_KNOB] = "Page knob", [ACT_MACRO] = "Macro",
        [ACT_MACRO_LEARN] = "Learn", [ACT_MASTER_VOLUME] = "Volume",
    };
    return (a >= 0 && a < ACT_COUNT && n[a]) ? n[a] : "?";
}

const char *control_name(control_id_t c) {
    static const char *const n[CTL_COUNT] = {
        [CTL_NONE] = "-", [CTL_VOLUME] = "VOLUME", [CTL_ENC_A] = "ENC A", [CTL_ENC_A_SW] = "ENC A SW", [CTL_ENC_B] = "ENC B",
        [CTL_ENC_B_SW] = "ENC B SW", [CTL_PLAY] = "PLAY",
        [CTL_COL_KNOB_0] = "COL KNOB 1", [CTL_COL_KNOB_1] = "COL KNOB 2", [CTL_COL_KNOB_2] = "COL KNOB 3", [CTL_COL_KNOB_3] = "COL KNOB 4",
        [CTL_KNOB_R1] = "KNOB R1", [CTL_KNOB_R2] = "KNOB R2", [CTL_KNOB_R3] = "KNOB R3",
        [CTL_JOY_X] = "JOY X", [CTL_JOY_Y] = "JOY Y", [CTL_JOY_SW] = "JOY PUSH", [CTL_BTN_1] = "BTN 1", [CTL_BTN_2] = "BTN 2", [CTL_BTN_3] = "BTN 3",
        [CTL_JOY_LEFT] = "JOY LEFT", [CTL_JOY_RIGHT] = "JOY RIGHT", [CTL_JOY_UP] = "JOY UP", [CTL_JOY_DOWN] = "JOY DOWN",
    };
    static char key[16];
    if (c >= CTL_KEY_FIRST && c <= CTL_KEY_LAST) {
        snprintf(key, sizeof key, "KEY %d.%d", (c - CTL_KEY_FIRST) / KEY_COLS, (c - CTL_KEY_FIRST) % KEY_COLS);
        return key;
    }
    return (c >= 0 && c < CTL_COUNT && n[c]) ? n[c] : "?";
}
