// ESP32-S3 audio: the engine renders blocks on a dedicated task pinned to core 1 and streams them to an I2S DAC.
// Memory: modules and plans in internal RAM (fast), delay lines and the reverb pre-delay in PSRAM (bulk).
// SKELETON: written for ESP-IDF 5 / arduino-esp32 3 (driver/i2s_std.h), NOT compiled or tested on hardware yet.
// Pins are in board_pins.h. The UI runs in loop() on core 0 and talks to the engine only through its command queue.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include "hal/hal_audio.h"
#include "platform/engine/engine_synth.h"
#include "board_pins.h"

namespace {
constexpr size_t kFastBytes = 220 * 1024;          // internal RAM: voices, plans, chorus, reverb tank (about 100 KB)
constexpr size_t kBulkBytes = 1024 * 1024;         // PSRAM: two delay lines (192 KB at 48 kHz) and the pre-delay
constexpr int kFrames = 64;                        // frames per I2S write (the engine renders its own block size inside)

i2s_chan_handle_t tx;
TaskHandle_t audio_task;

void audio_task_main(void *) {
    static int16_t buf[kFrames * 2];
    for (;;) {
        engine_synth_render(buf, kFrames);
        size_t written = 0;
        i2s_channel_write(tx, buf, sizeof buf, &written, portMAX_DELAY);     // blocks until the DMA ring has room: this paces the task
    }
}
}  // namespace

extern "C" void audio_init(void) {
    void *fast = heap_caps_malloc(kFastBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *bulk = heap_caps_malloc(kBulkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fast || !bulk || engine_synth_init(fast, kFastBytes, bulk, kBulkBytes) != 0) return;

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 6;
    chan.dma_frame_num = kFrames;
    if (i2s_new_channel(&chan, &tx, nullptr) != ESP_OK) return;
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(engine_synth_sample_rate())),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = static_cast<gpio_num_t>(PIN_I2S_MCLK), .bclk = static_cast<gpio_num_t>(PIN_I2S_BCLK),
                     .ws = static_cast<gpio_num_t>(PIN_I2S_LRC), .dout = static_cast<gpio_num_t>(PIN_I2S_DOUT), .din = I2S_GPIO_UNUSED,
                     .invert_flags = {false, false, false}},
    };
    i2s_channel_init_std_mode(tx, &cfg);
    i2s_channel_enable(tx);
    xTaskCreatePinnedToCore(audio_task_main, "audio", 8192, nullptr, configMAX_PRIORITIES - 2, &audio_task, 1);
}

extern "C" void audio_shutdown(void) {
    if (audio_task) vTaskDelete(audio_task);
    if (tx) { i2s_channel_disable(tx); i2s_del_channel(tx); }
    engine_synth_shutdown();
}

extern "C" void audio_set_params(const rack_t *r, const synth_params_t *p) { engine_synth_set_params(r, p); }
extern "C" void audio_build(const rack_t *r, const synth_params_t *p)      { engine_synth_build(r, p); }
extern "C" int audio_sample_count(void)                                     { return engine_synth_sample_count(); }   // the TF card library is not wired up yet
extern "C" bool audio_sample_info(int i, audio_sample_info_t *out)         { return engine_synth_sample_info(i, out); }
extern "C" bool audio_sample_prepare(int)                                 { return false; }
extern "C" int audio_samples_rescan(void)                                  { return engine_synth_sample_count(); }
extern "C" void audio_set_clock(int bpm, int steps, int swing, int running) { engine_synth_set_clock(bpm, steps, swing, running); }
extern "C" void audio_motion_restart(void)                                 { engine_synth_motion_restart(); }
extern "C" void audio_note_on(int midi_note)                               { engine_synth_note_on(midi_note); }
extern "C" void audio_note_off(int midi_note)                              { engine_synth_note_off(midi_note); }
extern "C" uint32_t audio_millis(void)                                     { return engine_synth_millis(); }
extern "C" void audio_update(void)                                         {}     // nothing to pump: the audio task is independent
#endif // ARDUINO_ARCH_ESP32
