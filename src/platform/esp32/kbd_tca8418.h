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

// false if the TCA8418 does not answer (the rest of the firmware keeps running). Before the key scan starts it checks whether the key at TCA
// (hold_row, hold_col) is held, for up to hold_ms (setup waits that long only while the key stays down): see kbd_boot_hold().
bool kbd_init(int hold_row, int hold_col, uint32_t hold_ms);
bool kbd_boot_hold(void);                 // that key was held for the whole hold_ms at power-on
int  kbd_read_events(kbd_event_t *out, int max);   // drains the FIFO, returns the number of events; recovers the bus after repeated I2C failures
#endif
