#pragma once
// SdStorage: the sampler's StorageDevice on the TF card (files under SD_SAMPLE_DIR). The sample bank calls it from the I/O task only
// (engine_synth_io_pump), so a request is executed synchronously inside poll(): submit() queues it, poll() reads it from the card.
// poll() runs under the engine's lock, which the UI thread takes for nearly every engine call: a ring block (4 KB, a few ms) is read
// there, but a sample's header and heads (IoRead::pinned: the first open of the file, up to 0.4 s in a folder of 1800 files, and a
// whole pad sample in one read, up to 0.4 s more) are read by service(), which the I/O task calls outside that lock before each pump.
// FileHandle = an index into a table of paths; real file descriptors are opened on demand and the least recently used one is closed,
// so any number of loaded samples fits in the few file objects the FAT driver allows.
//
// Reads go through a small buffer in internal DMA-capable RAM: the destinations are rings and heads in PSRAM, which the SD host cannot
// fill by DMA, and without the bounce buffer the driver falls back to one single-sector command (plus a malloc) per 512 bytes instead of
// one multi-block command per request.
#if defined(ARDUINO_ARCH_ESP32)
#include <stdint.h>
#include "engine/sampler/storage.h"

class SdStorage : public sc::StorageDevice {
public:
    sc::FileHandle open(const char *name) override;                      // "<name>.smp" below SD_SAMPLE_DIR; no card access (a missing file fails its reads); -1: the table is full
    void forget(sc::FileHandle f) override;                              // its sample was unloaded: the table entry is free for another file
    uint32_t size(sc::FileHandle f) const override { return f >= 0 && f < n_files_ && file_[f].size != kUnknownSize ? file_[f].size : 0; }
    bool submit(const sc::IoRead &r, uint64_t now_us) override;
    bool poll(sc::IoDone &d, uint64_t now_us) override;
    int pending() const override { return count_; }

    // The I/O task, outside the engine's lock: reads the request at the head of the queue when it is a pinned one (the next poll()
    // hands its result out). True when it read something. pinned_next: such a request waits (from the first call on, poll() leaves
    // them to service()).
    bool service();
    bool pinned_next() { service_on_ = true; return count_ > 0 && !served_ && q_[head_].pinned; }
    // The descriptor of a file the I/O task just read the header of (samples_esp32.cpp read_info), kept open for the load that
    // usually follows: opening a file walks its folder from the start (0.4 s for the last of 1800). keep takes the descriptor over;
    // take gives it back when it is `path`'s (the caller owns it again), else closes it and returns -1.
    void keep_spare(const char *path, int fd);
    int take_spare(const char *path);

    void close_all();                                                    // the card is going away: drop every descriptor (handles stay valid and reopen)

    // what the card did since boot (the I/O task prints them with HWV1_DEBUG_AUDIO)
    struct Stats { uint32_t reads, errors, opens, seeks; uint64_t bytes, read_us, seek_us; uint32_t max_us; };   // seek_us: part of read_us spent in lseek
    Stats stats() const { return stats_; }

private:
    static constexpr int kFiles = 48;          // sample files open as handles at once (the catalog holds at most AUDIO_SAMPLES_MAX = 32)
    static constexpr int kOpenFds = 6;         // descriptors held open at once (sd_card.cpp allows 8 files: these, a scan and a directory)
    static constexpr int kQueue = 32;
    static constexpr uint32_t kBounceBytes = 8192;
    static constexpr uint32_t kUnknownPos = 0xFFFFFFFFu;
    static constexpr uint32_t kUnknownSize = 0xFFFFFFFFu;   // a file not opened yet (the I/O task reads its size at the first read)
    struct File { char path[80]; uint32_t size; int fd; uint32_t used; uint32_t pos; uint8_t refs; };   // SD_SAMPLE_DIR/<a 47-character name>.smp; "" = free; refs: opens not forgotten yet
    int fd_of(int f);
    bool read_into(File &fl, uint32_t offset, void *dst, uint32_t bytes);
    bool execute(const sc::IoRead &r);

    bool service_on_ = false;                  // service() is called: poll() does not read pinned requests itself
    bool served_ = false, served_ok_ = false;  // the request at the head of the queue was read by service()
    char spare_path_[80] = "";
    int spare_fd_ = -1;

    File file_[kFiles] = {};
    int n_files_ = 0;
    sc::IoRead q_[kQueue] = {};
    int head_ = 0, count_ = 0;
    uint32_t tick_ = 0;
    uint8_t *bounce_ = nullptr;
    Stats stats_ = {};
};
#endif
