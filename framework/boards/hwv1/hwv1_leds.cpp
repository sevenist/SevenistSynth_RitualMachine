// Where each LED of the first prototype sits in its chain (boards/esp32/board_esp32.h, board_led_of_control).
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <stdint.h>
#include "boards/esp32/board_esp32.h"
#include "boards/esp32/board_pins.h"
#include "boards/hwv1/hwv1_layout.h"

namespace {
// Chain index of the LED under the key at physical position (row from the top, column from the left), -1 = none. Serpentine, read left to
// right: function keys 32..35 on the top row, then the note rows from the top: 31..24, 16..23, 15..8, 0..7 (the user saw every row mirrored
// with the column flip, 2026-10-06). SynthBox's table (crdToIdx, OOPSIE_LED_FLAG) is in TCA column order, which runs
// right to left on this board; the table's rows and columns are physical already, so no HWV1_FLIP_COLS here (applying it mirrored every row).
int led_index(int pr, int pc) {
    if (HWV1_FLIP_ROWS) pr = HW_KBD_ROWS - 1 - pr;
    switch (pr) {
        case 0: return pc < 4 ? 32 + pc : -1;
        case 1: return 31 - pc;
        case 2: return 16 + pc;
        case 3: return 15 - pc;
        case 4: return pc;
    }
    return -1;
}
}  // namespace

int board_led_of_control(int ctl) {
    if (ctl < 0 || ctl >= HWV1_CONTROL_COUNT || !kHwv1Controls[ctl].led) return -1;
    return led_index(kHwv1Controls[ctl].row, kHwv1Controls[ctl].col);
}

void board_leds_refresh(void) {}               // every LED is the app's
#endif
