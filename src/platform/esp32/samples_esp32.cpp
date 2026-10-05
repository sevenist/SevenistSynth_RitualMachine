// Sample library of the ESP32 build: the .smp files in SD_SAMPLE_DIR of the TF card (cook them on the PC with tools/wav2smp.py; there is no
// .wav / .mp3 import on the board). One task on core 0 at low priority does everything that touches the card: mount, scan, and the loader
// (engine_synth_io_pump, ADR-022), so the audio task never waits for it. It also watches the card: inserted, too slow (then it is left alone and
// the synth runs as if there were none), removed or swapped. Like the desktop's samples/ folder, new files are appended to the
// catalog after the known ones, so the index a rack stores stays valid.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <ctype.h>
#include <dirent.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "engine/sampler/smp_format.h"
#include "platform/engine/engine_synth.h"
#include "platform/engine/sample_catalog.h"
#include "platform/esp32/sd_card.h"
#include "platform/esp32/storage_sd.h"
#include "platform/esp32/samples_esp32.h"

namespace {
using namespace sc;

constexpr int kStackBytes = 8192;                 // long file names (FATFS LFN on the stack) and the directory walk
// Task priorities (FreeRTOS, higher = more urgent): audio 23 (core 1) > input scan 8 > this loader > the Arduino loop that draws the UI (1), both on core 0.
// The loader has deadlines (a stream that runs dry is audible), a frame of the UI has none, so it ranks above the UI. It does not hog the core:
// the card transfers wait on DMA, and after kBurst reads in a row it sleeps a tick.
#ifndef HWV1_SD_IO_PRIO
#define HWV1_SD_IO_PRIO 5
#endif
constexpr UBaseType_t kPriority = HWV1_SD_IO_PRIO;
constexpr int kBurst = 8;
SdStorage g_storage;
audio_sample_info_t g_cat[AUDIO_SAMPLES_MAX];
volatile int g_n = 0;
bool g_attached = false;
TaskHandle_t g_task = nullptr;
uint8_t g_blk[kSmpBlockBytes];                    // one card block: the header, then a block per bucket of the amplitude overview

uint32_t g_scan_reads = 0, g_scan_worst_us = 0;     // card reads of the scan (timed: a slow card shows up in the boot log)

bool read_at(int fd, uint32_t offset, void *dst, uint32_t bytes) {
    const int64_t t0 = esp_timer_get_time();
    const bool ok = lseek(fd, static_cast<off_t>(offset), SEEK_SET) == static_cast<off_t>(offset) && read(fd, dst, bytes) == static_cast<ssize_t>(bytes);
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    g_scan_reads++;
    if (us > g_scan_worst_us) g_scan_worst_us = us;
    return ok;
}

bool known(const char *stem) {
    for (int i = 0; i < g_n; i++) if (!strcmp(g_cat[i].name, stem)) return true;
    return false;
}

// Header + amplitude overview of one file into entry `i`. The overview comes from the header; only an older file without it costs 64 reads.
bool fill_entry(int i, const char *stem, const char *path) {
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) return false;
    SmpHeader h;
    audio_sample_info_t &in = g_cat[i];
    bool ok = read_at(fd, 0, g_blk, kSmpBlockBytes) && smp_parse_header(g_blk, kSmpBlockBytes, h);
    if (ok) {
        memset(&in, 0, sizeof in);
        snprintf(in.name, sizeof in.name, "%s", stem);
        in.frames = h.frames; in.rate = h.sample_rate;
        in.root = static_cast<uint8_t>(h.root_note > 127 ? 127 : h.root_note);
        in.slices = h.slice_count; in.loop_mode = h.loop_mode; in.loop_start = h.loop_start; in.loop_end = h.loop_end;
        for (int k = 0; k < h.slice_count; k++) in.slice[k] = h.slice[k];
        static_assert(AUDIO_PEAKS == kSmpPeaks, "the catalog overview is the one stored in the .smp header");
        if (h.has_peaks()) memcpy(in.peaks, h.peaks, sizeof in.peaks);     // stored by the converter: no more reads
        const uint32_t blocks = h.blocks();
        for (int b = 0; b < AUDIO_PEAKS && ok && !h.has_peaks(); b++) {    // a file cooked before that: 64 reads, one block in the middle of each bucket
            const uint32_t blk = static_cast<uint32_t>((static_cast<uint64_t>(blocks) * (2u * b + 1)) / (2u * AUDIO_PEAKS));
            const uint32_t frames_here = blk + 1 == blocks ? h.frames - blk * kSmpBlockFrames : kSmpBlockFrames;
            ok = read_at(fd, (blk + 1) * kSmpBlockBytes, g_blk, frames_here * 2);
            if ((b & 3) == 3) vTaskDelay(1);                                // the SD driver busy-waits on the card: let the idle task run (watchdog)
            int peak = 0;
            for (uint32_t f = 0; ok && f < frames_here; f++) {
                int v = static_cast<int16_t>(get16(&g_blk[2 * f]));
                if (v < 0) v = -v;
                if (v > peak) peak = v;
            }
            in.peaks[b] = static_cast<uint8_t>(peak >> 7 > 255 ? 255 : peak >> 7);
        }
    }
    ::close(fd);
    return ok;
}

