#pragma once
// HWV1 keyboard: TCA8418 key-scan IC on its own I2C bus (Wire1). 5 x 8 matrix, hardware debounce, FIFO of key events.
// Only the input task calls these.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <stdint.h>

typedef struct {
    uint8_t row;        // TCA row 0..4
    uint8_t col;        // TCA column 0..7
    bool    pressed;
} kbd_event_t;

bool kbd_init(void);                      // false if the TCA8418 does not answer (the rest of the firmware keeps running)
int  kbd_read_events(kbd_event_t *out, int max);   // drains the FIFO, returns the number of events; recovers the bus after repeated I2C failures
#endif
