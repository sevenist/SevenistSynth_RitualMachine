#pragma once
// Platform-independent application logic. Knows nothing about SDL, Windows,
// Arduino or the ESP32: everything platform-specific comes through hal/*.h. Its files: app/app_internal.h.
#include <stdbool.h>
#include "u8g2.h"
#include "core/synth_params.h"
#include "core/history.h"
#include "core/pad_seq.h"
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

// Recording into the step sequencer from the keys (app_seq.c): armed, the chord being entered step by step, and the notes of a live
// recording, each from its key down to the moment its step is written.
#define SEQ_REC_NOTES 8
enum { SEQ_REC_FREE, SEQ_REC_ENTRY, SEQ_REC_DOWN, SEQ_REC_WAIT };
typedef struct {
    uint8_t  state;                     // SEQ_REC_*: free, a held key of the step entry, a live note held, a live note waiting for its step
    uint8_t  ctl, note, step, len;      // the key, the note (without the transpose), the step it goes to, its length in steps
    uint32_t count;                     // the sequencer's step count when that step plays
} seq_rec_note_t;
typedef struct {
    bool     armed;
    uint8_t  entry_held, entry_n;       // step entry: keys down, notes of the chord so far
    seq_rec_note_t note[SEQ_REC_NOTES];
} seq_rec_t;

// Live recording into the Pads section's sequencer (app_pseq.c): armed (Select + Rec), and the pushes on their way to a step.
#define PSEQ_REC_NOTES 8
enum { PSEQ_REC_FREE, PSEQ_REC_DOWN, PSEQ_REC_WAIT };
typedef struct { uint8_t state, pad, step, len; uint32_t count; } pseq_rec_note_t;
typedef struct { bool armed; pseq_rec_note_t note[PSEQ_REC_NOTES]; } pseq_rec_t;

// The two workflows (ADR-041 decision 5): the synth, or the Pads section (an SP-404-style sampler). Shift + Menu switches.
typedef enum { SECTION_SYNTH, SECTION_PADS } section_t;

typedef struct {
    u8g2_t        *display;   // injected by the platform layer
    synth_params_t params;
    seq_t          seq;
    seq_rec_t      rec;         // recording into it from the keys
    // Its patterns and their chain (app_seq.c; seq.cfg and pattern1.cfg .. on the card): the pattern handed to the engine as the
    // next one (-1: none), whether it ran at the last step, the hashes of what the card holds, and the autosave's state
    seq_bank_t     bank;
    int8_t         seq_queued;
    bool           seq_was_running;
    uint32_t       pat_saved[SEQ_PATTERNS], seq_cfg_saved;
    uint32_t       seq_seen, seq_ms, seq_look_ms;
    bool           seq_pending;
    // The Pads section's sequencer (app_pseq.c; padseq.cfg and padpat1.cfg .. on the card): its patterns (bulk memory) and chain, the
    // same hand-over and autosave state as the synth sequencer's, and its live recording
    pseq_bank_t    pbank;
    pseq_rec_t     prec;
    int8_t         pseq_queued;
    bool           pseq_was_running;
    uint32_t       ppat_saved[PSEQ_PATTERNS], pseq_cfg_saved;
    uint32_t       pseq_seen, pseq_ms, pseq_look_ms;
    bool           pseq_pending;
    rack_t         rack;
    pads_t         pads;        // the Pads section's 64 pads (pads.cfg)
    history_t      history;     // the versions of the library's samples (history.cfg next to them)
    pad_render_t   render;
    pad_capture_t  capture;
    uint8_t        section;     // section_t
    // The pads' files and the catalog (hal_audio.h: it holds the files in use, each read when asked for): lib_dirty = a pad's file may
    // not be in it yet (app_pads_resolve takes one per step), lib_missing = bit k: pad k's file is not on the card (not asked again),
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
    int            boot_anim;   // the boot animation while it plays over the UI (app_boot_animation), else GUI_ANIM_INVALID
    bool           started;     // the start is over (the card read, the pads' samples in): the platform was told (display_boot_end)
    uint8_t        start_pct;   // ... and how far it is until then, as last reported (display_boot_progress)
    bool           reset_held;  // the reset key went down in the first seconds and is still held
    uint32_t       reset_since; // ... since then
    bool           settings_pending; // the UI settings (ui.cfg) changed and wait to be saved ...
    uint32_t       settings_ms;      // ... since then
    // The sound's working state and the presets (app_preset.c, core/patch.h)
    char           preset[PAD_NAME_MAX];   // the preset the sound was loaded from or saved to last (its path below presets/), "" = none
    uint32_t       preset_hash;  // the sound's hash then: another one now = edited since
    bool           preset_tried; // the preset browser has put a preset in place of the working state (Back reads the state again)
    uint32_t       state_hash;   // the hash of what synth.cfg holds (the sound and the sequencer)
    uint32_t       state_seen;   // ... of what was there at the last look ...
    bool           state_pending;    // ... which differs and waits to be saved ...
    uint32_t       state_ms, state_look_ms;   // ... since then; the last look
    uint8_t        batt_warned; // the battery warning given so far: 0 none, 1 low, 2 almost empty (hal_power.h)
    uint32_t       batt_ms;     // when the battery was last looked at
    popup_t        popup;       // popup messages over the screen (core/popup.h); a modal one takes the input
    char           status[48];  // last control -> action, for the simulator panel / debugging ("ENC A > Row +1")
} app_t;

// Call after audio_init(): pushes the default sound to the synth and draws the first frame.
void app_init(app_t *app, u8g2_t *display);

// Plays the boot animation (the image 64/boot of assets/UI_Sprites, its frames once, BOOT_FRAME_MS each) over the UI, which runs
// under it: the synth plays from the first step. It ends by itself, or at the first key, push or encoder step, which acts as usual.
// Nothing happens without the image, or while the "keys reset" notice is up. The simulator calls it right after app_init. The board
// does not: there the display driver plays the same frames from its own task, started before the application exists (user,
// 2026-10-11: a high-priority task, deleted at its end; platform/esp32/display_esp32.cpp), and BOOT_FRAME_MS is its rhythm too
// (50 ms = 20 frames a second: the user's choice). That task loops back and forth until the application's start is over, which
// app_step reports once (display_boot_end): the card was read and the pads' files are in the catalog, or no card showed up within
// START_CARD_WAIT_MS of app_init; after START_MAX_MS whatever is still going on.
#define BOOT_FRAME_MS 50
#define START_CARD_WAIT_MS 1500
#define START_MAX_MS 8000
void app_boot_animation(app_t *app);

// The words of the boot screen: the machine's name above the animation, the version at the bottom. Drawn into the buffer as it is
// (no clear, no send); nothing on a screen too short to hold them beside the image (64 pixels). The board's own boot animation
// shows the same words (display_esp32.cpp).
#define BOOT_TITLE   "RITUAL MACHINE"
#define BOOT_VERSION "beta 1.0"
void app_boot_draw_text(u8g2_t *g);

// Handles one hardware input event (kind == IN_NONE: none) and redraws. Returns false when the app should exit.
bool app_step(app_t *app, input_event_t event);

// Also for the dev serial commands (the board's tests): the section (Synth / Pads) as Shift + Menu switches it, and a pad's settings
// changed, from the UI or from outside it (sent to the engine, pads.cfg saved a moment later).
void app_set_section(app_t *app, bool pads);
void app_pad_changed(app_t *app, int pad);

// The machine is about to be switched off (or restarted): the settings files that wait for their autosave are written now.
void app_save_now(app_t *app);

#ifdef __cplusplus
}
#endif
