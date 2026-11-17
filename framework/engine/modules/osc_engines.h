#pragma once
// Oscillator engines (voice scope): one module, nineteen sound generators chosen by `engine`, each with the same three controls
// (pitch, timbre, morph) so a rack oscillator can switch between them. Inspired by the macro-oscillator idea of Mutable Instruments
// Plaits (parameters named by effect, not by algorithm), implemented from scratch in fixed point.
//
//   engine        timbre                              morph
//   KARP  string  brightness of the pluck / filter    sustain (decay time of the string)
//   MODAL struck  material: harmonic string -> bell   decay time of the modes (higher modes die first)
//   FM2   2-op FM modulation index (0..8 rad)         frequency ratio (0.5 .. 12, stepped)
//   FOLD  folder  fold gain (1x .. 9x)                waveform that is folded: sine -> saw
//   SSAW  supersaw detune spread (up to +-4.5 %)      centre voice -> full unison
//   VOWEL formant vowel position A E I O U            resonance (bandwidth) of the formants
//   ADD   additive (12 harmonics) brightness (slope)  odd / even balance (1 = odd only)
//   DUST  crackle  density (2 Hz .. 4 kHz)            resonance of the pitched pings
//   STR   strings  detune of a pair of oscillators    wave: saw -> pulse -> triangle (thirds)          (ADR-037: the Strings voice's oscillators)
//   GENDY stochastic (Xenakis): 12 breakpoints  walk of the amplitudes (0 = a clean wave)   walk of the segment lengths (pitch jitter)
//   ROSSL Roessler attractor, pitch = step      mix x -> spiky z                           chaos (the c parameter: periodic -> chaotic)
//   CA    a 64-cell automaton row = the wave    rule (8 of Wolfram's: 30 90 110 150 45 22 126 54)   generations a second (0 = frozen)
//   PULSAR pulsaret length (4 % .. 100 % of the period)  cycles in the pulsaret (0.5 .. 16)                 (Roads' pulsar synthesis, no masking)
//   TONEW tonewheel organ: drawbars brought in 8' .. all 9   key click and leakage noise
//   FOF   formant bursts, vowels A E I O U       bandwidth of the formants (the burst length)
//   TERRA wave terrain: orbit radius             the orbit's centre across the surface
//   SCAN  scanned synthesis: stiffness of the chain   sustain of the chain (damping)
//   WIND  waveguide reed (clarinet): breath pressure  brightness of the bore (the gate is the breath)
//   FOLEY ambience: rain / wind / fire / crackle      intensity
// GENDY, ROSSL, CA, TERRA, SCAN and WIND keep their state in the Karplus string's buffer (the engines never run together). They are for the first-order sound of
// the idea, not band-limited: GENDY and CA alias at high pitch (CA: harmonics above 32), ROSSL follows the pitch roughly (it is a
// chaotic orbit, the step is set for the Roessler loop of about 6 time units).
//
// quality (STR, and nothing else here): 0 = Blep (means Mip for STR), 1 = Mip (band-limited tables), 2 = Naive (cheapest, aliases).
// STR follows the pitch at block rate.
//
// in 0 pitch CV (like Osc), in 1 gate (strikes KARP / MODAL, retriggers); mod: pitch (+-pitch_mod), timbre, morph (q15, block rate).
#include "engine/core/module.h"

namespace sc {

enum OscxEngine : int { OSCX_KARP, OSCX_MODAL, OSCX_FM2, OSCX_FOLD, OSCX_SSAW, OSCX_VOWEL, OSCX_ADD, OSCX_DUST, OSCX_STR, OSCX_GENDY, OSCX_ROSSL, OSCX_CA, OSCX_PULSAR, OSCX_TONEW, OSCX_FOF, OSCX_TERRA, OSCX_SCAN, OSCX_WIND, OSCX_FOLEY, OSCX_ENGINES };
enum { OSCX_ENGINE, OSCX_PITCH, OSCX_TIMBRE, OSCX_MORPH, OSCX_LEVEL, OSCX_PITCH_MOD, OSCX_QUAL, OSCX_N };

constexpr int T_OSCX = 60;

// Per-engine cycle counters (ESP32 with -DENGINE_PROFILE): process() adds its cycles to g_osc_prof[engine]; the audio task prints and clears them.
#if defined(ENGINE_PROFILE) && defined(ARDUINO_ARCH_ESP32)
extern uint32_t g_osc_prof[OSCX_ENGINES];
#endif
void register_osc_engines(Registry &reg);

}  // namespace sc
