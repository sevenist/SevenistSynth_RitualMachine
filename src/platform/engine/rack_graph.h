#pragma once
// Rack -> engine graph (ADR-026). Translates the application's data (rack_t with its general settings, the amp
// envelope in synth_params_t) into a GraphDesc for the engine, with the same signal semantics as before:
//   audio modules form one chain per row in slot order; a source (OC) adds to the running signal, a processor (FL, SA) acts on
//   everything to its left; modulators (LF, EN, an OC with a target) are cables with depth into the target's parameter.
// Voice:  NoteIn -> [branch b] -> amp VCA (amp envelope) -> VoiceOut input b (voice bus b)     FM mode: NoteIn -> Dx7 -> VoiceOut
//         (Para: the branch's shared part runs after its BusIn)
// Master: each branch's output at its MIX level / pan -> row M's modules in slot order -> MasterOut (ADR-041)
// A branch's shared part (after its Para point) and row M use one builder: mono until a stereo effect, then a node per channel.
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
    static constexpr int kMaxConv = 4;          // Convolver nodes and the IR each one plays (sent as taps with set_blob, see fx2_modules.h)
    int conv_count = 0;
    int conv_node[kMaxConv] = {}, conv_ir[kMaxConv] = {}, conv_len[kMaxConv] = {};
};

enum RackNode : int {
    RN_NOTE = 1, RN_AMP_ENV, RN_AMP_VCA, RN_VOICE_OUT, RN_BUS, RN_MASTER, RN_DX7,
    RN_STR, RN_STR_FLT_L, RN_STR_FLT_R,     // the Strings type: its voice module and the shared stereo filter
    RN_GATE_IN, RN_PARA_GATE, RN_PARA_AMP_ENV, RN_PARA_AMP_VCA,   // Para: the keyboard gate, the voice gate, the shared amp
    RN_AMP_VCA2, RN_PARA_GATE2, RN_PARA_AMP_ENV2, RN_PARA_AMP_VCA2, RN_BUS2,   // the same for the rack's branch 2 (ADR-041; the amp env is shared)
    RN_PARA_AMP_VCA_R, RN_PARA_AMP_VCA2_R,                           // a branch's shared amp on the right channel, once its shared part is stereo
    RN_MODULES = 32,         // module nodes: RN_MODULES + 2 * (rack id % 100) (+1 for the helper node of the same module, or the right channel of a pair)
    RN_EXTRA = 232           // RN_EXTRA + slot: a third node of a module (the envelope of a filter that is a pair), up to 255
};

// `reg` supplies the default parameters of each module type. Returns false if the graph does not fit the limits.
// `slot_of_file[i]` is the sample-bank slot of catalog file i (-1 = not loaded), `n_files` its length (may be null / 0).
bool rack_graph_build(const rack_t &rack, const synth_params_t &params, const Registry &reg, RackGraph &out,
                      const int16_t *slot_of_file = nullptr, int n_files = 0);

}  // namespace sc
