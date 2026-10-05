#pragma once
// ESP32 board services that are not one of the HAL areas (power latch, audio-chip enables) plus the init of the input driver.
#if defined(ARDUINO_ARCH_ESP32)
void board_power_init(void);      // FIRST thing in setup(): latches the power rail so the board stays on after the button is released
void board_audio_enable(void);    // unmutes the DAC and enables the speaker amplifier
void board_power_poll(void);      // call from loop(): switches the board off when the power button asks for it
void input_esp32_init(void);      // starts the input scan (keyboard, knobs, joystick); see input_esp32.cpp
#endif
