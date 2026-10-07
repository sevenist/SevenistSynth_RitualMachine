#pragma once
// Board wiring, one block per hardware revision. Select it with a build flag (platformio.ini: -DHWV1).
// Every pin definition lives inside a revision block, so a new prototype adds #elif defined(HWV2) and nothing else changes.
// -1 = not wired / not present on that revision.

#if defined(HWV1)
// HWV1 = ESP32-S3 prototype, same hardware as github.com/clement-chupin/SynthBox (include/HWConfig.h).

// I2S audio out (PCM5102A DAC + MAX98357A speaker amp)
#define PIN_I2S_MCLK  -1   // HWConfig.h lists GPIO 10 (PCM5102A SCK) but SynthBox's working firmware never drives it (mclk=9=bclk); -1 = no MCLK
#define PIN_I2S_BCLK  9
#define PIN_I2S_DOUT  8
#define PIN_I2S_LRC   7    // word select / LRCK
#define PIN_I2S_XSMT  13   // PCM5102A soft mute (HIGH = unmute)
#define PIN_SPK_SD    5    // MAX98357A shutdown (HIGH = enabled)

// OLED (I2C)
#define PIN_OLED_SDA  38
#define PIN_OLED_SCL  39

// Joystick: X/Y on ADC1 (ADC2 is unusable while Wi-Fi runs)
#define PIN_JOY_X     2
#define PIN_JOY_Y     1
#define PIN_JOY_SW    40

// Analog multiplexer, 16 channels (4 select lines, shared ADC input)
#define PIN_MUX_S0    18
#define PIN_MUX_S1    33
#define PIN_MUX_S2    35
#define PIN_MUX_S3    34
#define PIN_MUX_ADC   4

// Keyboard on its own I2C bus; WS2812 LED chain (36 LEDs)
#define PIN_KBD_SDA   42
#define PIN_KBD_SCL   41
#define PIN_LED_DATA  14

// Keyboard controller: TCA8418 on Wire1, 5 rows x 8 columns (rows 0..3 = 4 x 8 note keys, row 4 = 4 function keys in columns 4..7)
#define HW_TCA_ADDR       0x34
#define HW_KBD_ROWS       5
#define HW_KBD_COLS       8
#define HW_KBD_I2C_HZ     400000

// LED chain: 36 x SK6812, FastLED colour order GRB (the SK6812's own order). SynthBox used BGR (red and blue swapped, seen 2026-10-06); RGB then
// left red and green swapped (the user, 2026-10-07: the LEDS tab's Red showed green). Not yet checked on the board.
#define HW_NUM_LEDS       36
#define HW_LED_ORDER      GRB
#ifndef HW_LED_MAX_BRIGHTNESS      // set in platformio.ini
#define HW_LED_MAX_BRIGHTNESS 51   // 20 % of 255: the user's limit (power: full white on 36 LEDs ~2 A browns out the board / USB). The driver never exceeds it
#endif
#ifndef HW_LED_BRIGHTNESS
#define HW_LED_BRIGHTNESS HW_LED_MAX_BRIGHTNESS   // at boot: the cap (leds_set_brightness may lower it; nothing calls it yet)
#endif

// Soft power-off
#define PIN_PWR_ON_EN 6
#define PIN_PWR_SENSE 12

// SD card (SPI)
#define PIN_SD_MOSI   36
#define PIN_SD_SCLK   15
#define PIN_SD_MISO   16
#define PIN_SD_CS     37

// Not on HWV1: encoders, discrete buttons, a direct key matrix (keys are behind the I2C keyboard), a dedicated volume pin.
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
#define PIN_VOLUME      -1      // read through a mux channel instead
#define PIN_KEY_COL_0   -1
#define PIN_KEY_COL_1   -1
#define PIN_KEY_COL_2   -1
#define PIN_KEY_COL_3   -1
#define PIN_KEY_ROW_0   -1
#define PIN_KEY_ROW_1   -1
#define PIN_KEY_ROW_2   -1
#define PIN_KEY_ROW_3   -1
#define PIN_KEY_ROW_4   -1

#else
#error "No hardware revision selected: add -DHWV1 (or a new revision block in board_pins.h) to build_flags"
#endif
