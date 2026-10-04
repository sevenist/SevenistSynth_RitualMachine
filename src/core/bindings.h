#pragma once
// The link between hardware controls and what they do: ONE table (bindings.c). Changing what a button, encoder or knob does
// means changing a row there; no other file needs to know which control triggers which action.
//
//   control (hal/hal_input.h) + kind of event + modifier state  ->  action + argument
//
// Actions are implemented in core/app.c (app_run_action). To add one: a value in action_id_t, its name in action_names[]
// (bindings.c), a case in app_run_action, then use it in the table.
#include <stdint.h>
#include "hal/hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ACT_NONE,
    // navigation (arg = direction / amount; an encoder multiplies it by the detents it turned)
    ACT_ROW_MOVE,           // rows: +1 = next row (down), -1 = previous
    ACT_NAV,                // the joystick: arg = NAV_LEFT / RIGHT / UP / DOWN moves the focus (on a declarative screen, see ui_screen.h)
    ACT_LATCH,              // joystick push: latches the focused value (the joystick then edits it) or activates a button
    ACT_VALUE_ADJUST,       // steps of the focused value, no latch needed (encoder B): +1 = increase; on row 0 it changes the page / tab
    ACT_PAGE_MOVE,          // page (or menu tab) from any row: +1 = next
    ACT_ROW_TOP,            // jump to the page selector
    ACT_SELECT,             // push-to-activate (Insert / Delete / Run rows...): like +1 on a row
    ACT_MENU,               // open / close the menu
    ACT_BACK,               // delete the selected module (RACK tab)
    ACT_PLAY,               // start / stop the sequencer
    // modifier and keyboard
    ACT_SHIFT,              // HOLD action: the Shift modifier is on while the control is down
    ACT_NOTE,               // HOLD action: arg = semitones above the keyboard base note; the octave is added; release = note off
    ACT_OCTAVE,             // arg = +1 / -1: shifts the keyboard by an octave
    // knobs (value = position 0..INPUT_VALUE_MAX)
    ACT_PAGE_KNOB,          // arg = row 1..4 of the current page: sets that parameter
    ACT_MACRO,              // arg = macro 0..2: sets the parameter the macro drives
    ACT_MACRO_LEARN,        // arg = macro 0..2: the macro takes the parameter under the cursor
    ACT_MASTER_VOLUME,      // master volume
    ACT_COUNT
} action_id_t;

// Arguments of ACT_NAV.
enum { NAV_LEFT, NAV_RIGHT, NAV_UP, NAV_DOWN };

// Which modifier states a binding is active in.
enum { MODS_NONE = 1, MODS_SHIFT = 2, MODS_ANY = MODS_NONE | MODS_SHIFT };

typedef struct {
    control_id_t ctl;       // the control
    input_kind_t on;        // IN_PRESS, IN_DELTA (encoder), IN_VALUE (knob). HOLD actions also see the matching IN_RELEASE.
    uint8_t      mods;      // MODS_*
    action_id_t  act;
    int          arg;
} binding_t;

extern const binding_t bindings[];
extern const int       bindings_count;

// Actions that need to see the release of their control as well (note off, shift off).
int  action_is_hold(action_id_t a);

const char *action_name(action_id_t a);                 // "Row move"
const char *control_name(control_id_t c);               // "ENC A", "KEY 2.1" (a static buffer for keys: use it at once)

// Joystick as four buttons: the HAL reports axes, the application turns deflection into CTL_JOY_LEFT / RIGHT / UP / DOWN presses.
#define JOY_THRESHOLD_LOW    300                        // axis value below / above which the stick counts as pushed
#define JOY_THRESHOLD_HIGH   724
#define JOY_REPEAT_FIRST_MS  400                        // key repeat while held
#define JOY_REPEAT_MS        140

// Keyboard: MIDI note of the first key (bottom-left of the matrix) and the octave range. Note 0..127 are all reachable:
// with the base note 60 and 20 keys, octave -5 covers 0..19 and octave +4 covers 108..127.
#define KEYBOARD_BASE_NOTE   60
#define KEYBOARD_OCTAVE_MIN  (-5)
#define KEYBOARD_OCTAVE_MAX  4

#ifdef __cplusplus
}
#endif
