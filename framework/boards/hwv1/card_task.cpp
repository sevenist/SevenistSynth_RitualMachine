// First prototype: the card task (card_task.h) and the storage HAL on it (hal/hal_storage.h). One task on core 0 at low priority does
// everything that touches the card: mount, the small files, the folders, and an app's own card work through its hooks (a sample
// library, the loader of its streams), so the audio task never waits for the card. The card driver has no locks, so nothing else may
// touch the card. The task also watches the card: inserted, too slow (then it is left alone, apart from its small files), removed or
// swapped, and queues those as card events for the UI (hal_storage.h) with the result of its two checks: the read speed and the
// folders the app listed (made on request, storage_make_folders).
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <Arduino.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "hal/hal_storage.h"
#include "boards/hwv1/card_task.h"
#include "boards/hwv1/sd_card.h"

extern "C" bool display_boot_playing(void);       // display_esp32.cpp: the boot animation still covers the UI

namespace {
constexpr int kStackBytes = 8192;                 // long file names (FATFS LFN on the stack) and the directory walk
// Task priorities (FreeRTOS, higher = more urgent): audio 23 (core 1) > input scan 8 > this task > the Arduino loop that draws the UI (1), both on core 0.
// A loader has deadlines (a stream that runs dry is audible), a frame of the UI has none, so it ranks above the UI. It does not hog the core:
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
struct Calm {                                     // (not during the boot animation: nobody watches the UI, the start should be short)
    Calm() { if (!display_boot_playing()) vTaskPrioritySet(nullptr, kCalmPriority); }
    ~Calm() { vTaskPrioritySet(nullptr, kPriority); }
};
SdStorage *g_st = nullptr;                        // card_task_storage(), once the task runs
TaskHandle_t g_task = nullptr;
const card_hooks_t kNoHooks = {};
const card_hooks_t *g_hooks = &kNoHooks;

// The app's folders and the moves from an older layout (hal_storage.h).
const char *const *g_dirs = nullptr;
int g_dir_count = 0;
const storage_move_t *g_moves = nullptr;
int g_move_count = 0;

// ---- the card's life: inserted, too slow, removed ----
// HWV1 has no card-detect pin, so the card is polled: a mount attempt once a second while there is none, a status command once a second while there is one.
constexpr uint32_t kSlowReadUs = 15000;           // a single sector read above this: the card cannot stream samples (healthy cards: 300-2000 us)
constexpr uint32_t kPollMs = 1000, kPollSlowMs = 2000;
volatile card_state_t g_state = CARD_NONE;
volatile uint32_t g_slow_us = 0, g_gen = 0;

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

// Bit i set = folder i of the app's list is not on the card.
uint8_t missing_folders() {
    uint8_t m = 0;
    char p[96];
    for (int i = 0; i < g_dir_count; i++) {
        snprintf(p, sizeof p, SD_MOUNT_POINT "/%s", g_dirs[i]);
        if (!is_dir(p)) m |= static_cast<uint8_t>(1u << i);
    }
    return m;
}

// Makes the folders that lead to `name` (a path below the card's root): its parents, not `name` itself.
void make_parents(const char *name) {
    char p[96];
    for (int i = 0; name[i]; i++) {
        if (name[i] != '/') continue;
        if (snprintf(p, sizeof p, SD_MOUNT_POINT "/%.*s", i, name) >= static_cast<int>(sizeof p)) return;
        ::mkdir(p, 0777);
    }
}

// The folders the user agreed to (an ASK popup): the files of an older layout move first (storage_set_moves; a rename moves a whole
// folder at once on FAT), then the missing folders are made. A card without them stays usable for reading what it has.
void make_folders() {
    g_mkdirs = false;
    storage_event_t e = {};
    e.kind = STORAGE_EV_FOLDERS_DONE;
    if (!sd_card_mounted()) { push_event(e); return; }
    char p[96], q[96];
    for (int i = 0; i < g_move_count; i++) {
        make_parents(g_moves[i].to);
        snprintf(p, sizeof p, SD_MOUNT_POINT "/%s", g_moves[i].from);
        snprintf(q, sizeof q, SD_MOUNT_POINT "/%s", g_moves[i].to);
        if ((is_dir(p) || is_file(p)) && !is_dir(q) && !is_file(q)) e.moved = ::rename(p, q) == 0 || e.moved;
    }
    for (int i = 0; i < g_dir_count; i++) {
        snprintf(p, sizeof p, SD_MOUNT_POINT "/%s", g_dirs[i]);
        if (!is_dir(p) && ::mkdir(p, 0777) != 0) Serial.printf("[SD] could not make %s (errno %d)\n", p, errno);
    }
    e.ok = missing_folders() == 0;
    Serial.printf("[SD] folders %s%s\n", e.ok ? "ready" : "NOT all made", e.moved ? ", an older layout's files moved" : "");
    push_event(e);
    g_gen = g_gen + 1;                            // the application reads its files again
}

void card_gone() {
    g_st->close_all();
    sd_card_unmount();
    if (g_hooks->card_gone) g_hooks->card_gone();
    g_state = CARD_NONE;
    g_gen = g_gen + 1;
    Serial.println("[SD] card removed");
    storage_event_t e = {};
    e.kind = STORAGE_EV_REMOVED;
    push_event(e);
}

void card_in() {
#ifdef HWV1_SD_BENCH
    sd_card_bench();
#endif
    const uint32_t id = sd_card_id();
    const uint32_t us = sd_card_probe_us();
    if (us == 0) {                                // it mounted but cannot be read: treat it as no card, try again at the next poll
        Serial.println("[SD] the card does not read: ignored");
        sd_card_unmount();
        return;
    }
    if (g_hooks->card_seen) g_hooks->card_seen(id);
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
        g_state = CARD_SLOW;                      // stays mounted so its removal is seen, but nothing streams from it
        g_gen = g_gen + 1;
        Serial.printf("[SD] card too slow: a sector read takes %u us (limit %u): not used\n", (unsigned)us, (unsigned)kSlowReadUs);
        return;
    }
    if (g_hooks->card_ready) g_hooks->card_ready(id);
    g_state = CARD_OK;
    Serial.printf("[SD] card ready: a sector read takes %u us\n", (unsigned)us);
    g_gen = g_gen + 1;                            // the application looks its files up again
}

