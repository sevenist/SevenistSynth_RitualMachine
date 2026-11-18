// Hadamard: a stereo feedback-delay-network reverb, eight lines (fx_modules.h has its parameters).
//
// The loop: s = the eight delay outputs; f = damp(g . sign . s / sqrt 8) per line; m = H8 (f + the input on two lines); line perm[k] gets m[k].
// What breaks the symmetry of a plain Hadamard network:
//   - the feedback matrix is P . H8 . D (a permutation after it, sign flips before it): still orthogonal (lossless), but not symmetric and
//     not its own inverse; its eigenvalues spread around the unit circle (smallest gap 0.72 rad of the 0.785 possible; H8's are +-1, a
//     gap of 0) and it has no finite order (no number of passes brings a vector back). Chosen by a search over permutations and signs.
//   - the delays are primes (pairwise coprime at every size: each length is snapped to a prime, all distinct), and no two of them are
//     within 25 cents of a simple ratio p / q (p, q < 8), so their modes do not pile up.
//   - the inputs enter on two different lines, the outputs are two orthogonal sign patterns that are not rows of H8 (decorrelated L / R).
//   - two lines are modulated by two slow sines of unrelated rates.
//   - the decay gain and the damping of each line are set from its own length (Jot): every line loses the same dB per second, at the
//     top as at the bottom, so no line rings on alone.
//
// Cost (the ESP32-S3: an in-order core, int16 <-> float conversions 4 / 8 cycles, a multiply-add 6 cycles of latency):
//   - the tank runs at half the sample rate (a half-band FIR in, a cubic midpoint out, as the Reverb's ENGINE_REVERB_HALF);
//   - in passes of up to kSub tank samples: every delay is longer than a pass, so each line's output for the whole pass was written before it
//     starts. A line is read as one run (a pointer, no index arithmetic; a pass that crosses the end of the ring copies the run first),
//     the matrix runs over the pass with all eight values in registers, and each line is written back as one run;
//   - all lines share one power-of-two ring and one write pointer (each line a fixed segment of it): one pointer update per pass;
//   - the per-line filter is one multiply-add on its state (lp = c . s + p . lp), and the lines go two at a time so the two state chains
//     overlap their latency;
//   - the gains, primes and filter coefficients are computed on a parameter change only;
//   - saturation is sat16 (a MAX and a MIN): the S3's CLAMPS by inline asm costs more, GCC keeps a loop with asm out of the zero-overhead LOOP.
#include <cmath>
#include <cstring>
#include "engine/dsp/phase.h"
#include "engine/modules/fx_modules.h"

