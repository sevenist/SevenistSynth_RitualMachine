#if defined(ARDUINO_ARCH_ESP32)
// The firmware's entry point for an app made from apps/template (app/app_main.h). The library's board (framework/boards/hwv1 or hwv2, the
// build's choice) gives the display, the controls, the power button or switch and the audio output: its audio task on core 1 calls
// synth_render() and streams over I2S. loop() runs on core 0 (build flag ARDUINO_RUNNING_CORE=0), reads the controls and draws.
#include <Arduino.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include "app/app_main.h"
#include "boards/esp32/board_esp32.h"
#include "hal/hal_audio_out.h"

#ifndef APP_FAST_BYTES
#define APP_FAST_BYTES (64 * 1024)          // internal RAM for the engine: module state and the plan's buffers
#endif
#ifndef APP_BULK_BYTES
#define APP_BULK_BYTES (256 * 1024)         // PSRAM, when the board has it: long delay lines, samples
#endif

namespace {
constexpr size_t kFastBytes = APP_FAST_BYTES, kBulkBytes = APP_BULK_BYTES;
char tag[24];                               // "[NAME]", from app_name()

bool synth_start() {
    void *fast = heap_caps_malloc(kFastBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *bulk = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 2 * kBulkBytes ? heap_caps_malloc(kBulkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    return fast && synth_init(fast, kFastBytes, bulk, bulk ? kBulkBytes : 0);
}
}  // namespace

void setup() {
    board_power_init();                             // before anything slow: the board keeps itself powered
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);                       // never wait for a host that does not read the port
    size_t n = 0;
    tag[n++] = '[';
    for (const char *c = app_name(); *c && n + 2 < sizeof tag; c++) tag[n++] = static_cast<char>(toupper(static_cast<unsigned char>(*c)));
    tag[n++] = ']';
    tag[n] = 0;
    u8g2_t *const display = display_init();
    input_esp32_init();                             // the key matrix, the knobs and the joystick, scanned by the board's own task
    board_audio_enable();                           // the DAC stays muted while everything starts
    if (!synth_start()) Serial.printf("%s the synth did not start\n", tag);
    else if (!audio_out_start(synth_render, synth_sample_rate())) Serial.printf("%s the audio output did not start\n", tag);
    app_start(display);
    display_boot_end();                             // the start is over: the board lets the sound through
    Serial.printf("%s running at %d Hz\n", tag, synth_sample_rate());
}

void loop() {
    if (board_power_poll()) board_power_off();      // the power button went up: on the battery the board stops here
    const input_event_t e = input_poll();
    if (e.kind != IN_NONE) app_event(e);
    synth_update();
    app_draw();
    if (e.kind == IN_NONE) delay(1);                // idle: let the idle task of this core run (its watchdog fires otherwise)
}
#endif  // ARDUINO_ARCH_ESP32
