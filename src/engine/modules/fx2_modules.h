#pragma once
// More effects. All global scope and stereo (ins L, R; outs L, R) except Comb (a voice-scope processor). Parameters are int32 in the
// units given below; see ENGINE_DESIGN.md (ADR-031) for the choices.
#include "engine/core/command_queue.h"
#include "engine/core/module.h"

namespace sc {

enum Fx2Type : int { T_PHASER = 61, T_FLANGER, T_TREMOLO, T_COMP, T_EQ3, T_SHIFTER, T_CONV, T_COMB };

// Phaser: 2..8 first-order all-pass stages (stages 1..4 = 2, 4, 6, 8) swept by a triangle LFO (R channel 90 degrees ahead).
//   rate  pitch units relative to 1 Hz (12 * 256 per octave)     depth q15 (x 4 octaves of sweep)     center pitch units (MIDI * 256)
//   feedback q15 signed     mix q15 (0.5 = deepest notches)
enum { PHS_RATE, PHS_DEPTH, PHS_CENTER, PHS_FEEDBACK, PHS_STAGES, PHS_MIX, PHS_N };

// Flanger: a modulated delay of the input with feedback.   delay 1/16 ms (center, 0.5 .. 10 ms)   depth q15 (fraction of the delay)
//   rate like the phaser     feedback q15 signed     mix q15 (wet added to the dry signal)
enum { FLG_RATE, FLG_DEPTH, FLG_DELAY, FLG_FEEDBACK, FLG_MIX, FLG_N };

// Tremolo / auto-pan.   rate like the phaser   depth q15   shape 0 sine, 1 triangle, 2 soft square   mode 0 tremolo (both channels), 1 auto-pan
enum { TRM_RATE, TRM_DEPTH, TRM_SHAPE, TRM_MODE, TRM_N };

// Compressor (stereo linked, feed-forward peak detector).   threshold dB (-60 .. 0)   ratio x10 (10 .. 200)   attack ms   release ms
//   makeup dB (0 .. 24)   mix q15 (parallel compression)
enum { CMP_THRESH, CMP_RATIO, CMP_ATTACK, CMP_RELEASE, CMP_MAKEUP, CMP_MIX, CMP_N };

// 3-band EQ: low shelf (150 Hz), peaking mid, high shelf (6 kHz).   gains in 1/10 dB (+-15 dB)   midf pitch units
enum { EQ_LOW, EQ_MID, EQ_MIDF, EQ_HIGH, EQ_N };

// Ring modulator / frequency shifter.   mode 0 ring, 1 shift up, 2 shift down   freq pitch units relative to 1 Hz   mix q15
enum { SFT_MODE, SFT_FREQ, SFT_MIX, SFT_N };

// Convolver (short impulse response, direct form): built-in cabinets and body resonances, or a custom IR loaded with set_blob.
//   ir 0..7 built-in, 8 = custom     length taps (16 .. 512)     mix q15     level q15 (output trim, 0.5 = unity of the normalised IR)
enum { CNV_IR, CNV_LENGTH, CNV_MIX, CNV_LEVEL, CNV_N };
enum { CNVIR_CAB_1X12, CNVIR_CAB_4X12, CNVIR_CAB_BRIGHT, CNVIR_CAB_DARK, CNVIR_ACOUSTIC, CNVIR_VIOLIN, CNVIR_DRUM, CNVIR_PHONE, CNVIR_CUSTOM, CNVIR_COUNT };
constexpr int kConvMaxTaps = 512;
constexpr int kConvChunk = 120;
struct ConvBlob { uint16_t start, count; int16_t tap[kConvChunk]; };      // custom IR in chunks: taps [start, start + count)
static_assert(sizeof(ConvBlob) <= kCmdBlobMax, "a chunk must fit in one command");

// Comb (voice scope): in 0 audio, in 1 pitch CV; out 0. A tuned feedback comb (resonator) that follows the key.
//   tune pitch units (the note plays at `tune` + CV)   feedback q15 signed (negative = odd harmonics only)   damp cutoff pitch units
//   interval semitones of a second comb added to the first (0 = off)   mix q15
enum { CMB_TUNE, CMB_FEEDBACK, CMB_DAMP, CMB_INTERVAL, CMB_MIX, CMB_N };

void register_fx2_modules(Registry &reg);

}  // namespace sc
