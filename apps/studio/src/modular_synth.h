#pragma once
// The studio's Modular synth: a SynthLab synth (lab.h) that plays a patch (framework/modular/patch.h). Its value list is the patch's (a
// section per module, titled "<module> <id>", then its values), so the SOUND tab's knobs and macros and the LAYOUT tab work on a patch as
// on any synth. Only the studio lists it (SynthLab's synth_list.cpp, built with -DLAB_STUDIO): the board's SynthLab does not carry its memory.
//
// The patch's structure reaches the voices by modular_publish (two copies, an atomic index: a voice rebuilds at its next block); the
// values travel as any synth's (synth_set_value).
#include "lab.h"
#include "modular/patch.h"

extern lab::Synth synth_modular;

namespace studio {

// The memory a Modular voice and its FX build their area in (a patch larger than that plays nothing: the PATCH tab says so).
constexpr size_t kModularVoiceBytes = 2u << 20, kModularFxBytes = 4u << 20;

int  modular_index();                                   // its place in lab::kSynths
// The patch becomes the synth's: its value list (names, ranges, defaults) and its structure for the voices. Call it from the studio's
// thread after any change of modules or cables (then send the values: modular_push_values).
void modular_publish(const mod::Patch &p);
void modular_push_values(const mod::Patch &p);          // the patch's values -> the synth's (synth_set_value; the Modular synth selected)
void modular_pull_values(mod::Patch &p);                // the synth's values -> the patch (before an edit changes the list's layout)
int  modular_slot(const mod::Patch &p, int node_index, int param);  // the value's index in the synth's list (-1: beyond the list)

}  // namespace studio