void scan() {
    DIR *d = opendir(SD_SAMPLE_DIR);
    if (!d) { Serial.println("[SD] no " SD_SAMPLE_DIR " folder"); return; }
    int added = 0;
    while (const dirent *e = readdir(d)) {
        const size_t len = strlen(e->d_name);
        if (len < 5 || len - 4 > sizeof g_cat[0].name - 1 || strcasecmp(e->d_name + len - 4, ".smp") != 0) continue;   // *.smp with a stem that fits the catalog name
        char stem[sizeof g_cat[0].name];
        memcpy(stem, e->d_name, len - 4);
        stem[len - 4] = 0;
        if (known(stem)) continue;
        if (g_n >= AUDIO_SAMPLES_MAX) { Serial.printf("[SD] library full (%d files), skipping %s\n", AUDIO_SAMPLES_MAX, e->d_name); break; }
        char path[sizeof(e->d_name) + sizeof SD_SAMPLE_DIR + 2];
        snprintf(path, sizeof path, SD_SAMPLE_DIR "/%s", e->d_name);
        g_scan_reads = g_scan_worst_us = 0;
        const uint32_t t0 = millis();
        const bool ok = fill_entry(g_n, stem, path);
        Serial.printf("[SD] scan %s: %u ms, %u reads, slowest read %u us%s\n", e->d_name, (unsigned)(millis() - t0), (unsigned)g_scan_reads, (unsigned)g_scan_worst_us, ok ? "" : " (not a valid .smp)");
        if (!ok) continue;
        g_n = g_n + 1;
        added++;
        vTaskDelay(1);
        engine_synth_catalog_set(g_cat, g_n);       // listed as soon as it is read: the screen fills in while the rest is scanned
    }
    closedir(d);
    Serial.printf("[SD] %d new sample(s), %d in the library\n", added, (int)g_n);
}

#ifdef HWV1_SD_BENCH
// File reads through VFS / FATFS at the sizes the loader uses, next to sd_card_bench()'s raw reads.
void file_bench() {
    const int fd = ::open(SD_SAMPLE_DIR "/kick.smp", O_RDONLY);
    if (fd < 0) { Serial.println("[SD] bench: no kick.smp"); return; }
    static uint8_t buf[8192];
    for (uint32_t size : {512u, 4096u}) {
        lseek(fd, 4096, SEEK_SET);
        uint32_t worst = 0;
        const int64_t t0 = esp_timer_get_time();
        int n = 0;
        for (; n < 8; n++) {
            const int64_t a = esp_timer_get_time();
            if (read(fd, buf, size) != static_cast<ssize_t>(size)) break;
            const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - a);
            if (us > worst) worst = us;
        }
        Serial.printf("[SD] bench file reads of %u bytes: avg %u us, worst %u us (%d done)\n", (unsigned)size, n ? (unsigned)((esp_timer_get_time() - t0) / n) : 0u, (unsigned)worst, n);
    }
    ::close(fd);
}
#endif

// ---- the card's life: inserted, too slow, removed ----
// HWV1 has no card-detect pin, so the card is polled: a mount attempt once a second while there is none, a status command once a second while there is one.
constexpr uint32_t kSlowReadUs = 15000;           // a single sector read above this: the card cannot stream samples (healthy cards: 300-2000 us)
constexpr uint32_t kPollMs = 1000, kPollSlowMs = 2000;
volatile sd_state_t g_state = SD_NONE;
volatile uint32_t g_slow_us = 0, g_gen = 0;
uint32_t g_catalog_card = 0;                      // serial number of the card the catalog was read from (0 = none)

void card_gone() {
    g_storage.close_all();
    sd_card_unmount();
    g_state = SD_NONE;
    g_gen = g_gen + 1;
    Serial.println("[SD] card removed");
}

