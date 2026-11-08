#pragma once
// Single-producer single-consumer ring of fixed-size commands (ADR-024): the UI / control thread posts, the audio
// thread drains at the start of every block. No locks, no allocation after attach(); a full queue rejects the
// command (the caller sees `false`) instead of blocking.
// The slots come from a heap at attach() (the engine gives its bulk heap: 128 slots are ~34 KB, too much for the
// board's internal RAM); a copy in or out moves the header and the `size` bytes of payload only.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "engine/core/heap.h"

namespace sc {

enum class Cmd : uint8_t { None, NoteOn, NoteOff, AllNotesOff, SetParam, SetBlob, SetDepth, VoiceMode, Transport, SeqSetup, SeqSteps, PadSeqSetup, PadSeqHits, SeqRecord, PadSeqRoute };   // VoiceMode: node = mode, idx = legato, value = glide coefficient (q15, 0 = none); Transport: node = running, idx = click, value = bpm; SeqSetup / SeqSteps: blob = SeqSetup / SeqChunk (sequencer.h); PadSeqSetup / PadSeqHits: blob = SeqSetup / PadHitChunk, node = the Pads module's node id; SeqRecord: value = passes to skip (-1: none), idx = stop at its end; PadSeqRoute: blob = kPadSeqGroups node ids (int8_t, < 0: none), size 0 = no routing

constexpr int kCmdBlobMax = 256;

struct Command {
    Cmd type = Cmd::None;
    uint8_t node = 0;           // node id (SetParam / SetBlob); edge index (SetDepth)
    uint8_t idx = 0;            // parameter index
    uint8_t note = 0;           // NoteOn / NoteOff
    int32_t value = 0;          // parameter value / velocity
    uint16_t size = 0;          // SetBlob payload size
    uint8_t blob[kCmdBlobMax];
};

template <int N>
class CommandRing {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
public:
    bool attach(Heap &h) {                                          // [INIT] once, before the first push
        if (!slot_) slot_ = h.alloc_array<Command>(N);
        return slot_ != nullptr;
    }
    void detach(Heap &h) {                                          // [INIT] the slots back to the heap (queued commands are dropped)
        h.free(slot_);
        slot_ = nullptr;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }
    bool push(const Command &c) {                                   // [CONTROL] producer
        const uint32_t h = head_.load(std::memory_order_relaxed);
        if (!slot_ || h - tail_.load(std::memory_order_acquire) >= static_cast<uint32_t>(N)) return false;
        copy(slot_[h & (N - 1)], c);
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(Command &c) {                                          // [AUDIO] consumer
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        copy(c, slot_[t & (N - 1)]);
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    uint32_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }

private:
    static void copy(Command &to, const Command &from) {
        std::memcpy(static_cast<void *>(&to), &from, offsetof(Command, blob));    // (plain data: its only non-trivial part is the member defaults)
        if (from.size) std::memcpy(to.blob, from.blob, from.size < kCmdBlobMax ? from.size : kCmdBlobMax);
    }
    Command *slot_ = nullptr;
    std::atomic<uint32_t> head_{0}, tail_{0};
};

}  // namespace sc
