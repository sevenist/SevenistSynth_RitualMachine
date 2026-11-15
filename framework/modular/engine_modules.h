#pragma once
// The modular catalog's own modules as engine modules (framework/engine): each one's float code (modules.h, the same code the studio
// plays and a generated synth calls) inside an engine module, so that the engine, and so the Ritual Machine and an exported engine graph,
// has every module of the catalog with its features. The user chose (2026-11-15) the engine as the one module system, the float modules
// brought to it through this wrapper first, a module rewritten in q15 later only where the bench shows it is worth it.
//
// An engine module "F" + the catalog's name ("FBody", "FOsc", "FADSR"): its inputs, outputs and values are the catalog's.
//   - Signals: q15 in, float in the module, q15 out (full scale +-1 both sides). An input named "Pitch" is a pitch in octaves in the
//     catalog and the engine's pitch CV here (1.0 = 72 semitones): x 6 on the way in.
//   - An input with nothing plugged in reads what the catalog's module reads then (its InDefault: 1, the voice's gate, a pulse at the
//     note's start, its first input, the voice's pitch): the engine tells an unplugged input (ProcessCtx::silence) from a silent one.
//   - Values: int32, the catalog's value x 100 (x 1 for whole numbers: a choice, a count); a modulation input (Ports::mod) moves the
//     value along its range by its mean over the block, depth 1 = the whole range: the modular runner's rule (patch.cpp).
//   - The note: a voice's pitch, velocity and gate (Voice scope); on the global section the keys held and a start when any voice starts.
//   - Memory: the module's state from the fast heap (the bulk one beyond 8 KB: a delay line).
// The In / Out modules (Note, VoiceOut, Voices, Output) are not wrapped: the engine has its own (NoteIn, VoiceOut, BusIn, MasterOut).
#include "engine/core/module.h"
#include "modular/modular.h"

namespace mod {

constexpr int kModularTypeFirst = 128;      // their type ids: 128 + 2 x the module's rank (+1: its Global type); Registry::kMax 192

// Every catalog module but the In / Out ones, in both scopes the catalog allows (a module of Scope::Both: a Voice and a Global type of
// one name). Returns the number of types added.
int register_modular_types(sc::Registry &reg);
// One module (by its catalog name): what an app that plays only some of them registers (each one's code then sits in internal RAM).
bool register_modular_type(sc::Registry &reg, const char *name);
// The engine's type id of a catalog module in that scope (-1: none, or an In / Out module).
int modular_type_id(const char *name, sc::Scope scope);
// The catalog module an engine type wraps (nullptr: an engine module of its own).
const ModuleDef *modular_def_of(const sc::ModuleInfo *info);
// A value of the catalog's (in its units) as the engine type's int32, and back.
int32_t modular_raw(const Param &q, float v);
float modular_value(const Param &q, int32_t raw);

}  // namespace mod
