#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "platform/esp32/board_esp32.h"
#include "platform/esp32/board_pins.h"

#if defined(HWV1)
// Soft power: PWR_ON_EN HIGH holds the supply on; the power button shows up as PWR_SENSE low, and pulling PWR_ON_EN low cuts the board.
void board_power_init(void) {
    pinMode(PIN_PWR_ON_EN, OUTPUT);
    digitalWrite(PIN_PWR_ON_EN, HIGH);
    pinMode(PIN_PWR_SENSE, INPUT);
}

// MAX98357A SD_MODE is not a plain enable: its voltage picks the channel (about 3.3 V = left only, about 1 V = (L+R)/2, low = shutdown), and
// the board's resistors set that level. SynthBox's firmware never drives the pin ("let pull-up handle it" in GrvEP, "pull-down" in Ritual
// Machine), so the default here is to leave it floating. HWV1_SPK_SD_MODE=1 drives it HIGH instead (left channel only).
#ifndef HWV1_SPK_SD_MODE
#define HWV1_SPK_SD_MODE 0
#endif

void board_audio_enable(void) {
    pinMode(PIN_I2S_XSMT, OUTPUT);
    digitalWrite(PIN_I2S_XSMT, HIGH);          // PCM5102A soft mute off
    pinMode(PIN_SPK_SD, INPUT);                // first look at what the board sets by itself
    delay(5);
    const int floating = digitalRead(PIN_SPK_SD);
#if HWV1_SPK_SD_MODE == 1
    pinMode(PIN_SPK_SD, OUTPUT);
    digitalWrite(PIN_SPK_SD, HIGH);
#endif
    Serial.printf("[BOARD] XSMT (GPIO %d) reads %d; SPK_SD (GPIO %d) floating reads %d, mode %d (0 = left floating, 1 = driven HIGH)\n", PIN_I2S_XSMT,
                  digitalRead(PIN_I2S_XSMT), PIN_SPK_SD, floating, HWV1_SPK_SD_MODE);
}

void board_power_poll(void) {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (now - last < 500) return;
    last = now;
    if (now < 2000) return;                    // ignore the sense line right after boot, while the power button may still be down
    if (digitalRead(PIN_PWR_SENSE) == LOW) {
        delay(50);
        if (digitalRead(PIN_PWR_SENSE) == LOW) digitalWrite(PIN_PWR_ON_EN, LOW);
    }
}
#else
void board_power_init(void) {}
void board_audio_enable(void) {}
void board_power_poll(void) {}
#endif
#endif // ARDUINO_ARCH_ESP32
