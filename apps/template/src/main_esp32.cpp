#if defined(ARDUINO_ARCH_ESP32)
// Firmware for the first prototype (HWV1). The engine renders on its own task on core 1 and streams to the DAC over I2S; loop()
// runs on core 0 (build flag ARDUINO_RUNNING_CORE=0), reads the key matrix and sends notes.
//
//   the four note rows    notes, as on the Ritual Machine: bottom left = C3 (48), +1 per key to the right, +8 per row up
//   function keys 1 / 2   octave down / up
//   function keys 3 / 4   filter cutoff down / up
//
// Run on the board (2026-10-13). The I2S, DAC, power and key code follow the Ritual Machine's drivers.
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include "board_pins.h"
#include "kbd_tca8418.h"
#include "synth.h"

namespace {
constexpr int kFrames = 64;                         // frames per I2S write (the engine renders its own block size inside)
// The prototype's output stage is loud on headphones: keep the level low until you know your setup (100 = full scale).
constexpr int kOutGainPct = 25;
constexpr int32_t kOutGainQ15 = kOutGainPct * 32768 / 100;
constexpr size_t kFastBytes = 64 * 1024;            // internal RAM for the engine: module state and the plan's buffers
constexpr size_t kBulkBytes = 256 * 1024;           // PSRAM, when the board has it: long delay lines, samples (unused by this small synth)

i2s_chan_handle_t tx;
int octave = 0;                                     // added to every key, in octaves
uint8_t held[HW_KBD_ROWS][HW_KBD_COLS];             // note + 1 a key started, so its release stops the same note after an octave change

void audio_task(void *) {
    static int16_t buf[kFrames * 2];
    for (;;) {
        synth_render(buf, kFrames);
        for (int i = 0; i < kFrames * 2; i++) buf[i] = static_cast<int16_t>((static_cast<int32_t>(buf[i]) * kOutGainQ15) >> 15);
        size_t written = 0;
        i2s_channel_write(tx, buf, sizeof buf, &written, portMAX_DELAY);    // blocks until the DMA ring has room: this paces the task
    }
}

bool audio_start() {
    void *fast = heap_caps_malloc(kFastBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *bulk = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 2 * kBulkBytes ? heap_caps_malloc(kBulkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    if (!fast || !synth_init(fast, kFastBytes, bulk, bulk ? kBulkBytes : 0)) { Serial.println("[AUDIO] the synth did not start"); return false; }

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 6;
    chan.dma_frame_num = kFrames;
    if (i2s_new_channel(&chan, &tx, nullptr) != ESP_OK) { Serial.println("[AUDIO] i2s_new_channel failed"); return false; }
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(synth_sample_rate())),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = PIN_I2S_MCLK >= 0 ? static_cast<gpio_num_t>(PIN_I2S_MCLK) : I2S_GPIO_UNUSED, .bclk = static_cast<gpio_num_t>(PIN_I2S_BCLK),
                     .ws = static_cast<gpio_num_t>(PIN_I2S_LRC), .dout = static_cast<gpio_num_t>(PIN_I2S_DOUT), .din = I2S_GPIO_UNUSED,
                     .invert_flags = {false, false, false}},
    };
    i2s_channel_init_std_mode(tx, &cfg);
    i2s_channel_enable(tx);
    // Core 1 belongs to the audio task alone; a missed block is a click, so it runs above everything else.
    xTaskCreatePinnedToCore(audio_task, "audio", 8192, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);
    return true;
}

void key_event(const kbd_event_t &e) {
    if (e.row == 4) {                               // the function keys: columns 7, 6, 5, 4 from the left
        if (!e.pressed) return;
        if (e.col == 7 && octave > -3) octave--;
        else if (e.col == 6 && octave < 3) octave++;
        else if (e.col == 5) synth_set_cutoff(synth_cutoff() - 4);
        else if (e.col == 4) synth_set_cutoff(synth_cutoff() + 4);
        return;
    }
    uint8_t &h = held[e.row][e.col];
    if (e.pressed) {
        const int note = 48 + 12 * octave + 8 * e.row + (HW_KBD_COLS - 1 - e.col);
        if (note < 0 || note > 127) return;
        synth_note_on(note);
        h = static_cast<uint8_t>(note + 1);
    } else if (h) {
        synth_note_off(h - 1);
        h = 0;
    }
}

// The power button. Whatever PWR_SENSE read at the start is "as it was"; once it has changed for 50 ms the supply is let go: on the
// battery the board stops when the button is up, on the USB cable it keeps running. (Judged by the change, not by the level, so it
// does not depend on which level means "down".)
void power_poll() {
    static int level = digitalRead(PIN_PWR_SENSE);
    static uint32_t changed_at = 0;
    const int now = digitalRead(PIN_PWR_SENSE);
    if (now == level) { changed_at = 0; return; }
    if (!changed_at) { changed_at = millis(); return; }
    if (millis() - changed_at < 50) return;
    level = now;
    changed_at = 0;
    digitalWrite(PIN_PWR_ON_EN, LOW);
}
}  // namespace

void setup() {
    pinMode(PIN_PWR_ON_EN, OUTPUT);                 // before anything slow: the board keeps itself powered
    digitalWrite(PIN_PWR_ON_EN, HIGH);
    pinMode(PIN_PWR_SENSE, INPUT);
    pinMode(PIN_I2S_XSMT, OUTPUT);
    digitalWrite(PIN_I2S_XSMT, LOW);                // the DAC stays muted while everything starts
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);                       // never wait for a host that does not read the port
    power_poll();                                   // takes the button's level at the start
    kbd_init();
    if (audio_start()) digitalWrite(PIN_I2S_XSMT, HIGH);
    Serial.printf("[TEMPLATE] running at %d Hz\n", synth_sample_rate());
}

void loop() {
    kbd_event_t ev[10];
    const int n = kbd_read_events(ev, 10);
    for (int i = 0; i < n; i++) key_event(ev[i]);
    synth_update();
    power_poll();
    delay(5);                                       // the key controller debounces by itself; 5 ms is fast enough for notes
}
#endif  // ARDUINO_ARCH_ESP32
