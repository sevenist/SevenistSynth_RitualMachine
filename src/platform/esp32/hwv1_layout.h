#pragma once
// HWV1 (the old prototype) layout: how its physical parts are presented to the application as the HAL's controls (hal/hal_input.h).
// The HAL describes the NEW panel (encoders, 3 buttons, play, 4 x 5 keys, 3 right knobs); the old board has a 4 x 8 + 4 key
// keyboard, 7 relative knobs and a joystick, so this file is the one place that decides the correspondence. Edit the tables to change it.
//
// Orientation (measured on the board with the key log): TCA row 4 is the function-key row at the top, TCA row 0 the bottom note row,
// and the columns run right to left: the leftmost key of every row is TCA column 7. HWV1_FLIP_COLS therefore defaults to 1; it applies
// to every table here and in leds_esp32.cpp (the LED chain order is NOT verified against the board).
#if defined(HWV1)
#include "hal/hal_input.h"

#ifndef HWV1_FLIP_ROWS
#define HWV1_FLIP_ROWS 0
#endif
#ifndef HWV1_FLIP_COLS
#define HWV1_FLIP_COLS 1
#endif

// Joystick: invert an axis if up / left report the wrong way. Raw ADC range 0..4095, centre taken at boot.
#ifndef HWV1_JOY_INVERT_X
#define HWV1_JOY_INVERT_X 0
#endif
#ifndef HWV1_JOY_INVERT_Y
#define HWV1_JOY_INVERT_Y 0
#endif

// ---- keys -------------------------------------------------------------------------------------------------------------------
// Indexed [physical row from the top][column from the left]: physical row 0 = TCA row 4 (function keys), physical row 4 = TCA row 0.
// The left 4 x 4 block of the notes is the HAL's rows 1..4; the HAL's extra row 0 and the right half of the note keys are unmapped
// (CTL_NONE) for now. Function keys (the 4 leftmost keys of the top row): Shift (hold), Menu, Back (delete module), Play.
#define K(r, c) ((control_id_t)CTL_KEY(r, c))
static const control_id_t kHwv1KeyMap[HW_KBD_ROWS][HW_KBD_COLS] = {
    {CTL_BTN_3, CTL_BTN_1, CTL_BTN_2, CTL_PLAY, CTL_NONE, CTL_NONE, CTL_NONE, CTL_NONE},
    {K(1, 0),  K(1, 1),  K(1, 2),  K(1, 3),  CTL_NONE,  CTL_NONE,  CTL_NONE,  CTL_NONE},
    {K(2, 0),  K(2, 1),  K(2, 2),  K(2, 3),  CTL_NONE,  CTL_NONE,  CTL_NONE,  CTL_NONE},
    {K(3, 0),  K(3, 1),  K(3, 2),  K(3, 3),  CTL_NONE,  CTL_NONE,  CTL_NONE,  CTL_NONE},
    {K(4, 0),  K(4, 1),  K(4, 2),  K(4, 3),  CTL_NONE,  CTL_NONE,  CTL_NONE,  CTL_NONE},
};
#undef K

// ---- relative knobs ---------------------------------------------------------------------------------------------------------
// 7 potentiometers without end stops. Each one is read as a sin / cos pair on two mux channels (pot n -> channels 2+2n and 3+2n) and
// tracked by angle. SynthBox numbering: 0 volume, 1 cutoff, 2 resonance (top row), 3..6 around the joystick; physical position of
// the four: TL = pot 6, TR = pot 3, BL = pot 5, BR = pot 4.
//
// A knob is presented either as an encoder (HW_KNOB_ENCODER: IN_DELTA, detents) or as an absolute knob (HW_KNOB_ABSOLUTE: IN_VALUE
// 0..INPUT_VALUE_MAX, a full turn sweeps the whole range). Absolute knobs start at `start` and do not know where the parameter is, so the
// value jumps to the knob's counter on the first move (no pick-up).
enum { HW_KNOB_ENCODER, HW_KNOB_ABSOLUTE };
typedef struct {
    control_id_t ctl;
    int          mode;
    int          sign;      // +1 / -1: turning clockwise must increase the value (SynthBox flips pots 2..6)
    int          start;     // initial value of an absolute knob
} hwv1_knob_t;

#define HWV1_KNOB_COUNT 7
static const hwv1_knob_t kHwv1Knobs[HWV1_KNOB_COUNT] = {
    /* pot 0, top left   */ {CTL_VOLUME,     HW_KNOB_ABSOLUTE, +1, 768},
    /* pot 1, top centre */ {CTL_ENC_A,      HW_KNOB_ENCODER,  +1, 0},      // rows
    /* pot 2, top right  */ {CTL_ENC_B,      HW_KNOB_ENCODER,  -1, 0},      // value
    /* pot 3, around joystick, TR */ {CTL_COL_KNOB_1, HW_KNOB_ABSOLUTE, -1, 512},
    /* pot 4, BR */                  {CTL_COL_KNOB_3, HW_KNOB_ABSOLUTE, -1, 512},
    /* pot 5, BL */                  {CTL_COL_KNOB_2, HW_KNOB_ABSOLUTE, -1, 512},
    /* pot 6, TL */                  {CTL_COL_KNOB_0, HW_KNOB_ABSOLUTE, -1, 512},
};

#define HWV1_COUNTS_PER_REV    100      // a full turn = 100 counts (SynthBox: 50 per half turn)
#define HWV1_COUNTS_PER_DETENT 4        // encoder mode: counts for one IN_DELTA step (25 detents per turn = 0.25 rad per step and per hysteresis)
#define HWV1_ABS_STEP          8        // absolute mode: the value moves in steps of this many units (128 steps over a full turn, 0.049 rad each)

// Scan periods. The key controller debounces in hardware and the analog parts do not need to be faster than the screen can show.
#define HWV1_KEY_PERIOD_MS     5
#define HWV1_ANALOG_PERIOD_MS  10       // multiple of HWV1_KEY_PERIOD_MS
#define HWV1_MUX_BATTERY       1        // mux channel 1 is the battery divider (SynthBox: raw / 4095 * 10.4 V)
#define HWV1_MUX_KNOB_BASE     2        // first channel of the sin / cos pairs
#endif // HWV1