void poll_card() {
    if (!sd_card_mounted()) {
        if (sd_card_mount()) { Serial.println("[SD] card inserted"); card_in(); }
    } else if (!sd_card_alive()) {
        card_gone();
    }
}

// ---- small files (hal_storage.h): one job at a time, posted by the UI task, run here ----
// state: 0 = free, 1 = posted (the card task owns the job), 2 = done (the poster owns it). A poster that gave up waiting leaves it at 1 or 2;
// the next post waits for / takes it over, so the card task never writes into a job that is being refilled.
struct FileJob {
    char name[80];                                 // (the Ritual Machine's longest: presets/<a 47-character name>.preset)
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
    if (j.remove || j.write) g_st->take_spare("");   // (a header read's descriptor: the file it holds open may be the one that goes)
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
    bool reading = false;                           // the streams still had reads in flight after the last pump
    const card_hooks_t &h = *g_hooks;
    for (;;) {
        const uint32_t now = millis();
        if (first || now - t_poll >= (g_state == CARD_SLOW ? kPollSlowMs : kPollMs)) { first = false; t_poll = now; poll_card(); }
        if (h.calm_pending && h.calm_pending()) { Calm calm; if (h.calm_run) h.calm_run(); }   // a file's header or a folder's list, asked by the UI
        if (g_job.state == 1) run_file_job();      // a small file (keys.cfg ...), also on a card too slow for samples
        if (g_mkdirs) make_folders();
        // Work that may wait gets one chunk, and only while the streams have nothing waiting. The streams come first: a chunk holds the card
        // for ~50 ms (4 KB read, 4 KB written), and measured with two chunks before every pump, 4 streamed pads lost 13..48 blocks per job.
        if (g_state == CARD_OK && h.always) h.always();           // a recording: its chunk first, every time (what waits in its ring is lost when it fills)
        if (g_state == CARD_OK && !reading && h.idle) h.idle();
        if (g_state == CARD_OK) {
            if (g_st->pinned_next()) { Calm calm; g_st->service(); }   // a sample's header or heads: read outside the engine's lock, below the UI
            const bool more = h.pump ? h.pump() : false;   // reads still in flight: go on at once, a stream may be about to run dry
            reading = more;
            if (more && ++burst < kBurst) continue;
            if (more) reading = false;                  // a long run of reads: the work that may wait still gets its chunk (it has to end)
            burst = 0;
        }
#ifdef HWV1_DEBUG_AUDIO
        if (h.report) h.report();
#endif
        vTaskDelay(g_state == CARD_OK ? 2 : pdMS_TO_TICKS(50));   // at least one whole tick: the card driver busy-waits, and the idle task of this core must get time (task watchdog)
    }
}
}  // namespace

void card_task_start(const card_hooks_t *hooks) {
    if (g_task) return;
    if (hooks) g_hooks = hooks;
    g_st = &card_task_storage();
    xTaskCreatePinnedToCore(io_task, "sd_io", kStackBytes, nullptr, kPriority, &g_task, 0);
}
bool card_task_running() { return g_task != nullptr; }
card_state_t card_task_state() { return g_state; }
uint32_t card_task_read_us() { return g_slow_us; }
uint32_t card_task_generation() { return g_gen; }
// Made at its first use: an app that never uses the card does not carry its 5.7 KB of static RAM (the linker drops what nothing calls).
SdStorage &card_task_storage() {
    static SdStorage storage;
    return storage;
}

extern "C" void storage_set_folders(const char *const *folders, int count) {
    g_dirs = folders;
    g_dir_count = count < 0 ? 0 : count > STORAGE_FOLDERS_MAX ? STORAGE_FOLDERS_MAX : count;
}
extern "C" void storage_set_moves(const storage_move_t *moves, int count) { g_moves = moves; g_move_count = count < 0 ? 0 : count; }

extern "C" int storage_read(const char *name, char *buf, int cap) {
    if (g_state == CARD_NONE || !job_free()) return -1;
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
    if (g_state == CARD_NONE || len < 0 || len > STORAGE_FILE_MAX || !job_free()) return false;
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
    if (g_state == CARD_NONE || !job_free()) return false;
    snprintf(g_job.name, sizeof g_job.name, "%s", name);
    g_job.write = false;
    g_job.remove = true;
    if (!post_job()) return false;
    const bool ok = g_job.len == 0;
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
#endif  // ARDUINO_ARCH_ESP32 && HWV1
