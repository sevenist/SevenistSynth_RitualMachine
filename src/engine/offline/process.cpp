#include "engine/offline/process.h"
#include <cmath>
#include <cstring>

namespace sc {

namespace {
inline q15 sat(float v) { return static_cast<q15>(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : static_cast<int32_t>(std::lround(v))); }
constexpr float kPi = 3.14159265f;
inline float soft(float x) { return x >= 3.0f ? 1.0f : x <= -3.0f ? -1.0f : x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }   // ~tanh
inline uint32_t scaled(uint32_t v, uint32_t num, uint32_t den) { return den ? static_cast<uint32_t>((static_cast<uint64_t>(v) * num + den / 2) / den) : v; }
}

/* ---------------- time & pitch: WSOLA and the resampler ---------------- */

bool Processor::fetch(FrameReader &r, int64_t start, uint32_t n, q15 *dst) {
    const int64_t end = start + n, len = in_.frames;
    const int64_t a = start < 0 ? 0 : start, b = end > len ? len : end;
    if (b <= a) { std::memset(dst, 0, n * sizeof(q15)); return true; }
    if (a > start) std::memset(dst, 0, static_cast<size_t>(a - start) * sizeof(q15));
    if (!r.read(static_cast<uint32_t>(a), static_cast<uint32_t>(b - a), dst + (a - start))) return false;
    if (end > b) std::memset(dst + (b - start), 0, static_cast<size_t>(end - b) * sizeof(q15));
    return true;
}

// The offset (-srch_..srch_) at which seg_ looks most like tgt_, the continuation of the previous window: the normalised correlation
// over the overlap, on every 4th frame (12-bit values: the sums stay inside int32), every 4th offset first, then the 7 around the best.
int Processor::ws_search() const {
    const int m = hs_ / 4;
    auto score = [&](int d) {
        const q15 *s = seg_ + srch_ + d;
        int32_t c = 0, e = 0;
        for (int i = 0; i < m; i++) { const int32_t v = s[4 * i] >> 4; c += tgt_[i] * v; e += v * v; }
        return c <= 0 ? static_cast<float>(c) : static_cast<float>(c) / std::sqrt(static_cast<float>(e) + 1.0f);
    };
    int best = 0;
    float top = score(0);
    for (int d = -srch_; d <= srch_; d += 4) { const float s = score(d); if (s > top) { top = s; best = d; } }
    const int mid = best;
    for (int d = mid - 3; d <= mid + 3; d++) {
        if (d < -srch_ || d > srch_ || d == mid) continue;
        const float s = score(d);
        if (s > top) { top = s; best = d; }
    }
    return best;
}

bool Processor::ws_hop(FrameReader &r) {
    const int hs = hs_;
    const int64_t a0 = static_cast<int64_t>(static_cast<uint64_t>(ws_k_) * static_cast<uint32_t>(hs) * in_.frames / ws_len_);   // the ideal place
    if (!fetch(r, a0 - srch_, static_cast<uint32_t>(2 * hs + 2 * srch_), seg_)) return false;
    int d = 0;
    if (ws_k_ > 0) {
        const int64_t natural = ws_prev_ + hs - a0;                           // going straight on from the previous window: no seam at all
        d = natural >= -srch_ && natural <= srch_ ? static_cast<int>(natural) : ws_search();
    }
    const q15 *s = seg_ + srch_ + d;
    // a Hann window in two halves that sum to 1: w rises over the first hop, 1 - w falls over the second (the first window of the
    // file does not rise: the attack stays as it is)
    const float th = kPi / static_cast<float>(hs), ct = std::cos(th), st = std::sin(th);
    float c = 1.0f, sn = 0.0f;
    for (int n = 0; n < hs; n++) {
        const float w = 0.5f - 0.5f * c;
        ready_[n] = ws_k_ ? sat(w * static_cast<float>(s[n]) + static_cast<float>(tail_[n])) : s[n];
        tail_[n] = sat((1.0f - w) * static_cast<float>(s[hs + n]));
        const float c2 = c * ct - sn * st;
        sn = sn * ct + c * st;
        c = c2;
    }
    for (int i = 0; i < hs / 4; i++) tgt_[i] = static_cast<int16_t>(s[hs + 4 * i] >> 4);
    ws_prev_ = a0 + d;
    ws_k_++;
    ready_n_ = static_cast<uint32_t>(hs);
    ready_at_ = 0;
    return true;
}

bool Processor::ws_pull(FrameReader &r, q15 *dst, uint32_t n) {
    while (n) {
        if (ready_at_ >= ready_n_ && !ws_hop(r)) return false;
        const uint32_t k = ready_n_ - ready_at_ < n ? ready_n_ - ready_at_ : n;
        std::memcpy(dst, ready_ + ready_at_, k * sizeof(q15));
        ready_at_ += k; dst += k; n -= k;
    }
    return true;
}

bool Processor::stage1(FrameReader &r, q15 *dst, uint32_t n) {
    const bool ok = op_ == ProcOp::Pitch ? ws_pull(r, dst, n) : fetch(r, s1_pos_, n, dst);
    if (ok && s1_pos_ + n > s1_len_) {                                        // past its end: silence
        const uint32_t keep = s1_pos_ < s1_len_ ? s1_len_ - s1_pos_ : 0;
        std::memset(dst + keep, 0, (n - keep) * sizeof(q15));
    }
    s1_pos_ += n;
    return ok;
}

bool Processor::rs_fill(FrameReader &r, q15 *dst, uint32_t n) {
    const int half = kRsTaps / 2;
    for (uint32_t i = 0; i < n; i++) {
        const uint64_t t = static_cast<uint64_t>(pos_ + i) * rs_step_;       // the place in the input, Q32
        const int64_t i0 = static_cast<int64_t>(t >> 32);
        while (rs_base_ + rs_n_ <= i0 + half) {                               // the window i0 - 15 .. i0 + 16 must be in the buffer
            const int64_t drop = i0 - (half - 1) - rs_base_;
            if (drop > 0) {
                const uint32_t dn = drop < rs_n_ ? static_cast<uint32_t>(drop) : rs_n_;
                std::memmove(rs_buf_, rs_buf_ + dn, (rs_n_ - dn) * sizeof(q15));
                rs_base_ += dn; rs_n_ -= dn;
            }
            const uint32_t room = static_cast<uint32_t>(kRsBuf) - rs_n_, m = room < 512 ? room : 512;
            if (!stage1(r, rs_buf_ + rs_n_, m)) return false;
            rs_n_ += m;
        }
        const uint32_t fr = static_cast<uint32_t>(t) >> 8;                   // the fraction, 24 bits
        const uint32_t ph = fr >> 19;                                         // 0..31
        const float u = static_cast<float>(fr & 0x7FFFF) * (1.0f / 524288.0f);
        const float *ta = rs_tab_ + ph * kRsTaps, *tb = ta + kRsTaps;
        const q15 *x = rs_buf_ + (i0 - (half - 1) - rs_base_);
        float acc = 0.0f;
        for (int j = 0; j < kRsTaps; j++) acc += (ta[j] + u * (tb[j] - ta[j])) * static_cast<float>(x[j]);
        dst[i] = sat(acc);
    }
    return true;
}

bool Processor::begin(const SmpHeader &in, ProcOp op, int32_t amount, int32_t amount2) {
    in_ = in;
    out_ = in;
    op_ = op;
    amount_ = amount;
    pos_ = 0;
    peak_ = 0;
    gain_ = 1.0f;
    first_ = 0;
    last_ = in.frames;
    dc_x_ = dc_y_ = 0.0f;
    dc_r_ = 1.0f - 2.0f * 3.14159265f * 10.0f / static_cast<float>(in.sample_rate ? in.sample_rate : 48000);
    std::memset(bucket_peak_, 0, sizeof bucket_peak_);
    if (!in.frames) { phase_ = Phase::Done; return false; }
    if (timed()) {
        const uint32_t rate = in.sample_rate ? in.sample_rate : 48000;
        hs_ = static_cast<int>(static_cast<uint64_t>(kWsHop) * rate / 48000) & ~3;   // ~21 ms, a multiple of 4 (the search's stride)
        hs_ = hs_ < 64 ? 64 : hs_ > kWsHop ? kWsHop : hs_;
        srch_ = hs_ / 2;
        double ratio = 1.0;                                                   // of the stretch
        if (op == ProcOp::Pitch) ratio = std::pow(2.0, static_cast<double>(amount < -2400 ? -2400 : amount > 2400 ? 2400 : amount) / 1200.0);
        else ratio = static_cast<double>(amount < 250000 ? 250000 : amount > 4000000 ? 4000000 : amount) / 1e6;
        const double len = static_cast<double>(in.frames) * ratio;
        const uint32_t scaled_len = len < 1.0 ? 1u : static_cast<uint32_t>(len + 0.5);
        ws_len_ = scaled_len;
        ws_k_ = 0; ws_prev_ = 0; ready_n_ = ready_at_ = 0;
        out_.frames = op == ProcOp::Pitch ? in.frames : scaled_len;
        s1_len_ = op == ProcOp::Pitch ? scaled_len : in.frames;               // the resampler: s1_len_ frames in, out_.frames out
        s1_pos_ = 0;
        rs_step_ = (static_cast<uint64_t>(s1_len_) << 32) / out_.frames;
        rs_base_ = -(kRsTaps / 2); rs_n_ = kRsTaps / 2;                       // (silence before the first frame)
        std::memset(rs_buf_, 0, sizeof rs_buf_);
        // the kernel: a Hann-windowed sinc, its cutoff at the lower of the two Nyquists, every phase normalised to a gain of 1
        const float fc = s1_len_ > out_.frames ? static_cast<float>(out_.frames) / static_cast<float>(s1_len_) : 1.0f;
        for (int p = 0; p <= kRsPhases; p++) {
            float sum = 0.0f, *t = rs_tab_ + p * kRsTaps;
            for (int j = 0; j < kRsTaps; j++) {
                const float x = static_cast<float>(j - (kRsTaps / 2 - 1)) - static_cast<float>(p) / kRsPhases;
                const float a = kPi * fc * x, w = 0.5f + 0.5f * std::cos(kPi * x / (kRsTaps / 2));
                t[j] = (std::fabs(a) < 1e-6f ? 1.0f : std::sin(a) / a) * (std::fabs(x) < kRsTaps / 2 ? w : 0.0f);
                sum += t[j];
            }
            for (int j = 0; j < kRsTaps; j++) t[j] /= sum;
        }
        phase_ = Phase::Write;
        return true;
    }
    if (op == ProcOp::Gain) gain_ = std::pow(10.0f, static_cast<float>(amount) / 200.0f);
    const uint32_t rate = in.sample_rate ? in.sample_rate : 48000;
    if (op == ProcOp::Decimate) {
        dec_rate_ = amount < 100 ? 100u : static_cast<uint32_t>(amount) > rate ? rate : static_cast<uint32_t>(amount);
        dec_acc_ = rate - dec_rate_;                                          // (the first frame is taken)
        held_ = 0.0f;
    } else if (op == ProcOp::Bits) {
        k1_ = static_cast<float>(1 << (16 - (amount < 1 ? 1 : amount > 16 ? 16 : amount)));
    } else if (op == ProcOp::Drive) {
        const float g = std::pow(10.0f, static_cast<float>(amount < 0 ? 0 : amount > 360 ? 360 : amount) / 200.0f);
        k1_ = g / 32768.0f;
        k2_ = 32767.0f / soft(g);
    } else if (op == ProcOp::LowPass || op == ProcOp::HighPass) {
        const float top = 0.45f * static_cast<float>(rate), fc = static_cast<float>(amount) < 20.0f ? 20.0f : static_cast<float>(amount) > top ? top : static_cast<float>(amount);
        const float g = std::tan(kPi * fc / static_cast<float>(rate)), k = 1.41421356f;
        f_a1_ = 1.0f / (1.0f + g * (g + k));
        f_a2_ = g * f_a1_;
        f_a3_ = g * f_a2_;
        f_s1_ = f_s2_ = 0.0f;
    }
    phase_ = (op == ProcOp::Normalize || op == ProcOp::TrimSilence) ? Phase::Analyze : Phase::Write;
    if (op == ProcOp::Crop) {
        first_ = amount < 0 ? 0u : static_cast<uint32_t>(amount) >= in.frames ? in.frames - 1 : static_cast<uint32_t>(amount);
        last_ = amount2 <= 0 || static_cast<uint32_t>(amount2) > in.frames ? in.frames : static_cast<uint32_t>(amount2);
        if (last_ <= first_) last_ = first_ + 1;
        out_.frames = last_ - first_;
    } else if (phase_ == Phase::Write) {
        out_.frames = op == ProcOp::PadTo && amount > static_cast<int32_t>(in.frames) ? static_cast<uint32_t>(amount) : in.frames;
    }
    return true;
}

float Processor::progress() const {
    if (phase_ == Phase::Done) return 1.0f;
    const bool two = op_ == ProcOp::Normalize || op_ == ProcOp::TrimSilence;
    const uint32_t len = phase_ == Phase::Analyze ? in_.frames : out_.frames;
    const float part = len ? static_cast<float>(pos_) / static_cast<float>(len) : 1.0f;
    return two ? (phase_ == Phase::Write ? 0.5f : 0.0f) + 0.5f * part : part;
}

uint32_t Processor::source_of(uint32_t o) const { return op_ == ProcOp::TrimSilence || op_ == ProcOp::Crop ? first_ + o : o; }

bool Processor::step(FrameReader &r, FrameWriter &w, int chunks) {
    for (int c = 0; c < chunks && phase_ != Phase::Done; c++) {
        if (phase_ == Phase::Analyze) {
            const uint32_t n = in_.frames - pos_ < kChunk ? in_.frames - pos_ : kChunk;
            if (!r.read(pos_, n, buf_)) return false;
            if (op_ == ProcOp::Normalize) {
                for (uint32_t i = 0; i < n; i++) { const int32_t v = buf_[i] < 0 ? -buf_[i] : buf_[i]; if (v > peak_) peak_ = v; }
            } else {                                                          // the trim: the first and the last frame over the threshold
                const int32_t thr = static_cast<int32_t>(32768.0f * std::pow(10.0f, static_cast<float>(amount_) / 20.0f));
                for (uint32_t i = 0; i < n; i++) {
                    const int32_t v = buf_[i] < 0 ? -buf_[i] : buf_[i];
                    if (v >= thr) { if (!peak_) first_ = pos_ + i; peak_ = 1; last_ = pos_ + i + 1; }
                }
            }
            pos_ += n;
            if (pos_ >= in_.frames) {                                         // the analysis is complete: the output's length and gain
                if (op_ == ProcOp::Normalize) {
                    const float target = 32767.0f * std::pow(10.0f, static_cast<float>(amount_) / 200.0f);
                    gain_ = peak_ ? target / static_cast<float>(peak_) : 1.0f;
                } else {
                    if (!peak_) { first_ = 0; last_ = in_.frames; }           // all under the threshold: kept whole
                    const uint32_t keep = in_.sample_rate / 200;              // 5 ms before the first sound, 5 ms after the last
                    first_ = first_ > keep ? first_ - keep : 0;
                    last_ = last_ + keep < in_.frames ? last_ + keep : in_.frames;
                }
                out_.frames = last_ - first_;
                phase_ = Phase::Write;
                pos_ = 0;
            }
            continue;
        }
        // the output, a chunk at a time
        const uint32_t unit = op_ == ProcOp::Stretch || op_ == ProcOp::Pitch ? static_cast<uint32_t>(hs_) : kChunk;   // (a hop is the heavy unit)
        const uint32_t n = out_.frames - pos_ < unit ? out_.frames - pos_ : unit;
        if (timed()) {
            if (!(op_ == ProcOp::Stretch ? ws_pull(r, buf_, n) : rs_fill(r, buf_, n))) return false;
        } else if (op_ == ProcOp::Reverse) {                                         // output [pos_, pos_ + n) = the source's [frames - pos_ - n, frames - pos_) backwards
            if (!r.read(in_.frames - pos_ - n, n, buf_)) return false;
            for (uint32_t i = 0; i < n / 2; i++) { const q15 t = buf_[i]; buf_[i] = buf_[n - 1 - i]; buf_[n - 1 - i] = t; }
        } else if (op_ == ProcOp::PadTo && pos_ + n > in_.frames) {           // past the source: the silence (and the source's last part)
            const uint32_t have = pos_ < in_.frames ? in_.frames - pos_ : 0;
            if (have && !r.read(pos_, have, buf_)) return false;
            for (uint32_t i = have; i < n; i++) buf_[i] = 0;
        } else if (!r.read(source_of(pos_), n, buf_)) {
            return false;
        }
        const float fade = static_cast<float>(amount_) * static_cast<float>(in_.sample_rate) / 1000.0f;   // frames
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t o = pos_ + i;
            float v = buf_[i];
            switch (op_) {
                case ProcOp::Normalize: case ProcOp::Gain: v *= gain_; break;
                case ProcOp::FadeIn:  if (fade > 0.0f && o < fade) v *= static_cast<float>(o) / fade; break;
                case ProcOp::FadeOut: { const float left = static_cast<float>(out_.frames - 1 - o); if (fade > 0.0f && left < fade) v *= left / fade; } break;
                case ProcOp::RemoveDc: { const float y = v - dc_x_ + dc_r_ * dc_y_; dc_x_ = v; dc_y_ = y; v = y; } break;
                case ProcOp::Decimate: dec_acc_ += dec_rate_; if (dec_acc_ >= in_.sample_rate) { dec_acc_ -= in_.sample_rate; held_ = v; } v = held_; break;
                case ProcOp::Bits: v = std::floor(v / k1_ + 0.5f) * k1_; break;
                case ProcOp::Drive: v = soft(v * k1_) * k2_; break;
                case ProcOp::LowPass: case ProcOp::HighPass: {
                    const float v3 = v - f_s2_, v1 = f_a1_ * f_s1_ + f_a2_ * v3, v2 = f_s2_ + f_a2_ * f_s1_ + f_a3_ * v3;
                    f_s1_ = 2.0f * v1 - f_s1_;
                    f_s2_ = 2.0f * v2 - f_s2_;
                    v = op_ == ProcOp::LowPass ? v2 : v - 1.41421356f * v1 - v2;
                } break;
                default: break;
            }
            buf_[i] = sat(v);
            const int32_t a = buf_[i] < 0 ? -buf_[i] : buf_[i];
            const uint32_t b = static_cast<uint32_t>(static_cast<uint64_t>(o) * kSmpPeaks / out_.frames);
            if (static_cast<uint32_t>(a) > bucket_peak_[b]) bucket_peak_[b] = static_cast<uint32_t>(a);
        }
        if (!w.write(buf_, n)) return false;
        pos_ += n;
        if (pos_ >= out_.frames) {                                            // the header of the result
            for (int b = 0; b < kSmpPeaks; b++) out_.peaks[b] = static_cast<uint8_t>(bucket_peak_[b] >> 7 > 255 ? 255 : bucket_peak_[b] >> 7);
            if (!out_.peaks[0]) out_.peaks[0] = 1;                            // (all zero would read as "not stored")
            if (op_ == ProcOp::TrimSilence || op_ == ProcOp::Crop) {          // loop and slices move with the cut; what falls outside goes
                const uint32_t f = first_, l = last_;
                if (in_.has_loop() && in_.loop_start >= f && in_.loop_end <= l) { out_.loop_start = in_.loop_start - f; out_.loop_end = in_.loop_end - f; }
                else { out_.loop_start = out_.loop_end = 0; out_.loop_mode = SMP_LOOP_OFF; }
                out_.slice_count = 0;
                for (int s = 0; s < in_.slice_count; s++)
                    if (in_.slice[s] >= f && in_.slice[s] < l) out_.slice[out_.slice_count++] = in_.slice[s] - f;
                for (int s = out_.slice_count; s < kSmpMaxSlices; s++) out_.slice[s] = 0;
            } else if (op_ == ProcOp::Reverse) {                              // the loop mirrored; slices make no sense backwards: dropped
                if (in_.has_loop()) { out_.loop_start = in_.frames - in_.loop_end; out_.loop_end = in_.frames - in_.loop_start; }
                out_.slice_count = 0;
                for (auto &s : out_.slice) s = 0;
            } else if (op_ == ProcOp::Stretch || op_ == ProcOp::Resample) {   // loop and slices scale with the length
                out_.loop_start = scaled(in_.loop_start, out_.frames, in_.frames);
                out_.loop_end = scaled(in_.loop_end, out_.frames, in_.frames);
                for (int s = 0; s < in_.slice_count; s++) out_.slice[s] = scaled(in_.slice[s], out_.frames, in_.frames);
            }
            phase_ = Phase::Done;
        }
    }
    return true;
}

}  // namespace sc
