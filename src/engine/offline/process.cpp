#include "engine/offline/process.h"
#include <cmath>
#include <cstring>

namespace sc {

namespace {
inline q15 sat(float v) { return static_cast<q15>(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : static_cast<int32_t>(std::lround(v))); }
}

bool Processor::begin(const SmpHeader &in, ProcOp op, int32_t amount) {
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
    if (op == ProcOp::Gain) gain_ = std::pow(10.0f, static_cast<float>(amount) / 200.0f);
    phase_ = (op == ProcOp::Normalize || op == ProcOp::TrimSilence) ? Phase::Analyze : Phase::Write;
    if (phase_ == Phase::Write) out_.frames = op == ProcOp::PadTo && amount > static_cast<int32_t>(in.frames) ? static_cast<uint32_t>(amount) : in.frames;
    return true;
}

float Processor::progress() const {
    if (phase_ == Phase::Done) return 1.0f;
    const bool two = op_ == ProcOp::Normalize || op_ == ProcOp::TrimSilence;
    const float total = static_cast<float>(in_.frames) * (two ? 2.0f : 1.0f);
    const float done = (phase_ == Phase::Write && two ? static_cast<float>(in_.frames) : 0.0f) + static_cast<float>(pos_);
    return total > 0.0f ? done / total : 1.0f;
}

uint32_t Processor::source_of(uint32_t o) const { return op_ == ProcOp::TrimSilence ? first_ + o : o; }

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
        const uint32_t n = out_.frames - pos_ < kChunk ? out_.frames - pos_ : kChunk;
        if (op_ == ProcOp::Reverse) {                                         // output [pos_, pos_ + n) = the source's [frames - pos_ - n, frames - pos_) backwards
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
            if (op_ == ProcOp::TrimSilence) {                                 // loop and slices move with the cut; what falls outside goes
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
            }
            phase_ = Phase::Done;
        }
    }
    return true;
}

}  // namespace sc
