#pragma once
// Control-side conversion of the application's editable FM patch (core/dx7.h, floats and milliseconds) into the engine's
// fixed-point Dx7Patch. Floats are fine here: this runs on the UI thread, never in the audio path.
#include "core/dx7.h"
#include "engine/modules/dx7_voice.h"

namespace sc {

Dx7Patch dx7_convert(const dx7_patch_t &p);

}  // namespace sc
