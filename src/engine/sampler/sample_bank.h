#pragma once
// Sample bank + background loader (ADR-022).
//
//  * load() opens a .smp file and brings in its RAM heads: the start of the sample (default 150 ms) and a short head
//    for every slice, so a note-on or a slice trigger starts at once whatever the card latency is. head_ms = 0 makes
//    the whole sample resident (granular playback, short one-shots need no streaming).
//  * Every Sampler voice owns a Stream (ring of blocks). pump() keeps the rings filled ahead of the playheads with
//    a small number of outstanding reads, always serving the stream that will run out first (earliest deadline).
//  * pump() is the loader's body: call it from the I/O task (ESP32, other core) or from the test loop. It only
//    touches atomics shared with the audio thread (see stream.h); load / unload / add_zone are control-time.
#include <atomic>
#include <cstdint>
#include "engine/core/module.h"
#include "engine/sampler/smp_format.h"
#include "engine/sampler/storage.h"
#include "engine/sampler/stream.h"

namespace sc {

enum SlotState : uint8_t { SLOT_FREE, SLOT_HEADER, SLOT_HEADS, SLOT_READY, SLOT_FAILED };

struct Head {
    uint32_t start_frame = 0;       // block aligned
    uint32_t frames = 0;
    q15 *data = nullptr;
};

struct SampleSlot {
    SlotState state = SLOT_FREE;
    FileHandle file = -1;
    SmpHeader h;
    Head head[2 + kSmpMaxSlices];            // start, one per slice, and the tail (reverse playback starts there)
    int n_heads = 0;
    bool resident = false;          // the whole sample is in head[0]
    uint32_t head_ms = 0;           // requested head length (0 = resident)
    int next_issue = 0, pending = 0;
    uint8_t *hdr_tmp = nullptr;
};

struct Zone {
    uint8_t lo_note = 0, hi_note = 127, lo_vel = 1, hi_vel = 127;
    int16_t sample = -1;
    int16_t root = -1;              // MIDI note, -1 = from the sample header
    int16_t tune_cents = 0;
    q15 gain = kUnity;
};

struct SamplerStats {
    std::atomic<uint32_t> underruns{0};     // playheads that found a block missing
    std::atomic<uint32_t> block_reads{0};
    std::atomic<uint32_t> late_reads{0};    // blocks that arrived after the playhead needed them
};

class SampleBank {
public:
    static constexpr int kMaxSamples = 32;
    static constexpr int kMaxStreams = 32;
    static constexpr int kMaxJobs = 64;
    static constexpr int kMaxInstruments = 8;
    static constexpr int kMaxZones = 64;
    static constexpr int kQueueDepth = 2;    // outstanding reads

    bool init(StorageDevice &dev, Memory &mem);
    void shutdown();

    // --- control time ---
    int load(const char *name, int head_ms = 150);          // slot id or -1; the heads arrive through pump()
    bool ready(int id) const { return id >= 0 && id < kMaxSamples && slot_[id].state == SLOT_READY; }
    bool failed(int id) const { return id >= 0 && id < kMaxSamples && slot_[id].state == SLOT_FAILED; }
    const SampleSlot *slot(int id) const { return id >= 0 && id < kMaxSamples ? &slot_[id] : nullptr; }
    void unload(int id);
    int add_instrument();
    bool add_zone(int inst, const Zone &z);
    const Zone *pick(int inst, int note, int velocity127) const;

    // --- loader ---
    void pump(uint64_t now_us);
    bool idle() const { return jobs_in_flight_ == 0; }
    void add_stream(Stream *s);
    void remove_stream(Stream *s);
    SamplerStats stats;

    Memory &memory() { return mem_; }

private:
    enum JobKind : uint8_t { J_FREE, J_HEADER, J_HEAD, J_BLOCK };
    struct Job { JobKind kind = J_FREE; int16_t slot = 0, stream = 0, ring_slot = 0, head = 0; uint32_t gen = 0, blk = 0; int sample = 0; };
    struct Instrument { Zone zone[kMaxZones]; int n = 0; bool used = false; };

    int alloc_job();
    void complete(const IoDone &d);
    void issue_admin(uint64_t now_us);
    void issue_streams(uint64_t now_us);
    void finish_slot_heads(SampleSlot &s);
    void free_heads(SampleSlot &s);
    int desired_blocks(const StreamSnap &sn, int64_t out_blk[], int64_t out_until[]) const;

    StorageDevice *dev_ = nullptr;
    Memory mem_;
    SampleSlot slot_[kMaxSamples];
    Stream *streams_[kMaxStreams] = {};
    Job jobs_[kMaxJobs];
    int jobs_in_flight_ = 0;
    Instrument inst_[kMaxInstruments];
};

}  // namespace sc
