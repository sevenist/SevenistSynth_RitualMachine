#pragma once
// The wiring this project uses on the first prototype (HWV1: ESP32-S3, PCM5102A DAC, TCA8418 key matrix), one block per hardware
// revision: a new board adds #elif defined(HWV2). The Ritual Machine's own board_pins.h (apps/ritual_machine/src/platform/esp32) has
// the whole board: display, joystick, knobs, LEDs, TF card.
#if defined(HWV1)

// I2S audio out (PCM5102A)
#define PIN_I2S_MCLK  -1   // not wired: the DAC makes its own master clock
#define PIN_I2S_BCLK  9
#define PIN_I2S_DOUT  8
#define PIN_I2S_LRC   7    // word select
#define PIN_I2S_XSMT  13   // PCM5102A soft mute (HIGH = sound)

// Keyboard controller: TCA8418 on its own I2C bus (Wire1), 5 rows x 8 columns
#define PIN_KBD_SDA   42
#define PIN_KBD_SCL   41
#define HW_TCA_ADDR       0x34
#define HW_KBD_ROWS       5    // TCA rows 0..3 = the four note rows from the bottom, row 4 = the function keys
#define HW_KBD_COLS       8    // the columns run right to left: the leftmost key of a row is column 7
#define HW_KBD_I2C_HZ     400000

// Power: the button is a latching switch (down = on) read on PWR_SENSE; PWR_ON_EN high keeps the supply on after it came up.
#define PIN_PWR_ON_EN 6
#define PIN_PWR_SENSE 12

#else
#error "No hardware revision selected: add -DHWV1 (or a new revision block in board_pins.h) to build_flags"
#endif
