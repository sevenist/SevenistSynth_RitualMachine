#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "hal/hal_power.h"
#include "boards/esp32/board_esp32.h"
#include "boards/esp32/board_pins.h"

#if defined(HWV1)
#include "boards/esp32/mux_esp32.h"
#include "boards/esp32/power_esp32.h"
#include "boards/hwv1/hwv1_layout.h"

// The first prototype's board services (boards/esp32/board_esp32.h): how its power button and joystick are read for the shared soft power
// (boards/esp32/power_esp32.cpp), the DAC's mute and the speaker's amplifier, what the card reader reads.

namespace {
bool g_speaker_on = true;                      // what board_speaker_on was last told (board_audio_off does not change it)
bool g_audio_open = false;                     // board_audio_open was called: the start is over, the DAC is unmuted
}  // namespace

// ---- the power button, for power_esp32.cpp: a latching switch whose position reads on PWR_SENSE (board_pins.h) ----
void power_switch_init(void) { pinMode(PIN_PWR_SENSE, INPUT); }
bool power_switch_direct(void) { return digitalRead(PIN_PWR_SENSE) == (HW_PWR_SENSE_ON ? HIGH : LOW); }
bool board_power_button_on(void) { return power_switch_direct(); }
void power_sense_sample(int) {}                 // the button has its own pin: the battery's channel says nothing about it

bool power_joystick_held(void) {
    pinMode(PIN_JOY_SW, INPUT_PULLUP);
    delay(2);
    return digitalRead(PIN_JOY_SW) == (HWV1_JOY_SW_PUSHED ? HIGH : LOW);
}

void power_audio_resume(void) {
    if (g_audio_open) digitalWrite(PIN_I2S_XSMT, HIGH);
    board_speaker_on(g_speaker_on);
}

// ---- the card reader (a firmware of its own: no input task) ----
void board_reader_init(void) {
    mux_init();
    pinMode(PIN_JOY_SW, INPUT_PULLUP);
}

bool board_reader_joystick(void) { return digitalRead(PIN_JOY_SW) == (HWV1_JOY_SW_PUSHED ? HIGH : LOW); }

void board_reader_sensors(void) {               // the battery, as the input task reads it in the synth
    const uint16_t mv = mux_read_mv(HWV1_MUX_BATTERY);
    board_battery_sample(mux_read(HWV1_MUX_BATTERY), mv);
}

// The speaker's amplifier is an LM4871 (user, 2026-10-22; these comments first described a MAX98357A, which the board does not have):
// an analog mono amplifier after the PCM5102A, fed by its left channel, its shutdown pin on GPIO 5. As the LM4871's data sheet has
// it, a HIGH on that pin shuts it down and a LOW keeps it on (left floating, the board holds it low: the speaker plays). Driven
// both ways since 2026-10-22 (the user's choice: Spk Off is a real shutdown, no idle current, no hiss).
constexpr int kSpkOn = LOW, kSpkShutdown = HIGH;

static void speaker_pin(int level) {
    pinMode(PIN_SPK_SD, OUTPUT);
    digitalWrite(PIN_SPK_SD, level);
}

// The DAC stays muted here: board_audio_open unmutes it when the loading screen has ended.
void board_audio_enable(void) {
    pinMode(PIN_I2S_XSMT, OUTPUT);
    digitalWrite(PIN_I2S_XSMT, LOW);           // PCM5102A soft mute on
    pinMode(PIN_SPK_SD, INPUT);                // first look at what the board sets by itself (for the log)
    delay(5);
    const int floating = digitalRead(PIN_SPK_SD);
    speaker_pin(g_speaker_on ? kSpkOn : kSpkShutdown);
    Serial.printf("[BOARD] XSMT (GPIO %d) muted until the start is over; SPK_SD (GPIO %d) floating reads %d, now driven %s\n", PIN_I2S_XSMT,
                  PIN_SPK_SD, floating, g_speaker_on ? "LOW (on)" : "HIGH (shutdown)");
}

#ifndef HWV1_CARD_READER
void audio_esp32_open(void);                   // audio_esp32.cpp: the audio task's own gate

void board_audio_open(void) {
    if (g_audio_open) return;
    g_audio_open = true;
    digitalWrite(PIN_I2S_XSMT, HIGH);          // PCM5102A soft mute off
    audio_esp32_open();
}
#endif

void board_audio_off(void) {
    pinMode(PIN_I2S_XSMT, OUTPUT);
    digitalWrite(PIN_I2S_XSMT, LOW);           // PCM5102A soft mute
    speaker_pin(kSpkShutdown);                 // amplifier shutdown
}

// Off shuts the LM4871 down, on wakes it. The speaker's channel also goes to gain 0 at Off (audio_out_esp32.cpp board_speaker_level),
// which alone silenced it before the pin was driven the right way (the user, 2026-10-22).
void board_speaker_on(bool on) {
    g_speaker_on = on;
    speaker_pin(on ? kSpkOn : kSpkShutdown);
}
#endif // HWV1
#endif // ARDUINO_ARCH_ESP32
