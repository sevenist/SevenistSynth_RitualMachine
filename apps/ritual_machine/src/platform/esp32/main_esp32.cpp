// ESP32 entry point (PlatformIO / Arduino). Build flag -DHWV1 selects the pin block and the drivers of the old prototype.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "app/app.h"
#include "core/audio.h"
#include "core/card_layout.h"
#include "core/ui_sprites_gen.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_leds.h"
#include "boards/hwv1/board_esp32.h"
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
    if (board_power_started_off()) board_card_reader();   // the power button is up, the cable feeds the board: the card reader's firmware (back only when there is none)
    card_layout_apply();                       // the card's folders, before the card task starts (audio_init)
    {   // the board's boot animation: the image 64/boot of assets/UI_Sprites, the machine's name and version over it
        static display_boot_t boot;
        const gui_sprite_t *const sp = ui_sprite("64/boot");
        if (sp) {
            boot = {sp->data, sp->w, sp->h, sp->frames, BOOT_FRAME_MS, app_boot_draw_text};
            display_boot_config(&boot);
        }
    }
    u8g2_t *const display = display_init();    // first: its boot task plays the animation while everything below starts (display_esp32.cpp)
#ifdef BOOT_TRACE
    Serial.printf("[BOOT] display ready at %u ms\n", (unsigned)millis());
#endif
#ifdef DEV_BOOT_DELAY_MS
    delay(DEV_BOOT_DELAY_MS);                  // dev only: lets the serial monitor reconnect after a reset so the boot messages are not lost
#endif
    display_esp32_boot_stage(40);              // the loading bar under the boot animation: setup()'s part of it, by the time each step takes
#ifdef HWV1_DEBUG_AUDIO
    // dev only: internal RAM (free, largest block) before each init step; the audio fast heap needs a large block (audio_esp32.cpp)
    auto ram = [](const char *step) { Serial.printf("[RAM] %-14s internal free %u, largest block %u\n", step, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                                                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)); };
    ram("boot");
#else
    auto ram = [](const char *) {};
#endif
    leds_init();
    ram("leds_init");
    input_esp32_init();
    ram("input_init");
    board_audio_enable();
    ram("audio_enable");
    display_esp32_boot_stage(70);
#ifdef BOOT_TRACE
    const uint32_t bt0 = millis();
#endif
    audio_init();
    display_esp32_boot_stage(80);
#ifdef BOOT_TRACE
    const uint32_t bt1 = millis();
#endif
    app_init(&app, display);                   // (its first frame waits in the display task until the boot animation has ended)
    display_esp32_boot_stage(100);             // the rest of the bar is the application's (display_boot_progress)
#ifdef BOOT_TRACE
    Serial.printf("[BOOT] audio_init %u..%u ms, app_init ..%u ms\n", (unsigned)bt0, (unsigned)bt1, (unsigned)millis());
#endif
#ifdef DEV_SERIAL_CMD
    serial_cmd_attach(&app);
#endif
}

void loop() {
    if (board_power_poll()) {                  // the power button went up: what waits to be saved, then the supply
        app_save_now(&app);
        board_power_off();
    }
#ifdef DEV_SERIAL_CMD
    serial_cmd_poll();                         // dev only: commands from tools/serial_test.py
#endif
    const input_event_t e = input_poll();
#ifdef HWV1_DEBUG_UI
    const uint32_t t0 = micros();
#endif
#ifdef BOOT_TRACE
    // dev only: when the loop starts, and the controls that speak in its first seconds (an event that waits at start acts on the UI)
    static uint32_t bt_loop = 0;
    if (!bt_loop) { bt_loop = millis(); Serial.printf("[BOOT] loop starts at %u ms\n", (unsigned)bt_loop); }
    if (e.kind != IN_NONE && millis() - bt_loop < 5000) Serial.printf("[BOOT] +%u ms: event kind %d ctl %d value %d\n", (unsigned)(millis() - bt_loop), (int)e.kind, (int)e.ctl, e.value);
    const uint32_t bt_a = micros();
    app_step(&app, e);
    {   // what the start waits for: the card, the pads' files (lib_dirty), then `started`
        static int was_dirty = -1, was_started = -1, was_sd = -1;
        static uint32_t was_gen = 0xFFFFFFFFu;
        const uint32_t us = micros() - bt_a;
        if (!app.started && us > 100000) Serial.printf("[BOOT] +%u ms: a step of %u ms\n", (unsigned)(millis() - bt_loop), (unsigned)(us / 1000));
        if ((int)app.lib_dirty != was_dirty || (int)app.started != was_started || (int)audio_sd_state() != was_sd || app.sd_gen != was_gen) {
            was_dirty = app.lib_dirty; was_started = app.started; was_sd = (int)audio_sd_state(); was_gen = app.sd_gen;
            Serial.printf("[BOOT] +%u ms: card state %d generation %u, pads' files %s, missing mask %llx, start %s (audio clock %u, boot_ms %u)\n", (unsigned)(millis() - bt_loop), was_sd,
                          (unsigned)was_gen, app.lib_dirty ? "being read" : "in", (unsigned long long)app.lib_missing, app.started ? "over" : "going on", (unsigned)audio_millis(), (unsigned)app.boot_ms);
        }
    }
#else
    app_step(&app, e);
#endif
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
