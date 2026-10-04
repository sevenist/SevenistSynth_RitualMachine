// SKELETON: the ESP32 input driver. It must only report PHYSICAL controls (see hal/hal_input.h); what they do is decided in
// core/bindings.c, so nothing here changes when a binding changes.
//
// To do (pins in board_pins.h): scan the key matrix (20 keys, debounce ~5 ms) -> CTL_KEY(row, col) IN_PRESS / IN_RELEASE;
// quadrature decode the two encoders (interrupt or PCNT) -> CTL_ENC_A / CTL_ENC_B IN_DELTA, switches -> CTL_ENC_*_SW;
// read the ADC knobs (oversample, scale 12 bit -> 0..INPUT_VALUE_MAX, report only when the value moved by 2+ counts) ->
// CTL_VOLUME, CTL_COL_KNOB_0..3, CTL_KNOB_R1..3 IN_VALUE; joystick axes -> CTL_JOY_X / CTL_JOY_Y IN_VALUE (the core makes
// the four direction buttons and the key repeat), CTL_JOY_SW and the buttons -> IN_PRESS / IN_RELEASE.
// input_poll() returns one event per call, so keep a small ring buffer filled by the scan code (as input_sim.c does).
#if defined(ARDUINO_ARCH_ESP32)
#include "hal/hal_input.h"
#include "platform/esp32/board_pins.h"

extern "C" input_event_t input_poll(void) { return input_event_t{CTL_NONE, IN_NONE, 0, false}; }
#endif // ARDUINO_ARCH_ESP32
