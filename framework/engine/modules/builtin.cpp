#include "engine/modules/builtin.h"
#include "engine/sampler/capture.h"
#include "engine/dsp/block.h"

namespace sc {
namespace {

class NoteIn : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"NoteIn", Scope::Voice, 0, 3, 0, false, {}, {"pitch", "gate", "vel"}, {}, Rate::Control};
        return i;
    }
    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
        const VoiceState *v = ctx.voice;
        q15 pitch = v ? pitch_to_cv(v->pitch) : 0, gate = v && v->gate ? kUnity : 0, vel = v ? v->velocity : 0;
        q15 *o0 = p.out[0], *o1 = p.out[1], *o2 = p.out[2];
        const int n = ctx.frames;
        if (pairs_ok(o0, n) && pairs_ok(o1, n) && pairs_ok(o2, n)) {          // (the plan's blocks: two samples a store, one loop for the three)
            const uint32_t w0 = q15_pair(pitch), w1 = q15_pair(gate), w2 = q15_pair(vel);
            q15_pair_t *d0 = reinterpret_cast<q15_pair_t *>(o0), *d1 = reinterpret_cast<q15_pair_t *>(o1), *d2 = reinterpret_cast<q15_pair_t *>(o2);
            for (int k = 0; k < n / 2; k++) { d0[k] = w0; d1[k] = w1; d2[k] = w2; }
            return;
        }
        for (int i = 0; i < n; i++) { o0[i] = pitch; o1[i] = gate; o2[i] = vel; }
    }
};

class VoiceOut : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"VoiceOut", Scope::Voice, 4, 0, 6, false, {"in", "in2", "in3", "in4"}, {},
                                     {{"level", kUnity, 0, kUnity}, {"pan", 0, -32768, 32767}, {"tail_ms", 4000, 0, 60000}, {"in2", 0, 0, 1},
                                      {"in3", 0, 0, 1}, {"in4", 0, 0, 1}}};
        return i;
    }
    void reset() override { silent_ = 0; }
    void set_param(int idx, int32_t v) override {
        if (idx == VO_LEVEL) level_ = static_cast<q15>(v);
        else if (idx == VO_PAN) pan_ = static_cast<q15>(v);
        else if (idx == VO_TAIL_MS) tail_blocks_ = static_cast<uint32_t>(v) * kControlRate / 1000u;
        else if (idx == VO_IN2) in2_ = v != 0;
        else if (idx == VO_IN3) send_[0] = v != 0;
        else if (idx == VO_IN4) send_[1] = v != 0;
    }
    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
        // pan: linear, the louder side stays at `level` (centre = both at level)
        int32_t pan = pan_;
        q15 gl = pan > 0 ? mul15(level_, static_cast<q15>(32767 - pan)) : level_;
        q15 gr = pan < 0 ? mul15(level_, static_cast<q15>(32767 + pan)) : level_;
        // The bus is 32 bits: the voices add without clipping, BusIn saturates the sum once (a q15 bus clipped after every voice).
        add(ctx.frames, p.in[0], gl, gr, ctx.bus_l, ctx.bus_r, ctx.bus_mono, 0);
        if (in2_ && ctx.bus2_l) add(ctx.frames, p.in[1], gl, gr, ctx.bus2_l, ctx.bus2_r, ctx.bus_mono, 1);
        for (int b = 0; b < kSendBuses; b++)
            if (send_[b] && ctx.send_l[b]) add(ctx.frames, p.in[2 + b], gl, gr, ctx.send_l[b], ctx.send_r[b], ctx.bus_mono, 2 + b);

        VoiceState *v = ctx.voice;
        if (!v || v->gate) { silent_ = 0; return; }
        bool quiet = block_peak(p.in[0], ctx.frames) <= 4 && (!in2_ || block_peak(p.in[1], ctx.frames) <= 4);
        for (int b = 0; b < kSendBuses; b++) quiet = quiet && (!send_[b] || block_peak(p.in[2 + b], ctx.frames) <= 4);
        silent_ = quiet ? silent_ + 1 : 0;
        if (silent_ >= 8 || v->release_age >= tail_blocks_) v->done = true;
    }
private:
    // mono, bit: the engine's ProcessCtx::bus_mono and this bus's bit in it (a centred voice into a bus whose right half is its left one
    // adds to the left one only; a panned one first makes the right one real)
    SC_INLINE static void add(int n, const q15 *x, q15 gl, q15 gr, int32_t *bl, int32_t *br, uint8_t *mono, int bit) {
        if (mono && ((*mono >> bit) & 1u)) {
            if (gl == gr) {
                for (int i = 0; i < n; i++) bl[i] += mul15(x[i], gl);
                return;
            }
            for (int i = 0; i < n; i++) br[i] = bl[i];
            *mono = static_cast<uint8_t>(*mono & ~(1u << bit));
        }
        if (gl == gr) {                                              // centred (the usual case): one product for both sides
            for (int i = 0; i < n; i++) { const int32_t m = mul15(x[i], gl); bl[i] += m; br[i] += m; }
        } else {
            for (int i = 0; i < n; i++) { bl[i] += mul15(x[i], gl); br[i] += mul15(x[i], gr); }
        }
    }
    q15 level_ = kUnity, pan_ = 0;
    bool in2_ = false, send_[kSendBuses] = {};
    uint32_t silent_ = 0, tail_blocks_ = 4000u * kControlRate / 1000u;
};

