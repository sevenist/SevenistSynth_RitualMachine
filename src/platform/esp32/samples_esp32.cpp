// Sample library of the ESP32 build: the .smp files in SD_SAMPLE_DIR of the TF card (cook them on the PC with tools/wav2smp.py; there is no
// .wav / .mp3 import on the board). One task on core 0 at low priority does everything that touches the card: mount, scan, and the loader
// (engine_synth_io_pump, ADR-022), so the audio task never waits for it. It also watches the card: inserted, too slow (then it is left alone and
// the synth runs as if there were none), removed or swapped, and queues those as card events for the UI (hal_storage.h) with the result of
// its two checks: the read speed and the folders of STORAGE_FOLDERS (made on request, storage_make_folders). Like the desktop's samples/ folder, new files are appended to the
// catalog after the known ones, so the index a rack stores stays valid. The settings files of hal_storage.h (keys.cfg) are read and
// written here too, as jobs the UI posts: the card driver has no locks, so nothing else may touch the card.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <ctype.h>
#include <dirent.h>
#include <esp_timer.h>
#include <errno.h>
#include <fcntl.h>
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
    if (g_state == SD_OK) scan();                 // the library folder may hold files now
    g_gen = g_gen + 1;                            // the application reads keys.cfg again and looks its samples up
}

void card_gone() {
    g_storage.close_all();
    sd_card_unmount();
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
    if (id != g_catalog_card && g_n > 0) {        // another card: its files are not the ones the catalog lists
        engine_synth_catalog_reset();
        g_n = 0;
        g_catalog_card = 0;
    }
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
    char name[32];
    char data[STORAGE_FILE_MAX];
    int  len;                                      // write: bytes in data; read: capacity in, bytes read out (-1 = failed)
    bool write;
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
    char path[48], tmp[52];
    snprintf(path, sizeof path, SD_MOUNT_POINT "/%s", j.name);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!sd_card_mounted()) {
        j.len = -1;
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
    for (;;) {
        const uint32_t now = millis();
        if (first || now - t_poll >= (g_state == SD_SLOW ? kPollSlowMs : kPollMs)) { first = false; t_poll = now; poll_card(); }
        if (ulTaskNotifyTake(pdTRUE, 0) && g_state == SD_OK) scan();      // the Scan row of the sample list
        if (g_job.state == 1) run_file_job();      // a settings file (keys.cfg ...), also on a card too slow for samples
        if (g_mkdirs) make_folders();
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

extern "C" int storage_read(const char *name, char *buf, int cap) {
    if (g_state == SD_NONE || !job_free()) return -1;
    snprintf(g_job.name, sizeof g_job.name, "%s", name);
    g_job.write = false;
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
    if (!post_job()) return false;
    const bool ok = g_job.len >= 0;
    g_job.state = 0;
    return ok;
}

extern "C" bool storage_poll_event(storage_event_t *e) {
    if (g_ev_r == g_ev_w) return false;
    *e = g_ev[g_ev_r % kEvents];
    __sync_synchronize();
    g_ev_r = g_ev_r + 1;
    return true;
}

extern "C" void storage_make_folders(void) { g_mkdirs = true; }

bool samples_esp32_ready(int index) { return index >= 0 && index < g_n && !g_cat[index].pending; }
sd_state_t samples_esp32_sd_state() { return g_state; }
uint32_t samples_esp32_sd_read_us() { return g_slow_us; }
uint32_t samples_esp32_sd_generation() { return g_gen; }
#endif  // ARDUINO_ARCH_ESP32
