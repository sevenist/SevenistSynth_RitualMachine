// ESP32-S3 audio: the engine renders blocks on a dedicated task pinned to core 1 and streams them to an I2S DAC.
// Memory: modules and plans in internal RAM (fast), delay lines and the reverb pre-delay in PSRAM (bulk).
// SKELETON: written for ESP-IDF 5 / arduino-esp32 3 (driver/i2s_std.h), NOT compiled or tested on hardware yet.
// Pins are in board_pins.h. The UI runs in loop() on core 0 and talks to the engine only through its command queue.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "hal/hal_audio.h"
#include "platform/engine/engine_synth.h"
#include "engine/dsp/config.h"
#include "platform/esp32/bench_esp32.h"
#include "board_pins.h"

namespace {
constexpr size_t kFastMarginBytes = 40 * 1024;     // internal RAM kept free for what is created after audio_init (tasks, DMA, display buffer)
constexpr size_t kFastMaxBytes = 256 * 1024;       // no point in taking more than this
constexpr size_t kFastMinBytes = 64 * 1024;        // below this the internal block is not worth it: the fast heap goes to PSRAM
constexpr size_t kFastSpiramBytes = 400 * 1024;    // the fast heap when it has to live in PSRAM
constexpr size_t kBulkHeadroomBytes = 128 * 1024;  // PSRAM left for the rest of the firmware (sample cache, SD buffers, display)
constexpr size_t kBulkMaxBytes = 1600 * 1024;      // PSRAM: delay lines, the pre-delay, the spill of the fast heap
// Dev output attenuation, in percent of full scale (build flag DEV_OUTPUT_GAIN_PCT, 0..100): the first prototype's output stage has a very low
// impedance and is loud on headphones. It is the last step before the DAC, so it applies to the engine and to the test tone alike.
#ifndef DEV_OUTPUT_GAIN_PCT
#define DEV_OUTPUT_GAIN_PCT 100
#endif
static_assert(DEV_OUTPUT_GAIN_PCT >= 0 && DEV_OUTPUT_GAIN_PCT <= 100, "DEV_OUTPUT_GAIN_PCT is a percentage, 0..100");
constexpr int32_t kOutGainQ15 = DEV_OUTPUT_GAIN_PCT * 32768 / 100;
constexpr int kFrames = 64;                        // frames per I2S write (the engine renders its own block size inside)

i2s_chan_handle_t tx;
TaskHandle_t audio_task;

void audio_task_main(void *) {
    static int16_t buf[kFrames * 2];
#ifdef HWV1_DEBUG_AUDIO
    uint32_t t_report = millis(), blocks = 0, worst = 0, total = 0, late = 0;
    const uint32_t budget_us = (uint32_t)(1000000ull * kFrames / engine_synth_sample_rate());
#endif
    for (;;) {
#ifdef HWV1_DEBUG_AUDIO
        const uint32_t t0 = micros();
#endif
#ifdef HWV1_TEST_TONE
        for (int i = 0; i < kFrames; i++) {                  // 440 Hz sine, bypasses the engine: tests the I2S / DAC / amplifier path alone
            static float ph = 0;
            const int16_t v = (int16_t)(sinf(ph) * 8000);
            ph += 6.2831853f * 440.0f / (float)engine_synth_sample_rate();
            if (ph > 6.2831853f) ph -= 6.2831853f;
            buf[2 * i] = buf[2 * i + 1] = v;
        }
#else
        engine_synth_render(buf, kFrames);
#endif
#ifdef HWV1_DEBUG_AUDIO
        const uint32_t dt = micros() - t0;
        total += dt; blocks++;
        if (dt > worst) worst = dt;
        if (dt > budget_us) late++;
#endif
        if (kOutGainQ15 != 32768)
            for (int i = 0; i < kFrames * 2; i++) buf[i] = (int16_t)(((int32_t)buf[i] * kOutGainQ15) >> 15);
        size_t written = 0;
        i2s_channel_write(tx, buf, sizeof buf, &written, portMAX_DELAY);     // blocks until the DMA ring has room: this paces the task
#ifdef HWV1_DEBUG_AUDIO
        if (millis() - t_report >= 1000 && Serial.availableForWrite() > 160) {      // skip the report rather than block when the port is not being read
            Serial.printf("[AUDIO] render avg %u us, worst %u us, budget %u us per %d frames, %u blocks over budget of %u\n", (unsigned)(total / blocks),
                          (unsigned)worst, (unsigned)budget_us, kFrames, (unsigned)late, (unsigned)blocks);
#ifdef ENGINE_PROFILE
            {   // CPU cycles per rendered block, per module type; the block budget is cpu_hz * block / sample_rate
                struct Row { const char *name; uint32_t cyc, calls; };
                struct Acc { Row row[40]; int n; uint32_t sum; } acc = {{}, 0, 0};
                uint32_t nblocks = 0;
                engine_synth_profile([](const char *name, uint32_t cyc, uint32_t calls, void *u) {
                    Acc *a = static_cast<Acc *>(u);
                    a->sum += cyc;
                    if (a->n < 40) a->row[a->n++] = Row{name, cyc, calls};
                }, &acc, &nblocks);
                for (int i = 1; i < acc.n; i++)                                       // insertion sort, biggest first
                    for (int j = i; j > 0 && acc.row[j].cyc > acc.row[j - 1].cyc; j--) { const Row t = acc.row[j]; acc.row[j] = acc.row[j - 1]; acc.row[j - 1] = t; }
                char line[480];
                int len = 0;
                for (int i = 0; i < acc.n && i < 14 && len < (int)sizeof line - 40; i++)
                    len += snprintf(line + len, sizeof line - len, " %s=%u(x%u)", acc.row[i].name, (unsigned)acc.row[i].cyc, (unsigned)acc.row[i].calls);
                Serial.printf("[PROF] cycles per block (budget %u), total %u:%s\n", (unsigned)(ESP.getCpuFreqMHz() * 1000000ull * ENGINE_BLOCK / ENGINE_SR), (unsigned)acc.sum, line);
            }
#endif
            t_report = millis(); blocks = worst = total = late = 0;
        }
#endif
    }
}
}  // namespace

