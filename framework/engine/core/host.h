#pragma once
// The engine as an app's synth holds it: its two heaps, the engine, and the hand-out of its blocks to an audio output that asks for any
// number of frames (hal/hal_audio_out.h: the board's audio task calls the app's synth_render, which calls render() here).
//
//   EngineHost host;
//   Engine &eng = host.engine;                         // (the app's graph, set_param, notes ... are the engine's own calls)
//
//   bool synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes) {
//       if (!host.init(fast, fast_bytes, bulk, bulk_bytes, kVoices)) return false;
//       register_synth_modules(eng.registry());        // the module families the graph uses
//       GraphDesc &g = host.new_graph();
//       return build_graph(g) && host.load(g, kVoices);
//   }
//   void synth_render(int16_t *stereo, int frames) { host.render(stereo, frames); }
//   void synth_update() { host.update(); }
//   void synth_shutdown() { host.shutdown(); }
//
// fast: internal RAM (module state, the plan's buffers). bulk: large, slower RAM (long delay lines, samples); may be the same block, or
// none: everything then comes from fast. A full fast heap takes from the bulk one instead of failing.
// Threads (framework/docs/DESIGN.md, ADR-024): render() is the only call of the audio thread; the others are made from one other thread,
// and the audio output is started after load() and stopped before shutdown().
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "engine/core/engine.h"

namespace sc {

class EngineHost {
public:
    Engine engine;

    // The heaps and the engine with `voices` (it registers the structure modules: NoteIn, VoiceOut, BusIn, MasterOut). False when the
    // engine could not start.
    bool init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes, int voices) {
        fast_.init(fast, fast_bytes);
        Memory mem{&fast_, &fast_};
        if (bulk && bulk != fast) {
            bulk_.init(bulk, bulk_bytes);
            fast_.set_spill(&bulk_);
            mem.bulk = &bulk_;
        }
        return engine.init(mem, voices);
    }

    // An empty graph to build into: one for the whole program, static (it is large: never on the stack).
    static GraphDesc &new_graph() {
        static GraphDesc g;
        g = GraphDesc{};
        return g;
    }

    // Loads `g`; from the first load on, render() hands out the engine's sound. False when the graph does not compile.
    bool load(const GraphDesc &g, int voices) {
        if (engine.load(g, voices) != Err::Ok) return false;
        if (!ready_.load(std::memory_order_relaxed)) {
            pos_ = kBlock;
            ready_.store(true, std::memory_order_release);
        }
        return true;
    }

    bool ready() const { return ready_.load(std::memory_order_acquire); }

    // [AUDIO] `frames` stereo frames of 16-bit audio, interleaved L R: the engine renders blocks of kBlock frames, handed out frame by
    // frame. Silence before the first load and after shutdown().
    void render(int16_t *stereo, int frames) {
        if (!ready()) { std::memset(stereo, 0, static_cast<size_t>(frames) * 4); return; }
        for (int i = 0; i < frames; i++) {
            if (pos_ >= kBlock) { engine.render(l_, r_); pos_ = 0; }
            stereo[2 * i] = l_[pos_];
            stereo[2 * i + 1] = r_[pos_];
            pos_++;
        }
    }

    // [CONTROL] call it often: frees what the engine no longer uses.
    void update() { if (ready()) engine.gc(); }

    void shutdown() {
        if (!ready()) return;
        ready_.store(false, std::memory_order_release);
        engine.shutdown();
    }

private:
    Heap fast_, bulk_;
    q15 l_[kBlock], r_[kBlock];
    int pos_ = kBlock;                  // frames of the rendered block already handed out
    std::atomic<bool> ready_{false};
};

}  // namespace sc
