#include "engine/modules/motion_seq.h"
#include <cstring>

namespace sc {

int64_t motion_lane_value_q16(const MotionBlob &b, int lane, int steps, uint32_t pos_q16) {
    if (steps < 1) steps = 1;
    if (steps > kMsSteps) steps = kMsSteps;
    const unsigned mask = b.active[lane] & ((1u << steps) - 1u);
    if (!mask) return -1;
    int k = static_cast<int>(pos_q16 >> 16);
    if (k >= steps) k = steps - 1;
    const int64_t frac = pos_q16 & 0xFFFF;
    int prev = k;
    for (int d = 0; d < steps; d++) {
        const int i = (k - d + steps) % steps;
        if (mask >> i & 1u) { prev = i; break; }
    }
    if (!b.linear[lane] || (mask & (mask - 1)) == 0) return static_cast<int64_t>(b.val[lane][prev]) << 16;   // STEP, or a single active step
    int next = prev;
    for (int d = 1; d <= steps; d++) {
        const int i = (k + d) % steps;
        if (mask >> i & 1u) { next = i; break; }
    }
    const int dist = (next - prev + steps) % steps;
    const int done = (k - prev + steps) % steps;
    const int64_t t = ((static_cast<int64_t>(done) << 16) + frac) / dist;                       // Q16 position between prev and next
    const int64_t a = static_cast<int64_t>(b.val[lane][prev]) << 16, c = static_cast<int64_t>(b.val[lane][next]) << 16;
    return a + (((c - a) * t) >> 16);
}

namespace {

class MotionSeq : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"MotionSeq", Scope::Global, 0, kMsLanes, MSP_N, false, {}, {"lane1", "lane2", "lane3", "lane4"},
            {{"run", 0, 0, 1}, {"bpm", 110, 40, 240}, {"steps", 16, 1, kMsSteps}, {"swing", 0, 0, 50}, {"restart", 0, 0, 0x7FFFFFFF}}};
        return i;
    }
    bool init(Memory &) override { schedule(); return true; }
    void reset() override { run_ = false; pending_restart_ = false; started_ = false; t0_ = 0; }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case MSP_RUN: run_ = v != 0; break;
            case MSP_BPM: bpm_ = v; schedule(); break;
            case MSP_STEPS: steps_ = v; schedule(); break;
            case MSP_SWING: swing_ = v; schedule(); break;
            case MSP_RESTART: if (v != restart_seen_) { restart_seen_ = v; pending_restart_ = true; } break;
        }
    }
    void set_blob(const void *data, size_t bytes) override { if (bytes == sizeof(MotionBlob)) std::memcpy(&blob_, data, sizeof blob_); }

    void process(const ProcessCtx &ctx, const Ports &p) override {
        if (!run_) {
            for (int l = 0; l < kMsLanes; l++) for (int i = 0; i < ctx.frames; i++) p.out[l][i] = 0;
            started_ = false;
            return;
        }
        if (pending_restart_ || !started_) { t0_ = ctx.time; pending_restart_ = false; started_ = true; }
        const uint64_t f0 = (ctx.time - t0_) * static_cast<uint64_t>(ctx.frames);
        const uint32_t pos0 = pos_q16(f0), pos1 = pos_q16(f0 + static_cast<uint64_t>(ctx.frames));
        for (int l = 0; l < kMsLanes; l++) {
            const int32_t a = out_of(l, pos0), c = out_of(l, pos1);
            if (a == c) { for (int i = 0; i < ctx.frames; i++) p.out[l][i] = static_cast<q15>(a); continue; }
            for (int i = 0; i < ctx.frames; i++) p.out[l][i] = static_cast<q15>(a + static_cast<int32_t>((static_cast<int64_t>(c - a) * i) / ctx.frames));
        }
    }

private:
    // Step start times in frames, built from the same integer-millisecond rules as the note sequencer (seq.c)
    void schedule() {
        const int steps = steps_ < 1 ? 1 : (steps_ > kMsSteps ? kMsSteps : steps_);
        const int step_ms = 15000 / (bpm_ < 1 ? 1 : bpm_);
        int64_t ms = 0;
        start_[0] = 0;
        for (int k = 0; k < steps; k++) {
            ms += static_cast<int64_t>(step_ms) * (100 + ((k & 1) ? -swing_ : swing_)) / 100;
            start_[k + 1] = static_cast<int32_t>(ms * kSampleRate / 1000);
        }
        n_steps_ = steps;
        if (start_[steps] < 1) start_[steps] = 1;
    }

    uint32_t pos_q16(uint64_t frame) const {
        const int32_t total = start_[n_steps_];
        const int32_t c = static_cast<int32_t>(frame % static_cast<uint64_t>(total));
        int k = 0;
        while (k + 1 < n_steps_ && start_[k + 1] <= c) k++;
        const int32_t len = start_[k + 1] - start_[k];
        const uint32_t frac = len > 0 ? static_cast<uint32_t>((static_cast<int64_t>(c - start_[k]) << 16) / len) : 0;
        return (static_cast<uint32_t>(k) << 16) | (frac > 0xFFFF ? 0xFFFF : frac);
    }

    int32_t out_of(int lane, uint32_t pos) const {
        const int64_t v = motion_lane_value_q16(blob_, lane, n_steps_, pos);
        if (v < 0) return 0;                                                   // no active step: the lane outputs nothing
        if (blob_.bipolar[lane]) return static_cast<int32_t>(((v - (50 << 16)) * 2 * 32767) / (100ll << 16));
        return static_cast<int32_t>((v * 32767) / (100ll << 16));
    }

    MotionBlob blob_{};
    int32_t start_[kMsSteps + 1] = {};
    int n_steps_ = 16;
    int32_t bpm_ = 110, steps_ = 16, swing_ = 0, restart_seen_ = 0;
    bool run_ = false, pending_restart_ = false, started_ = false;
    uint64_t t0_ = 0;
};

template <typename T>
ModuleType type_of() { static T probe; return {&probe.info(), &create_module<T>}; }

}  // namespace

void register_motion_module(Registry &r) { r.add(T_MSEQ, type_of<MotionSeq>()); }

}  // namespace sc
