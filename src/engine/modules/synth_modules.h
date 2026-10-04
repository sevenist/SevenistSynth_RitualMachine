#pragma once
// Synth modules built on the dsp/ kernels. Type ids and parameter indexes for graph builders.
//
// Conventions (see ADR-011): a parameter cable (`mod[p]`) is a bipolar q15 signal, full scale = +-1.0, added to
// the parameter's base value after scaling by the module's own range parameter where the unit is not q15.
// Pitch-like values (pitch, cutoff) are in 1/256 semitone, as MIDI note * 256. Pitch CV: see builtin.h.
#include "engine/core/module.h"
#include "engine/modules/builtin.h"

namespace sc {

enum SynthType : int {
    T_OSC = 32, T_ENV, T_LFO_V, T_LFO_G, T_FILTER_V, T_FILTER_G, T_VCA_V, T_VCA_G,
    T_MIX4_V, T_MIX4_G, T_MULT_V, T_MULT_G, T_SHAPER_V, T_SHAPER_G, T_CONST_V, T_CONST_G,
    T_EG = 59,                                      // multi-stage envelope (see Eg)
    T_OSC_G = 51,                                   // the oscillator in global scope (drones, audio-rate sources for FX tests)
};

// Osc: in 0 pitch CV (added to `pitch`), out 0.   mod: pitch (+-pitch_mod), pw, level
enum { OSC_WAVE, OSC_PITCH, OSC_PW, OSC_LEVEL, OSC_PITCH_MOD, OSC_N };
enum { WAVE_SINE_, WAVE_SAW_, WAVE_PULSE_, WAVE_TRI_, WAVE_NOISE_, WAVE_SAW_DOWN_ };

// Env (ADSR with hold, start level and curves): in 0 gate, out 0 in 0..1.   times in ms, sustain / start q15, curves q15 (-1..+1, see dsp/curve.h:
// +: fast start slow end, 0: line, -: slow start fast end). Every segment lasts exactly its time.
enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_HOLD, ENV_START, ENV_A_CURVE, ENV_D_CURVE, ENV_R_CURVE, ENV_N };

// Eg (four breakpoints): t1 l1 c1 .. t4 l4 c4 (time ms, 0 = unused / level q15 / curve q15), then sustain (points played before holding, 1..4),
// release ms, release curve, oneshot (1 = ignores the gate and always plays out).
enum { EG_T1, EG_L1, EG_C1, EG_T2, EG_L2, EG_C2, EG_T3, EG_L3, EG_C3, EG_T4, EG_L4, EG_C4, EG_SUSTAIN, EG_RELEASE, EG_RCURVE, EG_ONESHOT, EG_N };

// Lfo: out 0 (bipolar, or 0..1 when unipolar).   rate in pitch units (1/256 semitone) relative to 1 Hz,
//      so 12*256 = one octave above 1 Hz.   mod: rate (+-rate_mod)
enum { LFO_SHAPE, LFO_RATE, LFO_LEVEL, LFO_UNIPOLAR, LFO_RATE_MOD, LFO_N };
enum { LFOS_SINE, LFOS_TRI, LFOS_SAW_DOWN, LFOS_SAW_UP, LFOS_SQUARE, LFOS_SH };

// Filter: in 0, out 0.   sections 1..4 = 12..48 dB/oct.   res: q15 0..1 (Q boost on the last section)   mod: cutoff
enum { FLT_MODE, FLT_SECTIONS, FLT_CUTOFF, FLT_RES, FLT_CUT_MOD, FLT_N };
enum { FLTM_LP, FLTM_BP, FLTM_HP, FLTM_NOTCH };

// Shaper (naive, no anti-aliasing: ADR-013): in 0, out 0.   drive in 1/256 octave of gain.   mod: drive
enum { SHP_MODE, SHP_DRIVE, SHP_MIX, SHP_BITS, SHP_DRIVE_MOD, SHP_N };   // drive_mod: octaves of gain at full-scale modulation
enum { SHPM_TANH, SHPM_CLIP, SHPM_FOLD, SHPM_CRUSH, SHPM_TUBE, SHPM_TAPE, SHPM_DIODE, SHPM_CHEB, SHPM_RECT, SHPM_DECIM, SHPM_N };
// TUBE: biased tanh (even harmonics).  TAPE: tanh followed by a gentle high cut.  DIODE: soft on the positive half, harder and smaller on the
// negative half.  CHEB: Chebyshev T3 + T5 (odd harmonics at an exact ratio).  RECT: full-wave rectifier (octave up).  DECIM: sample-rate
// reduction by `drive` (1x .. 16x hold) plus the bit depth.  The asymmetric ones go through a DC blocker.

// Vca: in 0, out 0 = in * (level + mod[0]).   Const: out 0 = value.   Mult: out = in0 * in1.   Mix4: sum of 4 weighted ins
enum { VCA_LEVEL };
enum { CONST_VALUE };

void register_synth_modules(Registry &reg);

}  // namespace sc
