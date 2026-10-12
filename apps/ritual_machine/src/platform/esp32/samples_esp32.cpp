// Sample library of the ESP32 build: the .smp files below SD_SAMPLE_DIR of the TF card, in folders of any depth (cook them on the PC with
// tools/wav2smp.py; there is no .wav / .mp3 import on the board). One task on core 0 at low priority does everything that touches the
// card: mount, the loader (engine_synth_io_pump, ADR-022), so the audio task never waits for it, and the jobs the UI posts: a sample's
// header when a file is asked for by name (nothing is scanned at start: the engine's catalog holds the files in use), a folder's list for
// the browser, and the settings files of hal_storage.h (keys.cfg ...). The card driver has no locks, so nothing else may touch the card.
// The task also watches the card: inserted, too slow (then it is left alone and the synth runs as if there were none), removed or
// swapped, and queues those as card events for the UI (hal_storage.h) with the result of its two checks: the read speed and the folders
// of STORAGE_FOLDERS (made on request, storage_make_folders).
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
#include "hal/hal_storage.h"
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
// Card work that has no deadline (a sample's header for the list, a folder's list, the load of a sample's heads) runs at the UI's
// priority, sharing the core with it tick by tick: the card driver busy-waits, so at kPriority a walk through a long folder or a whole
// pad sample (0.2 .. 0.4 s each) held the UI's core for that long (the browser: the list stood still, measured 2026-10-11). Not 0:
// there the work starves (a folder's list took 3 x longer, a header did not arrive in 1.5 s). The streams' ring blocks keep kPriority.
#ifndef HWV1_SD_CALM_PRIO
#define HWV1_SD_CALM_PRIO 1
#endif
constexpr UBaseType_t kCalmPriority = HWV1_SD_CALM_PRIO;
#ifdef HWV1
extern "C" bool display_boot_playing(void);       // display_esp32.cpp: the boot animation still covers the UI
#else
bool display_boot_playing(void) { return false; }
#endif
struct Calm {                                     // (not during the boot animation: nobody watches the UI, the start should be short)
    Calm() { if (!display_boot_playing()) vTaskPrioritySet(nullptr, kCalmPriority); }
    ~Calm() { vTaskPrioritySet(nullptr, kPriority); }
};
SdStorage g_storage;
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

// ---- the library jobs (hal_audio.h audio_sample_use, audio_dir_open): posted by the UI task, run here, like the settings files ----
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
    const bool presets = dir[0] == ':';                                     // (hal_audio.h: a folder of the presets, its *.preset files)
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

// ---- the card's life: inserted, too slow, removed ----
// HWV1 has no card-detect pin, so the card is polled: a mount attempt once a second while there is none, a status command once a second while there is one.
constexpr uint32_t kSlowReadUs = 15000;           // a single sector read above this: the card cannot stream samples (healthy cards: 300-2000 us)
constexpr uint32_t kPollMs = 1000, kPollSlowMs = 2000;
volatile sd_state_t g_state = SD_NONE;
volatile uint32_t g_slow_us = 0, g_gen = 0;
uint32_t g_catalog_card = 0;                      // serial number of the card the catalog was read from (0 = none)

// ---- card events (hal_storage.h): written by this task, read by the UI loop. One writer, one reader: the indexes are enough. ----
constexpr uint32_t kEvents = 8;
storage_event_t g_ev[kEvents];
volatile uint32_t g_ev_w = 0, g_ev_r = 0;
volatile bool g_mkdirs = false;                   // storage_make_folders() asked for the folders

void push_event(const storage_event_t &e) {
    if (g_ev_w - g_ev_r >= kEvents) return;       // the UI has not looked for a while: drop the newest
    g_ev[g_ev_w % kEvents] = e;
    __sync_synchronize();
    g_ev_w = g_ev_w + 1;
}

bool is_dir(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISDIR(st.st_mode); }
bool is_file(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISREG(st.st_mode); }

// Bit i set = folder i of STORAGE_FOLDERS is not on the card.
uint8_t missing_folders() {
    static const char *const dirs[STORAGE_FOLDER_COUNT] = STORAGE_FOLDERS;
    uint8_t m = 0;
    char p[64];
    for (int i = 0; i < STORAGE_FOLDER_COUNT; i++) {
        snprintf(p, sizeof p, SD_MOUNT_POINT "/%s", dirs[i]);
        if (!is_dir(p)) m |= static_cast<uint8_t>(1u << i);
    }
    return m;
}

