#pragma once
// Module interface (ADR-009). A module is a polymorphic object created in the engine Heap; the plan calls
// process() once per block through one virtual call. Signals are always q15 blocks of kBlock frames
// (ADR-010): audio, control and modulation use the same buffer type.
//
// Inputs come in two kinds:
//   in[i]   signal inputs (never null: an unconnected input points at a silent buffer)
//   mod[p]  modulation for parameter p: a q15 bipolar signal that is already the weighted sum of every cable
//           connected to the parameter (depths applied by the plan), or nullptr when nothing is connected.
//           The module maps full scale (+-1.0) to its own range and adds it to the parameter's base value.
// Parameters are int32 in module-defined units (see ParamDesc), set at control rate by set_param().
#include <cstddef>
#include <cstdint>
#include "engine/core/heap.h"
#include "engine/dsp/config.h"
#include "engine/dsp/q.h"

namespace sc {

constexpr int kMaxIn = 4;
constexpr int kMaxOut = 4;
constexpr int kMaxParams = 16;
constexpr q15 kUnity = 32767;       // depth 1.0: a single cable with this depth is aliased, no copy

enum class Scope : uint8_t { Voice, Global };

struct ParamDesc {
    const char *name;
    int32_t def, min, max;
};

struct ModuleInfo {
    const char *name;
    Scope scope;
    uint8_t n_in, n_out, n_param;
    bool reads_bus;                         // Global only: depends on the summed voices (runs after them)
    const char *in_name[kMaxIn];
    const char *out_name[kMaxOut];
    ParamDesc param[kMaxParams];
};

// Per-voice state owned by the engine, visible to voice-scope modules.
struct VoiceState {
    int note = 0;
    int32_t pitch = 0;          // 1/256 semitone (note * 256); in Mono mode with glide it moves towards target_pitch
    int32_t target_pitch = 0;   // Mono mode: where the glide is going
    q15 velocity = 0;
    bool gate = false;          // key held
    bool started = false;       // note-on happened since the previous block
    bool active = false;
    uint32_t age = 0;           // blocks since note-on
    uint32_t release_age = 0;   // blocks since note-off (0 while gate)
    bool done = false;          // set by a sink (VoiceOut) when the voice has gone silent: engine frees it
};

struct ProcessCtx {
    int frames;                 // always kBlock for now
    uint64_t time;              // blocks rendered so far
    VoiceState *voice;          // voice-scope modules only, else nullptr
    int32_t *bus_l, *bus_r;     // summed voice output in 32 bits (VoiceOut accumulates without clipping; BusIn saturates once)
    q15 *out_l, *out_r;         // final output (MasterOut writes here)
    // Every voice, for global modules that follow the keyboard as a whole (GateIn, paraphonic mode, ADR-036 stage 2). Filled before the
    // pre-voice section: `started` is still set for the voices that began in this block.
    const VoiceState *voices = nullptr;
    int nvoices = 0;
    int keys_held = 0;          // voices whose key is down
};

struct Ports {
    const q15 *in[kMaxIn];
    const q15 *mod[kMaxParams];
    q15 *out[kMaxOut];
};

class SampleBank;           // sampler/sample_bank.h

// Where a module may put its buffers. `fast` is internal SRAM (filter state, short lines); `bulk` is large and
// slower memory (PSRAM: long delay lines, sample caches). On the desktop and simple targets both are the same heap.
struct Memory {
    Heap *fast = nullptr;
    Heap *bulk = nullptr;
    SampleBank *bank = nullptr;     // sample streaming service (null when the application has no sampler)
};

class Module {
public:
    virtual ~Module() = default;
    virtual const ModuleInfo &info() const = 0;
    virtual bool init(Memory &) { return true; }            // [INIT] allocate buffers here (free them in the destructor); false = out of memory
    virtual void reset() {}                                  // [CONTROL] clear state (voice start / new instance)
    virtual void set_param(int /*idx*/, int32_t /*value*/) {}   // [CONTROL]
    virtual void set_blob(const void * /*data*/, size_t /*bytes*/) {}   // [AUDIO via the command queue] structured settings (e.g. a whole FM patch)
    virtual void process(const ProcessCtx &ctx, const Ports &p) = 0;   // [AUDIO]
};

struct ModuleType {
    const ModuleInfo *info;
    Module *(*create)(Memory &);
};

template <typename T>
Module *create_module(Memory &mem) {
    T *m = mem.fast->make<T>();
    if (!m) return nullptr;
    if (!m->init(mem)) { m->~T(); mem.fast->free(m); return nullptr; }
    return m;
}
inline void destroy_module(Heap &h, Module *m) {
    if (!m) return;
    m->~Module();
    h.free(m);
}

// Module type table: type ids are chosen by the application (builtins use the low ids, see builtin.h).
class Registry {
public:
    static constexpr int kMax = 128;
    bool add(int id, const ModuleType &t) {
        if (id < 0 || id >= kMax || t_[id].info) return false;
        t_[id] = t;
        return true;
    }
    const ModuleType *get(int id) const { return id >= 0 && id < kMax && t_[id].info ? &t_[id] : nullptr; }
private:
    ModuleType t_[kMax] = {};
};

}  // namespace sc
