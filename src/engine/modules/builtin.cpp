#include "engine/modules/builtin.h"
#include "engine/dsp/block.h"

namespace sc {
namespace {

class NoteIn : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"NoteIn", Scope::Voice, 0, 3, 0, false, {}, {"pitch", "gate", "vel"}, {}};
        return i;
    }
    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
        const VoiceState *v = ctx.voice;
        q15 pitch = v ? pitch_to_cv(v->pitch) : 0, gate = v && v->gate ? kUnity : 0, vel = v ? v->velocity : 0;
        for (int i = 0; i < ctx.frames; i++) { p.out[0][i] = pitch; p.out[1][i] = gate; p.out[2][i] = vel; }
    }
};

class VoiceOut : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"VoiceOut", Scope::Voice, 1, 0, 3, false, {"in"}, {},
                                     {{"level", kUnity, 0, kUnity}, {"pan", 0, -32768, 32767}, {"tail_ms", 4000, 0, 60000}}};
        return i;
    }
    void reset() override { silent_ = 0; }
    void set_param(int idx, int32_t v) override {
        if (idx == VO_LEVEL) level_ = static_cast<q15>(v);
        else if (idx == VO_PAN) pan_ = static_cast<q15>(v);
        else if (idx == VO_TAIL_MS) tail_blocks_ = static_cast<uint32_t>(v) * kControlRate / 1000u;
    }
    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
        // pan: linear, the louder side stays at `level` (centre = both at level)
        int32_t pan = pan_;
        q15 gl = pan > 0 ? mul15(level_, static_cast<q15>(32767 - pan)) : level_;
        q15 gr = pan < 0 ? mul15(level_, static_cast<q15>(32767 + pan)) : level_;
        block_mac(ctx.bus_l, p.in[0], gl, ctx.frames);
        block_mac(ctx.bus_r, p.in[0], gr, ctx.frames);

        VoiceState *v = ctx.voice;
        if (!v || v->gate) { silent_ = 0; return; }
        silent_ = block_peak(p.in[0], ctx.frames) <= 4 ? silent_ + 1 : 0;
        if (silent_ >= 8 || v->release_age >= tail_blocks_) v->done = true;
    }
private:
    q15 level_ = kUnity, pan_ = 0;
    uint32_t silent_ = 0, tail_blocks_ = 4000u * kControlRate / 1000u;
};

class BusIn : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"BusIn", Scope::Global, 0, 2, 0, true, {}, {"L", "R"}, {}};
        return i;
    }
    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
        block_copy(p.out[0], ctx.bus_l, ctx.frames);
        block_copy(p.out[1], ctx.bus_r, ctx.frames);
    }
};

class MasterOut : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"MasterOut", Scope::Global, 2, 0, 1, true, {"L", "R"}, {}, {{"level", kUnity, 0, kUnity}}};
        return i;
    }
    void set_param(int idx, int32_t v) override { if (idx == 0) level_ = static_cast<q15>(v); }
    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
        block_gain(ctx.out_l, p.in[0], level_, ctx.frames);
        block_gain(ctx.out_r, p.in[1], level_, ctx.frames);
    }
private:
    q15 level_ = kUnity;
};

template <typename T>
ModuleType type_of() { static T probe; return {&probe.info(), &create_module<T>}; }

}  // namespace

void register_builtin_modules(Registry &reg) {
    reg.add(T_NOTE_IN, type_of<NoteIn>());
    reg.add(T_VOICE_OUT, type_of<VoiceOut>());
    reg.add(T_BUS_IN, type_of<BusIn>());
    reg.add(T_MASTER_OUT, type_of<MasterOut>());
}

}  // namespace sc
