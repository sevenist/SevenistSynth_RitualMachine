#if defined(ARDUINO_ARCH_ESP32)
// Firmware for the first prototype (HWV1). The library's board (framework/boards/hwv1) gives the display, the controls, the power
// button and the audio output: its audio task on core 1 calls synth_render() and streams to the DAC over I2S. loop() runs on core 0
// (build flag ARDUINO_RUNNING_CORE=0), reads the controls and draws. What the controls do: src/app.cpp.
//
// NOT RUN ON THE BOARD in this form (2026-10-13: it builds). The version before it, with its own I2S and key code, did run there.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "app.h"
#include "boards/hwv1/board_esp32.h"
#include "hal/hal_audio_out.h"
#include "synth.h"

namespace {
constexpr size_t kFastBytes = 64 * 1024;            // internal RAM for the engine: module state and the plan's buffers
constexpr size_t kBulkBytes = 256 * 1024;           // PSRAM, when the board has it: long delay lines, samples (unused by this small synth)

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
    u8g2_t *const display = display_init();
    input_esp32_init();                             // the key matrix, the knobs and the joystick, scanned by the board's own task
    board_audio_enable();                           // the DAC stays muted while everything starts
    if (!synth_start()) Serial.println("[TEMPLATE] the synth did not start");
    else if (!audio_out_start(synth_render, synth_sample_rate())) Serial.println("[TEMPLATE] the audio output did not start");
    app_start(display);
    display_boot_end();                             // the start is over: the board lets the sound through
    Serial.printf("[TEMPLATE] running at %d Hz\n", synth_sample_rate());
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
