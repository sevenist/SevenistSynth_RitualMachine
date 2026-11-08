// The second prototype's panel (boards/esp32/display_panel.h): a 1.77" 128 x 160 TFT, ST7735S family, on SPI (pins in board_pins.h). The
// application draws in 1 bit with u8g2 as on any board; this file paints its pixels in two colours. A DMA transfer: the display task sleeps
// through it, so the UI loop on its core keeps running. A full frame is 41 KB, about 22 ms at this clock (the first prototype's OLED: 120 ms).
//
// CS is tied low on rev A and, without PIN_LCD_RST, nothing can reset the panel but a power cycle: a stray clock edge during the ESP32's start would
// put it out of step for good, which shows as a blank or garbled screen until the power goes off (REV-B-NOTES.md).
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV2)
#include <Arduino.h>
#include <U8g2lib.h>                            // (what makes PlatformIO link the library; u8g2 is used through its C interface)
#include <driver/spi_master.h>
#include "boards/esp32/board_pins.h"
#include "boards/esp32/display_panel.h"

namespace {
constexpr int kTilesX = DISPLAY_WIDTH / 8, kTilesY = DISPLAY_HEIGHT / 8;
constexpr int kClockHz = 15000000;              // the ST7735S's write limit
constexpr int kOffsetX = 0, kOffsetY = 0;       // where the panel's first pixel sits in the controller's memory
constexpr uint8_t kMadctl = 0x00;               // rotation and colour order: upright, as mounted, RGB (0x08 swaps red and blue)
// The two colours, as the controller reads them (RGB565, the high byte first).
constexpr uint16_t kColorOn = 0xFFFF, kColorOff = 0x0000;

spi_device_handle_t lcd;
u8g2_t u8g2;
uint8_t buffer[DISPLAY_WIDTH * kTilesY];        // the application's frame
DMA_ATTR uint16_t pixels[DISPLAY_WIDTH * 8];    // a row of tiles

// Blocks this task until the transfer has ended. From internal RAM: the arguments on a task's stack, `pixels`.
void send(const void *data, size_t bytes) {
    spi_transaction_t t = {};
    t.length = bytes * 8;
    t.tx_buffer = data;
    spi_device_transmit(lcd, &t);
}

void command(uint8_t c, const uint8_t *data = nullptr, size_t bytes = 0) {
    digitalWrite(PIN_LCD_DC, LOW);
    send(&c, 1);
    digitalWrite(PIN_LCD_DC, HIGH);
    if (bytes) send(data, bytes);
}

// `count` tiles of the row of tiles `ty`, from tile `tx`.
void send_tiles(const uint8_t *frame, int tx, int ty, int count) {
    const int x0 = tx * 8 + kOffsetX, x1 = x0 + count * 8 - 1, y0 = ty * 8 + kOffsetY, y1 = y0 + 7;
    const uint8_t columns[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    const uint8_t rows[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    command(0x2A, columns, 4);                  // CASET
    command(0x2B, rows, 4);                     // RASET
    command(0x2C);                              // RAMWR
    const uint8_t *src = frame + ty * DISPLAY_WIDTH + tx * 8;
    uint16_t *out = pixels;
    for (int bit = 0; bit < 8; bit++)
        for (int x = 0; x < count * 8; x++) *out++ = (src[x] >> bit) & 1 ? kColorOn : kColorOff;
    send(pixels, (size_t)(count * 8 * 8) * sizeof pixels[0]);
}

uint8_t display_cb(u8x8_t *u8x8, uint8_t msg, uint8_t, void *) {
    static const u8x8_display_info_t info = {
        /* chip_enable_level = */ 0, /* chip_disable_level = */ 1,
        /* post_chip_enable_wait_ns = */ 0, /* pre_chip_disable_wait_ns = */ 0, /* reset_pulse_width_ms = */ 0, /* post_reset_wait_ms = */ 0,
        /* sda_setup_time_ns = */ 0, /* sck_pulse_width_ns = */ 0, /* sck_clock_hz = */ kClockHz, /* spi_mode = */ 0,
        /* i2c_bus_clock_100kHz = */ 0, /* data_setup_time_ns = */ 0, /* write_pulse_width_ns = */ 0,
        /* tile_width = */ kTilesX, /* tile_height = */ kTilesY,
        /* default_x_offset = */ 0, /* flipmode_x_offset = */ 0,
        /* pixel_width = */ DISPLAY_WIDTH, /* pixel_height = */ DISPLAY_HEIGHT
    };
    if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) u8x8_d_helper_display_setup_memory(u8x8, &info);
    return 1;
}
}  // namespace

u8g2_t *display_panel_init(void) {
    u8g2_SetupDisplay(&u8g2, display_cb, u8x8_cad_empty, u8x8_byte_empty, u8x8_dummy_cb);
    u8g2_SetupBuffer(&u8g2, buffer, kTilesY, u8g2_ll_hvline_vertical_top_lsb, &u8g2_cb_r0);

    pinMode(PIN_LCD_DC, OUTPUT);
    digitalWrite(PIN_LCD_DC, HIGH);
    spi_bus_config_t bus = {};
    bus.mosi_io_num = PIN_LCD_SDA;
    bus.sclk_io_num = PIN_LCD_SCK;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = sizeof pixels;
    spi_device_interface_config_t device = {};
    device.clock_speed_hz = kClockHz;
    device.mode = 0;
    device.spics_io_num = -1;
    device.queue_size = 1;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK || spi_bus_add_device(SPI2_HOST, &device, &lcd) != ESP_OK) {
        Serial.println("[LCD] the SPI bus did not start");
        return &u8g2;
    }

#if PIN_LCD_RST >= 0
    pinMode(PIN_LCD_RST, OUTPUT);
    digitalWrite(PIN_LCD_RST, LOW);
    delay(10);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(120);
#endif
    const uint8_t pixel_format = 0x05;          // 16 bits a pixel
    command(0x01);                              // SWRESET: the ESP32 can restart while the panel stays powered
    delay(150);
    command(0x11);                              // SLPOUT
    delay(120);
    command(0x3A, &pixel_format, 1);            // COLMOD
    command(0x36, &kMadctl, 1);                 // MADCTL
    command(0x20);                              // INVOFF
    command(0x13);                              // NORON
    u8g2_ClearBuffer(&u8g2);
    for (int ty = 0; ty < kTilesY; ty++) send_tiles(buffer, 0, ty, kTilesX);   // what is in the panel's memory at power-on is noise: not shown
    command(0x29);                              // DISPON
    return &u8g2;
}

void display_panel_show(const uint8_t *frame, const uint8_t *shown) {
    if (!lcd) return;
    for (int ty = 0; ty < kTilesY; ty++) {
        const uint8_t *row = frame + ty * DISPLAY_WIDTH, *old = shown + ty * DISPLAY_WIDTH;
        int tx = 0;
        while (tx < kTilesX) {
            if (!memcmp(row + tx * 8, old + tx * 8, 8)) { tx++; continue; }
            int end = tx + 1;                   // a run of changed tiles; a single unchanged tile inside it is sent too (one more tile costs less than 3 commands)
            while (end < kTilesX && (memcmp(row + end * 8, old + end * 8, 8) ||
                                     (end + 1 < kTilesX && memcmp(row + (end + 1) * 8, old + (end + 1) * 8, 8)))) end++;
            send_tiles(frame, tx, ty, end - tx);
            tx = end;
        }
    }
}
#endif
