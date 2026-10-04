#pragma once
// Interpolation for q15 samples. `frac` is an unsigned 16-bit fraction (0..65535 = 0..1).
// Used by delay lines, wavetables and the sampler.
#include <cstdint>
#include "engine/dsp/q.h"

namespace sc {

// Linear: a + (b - a) * frac
constexpr q15 lerp15(q15 a, q15 b, uint32_t frac) {
    // rounded, not floored: a floor bias of half an LSB per read accumulates into a DC offset inside feedback loops
    return static_cast<q15>(a + (((static_cast<int32_t>(b) - a) * static_cast<int32_t>(frac) + 0x8000) >> 16));
}

// 4-point, 3rd-order Hermite between y1 and y2 (y0, y3 are the neighbours). Smoother than linear for
// modulated delays (chorus) and pitched sample playback, at about 3x the cost.
constexpr q15 hermite15(q15 y0, q15 y1, q15 y2, q15 y3, uint32_t frac) {
    const int64_t t = static_cast<int64_t>(frac);                                   // Q16
    const int64_t c1 = (static_cast<int64_t>(y2) - y0);                              // x2 scale: (y2 - y0) / 2
    const int64_t c2 = 2 * y0 - 5 * static_cast<int64_t>(y1) + 4 * static_cast<int64_t>(y2) - y3;   // x2
    const int64_t c3 = (static_cast<int64_t>(y3) - y0) + 3 * (static_cast<int64_t>(y1) - y2);       // x2
    int64_t acc = c3;                                // ((c3 t + c2) t + c1) t + 2 y1, all x2
    acc = (acc * t >> 16) + c2;
    acc = (acc * t >> 16) + c1;
    acc = (acc * t >> 16) + 2 * static_cast<int64_t>(y1);
    return sat16(static_cast<int32_t>((acc + 1) >> 1));
}

}  // namespace sc
