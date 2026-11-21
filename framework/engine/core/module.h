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
#include <cstdlib>
#include <new>
#include "engine/core/heap.h"
#include "engine/dsp/config.h"
#include "engine/dsp/q.h"

namespace sc {

constexpr int kMaxIn = 4;
constexpr int kMaxOut = 8;          // (the Pads player: four stereo buses)
constexpr int kMaxParams = 16;
constexpr q15 kUnity = 32767;       // depth 1.0: a single cable with this depth is aliased, no copy

enum class Scope : uint8_t { Voice, Global };

struct ParamDesc {
    const char *name;
    int32_t def, min, max;
};

// What a module's outputs carry, for what they modulate (ADR-050): the source decides whether a modulation input is read per sample or
// once per block. The plan follows it through the graph (Ports::mod_slow).
//   Audio    audio rate: what it modulates is read per sample (the default: a module that declares nothing keeps the per-sample reading)
//   Control  slow: envelopes, constants, the note, gates; what it modulates may be read once per block
//   Follow   a processor (VCA, mixer, filter, shaper): fast when anything feeding it, inputs or modulations, is fast
//   Dynamic  decided at run time, each block, by Module::fast_out() (an LFO: fast above 100 Hz)
enum class Rate : uint8_t { Audio, Control, Follow, Dynamic };

// The number type of a module's signals (ADR-050 step 2): what its inputs, modulations and outputs are. Q15 blocks (the default, every
// module written before) or float32 blocks, where 1.0 is q15's full scale (32768): a q15 value read as float is exact; a float written
// to q15 is rounded and clamped. The plan converts a cable where it joins two types (one converted copy for all its readers there), so
// a graph of one type has no conversion.
enum class Sig : uint8_t { Q15, F32 };

struct ModuleInfo {
    const char *name;
    Scope scope;
    uint8_t n_in, n_out, n_param;
    bool reads_bus;                         // Global only: depends on the summed voices (runs after them)
    const char *in_name[kMaxIn];
    const char *out_name[kMaxOut];
    ParamDesc param[kMaxParams];
    Rate rate = Rate::Audio;                // (after the values: a module's table that ends with its values declares Audio)
    Sig sig = Sig::Q15;
};

// The machine clock (ADR-041 decision 24): a 4/4 transport counted in beats, advanced by the engine at the end of every block. A module reads
// where the block starts and how far one sample moves it (Engine::set_transport sets the tempo and starts / stops it; a start is beat 0).
struct Transport {
    bool running = false;
    int32_t bpm = 120;
    uint64_t beat_q32 = 0;      // the position at the first sample of this block, in beats (Q32); the bar is beat / 4
    uint64_t inc_q32 = 0;       // beats per sample (Q32)
    // The sample of this block (0..n-1) where the position reaches `at` (beats Q32); n when it does not in this block (or stopped).
    int sample_at(uint64_t at, int n) const {
        if (!running || inc_q32 == 0) return n;
        if (at <= beat_q32) return 0;
        const uint64_t k = (at - beat_q32 + inc_q32 - 1) / inc_q32;
        return k < static_cast<uint64_t>(n) ? static_cast<int>(k) : n;
    }
};

// Where the engine's step sequencer (sequencer.h) is at the END of the block, for the modules that follow it (MotionSeq): the step that
// plays and how far into it (Q16: step << 16 | fraction; the fraction runs down while the pattern goes backwards). Not running: stopped,
// or waiting for its first step.
struct SeqPos {
    bool running = false;
    uint8_t steps = 16;         // the pattern's length
    uint32_t pos_q16 = 0;
    // A pass to record (StepSeq::record_pass) starts at this block's first sample, where its first step sounds (rec_start = 1 in that
    // block only), and lasts rec_frames: its steps at the tempo it starts at. The recording tap (CaptureTap::arm_seq) begins on it.
    uint8_t rec_start = 0;
    uint32_t rec_frames = 0;
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

constexpr int kSendBuses = 2;   // voice buses beyond the two main ones (ProcessCtx.send_l / send_r)
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
    int32_t *bus2_l = nullptr, *bus2_r = nullptr;   // the second voice bus (VoiceOut's input 2, BusIn with bus 1): the rack's branch 2 (ADR-041)
    int32_t *send_l[kSendBuses] = {}, *send_r[kSendBuses] = {};   // the send buses (VoiceOut's inputs 3, 4; BusIn with bus 2, 3): a voice module
                                                    // cabled into a shared one (the Ritual Machine's audio cables, its UI.md step 15)
    const Transport *transport = nullptr;           // the machine clock at the start of this block
    const q15 *silence = nullptr;                   // what an unplugged input reads: a module may tell it from a silent signal (the
                                                    //   catalog's modules read a default there: framework/modular/engine_modules.h)
    const float *fsilence = nullptr;                // the same for a Sig::F32 module (Ports::fin)
    const SeqPos *seq = nullptr;                    // the step sequencer's place at the end of this block
    // The voice buses whose right half is the left one this block (bit 0 bus_l / bus_r, 1 bus2, 2 + k send k): every voice added so far was
    // centred, so the right buffer was not written (it is stale) and reads as the left one. VoiceOut copies left to right and clears the bit
    // before it adds a panned voice. nullptr: both buffers always written (the tests, the benches). The engine's (2026-11-19): a voice's
    // add to a bus is a third cheaper, the right buses are not cleared.
    uint8_t *bus_mono = nullptr;
};

// The module's blocks: in / mod / out for a Sig::Q15 module, fin / fmod / fout (the same pointers, as float blocks) for a Sig::F32 one.
struct Ports {
    union { const q15 *in[kMaxIn]; const float *fin[kMaxIn]; };
    union { const q15 *mod[kMaxParams]; const float *fmod[kMaxParams]; };
    union { q15 *out[kMaxOut]; float *fout[kMaxOut]; };
    // ADR-050: bit p set = everything that feeds mod[p] (in[p] for in_slow) is slow in this block (envelopes, LFOs under 100 Hz,
    // constants): the module may read it once per block, along a ramp to its last sample (mod_ramp below), instead of per sample.
    // 0 (the default, a Ports built by hand): read per sample.
    uint16_t mod_slow = 0;
    uint8_t in_slow = 0;
};

#define SC_MOD_RATES 1                  // (ADR-050 step 1 is in: Ports::mod_slow / in_slow, ModuleInfo::rate)
inline bool slow_mod(const Ports &p, int j) { return (p.mod_slow >> j) & 1u; }
inline bool slow_in(const Ports &p, int k) { return (p.in_slow >> k) & 1u; }

// A slow modulation read once per block, as a straight line from where the previous block ended to this block's last sample: exact at
// every block's end, no step inside it. `last` is the module's state (the previous block's end); `fresh` after a reset (the line
// starts at this block's first sample). Gives the value at the first sample and the step per sample, both << 8 (Q8: a 1/256 LSB step).
struct ModRamp {
    int32_t last = 0;
    bool fresh = true;
    void begin(const q15 *m, int n, int32_t *v_q8, int32_t *step_q8) {
        const int32_t end = m[n - 1];
        if (fresh || n < 2) {                                // from this block's first sample to its last
            *step_q8 = n > 1 ? ((end - m[0]) * 256) / (n - 1) : 0;
            *v_q8 = m[0] * 256;
        } else {                                             // from the previous block's last sample (one step before this block's first)
            *step_q8 = ((end - last) * 256) / n;
            *v_q8 = last * 256 + *step_q8;
        }
        fresh = false;
        last = end;
    }
    // a block read per sample (the source was fast): where it ended, so a later slow block's line starts there
    void note(const q15 *m, int n) { last = m[n - 1]; fresh = false; }
};
// (a value scaled from a Q8 ramp: (v_q8 / 256) * range / 32768, exact for a whole v)
inline int32_t mod_scaled_q8(int32_t v_q8, int32_t range) { return static_cast<int32_t>((static_cast<int64_t>(v_q8) * range) >> 23); }

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
    // [AUDIO] a hit of the pads' sequencer routed to this module (Engine::pseq_route): `lane` 0..7 struck at `level` 0..15, or let go
    // (release: the hit's length is over). Modules that play no hits ignore it.
    virtual void hit(int /*lane*/, int /*level*/, bool /*release*/) {}
    virtual void process(const ProcessCtx &ctx, const Ports &p) = 0;   // [AUDIO]
    // [AUDIO] Rate::Dynamic modules only: whether the block just processed was fast (audio rate), read by the engine after process()
    virtual bool fast_out() const { return true; }
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

// A type's table entry. Its description is read from a short-lived instance: info() returns a function-local static, so the
// description outlives it. (A static probe instance per type held ~20 KB of internal RAM on the board for 106 types.) The instance
// is built in a malloc'd buffer that is freed again without running its destructor, as the static probe's never ran either.
template <typename T>
ModuleType module_type_of() {
    void *raw = std::malloc(sizeof(T) + alignof(T));
    if (!raw) return {nullptr, nullptr};
    const uintptr_t at = (reinterpret_cast<uintptr_t>(raw) + alignof(T) - 1) & ~static_cast<uintptr_t>(alignof(T) - 1);
    const ModuleInfo *info = &(new (reinterpret_cast<void *>(at)) T())->info();
    std::free(raw);
    return {info, &create_module<T>};
}

// Module type table: type ids are chosen by the application (builtins use the low ids, see builtin.h).
class Registry {
public:
    static constexpr int kMax = 192;        // (128..191: the modular catalog's modules, framework/modular/engine_modules.h)
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
