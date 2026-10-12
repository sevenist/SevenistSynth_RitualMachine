#pragma once
// PRIVATE to the application (the files of src/app): what they share. The public interface is app/app.h.
//
//   app.c           the start (app_init, the boot animation), one frame, the step (app_step: what runs at every pass of the main loop)
//   app_input.c     where an input event goes: the matrix keys, the other controls, the binding table, the joystick (buttons or XY)
//   app_layers.c    the Shift / Mod layers, Learn, the catch of the column knobs
//   app_actions.c   what an action does (app_run_action), UI events, the keyboard's notes, the tempo
//   app_pads.c      the Pads section: the switch Synth <-> Pads, the pad keys, its knobs, its screens' events, the chop to pads
//   app_library.c   the samples in use (the catalog) and the sample browser
//   app_render.c    a pad's sample processed into a new version (the EDIT page)
//   app_capture.c   the Rec key: what plays, recorded onto a pad
//   app_pseq.c      the Pads section's sequencer: its patterns to the engine, their files, step mode, live record
//   app_seq.c       the step sequencer: its patterns and chain handed to the engine, their files, recording from the keys
//   app_card.c      the card: the settings files read and saved, the card's events as popups
//   app_power.c     the battery: a warning when it gets low
//   app_preset.c    the sound's working state (saved by itself, back at power-on) and the presets
//   key_leds.c/.h   the LEDs under the keys
#include "app/app.h"
#include "hal/hal_audio.h"

#define LEARN_INFO_MS 2500              // how long a popup about Learn, the latch or Stop all stays

static inline bool in_pads(const app_t *app) { return app->section == SECTION_PADS; }

/* ---------------- app.c ---------------- */

// One frame: the full-screen notice or the UI, the popup on top, then the buffer goes to the display.
void app_draw_frame(app_t *app);

/* ---------------- app_input.c ---------------- */

void app_input_event(app_t *app, input_event_t e, uint32_t now);    // one input event (not IN_NONE)
void app_input_poll(app_t *app, uint32_t now);                      // every step: the joystick's repeat, the key-layout reset
bool app_joy_bind(app_t *app);                                      // a push on a page row binds its parameter to a joystick axis
void app_joy_mode(app_t *app, bool on);                             // the joystick: XY controller / navigation
void app_joy_xy_off(app_t *app);                                    // leaving the synth: XY mode off

/* ---------------- app_layers.c ---------------- */

extern bool app_catch_stale;                                        // the column knobs are to be checked against what they drive
bool app_layer_event(app_t *app, input_event_t e);                  // Shift / Mod + a control; false: the base handles the event
bool app_mods_tab_select(app_t *app, input_event_t e);              // the MODIFIERS tab: a control selects itself in the tab
bool app_curves_tab_knob(app_t *app, input_event_t e);              // the CURVES tab: the X / Y knobs move the selected point
bool app_learn_push(app_t *app, action_id_t act);                   // while a Learn button waits: the push that assigns, or Back
void app_learn_poll(app_t *app, uint32_t now);                      // every step: a Learn button was pushed
void app_catch_poll(app_t *app);                                    // every step: the knobs' catch, measured again when stale

/* ---------------- app_actions.c ---------------- */

// Runs one binding for one event. `e.kind` is IN_RELEASE only for hold actions.
void app_run_action(app_t *app, const binding_t *b, input_event_t e);
// Gives the UI one event; pushes the sound to the audio side when a value changed.
void app_ui_event(app_t *app, ui_event_t ev);
void app_note_off(app_t *app, control_id_t ctl);                    // the note a control started stops (not a latched one)

/* ---------------- app_pads.c (app_set_section and app_pad_changed: app.h) ---------------- */

void app_pads_event(app_t *app, ui_event_t ev);                     // one UI event in the Pads section
void app_pad_key_down(app_t *app, control_id_t ctl, int k);         // a pad key, k = 0..15 of the bank shown
void app_pad_key_up(app_t *app, control_id_t ctl);
void app_pads_knob(app_t *app, int row, int value);                 // a column knob in the Pads section
void app_pads_poll(app_t *app, uint32_t now);                       // every step in the Pads section

/* ---------------- app_library.c ---------------- */

