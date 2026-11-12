// The synths of SynthLab, in the order the screen offers them. A new synth: its file in this folder (copy basic.cpp), then one line
// here and one in the list.
#include "lab.h"

extern const lab::Synth synth_basic;
extern const lab::Synth synth_fm2;
extern const lab::Synth synth_kick;
extern const lab::Synth synth_kick_lite;

namespace lab {

const Synth *const kSynths[] = {
    &synth_basic,
    &synth_fm2,
    &synth_kick,
    &synth_kick_lite,
};
const int kSynthCount = static_cast<int>(sizeof kSynths / sizeof kSynths[0]);

}  // namespace lab
