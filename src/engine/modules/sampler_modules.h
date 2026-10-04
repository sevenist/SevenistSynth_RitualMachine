#pragma once
// Sample playback modules (ADR-021..023). They need a SampleBank in Memory::bank (see sampler/sample_bank.h).
#include "engine/core/module.h"

namespace sc {

enum SamplerType : int { T_SAMPLER = 54, T_GRANULAR_V, T_GRANULAR_G };

// Sampler (voice): in 0 pitch CV (like Osc), out 0.   mod: tune (+-96 semitones at full scale)
//   sample / instrument  bank slot id / instrument id (an instrument picks the zone for the note and velocity;
//                        -1 = unused). Zones also override root, tune and gain.
//   tune                 pitch units (1/256 semitone) added to the played pitch
//   root                 pitch units of the note that plays the sample at its original speed; -1 = from the file
//   loop                 -1 = file default, 0 off, 1 forward, 2 ping-pong      reverse  0 / 1
//   start                offset in 1/16 ms from the start of the sample or slice
//   slice                -1 = whole sample, else start at slice n        slice_mode  0 = play on, 1 = stop at the next slice
//   gain q15, interp 0 = linear / 1 = Hermite, track 1 = follow the keyboard pitch / 0 = always original speed (drums)
enum { SMPR_SAMPLE, SMPR_INSTRUMENT, SMPR_TUNE, SMPR_ROOT, SMPR_LOOP, SMPR_REVERSE, SMPR_START, SMPR_SLICE, SMPR_SLICE_MODE,
       SMPR_GAIN, SMPR_INTERP, SMPR_TRACK, SMPR_N };

// Granular (voice or global): in 0 pitch CV, out 0. Reads the RAM head of a sample (load it with head_ms = 0 to make
// it fully resident). position q15 = place in the sample, speed Q8 (256 = real time, 0 = frozen, negative = backwards),
// size ms, density grains/s, pitch in pitch units, jitter q15 = random position spread, level q15.
enum { GRN_SAMPLE, GRN_POSITION, GRN_SPEED, GRN_SIZE, GRN_DENSITY, GRN_PITCH, GRN_JITTER, GRN_LEVEL, GRN_N };

void register_sampler_modules(Registry &reg);

}  // namespace sc
