#pragma once
// Board wiring. PLACEHOLDERS: set these to the GPIOs you actually wired.
// Defaults match an I2S DAC (e.g. PCM5102 / UDA1334) on free ESP32-S3 pins.

// I2S audio out
#define PIN_I2S_BCLK  8
#define PIN_I2S_LRC   9    // word select / LRCK
#define PIN_I2S_DOUT  10
#define PIN_I2S_MCLK  7    // only needed by DACs that want a master clock; -1 if unused

// Controls of the prototype (see hal/hal_input.h for the layout). PLACEHOLDERS, -1 = not wired yet.
// Encoders: two quadrature pins + the switch. Knobs / joystick axes: ADC1 channels (ADC2 is unusable while Wi-Fi runs).
#define PIN_ENC_A_1     -1
#define PIN_ENC_A_2     -1
#define PIN_ENC_A_SW    -1
#define PIN_ENC_B_1     -1
#define PIN_ENC_B_2     -1
#define PIN_ENC_B_SW    -1
#define PIN_PLAY        -1
#define PIN_BTN_1       -1
#define PIN_BTN_2       -1
#define PIN_BTN_3       -1
#define PIN_JOY_SW      -1
#define PIN_JOY_X       -1      // ADC
#define PIN_JOY_Y       -1      // ADC
#define PIN_VOLUME      -1      // ADC (or an analog mux channel)
// 20 keys as a 4 x 5 matrix: 4 column lines, 5 row lines (row 0 = the extra row). 8 column + 3 right knobs behind an analog mux:
#define PIN_KEY_COL_0   -1
#define PIN_KEY_COL_1   -1
#define PIN_KEY_COL_2   -1
#define PIN_KEY_COL_3   -1
#define PIN_KEY_ROW_0   -1
#define PIN_KEY_ROW_1   -1
#define PIN_KEY_ROW_2   -1
#define PIN_KEY_ROW_3   -1
#define PIN_KEY_ROW_4   -1
#define PIN_MUX_S0      -1      // 7 knobs (4 column + 3 right) on a 8-channel analog mux: select lines and the shared ADC input
#define PIN_MUX_S1      -1
#define PIN_MUX_S2      -1
#define PIN_MUX_ADC     -1
