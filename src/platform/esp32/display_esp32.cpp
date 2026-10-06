// ESP32 display driver. HWV1: SH1107 128x128 OLED on I2C (Wire0, pins in board_pins.h), same panel and setup as
// github.com/clement-chupin/SynthBox (src/oled.cpp).
//
// A full frame takes about 120 ms on this bus (measured, 400 kHz), and the UI loop used to wait for it: a key or a knob turn arriving meanwhile
// was handled up to 120 ms late (late notes, steppy cutoff sweeps). Now display_send() only copies the frame (2 KB) and wakes the display task,
// which sends the tiles (8 x 8 pixels) that differ from the last frame it sent. The task alone uses the bus after init; the application keeps
// drawing into the u8g2 buffer. If frames come faster than the bus, the task skips to the newest one.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "hal/hal_display.h"
#include "platform/esp32/board_pins.h"

// U8g2 constructor order for HW I2C with explicit pins: (rotation, reset, clock, data).
static U8G2_SH1107_128X128_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

namespace {
constexpr int kTilesX = DISPLAY_WIDTH / 8, kTilesY = DISPLAY_HEIGHT / 8, kBytes = kTilesX * kTilesY * 8;
uint8_t pending[kBytes];                       // the newest frame from the application (guarded by mtx)
uint8_t work[kBytes], sent[kBytes];            // the display task's copy, and what the panel shows
SemaphoreHandle_t mtx;
TaskHandle_t task;

void display_task(void *) {
    u8x8_t *u8x8 = u8g2_GetU8x8(oled.getU8g2());
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);                  // pdTRUE: several frames sent meanwhile = one wake-up, the newest frame
        xSemaphoreTake(mtx, portMAX_DELAY);
        memcpy(work, pending, kBytes);
        xSemaphoreGive(mtx);
        for (int ty = 0; ty < kTilesY; ty++) {
            const uint8_t *row = work + ty * kTilesX * 8, *old = sent + ty * kTilesX * 8;
            int tx = 0;
            while (tx < kTilesX) {
                if (!memcmp(row + tx * 8, old + tx * 8, 8)) { tx++; continue; }
                int end = tx + 1;                                     // a run of changed tiles; a single unchanged tile inside it is sent too
                while (end < kTilesX && (memcmp(row + end * 8, old + end * 8, 8) ||          // (one more tile costs less than a new command)
                                         (end + 1 < kTilesX && memcmp(row + (end + 1) * 8, old + (end + 1) * 8, 8)))) end++;
                u8x8_DrawTile(u8x8, (uint8_t)tx, (uint8_t)ty, (uint8_t)(end - tx), const_cast<uint8_t *>(row + tx * 8));
                tx = end;
            }
        }
        memcpy(sent, work, kBytes);
    }
}
}  // namespace

extern "C" u8g2_t *display_init(void) {
    oled.begin();
    Wire.setClock(400000);                  // 400 kHz (SH1107 limit); about 120 ms per full frame measured
    oled.sendF("ca", 0xD3, -32);            // display offset, required by this panel (see SynthBox HWLayout.h)
    oled.clearBuffer();
    oled.sendBuffer();
    memset(sent, 0, kBytes);                // the panel is blank
    mtx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(display_task, "display", 4096, nullptr, 1, &task, 0);   // core 0 with the UI; the I2C waits let the UI loop run
    return oled.getU8g2();
}

extern "C" void display_send(u8g2_t *g) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    memcpy(pending, u8g2_GetBufferPtr(g), kBytes);
    xSemaphoreGive(mtx);
    xTaskNotifyGive(task);
}
#endif // ARDUINO_ARCH_ESP32 && HWV1
