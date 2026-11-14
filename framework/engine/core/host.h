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
// An app that puts its own data in the heaps before the engine takes its share (the Ritual Machine: its rack's graphs, in the bulk heap)
// calls init_memory(), allocates from fast_heap() / bulk_heap(), then start().
//
// fast: internal RAM (module state, the plan's buffers). bulk: large, slower RAM (long delay lines, samples); may be the same block, or
// none: everything then comes from fast. A full fast heap takes from the bulk one instead of failing. bank: the sample streaming service,
// for an app with samplers (engine/sampler/sample_bank.h).
// Threads (framework/docs/DESIGN.md, ADR-024): render() is the only call of the audio thread; the others are made from one other thread,
// and the audio output is started after start() (or init()) and stopped before shutdown().
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "engine/core/engine.h"

namespace sc {

class EngineHost {
public:
    Engine engine;

    // The heaps, then the engine with `voices` (it registers the structure modules: NoteIn, VoiceOut, BusIn, MasterOut). False when the
    // engine could not start.
    bool init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes, int voices, SampleBank *bank = nullptr) {
        init_memory(fast, fast_bytes, bulk, bulk_bytes, bank);
        return start(voices);
    }

    // init() in two steps, for an app with data of its own in the heaps.
    void init_memory(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes, SampleBank *bank = nullptr) {
        fast_.init(fast, fast_bytes);
        mem_ = Memory{&fast_, &fast_, bank};
        if (bulk && bulk != fast) {
            bulk_.init(bulk, bulk_bytes);
            fast_.set_spill(&bulk_);
            mem_.bulk = &bulk_;
        }
    }
    bool start(int voices) {
        if (!engine.init(mem_, voices)) return false;
        pos_ = kBlock;
        ready_.store(true, std::memory_order_release);
        return true;
    }
    Heap &fast_heap() { return fast_; }
    Heap &bulk_heap() { return *mem_.bulk; }            // (the fast heap when there is no bulk block)
    Memory &memory() { return mem_; }                   // what the engine allocates from (a SampleBank's init takes it too; it lives here)

    // An empty graph to build into: one for the whole program, static (it is large: never on the stack).
    static GraphDesc &new_graph() {
        static GraphDesc g;
        g = GraphDesc{};
        return g;
    }

    // Loads `g` (Engine::load: the audio thread takes it at its next block). False when the graph does not compile.
    bool load(const GraphDesc &g, int voices) { return engine.load(g, voices) == Err::Ok; }

    // The engine runs: from start() on (before the first load it renders silence) until shutdown().
    bool ready() const { return ready_.load(std::memory_order_acquire); }

    // [AUDIO] `frames` stereo frames of 16-bit audio, interleaved L R: the engine renders blocks of kBlock frames, handed out frame by
    // frame. Silence before start() and after shutdown().
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
    Memory mem_;
    q15 l_[kBlock], r_[kBlock];
    int pos_ = kBlock;                  // frames of the rendered block already handed out
    std::atomic<bool> ready_{false};
};

}  // namespace sc
