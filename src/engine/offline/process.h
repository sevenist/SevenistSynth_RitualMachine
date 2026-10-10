#pragma once
// Offline processing of a sample (ADR-041 decisions 36-38: the pads' sample tools). Mono q15 in, mono q15 out, block by block: the card
// holds the file, nothing the size of the sample is ever in memory (the prototype has ~60 KB of PSRAM to spare). The caller supplies the
// reader (any frame range of the source) and the writer (the destination, frames in order); Processor runs in steps of a few blocks so
// the card task can keep the sample streams fed in between (platform/engine/render_job.cpp). Not the audio thread: floats are fine here.
#include <cstdint>
#include "engine/dsp/q.h"
#include "engine/sampler/smp_format.h"

namespace sc {

struct FrameReader {
    virtual ~FrameReader() = default;
    virtual bool read(uint32_t frame, uint32_t n, q15 *dst) = 0;      // frames [frame, frame + n) of the source (inside it)
};
struct FrameWriter {
    virtual ~FrameWriter() = default;
    virtual bool write(const q15 *src, uint32_t n) = 0;              // the next n frames of the result
};

// Level & cleanup (decision 38: the first group). amount:
//   Normalize   the peak's target in 0.1 dBFS (-3 = -0.3 dB)          Gain       0.1 dB (-240..+240), saturating
//   FadeIn/Out  ms from the file's start / to its end (linear)         TrimSilence the threshold in dBFS (-90..-6): what is under it at
//                                                                      both ends goes (5 ms kept before the first sound)
//   Reverse     -                                                      RemoveDc   - (a 10 Hz one-pole high-pass)
//   PadTo       the length in frames: silence appended up to it (a Grid chop that fills whole grid units, decision 43); a shorter
//               length keeps the sample as it is
enum class ProcOp : uint8_t { Normalize, Gain, FadeIn, FadeOut, TrimSilence, Reverse, RemoveDc, PadTo, Count };

class Processor {
public:
    static constexpr uint32_t kChunk = kSmpBlockFrames;                   // frames per step unit
    // The source's header and the operation. False when there is nothing to do (an empty sample).
    bool begin(const SmpHeader &in, ProcOp op, int32_t amount);
    // Up to `chunks` chunks of work (analysis first when the operation needs one, then the output). False on a read / write error.
    bool step(FrameReader &r, FrameWriter &w, int chunks);
    bool done() const { return phase_ == Phase::Done; }
    float progress() const;                                               // 0..1 over the analysis and the output
    // The result's header (frames, rate, loop and slices carried over: shifted by a trim, mirrored by a reverse; the overview of the
    // output): valid once done().
    const SmpHeader &out_header() const { return out_; }

private:
    enum class Phase : uint8_t { Analyze, Write, Done };
    uint32_t source_of(uint32_t o) const;                                 // the first source frame of output frame o's chunk (Reverse: see write)
    SmpHeader in_, out_;
    ProcOp op_ = ProcOp::Gain;
    int32_t amount_ = 0;
    Phase phase_ = Phase::Done;
    uint32_t pos_ = 0;                                                    // frames done in the current phase
    uint32_t first_ = 0, last_ = 0;                                       // the trim: the kept region [first_, last_)
    int32_t peak_ = 0;                                                    // the analysis: the largest |sample|
    float gain_ = 1.0f;
    float dc_x_ = 0.0f, dc_y_ = 0.0f, dc_r_ = 0.999f;                     // the DC blocker's state
    uint32_t bucket_peak_[kSmpPeaks] = {};
    q15 buf_[kChunk];
};

}  // namespace sc
