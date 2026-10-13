// The Ritual Machine's sample library on the first prototype: the .smp files below SD_SAMPLE_DIR of the TF card, in folders of any depth
// (cook them on the PC with tools/wav2smp.py; there is no .wav / .mp3 import on the board). Everything that touches the card runs on
// the library's card task (boards/hwv1/card_task.h), which mounts and watches the card and serves the small files of hal_storage.h;
// this file is the app's own card work, run there through the task's hooks: a sample's header when a file is asked for by name
// (nothing is scanned at start: the engine's catalog holds the files in use), a folder's list for the browser, the loader of the
// streams (engine_synth_io_pump, ADR-022), a recording's and a sample job's chunks. The audio task never waits for the card.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <ctype.h>
#include <dirent.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "engine/sampler/smp_format.h"
#include "core/card_layout.h"
#include "platform/engine/engine_synth.h"
#include "platform/engine/sample_catalog.h"
#include "boards/hwv1/card_task.h"
#include "boards/hwv1/sd_card.h"
#include "boards/hwv1/storage_sd.h"
#include "platform/esp32/samples_esp32.h"

#define SD_SAMPLE_DIR SD_MOUNT_POINT "/" STORAGE_DIR_SAMPLES     // the *.smp files of the sample library

namespace {
using namespace sc;

SdStorage &g_storage = card_task_storage();      // the streaming device on the card (the library's; this app's samples go through it)
bool g_attached = false;
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

// Header + amplitude overview of the sample `name` (its path below SD_SAMPLE_DIR, no extension) into `in`. The overview comes from the
// header; only an older file without it costs 64 reads. The file stays open for the loader (SdStorage::keep_spare): the sample is
// usually loaded next, and opening it again would walk its folder a second time.
bool read_info(const char *name, audio_sample_info_t &in) {
    char path[sizeof SD_SAMPLE_DIR + AUDIO_NAME_MAX + 8];
    snprintf(path, sizeof path, SD_SAMPLE_DIR "/%s.smp", name);
    int fd = g_storage.take_spare(path);
    if (fd < 0) fd = ::open(path, O_RDONLY);
    if (fd < 0) return false;
    SmpHeader h;
    g_scan_reads = g_scan_worst_us = 0;
    const uint32_t t0 = millis();
    bool ok = read_at(fd, 0, g_blk, kSmpBlockBytes) && smp_parse_header(g_blk, kSmpBlockBytes, h);
    if (ok) {
        memset(&in, 0, sizeof in);
        snprintf(in.name, sizeof in.name, "%s", name);
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
    if (ok) g_storage.keep_spare(path, fd); else ::close(fd);
#ifndef HWV1_DEBUG_AUDIO
    if (!ok || millis() - t0 >= 100)                  // (always with the debug build; else only a failed or a slow read)
#endif
    Serial.printf("[SD] read %s: %u ms, %u reads, slowest read %u us%s\n", path, (unsigned)(millis() - t0), (unsigned)g_scan_reads, (unsigned)g_scan_worst_us, ok ? "" : " (not a valid .smp)");
    return ok;
}

// ---- the library jobs (core/audio.h audio_sample_use, audio_dir_open): posted by the UI task, run here, like the settings files ----
// state: 0 = free, 1 = posted (this task owns the job), 2 = done (the poster owns it); see FileJob below.
struct LibJob {
    char name[AUDIO_NAME_MAX];                    // the sample, or the folder to list ("" = SD_SAMPLE_DIR itself)
    bool list;
    bool asked;                                   // posted by samples_esp32_request: nobody waits, its answer is taken at a later call
    int  result;                                  // a sample: 1 = `info` holds it, 0 = no such sample; a folder: its entries, -1 = no such folder
    audio_sample_info_t info;
    volatile int state;
};
LibJob g_lib;
// The folder the browser shows: its sub-folders and samples, sorted. Kept compact, because the PSRAM outside the engine is short (59 KB
// free at boot, and a spectral sample job wants 37 KB of it): one block of kDirBytes, the names packed one after the other (a flag byte:
// 1 = folder, the name, a 0), then kDirMax offsets, sorted. The block is taken at a listing and given back by samples_esp32_dir_close
// (the application: before a sample job). While it is held, opening the same folder again does not read the card (g_dir_path).
// A folder with more names than fit shows what fits (in the card's order), sorted; the log says how many were left out.
constexpr int kDirMax = 2048;
constexpr size_t kDirPool = 30 * 1024, kDirBytes = kDirPool + kDirMax * sizeof(uint16_t);
char *g_dir = nullptr;                            // the block: the names, then the offsets
uint16_t *g_dir_ofs = nullptr;
volatile int g_dir_n = 0;
char g_dir_path[AUDIO_NAME_MAX] = "";             // the folder the block lists
volatile bool g_dir_valid = false;

int dir_cmp(const void *a, const void *b) {
    const char *x = g_dir + *static_cast<const uint16_t *>(a), *y = g_dir + *static_cast<const uint16_t *>(b);
    if (x[0] != y[0]) return x[0] ? -1 : 1;                                 // folders first
    return strcasecmp(x + 1, y + 1);
}

int list_folder(const char *dir) {
    g_dir_valid = false;
    g_dir_n = 0;
    if (!g_dir) {
        g_dir = static_cast<char *>(heap_caps_malloc(kDirBytes, MALLOC_CAP_SPIRAM));
        if (!g_dir) { Serial.println("[SD] no memory for the folder list"); return -1; }
        g_dir_ofs = reinterpret_cast<uint16_t *>(g_dir + kDirPool);
    }
    char path[sizeof SD_SAMPLE_DIR + AUDIO_NAME_MAX + 2];
    const bool presets = dir[0] == ':';                                     // (core/audio.h: a folder of the presets, its *.preset files)
    const char *const sub = presets ? dir + 1 : dir, *const root = presets ? SD_MOUNT_POINT "/presets" : SD_SAMPLE_DIR;
    const char *const ext = presets ? ".preset" : ".smp";
    const size_t ext_len = strlen(ext);
    if (sub[0]) snprintf(path, sizeof path, "%s/%s", root, sub); else snprintf(path, sizeof path, "%s", root);
    const uint32_t t0 = millis();
    DIR *d = opendir(path);
    if (!d) return -1;
    int n = 0, seen = 0, left_out = 0;
    size_t used = 0;
    while (const dirent *e = readdir(d)) {
        if ((++seen & 15) == 0) engine_synth_io_pump();                     // a long folder: the streams still get their blocks
        if ((seen & 63) == 0) vTaskDelay(1);                                // ... and the idle task its time (watchdog)
        size_t len = strlen(e->d_name);
        if (!len || e->d_name[0] == '.') continue;
        const bool is_dir = e->d_type == DT_DIR;
        if (!is_dir) {                                                      // *.smp (or *.preset); no versions (name~n)
            if (len <= ext_len || strcasecmp(e->d_name + len - ext_len, ext) != 0) continue;
            len -= ext_len;
            if (!presets && memchr(e->d_name, '~', len)) continue;
        }
        if (len >= AUDIO_NAME_MAX) continue;                                // (too long to be a library name)
        if (n >= kDirMax || used + len + 2 > kDirPool) { left_out++; continue; }
        g_dir_ofs[n++] = static_cast<uint16_t>(used);
        g_dir[used++] = is_dir ? 1 : 0;
        memcpy(g_dir + used, e->d_name, len);
        used += len;
        g_dir[used++] = 0;
    }
    closedir(d);
    const uint32_t t1 = millis();
    qsort(g_dir_ofs, static_cast<size_t>(n), sizeof *g_dir_ofs, dir_cmp);
    snprintf(g_dir_path, sizeof g_dir_path, "%s", dir);
    g_dir_n = n;
    g_dir_valid = true;
    Serial.printf("[SD] folder %s: %d entries of %d read in %u ms, sorted in %u ms, %u bytes of names", path, n, seen, (unsigned)(t1 - t0), (unsigned)(millis() - t1), (unsigned)used);
    if (left_out) Serial.printf(" (%d left out: the list is full)", left_out);
    Serial.println();
    return n;
}

void run_lib_job() {
    LibJob &j = g_lib;
    if (!sd_card_mounted()) j.result = j.list ? -1 : 0;                     // (a slow card is not read: the posters check)
    else if (j.list) j.result = list_folder(j.name);
    else j.result = read_info(j.name, j.info) ? 1 : 0;
    j.state = 2;
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

// ---- the card task's hooks (card_task.h) ----
uint32_t g_catalog_card = 0;                      // serial number of the card the catalog was read from (0 = none)

void card_seen(uint32_t id) {
#ifdef HWV1_SD_BENCH
    file_bench();
#endif
    if (id != g_catalog_card && engine_synth_sample_count() > 0) {   // another card: its files are not the ones the catalog lists
        engine_synth_catalog_reset();
        g_catalog_card = 0;
    }
    g_dir_valid = false;                          // (the browser reads its folder again)
    g_dir_n = 0;
}

void card_ready(uint32_t id) {
    g_catalog_card = id;
    if (!g_attached) g_attached = engine_synth_attach_storage(&g_storage);
}

void card_gone() {
    g_dir_valid = false;
    g_dir_n = 0;
}

bool calm_pending() { return g_lib.state == 1; }   // a sample's header or a folder's list, asked by the UI
void always() { engine_synth_capture_step(); }     // a recording: its chunk first, every time
void idle() { engine_synth_render_step(); }        // a processing job: one chunk, only while the loader has nothing waiting
bool pump() { return engine_synth_io_pump(); }     // the loader of the streams

#ifdef HWV1_DEBUG_AUDIO
// Every 2 s, when anything happened: what the card delivered and whether a playhead ran dry ("underruns" is what you hear as a dropout).
void report() {
    static uint32_t t_last = 0, reads0 = 0, under0 = 0, blocks0 = 0, seeks0 = 0;
    static uint64_t bytes0 = 0, us0 = 0, seek_us0 = 0;
    const uint32_t now = millis();
    if (now - t_last < 2000) return;
    const SdStorage::Stats s = g_storage.stats();
    uint32_t under = 0, blocks = 0;
    engine_synth_sampler_stats(&under, &blocks);
    if (s.reads != reads0 || under != under0) {
        const uint32_t dr = s.reads - reads0;
        const uint32_t dt = now - t_last;
        const uint32_t ds = s.seeks - seeks0;
        Serial.printf("[SD] %u reads in %u ms: %u KB/s, avg %u us, worst ever %u us; opens %u, seeks %u, errors %u | [SMP] stream blocks +%u, underruns %u (+%u); seek avg %u us\n",
                      (unsigned)dr, (unsigned)dt, (unsigned)((s.bytes - bytes0) * 1000 / 1024 / (dt ? dt : 1)), dr ? (unsigned)((s.read_us - us0) / dr) : 0u, (unsigned)s.max_us,
                      (unsigned)s.opens, (unsigned)s.seeks, (unsigned)s.errors, (unsigned)(blocks - blocks0), (unsigned)under, (unsigned)(under - under0),
                      ds ? (unsigned)((s.seek_us - seek_us0) / ds) : 0u);
    }
    static uint32_t crc0 = 0;
    const uint32_t crc = sd_card_crc_errors();
    if (crc != crc0) Serial.printf("[SD] data CRC errors %u (+%u), each block read again\n", (unsigned)crc, (unsigned)(crc - crc0));
    crc0 = crc;
    t_last = now; reads0 = s.reads; bytes0 = s.bytes; us0 = s.read_us; under0 = under; blocks0 = blocks; seeks0 = s.seeks; seek_us0 = s.seek_us;
}
#endif

constexpr uint32_t kJobWaitMs = 1500;

const card_hooks_t kHooks = {
    card_seen, card_ready, card_gone, calm_pending, run_lib_job, always, idle, pump,
#ifdef HWV1_DEBUG_AUDIO
    report,
#else
    nullptr,
#endif
};
}  // namespace

void samples_esp32_start() {
    engine_synth_set_card_root(SD_MOUNT_POINT);                // the processing jobs write next to the library
    g_storage.set_dir(SD_SAMPLE_DIR);
    card_task_start(&kHooks);
}

// Posts the library job and waits for it (as post_job / job_free do for the settings files). A folder's list may take seconds (a
// folder of a thousand files on a slow card), a header a few milliseconds.
static bool lib_post(uint32_t wait_ms) {
    if (!card_task_running()) return false;
    g_lib.state = 1;
    for (uint32_t t0 = millis(); g_lib.state != 2;) {
        if (millis() - t0 > wait_ms) return false;
        vTaskDelay(1);
    }
    return true;
}
static bool lib_free() {
    for (uint32_t t0 = millis(); g_lib.state == 1;) {
        if (millis() - t0 > kJobWaitMs) return false;
        vTaskDelay(1);
    }
    return true;
}

int samples_esp32_find(const char *name) { return engine_synth_catalog_find(name); }

int samples_esp32_use(const char *name) {
    if (!name || !name[0] || strlen(name) >= AUDIO_NAME_MAX) return -1;
    const int have = engine_synth_catalog_find(name);
    if (have >= 0) return have;
    if (card_task_state() != CARD_OK || !lib_free()) return -1;
    snprintf(g_lib.name, sizeof g_lib.name, "%s", name);
    g_lib.list = false;
    g_lib.asked = false;
    if (!lib_post(kJobWaitMs)) return -1;
    int index = -1;                                                         // no such sample
    if (g_lib.result == 1 && (index = engine_synth_catalog_add(&g_lib.info)) < 0) index = -2;   // the catalog is full
    g_lib.state = 0;
    return index;
}

// The same without waiting: the job is posted and its answer taken at a later call for the same name (an answer for another name is
// dropped: the highlight moved on). A catalog that is full keeps the answer for the call that follows the application's clean-up.
int samples_esp32_request(const char *name) {
    if (!name || !name[0] || strlen(name) >= AUDIO_NAME_MAX) return -1;
    const int have = engine_synth_catalog_find(name);
    if (have >= 0) return have;
    if (card_task_state() != CARD_OK || !card_task_running()) return -1;
    if (g_lib.state == 1) return -3;                                        // the card task is on a job (this one, or one it finishes first)
    if (g_lib.state == 2 && g_lib.asked && !g_lib.list && !strcmp(g_lib.name, name)) {
        if (g_lib.result != 1) { g_lib.state = 0; return -1; }
        const int index = engine_synth_catalog_add(&g_lib.info);
        if (index < 0) return -2;
        g_lib.state = 0;
        return index;
    }
    snprintf(g_lib.name, sizeof g_lib.name, "%s", name);
    g_lib.list = false;
    g_lib.asked = true;
    g_lib.state = 1;
    return -3;
}

int samples_esp32_dir_open(const char *dir) {
    if (!dir) dir = "";
    if (card_task_state() != CARD_OK || strlen(dir) >= AUDIO_NAME_MAX || !lib_free()) return -1;
    if (g_dir_valid && g_dir && !strcmp(g_dir_path, dir)) return g_dir_n;   // the list is still held: no card read
    snprintf(g_lib.name, sizeof g_lib.name, "%s", dir);
    g_lib.list = true;
    g_lib.asked = false;
    if (!lib_post(15000)) return -1;
    const int n = g_lib.result;
    g_lib.state = 0;
    return n;
}

int samples_esp32_dir_read(int first, audio_dir_entry_t *out, int max) {
    if ((g_lib.state == 1 && g_lib.list) || !g_dir_valid || !g_dir || first < 0) return 0;   // (a listing still runs: nothing to show yet)
    int n = 0;
    for (; n < max && first + n < g_dir_n; n++) {
        const char *e = g_dir + g_dir_ofs[first + n];
        out[n].dir = static_cast<uint8_t>(e[0]);
        snprintf(out[n].name, sizeof out[n].name, "%s", e + 1);
    }
    return n;
}

void samples_esp32_dir_close() {
    if (!lib_free() || g_lib.state == 1) return;                            // (a listing that never ended keeps its block)
    g_dir_valid = false;
    g_dir_n = 0;
    if (g_dir) heap_caps_free(g_dir);
    g_dir = nullptr;
    g_dir_ofs = nullptr;
}

void samples_esp32_hide(int index) { engine_synth_catalog_remove(index); }

bool samples_esp32_ready(int index) {
    audio_sample_info_t in;
    return engine_synth_sample_info(index, &in) && in.name[0] && !in.pending;
}
sd_state_t samples_esp32_sd_state() { return static_cast<sd_state_t>(card_task_state()); }   // (the same three values, in the same order)
uint32_t samples_esp32_sd_read_us() { return card_task_read_us(); }
uint32_t samples_esp32_sd_generation() { return card_task_generation(); }
#endif  // ARDUINO_ARCH_ESP32
