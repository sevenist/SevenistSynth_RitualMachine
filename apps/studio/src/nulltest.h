#pragma once
// The null test of a rebuild: a SynthLab synth (at its default values) and a patch (at its own values) play the same notes, rendered
// offline (no engine: the synth's Voice and Fx called directly, the patch through the runner); the difference is measured. The depth is
// how far under the synth the difference lies: 60 dB and more, nobody hears it (SynthLab's Kick Lite rule); under 20 dB, a resemblance.
//
// The notes: C3 then C4, each held 1 s and let go for 1 s, at velocity 100 (of 127), at 48 kHz, in blocks of 32 (the engine's).
#include <string>
#include "lab.h"
#include "modular/patch.h"

namespace studio {

struct NullResult {
    float depth_db = 0.f;           // 20 log10(rms of the synth / rms of the difference)
    float synth_rms_db = -200.f, patch_rms_db = -200.f;
    float residual_peak_db = -200.f;
};

NullResult null_test(const lab::Synth &s, const mod::Patch &p);
// The rebuilt patches (apps/studio/library/<id>.patch, id: the synth's name in lower case, "_" for spaces): its path, and whether there is one.
std::string library_path(const lab::Synth &s);
bool has_rebuild(const lab::Synth &s);

}  // namespace studio
