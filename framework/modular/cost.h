#pragma once
// What the synth exported from a patch (apps/studio's Export > Synth) costs on the board, in CPU cycles per 32-frame block, from the
// cost table measured there (module_costs.h). SynthLab Studio's cycles wallet shows it against the board's budget so that the patch's
// designer can choose its voice count (Patch::voices).
//
// A voice: the engine's share of a voice, the generated voice's own work, each module of the VOICE area with its values (a module whose
// slowest value is set costs its worst), each modulation cable (by the curve of the value it moves), each input that sums cables. The FX
// area the same, once. Then the engine's fixed cost. An estimate: the table's modules each measured alone, their inputs fed a sine; a
// module the table does not have (an engine module: "RM ...") counts 0 and is reported.
#include <cstdint>
#include "modular/module_costs.h"
#include "modular/patch.h"

namespace mod {

struct PatchCost {
    uint32_t voice = 0;                 // one voice (VOICE area, cables, the voice's glue, the engine's share)
    uint32_t fx = 0;                    // the FX area, once (0: nothing but Voices -> Output)
    uint32_t fixed = 0;                 // the engine with no note
    uint32_t total = 0;                 // fixed + voices x voice + fx
    uint32_t worst = 0;                 // the same with every module at its slowest values
    uint32_t budget = 0;                // the board's cycles a block
    uint32_t voice_bytes = 0;           // the modules' state in one voice
    uint32_t fx_bytes = 0;
    int unmeasured = 0;                 // modules the table does not have (counted as 0)
};

// One module with its values (worst: at its slowest values, whatever they are). 0 when the table does not have it.
uint32_t node_cycles(const Node &n, bool worst = false);
// The cables into node n (modulations by curve, summed inputs): what the generated code adds for it.
uint32_t cable_cycles(const Patch &p, const Node &n);
PatchCost patch_cost(const Patch &p, int voices);
// The most voices (1..ceiling) whose total fits the budget; 0: not even one.
int voices_in_budget(const Patch &p, int ceiling);

}  // namespace mod
