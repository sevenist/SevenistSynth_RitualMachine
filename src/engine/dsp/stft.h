#pragma once
// Short-time Fourier transform engine for spectral effects (ADR-016): sine analysis window, FFT, a callback that
// edits the spectrum, inverse FFT, sine synthesis window, overlap-add with 75 % overlap (hop = N/4).
// sin^2 windows at hop N/4 sum to 2, folded into the output scaling, so an untouched spectrum is a perfect
// reconstruction (measured in the tests). Latency is reported by latency().
//
// The callback gets the spectrum as Frame{re, im, exp}: true value = stored * 2^exp (block floating point).
// It may edit bins and change `exp` (e.g. when it restores held, older data). Mono mode (hermitian = true): the
// input is real, the callback edits bins 0..N/2 only and the engine mirrors them. Dual mode: a second input is
// packed into the imaginary part (X = A + jB); the callback owns the whole spectrum and must leave the Hermitian
// spectrum of the (real) result in re/im.
#include <cstdint>
#include "engine/core/heap.h"
#include "engine/dsp/fft.h"
#include "engine/dsp/q.h"
#include "engine/dsp/tables.h"

namespace sc {

struct StftFrame {
    int16_t *re;
    int16_t *im;
    int exp;
};

class Stft {
public:
    static constexpr int N = kFftN;
    static constexpr int H = N / 4;
    static constexpr int kFifo = 2 * H;

    Stft() = default;
    Stft(const Stft &) = delete;
    Stft &operator=(const Stft &) = delete;
    ~Stft() { release(); }

    bool init(Heap &h, bool dual) {
        release();
        heap_ = &h;
        mem_ = static_cast<uint8_t *>(h.alloc(bytes(dual)));
        if (!mem_) { heap_ = nullptr; return false; }
        uint8_t *p = mem_;
        in_ = reinterpret_cast<q15 *>(p); p += sizeof(q15) * N;
        if (dual) { in2_ = reinterpret_cast<q15 *>(p); p += sizeof(q15) * N; }
        re_ = reinterpret_cast<int16_t *>(p); p += sizeof(int16_t) * N;
        im_ = reinterpret_cast<int16_t *>(p); p += sizeof(int16_t) * N;
        fifo_ = reinterpret_cast<q15 *>(p); p += sizeof(q15) * kFifo;
        acc_ = reinterpret_cast<int32_t *>(p);
        reset();
        return true;
    }
    void release() { if (heap_) heap_->free(mem_); mem_ = nullptr; heap_ = nullptr; }
    bool ready() const { return mem_ != nullptr; }

    void reset() {
        for (int i = 0; i < N; i++) { in_[i] = 0; if (in2_) in2_[i] = 0; acc_[i] = 0; }
        for (int i = 0; i < kFifo; i++) fifo_[i] = 0;
        ipos_ = opos_ = hop_ = 0;
        rd_ = 0; wr_ = H; count_ = H;               // prefill: the first output block never underflows
    }

    // Input-to-output delay in samples (measured by the tests): window fill + hop alignment.
    static constexpr int latency() { return N; }

    // Processes n samples; in2 may be null when the engine was initialised mono.
    template <typename Fn>
    void process(const q15 *in, const q15 *in2, q15 *out, int n, Fn &&fn) {
        for (int i = 0; i < n; i++) {
            in_[ipos_] = in[i];
            if (in2_) in2_[ipos_] = in2 ? in2[i] : 0;
            ipos_ = (ipos_ + 1) & (N - 1);
            if (++hop_ == H) { hop_ = 0; frame(fn); }
            out[i] = fifo_[rd_];
            rd_ = (rd_ + 1) & (kFifo - 1);
            count_--;
        }
    }

private:
    static size_t bytes(bool dual) {
        return sizeof(q15) * N * (dual ? 2 : 1) + sizeof(int16_t) * N * 2 + sizeof(q15) * kFifo + sizeof(int32_t) * N + 16;
    }

    template <typename Fn>
    void frame(Fn &fn) {
        for (int n = 0; n < N; n++) {
            const int idx = (ipos_ + n) & (N - 1);
            re_[n] = mul15(kStftWin[n], in_[idx]);
            im_[n] = in2_ ? mul15(kStftWin[n], in2_[idx]) : 0;
        }
        StftFrame f{re_, im_, fft_q15(re_, im_)};
        fn(f);
        if (!in2_) {                                              // mono: enforce a Hermitian spectrum
            im_[0] = 0;
            im_[N / 2] = 0;
            for (int k = 1; k < N / 2; k++) { re_[N - k] = re_[k]; im_[N - k] = sat16(-static_cast<int32_t>(im_[k])); }
        }
        const int ei = ifft_q15(re_, im_);
        const int s = f.exp + ei - kFftLog2N - 1;                 // 1/N of the inverse, 1/2 of the window overlap
        for (int n = 0; n < N; n++) {
            int64_t v = (static_cast<int64_t>(re_[n]) * kStftWin[n] + (1 << 14)) >> 15;
            v = s >= 0 ? (v << s) : ((v + (1LL << (-s - 1))) >> -s);
            acc_[(opos_ + n) & (N - 1)] = sat32(static_cast<int64_t>(acc_[(opos_ + n) & (N - 1)]) + v);
        }
        for (int j = 0; j < H; j++) {                              // the oldest H samples are complete
            const int idx = (opos_ + j) & (N - 1);
            fifo_[wr_] = sat16(acc_[idx]);
            wr_ = (wr_ + 1) & (kFifo - 1);
            acc_[idx] = 0;
        }
        count_ += H;
        opos_ = (opos_ + H) & (N - 1);
    }

    Heap *heap_ = nullptr;
    uint8_t *mem_ = nullptr;
    q15 *in_ = nullptr, *in2_ = nullptr, *fifo_ = nullptr;
    int16_t *re_ = nullptr, *im_ = nullptr;
    int32_t *acc_ = nullptr;
    int ipos_ = 0, opos_ = 0, hop_ = 0, rd_ = 0, wr_ = 0, count_ = 0;
};

}  // namespace sc