// The folders the user agreed to (an ASK popup): the old layout's /samples folder and /keys.cfg move into /system first (a rename moves a
// whole folder at once on FAT), then the missing folders are made. A card without them stays usable for reading what it has.
void make_folders() {
    g_mkdirs = false;
    storage_event_t e = {};
    e.kind = STORAGE_EV_FOLDERS_DONE;
    if (!sd_card_mounted()) { push_event(e); return; }
    ::mkdir(SD_MOUNT_POINT "/system", 0777);
    if (is_dir(SD_MOUNT_POINT "/samples") && !is_dir(SD_SAMPLE_DIR)) e.moved = ::rename(SD_MOUNT_POINT "/samples", SD_SAMPLE_DIR) == 0 || e.moved;
    ::mkdir(SD_MOUNT_POINT "/" STORAGE_DIR_CONFIG, 0777);
    if (is_file(SD_MOUNT_POINT "/keys.cfg") && !is_file(SD_MOUNT_POINT "/" STORAGE_DIR_CONFIG "/keys.cfg"))
        e.moved = ::rename(SD_MOUNT_POINT "/keys.cfg", SD_MOUNT_POINT "/" STORAGE_DIR_CONFIG "/keys.cfg") == 0 || e.moved;
    static const char *const dirs[STORAGE_FOLDER_COUNT] = STORAGE_FOLDERS;
    char p[64];
    for (int i = 0; i < STORAGE_FOLDER_COUNT; i++) {
        snprintf(p, sizeof p, SD_MOUNT_POINT "/%s", dirs[i]);
        if (!is_dir(p) && ::mkdir(p, 0777) != 0) Serial.printf("[SD] could not make %s (errno %d)\n", p, errno);
    }
    e.ok = missing_folders() == 0;
    Serial.printf("[SD] folders %s%s\n", e.ok ? "ready" : "NOT all made", e.moved ? ", old layout moved into /system" : "");
    push_event(e);
    g_gen = g_gen + 1;                            // the application reads keys.cfg again and looks its samples up
}

