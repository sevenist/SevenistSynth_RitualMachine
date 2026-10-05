#pragma once
// SdStorage: the sampler's StorageDevice on the TF card (files under SD_SAMPLE_DIR). The sample bank calls it from the I/O task only
// (engine_synth_io_pump), so a request is executed synchronously inside poll(): submit() queues it, poll() reads it from the card.
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
    sc::FileHandle open(const char *name) override;                      // "<stem>.smp" in SD_SAMPLE_DIR; -1 when missing
    uint32_t size(sc::FileHandle f) const override { return f >= 0 && f < n_files_ ? file_[f].size : 0; }
    bool submit(const sc::IoRead &r, uint64_t now_us) override;
    bool poll(sc::IoDone &d, uint64_t now_us) override;
    int pending() const override { return count_; }

    void close_all();                                                    // the card is going away: drop every descriptor (handles stay valid and reopen)

    // what the card did since boot (the I/O task prints them with HWV1_DEBUG_AUDIO)
    struct Stats { uint32_t reads, errors, opens, seeks; uint64_t bytes, read_us; uint32_t max_us; };
    Stats stats() const { return stats_; }

private:
    static constexpr int kFiles = 48;          // distinct sample files seen (the library holds at most AUDIO_SAMPLES_MAX = 32)
    static constexpr int kOpenFds = 6;         // descriptors held open at once (sd_card.cpp allows 8 files: these, a scan and a directory)
    static constexpr int kQueue = 32;
    static constexpr uint32_t kBounceBytes = 8192;
    static constexpr uint32_t kUnknownPos = 0xFFFFFFFFu;
    struct File { char path[64]; uint32_t size; int fd; uint32_t used; uint32_t pos; };
    int fd_of(int f);
    bool read_into(File &fl, uint32_t offset, void *dst, uint32_t bytes);

    File file_[kFiles] = {};
    int n_files_ = 0;
    sc::IoRead q_[kQueue] = {};
    int head_ = 0, count_ = 0;
    uint32_t tick_ = 0;
    uint8_t *bounce_ = nullptr;
    Stats stats_ = {};
};
#endif