void card_in() {
#ifdef HWV1_SD_BENCH
    sd_card_bench();
    file_bench();
#endif
    const uint32_t id = sd_card_id();
    const uint32_t us = sd_card_probe_us();
    if (us == 0) {                                // it mounted but cannot be read: treat it as no card, try again at the next poll
        Serial.println("[SD] the card does not read: ignored");
        sd_card_unmount();
        return;
    }
    if (id != g_catalog_card && g_n > 0) {        // another card: its files are not the ones the catalog lists
        engine_synth_catalog_reset();
        g_n = 0;
        g_catalog_card = 0;
    }
    if (us > kSlowReadUs) {
        g_slow_us = us;
        g_state = SD_SLOW;                        // stays mounted so its removal is seen, but nothing reads it: the synth runs as if there were no card
        g_gen = g_gen + 1;
        Serial.printf("[SD] card too slow: a sector read takes %u us (limit %u): not used\n", (unsigned)us, (unsigned)kSlowReadUs);
        return;
    }
    g_catalog_card = id;
    if (!g_attached) g_attached = engine_synth_attach_storage(&g_storage);
    g_state = SD_OK;
    Serial.printf("[SD] card ready: a sector read takes %u us\n", (unsigned)us);
    scan();
    g_gen = g_gen + 1;                            // the application looks the samples of its rack up again
}

void poll_card() {
    if (!sd_card_mounted()) {
        if (sd_card_mount()) { Serial.println("[SD] card inserted"); card_in(); }
    } else if (!sd_card_alive()) {
        card_gone();
    }
}

#ifdef HWV1_DEBUG_AUDIO
// Every 2 s, when anything happened: what the card delivered and whether a playhead ran dry ("underruns" is what you hear as a dropout).
void report() {
    static uint32_t t_last = 0, reads0 = 0, under0 = 0, blocks0 = 0;
    static uint64_t bytes0 = 0, us0 = 0;
    const uint32_t now = millis();
    if (now - t_last < 2000) return;
    const SdStorage::Stats s = g_storage.stats();
    uint32_t under = 0, blocks = 0;
    engine_synth_sampler_stats(&under, &blocks);
    if (s.reads != reads0 || under != under0) {
        const uint32_t dr = s.reads - reads0;
        const uint32_t dt = now - t_last;
        Serial.printf("[SD] %u reads in %u ms: %u KB/s, avg %u us, worst ever %u us; opens %u, seeks %u, errors %u | [SMP] stream blocks +%u, underruns %u (+%u)\n",
                      (unsigned)dr, (unsigned)dt, (unsigned)((s.bytes - bytes0) * 1000 / 1024 / (dt ? dt : 1)), dr ? (unsigned)((s.read_us - us0) / dr) : 0u, (unsigned)s.max_us,
                      (unsigned)s.opens, (unsigned)s.seeks, (unsigned)s.errors, (unsigned)(blocks - blocks0), (unsigned)under, (unsigned)(under - under0));
    }
    t_last = now; reads0 = s.reads; bytes0 = s.bytes; us0 = s.read_us; under0 = under; blocks0 = blocks;
}
#endif

void io_task(void *) {
    uint32_t t_poll = 0;
    bool first = true;
    int burst = 0;
    for (;;) {
        const uint32_t now = millis();
        if (first || now - t_poll >= (g_state == SD_SLOW ? kPollSlowMs : kPollMs)) { first = false; t_poll = now; poll_card(); }
        if (ulTaskNotifyTake(pdTRUE, 0) && g_state == SD_OK) scan();      // the Scan row of the sample list
        if (g_state == SD_OK) {
            const bool more = engine_synth_io_pump();   // reads still in flight: go on at once, a stream may be about to run dry
            if (more && ++burst < kBurst) continue;
            burst = 0;
        }
#ifdef HWV1_DEBUG_AUDIO
        report();
#endif
        vTaskDelay(g_state == SD_OK ? 2 : pdMS_TO_TICKS(50));   // at least one whole tick: the card driver busy-waits, and the idle task of this core must get time (task watchdog)
    }
}
}  // namespace

void samples_esp32_start() {
    if (!g_task) xTaskCreatePinnedToCore(io_task, "sd_io", kStackBytes, nullptr, kPriority, &g_task, 0);
}

int samples_esp32_rescan() {
    if (g_task) xTaskNotifyGive(g_task);
    return g_n;
}

bool samples_esp32_ready(int index) { return index >= 0 && index < g_n && !g_cat[index].pending; }
sd_state_t samples_esp32_sd_state() { return g_state; }
uint32_t samples_esp32_sd_read_us() { return g_slow_us; }
uint32_t samples_esp32_sd_generation() { return g_gen; }
#endif  // ARDUINO_ARCH_ESP32