extern "C" void audio_init(void) {
#ifdef HWV1_BENCH
    bench_run();
#endif
    // "fast" = internal RAM (module state, short delay lines), sized from what is really free: the largest block minus a margin for the tasks,
    // DMA descriptors and display buffer created after this point. "bulk" = PSRAM (long delay lines, samples) and also where a full fast heap
    // spills over (Heap::set_spill), so a rack that needs more than fits in internal RAM still builds, just slower.
    const size_t internal_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    Serial.printf("[AUDIO] internal free %u (largest block %u), PSRAM free %u of %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)internal_block, (unsigned)psram_free, (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    size_t fast_bytes = internal_block > kFastMarginBytes ? internal_block - kFastMarginBytes : 0;
    if (fast_bytes > kFastMaxBytes) fast_bytes = kFastMaxBytes;
    bool fast_internal = fast_bytes >= kFastMinBytes;
    void *fast = fast_internal ? heap_caps_malloc(fast_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : nullptr;
    if (!fast) {                                       // not enough internal RAM: the whole fast heap goes to PSRAM
        fast_internal = false;
        fast_bytes = kFastSpiramBytes;
        fast = heap_caps_malloc(fast_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    size_t bulk_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    bulk_bytes = bulk_bytes > kBulkHeadroomBytes ? bulk_bytes - kBulkHeadroomBytes : 0;
    if (bulk_bytes > kBulkMaxBytes) bulk_bytes = kBulkMaxBytes;
    void *bulk = heap_caps_malloc(bulk_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    Serial.printf("[AUDIO] fast heap %u bytes (%s), bulk heap %u bytes (PSRAM)\n", (unsigned)fast_bytes, fast_internal ? "internal RAM" : "PSRAM", (unsigned)bulk_bytes);
    if (!fast || !bulk) { Serial.printf("[AUDIO] allocation failed: fast %p, bulk %p\n", fast, bulk); return; }
    if (engine_synth_init(fast, fast_bytes, bulk, bulk_bytes) != 0) { Serial.println("[AUDIO] engine_synth_init failed"); return; }

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 6;
    chan.dma_frame_num = kFrames;
    if (i2s_new_channel(&chan, &tx, nullptr) != ESP_OK) { Serial.println("[AUDIO] i2s_new_channel failed"); return; }
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(engine_synth_sample_rate())),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = PIN_I2S_MCLK >= 0 ? static_cast<gpio_num_t>(PIN_I2S_MCLK) : I2S_GPIO_UNUSED, .bclk = static_cast<gpio_num_t>(PIN_I2S_BCLK),
                     .ws = static_cast<gpio_num_t>(PIN_I2S_LRC), .dout = static_cast<gpio_num_t>(PIN_I2S_DOUT), .din = I2S_GPIO_UNUSED,
                     .invert_flags = {false, false, false}},
    };
    i2s_channel_init_std_mode(tx, &cfg);
    i2s_channel_enable(tx);
    Serial.println("[AUDIO] running");
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
