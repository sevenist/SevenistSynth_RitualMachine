#pragma once
// Storage abstraction for the sampler (ADR-022). Reads are asynchronous: the sampler never waits for the card.
// An implementation per backend: TF card driver (ESP32, runs on the I/O core), memory-mapped flash / PSRAM
// (completes at once), and SimStorage (desktop / tests) which models latency, bandwidth and stalls of a slow card
// against an explicit clock, so underruns can be reproduced deterministically.
//
// All times are microseconds on the caller's clock (the engine's block counter on the desktop, esp_timer on target).
#include <cstdint>

namespace sc {

using FileHandle = int;

struct IoRead {
    FileHandle file;
    uint32_t offset;        // bytes from the start of the file
    uint32_t bytes;
    void *dst;              // the device writes here when the request completes
    uint32_t tag;           // returned in IoDone
};

struct IoDone {
    uint32_t tag;
    bool ok;
};

class StorageDevice {
public:
    virtual ~StorageDevice() = default;
    virtual FileHandle open(const char *name) = 0;                       // -1 if missing
    virtual uint32_t size(FileHandle f) const = 0;
    virtual bool submit(const IoRead &r, uint64_t now_us) = 0;           // false when the device queue is full
    virtual bool poll(IoDone &d, uint64_t now_us) = 0;                   // pops one completed request, if any
    virtual int pending() const = 0;
};

}  // namespace sc
