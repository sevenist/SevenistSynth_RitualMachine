#pragma once
// A patch: modules of the catalog (modular.h) and the cables between them, in two areas, as the engine's scopes: the VOICE area (copied for
// each voice: its Note in, its VoiceOut out) and the FX area (once, on the sum of the voices: Voices in, Output out). A cable goes from an
// output to an input (audio: the signal times its depth, the cables into one input summed) or to a value (modulation: the source's mean
// over the block, times its depth, moves the value along its range: depth 1 = the whole range). Plain data, fixed sizes: no allocation.
//
// The runner plays one area of a patch: it builds the modules in a block of memory the caller gives (a voice's), orders them (a module
// after the ones it listens to; a loop is broken where it closes: the cable that closes it reads the block before) and runs them a block
// at a time. The values come as a synth's list (patch_slots): a section per module, then its values, in the units shown.
//
// The text form (a .patch file):
//   patch "Hollow Kick"
//   node 1 voice Note 20 40                         id, area, module, x y (where the studio draws it)
//   node 2 voice Body 180 40 Tune=48 Harm=10 ...    then its values by name (the ones not given: their defaults)
//   cable 1.Trig 3.Trig 1                           output -> input, depth
//   mod 3.Env 2.Level 0.5                           output -> value, depth (-1..1)
#include <cstddef>
#include <cstdint>
#include "modular/modular.h"

namespace mod {

constexpr int kMaxNodes = 48;
constexpr int kMaxCables = 192;
constexpr int kMaxFanIn = 8;            // cables into one input, or onto one value
constexpr int kMaxSlots = 256;          // the values of a patch as a synth's list (titles included)

enum Area : uint8_t { kVoiceArea, kFxArea };

struct Node {
    uint8_t id = 0;                     // unique, 1..255, kept across edits (the cables and the files name it)
    uint8_t area = kVoiceArea;
    const ModuleDef *def = nullptr;
    float v[kMaxParams] = {};           // its values, in units
    int16_t x = 0, y = 0;               // where the studio draws it
};

struct Cable {
    uint8_t src = 0, src_port = 0;      // node id, output
    uint8_t dst = 0, dst_port = 0;      // node id, input (or value)
    bool to_param = false;
    float depth = 1.f;                  // -1..1 onto a value; any onto an input
};

struct Patch {
    char name[32] = "Init";
    Node node[kMaxNodes];
    int n_nodes = 0;
    Cable cable[kMaxCables];
    int n_cables = 0;

    int index_of(int id) const;
    Node *find(int id);
    const Node *find(int id) const;
    Node *add(const ModuleDef *d, int area, int x, int y);     // a new id, the defaults; nullptr when full
    void remove(int id);                                        // with its cables
    // A cable (the same one again: its depth is changed). False: full, an unknown port, or across areas.
    bool connect(int src, int src_port, int dst, int dst_port, bool to_param, float depth);
    void disconnect(int k);
    void clear();
    void init_default();                // Note -> Osc -> Filter -> VCA (ADSR) -> VoiceOut; Voices -> Output
};

bool fits_area(const ModuleDef *d, int area);      // its scope allows that area (a Note per voice, the Voices on the sum)

// "Osc 2": the module's name and its rank among the patch's modules of that kind (1 when it is the only one: "Osc").
void node_title(const Patch &p, int index, char *out, size_t size);

// The values as a synth's list: node k's section title at base[k], its values after it. Returns the list's length (at most kMaxSlots:
// the modules beyond it get base -1, they play their defaults).
int patch_slots(const Patch &p, int *base);

// The text form (see above). to_text returns the length written (the text is cut at cap - 1); from_text replaces the patch (false: not a
// patch, `err` says why; lines it cannot read are skipped and counted in *skipped).
int  patch_to_text(const Patch &p, char *buf, int cap);
bool patch_from_text(Patch &p, const char *text, char *err, int err_cap, int *skipped = nullptr);

class Runner {
public:
    Runner() = default;
    ~Runner() { clear(); }
    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;

    static size_t bytes_for(const Patch &p, int area);          // the memory build() needs for that area
    // Builds `area` of `p` in mem (16-byte aligned, `bytes` long; it stays the caller's). False: it does not fit (the runner is then empty:
    // silence). `base` = patch_slots's.
    bool build(const Patch &p, int area, void *mem, size_t bytes);
    void clear();                                               // the modules destroyed
    bool empty() const { return n_ == 0; }
    void start(const Ctx &c);                                   // a note starts (its Trig pulse in the next block)
    // `frames` samples: values = the synth's list (nullptr: every module at its patch values), in = the voices' sum (the FX area; nullptr
    // in a voice). The area's output is ADDED to out. Returns the largest |sample| added.
    float process(const Ctx &c, const float *values, const float *in, float *out, int frames);

private:
    static constexpr int kRefs = 16;        // cables into one module (its inputs and its values together)
    struct Ref { const float *buf; float depth; uint8_t port; bool to_param; };
    struct Inst {
        const ModuleDef *def;
        void *state;
        float *out[kMaxOut];
        float *sum[kMaxIn];                 // where several cables into an input are summed
        const float *in[kMaxIn];            // what the module reads this block
        Ref ref[kRefs];
        uint8_t n_ref;
        int base;                           // its section in the value list (-1: none)
        float v[kMaxParams];                // its patch values (when no list is given)
        bool is_out, is_voices;
    };
    void run_chunk(const Ctx &c, const float *values, const float *in, float *out, int frames, float &peak);
    Inst inst_[kMaxNodes] = {};
    int n_ = 0;
    bool trig_ = false;
    float zero_[kMaxFrames] = {}, one_[kMaxFrames] = {}, gate_[kMaxFrames] = {}, trigbuf_[kMaxFrames] = {}, pitchbuf_[kMaxFrames] = {};
};

}  // namespace mod
