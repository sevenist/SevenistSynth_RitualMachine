#pragma once
// SynthLab's DSP parts. Since 2026-11-14 they are in the library, shared with the modular catalog: framework/modular/mdsp.h (namespace
// mod). This header keeps the synths' names: lab::Osc, lab::Svf, lab::flush ... are mod's, and LAB_COLD is MOD_COLD.
#include "modular/mdsp.h"

#define LAB_COLD MOD_COLD

namespace lab {
using namespace mod;
}  // namespace lab
