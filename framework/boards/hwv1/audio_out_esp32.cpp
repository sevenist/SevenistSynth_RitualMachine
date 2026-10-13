// First prototype, the audio output (hal/hal_audio_out.h): a task of its own on core 1 asks the app for sound and streams it to the
// I2S DAC. Pins are in board_pins.h. Whatever uses the controls and the screen runs on core 0 and never waits for this task.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <atomic>
#include "hal/hal_audio_out.h"
#include "hal/hal_mem.h"
#include "boards/hwv1/board_esp32.h"
#include "boards/hwv1/board_pins.h"

namespace {
// Dev output attenuation, in percent of full scale (build flag DEV_OUTPUT_GAIN_PCT, 0..100): the first prototype's output stage has a very low
// impedance and is loud on headphones. It is the last step before the DAC, so it applies to the app's sound and to the test tone alike.
#ifndef DEV_OUTPUT_GAIN_PCT
#define DEV_OUTPUT_GAIN_PCT 100
#endif
static_assert(DEV_OUTPUT_GAIN_PCT >= 0 && DEV_OUTPUT_GAIN_PCT <= 100, "DEV_OUTPUT_GAIN_PCT is a percentage, 0..100");
constexpr int32_t kOutGainQ15 = DEV_OUTPUT_GAIN_PCT * 32768 / 100;
constexpr int kFrames = 64;                        // frames per I2S write (an engine renders its own block size inside)
#ifdef HWV1_DEBUG_AUDIO
volatile uint32_t g_dma_underruns = 0;              // I2S "send queue overflow" events: the DMA re-sent a buffer the task had not refilled
bool IRAM_ATTR dma_underrun_cb(i2s_chan_handle_t, i2s_event_data_t *, void *) { g_dma_underruns = g_dma_underruns + 1; return false; }
audio_report_fn g_report = nullptr;
#endif

i2s_chan_handle_t tx;
TaskHandle_t audio_task;
audio_render_fn g_render = nullptr;
int g_rate = 0;
// The built-in loudspeaker: the MAX98357A plays one DAC channel, the left one, so its level scales that channel. Off is gain 0,
// plus an attempt to shut the amplifier down through SD_MODE (board_speaker_on).
constexpr int kSpeakerCh = 0;                      // 0 = left, 1 = right
std::atomic<int32_t> g_spk_gain{32768};            // Q15, written by the UI, read by the audio task
int g_spk_pct = -1;                                // UI side: the last level applied
// The output is closed while the machine starts (user, 2026-10-11: no sound before the loading screen has ended): the app renders,
// zeros go to the DAC and the amplifier. audio_esp32_open lets it through, faded in over one buffer.
std::atomic<bool> g_out_open{false};

void audio_task_main(void *) {
    static int16_t buf[kFrames * 2];
#ifdef HWV1_DEBUG_AUDIO
    uint32_t t_report = millis(), blocks = 0, worst = 0, total = 0, late = 0;
    const uint32_t budget_us = (uint32_t)(1000000ull * kFrames / g_rate);
#endif
    for (;;) {
#ifdef HWV1_DEBUG_AUDIO
        const uint32_t t0 = micros();
#endif
#ifdef HWV1_TEST_TONE
        for (int i = 0; i < kFrames; i++) {                  // 440 Hz sine, bypasses the app: tests the I2S / DAC / amplifier path alone
            static float ph = 0;
            const int16_t v = (int16_t)(sinf(ph) * 8000);
            ph += 6.2831853f * 440.0f / (float)g_rate;
            if (ph > 6.2831853f) ph -= 6.2831853f;
            buf[2 * i] = buf[2 * i + 1] = v;
        }
#else
        g_render(buf, kFrames);
#endif
#ifdef HWV1_DEBUG_AUDIO
        const uint32_t dt = micros() - t0;
        total += dt; blocks++;
        if (dt > worst) worst = dt;
        if (dt > budget_us) late++;
#endif
        if (kOutGainQ15 != 32768)
            for (int i = 0; i < kFrames * 2; i++) buf[i] = (int16_t)(((int32_t)buf[i] * kOutGainQ15) >> 15);
        {   // the speaker channel: ramped over the buffer from the previous gain, so a 5 % step does not click
            static int32_t g_prev = 32768;
            const int32_t g_new = g_spk_gain.load(std::memory_order_relaxed);
            if (g_new != 32768 || g_prev != 32768)
                for (int i = 0; i < kFrames; i++) {
                    const int32_t g = g_prev + (g_new - g_prev) * (i + 1) / kFrames;
                    buf[2 * i + kSpeakerCh] = (int16_t)(((int32_t)buf[2 * i + kSpeakerCh] * g) >> 15);
                }
            g_prev = g_new;
        }
        {   // closed until the start is over
            static int32_t m_prev = 0;
            const int32_t m_new = g_out_open.load(std::memory_order_relaxed) ? 32768 : 0;
            if (m_new != 32768 || m_prev != 32768)
                for (int i = 0; i < kFrames; i++) {
                    const int32_t g = m_prev + (m_new - m_prev) * (i + 1) / kFrames;
                    buf[2 * i] = (int16_t)(((int32_t)buf[2 * i] * g) >> 15);
                    buf[2 * i + 1] = (int16_t)(((int32_t)buf[2 * i + 1] * g) >> 15);
                }
            m_prev = m_new;
        }
        size_t written = 0;
        i2s_channel_write(tx, buf, sizeof buf, &written, portMAX_DELAY);     // blocks until the DMA ring has room: this paces the task
#ifdef HWV1_DEBUG_AUDIO
        if (millis() - t_report >= 1000 && Serial.availableForWrite() > 160) {      // skip the report rather than block when the port is not being read
            if (g_report) {
                const audio_out_stats_t s = {blocks, late, total / blocks, worst, budget_us, (uint32_t)kFrames, g_dma_underruns};
                g_report(&s);
            }
            g_dma_underruns = 0;
            t_report = millis(); blocks = worst = total = late = 0;
        }
#endif
    }
}
}  // namespace

