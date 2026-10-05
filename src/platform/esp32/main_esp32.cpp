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
}

void loop() {
    board_power_poll();
#ifdef DEV_SERIAL_CMD
    serial_cmd_poll();                         // dev only: commands from tools/serial_test.py
#endif
    const input_event_t e = input_poll();
    app_step(&app, e);
    if (e.kind == IN_NONE) delay(1);           // idle: yield so the idle task on this core can run (its watchdog fires otherwise)
}
#endif // ARDUINO_ARCH_ESP32
