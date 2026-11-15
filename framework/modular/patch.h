#pragma once
// A patch: modules of the catalog (modular.h) and the cables between them, in two areas, as the engine's scopes: the VOICE area (copied for
// each voice: its Note in, its VoiceOut out) and the FX area (once, on the sum of the voices: Voices in, Output out). A cable goes from an
// output to an input (audio: the signal times its depth, the cables into one input summed) or to a value (modulation: the source's mean
// over the block, times its depth, moves the value along its range: depth 1 = the whole range). Plain data, fixed sizes: no allocation.
//
// The runner plays one area of a patch: it builds the modules in a block of memory the caller gives (a voice's), orders them (a module
// after the ones it listens to; a loop is broken where it closes: the cable that closes it reads the block before) and runs them a block
// at a time. The values come as a synth's list (patch_slots): the Macros first (a section MACROS, a row each, named by the Macro's name),
// then a section per other module and its values, in the units shown.
//
// The text form (a .patch file):
//   patch "Hollow Kick"
//   voices 4                                        the synth's voice count (its designer's, from the cycles wallet; absent: 6)
//   node 1 voice Note 20 40                         id, area, module, x y (where the studio draws it)
//   node 2 voice Body 180 40 Tune=48 Harm=10 ...    then its values by name (the ones not given: their defaults)
//   node 7 voice Macro 20 200 "Punch" Value=40      a name in quotes (a Macro's: the synth's parameter)
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
constexpr int kMaxPatchVoices = 6;      // the most voices a synth plays on SynthLab (apps/synthlab/src/synth.cpp kVoices)

enum Area : uint8_t { kVoiceArea, kFxArea };

struct Node {
    uint8_t id = 0;                     // unique, 1..255, kept across edits (the cables and the files name it)
    uint8_t area = kVoiceArea;
    const ModuleDef *def = nullptr;
    float v[kMaxParams] = {};           // its values, in units
    int16_t x = 0, y = 0;               // where the studio draws it
    char name[16] = "";                 // a Macro's name ("": "Macro <id>")
    // Its other implementation (the merged list: a float module or its engine twin, the studio's switch), with the values it had there:
    // switching back gives them again. Not in the text form.
    const ModuleDef *alt = nullptr;
    float alt_v[kMaxParams] = {};
    uint8_t group = 0;                  // the switch group it is in (Group::id; 0: none)
};

// Switch groups (the user, 2026-11-15: "a container that can be bypassed and offer a switch widget"; both a bypass and a selector).
// A group is a set of modules of one area.
//   - A bypass group has its own switch, On / Bypass. Bypassed, its modules do not run (no CPU), and the signal coming into the group
//     from outside (the cables into its modules from the others) goes out where the group's own outputs went; a group with nothing
//     coming in goes silent.
//   - The groups of a selector share its switch, which picks the one that plays (by their order in the patch); the others do not run
//     and send silence.
// The switches are the synth's values: a section SWITCHES after the Macros (patch_slots), a row each (Bypass / On; a selector: its
// groups' names), so a layout shows them as a switch, a list or a knob, and an exported synth keeps them.
constexpr int kMaxGroups = 8, kMaxSelectors = 4;
constexpr const char *kSwitchSection = "SWITCHES";
struct Group {
    uint8_t id = 0;                     // 1..255, kept across edits
    char name[16] = "";
    uint8_t selector = 0;               // 0: a bypass group; else the id of the selector it is a choice of
    bool on = true;                     // a bypass group's switch in the patch (the list's value overrides it)
};
struct Selector {
    uint8_t id = 0;
    char name[16] = "";
    uint8_t value = 0;                  // the rank of its group that plays (in the patch's order of its groups)
};

struct Cable {
    uint8_t src = 0, src_port = 0;      // node id, output
    uint8_t dst = 0, dst_port = 0;      // node id, input (or value)
    bool to_param = false;
    float depth = 1.f;                  // -1..1 onto a value; any onto an input
};

struct Patch {
    char name[32] = "Init";
    int voices = kMaxPatchVoices;       // 1..kMaxPatchVoices: what the synth exported from it plays at most (its CPU budget, cost.h)
    Node node[kMaxNodes];
    int n_nodes = 0;
    Cable cable[kMaxCables];
    int n_cables = 0;
    Group group[kMaxGroups];
    int n_groups = 0;
    Selector selector[kMaxSelectors];
    int n_selectors = 0;

