#pragma once
// The recording tap (ADR-041: capture / resampling). The audio thread hands it every block the master output receives (before the
// volume), mixed to mono; another thread takes the frames and writes them where they go (the card: platform/engine/capture_job.cpp).
// One producer, one consumer, no lock: a ring of frames between them. When the consumer is too slow the ring fills and the frames that
// do not fit are lost (counted in dropped()).
//
// Armed, the tap waits: for the next bar line of the running clock (to the sample), or for the first block louder than a threshold, or
// for nothing. Recording, it stops by itself after `limit` frames when there is one (a number of bars), else at stop().
// Armed for the step sequencer (arm_seq), it waits for the pass the sequencer marks (SeqPos::rec_start) and records its rec_frames;
// with a tail it goes on after them until the sound has stayed under a threshold for a while (or for `tail_max` frames at most).
#include <atomic>
#include <cstdint>
#include "engine/core/module.h"

namespace sc {

class CaptureTap {
public:
    enum State : int { Idle, Armed, Recording, Done };

    // [control thread, Idle] the ring: `frames` a power of two.
    void set_buffer(q15 *buf, uint32_t frames) { buf_ = buf; mask_ = frames - 1; }
    // [control thread, Idle] threshold: q15 peak that starts the recording (0 = at once); at_bar: with the clock running, the next
    // bar line starts it instead; limit: frames to record (0 = until stop()).
    void arm(q15 threshold, bool at_bar, uint32_t limit);
    // [control thread, Idle] the sequencer's marked pass: its frames, then (tail_max > 0) on until `tail_quiet` frames in a row stayed
    // under `tail_threshold`, `tail_max` frames after the pass at most.
    void arm_seq(q15 tail_threshold, uint32_t tail_quiet, uint32_t tail_max);
    void stop();                                        // [control] Recording -> Done (the ring is still to be emptied), Armed -> Done
    void reset();                                       // [control, once the consumer is done] -> Idle, the ring empty
    void feed(const q15 *l, const q15 *r, int n, const Transport *t, const SeqPos *seq = nullptr);   // [audio thread]
    uint32_t take(q15 *dst, uint32_t max);              // [consumer] up to max frames: how many came

    State state() const { return static_cast<State>(state_.load(std::memory_order_acquire)); }
    uint32_t waiting() const { return w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire); }   // frames in the ring
    uint32_t frames() const { return time_.load(std::memory_order_relaxed); }      // recorded so far (the lost ones too)
    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    q15 *buf_ = nullptr;
    uint32_t mask_ = 0;
    std::atomic<uint32_t> w_{0}, r_{0};                 // frames written / taken since arm (the ring index is the value & mask_)
    std::atomic<int> state_{Idle};
    std::atomic<uint32_t> time_{0}, dropped_{0};
    q15 threshold_ = 0;
    bool at_bar_ = false;
    uint32_t limit_ = 0;
    bool at_seq_ = false;
    q15 tail_threshold_ = 0;
    uint32_t tail_quiet_ = 0, tail_max_ = 0, quiet_ = 0;
};

CaptureTap &capture_tap();                              // the one tap (the master output feeds it)

}  // namespace sc
