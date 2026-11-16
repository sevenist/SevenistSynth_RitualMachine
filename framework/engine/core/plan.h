#pragma once
// Execution plan (ADR-003): a flat array of steps compiled from a GraphDesc. The audio thread only walks it.
//
// Order per block:  [pre]  global nodes that do not depend on the voices (global modulators, ...)
//                   [voice] repeated for every active voice, same buffers, per-voice module instances
//                   [post] global nodes that depend on the summed voices (FX chain, master out)
// Buffers come from a pool shared by all steps, assigned by liveness (a buffer can be reused once its last
// reader has run). Cables with unity depth are aliased (no copy); other cables become a MIX step. A delayed
// cable reads a persistent feedback buffer that a COPY step refreshes at the end of the section.
//
// Every buffer has the number type of the module that writes it (ModuleInfo::sig, ADR-050 step 2). A reader of the other type reads a
// converted copy (a TOF / TOQ step before the first such reader in the section, shared by the later ones); a MIX is done in its reader's
// type. A graph of one type has no conversion step.
#include <cstdint>
#include "engine/core/graph.h"
#include "engine/core/heap.h"
#include "engine/core/module.h"

namespace sc {

enum class Err : uint8_t { Ok, NoMem, UnknownType, DupId, BadEdge, Scope, Cycle, FanIn, TooBig, BusLoop };
const char *err_name(Err e);

// Mix / Copy on q15 blocks, MixF / CopyF on float blocks; ToF: q15 -> float, ToQ: float -> q15 (src[0] -> dst).
enum class StepKind : uint8_t { Module, Mix, Copy, MixF, CopyF, ToF, ToQ };

struct Step {
    StepKind kind;
    uint8_t node;               // index in the plan's node table (Module steps)
    uint8_t n;                  // Mix: number of sources
    bool fb;                    // voice section: uses per-voice feedback buffers (pointers need relocating)
    union { q15 *dst; float *fdst; };                           // Mix / Copy / conversion destination
    union { const q15 *src[kMaxFanIn]; const float *fsrc[kMaxFanIn]; };   // Mix sources (Copy, conversions: src[0])
    q15 gain[kMaxFanIn];        // (MixF too: kUnity is 1.0, else gain / 32768)
    Ports p;                    // Module steps (p.mod_slow / p.in_slow: what is slow whatever runs, ADR-050)
    // Module steps fed by a Rate::Dynamic source (an LFO): those values / inputs are slow in a block where none of the nodes in
    // dyn_nodes (bit = node index) was fast; dyn_src: this node is such a source (the engine reads its fast_out() after it runs).
    uint64_t dyn_nodes;
    uint16_t mod_dyn;
    uint8_t in_dyn;
    bool dyn_src;
};

struct ParamUpdate {
    Module *m;
    uint8_t idx;
    int32_t value;
};

struct Plan {
    Heap *heap = nullptr;
    Plan *next_retired = nullptr;                   // link of the list of plans the audio thread has finished with
    ParamUpdate *updates = nullptr;                 // new parameter values of instances this plan shares with the running one,
    int n_updates = 0;                              // applied by the audio thread at the moment it switches to this plan
    int n_nodes = 0;
    uint8_t node_id[kMaxNodes] = {};
    uint8_t node_type[kMaxNodes] = {};
    Scope node_scope[kMaxNodes] = {};
    int rec[kMaxNodes] = {};                        // engine instance record per node (filled by the engine)
    Module **inst_ = nullptr;                       // n_nodes x nvoices, allocated with the plan (sized by the plan's voices, not the ceiling)
    int nvoices = 0;
    Module *&inst(int node, int voice) { return inst_[node * nvoices + voice]; }             // global nodes use voice 0
    Module *inst(int node, int voice) const { return inst_[node * nvoices + voice]; }

    Step *steps = nullptr;
    int n_pre = 0, n_voice = 0, n_post = 0;         // steps[0..n_pre) pre, then voice, then post

    q15 *pool = nullptr;
    int n_slots = 0;
    float *fpool = nullptr;                         // the float buffers' slots (none in a graph of q15 modules)
    int n_fslots = 0;
    uint8_t edge_step[kMaxEdges];                   // per cable: the MIX step that applies its depth and the slot in it (edge_step 255 = aliased, no gain)
    uint8_t edge_slot[kMaxEdges];
    q15 *silence = nullptr;
    float *fsilence = nullptr;
    // The delayed cables' buffers, kBlock floats each whatever their type (a q15 one uses the first half): voice 0's region first, then
    // the next voice's, fbv_stride floats apart; the engine moves a voice's pointers by bytes.
    float *fbv = nullptr;
    int fbv_stride = 0;
    float *fbg = nullptr;
    bool has_master = false;                        // a node writes the final output (engine fills)
};

// Compiles `g` for `nvoices` voices. On success *out is a new plan (instances not attached yet), allocated in `heap`. The compiler's
// own working tables (about 30 KB, freed before it returns) come from `scratch` when given: the engine passes its bulk heap, so a
// rebuild neither takes nor fragments the fast heap (internal RAM on the board) for them.
Err compile_plan(const GraphDesc &g, const Registry &reg, Heap &heap, int nvoices, Plan **out, Heap *scratch = nullptr);
void free_plan(Plan *p);

}  // namespace sc
