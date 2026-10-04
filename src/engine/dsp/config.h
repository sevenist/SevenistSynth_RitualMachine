#pragma once
// Engine-wide compile-time constants (ADR-004). Override with -D on the compiler command line.
// Everything time-based in the engine is derived from these; nothing hardcodes 48000 or 32.

#ifndef ENGINE_SR
#define ENGINE_SR 48000           // sample rate, Hz
#endif

#ifndef ENGINE_BLOCK
#define ENGINE_BLOCK 32           // frames per processing block (power of two)
#endif

#ifndef ENGINE_MAX_VOICES
#define ENGINE_MAX_VOICES 8
#endif

static_assert((ENGINE_BLOCK & (ENGINE_BLOCK - 1)) == 0 && ENGINE_BLOCK >= 8 && ENGINE_BLOCK <= 256,
              "ENGINE_BLOCK must be a power of two in 8..256");
static_assert(ENGINE_SR >= 8000 && ENGINE_SR <= 96000, "ENGINE_SR out of range");

namespace sc {
constexpr int kSampleRate = ENGINE_SR;
constexpr int kBlock      = ENGINE_BLOCK;
constexpr int kControlRate = ENGINE_SR / ENGINE_BLOCK;   // Hz: one control tick per block
constexpr int kMaxVoices  = ENGINE_MAX_VOICES;
}  // namespace sc
