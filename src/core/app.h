#pragma once
// Platform-independent application logic. Knows nothing about SDL, Windows,
// Arduino or the ESP32: everything platform-specific comes through hal/*.h.
#include <stdbool.h>
#include "u8g2.h"
#include "core/synth_params.h"
#include "core/rack.h"
#include "core/seq.h"
#include "core/synth_ui.h"
#include "hal/hal_input.h"
#include "core/bindings.h"

#ifdef __cplusplus
extern "C" {
#endif

// State of the input layer (see core/bindings.h): modifier, keyboard octave, sounding notes, joystick as buttons.
typedef struct {
    bool         shift;                 // the Shift hold-action is active
    int          octave;                // keyboard octave offset, KEYBOARD_OCTAVE_MIN..MAX
    uint8_t      held[CTL_COUNT];       // note + 1 started by a key control (0 = none), so its release stops the right note
    int          axis_x, axis_y;        // joystick axes 0..INPUT_VALUE_MAX
    control_id_t joy_dir;               // virtual direction control currently held (CTL_NONE = centred)
    uint32_t     joy_next_ms;           // when the held direction repeats
} input_state_t;

typedef struct {
    u8g2_t        *display;   // injected by the platform layer
    synth_params_t params;
    seq_t          seq;
    rack_t         rack;
    synth_ui_t     ui;
    input_state_t  in;
    bool           dirty;       // the screen needs a redraw after this step
    char           status[48];  // last control -> action, for the simulator panel / debugging ("ENC A > Row +1")
} app_t;

// Call after audio_init(): pushes the default sound to the synth and draws the first frame.
void app_init(app_t *app, u8g2_t *display);

// Handles one hardware input event (kind == IN_NONE: none) and redraws. Returns false when the app should exit.
bool app_step(app_t *app, input_event_t event);

#ifdef __cplusplus
}
#endif
