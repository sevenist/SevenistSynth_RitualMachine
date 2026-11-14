// The synths of SynthLab, in the order the screen offers them. A new synth: its file in this folder (copy basic.cpp), then one line
// here and one in the list.
#include "lab.h"

extern const lab::Synth synth_basic;
extern const lab::Synth synth_fm2;
extern const lab::Synth synth_kick;
extern const lab::Synth synth_kick_lite;
extern const lab::Synth synth_snare;
extern const lab::Synth synth_drumlab;
// (synths exported from a patch by SynthLab Studio are added above this line)
#ifdef LAB_STUDIO
extern lab::Synth synth_modular;                        // apps/studio's: it plays a patch (only the studio builds it)
#endif

namespace lab {

const Synth *const kSynths[] = {
    &synth_basic,
    &synth_fm2,
    &synth_kick,
    &synth_kick_lite,
    &synth_snare,
    &synth_drumlab,
    // (exported synths: above this line)
#ifdef LAB_STUDIO
    &synth_modular,
#endif
};
const int kSynthCount = static_cast<int>(sizeof kSynths / sizeof kSynths[0]);

}  // namespace lab
