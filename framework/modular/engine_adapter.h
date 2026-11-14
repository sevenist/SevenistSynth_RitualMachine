#pragma once
// The engine's modules in the modular catalog: the Ritual Machine's own modules (framework/engine/modules: its oscillators and
// oscillator engines, filters, envelopes, LFOs, shapers, its effects ...) played inside a patch exactly as the engine plays them. Each
// catalog entry ("RM" + the engine's name, filed under its kind: Sources, Envelopes, Filters, Shapers, Utilities, Effects) hosts
// the real engine module: its inputs and outputs converted
// between the patch's float signals and the engine's q15 blocks, its values sent with set_param when they change, a voice's state
// (its pitch, gate, velocity, its start) given as the engine gives it. Its memory is the module's own heap, inside the runner's block:
// no allocation on the audio thread. A type registered in both scopes (Osc, Filter, Lfo ...) is one entry: the runner builds the Voice
// type in the VOICE area, the Global one in the FX area. Every entry goes in both areas: a type the engine has in one scope only is built
// in the other too (a Voice type on the FX area plays the last note, mod::LastNote; a Global type in a voice: a copy per voice, its
// memory each time). Export > Engine cannot write those (engine_type_in): the engine would need a module of that scope.
//
// Values are shown in the engine's units, except two families made readable: a level in q15 (0..32767) as a percentage, and a pitch in
// 1/256 semitone (a "pitch", "cut...", "damp", "bandwidth" value) as semitones. A "pitch" input reads the patch's pitch (octaves over
// middle C; by default the voice's: the module follows the key) and gives the engine its CV (72 semitones a unit); a "gate" input reads
// the voice's gate by default.
//
// Not hosted (they need what only the Ritual Machine gives them): the in / out ends (the patch's Note, VoiceOut, Voices, Output stand for
// them), Sampler, Granular and Pads (the card's samples), MotionSeq (the step sequencer), GateIn and ParaGate (every voice at once),
// Convolver (its impulse from the app), Dx7 (its FM patch from the app). A block is the engine's (kBlock frames): the runner's blocks
// must be multiples of it (they are: 32 or 64).
#include "engine/core/module.h"
#include "modular/modular.h"

namespace mod {

// Adds every type of `reg` that can be hosted (see above) to the catalog. Call once, before patches are read. Returns the number added.
int register_engine_modules(const sc::Registry &reg);

bool is_engine_module(const ModuleDef *d);                  // an "RM" entry: d->engine is the engine type's name
int32_t engine_raw_value(const ModuleDef *d, int param, float v);   // value `param` at v (as shown) in the engine's own units
const char *engine_param_name(const ModuleDef *d, int param);       // its name in the engine (the export uses it)
// The engine registers `type` (an engine module's name: "Eg", "Delay") in the scope of `area` (kVoiceArea: Voice, kFxArea: Global).
// False also for a type it does not have, or before register_engine_modules.
bool engine_type_in(const char *type, int area);

}  // namespace mod
