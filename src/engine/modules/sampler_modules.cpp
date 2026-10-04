#include "engine/modules/sampler_modules.h"
#include <algorithm>
#include <cmath>
#include "engine/dsp/block.h"
#include "engine/dsp/interp.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/util.h"
#include "engine/modules/builtin.h"
#include "engine/sampler/sample_bank.h"

namespace sc {
namespace {

constexpr int32_t kSemi = 256;
inline int32_t scaled(q15 m, int32_t range) { return static_cast<int32_t>((static_cast<int64_t>(m) * range) >> 15); }

/* ------------------------------------------------------------------ Sampler */

class Sampler : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Sampler", Scope::Voice, 1, 1, SMPR_N, false, {"pitch"}, {"out"},
            {{"sample", -1, -1, 31}, {"instrument", -1, -1, 7}, {"tune", 0, -48 * kSemi, 48 * kSemi}, {"root", -1, -1, 127 * kSemi},
             {"loop", -1, -1, 2}, {"reverse", 0, 0, 1}, {"start", 0, 0, 60000 * 16}, {"slice", -1, -1, 15}, {"slice_mode", 0, 0, 1},
             {"gain", kUnity, 0, kUnity}, {"interp", 1, 0, 1}, {"track", 1, 0, 1}}};
        return i;
    }
    bool init(Memory &m) override {
        bank_ = m.bank;
        if (!bank_) return true;                                           // no sampler service: the module stays silent
        ring_heap_ = m.bulk;
        ring_ = static_cast<q15 *>(m.bulk->alloc(static_cast<size_t>(kRingBlocks) * kSmpBlockBytes));
        if (!ring_) return false;
        stream_.attach(ring_);
        bank_->add_stream(&stream_);
        return true;
    }
    ~Sampler() override {
        if (bank_) bank_->remove_stream(&stream_);
        if (ring_heap_) ring_heap_->free(ring_);
    }
    void reset() override { want_start_ = true; retry_ = 0; active_ = false; held_ = 0; xf_ = 32767; underrun_ = false; if (bank_ && ring_) stream_.stop(); }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case SMPR_SAMPLE: sample_ = v; break;
            case SMPR_INSTRUMENT: inst_ = v; break;
            case SMPR_TUNE: tune_ = v; break;
            case SMPR_ROOT: root_ = v; break;
            case SMPR_LOOP: loop_param_ = v; break;
            case SMPR_REVERSE: reverse_ = v != 0; break;
            case SMPR_START: start_ = v; break;
            case SMPR_SLICE: slice_ = v; break;
            case SMPR_SLICE_MODE: slice_mode_ = v; break;
            case SMPR_GAIN: gain_ = static_cast<q15>(v); break;
            case SMPR_INTERP: hermite_ = v != 0; break;
            case SMPR_TRACK: track_ = v != 0; break;
        }
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const int n = ctx.frames;
        if (!bank_ || !ring_) { block_clear(p.out[0], n); return; }
        if (want_start_) {
            if (begin(ctx.voice)) want_start_ = false;
            else if (++retry_ > 64) want_start_ = false;                    // the sample never became ready: stay silent
        }
        if (!active_) { block_clear(p.out[0], n); return; }

        const q15 *cv = p.in[0], *mt = p.mod[SMPR_TUNE];
        const bool varying = mt || cv[0] != cv[n - 1];
        int64_t rate = rate_for(cv[0], mt ? mt[0] : 0);
        for (int i = 0; i < n; i++) {
            if (varying) rate = rate_for(cv[i], mt ? mt[i] : 0);
            const int64_t f = pos_ >> 32;
            const uint32_t frac = static_cast<uint32_t>((pos_ >> 16) & 0xFFFF);
            q15 y1 = 0, y2 = 0;
            bool ok = fetch(f, y1) & fetch(f + 1, y2);
            int32_t s;
            if (hermite_) {
                q15 y0 = 0, y3 = 0;
                ok &= fetch(f - 1, y0) & fetch(f + 2, y3);
                s = hermite15(y0, y1, y2, y3, frac);
            } else {
                s = lerp15(y1, y2, frac);
            }
            // Underrun: the output keeps the value it had and decays by 1/64 per sample (a half-interpolated value would
            // click); the timeline keeps running. When the data is back, the output crosses over from the held value to
            // the live signal in 64 samples, so it is continuous at both ends whatever the length of the gap.
            int32_t y;
            if (ok) {
                if (underrun_) { underrun_ = false; xf_ = 0; }
                if (xf_ < 32767) {
                    xf_ = xf_ + 512 > 32767 ? 32767 : xf_ + 512;
                    y = held_ + static_cast<int32_t>((static_cast<int64_t>(s - held_) * xf_) >> 15);
                } else {
                    y = s;
                }
                held_ = y;
            } else {
                if (!underrun_) { underrun_ = true; bank_->stats.underruns.fetch_add(1, std::memory_order_relaxed); }
                held_ -= held_ >> 6;
                y = held_;
            }
            p.out[0][i] = mul15(sat16(y), gain_eff_);

            pos_ += dir_ > 0 ? rate : -rate;
            if (!wrap_position(pos_, dir_, loop_, frames_) || (stop_frame_ && dir_ > 0 && (pos_ >> 32) >= stop_frame_)) {
                active_ = false;
                for (int k = i + 1; k < n; k++) p.out[0][k] = 0;
                stream_.stop();
                return;
            }
        }
        stream_.update(static_cast<uint32_t>(pos_ >> 32), dir_, static_cast<int32_t>(rate >> 16));
    }

