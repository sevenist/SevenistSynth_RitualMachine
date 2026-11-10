#pragma once
// Board wiring, one block per hardware revision. Select it with a build flag (platformio.ini: -DHWV1).
// Every pin definition lives inside a revision block, so a new prototype adds its own block here; the code that differs between revisions is in its folder (boards/hwv1).
// -1 = not wired / not present on that revision.

#if defined(HWV1)
// HWV1 = ESP32-S3 prototype, same hardware as github.com/clement-chupin/SynthBox (include/HWConfig.h).

// I2S audio out (PCM5102A DAC; the speaker: an LM4871, an analog mono amplifier fed by the DAC's left channel)
#define PIN_I2S_MCLK  -1   // HWConfig.h lists GPIO 10 (PCM5102A SCK) but SynthBox's working firmware never drives it (mclk=9=bclk); -1 = no MCLK
#define PIN_I2S_BCLK  9
#define PIN_I2S_DOUT  8
#define PIN_I2S_LRC   7    // word select / LRCK
#define PIN_I2S_XSMT  13   // PCM5102A soft mute (HIGH = unmute)
#define PIN_SPK_SD    5    // LM4871 shutdown (wired, user 2026-10-22; its data sheet: HIGH = shut down, LOW = on; board_speaker_on)

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
#define HW_LED_NOT_APP    0       // a bit per chain LED: not the app's (leds_set_all leaves it alone)
#define HW_LED_ORDER      GRB
#ifndef HW_LED_MAX_BRIGHTNESS      // set in platformio.ini
#define HW_LED_MAX_BRIGHTNESS 51   // 20 % of 255: the user's limit (power: full white on 36 LEDs ~2 A browns out the board / USB). The driver never exceeds it
#endif
#ifndef HW_LED_BRIGHTNESS
#define HW_LED_BRIGHTNESS HW_LED_MAX_BRIGHTNESS   // at boot: the cap (leds_set_brightness may lower it; nothing calls it yet)
#endif

// Soft power-off. The power button is a latching push switch (user, 2026-10-11): down = on. PWR_SENSE reads HW_PWR_SENSE_ON while it
// is down; PWR_ON_EN high keeps the supply on after it came up, so the firmware can finish before the board goes off. The USB cable
// feeds the board whatever the button says.
#define PIN_PWR_ON_EN 6
#define PIN_PWR_SENSE 12
#define HW_PWR_SENSE_ON 1      // (the first firmware switched off on a low level; not measured on this board)
// 1 = a board that runs with its power button up (so: fed by the USB cable) starts the card reader (usb_storage_esp32.cpp).
// 0 = it runs the synth, as before 2026-10-11.
#ifndef HWV1_USB_STORAGE
#define HWV1_USB_STORAGE 1
#endif
#define HW_USB_STORAGE HWV1_USB_STORAGE
#define HW_CARD_READER_ENV "card_reader"   // its platformio.ini environment (the log names it when the slot is empty)

// Battery: 2 cells in series, through a divider onto mux channel 1 (hwv1_layout.h). The first firmware computed raw / 4095 * 10.4 V:
// 10.4 V on the battery puts the ADC's 3.3 V on the pin. Not checked against a meter: the serial command `power` prints the raw count,
// the pin's voltage and the result; change HW_BATT_FULL_SCALE_MV if it reads off. (A 3-cell pack needs the three other values changed.)
#define HW_BATT_FULL_SCALE_MV 10400   // the battery voltage that gives 3300 mV on the pin
#define HW_BATT_EMPTY_MV      6000    // 0 %: 3.0 V a cell (the first firmware's scale, 6.0 .. 8.4 V)
#define HW_BATT_FULL_MV       8400    // 100 %: 4.2 V a cell
#define HW_BATT_PRESENT_MV    4000    // below this no battery is connected (the board runs from the cable): none is reported

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

#elif defined(HWV2)
// HWV2 = the second prototype, KiCad project GRV-mini (Waveshare ESP32-S3-Pico). Read from the PCB file; "GP" on the schematic = the GPIO number.

// I2S to the ES8388 (its control on the shared I2C bus below). The PCB's net BCK is the codec's MCLK, and its net SCK the bit clock.
#define PIN_I2S_MCLK  15
#define PIN_I2S_BCLK  17
#define PIN_I2S_DOUT  18
#define PIN_I2S_LRC   33
#define PIN_I2S_DIN   34
#define PIN_SPK_SD    35   // LM4871 shutdown (HIGH = shut down, LOW = on), through 10 k

