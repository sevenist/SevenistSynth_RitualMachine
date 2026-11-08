#pragma once
// HWV2 (the second prototype) wiring: how its physical parts become the controls of the board's table (hwv2_controls.h), from the PCB file. What a
// control does is not here: an app decides. Plain data and plain functions: the host tests read them too.
#include "boards/hwv2/hwv2_controls.h"

// ---- the scanner (U14, apps/HWV2_scanner/src/main.c describes its frame) ------------------------------------------------------
#define HWV2_SCANNER_FRAME 8
#define HWV2_SCANNER_INPUTS 22          // 20 keys, then the 2 encoders' pushes
#define HWV2_ENC_COUNTS_PER_DETENT 4    // quarter steps a click (seen on the board)
// +1 / -1 per encoder: turning clockwise must report a positive step.
#ifndef HWV2_ENC_A_SIGN
#define HWV2_ENC_A_SIGN 1
#endif
#ifndef HWV2_ENC_B_SIGN
#define HWV2_ENC_B_SIGN 1
#endif

// A bit of the scanner's frame as a control. The scanner counts its rows by the schematic: r1..r4 (bits' rows 0..3) are the 4 x 4 from the top, and r5
// (row 4) the top row, which is the table's function row 0. Its columns run left to right, as the table's.
static inline int hwv2_scanner_control(int bit) {
    if (bit == 20) return HWV2_ENC_A_SW;
    if (bit == 21) return HWV2_ENC_B_SW;
    if (bit < 0 || bit > 21) return INPUT_NO_CONTROL;
    const int row = bit / HWV2_KEY_COLS, col = bit % HWV2_KEY_COLS;
    return HWV2_KEY(row == 4 ? 0 : row + 1, col);
}

// ---- the mux's channels, by U2's pins (the net names i0..i15 are NOT the channel numbers) ------------------------------------
#define HWV2_MUX_R1      0
#define HWV2_MUX_SW23    1              // PLAY
#define HWV2_MUX_SW22    2              // B1
#define HWV2_MUX_SW19    3              // B2
#define HWV2_MUX_SW21    4              // B3
#define HWV2_MUX_JOY_SW  5
#define HWV2_MUX_JOY_X   6
#define HWV2_MUX_PWR     7              // the power switch and the battery
#define HWV2_MUX_R2      8
#define HWV2_MUX_VOLUME  9
#define HWV2_MUX_K4      10
#define HWV2_MUX_K3      11
#define HWV2_MUX_K2      12
#define HWV2_MUX_K1      13
#define HWV2_MUX_R3      14
#define HWV2_MUX_JOY_Y   15

// ---- the pots ------------------------------------------------------------------------------------------------------------------
// Rev A's pots read reversed: the wiper is high at the knob's low end (REV-B-NOTES.md). The ends of the travel are left a margin, so a knob reaches 0 and
// INPUT_VALUE_MAX whatever the ADC's own ends do. A board with the pots' pins swapped (rev B) sets this to 0.
#ifndef HWV2_KNOBS_REVERSED
#define HWV2_KNOBS_REVERSED 1
#endif
#define HWV2_KNOB_MARGIN 40             // raw counts at each end

static inline int hwv2_knob_value(int raw) {
    if (HWV2_KNOBS_REVERSED) raw = 4095 - raw;
    const int span = 4095 - 2 * HWV2_KNOB_MARGIN;
    const int v = (raw - HWV2_KNOB_MARGIN) * INPUT_VALUE_MAX / span;
    return v < 0 ? 0 : v > INPUT_VALUE_MAX ? INPUT_VALUE_MAX : v;
}

// A knob at rest must move HWV2_KNOB_START units before it reports; while it keeps turning the same way (a report less than HWV2_KNOB_IDLE_MS ago) it
// reports every HWV2_KNOB_STEP units. Tune START above the noise seen at rest, STEP for the resolution.
#define HWV2_KNOB_START   6
#define HWV2_KNOB_STEP    2
#define HWV2_KNOB_IDLE_MS 150

// ---- the joystick -------------------------------------------------------------------------------------------------------------
// 1 = the axis reads reversed. Both do on rev A (seen on the board, 2026-11-04). A board mounted or wired the other way: set them to 0 here, or with
// -DHWV2_JOY_INVERT_X=0 -DHWV2_JOY_INVERT_Y=0 in platformio.ini's [env:hwv2].
#ifndef HWV2_JOY_INVERT_X
#define HWV2_JOY_INVERT_X 1
#endif
#ifndef HWV2_JOY_INVERT_Y
#define HWV2_JOY_INVERT_Y 1
#endif

// ---- the VU meter: its LEDs from the top of the chain's run: 5 (red) to 10 -------------------------------------------------------
// A placeholder: the level the engine renders, peak with a fall, so it plays without the codec. Which levels light what can change once it can be heard.
#define HWV2_VU_FIRST 5
#define HWV2_VU_COUNT 6
#define HWV2_VU_FRAME_MS 20
#define HWV2_VU_FALL 0.93f              // per frame: about 30 dB a second

// How many LEDs a peak lights, from the bottom: -36, -24, -15, -9, -4 and -1 dB of full scale.
static inline int hwv2_vu_lit(int peak) {
    static const int kThresholds[HWV2_VU_COUNT] = {519, 2063, 5827, 11627, 20674, 29204};
    int lit = 0;
    while (lit < HWV2_VU_COUNT && peak >= kThresholds[lit]) lit++;
    return lit;
}

// ---- scan periods ---------------------------------------------------------------------------------------------------------------
#define HWV2_KEY_PERIOD_MS    5         // the scanner's frame (it debounces by itself)
#define HWV2_ANALOG_PERIOD_MS 10        // the mux (a multiple of HWV2_KEY_PERIOD_MS)
#define HWV2_SENSOR_PERIOD_MS 100       // the power switch and the battery, on mux channel 7 (a multiple of HWV2_KEY_PERIOD_MS)
#define HWV2_SCANNER_LOST     20        // failed reads in a row before the scanner counts as gone (100 ms)
