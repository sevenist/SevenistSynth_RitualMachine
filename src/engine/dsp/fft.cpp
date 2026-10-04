#include "engine/dsp/fft.h"

namespace sc {
namespace {

constexpr int N = kFftN;
constexpr int32_t kSafe = 13500;     // max |component| for which one butterfly stage cannot overflow int16: (1 + sqrt 2) * 13500 < 32768

int fft_core(int16_t *re, int16_t *im) {
    for (int i = 0; i < N; i++) {
        int j = kFftRev[i];
        if (j > i) {
            int16_t t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    int exp = 0;
    for (int len = 2; len <= N; len <<= 1) {
        int32_t mx = 0;
        for (int i = 0; i < N; i++) {
            int32_t a = re[i] < 0 ? -re[i] : re[i], b = im[i] < 0 ? -im[i] : im[i];
            if (a > mx) mx = a;
            if (b > mx) mx = b;
        }
        int sh = 0;
        while ((mx >> sh) > kSafe) sh++;
        exp += sh;
        const int32_t rnd = sh ? (1 << (sh - 1)) : 0;
        const int half = len >> 1, step = N / len;
        for (int base = 0; base < N; base += len) {
            for (int k = 0; k < half; k++) {
                const int32_t wr = kFftCos[k * step], wi = kFftSin[k * step];
                const int a = base + k, b = a + half;
                const int32_t ar = (re[a] + rnd) >> sh, ai = (im[a] + rnd) >> sh;
                const int32_t br = (re[b] + rnd) >> sh, bi = (im[b] + rnd) >> sh;
                const int32_t tr = (br * wr - bi * wi + (1 << 14)) >> 15;
                const int32_t ti = (br * wi + bi * wr + (1 << 14)) >> 15;
                re[a] = static_cast<int16_t>(ar + tr);
                im[a] = static_cast<int16_t>(ai + ti);
                re[b] = static_cast<int16_t>(ar - tr);
                im[b] = static_cast<int16_t>(ai - ti);
            }
        }
    }
    return exp;
}

}  // namespace

int fft_q15(int16_t *re, int16_t *im) { return fft_core(re, im); }
int ifft_q15(int16_t *re, int16_t *im) { return fft_core(im, re); }     // swapping re and im turns the forward FFT into the inverse

}  // namespace sc
