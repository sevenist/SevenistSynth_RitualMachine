#pragma once
// In-place radix-2 decimation-in-time complex FFT of size kFftN (512), q15 data with block floating point
// (ADR-016). Before each stage the whole block is scanned and shifted right just enough to keep the worst case
// butterfly growth (1 + sqrt 2) inside int16, so precision follows the signal level. The result is
//   X_true = stored * 2^exp      (exp = the returned number of right shifts)
// The forward transform is X[k] = sum x[n] e^{-j 2 pi n k / N}; the inverse is the unnormalised
// x[n] = sum X[k] e^{+j 2 pi n k / N}, so a forward + inverse round trip multiplies by N (callers fold 1/N into
// the exponent).
#include <cstdint>
#include "engine/dsp/tables.h"

namespace sc {

int fft_q15(int16_t *re, int16_t *im);      // forward, returns exp
int ifft_q15(int16_t *re, int16_t *im);     // inverse (unnormalised), returns exp

}  // namespace sc
