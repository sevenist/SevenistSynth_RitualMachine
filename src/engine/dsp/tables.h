#pragma once
// Constant tables (generated, live in flash). See tools/gen_engine_tables.py.
#include <cstdint>

namespace sc {

constexpr int kSineBits = 10;                       // 1024 entries per cycle
constexpr int kSineSize = 1 << kSineBits;
extern const int16_t kSineTab[kSineSize + 1];       // q15, guard entry at [kSineSize]

constexpr int kExp2Bits = 8;                        // 256 entries per octave
constexpr int kExp2Size = 1 << kExp2Bits;
extern const uint32_t kExp2Tab[kExp2Size + 1];      // 2^(i/256) as unsigned Q2.30, guard at [kExp2Size]

constexpr int kTanSize = 233;                       // tan(pi * i / 512), i = 0..233 (guard at the end)
extern const uint32_t kTanTab[kTanSize + 1];        // unsigned Q4.28

constexpr int kTanhSize = 1024;                     // tanh over [-4, 4]
extern const int16_t kTanhTab[kTanhSize + 1];       // q15

constexpr int kFftN = 512;                          // FFT / STFT size (generator: FFT_N)
constexpr int kFftLog2N = 9;
extern const int16_t kFftCos[kFftN / 2];            // cos(2 pi k / N), q15
extern const int16_t kFftSin[kFftN / 2];            // -sin(2 pi k / N), q15
extern const uint16_t kFftRev[kFftN];               // bit reversal permutation
extern const int16_t kStftWin[kFftN];               // sin(pi (n + 0.5) / N), q15

constexpr int kCordicIters = 20;
extern const uint32_t kCordicAtan[kCordicIters];    // atan(2^-i) as a phase (2^32 = one turn)

}  // namespace sc
