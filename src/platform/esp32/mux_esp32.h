#pragma once
// HWV1 analog multiplexer: 16 channels, 4 select lines, one shared ADC input (pins in board_pins.h).
// Not thread-safe: only the input task uses it.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <stdint.h>

void     mux_init(void);
uint16_t mux_read(uint8_t channel);        // 12 bit raw ADC value (0..4095) of channel 0..15
bool     mux_read_digital(uint8_t channel);
#endif