private:
    int64_t rate_for(q15 cv, q15 mod) const {
        int32_t pitch;
        if (track_) pitch = kPitchCvCenter + scaled(cv, kPitchCvSpan) + tune_total_ + (mod ? scaled(mod, 96 * kSemi) : 0);
        else pitch = root_pitch_ + tune_total_ + (mod ? scaled(mod, 96 * kSemi) : 0);
        const int64_t oct_q16 = static_cast<int64_t>(pitch - root_pitch_) * 64 / 3;                 // 65536 / (12 * 256)
        const int32_t x = static_cast<int32_t>(oct_q16 < -(1 << 21) ? -(1 << 21) : (oct_q16 > (1 << 21) ? (1 << 21) : oct_q16));
        const uint32_t ratio_q28 = exp2_scale(base_q28_, x);                                         // source frames per output sample
        return static_cast<int64_t>(ratio_q28) << 4;                                                 // Q32
    }

    bool begin(const VoiceState *v) {
        const int note = v ? v->note : 60;
        const int vel = v ? (v->velocity * 127 + 16383) / 32767 : 100;
        int slot = sample_;
        const Zone *zone = nullptr;
        if (inst_ >= 0) {
            zone = bank_->pick(inst_, note, vel);
            if (!zone) return true;                                                                  // nothing mapped here: silent, done
            slot = zone->sample;
        }
        const SampleSlot *sl = bank_->slot(slot);
        if (!sl || sl->state == SLOT_FAILED) return true;
        if (sl->state != SLOT_READY) return false;                                                   // still loading: try again next block
        const SmpHeader &h = sl->h;
        frames_ = h.frames;
        const int mode = loop_param_ >= 0 ? loop_param_ : h.loop_mode;
        loop_ = LoopInfo{};
        loop_.mode = static_cast<uint8_t>(mode);
        if (mode != SMP_LOOP_OFF) { loop_.start = h.has_loop() ? h.loop_start : 0; loop_.end = h.has_loop() ? h.loop_end : h.frames; }
        loop_.mode = loop_.active() ? static_cast<uint8_t>(mode) : static_cast<uint8_t>(SMP_LOOP_OFF);

        uint32_t start = 0;
        stop_frame_ = 0;
        if (slice_ >= 0 && slice_ < h.slice_count) {
            start = h.slice[slice_];
            if (slice_mode_ == 1 && slice_ + 1 < h.slice_count) stop_frame_ = h.slice[slice_ + 1];
        }
        start += static_cast<uint32_t>(static_cast<int64_t>(start_) * h.sample_rate / 16000);
        if (start >= frames_) return true;
        dir_ = reverse_ ? -1 : 1;
        pos_ = (reverse_ ? static_cast<int64_t>(frames_ > 0 ? frames_ - 1 : 0) : static_cast<int64_t>(start)) << 32;

        int32_t root = zone && zone->root >= 0 ? zone->root * kSemi : (root_ >= 0 ? root_ : h.root_note * kSemi);
        root_pitch_ = root;
        tune_total_ = tune_ + (h.tune_cents + (zone ? zone->tune_cents : 0)) * kSemi / 100;
        gain_eff_ = zone ? mul15(gain_, zone->gain) : gain_;
        base_q28_ = static_cast<uint32_t>((static_cast<uint64_t>(h.sample_rate) << 28) / kSampleRate);
        sl_ = sl;
        cache_lo_ = cache_hi_ = 0;
        cache_ptr_ = nullptr;
        held_ = 0;
        xf_ = 32767;
        underrun_ = false;
        active_ = true;
        stream_.start(slot, frames_, static_cast<uint32_t>(pos_ >> 32), dir_, static_cast<int32_t>(rate_for(0, 0) >> 16) , loop_);
        gen_ = stream_.generation();
        return true;
    }

    // One source frame; out-of-range frames read as silence. false = the data has not arrived (underrun).
    bool fetch(int64_t f, q15 &out) {
        if (f < 0 || f >= static_cast<int64_t>(frames_)) { out = 0; return true; }
        if (f >= cache_lo_ && f < cache_hi_) { out = cache_ptr_[f - cache_lo_]; return true; }
        for (int h = 0; h < sl_->n_heads; h++) {
            const Head &hd = sl_->head[h];
            if (f >= hd.start_frame && f < static_cast<int64_t>(hd.start_frame) + hd.frames) {
                cache_ptr_ = hd.data; cache_lo_ = hd.start_frame; cache_hi_ = static_cast<int64_t>(hd.start_frame) + hd.frames;
                out = cache_ptr_[f - cache_lo_];
                return true;
            }
        }
        const uint32_t blk = static_cast<uint32_t>(f / kSmpBlockFrames);
        const q15 *b = stream_.block(gen_, blk);
        if (!b) return false;
        cache_ptr_ = b; cache_lo_ = static_cast<int64_t>(blk) * kSmpBlockFrames; cache_hi_ = cache_lo_ + kSmpBlockFrames;
        out = cache_ptr_[f - cache_lo_];
        return true;
    }

    SampleBank *bank_ = nullptr;
    Heap *ring_heap_ = nullptr;
    q15 *ring_ = nullptr;
    Stream stream_;
    const SampleSlot *sl_ = nullptr;
    uint32_t gen_ = 0, frames_ = 0, stop_frame_ = 0, base_q28_ = 1u << 28;
    LoopInfo loop_;
    int64_t pos_ = 0, cache_lo_ = 0, cache_hi_ = 0;
    const q15 *cache_ptr_ = nullptr;
    int dir_ = 1, retry_ = 0;
    int32_t held_ = 0, xf_ = 32767, root_pitch_ = 60 * kSemi, tune_total_ = 0;
    bool want_start_ = true, active_ = false, underrun_ = false;
    // parameters
    int32_t sample_ = -1, inst_ = -1, tune_ = 0, root_ = -1, start_ = 0, slice_ = -1, slice_mode_ = 0;
    int32_t loop_param_ = -1;
    q15 gain_ = kUnity, gain_eff_ = kUnity;
    bool reverse_ = false, hermite_ = true, track_ = true;
};

