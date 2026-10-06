// ESP32 entry point (PlatformIO / Arduino). Build flag -DHWV1 selects the pin block and the drivers of the old prototype.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "core/app.h"
#include "hal/hal_audio.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_leds.h"
#include "platform/esp32/board_esp32.h"
#include "platform/esp32/serial_cmd_esp32.h"

static app_t app;

#ifdef HWV1_DEBUG_UI
// dev only: once a second, the input events handled, the slowest app step (input handling + redraw) and the slowest audio_set_params
extern uint32_t g_ui_params_n, g_ui_params_max_us;
static uint32_t ui_events, ui_step_max_us, ui_last_ms;
#endif

// Bigger loop stack than the 8 KB default (SynthBox raised it to 20 KB after a stack overflow); not measured for this app.
size_t getArduinoLoopTaskStackSize(void) { return 16384; }

void setup() {
    board_power_init();                        // before anything slow: the board must keep itself powered
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);                  // never wait for a host: debug prints from the audio and input tasks must not block them when nobody reads the port
#ifdef DEV_BOOT_DELAY_MS
    delay(DEV_BOOT_DELAY_MS);                  // dev only: lets the serial monitor reconnect after a reset so the boot messages are not lost
#endif
    leds_init();
    input_esp32_init();
    board_audio_enable();
    audio_init();
    app_init(&app, display_init());
#ifdef DEV_SERIAL_CMD
    serial_cmd_attach(&app);
#endif
}

void loop() {
    board_power_poll();
#ifdef DEV_SERIAL_CMD
    serial_cmd_poll();                         // dev only: commands from tools/serial_test.py
#endif
    const input_event_t e = input_poll();
#ifdef HWV1_DEBUG_UI
    const uint32_t t0 = micros();
#endif
    app_step(&app, e);
#ifdef HWV1_DEBUG_UI
    const uint32_t dt = micros() - t0;
    if (e.kind != IN_NONE) ui_events++;
    if (dt > ui_step_max_us) ui_step_max_us = dt;
    if (millis() - ui_last_ms >= 1000) {
        ui_last_ms = millis();
        if (ui_events) Serial.printf("[UI] events %u, slowest step %u us, set_params %u calls, slowest %u us\n", (unsigned)ui_events, (unsigned)ui_step_max_us, (unsigned)g_ui_params_n, (unsigned)g_ui_params_max_us);
        ui_events = ui_step_max_us = g_ui_params_n = g_ui_params_max_us = 0;
    }
#endif
    if (e.kind == IN_NONE) delay(1);           // idle: yield so the idle task on this core can run (its watchdog fires otherwise)
}
#endif // ARDUINO_ARCH_ESP32
