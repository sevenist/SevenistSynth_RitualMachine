#pragma once
// ESP32 board services that are not one of the HAL areas (power latch, power button, battery, audio-chip enables) plus the init of the
// input driver.
#if defined(ARDUINO_ARCH_ESP32)
void board_power_init(void);      // FIRST thing in setup(): latches the power rail so the board stays on while the firmware has work to finish
void board_audio_enable(void);    // sets up the DAC's mute pin (muted) and the speaker amplifier's pin
void board_audio_open(void);      // the loading screen has ended: the DAC is unmuted and the audio task's output let through (display_esp32.cpp calls it)
bool display_esp32_loading(void); // the loading screen is still up (the key LEDs stay dark meanwhile: leds_esp32.cpp)
void board_audio_off(void);       // DAC muted, speaker amplifier in shutdown (before the power goes, and in the card reader)
void board_speaker_on(bool on);   // false = speaker amplifier in shutdown (the DAC / headphone output is not affected)
void board_speaker_level(int pct);    // the built-in loudspeaker's level, 0..100 (0 = off): a gain on its channel of the output (audio_out_esp32.cpp)
void input_esp32_init(void);      // starts the input scan (keyboard, knobs, joystick); see input_esp32.cpp
void display_esp32_boot_stage(int percent);   // setup() says how far it is (0..100): its part of the boot animation's loading bar (display_esp32.cpp)

// The power button and the USB cable. The button is a latching switch: down = on. The cable feeds the board whatever the button says,
// so a board that runs with the button up is on the cable: the machine then counts as off, and becomes the card reader of its TF card
// until the button goes down. The card reader is a firmware of its own in the second application slot (usb_storage_esp32.cpp,
// platformio.ini env:card_reader): its USB stack would take 23 KB of the synth's internal RAM for nothing.
bool board_power_button_on(void); // the power button is down (on)
bool board_usb_host(void);        // a computer is on the USB cable (the serial port sees its frames); false for a charger
// Once, right after board_power_init: true when the button is up and the board runs all the same (on the cable): start the card
// reader. With the joystick held pushed at that moment it answers false: the synth, the button ignored until it is next used.
bool board_power_started_off(void);
bool board_power_poll(void);      // call from loop(): true once when the button went up (save, then board_power_off)
void board_power_hold(bool on);   // the latch by hand (the card reader: it follows the button, so pulling the cable with the button up stops the board)
void board_power_off(void);       // lets the supply go. On the battery the board stops here; on the cable the card reader starts
bool board_boot_slot(bool card_reader);   // which firmware the bootloader starts from now on: the synth's or the card reader's; false = that slot is empty
void board_card_reader(void);     // restarts into the card reader; returns only when there is none (then the synth goes on, the button ignored)

// The battery: the input task (or the card reader's loop) hands in what it read on the mux's battery channel, raw count and millivolts
// on the pin; hal/hal_power.h reports the smoothed result. board_battery_debug: the last reading as it came, for the serial command `power`.
void board_battery_sample(int raw, int pin_mv);
void board_battery_debug(int *raw, int *pin_mv);
int  input_esp32_aux_raw(void);   // the mux's spare input (hwv1_layout.h HWV1_MUX_AUX): its last raw count ...
bool input_esp32_aux_pushed(void);    // ... and read as a button
#endif