/* ------------------------------------------------------------------ Granular */

template <Scope S>
class Granular : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Granular", S, 1, 1, GRN_N, false, {"pitch"}, {"out"},
            {{"sample", -1, -1, 31}, {"position", 0, 0, kUnity}, {"speed", 256, -1024, 1024}, {"size", 80, 5, 500},
             {"density", 20, 1, 200}, {"pitch", 0, -48 * kSemi, 48 * kSemi}, {"jitter", 4000, 0, kUnity}, {"level", kUnity, 0, kUnity}}};
        return i;
    }
    bool init(Memory &m) override { bank_ = m.bank; return true; }
    void reset() override { for (auto &g : grain_) g.active = false; timer_ = 0; scan_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case GRN_SAMPLE: sample_ = v; break;
            case GRN_POSITION: position_ = v; break;
            case GRN_SPEED: speed_ = v; break;
            case GRN_SIZE: size_ = v; break;
            case GRN_DENSITY: density_ = v; break;
            case GRN_PITCH: pitch_ = v; break;
            case GRN_JITTER: jitter_ = v; break;
            case GRN_LEVEL: level_ = static_cast<q15>(v); break;
        }
        const double overlap = std::fmax(1.0, density_ * size_ / 1000.0);          // grains sounding at once on average
        norm_ = static_cast<q15>(32767.0 / std::sqrt(overlap));
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const int n = ctx.frames;
        const SampleSlot *sl = bank_ ? bank_->slot(sample_) : nullptr;
        if (!sl || sl->state != SLOT_READY || sl->n_heads == 0) { block_clear(p.out[0], n); return; }
        const Head &hd = sl->head[0];
        const int64_t region = static_cast<int64_t>(std::min<uint32_t>(hd.frames, sl->h.frames));
        if (region < 64) { block_clear(p.out[0], n); return; }
        const uint32_t grain_len = static_cast<uint32_t>(static_cast<int64_t>(size_) * kSampleRate / 1000);
        const int64_t interval_q16 = (static_cast<int64_t>(kSampleRate) << 16) / (density_ < 1 ? 1 : density_);   // mean samples between grains, Q16
        const int64_t src_per_out = (static_cast<int64_t>(sl->h.sample_rate) << 28) / kSampleRate;   // Q28 frames per output sample at original speed
        const int32_t pitch = pitch_ + (S == Scope::Voice ? kPitchCvCenter + scaled(p.in[0][0], kPitchCvSpan) - 60 * kSemi : 0);
        const int64_t inc = static_cast<int64_t>(exp2_scale(static_cast<uint32_t>(src_per_out), static_cast<int32_t>(static_cast<int64_t>(pitch) * 64 / 3))) << 4;   // Q32
        // the scan position moves at `speed` x real time through the sample, starting at `position`
        const int64_t scan_inc = (static_cast<int64_t>(speed_) * src_per_out) >> 4;               // Q32 frames per output sample (speed Q8, src Q28)

        for (int i = 0; i < n; i++) {
            timer_ -= 65536;
            if (timer_ <= 0) {
                spawn(region, grain_len, inc, scan_);
                // randomised onsets (0.5 .. 1.5 x the mean interval): a regular grain train is a comb filter that
                // puts strong peaks at multiples of the density and turns a tone into a buzz
                const int64_t u = noise_.next() + 32768;                                          // 0 .. 65535
                timer_ += (interval_q16 * (32768 + u)) >> 16;
            }
            scan_ += scan_inc;
            int32_t acc = 0;
            for (auto &g : grain_) {
                if (!g.active) continue;
                const uint32_t ph = static_cast<uint32_t>((static_cast<uint64_t>(g.age) << 31) / g.len);   // half a turn over the grain
                const int32_t sn = sine(ph);
                const int32_t w = (sn * sn) >> 15;                                                       // Hann = sin^2
                const int64_t f = g.pos >> 32;
                const int64_t f0 = ((f % region) + region) % region, f1 = (f0 + 1) % region;
                const q15 y = lerp15(hd.data[f0], hd.data[f1], static_cast<uint32_t>((g.pos >> 16) & 0xFFFF));
                acc += (y * w) >> 15;
                g.pos += g.inc;
                if (++g.age >= g.len) g.active = false;
            }
            p.out[0][i] = mul15(mul15(sat16(acc), norm_), level_);
        }
        if (scan_ < 0 || (scan_ >> 32) >= region) scan_ = ((scan_ % (region << 32)) + (region << 32)) % (region << 32);
    }