void audio_esp32_open(void) { g_out_open.store(true, std::memory_order_relaxed); }

// The loudspeaker's level, 0..100 (0 = off). Off is a zero gain on the speaker channel: pulling SD_MODE low alone did not silence the
// speaker on the prototype (2026-10-05), so the pin is not proven to reach the amplifier. The headphones did not follow the speaker
// level in the same test.
void board_speaker_level(int pct) {
    if (pct == g_spk_pct) return;
    if ((pct == 0) != (g_spk_pct == 0) || g_spk_pct < 0) board_speaker_on(pct > 0);
    g_spk_gain.store(pct * 32768 / 100, std::memory_order_relaxed);
    Serial.printf("[SPK] %d %%, SD pin (GPIO %d) reads %d\n", pct, PIN_SPK_SD, digitalRead(PIN_SPK_SD));
    g_spk_pct = pct;
}

extern "C" void audio_out_set_report(audio_report_fn report) {
#ifdef HWV1_DEBUG_AUDIO
    g_report = report;
#else
    (void)report;
#endif
}

extern "C" bool audio_out_start(audio_render_fn render, int sample_rate) {
    if (!render || sample_rate <= 0 || audio_task) return false;
    g_render = render;
    g_rate = sample_rate;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 6;
    chan.dma_frame_num = kFrames;
    if (i2s_new_channel(&chan, &tx, nullptr) != ESP_OK) { Serial.println("[AUDIO] i2s_new_channel failed"); return false; }
#ifdef HWV1_DEBUG_AUDIO
    {   // count the real underruns: the DMA ring ran empty and re-sent old data (a render over its budget alone is not one: the ring absorbs it)
        i2s_event_callbacks_t cbs = {};
        cbs.on_send_q_ovf = dma_underrun_cb;
        i2s_channel_register_event_callback(tx, &cbs, nullptr);
    }
#endif
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(sample_rate)),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = PIN_I2S_MCLK >= 0 ? static_cast<gpio_num_t>(PIN_I2S_MCLK) : I2S_GPIO_UNUSED, .bclk = static_cast<gpio_num_t>(PIN_I2S_BCLK),
                     .ws = static_cast<gpio_num_t>(PIN_I2S_LRC), .dout = static_cast<gpio_num_t>(PIN_I2S_DOUT), .din = I2S_GPIO_UNUSED,
                     .invert_flags = {false, false, false}},
    };
    i2s_channel_init_std_mode(tx, &cfg);
    i2s_channel_enable(tx);
    Serial.printf("[AUDIO] running; internal free %u (largest block %u)\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    // Core 1 belongs to the audio task alone; a missed buffer is a click, so it runs above everything else.
    return xTaskCreatePinnedToCore(audio_task_main, "audio", 8192, nullptr, configMAX_PRIORITIES - 2, &audio_task, 1) == pdPASS;
}

extern "C" void audio_out_stop(void) {
    if (audio_task) { vTaskDelete(audio_task); audio_task = nullptr; }
    if (tx) { i2s_channel_disable(tx); i2s_del_channel(tx); tx = nullptr; }
}

// Large application data (hal/hal_mem.h): PSRAM when it has room left (an engine's bulk heap may have taken most of it), else the internal heap.
extern "C" void *mem_bulk_alloc(size_t bytes) {
    void *p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, bytes);
}
#endif // ARDUINO_ARCH_ESP32 && HWV1