class BusIn : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"BusIn", Scope::Global, 0, 2, 1, true, {}, {"L", "R"}, {{"bus", 0, 0, 1 + kSendBuses}}};
        return i;
    }
    void set_param(int idx, int32_t v) override { if (idx == BUS_SEL) bus_ = static_cast<int>(v); }   // 0, 1: the voice buses; 2, 3: the sends
    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
        const int32_t *bl = ctx.bus_l, *br = ctx.bus_r;
        int bit = 0;
        if (bus_ == 1 && ctx.bus2_l) { bl = ctx.bus2_l; br = ctx.bus2_r; bit = 1; }
        else if (bus_ >= 2 && bus_ < 2 + kSendBuses && ctx.send_l[bus_ - 2]) { bl = ctx.send_l[bus_ - 2]; br = ctx.send_r[bus_ - 2]; bit = bus_; }
        if (ctx.bus_mono && ((*ctx.bus_mono >> bit) & 1u)) {               // (every voice centred: the right half is the left one)
            for (int i = 0; i < ctx.frames; i++) { const q15 v = sat16(bl[i]); p.out[0][i] = v; p.out[1][i] = v; }
            return;
        }
        for (int i = 0; i < ctx.frames; i++) { p.out[0][i] = sat16(bl[i]); p.out[1][i] = sat16(br[i]); }
    }
private:
    int bus_ = 0;
};

class MasterOut : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"MasterOut", Scope::Global, 2, 0, 3, true, {"L", "R"}, {},
                                     {{"level", kUnity, 0, kUnity}, {"boost", 0, 0, 1}, {"mono", 0, 0, 1}}};
        return i;
    }
    // level: q15 gain; boost = 1 doubles it (saturating), so the master volume reaches 2.0; mono = 1 sends (L + R) / 2 to both outputs.
    // A new gain is reached by a linear ramp over one block: the volume knob moves in 5 % steps, applied at once they were audible as zipper noise.
    void set_param(int idx, int32_t v) override {
        if (idx == 0) level_ = static_cast<q15>(v);
        else if (idx == 1) boost_ = v != 0;
        else if (idx == 2) mono_ = v != 0;
        target_ = boost_ ? 2 * static_cast<int32_t>(level_) : level_;     // q15 gain, up to 2.0
        if (first_) { gain_ = target_; }
    }
    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
        const q15 *l = p.in[0], *r = p.in[1];
        first_ = false;
        const int n = ctx.frames;
        // a recording takes what comes in, before the volume (feed is in flash: called only while the tap is armed or recording)
        const CaptureTap::State ts = tap_.state();
        if (ts == CaptureTap::Armed || ts == CaptureTap::Recording) tap_.feed(l, r, n, ctx.transport, ctx.seq);
        if (gain_ == target_) {                                               // the volume still (the usual case): one gain
            if (mono_) steady<true>(l, r, ctx.out_l, ctx.out_r, n, gain_);
            else steady<false>(l, r, ctx.out_l, ctx.out_r, n, gain_);
            return;
        }
        if (mono_) ramp<true>(l, r, ctx.out_l, ctx.out_r, n);
        else ramp<false>(l, r, ctx.out_l, ctx.out_r, n);
        gain_ = target_;
    }
private:
    template <bool M>
    static SC_HOT void steady(const q15 *l, const q15 *r, q15 *ol, q15 *orr, int n, int32_t g) {
        for (int i = 0; i < n; i++) {
            q15 a = l[i], b = r[i];
            if (M) a = b = static_cast<q15>((static_cast<int32_t>(a) + b) >> 1);
            ol[i] = sat16((static_cast<int32_t>(a) * g + (1 << 14)) >> 15);
            orr[i] = sat16((static_cast<int32_t>(b) * g + (1 << 14)) >> 15);
        }
    }
    template <bool M>
    SC_HOT void ramp(const q15 *l, const q15 *r, q15 *ol, q15 *orr, int n) const {
        const int32_t step = (target_ - gain_) / n;                           // the last sample lands exactly on the target
        int32_t g = gain_;
        for (int i = 0; i < n - 1; i++) {
            g += step;
            q15 a = l[i], b = r[i];
            if (M) a = b = static_cast<q15>((static_cast<int32_t>(a) + b) >> 1);
            ol[i] = sat16((static_cast<int32_t>(a) * g + (1 << 14)) >> 15);
            orr[i] = sat16((static_cast<int32_t>(b) * g + (1 << 14)) >> 15);
        }
        q15 a = l[n - 1], b = r[n - 1];
        if (M) a = b = static_cast<q15>((static_cast<int32_t>(a) + b) >> 1);
        ol[n - 1] = sat16((static_cast<int32_t>(a) * target_ + (1 << 14)) >> 15);
        orr[n - 1] = sat16((static_cast<int32_t>(b) * target_ + (1 << 14)) >> 15);
    }
    CaptureTap &tap_ = capture_tap();
    q15 level_ = kUnity;
    bool boost_ = false, mono_ = false, first_ = true;
    int32_t target_ = kUnity, gain_ = kUnity;                                 // q15 gain (0..65534): the set one, and where the ramp is
};

template <typename T>
ModuleType type_of() { return module_type_of<T>(); }

}  // namespace

void register_builtin_modules(Registry &reg) {
    reg.add(T_NOTE_IN, type_of<NoteIn>());
    reg.add(T_VOICE_OUT, type_of<VoiceOut>());
    reg.add(T_BUS_IN, type_of<BusIn>());
    reg.add(T_MASTER_OUT, type_of<MasterOut>());
}

}  // namespace sc
