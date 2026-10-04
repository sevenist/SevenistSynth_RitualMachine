// ESP32 entry point (PlatformIO / Arduino). SKELETON: not built or tested yet.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "core/app.h"
#include "hal/hal_audio.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"

static app_t app;

void setup() {
    audio_init();
    app_init(&app, display_init());
}

void loop() {
    app_step(&app, input_poll());
}
#endif // ARDUINO_ARCH_ESP32
