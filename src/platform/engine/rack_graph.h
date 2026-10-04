#pragma once
// Rack -> engine graph (ADR-026). Translates the application's data (rack_t with its general settings, the amp
// envelope in synth_params_t) into a GraphDesc for the engine, with the same signal semantics as before:
//   audio modules form one chain in slot order; a source (OC) adds to the running signal, a processor (FL, SA) acts on
//   everything to its left; modulators (LF, EN, an OC with a target) are cables with depth into the target's parameter.
// Voice:  NoteIn -> [chain] -> amp VCA (amp envelope) -> VoiceOut      FM mode: NoteIn -> Dx7 -> VoiceOut
// Master: BusIn -> Chorus -> Delay -> Reverb -> MasterOut (always present, driven by the FX tabs; "off" means mix 0)
// Node ids are stable (derived from the rack module ids), so a rebuild keeps the state of untouched modules.
#include "core/rack.h"
#include "core/synth_params.h"
#include "engine/core/graph.h"
#include "engine/modules/dx7_voice.h"
#include "engine/modules/motion_seq.h"

namespace sc {

struct RackGraph {
    GraphDesc g;
    bool fm = false;
    Dx7Patch fm_patch{};        // sent to the Dx7 node with set_blob (it does not fit in the parameter list)
    int dx7_node = 0;
    int ms_count = 0;                           // motion sequencers in the graph: node id and lane data (sent with set_blob) of each
    int ms_node[MS_POOL] = {};
    MotionBlob ms_blob[MS_POOL] = {};
};

enum RackNode : int {
    RN_NOTE = 1, RN_AMP_ENV, RN_AMP_VCA, RN_VOICE_OUT, RN_BUS, RN_MASTER, RN_DX7,
    RN_FX = 240,             // master effects: RN_FX + 2 * slot (a Drive slot uses a second node for the right channel)
    RN_MODULES = 32          // module nodes: RN_MODULES + 2 * (rack id % 100) (+1 for the helper node of the same module)
};

// `reg` supplies the default parameters of each module type. Returns false if the graph does not fit the limits.
// `slot_of_file[i]` is the sample-bank slot of catalog file i (-1 = not loaded), `n_files` its length (may be null / 0).
bool rack_graph_build(const rack_t &rack, const synth_params_t &params, const Registry &reg, RackGraph &out,
                      const int16_t *slot_of_file = nullptr, int n_files = 0);

}  // namespace sc