// The screen on CN7 (GMT177-10, 1.77" 128 x 160 TFT, ST7735S family) on SPI. CS and reset have no GPIO on rev A: solder jumpers tie CS to
// ground (JP3), reset to 3.3 V (JP1) and the backlight to 3.3 V (JP2). A reset line for later: REV-B-NOTES.md (GP38); set its number here.
#define PIN_LCD_SCK   42
#define PIN_LCD_SDA   41
#define PIN_LCD_DC    1
#define PIN_LCD_RST   -1
#define HW_DISPLAY_RGB565 1            // a colour screen: display_send_rgb565 frames go to it as they are (display_panel_show_rgb565)

// Analog multiplexer, 16 channels: the 8 pots, the joystick, the 4 buttons, the power sense. Rev A wires MuxCOM to GP38, which has no ADC: this needs
// the wire of REV-B-NOTES.md, bodge 4, from TP_muxCom1 to TP_bck2 (GP9).
#define PIN_MUX_S0    37
#define PIN_MUX_S1    36
#define PIN_MUX_S2    40
#define PIN_MUX_S3    39
#define PIN_MUX_ADC   9

// The I2C bus: the key scanner (U14, a CH32V006 at 0x42), the ES8388 at 0x10, the MPU-6050
#define PIN_I2C_SDA   13
#define PIN_I2C_SCL   14
#define HW_I2C_HZ     400000
#define HW_SCANNER_ADDR 0x42

// LED chain: 35 x WS2812B-Mini-V3J
#define PIN_LED_DATA      16
#define HW_NUM_LEDS       35
#define HW_LED_ORDER      GRB
#define HW_LED_NOT_APP    0x00000FE0   // the VU meter (5..10) and the indicator (11)
#define HW_HAS_VU         1            // the audio output measures its level (board_audio_peak)
#ifndef HW_LED_MAX_BRIGHTNESS
#define HW_LED_MAX_BRIGHTNESS 51
#endif
#ifndef HW_LED_BRIGHTNESS
#define HW_LED_BRIGHTNESS HW_LED_MAX_BRIGHTNESS
#endif

// Soft power: PWR_EN high keeps the supply on. The switch's position has no pin of its own: it is read with the battery, on mux channel 7
// (boards/hwv2/board_esp32.cpp).
#define PIN_PWR_ON_EN 10
// 1 = a board that runs switched off (so: fed by the USB cable) starts the card reader, as the first prototype does. 0 = it runs the synth, the switch
// ignored until it is seen on. 0 while no battery is connected: switched on or off, a board without one reads off (channel 7 sees no battery), and would
// start the card reader every time. Once a battery is in: 1 here, or -DHWV2_USB_STORAGE=1 in [env:hwv2].
#ifndef HWV2_USB_STORAGE
#define HWV2_USB_STORAGE 0
#endif
#define HW_USB_STORAGE HWV2_USB_STORAGE
#define HW_CARD_READER_ENV "hwv2_card_reader"

// Battery: 2 cells in series (REV-B-NOTES.md). Switched on, it reaches mux channel 7 through R20 + R21 (10 k + 150 k) over R22 (68 k): the pin
// sees 68 / 228 of it, so 3300 mV on the pin is 11.06 V. Computed from the parts, not checked against a meter yet: the serial command `power`
// prints the pin's voltage and the result; change HW_BATT_FULL_SCALE_MV if it reads off.
#define HW_BATT_FULL_SCALE_MV 11065   // the battery voltage that gives 3300 mV on the pin
#define HW_BATT_EMPTY_MV      6000    // 0 %: 3.0 V a cell
#define HW_BATT_FULL_MV       8400    // 100 %: 4.2 V a cell
#define HW_BATT_PRESENT_MV    4000    // below this, no battery (or switched off): none is reported, and the switch counts as off

// SD card: 4-bit SDMMC
#define PIN_SD_CLK    5
#define PIN_SD_CMD    6
#define PIN_SD_D0     2
#define PIN_SD_D1     4
#define PIN_SD_D2     8
#define PIN_SD_D3     7

#else
#error "No hardware revision selected: add -DHWV1 or -DHWV2 (or a new revision block in board_pins.h) to build_flags"
#endif