    int index_of(int id) const;
    Node *find(int id);
    const Node *find(int id) const;
    Node *add(const ModuleDef *d, int area, int x, int y);     // a new id, the defaults; nullptr when full
    void remove(int id);                                        // with its cables
    // A cable (the same one again: its depth is changed). False: full, an unknown port, or across areas.
    bool connect(int src, int src_port, int dst, int dst_port, bool to_param, float depth);
    void disconnect(int k);
    void clear();                       // no module, no cable, no group, the voices back to kMaxPatchVoices
    // Switch groups. make_group: a bypass group of these modules (one area; a module leaves the group it was in), or a choice of
    // selector `sel` (0: a bypass group). Returns its id, 0 when full or the modules are of two areas. ungroup: its modules free, the
    // group gone (and its selector, when it was its last group).
    int  make_group(const int *ids, int n, const char *name, int sel = 0);
    void ungroup(int id);
    int  add_selector(const char *name);    // its id; 0 when full
    Group *find_group(int id);
    const Group *find_group(int id) const;
    Selector *find_selector(int id);
    const Selector *find_selector(int id) const;
    void init_default();                // Note -> Osc -> Filter -> VCA (ADSR) -> VoiceOut; Voices -> Output
};

bool fits_area(const ModuleDef *d, int area);      // its scope allows that area (a Note per voice, the Voices on the sum)

// "Osc 2": the module's name and its rank among the patch's modules of that kind (1 when it is the only one: "Osc").
void node_title(const Patch &p, int index, char *out, size_t size);

// The Macros: a patch's parameters for the player of the synth made from it (the Macro module, modules.h MacroM).
constexpr const char *kMacroSection = "MACROS";
bool is_macro(const Node &n);
void macro_name(const Node &n, char *out, size_t size);    // its name, or "Macro <id>"
int  macro_count(const Patch &p);
// The first Macro whose name is the same as another one's (case aside), -1: none (an exported synth needs them apart).
int  macro_twin(const Patch &p);

// The values as a synth's list. Value j of node k is at base[k] + 1 + j: the Macros first, in a section MACROS at 0 (the Macro's row at
// base[k] + 1, named by macro_name), then the switches (below), then a section per other module (its title at base[k]) and its values.
// Returns the list's length (at most kMaxSlots: the modules beyond it get base -1, they play their values in the patch).
int patch_slots(const Patch &p, int *base);
// The switches in the list: a section SWITCHES (its title at *first - 1), then a row each: the bypass groups in the patch's order, then
// the selectors. *count 0: none (no section).
void patch_switches(const Patch &p, int *first, int *count);
// A switch's row (rank: 0.. as above): its name and its choices (a bypass group: "Bypass", "On"; a selector: its groups' names, *n of
// them; names points into the patch's groups, up to kMaxGroups).
void switch_row(const Patch &p, int rank, const char **name, const char **names, int *n);
// Which groups play (their index in p.group): the list's switches (values: the synth's list; nullptr: the patch's own).
void groups_on(const Patch &p, const float *values, bool *on);

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
        int8_t grp;                         // its switch group (an index in grp_; -1: none)
        uint8_t exit;                       // the outputs that leave its group (bit o): a bypassed group's input goes out there
    };
    // A switch group as the runner plays it: the cables into it from outside (summed: what a bypass lets through)
    struct GroupRt {
        Ref entry[kRefs];
        uint8_t n_entry;
    };
    void run_chunk(const Ctx &c, const float *values, const float *in, float *out, int frames, float &peak);
    Inst inst_[kMaxNodes] = {};
    int n_ = 0;
    GroupRt grp_[kMaxGroups] = {};
    Group gdef_[kMaxGroups];                // the patch's groups and selectors (copies), where its switches are in the list
    Selector sdef_[kMaxSelectors];
    int n_gdef_ = 0, n_sdef_ = 0, sw_first_ = 0;
    float entry_[kMaxFrames] = {};
    bool trig_ = false;
    float zero_[kMaxFrames] = {}, one_[kMaxFrames] = {}, gate_[kMaxFrames] = {}, trigbuf_[kMaxFrames] = {}, pitchbuf_[kMaxFrames] = {};
};

}  // namespace mod