private:
    struct Grain { bool active = false; int64_t pos = 0, inc = 0; uint32_t age = 0, len = 1; };
    void spawn(int64_t region, uint32_t len, int64_t inc, int64_t scan) {
        int slot = -1;
        for (int k = 0; k < kGrains; k++) if (!grain_[k].active) { slot = k; break; }
        if (slot < 0) return;                                                                         // all busy: skip this grain
        const int64_t base = (static_cast<int64_t>(position_) * region >> 15) << 32;
        const int32_t rnd = static_cast<int32_t>(noise_.next());                                      // -32768..32767
        const int64_t jit = ((static_cast<int64_t>(rnd) * jitter_ >> 15) * region) << 16;               // +-jitter x half the region, Q32 frames
        Grain &g = grain_[slot];
        g.pos = base + scan + jit;
        g.inc = inc;
        g.age = 0;
        g.len = len < 16 ? 16 : len;
        g.active = true;
    }
    static constexpr int kGrains = 16;
    SampleBank *bank_ = nullptr;
    Grain grain_[kGrains];
    Noise noise_{0x6A5Fu};
    int64_t timer_ = 0, scan_ = 0;
    int32_t sample_ = -1, position_ = 0, speed_ = 256, size_ = 80, density_ = 20, pitch_ = 0, jitter_ = 4000;
    q15 level_ = kUnity, norm_ = 8000;
};

template <typename T>
ModuleType type_of() { static T probe; return {&probe.info(), &create_module<T>}; }

}  // namespace

void register_sampler_modules(Registry &r) {
    r.add(T_SAMPLER, type_of<Sampler>());
    r.add(T_GRANULAR_V, type_of<Granular<Scope::Voice>>());
    r.add(T_GRANULAR_G, type_of<Granular<Scope::Global>>());
}

}  // namespace sc
