// The Ritual Machine's engine on the first prototype: its memory (modules and plans in internal RAM, delay lines and the reverb
// pre-delay in PSRAM), the function the board's audio output calls for sound (hal/hal_audio_out.h; the output itself, the I2S task on
// core 1, is the library's boards/hwv1/audio_out_esp32.cpp), and core/audio.h forwarded to the bridge (platform/engine/engine_synth.h).
// The UI runs in loop() on core 0 and talks to the engine only through its command queue.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "core/audio.h"
#include "hal/hal_audio_out.h"
#include "hal/hal_mem.h"
#include "platform/engine/engine_synth.h"
#include "engine/modules/fx_modules.h"
#include "engine/modules/mi_osc.h"
#include "engine/modules/osc_engines.h"
#include "engine/dsp/config.h"
#include "platform/esp32/bench_esp32.h"
#include "platform/esp32/samples_esp32.h"
#include "boards/hwv1/board_esp32.h"
#include <atomic>

namespace {
// Internal RAM kept free in the largest block for what is created after audio_init (tasks, DMA, display buffer). 40 KB made the fast heap fall
// into PSRAM on HWV1 (2026-10-06: 134 KB free in two blocks, the largest 90 KB -> 50 KB < kFastMinBytes); the other block (~44 KB) takes those:
// the boot log's "running" line shows the largest block untouched after them.
constexpr size_t kFastMarginBytes = 16 * 1024;
constexpr size_t kFastMaxBytes = 256 * 1024;       // no point in taking more than this
constexpr size_t kFastMinBytes = 64 * 1024;        // below this the internal block is not worth it: the fast heap goes to PSRAM
constexpr size_t kFastSpiramBytes = 400 * 1024;    // the fast heap when it has to live in PSRAM
constexpr size_t kBulkHeadroomBytes = 128 * 1024;  // PSRAM left for the rest of the firmware (sample cache, SD buffers, display)
constexpr size_t kBulkMaxBytes = 1600 * 1024;      // PSRAM: delay lines, the pre-delay, the spill of the fast heap

// The built-in loudspeaker's level (GENERAL "Spk"): the board applies it to the speaker's channel (board_speaker_level).
void apply_speaker(const rack_t *r) { board_speaker_level(synth_config_speaker_pct(&r->cfg)); }

#ifdef DEV_SERIAL_CMD
// Dev capture (serial "dump N"): the next N left-channel samples of the engine output, before the dev gain, for offline analysis on the PC.
int16_t *g_cap = nullptr;
std::atomic<int> g_cap_want{0}, g_cap_n{0};
void capture(const int16_t *stereo, int frames) {
    const int want = g_cap_want.load(std::memory_order_acquire);
    if (!want) return;
    int n = g_cap_n.load(std::memory_order_relaxed);
    for (int i = 0; i < frames && n < want; i++) g_cap[n++] = stereo[2 * i];
    g_cap_n.store(n, std::memory_order_release);
    if (n >= want) g_cap_want.store(0, std::memory_order_release);
}
#endif

// What the board's audio task calls for every buffer.
void render(int16_t *buf, int frames) {
    engine_synth_render(buf, frames);
#ifdef DEV_SERIAL_CMD
    capture(buf, frames);
#endif
}

#ifdef HWV1_DEBUG_AUDIO
// Once a second, from the audio task (hal_audio_out.h audio_out_set_report): the output's numbers, then the engine's.
void report(const audio_out_stats_t *s) {
    {
        {
            Serial.printf("[AUDIO] render avg %u us, worst %u us, budget %u us per %d frames, %u blocks over budget of %u, graph builds %u (last: %s)\n", (unsigned)s->avg_us,
                          (unsigned)s->worst_us, (unsigned)s->budget_us, (int)s->frames, (unsigned)s->late, (unsigned)s->blocks, engine_synth_build_count(), engine_synth_build_reason());
            Serial.printf("[AUDIO] DMA underruns %u in the last second\n", (unsigned)s->underruns);
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
                if (nblocks) {                                                              // inside the reverb (0..5) and the delay (8..11), per engine block
                    Serial.printf("[SEC] reverb: in %u, mod %u, left %u, right %u, taps %u, mix %u | delay: time %u, reads %u, lp+write %u, mix %u\n",
                                  (unsigned)(g_sec_prof[0] / nblocks), (unsigned)(g_sec_prof[1] / nblocks), (unsigned)(g_sec_prof[2] / nblocks), (unsigned)(g_sec_prof[3] / nblocks),
                                  (unsigned)(g_sec_prof[4] / nblocks), (unsigned)(g_sec_prof[5] / nblocks), (unsigned)(g_sec_prof[8] / nblocks), (unsigned)(g_sec_prof[9] / nblocks),
                                  (unsigned)(g_sec_prof[10] / nblocks), (unsigned)(g_sec_prof[11] / nblocks));
                    if (g_sec_prof[14])                                                     // SpectralFx (stereo, the FX rack): 12..14
                        Serial.printf("[SEC] spectral: stft + frame work %u, analysis %u, resynthesis %u\n", (unsigned)(g_sec_prof[14] / nblocks),
                                      (unsigned)(g_sec_prof[12] / nblocks), (unsigned)(g_sec_prof[13] / nblocks));
                    for (auto &v : g_sec_prof) v = 0;
                    static const char *const names[sc::OSCX_ENGINES] = {"karp", "modal", "fm2", "fold", "ssaw", "vowel", "add", "dust", "str"};
                    {
                        size_t fu = 0, fc = 0, fh = 0, bu = 0, sp = 0;
                        engine_synth_heap_stats(&fu, &fc, &fh, &bu, &sp);
                        Serial.printf("[HEAP] fast heap (internal RAM) %u of %u bytes used, high water %u; bulk heap (PSRAM) %u used, %u bytes of module data spilled there\n", (unsigned)fu, (unsigned)fc, (unsigned)fh, (unsigned)bu, (unsigned)sp);
                    }
                    char oline[160];
                    int ol = 0;
                    for (int e = 0; e < sc::OSCX_ENGINES; e++) {
                        if (sc::g_osc_prof[e]) ol += snprintf(oline + ol, sizeof oline - ol, " %s=%u", names[e], (unsigned)(sc::g_osc_prof[e] / nblocks));
                        sc::g_osc_prof[e] = 0;
                    }
                    if (ol) Serial.printf("[OSC] cycles per block, summed over voices:%s\n", oline);
                    char mline[200];
                    int ml = 0;
                    for (int m = 0; m < sc::MI_MODELS; m++) {                                  // the Mutable Instruments models (ADR-039)
                        if (sc::g_mi_prof[m] && ml < (int)sizeof mline - 24) ml += snprintf(mline + ml, sizeof mline - ml, " %s=%u", sc::mi_model_name(m), (unsigned)(sc::g_mi_prof[m] / nblocks));
                        sc::g_mi_prof[m] = 0;
                    }
                    if (ml) Serial.printf("[MI] cycles per block, summed over voices:%s\n", mline);
                }
            }
#endif
        }
    }
}
#endif
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
#ifdef HWV1_DEBUG_AUDIO
    audio_out_set_report(report);