namespace sc {
namespace {

constexpr int N = 8;
constexpr int kDecim = 2;
constexpr int kTankSR = kSampleRate / kDecim;
constexpr int kSub = 16;                                    // tank samples per pass
constexpr float kRefRate = 22050.f;                         // the lengths below are tank samples at this rate (44.1 kHz / 2)
constexpr float kRate = static_cast<float>(kTankSR) / kRefRate;
constexpr int kLen[N] = {571, 647, 727, 829, 929, 1063, 1193, 1361};
constexpr int kPerm[N] = {1, 3, 4, 7, 0, 6, 2, 5};          // matrix output k feeds line kPerm[k]
constexpr float kSign[N] = {-1.f, 1.f, -1.f, 1.f, -1.f, 1.f, -1.f, -1.f};
enum { kXL = N, kXR, kYL, kYR, kRows };                     // the pass's work array: the lines, then the inputs and outputs
constexpr int kApLen[4] = {131, 211, 151, 193};             // input all-passes: L, L, R, R (primes, none a line's length)
constexpr int kModLine[2] = {3, 6};
constexpr float kModMax = 8.f;                              // modulation excursion at mod = 1, tank samples at kRefRate
constexpr float kSizeMin = 0.2f;
constexpr int kGlide = 65536 / 8;                           // a size change moves a delay by at most 1/8 sample per sample (Q16)
constexpr int kModSamples = static_cast<int>(kModMax * kRate) + 2;
constexpr int kMinLen = kSub + kModSamples + 2;             // the shortest delay: longer than a pass, with room for the modulation
constexpr int kSlack = 40;                                  // a line's room above its length at size 1 (the prime snapping)
constexpr float kStore = 16384.f;                           // int16 storage: 1.0 (q15 full scale) = 16384, 6 dB of headroom. The network
                                                            // computes in these units (its writes need no scaling)
constexpr float kInGain = 0.25f;                            // the input into its line
constexpr float kOutGain = 2.0f;                            // the wet output, before the mix
static_assert(kBlock % 2 == 0, "the tank runs on pairs of samples");

bool is_prime(int n) {
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (int f = 3; f * f <= n; f += 2) if (n % f == 0) return false;
    return true;
}
int nearest_prime(int n) {
    for (int d = 0;; d++) {
        if (is_prime(n - d)) return n - d;
        if (is_prime(n + d)) return n + d;
    }
}
int scaled_len(int k, float f) { return static_cast<int>(std::lround(static_cast<float>(kLen[k]) * kRate * f)); }
int seg_max(int k) { return scaled_len(k, 1.f) + kSlack; }   // a line's longest delay
uint32_t pow2_at_least(uint32_t n) { uint32_t p = 1; while (p < n) p <<= 1; return p; }
inline float flush(float x) { return std::fabs(x) < 1e-15f ? 0.f : x; }

class Hadamard : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Hadamard", Scope::Global, 2, 2, HDM_N, false, {"L", "R"}, {"L", "R"},
            {{"decay", 2500, 100, 30000}, {"size", 22938, 0, kUnity}, {"damp", 13107, 0, kUnity}, {"diff", 26214, 0, kUnity},
             {"mod", 9830, 0, kUnity}, {"mix", 9830, 0, kUnity}}};
        return i;
    }
    ~Hadamard() override {
        if (heap_) { heap_->free(ring_); heap_->free(apr_); }
    }
    bool init(Memory &m) override {
        // the lines' segments of the ring: a line's write point (base) sits above its oldest sample by its longest delay (at size 1, the
        // prime snapping, the modulation, the interpolation's neighbour), and a pass is written ahead of it
        uint32_t total = 0;
        for (int k = 0; k < N; k++) {
            base_[k] = total + static_cast<uint32_t>(seg_max(k) + kModSamples + 2);
            total = base_[k] + kSub + 2;
        }
        const uint32_t size = pow2_at_least(total);
        uint32_t ap_total = 0;
        for (int j = 0; j < 4; j++) {
            ap_d_[j] = static_cast<int>(std::lround(static_cast<float>(kApLen[j]) * kRate));
            if (ap_d_[j] < kSub + 1) ap_d_[j] = kSub + 1;
            ap_base_[j] = ap_total;
            ap_total += static_cast<uint32_t>(ap_d_[j] + kSub + 4);
        }
        const uint32_t ap_size = pow2_at_least(ap_total);
        heap_ = m.fast;
        ring_ = m.fast->alloc_array<int16_t>(size);
        apr_ = m.fast->alloc_array<float>(ap_size);
        if (!ring_ || !apr_) return false;
        mask_ = size - 1;
        ap_mask_ = ap_size - 1;
        lfo_inc_[0] = hz_to_inc(0.71f) * kDecim;
        lfo_inc_[1] = hz_to_inc(0.97f) * kDecim;
        lfo_ph_[1] = 0x40000000u;
        update_lengths();
        update_gains();
        reset();
        return true;
    }
    void reset() override {
        if (ring_) std::memset(ring_, 0, (mask_ + 1) * sizeof(int16_t));
        if (apr_) std::memset(apr_, 0, (ap_mask_ + 1) * sizeof(float));
        for (int k = 0; k < N; k++) { lp_[k] = 0.f; base_cur_[k] = d_cur_[k] = d_tgt_[k]; }
        for (auto &v : dl_) v = 0.f;
        for (auto &v : dr_) v = 0.f;
        for (auto &v : yl_) v = 0.f;
        for (auto &v : yr_) v = 0.f;
        w_ = 0;
    }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case HDM_DECAY: if (v != decay_ms_) { decay_ms_ = v < 100 ? 100 : v; update_gains(); } break;
            case HDM_SIZE:  if (v != size_) { size_ = v; update_lengths(); update_gains(); } break;
            case HDM_DAMP:  if (v != damp_) { damp_ = v; update_gains(); } break;
            case HDM_DIFF:  ap_g_ = 0.7f * static_cast<float>(v) * (1.f / 32767.f); break;
            case HDM_MOD:   mod_exc_ = kModMax * kRate * 65536.f * static_cast<float>(v) * (1.f / 32767.f) * (1.f / 32767.f); break;
            case HDM_MIX:   mix_q_ = v; mix_ = static_cast<float>(v) * (1.f / 32767.f); break;
        }
    }
    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
        if (mix_q_ <= 0) {                                       // dry only: the tank skipped, emptied once
            if (!idle_) { reset(); idle_ = true; }
            for (int i = 0; i < ctx.frames; i++) { p.out[0][i] = p.in[0][i]; p.out[1][i] = p.in[1][i]; }
            return;
        }
        idle_ = false;
        const float dry_g = 1.f - mix_, wet_g = mix_ * kOutGain * (32768.f / kStore);
        for (int i = 0; i + 1 < ctx.frames;) {
            const int n = (ctx.frames - i) / 2 < kSub ? (ctx.frames - i) / 2 : kSub;
            alignas(16) float b[kRows][kSub];                    // the pass's work: one array, so its loops walk one pointer
            SEC_BEGIN();
            decimate(p.in[0] + i, dl_, b[kXL], n);
            decimate(p.in[1] + i, dr_, b[kXR], n);
            if (ap_g_ > 0.f) { allpass(0, b[kXL], n); allpass(1, b[kXL], n); allpass(2, b[kXR], n); allpass(3, b[kXR], n); }
            SEC_MARK(20);                                        // decimation, input diffusion
            tank(n, b);
            SEC_MARK(21);                                        // the network
            interpolate(p.in[0] + i, p.out[0] + i, yl_, b[kYL], n, dry_g, wet_g);
            interpolate(p.in[1] + i, p.out[1] + i, yr_, b[kYR], n, dry_g, wet_g);
            SEC_MARK(23);                                        // interpolation, mix
            i += 2 * n;
        }
    }

