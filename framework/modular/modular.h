#pragma once
// The modular catalog: float modules gathered from SynthLab's synths (an oscillator, the Kick's body, noise, the hat's metal, envelopes, the
// Snare's ratchet, filters, an EQ, the drive, a VCA, a mixer, a delay, the Kick's room, glue and the Snare's gate ...), each one a small
// struct with a description: its inputs, outputs and values. A patch (patch.h) wires them; the runner plays it; a patch can also be turned
// into a SynthLab synth or an engine graph (apps/studio: the exports).
//
// A module's description fits the engine's own module limits (engine/core/module.h: 4 inputs, 8 outputs, 16 values, Voice or Global
// scope), so that each one can become an engine module. Signals are float blocks of up to kMaxFrames samples, full scale +-1: audio, control
// and envelopes alike (an envelope is 0..1). Values are in the units shown (Hz, ms, %, dB), as SynthLab's (lab.h Param), read once a block.
//
// A module is a struct with
//   void start(const Ctx &c);                     a note starts on its voice (Voice modules; the state a hit resets)
//   void process(const Ctx &c, const IO &io);     one block: io.in[i] (never null), io.out[o], io.p[values]
// and is described by a ModuleDef (catalog.cpp). The rules of the audio thread hold: no allocation, no lock, no print.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>

namespace mod {

constexpr int kMaxIn = 4, kMaxOut = 8, kMaxParams = 16;     // the engine's limits
constexpr int kMaxFrames = 64;                              // a block, at most (the runner cuts longer ones)

// How a value moves (SynthLab's lab::Curve): Lin in equal steps, Exp by an equal ratio (Hz, ms; min > 0), Int whole numbers (with labels:
// a name for each, from min).
enum Curve : uint8_t { Lin, Exp, Int };

struct Param {
    const char *name;                   // no spaces (the patch files name values by it)
    float min, max, def;
    const char *unit = nullptr;
    Curve curve = Lin;
    const char *const *labels = nullptr;
};

enum class Scope : uint8_t { Voice, Global, Both };         // where it may sit: a voice (copied per voice), the FX (once), either

// What an input reads when no cable comes in: silence, 1 (a VCA's level), the voice's gate (an ADSR's), a pulse when the note starts, or
// what the module's first input reads (a gate's key: its own input).
enum InDefault : uint8_t { kZero, kOne, kGate, kTrig, kIn0, kPitch };   // kPitch: the voice's pitch, octaves over middle C

struct Ctx {
    float sr = 48000.f, inv_sr = 1.f / 48000.f;
    float hz = 261.63f;                 // the voice's pitch (Voice scope; the FX: middle C)
    float velocity = 1.f;               // 0..1
    bool gate = false;                  // the key is held
};

// The FX area's note: it plays once, on the sum, so it has no voice; it follows the last note played instead (its pitch and velocity),
// its gate held while any key is held, a start at each new note (a Perc's Trig, an ADSR's gate, a Body's pitch on the FX area). A
// stand-in until a mono mode is designed: the voices write it, the FX reads it, on the audio thread, the voices first in each block.
struct LastNote {
    float hz = 261.63f, velocity = 1.f;
    bool any_gate = false;              // a voice rendered with its key held since the FX's last block
    uint32_t starts = 0;                // notes started
    const void *last = nullptr;         // the voice of the last note
    void note_start(const void *voice, float note_hz, float vel) { last = voice; hz = note_hz; velocity = vel; starts++; any_gate = true; }
    void voice_block(const void *voice, bool gate, float note_hz) { if (gate) any_gate = true; if (voice == last) hz = note_hz; }
    void voice_gone(const void *voice) { if (voice == last) last = nullptr; }
    // The FX's block: c gets the last note; true when a note started since the FX's last block (`seen`: the FX's count).
    bool fx_block(Ctx &c, uint32_t &seen) {
        c.hz = hz;
        c.velocity = velocity;
        c.gate = any_gate;
        any_gate = false;
        const bool started = starts != seen;
        seen = starts;
        return started;
    }
};

struct IO {
    const float *in[kMaxIn];
    float *out[kMaxOut];
    const float *p;                     // the module's values, in units, modulation applied
    int frames;                         // 1..kMaxFrames
};

struct ModuleDef {
    const char *name;                   // no spaces: "Osc", "Filter"
    const char *category;               // "In / Out", "Sources", "Envelopes", "Filters", "Shapers", "Utilities", "Effects"
    const char *doc;                    // one line: what it does (shown in the studio)
    Scope scope;
    uint8_t n_in, n_out, n_param;
    const char *in_name[kMaxIn];
    uint8_t in_def[kMaxIn];             // InDefault
    const char *out_name[kMaxOut];
    Param param[kMaxParams];
    const char *engine;                 // the engine module that does the same (engine/modules: "Osc", "Filter" ...), nullptr: none yet
    const char *origin;                 // where it comes from ("kick.cpp Room", "lab_dsp Adsr")
    const char *cpp;                    // its struct in modules.h ("OscM"): what a generated synth declares
    size_t size, align;
    void (*construct)(void *);
    void (*destroy)(void *);
    void (*start)(void *, const Ctx &);
    void (*process)(void *, const Ctx &, const IO &);
    // Set by a module built at run time whose state depends on the area it sits in (an engine module: its Voice or its Global type,
    // engine_adapter.h); the runner then calls it instead of construct. nullptr for the catalog's own modules.
    void (*construct_in)(void *, int area) = nullptr;
};

template <typename M>
struct Ops {
    static void construct(void *m) { new (m) M(); }
    static void destroy(void *m) { static_cast<M *>(m)->~M(); }
    static void start(void *m, const Ctx &c) { static_cast<M *>(m)->start(c); }
    static void process(void *m, const Ctx &c, const IO &io) { static_cast<M *>(m)->process(c, io); }
};
#define MOD_OPS(M) #M, sizeof(M), alignof(M), mod::Ops<M>::construct, mod::Ops<M>::destroy, mod::Ops<M>::start, mod::Ops<M>::process

// The catalog (catalog.cpp), in the order the studio lists it.
const ModuleDef *const *catalog(int *count);
// Adds a module to the catalog (after its own: engine_adapter.cpp adds the engine's). False: full, or the name is taken.
bool catalog_add(const ModuleDef *d);       // (filed after the last module of its category: the kinds stay together)
const ModuleDef *find_module(const char *name);
int  param_index(const ModuleDef &d, const char *name);     // -1: none
int  in_index(const ModuleDef &d, const char *name);
int  out_index(const ModuleDef &d, const char *name);

// A value as a position 0..1 along its range (Exp: along its logarithm) and back: what a knob's travel and a modulation cable move.
// (inline: a synth generated from a patch needs the headers only)
inline float to_norm(const Param &p, float v) {
    if (p.max <= p.min) return 0.f;
    float n;
    if (p.curve == Exp && p.min > 0.f) n = std::log(v / p.min) / std::log(p.max / p.min);
    else n = (v - p.min) / (p.max - p.min);
    return n < 0.f ? 0.f : (n > 1.f ? 1.f : n);
}
inline float from_norm(const Param &p, float n) {
    n = n < 0.f ? 0.f : (n > 1.f ? 1.f : n);
    if (p.curve == Exp && p.min > 0.f) return p.min * std::pow(p.max / p.min, n);
    const float v = p.min + n * (p.max - p.min);
    return p.curve == Int ? std::floor(v + 0.5f) : v;
}

}  // namespace mod
