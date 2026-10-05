#pragma once
// The engine: owns the module instances, the voice manager and the active plan, and renders blocks.
//
// Threading contract (ADR-024):
//   [CONTROL] load(), gc(), set_param(), set_blob(), note_on(), note_off(), all_notes_off()   -- one control thread
//   [AUDIO]   render()                                                                       -- one audio thread, never allocates
// The control calls that touch sounding state do not touch it: they post a command into a lock-free ring that
// render() drains at the start of every block. load() compiles a new plan on the control thread and publishes it
// with one atomic pointer; render() switches to it at the next block start and applies, at that moment, the new
// parameter values of the module instances it shares with the plan that was running (so a rebuild never changes
// a module in the middle of a block). Instances with the same (id, type) are reused: filter memory, delay tails
// and sampler playheads survive. gc() frees retired plans and instances no plan references any more, and backs
// off while the audio thread is switching (seqlock), so it can never free what the next block is about to use.
#include <atomic>
#include "engine/core/command_queue.h"
#include "engine/core/graph.h"
#include "engine/core/heap.h"
#include "engine/core/module.h"
#include "engine/core/plan.h"

namespace sc {

class Engine {
public:
    bool init(Memory mem, int nvoices);                 // registers the builtin modules
    bool init(Heap &heap, int nvoices) { return init(Memory{&heap, &heap}, nvoices); }   // one heap for everything
    void shutdown();                                    // frees everything the engine allocated

    Registry &registry() { return reg_; }
    Err load(const GraphDesc &g);                       // [CONTROL]
    void gc();                                          // [CONTROL]

    bool note_on(int note, int velocity127 = 100);      // [CONTROL] false = the command queue was full
    bool note_off(int note);
    bool all_notes_off();
    bool set_param(int node_id, int idx, int32_t value);   // live parameter change, all voices of the node
    bool set_edge_depth(int edge, q15 depth);           // live depth of cable `edge` (index in the loaded GraphDesc); false = needs load() (aliased unity cable, or the new depth is unity)
    bool set_blob(int node_id, const void *data, size_t bytes);   // structured settings, at most kCmdBlobMax bytes

    void render(q15 *l, q15 *r);                        // [AUDIO] exactly kBlock frames
    int active_voices() const;

#ifdef ENGINE_PROFILE
    // Optional profiler (-DENGINE_PROFILE): CPU cycles spent in each module type, summed over voices. Read and reset it from the audio thread.
    struct ProfEntry { const char *name; uint64_t cycles; uint32_t calls; };
    static constexpr int kProfMax = 40;
    int prof_take(ProfEntry *out, int max, uint32_t *blocks);       // copies and clears; *blocks = blocks rendered since the last call
#endif
    uint64_t blocks() const { return time_.load(std::memory_order_relaxed); }
    const VoiceState &voice(int v) const { return voices_[v]; }

private:
    struct Rec { bool alive; uint8_t id, type; Module *inst[kMaxVoices]; };
    static constexpr int kMaxRecs = 2 * kMaxNodes;

    int find_rec(int id, int type) const;
    void destroy_rec(Rec &r);
    void run(const Plan *pl, int first, int count, int voice, ProcessCtx &ctx);
    int alloc_voice(int note);
    void apply(const Command &c);                       // [AUDIO]
    void do_note_on(int note, int velocity127);
    void do_note_off(int note);
    void free_retired();

    Memory mem_;
    Registry reg_;
    int nvoices_ = 0;
    Rec rec_[kMaxRecs] = {};
    VoiceState voices_[kMaxVoices];
    std::atomic<Plan *> active_{nullptr};
    std::atomic<Plan *> pending_{nullptr};
    std::atomic<uint32_t> swap_seq_{0};                 // odd while the audio thread switches plans (seqlock for gc)
    CommandRing<ENGINE_CMD_RING> cmd_;
    std::atomic<Plan *> retired_{nullptr};              // lock-free stack: pushed by the audio thread, taken whole by gc()
    alignas(16) q15 bus_l_[kBlock] = {}, bus_r_[kBlock] = {};
    std::atomic<uint64_t> time_{0};
#ifdef ENGINE_PROFILE
    ProfEntry prof_[kProfMax] = {};
    int prof_n_ = 0;
    uint32_t prof_blocks_ = 0;
    void prof_add(const char *name, uint32_t cycles);
#endif
};

}  // namespace sc