private:
    // 2:1: the half-band FIR (-1 0 9 16 9 0 -1) / 32 centred two inputs back (h = the five inputs before this pair), scaled to the network's
    // units (kStore) and the input gain. The 1e-20 keeps the all-passes' float state out of the denormals in silence (it is
    // below the int16 lines' last bit: the network never sees it).
    static void decimate(const q15 *in, float *h, float *x, int n) {
        constexpr float s = kInGain * kStore / (32.f * 32768.f);
        float h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4];
        for (int t = 0; t < n; t++) {
            const float a = static_cast<float>(in[2 * t]), b = static_cast<float>(in[2 * t + 1]);
            x[t] = ((h2 + h4) * 9.f + h3 * 16.f - (h0 + b)) * s + 1e-20f;
            h0 = h2; h1 = h3; h2 = h4; h3 = a; h4 = b;
        }
        h[0] = h0; h[1] = h1; h[2] = h2; h[3] = h3; h[4] = h4;
    }
    // 1:2 and the dry / wet mix: the even output is the tank sample two steps back, the odd one the cubic midpoint between it and the next.
    static void interpolate(const q15 *dry, q15 *out, float *h, const float *y, int n, float dry_g, float wet_g) {
        float h0 = h[0], h1 = h[1], h2 = h[2];
        const float we = wet_g, wo = wet_g * (1.f / 16.f);
        for (int t = 0; t < n; t++) {
            const float w = y[t];
            const float e = h1 * we, o = ((h1 + h2) * 9.f - (h0 + w)) * wo;
            const float d0 = static_cast<float>(dry[2 * t]), d1 = static_cast<float>(dry[2 * t + 1]);
            out[2 * t] = sat16(static_cast<int32_t>(d0 * dry_g + e));
            out[2 * t + 1] = sat16(static_cast<int32_t>(d1 * dry_g + o));
            h0 = h1; h1 = h2; h2 = w;
        }
        h[0] = h0; h[1] = h1; h[2] = h2;
    }
    // A Schroeder all-pass on x, in place: v = x + g d, y = d - g v (d = v, delay samples ago). The delay is longer than a pass.
    SC_HOT void allpass(int j, float *x, int n) {
        const float g = ap_g_;
        const uint32_t wr = (w_ + ap_base_[j]) & ap_mask_, rd = (w_ + ap_base_[j] - static_cast<uint32_t>(ap_d_[j])) & ap_mask_;
        if (wr + n <= ap_mask_ + 1 && rd + n <= ap_mask_ + 1) {   // both runs whole (the ring's end is crossed in a few passes only)
            float *pw = apr_ + wr;
            const float *pr = apr_ + rd;
            for (int t = 0; t < n; t++) { const float d = pr[t], v = x[t] + g * d; pw[t] = v; x[t] = d - g * v; }
        } else {
            for (int t = 0; t < n; t++) {
                const float d = apr_[(rd + t) & ap_mask_], v = x[t] + g * d;
                apr_[(wr + t) & ap_mask_] = v;
                x[t] = d - g * v;
            }
        }
    }
    // The run of line k's outputs for this pass when its delay holds an integer: a pointer into the ring, or the run copied into `tmp`
    // where it crosses the ring's end.
    const int16_t *run_of(int k, int16_t *tmp, int n) const {
        const uint32_t r = (w_ + base_[k] - static_cast<uint32_t>(d_cur_[k] >> 16)) & mask_;
        if (r + n <= mask_ + 1) return ring_ + r;
        for (int t = 0; t < n; t++) tmp[t] = ring_[(r + t) & mask_];
        return tmp;
    }
    // Line k read with its delay moving from d0 to d0 + n * step (Q16), linear interpolation, through the line's filter into f.
    void read_moving(int k, int32_t d0, int32_t step, float *f, int n) {
        const uint32_t wb = w_ + base_[k];
        const float c = c_[k], pk = p_[k];
        float lp = lp_[k];
        int32_t d = d0;
        for (int t = 0; t < n; t++) {
            d += step;
            const uint32_t at = wb + static_cast<uint32_t>(t) - static_cast<uint32_t>(d >> 16);
            const float s0 = static_cast<float>(ring_[at & mask_]), s1 = static_cast<float>(ring_[(at - 1) & mask_]);
            const float s = s0 + (s1 - s0) * (static_cast<float>(d & 0xFFFF) * (1.f / 65536.f));
            lp = c * s + pk * lp;
            f[t] = lp;
        }
        lp_[k] = flush(lp);
    }
    // One pass of the network: n tank samples in (rows kXL, kXR of f), out (rows kYL, kYR); rows 0..7 hold the lines.
    SC_HOT void tank(int n, float (*f)[kSub]) {
        // where each delay goes this pass: a size change glides, the two modulated lines follow their sines
        int32_t step[N];
        bool steady[N];
        for (int k = 0; k < N; k++) {
            int32_t b = base_cur_[k];
            const int32_t lim = kGlide * n, dg = d_tgt_[k] - b;
            b += dg > lim ? lim : (dg < -lim ? -lim : dg);
            base_cur_[k] = b;
            int32_t end = b;
            if (k == kModLine[0] || k == kModLine[1]) {
                const int m = k == kModLine[0] ? 0 : 1;
                lfo_ph_[m] += lfo_inc_[m] * static_cast<uint32_t>(n);
                end += static_cast<int32_t>(mod_exc_ * static_cast<float>(sine(lfo_ph_[m])));
            }
            const int32_t diff = end - d_cur_[k];
            if (diff > -n && diff < n) { d_cur_[k] = end; step[k] = 0; }          // (within a Q16 unit per sample: there)
            else step[k] = diff / n;
            steady[k] = step[k] == 0 && (d_cur_[k] & 0xFFFF) == 0;
        }
        // 1. the lines' outputs through their gain and damping (lp = c s + p lp), two lines at a time when both hold still
        SEC_BEGIN();
        alignas(4) int16_t tmp0[kSub], tmp1[kSub];
        for (int k = 0; k < N; k += 2) {
            if (steady[k] && steady[k + 1]) {
                const int16_t *r0 = run_of(k, tmp0, n), *r1 = run_of(k + 1, tmp1, n);
                const float c0 = c_[k], p0 = p_[k], c1 = c_[k + 1], p1 = p_[k + 1];
                float l0 = lp_[k], l1 = lp_[k + 1];
                float *f0 = f[k], *f1 = f[k + 1];
                for (int t = 0; t < n; t++) {
                    l0 = c0 * static_cast<float>(r0[t]) + p0 * l0;
                    l1 = c1 * static_cast<float>(r1[t]) + p1 * l1;
                    f0[t] = l0;
                    f1[t] = l1;
                }
                lp_[k] = flush(l0);
                lp_[k + 1] = flush(l1);
            } else {
                for (int j = k; j < k + 2; j++) {
                    if (steady[j]) {
                        const int16_t *r = run_of(j, tmp0, n);
                        const float c = c_[j], pj = p_[j];
                        float l = lp_[j];
                        for (int t = 0; t < n; t++) { l = c * static_cast<float>(r[t]) + pj * l; f[j][t] = l; }
                        lp_[j] = flush(l);
                    } else {
                        read_moving(j, d_cur_[j], step[j], f[j], n);
                        d_cur_[j] += step[j] * n;
                    }
                }
            }
        }
        SEC_MARK(22);                                            // the reads (the matrix and the writes: section 21 minus this)
        // 2. per sample, the eight values in registers: the outputs (two sign patterns, before the input joins), the input on two lines,
        //    the Hadamard butterflies; the matrix's outputs go back into f (row k = matrix output k)
        for (int t = 0; t < n; t++) {
            float u0 = f[0][t], u1 = f[1][t], u2 = f[2][t], u3 = f[3][t], u4 = f[4][t], u5 = f[5][t], u6 = f[6][t], u7 = f[7][t];
            const float o0 = (u0 - u1) - (u2 + u3), o1 = (u4 + u5) + (u6 - u7);
            f[kYL][t] = o0 - o1;                                 // + - - - - - - +
            f[kYR][t] = o0 + o1;                                 // + - - - + + + -
            u2 += f[kXL][t];                                     // the inputs join lines 2 and 5
            u5 += f[kXR][t];
            const float a0 = u0 + u1, a1 = u0 - u1, a2 = u2 + u3, a3 = u2 - u3, a4 = u4 + u5, a5 = u4 - u5, a6 = u6 + u7, a7 = u6 - u7;
            const float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3, b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
            f[0][t] = b0 + b4; f[1][t] = b1 + b5; f[2][t] = b2 + b6; f[3][t] = b3 + b7;
            f[4][t] = b0 - b4; f[5][t] = b1 - b5; f[6][t] = b2 - b6; f[7][t] = b3 - b7;
        }
        // 3. each line written back as one run (split where it crosses the ring's end), saturated to int16 (truncated towards zero: a
        //    quiet tail dies out instead of cycling on its last bit)
        for (int k = 0; k < N; k++) {
            const float *src = f[k];
            const uint32_t at = (w_ + base_[kPerm[k]]) & mask_;
            const int whole = at + n <= mask_ + 1 ? n : static_cast<int>(mask_ + 1 - at);
            int16_t *dst = ring_ + at;
            for (int t = 0; t < whole; t++) dst[t] = sat16(static_cast<int32_t>(src[t]));
            for (int t = whole; t < n; t++) ring_[t - whole] = sat16(static_cast<int32_t>(src[t]));
        }
        w_ += static_cast<uint32_t>(n);
    }

    // Line lengths from the size: each snapped to a prime, all distinct (so pairwise coprime) and in order, within the line's segment.
    void update_lengths() {
        hadamard_delays(size_, len_);
        for (int k = 0; k < N; k++) d_tgt_[k] = len_[k] << 16;
    }
    // Per line, from its length: the decay gain (the same dB per second on every line, T60 = decay) with its sign and H8's 1/sqrt(8), and
    // the damping pole p, set so that the loss at the top per second is the same on every line (the gain at Nyquist of the mean line from
    // `damp`, raised to the power length / mean). c = (1 - p) g.
    void update_gains() {
        float mean = 0.f;
        for (int k = 0; k < N; k++) mean += static_cast<float>(len_[k]) * (1.f / N);
        const float t60 = static_cast<float>(decay_ms_) * 0.001f;
        // the mean line's gain at Nyquist: a one-pole's with its cutoff from Nyquist (damp 0) down to 172 Hz (damp 1), less what that
        // pole still loses at damp 0 (a pole at Nyquist cuts 0.4 dB a pass at 5 kHz), in proportion, so that damp 0 is flat (p = 0)
        auto nyq_log2 = [](float fc) { const float pm = expf(-6.2831853f * fc); return log2f((1.f - pm) / (1.f + pm)); };
        const float d01 = static_cast<float>(damp_) * (1.f / 32767.f);
        const float lg = nyq_log2(0.5f * exp2f(-6.f * d01)) - nyq_log2(0.5f) * (1.f - d01);
        for (int k = 0; k < N; k++) {
            const float len = static_cast<float>(len_[k]);
            const float g = exp2f(-9.96578428f * len / (static_cast<float>(kTankSR) * t60));      // 10^(-3 len / (fs T60))
            const float gk = exp2f(lg * len / mean), pk = (1.f - gk) / (1.f + gk);
            p_[k] = pk;
            c_[k] = (1.f - pk) * g * kSign[k] * 0.353553391f;
        }
    }

    Heap *heap_ = nullptr;
    int16_t *ring_ = nullptr;
    float *apr_ = nullptr;
    uint32_t mask_ = 0, ap_mask_ = 0, w_ = 0;
    uint32_t base_[N] = {}, ap_base_[4] = {};
    int ap_d_[4] = {}, len_[N] = {};
    int32_t d_tgt_[N] = {}, d_cur_[N] = {}, base_cur_[N] = {};   // Q16 delays: the target (a prime), where the pass ended, the glide
    float c_[N] = {}, p_[N] = {}, lp_[N] = {};
    float dl_[5] = {}, dr_[5] = {}, yl_[3] = {}, yr_[3] = {};
    uint32_t lfo_ph_[2] = {}, lfo_inc_[2] = {};
    float mod_exc_ = 0.f, ap_g_ = 0.7f * 0.8f, mix_ = 0.3f;
    int32_t decay_ms_ = 2500, size_ = 22938, damp_ = 13107, mix_q_ = 9830;
    bool idle_ = false;
};

}  // namespace

// Each line's length scaled by the size and snapped to a prime; each above the one before (so all distinct: pairwise coprime), within
// the line's segment of the ring.
void hadamard_delays(int32_t size, int out[8]) {
    const float f = kSizeMin + (1.f - kSizeMin) * static_cast<float>(size) * (1.f / 32767.f);
    int prev = 0;
    for (int k = 0; k < N; k++) {
        int len = scaled_len(k, f);
        if (len < kMinLen) len = kMinLen;
        len = nearest_prime(len);
        if (len <= prev) { len = prev + 1; while (!is_prime(len)) len++; }
        if (len > seg_max(k)) len = seg_max(k);
        prev = len;
        out[k] = len;
    }
}

ModuleType hadamard_reverb_type() { return module_type_of<Hadamard>(); }

}  // namespace sc