// What app_sample_use answers instead of a catalog index (the comment at its definition).
enum { LIB_MISSING = -1, LIB_AGAIN = -2, LIB_FULL = -3, LIB_WAIT = -4 };
int  app_lib_index(const char *name);                               // a sample's catalog index, -1: not in the catalog
void app_lib_drop(app_t *app, int index);                           // a catalog entry leaves
int  app_sample_use(app_t *app, const char *name, bool wait);       // a sample by name: its index, read from the card if need be
void app_pads_resolve(app_t *app);                                  // every step: the pads' files not in the catalog yet, one at a time
void app_browse_open(app_t *app, int what);                         // the sample browser, for a pad (BROWSE_PAD) or the SAMPLES tab
bool app_browse_load(app_t *app, bool wait);                        // the highlighted sample into the catalog (and onto its pad)
void app_browse_close(app_t *app, bool chosen);
void app_browse_poll(app_t *app, uint32_t now);                     // every step while the browser is open

/* ---------------- app_render.c ---------------- */

// `op`: an EDIT_* operation and its amount as the page shows it; `chop`: a padding to `amount` frames instead (then the chop follows).
void app_render_start(app_t *app, int pad, int op, int32_t amount, bool chop);
void app_render_poll(app_t *app, uint32_t now);

/* ---------------- app_capture.c ---------------- */

void app_capture_key(app_t *app);                                   // the Rec key
void app_capture_poll(app_t *app, uint32_t now);

/* ---------------- app_seq.c ---------------- */

void app_seq_init(app_t *app);                                      // app_init: pattern 1 = the pattern there is, the others empty
void app_seq_step(app_t *app, bool clock_running);                  // every step: the pattern, what follows it and the clock, to the engine
bool app_seq_load(app_t *app);                                      // seq.cfg and the pattern files into the application
bool app_seq_unsaved(const app_t *app);                             // the patterns differ from what the card holds
bool app_seq_save(app_t *app);                                      // ... all written now
void app_seq_autosave(app_t *app, uint32_t now);                    // every step: a file at a time, a moment after the last change
void app_seq_rec_set(app_t *app, bool on);                          // Rec armed / off (Shift + Play, the key function Rec)
void app_seq_note_down(app_t *app, control_id_t ctl, int note);     // a key started a note / let it go: into the pattern while Rec is armed
void app_seq_note_up(app_t *app, control_id_t ctl);
void app_seq_poll(app_t *app);                                      // every step: the live notes whose step has come are written

/* ---------------- app_pseq.c ---------------- */

void app_pseq_init(app_t *app);                                     // app_init: the patterns' memory, all empty
void app_pseq_step(app_t *app);                                     // every step, BEFORE app_seq_step (which sets the clock): the pattern to the engine
bool app_pseq_load(app_t *app);                                     // padseq.cfg and the pattern files into the application
bool app_pseq_unsaved(const app_t *app);
bool app_pseq_save(app_t *app);
void app_pseq_autosave(app_t *app, uint32_t now);
void app_pseq_steps_set(app_t *app, bool on);                       // step mode (Select + Play): the pad keys are the selected pad's steps
void app_pseq_step_key(app_t *app, int k);                          // ... one of them was pushed
void app_pseq_rec_set(app_t *app, bool on);                         // live record armed / off (Select + Rec)
void app_pseq_pad_down(app_t *app, int pad);                        // a pad was pushed / let go: into the pattern while it records
void app_pseq_pad_up(app_t *app, int pad);
void app_pseq_poll(app_t *app);                                     // every step: the recorded pushes whose step has come are written

/* ---------------- app_card.c ---------------- */

void app_check_sd(app_t *app);                                      // every step: the card changed, its settings files follow
void app_card_events(app_t *app);                                   // every step: what the platform found on the card, as popups
void app_autosave(app_t *app, uint32_t now);                        // every step: the settings files that changed are written

/* ---------------- app_preset.c ---------------- */

void app_state_init(app_t *app);                                    // app_init: no preset, what plays is the reference
bool app_state_load(app_t *app);                                    // synth.cfg into the application (the caller builds the audio)
bool app_state_save(app_t *app);
bool app_state_unsaved(const app_t *app);                           // the sound differs from what synth.cfg holds
void app_preset_poll(app_t *app, uint32_t now);                     // every step: the state's autosave, the PRESETS tab's requests
bool app_preset_browse_begin(app_t *app);                           // the preset browser opens: the state goes to the card first
bool app_preset_try(app_t *app, const char *name);                  // the highlighted preset in place of the sound
void app_preset_browse_end(app_t *app, const char *chosen);         // it closes: `chosen` stays, NULL = the sound as it was

/* ---------------- app_power.c ---------------- */

void app_power_poll(app_t *app, uint32_t now);                      // every step: the battery, looked at now and then
