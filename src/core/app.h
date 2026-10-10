#pragma once
// Platform-independent application logic. Knows nothing about SDL, Windows,
// Arduino or the ESP32: everything platform-specific comes through hal/*.h.
#include <stdbool.h>
#include "u8g2.h"
#include "core/synth_params.h"
#include "core/pads.h"
#include "core/rack.h"
#include "core/seq.h"
#include "core/synth_ui.h"
#include "hal/hal_input.h"
#include "core/bindings.h"
#include "core/popup.h"

#ifdef __cplusplus
extern "C" {
#endif

// State of the input layer (see core/bindings.h): modifier, keyboard octave, sounding notes, joystick as buttons.
typedef struct {
    bool         shift;                 // the Shift hold-action is active
    control_id_t shift_key;             // the matrix key that holds Shift (CTL_NONE: none; the Shift button of the binding table is separate)
    bool         mod;                   // the Mod hold-action is active (a key function only: core/keymap.h)
    control_id_t mod_key;               // the matrix key that holds Mod
    int16_t      knob_last[CTL_COUNT];  // last position + 1 of an absolute knob whose Shift / Mod entry is a step action (0 = not seen yet)
    int          octave;                // keyboard octave offset, KEYBOARD_OCTAVE_MIN..MAX
    uint8_t      held[CTL_COUNT];       // note + 1 started by a key control (0 = none), so its release stops the right note
    bool         latched[CTL_COUNT];    // the note of this key was started with Shift: its release does not stop it, the next press does
    int          axis_x, axis_y;        // joystick axes 0..INPUT_VALUE_MAX
    control_id_t joy_dir;               // virtual direction control currently held (CTL_NONE = centred)
    uint32_t     joy_next_ms;           // when the held direction repeats
    uint8_t      pad_held[CTL_COUNT];   // pad + 1 a key pushed (0 = none), so its release reaches the same pad after a bank change
    bool         pad_select, pad_mute;  // the Pads section's Select / Mute keys are held
} input_state_t;

// The two workflows (ADR-041 decision 5): the synth, or the Pads section (an SP-404-style sampler). Shift + Menu switches.
typedef enum { SECTION_SYNTH, SECTION_PADS } section_t;

typedef struct {
    u8g2_t        *display;   // injected by the platform layer
    synth_params_t params;
    seq_t          seq;
    rack_t         rack;
    pads_t         pads;        // the Pads section's 64 pads (pads.cfg)
    uint8_t        section;     // section_t
    bool           pads_pending;     // the pads changed and wait to be saved (pads.cfg) ...
    uint32_t       pads_ms;          // ... since then
    uint64_t       pads_shown;  // the pads that sounded at the last draw (a change redraws the grid)
    bool           pads_run;    // the machine clock runs in the Pads section (Play; in the synth it runs with the sequencer)
    bool           metronome;   // the click on the beat (key function Click)
    seq_tap_t      tap;         // tap tempo
    uint32_t       clock_shown; // running / bar / beat / blink at the last draw (a change redraws the header)
    synth_ui_t     ui;
    input_state_t  in;
    bool           dirty;       // the screen needs a redraw after this step
    bool           redraw_owed; // a redraw was put off because more input was queued (app_step draws it when the queue is empty)
    uint32_t       sd_gen;      // the TF card generation the app has seen (hal_audio.h)
    bool           keys_notice; // the "keys reset" screen is up until a key is pressed or released, or a few seconds passed
    uint32_t       keys_notice_ms;
    bool           menu_open;   // the menu was open at the previous step (closing it saves the key layout)
    uint32_t       boot_ms;     // when app_init ran: the key-layout reset only works in the first seconds
    bool           reset_held;  // the reset key went down in the first seconds and is still held
    uint32_t       reset_since; // ... since then
    bool           settings_pending; // the UI settings (ui.cfg) changed and wait to be saved ...
    uint32_t       settings_ms;      // ... since then
    popup_t        popup;       // popup messages over the screen (core/popup.h); a modal one takes the input
    char           status[48];  // last control -> action, for the simulator panel / debugging ("ENC A > Row +1")
} app_t;

// Call after audio_init(): pushes the default sound to the synth and draws the first frame.
void app_init(app_t *app, u8g2_t *display);

// Handles one hardware input event (kind == IN_NONE: none) and redraws. Returns false when the app should exit.
bool app_step(app_t *app, input_event_t event);

#ifdef __cplusplus
}
#endif
