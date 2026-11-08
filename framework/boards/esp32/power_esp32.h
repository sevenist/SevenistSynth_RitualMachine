#pragma once
// What a board gives the soft power its ESP32 boards share (power_esp32.cpp: the latch, the switch's logic, the card reader's start, the battery's level).
// The logic is the same on every board: a supply latch on PIN_PWR_ON_EN, a power switch the firmware watches, a battery on a divider. How the switch and
// the joystick are read is the board's (boards/<board>/board_esp32.cpp), with board_power_button_on (board_esp32.h).
#if defined(ARDUINO_ARCH_ESP32)
void power_switch_init(void);     // once, from board_power_init: the switch's line set up
bool power_switch_direct(void);   // the switch read now. Only where nothing else uses its line: setup() before the input task starts, the card reader
bool power_joystick_held(void);   // at the start: the joystick held pushed (switched off on the cable, the synth all the same)
void power_audio_resume(void);    // the supply was let go but the board runs on (the cable): the audio outputs back as they were before board_audio_off
void power_sense_sample(int pin_mv);  // each battery reading (board_battery_sample): a board that reads its switch with the battery learns it here
#endif
