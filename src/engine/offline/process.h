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
//
// Time & pitch (the second group), all in the time domain:
//   Stretch     the new length in ppm of the old one (250000..4000000 = x0.25..x4), the pitch kept: WSOLA (overlap-add of ~43 ms
//               windows, each taken where it continues the previous one best; the first window keeps the attack untouched)
//   Resample    the new length in ppm of the old one: the speed changes and the pitch with it (a tape's speed); a 32-tap windowed
//               sinc, its cutoff lowered when the result is shorter
//   Pitch       cents (-2400..+2400), the length kept: Stretch by the pitch ratio, then Resample back, in one pass
// Loop and slices scale with the length.
//
//   Crop        frames [amount, amount2) only (amount2 0 = to the end); loop and slices move with it, what falls outside goes
//
// Color / lo-fi (the fourth group), the length kept:
//   Decimate    the rate in Hz the sample is held at (no filter before it: the folding is the point)
//   Bits        the word length (1..16): rounded to that many bits
//   Drive       0.1 dB of gain into a soft clipper (0..360); full scale stays full scale
//   LowPass / HighPass   the cutoff in Hz, 12 dB per octave (Butterworth)
enum class ProcOp : uint8_t { Normalize, Gain, FadeIn, FadeOut, TrimSilence, Reverse, RemoveDc, PadTo, Stretch, Resample, Pitch,
                              Crop, Decimate, Bits, Drive, LowPass, HighPass, Count };

class Processor {
public:
    static constexpr uint32_t kChunk = kSmpBlockFrames;                   // frames per step unit
    static constexpr int kWsHop = 1024, kWsSearch = kWsHop / 2;           // WSOLA at 48 kHz: the hop (half a window), the search each way
    static constexpr int kRsTaps = 32, kRsPhases = 32, kRsBuf = 1024;     // the resampler: taps, table phases, its input window
    // The source's header and the operation. False when there is nothing to do (an empty sample).
    bool begin(const SmpHeader &in, ProcOp op, int32_t amount, int32_t amount2 = 0);
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
    bool timed() const { return op_ == ProcOp::Stretch || op_ == ProcOp::Resample || op_ == ProcOp::Pitch; }
    bool fetch(FrameReader &r, int64_t start, uint32_t n, q15 *dst);      // source frames, zeros outside the file
    int  ws_search() const;                                               // the offset in seg_ that continues the previous window best
    bool ws_hop(FrameReader &r);                                          // the next hs_ stretched frames into ready_
    bool ws_pull(FrameReader &r, q15 *dst, uint32_t n);                   // the next n frames of the stretched signal
    bool stage1(FrameReader &r, q15 *dst, uint32_t n);                    // the next n frames of what the resampler reads
    bool rs_fill(FrameReader &r, q15 *dst, uint32_t n);                   // the next n resampled frames (from output frame pos_)
    SmpHeader in_, out_;
    ProcOp op_ = ProcOp::Gain;
    int32_t amount_ = 0;
    Phase phase_ = Phase::Done;
    uint32_t pos_ = 0;                                                    // frames done in the current phase
    uint32_t first_ = 0, last_ = 0;                                       // the trim: the kept region [first_, last_)
    int32_t peak_ = 0;                                                    // the analysis: the largest |sample|
    float gain_ = 1.0f;
    float dc_x_ = 0.0f, dc_y_ = 0.0f, dc_r_ = 0.999f;                     // the DC blocker's state
    uint32_t dec_rate_ = 0, dec_acc_ = 0;                                 // Decimate: the hold's rate and its phase
    float held_ = 0.0f;
    float k1_ = 1.0f, k2_ = 1.0f;                                         // Bits: the step; Drive: gain in, gain out
    float f_a1_ = 0.0f, f_a2_ = 0.0f, f_a3_ = 0.0f, f_s1_ = 0.0f, f_s2_ = 0.0f;   // the filter (a state-variable one) and its state
    uint32_t bucket_peak_[kSmpPeaks] = {};
    q15 buf_[kChunk];
    // WSOLA (Stretch, and the first stage of Pitch)
    int hs_ = kWsHop, srch_ = kWsSearch;                                  // hop and search at the file's rate
    uint32_t ws_len_ = 0, ws_k_ = 0;                                      // the stretched length, the hop's number
    int64_t ws_prev_ = 0;                                                 // where the previous window was taken
    uint32_t ready_n_ = 0, ready_at_ = 0;
    q15 seg_[2 * kWsHop + 2 * kWsSearch];                                 // the source around the hop's ideal position
    q15 tail_[kWsHop];                                                    // the previous window's falling half
    q15 ready_[kWsHop];
    int16_t tgt_[kWsHop / 4];                                             // what the next window should continue (every 4th frame, 12 bits)
    // the resampler (Resample, and the second stage of Pitch)
    uint32_t s1_len_ = 0, s1_pos_ = 0;                                    // its input: length, frames taken
    uint64_t rs_step_ = 0;                                                // input frames per output frame, Q32
    int64_t rs_base_ = 0;                                                 // the input frame of rs_buf_[0]
    uint32_t rs_n_ = 0;
    q15 rs_buf_[kRsBuf];
    float rs_tab_[(kRsPhases + 1) * kRsTaps];                             // the kernel at kRsPhases + 1 fractions
};

}  // namespace sc