#endif
    if (!audio_out_start(render, engine_synth_sample_rate())) { Serial.println("[AUDIO] the output did not start"); return; }
    samples_esp32_start();                              // TF card library + the sample loader (its own task on core 0)
}

#ifdef DEV_SERIAL_CMD
// Serial "dump N" (UI loop, core 0): waits for the audio task to capture N samples (at most 16384, PSRAM) and prints them, 32 per "[DUMP]" line.
extern "C" void audio_dev_dump(int n) {
    if (n < 1) n = 1;
    if (n > 16384) n = 16384;
    if (!g_cap) g_cap = static_cast<int16_t *>(heap_caps_malloc(16384 * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    if (!g_cap) { Serial.println("[DUMP] no memory"); return; }
    g_cap_n.store(0, std::memory_order_relaxed);
    g_cap_want.store(n, std::memory_order_release);
    const uint32_t t0 = millis();
    while (g_cap_want.load(std::memory_order_acquire) && millis() - t0 < 3000) delay(5);
    const int got = g_cap_n.load(std::memory_order_acquire);
    Serial.printf("[DUMP] begin %d samples at %d Hz\n", got, engine_synth_sample_rate());
    for (int i = 0; i < got; i += 32) {
        char line[300];
        int len = snprintf(line, sizeof line, "[DUMP] @%d", i);         // the index lets the reader detect a lost or mangled line
        for (int k = i; k < got && k < i + 32; k++) len += snprintf(line + len, sizeof line - len, " %d", g_cap[k]);
        while (Serial.availableForWrite() < len + 2) delay(1);
        Serial.println(line);
    }
    Serial.println("[DUMP] end");
}
#endif

extern "C" void audio_shutdown(void) {
    audio_out_stop();
    engine_synth_shutdown();
}

#ifdef HWV1_DEBUG_UI
uint32_t g_ui_params_n = 0, g_ui_params_max_us = 0;     // dev only: audio_set_params calls and the slowest, printed by main_esp32.cpp
#endif
extern "C" void audio_set_params(const rack_t *r, const synth_params_t *p) {
#ifdef HWV1_DEBUG_UI
    const uint32_t t0 = micros();
#endif
    apply_speaker(r);
    engine_synth_set_params(r, p);
#ifdef HWV1_DEBUG_UI
    const uint32_t dt = micros() - t0;
    g_ui_params_n++;
    if (dt > g_ui_params_max_us) g_ui_params_max_us = dt;
#endif
}
extern "C" void audio_build(const rack_t *r, const synth_params_t *p)      { apply_speaker(r); engine_synth_build(r, p); }
extern "C" int audio_sample_count(void)                                     { return engine_synth_sample_count(); }   // the files in use (samples_esp32.cpp reads each when asked)
extern "C" bool audio_sample_info(int i, audio_sample_info_t *out)         { return engine_synth_sample_info(i, out); }
extern "C" int audio_osc_preview(int m, float t, float mo, float h, int period, int16_t *out, int n) { return engine_synth_osc_preview(m, t, mo, h, period, out, n); }
extern "C" bool audio_sample_prepare(int i)                                { return samples_esp32_ready(i); }     // only cooked .smp files on the card: nothing to convert
extern "C" int audio_sample_find(const char *name)                         { return samples_esp32_find(name); }
extern "C" int audio_sample_use(const char *name)                          { return samples_esp32_use(name); }    // the I/O task reads the file's header
extern "C" int audio_sample_request(const char *name)                      { return samples_esp32_request(name); }
extern "C" int audio_dir_open(const char *dir)                             { return samples_esp32_dir_open(dir); }
extern "C" int audio_dir_read(int first, audio_dir_entry_t *out, int max)  { return samples_esp32_dir_read(first, out, max); }
extern "C" void audio_dir_close(void)                                      { samples_esp32_dir_close(); }
extern "C" sd_state_t audio_sd_state(void)                                  { return samples_esp32_sd_state(); }
extern "C" uint32_t audio_sd_read_us(void)                                  { return samples_esp32_sd_read_us(); }
extern "C" uint32_t audio_sd_generation(void)                               { return samples_esp32_sd_generation(); }
extern "C" void audio_seq_set(const seq_t *seq)                            { engine_synth_seq_set(seq); }
extern "C" int  audio_seq_pos(void)                                        { return engine_synth_seq_pos(); }
extern "C" bool audio_seq_where(int *step, int *frac, uint32_t *count)     { return engine_synth_seq_where(step, frac, count) != 0; }
extern "C" bool audio_seq_queue(const seq_pattern_t *p, int id)            { return engine_synth_seq_queue(p, id) != 0; }
extern "C" int  audio_seq_taken(void)                                      { return engine_synth_seq_taken(); }
extern "C" void audio_pseq_set(const pseq_pattern_t *p, bool run)          { engine_synth_pseq_set(p, run); }
extern "C" int  audio_pseq_pos(void)                                       { return engine_synth_pseq_pos(); }
extern "C" bool audio_pseq_where(int *step, int *frac, uint32_t *count)    { return engine_synth_pseq_where(step, frac, count) != 0; }
extern "C" bool audio_pseq_queue(const pseq_pattern_t *p, int id)          { return engine_synth_pseq_queue(p, id) != 0; }
extern "C" int  audio_pseq_taken(void)                                     { return engine_synth_pseq_taken(); }
extern "C" void audio_set_clock(int bpm, int running)                      { engine_synth_set_clock(bpm, running); }
extern "C" void audio_set_metronome(bool on)                                { engine_synth_set_metronome(on); }
extern "C" bool audio_transport(int *bar, int *beat, int *frac)             { return engine_synth_transport(bar, beat, frac) != 0; }
extern "C" void audio_note_on(int midi_note)                               { engine_synth_note_on(midi_note); }
extern "C" void audio_note_off(int midi_note)                              { engine_synth_note_off(midi_note); }
extern "C" void audio_pads_build(const pads_t *p, const rack_t *r)          { apply_speaker(r); engine_synth_pads_build(p, r); }
extern "C" void audio_pad_set(int pad, const pad_t *p)                      { engine_synth_pad_set(pad, p); }
extern "C" void audio_pads_fx(const pads_t *p, const rack_t *r)             { engine_synth_pads_fx(p, r); }
extern "C" bool audio_capture_start(const char *n, int th, int bar, uint32_t lim) { return engine_synth_capture_start(n, th, bar, lim); }
extern "C" void audio_capture_stop(void)                                    { engine_synth_capture_stop(); }
extern "C" int  audio_capture_poll(uint32_t *f, uint32_t *d)                { return (int)engine_synth_capture_poll(f, d); }
extern "C" void audio_capture_done(void)                                    { engine_synth_capture_done(); }
extern "C" int  audio_rate(void)                                            { return engine_synth_sample_rate(); }
extern "C" void audio_pad_push(int pad)                                     { engine_synth_pad_push(pad); }
extern "C" void audio_pad_release(int pad)                                  { engine_synth_pad_release(pad); }
extern "C" void audio_pads_stop(void)                                       { engine_synth_pads_stop(); }
extern "C" uint64_t audio_pads_playing(void)                                { return engine_synth_pads_playing(); }
extern "C" int32_t audio_pad_position(int pad)                              { return engine_synth_pad_position(pad); }
extern "C" int audio_sample_onsets(int i, uint32_t *f, uint16_t *s, int max) { return engine_synth_sample_onsets(i, f, s, max); }
extern "C" int64_t audio_sample_zero(int i, uint32_t frame, int dir)        { return engine_synth_sample_zero(i, frame, dir); }
extern "C" bool audio_render_start(const char *s, const char *d, int op, int a, int a2) { return engine_synth_render_start(s, d, op, a, a2); }
extern "C" int audio_render_poll(int *percent)                              { return static_cast<int>(engine_synth_render_poll(percent)); }
extern "C" void audio_render_done(void)                                     { engine_synth_render_done(); }
extern "C" void audio_sample_hide(int index)                                { samples_esp32_hide(index); }
extern "C" uint32_t audio_millis(void)                                     { return engine_synth_millis(); }
extern "C" void audio_update(void)                                         { engine_synth_housekeeping(); }     // frees an old graph once the audio task runs the new one
#endif // ARDUINO_ARCH_ESP32