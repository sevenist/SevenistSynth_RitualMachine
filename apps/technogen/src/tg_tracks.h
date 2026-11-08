#pragma once
// TechnoGen's track module for the engine: one global module per track, which plays its own hits (Module::hit, from the pads' sequencer
// routed to it: Engine::pseq_route) and adds its sound to the stereo signal at its inputs. The seven are chained, T1 into T2 ... into T7,
// then into the master: a sum with no mixer, which a module's four inputs could not hold.
//
// FIRST VERSION, A PLACEHOLDER: each kind is a few lines of cheap float DSP, enough to hear the dice and the patterns. The real engines
// (ADR-048: the kick's sweep, click and shaper modes, the bass's ALGO, the shared metal bank ...) replace it one at a time, each measured
// on the board against its budget.
#include "engine/core/module.h"

namespace tg {

constexpr int kTypeTrack = 112;          // its type id in the engine's registry (the library's own stop below 100)

// Its parameters: the track's seven values (0..100, the dice's), its level (0..100: the main view's knob; 0 = silent), and its kind
// (TrackId: which sound it makes). Set the kind in the graph; it is not meant to change while it plays.
enum TrackParam : int { TP_V0 = 0, TP_LEVEL = 7, TP_KIND = 8, TP_COUNT = 9 };

void register_track_module(sc::Registry &reg);

}  // namespace tg
