// SKELETON: needs the U8g2 library in platformio.ini (lib_deps = olikraus/U8g2)
// and the real pins/controller of your screen.
#if defined(ARDUINO_ARCH_ESP32)
#include <U8g2lib.h>
#include "hal/hal_display.h"

// Hardware I2C SSD1306 128x64; adjust controller/pins for your board.
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

extern "C" u8g2_t *display_init(void) {
    oled.begin();
    return oled.getU8g2();
}
#endif // ARDUINO_ARCH_ESP32
