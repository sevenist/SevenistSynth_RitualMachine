#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <Arduino.h>
#include "platform/esp32/mux_esp32.h"
#include "platform/esp32/board_pins.h"

namespace {
const uint8_t kSelect[4] = {PIN_MUX_S0, PIN_MUX_S1, PIN_MUX_S2, PIN_MUX_S3};
constexpr int kSettleUs = 15;      // the analog switch + the ADC's sample capacitor need a moment after the channel changes

void select(uint8_t ch) {
    for (int i = 0; i < 4; i++) digitalWrite(kSelect[i], (ch >> i) & 1);
    delayMicroseconds(kSettleUs);
}
}  // namespace

void mux_init(void) {
    for (int i = 0; i < 4; i++) pinMode(kSelect[i], OUTPUT);
    pinMode(PIN_MUX_ADC, INPUT);
    analogReadResolution(12);
}

uint16_t mux_read(uint8_t channel) {
    select(channel & 15);
    int sum = 0;
    for (int i = 0; i < 4; i++) sum += analogRead(PIN_MUX_ADC);      // the ESP32 ADC is noisy: average 4 reads
    return (uint16_t)(sum / 4);
}

bool mux_read_digital(uint8_t channel) {
    select(channel & 15);
    return digitalRead(PIN_MUX_ADC) != 0;
}
#endif