void card_gone() {
    g_storage.close_all();
    sd_card_unmount();
    g_dir_valid = false;
    g_dir_n = 0;
    g_state = SD_NONE;
    g_gen = g_gen + 1;
    Serial.println("[SD] card removed");
    storage_event_t e = {};
    e.kind = STORAGE_EV_REMOVED;
    push_event(e);
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
    if (id != g_catalog_card && engine_synth_sample_count() > 0) {   // another card: its files are not the ones the catalog lists
        engine_synth_catalog_reset();
        g_catalog_card = 0;
    }
    g_dir_valid = false;                          // (the browser reads its folder again)
    g_dir_n = 0;
    storage_event_t e = {};                       // the two checks of a new card: its read speed and its folders
    e.kind = STORAGE_EV_INSERTED;
    e.read_us = us;
    e.read_limit_us = kSlowReadUs;
    e.slow = us > kSlowReadUs;
    e.missing = missing_folders();
    if (e.missing) Serial.printf("[SD] folders missing (mask 0x%02x)\n", (unsigned)e.missing);
    push_event(e);
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
    g_gen = g_gen + 1;                            // the application looks the samples of its pads and its rack up again
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

// ---- settings files (hal_storage.h): one job at a time, posted by the UI task, run here ----
// state: 0 = free, 1 = posted (the card task owns the job), 2 = done (the poster owns it). A poster that gave up waiting leaves it at 1 or 2;
// the next post waits for / takes it over, so the card task never writes into a job that is being refilled.
struct FileJob {
    char name[80];                                 // (the longest: presets/<a 47-character name>.preset)
    char data[STORAGE_FILE_MAX];
    int  len;                                      // write: bytes in data; read: capacity in, bytes read out (-1 = failed)
    bool write;
    bool remove;                                   // delete the file (storage_remove)
    volatile int state;
};
FileJob g_job;
constexpr uint32_t kJobWaitMs = 1500;

bool write_file(const char *path, const char *tmp, const char *data, int len) {
    const int fd = ::open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return false;
    const bool ok = ::write(fd, data, len) == len && fsync(fd) == 0;
    ::close(fd);
    if (!ok) { ::unlink(tmp); return false; }
    ::unlink(path);                                // FATFS cannot rename over a file; the old one stays until the new one is complete
    return ::rename(tmp, path) == 0;
}

void run_file_job() {
    FileJob &j = g_job;
    char path[96], tmp[100];
    snprintf(path, sizeof path, SD_MOUNT_POINT "/%s", j.name);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (j.remove || j.write) g_storage.take_spare("");   // (the header read's descriptor: the file it holds open may be the one that goes)
    if (!sd_card_mounted()) {
        j.len = -1;
    } else if (j.remove) {
        j.len = ::unlink(path) == 0 ? 0 : -1;
        Serial.printf("[SD] %s %s\n", j.len == 0 ? "removed" : "could not remove", path);
    } else if (j.write) {
        const bool ok = write_file(path, tmp, j.data, j.len);
        Serial.printf("[SD] %s %s (%d bytes)\n", ok ? "wrote" : "could not write", path, j.len);
        j.len = ok ? j.len : -1;
    } else {
        const int fd = ::open(path, O_RDONLY);
        int n = -1;
        if (fd >= 0) { n = static_cast<int>(::read(fd, j.data, sizeof j.data - 1)); ::close(fd); }
        j.len = n;
    }
    j.state = 2;
}

// Posts the job and waits for it. false: no card task, or the card task did not get to it in time.
bool post_job() {
    if (!g_task) return false;
    g_job.state = 1;
    for (uint32_t t0 = millis(); g_job.state != 2;) {
        if (millis() - t0 > kJobWaitMs) return false;
        vTaskDelay(1);
    }
    return true;
}

// The previous job is still running after its poster gave up: let it finish first.
bool job_free() {
    for (uint32_t t0 = millis(); g_job.state == 1;) {
        if (millis() - t0 > kJobWaitMs) return false;
        vTaskDelay(1);
    }
    return true;
}

void io_task(void *) {
    uint32_t t_poll = 0;
    bool first = true;
    int burst = 0;
    bool reading = false;                           // the loader still had reads in flight after its last pump
    for (;;) {
        const uint32_t now = millis();
        if (first || now - t_poll >= (g_state == SD_SLOW ? kPollSlowMs : kPollMs)) { first = false; t_poll = now; poll_card(); }
        if (g_lib.state == 1) { Calm calm; run_lib_job(); }   // a sample's header or a folder's list, asked by the UI
        if (g_job.state == 1) run_file_job();      // a settings file (keys.cfg ...), also on a card too slow for samples
        if (g_mkdirs) make_folders();
        // A processing job: one chunk, and only while the loader has nothing waiting. The streams come first: a chunk holds the card
        // for ~50 ms (4 KB read, 4 KB written), and measured with two chunks before every pump, 4 streamed pads lost 13..48 blocks per job.
        if (g_state == SD_OK) engine_synth_capture_step();          // a recording: its chunk first, every time (what waits in its ring is lost when it fills)
        if (g_state == SD_OK && !reading) engine_synth_render_step();
        if (g_state == SD_OK) {
            if (g_storage.pinned_next()) { Calm calm; g_storage.service(); }   // a sample's header or heads: read outside the engine's lock, below the UI
            const bool more = engine_synth_io_pump();   // reads still in flight: go on at once, a stream may be about to run dry
            reading = more;
            if (more && ++burst < kBurst) continue;
            if (more) reading = false;                  // a long run of reads: the job still gets its chunk (it has to end)
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
    engine_synth_set_card_root(SD_MOUNT_POINT);                // the processing jobs write next to the library
    if (!g_task) xTaskCreatePinnedToCore(io_task, "sd_io", kStackBytes, nullptr, kPriority, &g_task, 0);
}

// Posts the library job and waits for it (as post_job / job_free do for the settings files). A folder's list may take seconds (a
// folder of a thousand files on a slow card), a header a few milliseconds.
static bool lib_post(uint32_t wait_ms) {
    if (!g_task) return false;
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
    if (g_state != SD_OK || !lib_free()) return -1;
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
    if (g_state != SD_OK || !g_task) return -1;
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
    if (g_state != SD_OK || strlen(dir) >= AUDIO_NAME_MAX || !lib_free()) return -1;
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

extern "C" int storage_read(const char *name, char *buf, int cap) {
    if (g_state == SD_NONE || !job_free()) return -1;
    snprintf(g_job.name, sizeof g_job.name, "%s", name);
    g_job.write = false;
    g_job.remove = false;
    if (!post_job() || g_job.len < 0) return -1;
    const int n = g_job.len < cap - 1 ? g_job.len : cap - 1;
    memcpy(buf, g_job.data, n);
    buf[n] = 0;
    g_job.state = 0;
    return n;
}

extern "C" bool storage_write(const char *name, const char *data, int len) {
    if (g_state == SD_NONE || len < 0 || len > STORAGE_FILE_MAX || !job_free()) return false;
    snprintf(g_job.name, sizeof g_job.name, "%s", name);
    memcpy(g_job.data, data, len);
    g_job.len = len;
    g_job.write = true;
    g_job.remove = false;
    if (!post_job()) return false;
    const bool ok = g_job.len >= 0;
    g_job.state = 0;
    return ok;
}

extern "C" bool storage_remove(const char *name) {
    if (g_state == SD_NONE || !job_free()) return false;
    snprintf(g_job.name, sizeof g_job.name, "%s", name);
    g_job.write = false;
    g_job.remove = true;
    if (!post_job()) return false;
    const bool ok = g_job.len == 0;
    g_job.state = 0;
    return ok;
}

void samples_esp32_hide(int index) { engine_synth_catalog_remove(index); }

extern "C" bool storage_poll_event(storage_event_t *e) {
    if (g_ev_r == g_ev_w) return false;
    *e = g_ev[g_ev_r % kEvents];
    __sync_synchronize();
    g_ev_r = g_ev_r + 1;
    return true;
}

extern "C" void storage_make_folders(void) { g_mkdirs = true; }

bool samples_esp32_ready(int index) {
    audio_sample_info_t in;
    return engine_synth_sample_info(index, &in) && in.name[0] && !in.pending;
}
sd_state_t samples_esp32_sd_state() { return g_state; }
uint32_t samples_esp32_sd_read_us() { return g_slow_us; }
uint32_t samples_esp32_sd_generation() { return g_gen; }
#endif  // ARDUINO_ARCH_ESP32
