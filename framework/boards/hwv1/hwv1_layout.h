#pragma once
// HWV1 (the old prototype) wiring: how its physical parts become the controls of the board's table (hwv1_controls.h). What a
// control does is not here: an app decides, the Ritual Machine in its map of this board (its core/boards/map_hwv1.h).
//
// Orientation (measured on the board with the key log): TCA row 4 is the function-key row at the top, TCA row 0 the bottom note row,
// and the columns run right to left: the leftmost key of every row is TCA column 7. HWV1_FLIP_COLS therefore defaults to 1; the input
// scan applies it; leds_esp32.cpp works in physical columns already and does not.
#if defined(HWV1)
#include "boards/hwv1/hwv1_controls.h"

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
// The level the joystick's switch pin reads while the stick is pushed. On this board the pin rests LOW (measured 2026-10-11: 0 at
// every sample with the stick untouched, the internal pull-up on), so a push is HIGH. Read as active low, the driver took the rest
// for a held push: a press event at every start, and each real push acted when it was let go.
#ifndef HWV1_JOY_SW_PUSHED
#define HWV1_JOY_SW_PUSHED 1
#endif

// ---- keys -------------------------------------------------------------------------------------------------------------------
// The key at physical place (row from the top, column from the left) is the control HWV1_KEY(row, col) of the table: physical row 0 =
// TCA row 4 (the 4 function keys, on the 4 leftmost places), physical row 4 = TCA row 0.

// ---- relative knobs ---------------------------------------------------------------------------------------------------------
// 7 potentiometers without end stops. Each one is read as a sin / cos pair on two mux channels (pot n -> channels 2+2n and 3+2n) and
// tracked by angle. SynthBox numbering: 0 volume, 1 cutoff, 2 resonance (top row), 3..6 around the joystick; physical position of
// the four: TL = pot 6, TR = pot 3, BL = pot 5, BR = pot 4. Pot n is the control HWV1_POT_0 + n.
//
// A pot reports steps (IN_DELTA, detents) or a position (IN_VALUE 0..INPUT_VALUE_MAX, a full turn sweeps the whole range): the app
// chooses per pot (input_control_mode, hal_input.h). A position starts where the app says and does not know where the parameter is,
// so the value jumps to the pot's counter on the first move (no pick-up).
#define HWV1_KNOB_COUNT 7
// +1 / -1 per pot: turning clockwise must increase the value (SynthBox flips pots 2..6)
static const int8_t kHwv1KnobSign[HWV1_KNOB_COUNT] = {+1, +1, -1, -1, -1, -1, -1};

#define HWV1_COUNTS_PER_REV    100      // a full turn = 100 counts (SynthBox: 50 per half turn)
#define HWV1_COUNTS_PER_DETENT 4        // encoder mode: counts for one IN_DELTA step (25 detents per turn = 0.25 rad per step and per hysteresis)
// Position reading, continuous knobs (user 2026-10-07) with a hysteresis against the prototype's ADC noise: a knob at rest must turn HWV1_ABS_START
// units (or reverse) before it reports; while it keeps turning the same way (a report less than HWV1_ABS_IDLE_MS ago) it reports every
// HWV1_ABS_STEP units. Tune START above the noise seen at rest, STEP for the resolution (1024 units = one turn).
#define HWV1_ABS_STEP          2        // units per report while moving (512 per turn, 0.012 rad)
#define HWV1_ABS_START         8        // units to start / reverse (0.049 rad: the old fixed step, which hid the noise)
#define HWV1_ABS_IDLE_MS       150      // after this long without a report the knob is at rest again

// Scan periods. The key controller debounces in hardware and the analog parts do not need to be faster than the screen can show.
#define HWV1_KEY_PERIOD_MS     5
#define HWV1_ANALOG_PERIOD_MS  10       // multiple of HWV1_KEY_PERIOD_MS
#define HWV1_MUX_AUX           0        // mux channel 0: a spare input (a button, as the user remembers it; not checked on the board), read, not used yet
#define HWV1_MUX_BATTERY       1        // mux channel 1 is the battery divider (its scale: board_pins.h)
#define HWV1_SENSOR_PERIOD_MS  100      // those two are read this often (multiple of HWV1_KEY_PERIOD_MS)
#define HWV1_AUX_PUSHED        0        // the level the spare input reads while pushed (a guess: a switch to ground; `power` prints the raw count)
#define HWV1_MUX_KNOB_BASE     2        // first channel of the sin / cos pairs
#endif // HWV1
