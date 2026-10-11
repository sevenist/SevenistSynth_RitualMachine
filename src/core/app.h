#pragma once
// Platform-independent application logic. Knows nothing about SDL, Windows,
// Arduino or the ESP32: everything platform-specific comes through hal/*.h.
#include <stdbool.h>
#include "u8g2.h"
#include "core/synth_params.h"
#include "core/history.h"
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

// A processing of a pad's sample in progress (the EDIT page, a Grid chop that pads): rendered by the card thread, then found in the
// library, then the pad takes it and the history records it.
typedef struct {
    uint8_t  stage;                     // 0 none, 1 rendering, 2 waiting for the library to list the new file
    int      pad, op;                   // op: engine/offline/process.h ProcOp (pads.h PROC_*)
    uint32_t frames_in;                 // the source's length (Stretch / Resample: the pad's trim points scale with the result's)
    char     name[PAD_NAME_MAX], parent[PAD_NAME_MAX], what[16];
    uint32_t since;
    bool     chop;                      // then chop the new version (CHOP: a Grid chop that needed padding)
} pad_render_t;

// A recording of what plays (capture / resampling, ADR-041): armed and recorded by the engine onto the card, then found in the
// library and put on a pad.
typedef struct {
    uint8_t  stage;                     // 0 none, 1 armed / recording / being finished, 2 waiting for the library to list the file
    uint8_t  bars;                      // the length setting: 0 = until Rec is pushed again, else that many bars at the tempo
    bool     loop;                      // this one has a length in bars: the pad loops it, on the bar
    int      pad;                       // where it goes
    char     name[PAD_NAME_MAX];
    uint32_t frames, dropped, since;
} pad_capture_t;

// The two workflows (ADR-041 decision 5): the synth, or the Pads section (an SP-404-style sampler). Shift + Menu switches.
typedef enum { SECTION_SYNTH, SECTION_PADS } section_t;

typedef struct {
    u8g2_t        *display;   // injected by the platform layer
    synth_params_t params;
    seq_t          seq;
    rack_t         rack;
    pads_t         pads;        // the Pads section's 64 pads (pads.cfg)
    history_t      history;     // the versions of the library's samples (history.cfg next to them)
    pad_render_t   render;
    pad_capture_t  capture;
    uint8_t        section;     // section_t
    // The pads' files and the catalog (hal_audio.h: it holds the files in use, each read when asked for): lib_dirty = a pad's file may
    // not be in it yet (pads_resolve takes one per step), lib_missing = bit k: pad k's file is not on the card (not asked again),
    // lib_tries = steps spent waiting for an unused sample to be unloaded.
    bool           lib_dirty;
    uint16_t       rec_next;         // the number of the next recording's name (recN); 0 = not known yet: the library's root is listed once
    uint64_t       lib_missing;
    uint8_t        lib_tries;
    uint32_t       lib_clock;        // counts the samples asked for; lib_stamp[i] = its value when catalog entry i was last asked
    uint32_t       lib_stamp[AUDIO_SAMPLES_MAX];   // for (0 = not by this application): the unused entry with the oldest stamp leaves first
    bool           pads_pending;     // the pads changed and wait to be saved (pads.cfg) ...
    uint32_t       pads_ms;          // ... since then
    bool           fx_pending;       // the pads' FX chains changed and wait to be saved (padsfx.cfg) ...
    uint32_t       fx_ms;            // ... since then
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

// For the dev serial commands (the board's tests): the section (Synth / Pads) as Shift + Menu switches it, and a pad's settings changed
// from outside the UI (sent to the engine, saved later).
void app_set_section(app_t *app, bool pads);
void app_pad_changed(app_t *app, int pad);

#ifdef __cplusplus
}
#endif
