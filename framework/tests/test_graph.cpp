#include <vector>
#include "test.h"
#include "engine/core/engine.h"
#include "engine/dsp/phase.h"
#include "engine/modules/builtin.h"

using namespace sc;

namespace {

/* ---- test modules ---- */
template <Scope S> class TConst : public Module {          // out = param0
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Const", S, 0, 1, 1, false, {}, {"out"}, {{"value", 0, -32768, 32767}}};
        return i;
    }
    void set_param(int, int32_t v) override { v_ = static_cast<q15>(v); }
    void process(const ProcessCtx &ctx, const Ports &p) override { for (int i = 0; i < ctx.frames; i++) p.out[0][i] = v_; }
private:
    q15 v_ = 0;
};

template <Scope S> class TScale : public Module {          // out = in * (param0 + mod0)
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Scale", S, 1, 1, 1, false, {"in"}, {"out"}, {{"gain", kUnity, -32768, 32767}}};
        return i;
    }
    void set_param(int, int32_t v) override { g_ = static_cast<q15>(v); }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        for (int i = 0; i < ctx.frames; i++) {
            q15 g = p.mod[0] ? add15(g_, p.mod[0][i]) : g_;
            p.out[0][i] = mul15(p.in[0][i], g);
        }
    }
private:
    q15 g_ = kUnity;
};

template <Scope S> class TSum : public Module {            // out = in0 + in1
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Sum", S, 2, 1, 0, false, {"a", "b"}, {"out"}, {}};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override { for (int i = 0; i < ctx.frames; i++) p.out[0][i] = add15(p.in[0][i], p.in[1][i]); }
};

class TOsc : public Module {                                // sine, pitch from CV input 0
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Osc", Scope::Voice, 1, 1, 0, false, {"pitch"}, {"out"}, {}};
        return i;
    }
    void reset() override { ph_ = 0; }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        int32_t pitch = kPitchCvCenter + static_cast<int32_t>(static_cast<int64_t>(p.in[0][0]) * kPitchCvSpan / 32768);
        uint32_t inc = pitch_to_inc(pitch, inc_a4());
        for (int i = 0; i < ctx.frames; i++, ph_ += inc) p.out[0][i] = sine(ph_);
    }
private:
    uint32_t ph_ = 0;
};

class TCounter : public Module {                            // out = block counter (state that must survive rebuilds)
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Counter", Scope::Global, 0, 1, 0, false, {}, {"out"}, {}};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override { q15 v = static_cast<q15>(n_++); for (int i = 0; i < ctx.frames; i++) p.out[0][i] = v; }
private:
    int n_ = 0;
};

template <typename T> ModuleType mt() { static T probe; return {&probe.info(), &create_module<T>}; }

enum { G_CONST = 16, G_SCALE, G_SUM, V_CONST, V_SCALE, V_SUM, V_OSC, G_COUNTER };

struct Rig {
    std::vector<uint8_t> mem;
    Heap heap;
    Engine eng;
    explicit Rig(int voices = 4, size_t bytes = 1 << 20) : mem(bytes) {
        heap.init(mem.data(), mem.size());
        eng.init(heap, voices);
        Registry &r = eng.registry();
        r.add(G_CONST, mt<TConst<Scope::Global>>());
        r.add(G_SCALE, mt<TScale<Scope::Global>>());
        r.add(G_SUM, mt<TSum<Scope::Global>>());
        r.add(V_CONST, mt<TConst<Scope::Voice>>());
        r.add(V_SCALE, mt<TScale<Scope::Voice>>());
        r.add(V_SUM, mt<TSum<Scope::Voice>>());
        r.add(V_OSC, mt<TOsc>());
        r.add(G_COUNTER, mt<TCounter>());
    }
    NodeDesc *add(GraphDesc &g, int id, int type) { return g.add_node(eng.registry(), id, type); }
    // renders n blocks, appends to l / r
    void run(int n, std::vector<q15> *l, std::vector<q15> *r = nullptr) {
        q15 bl[kBlock], br[kBlock];
        for (int b = 0; b < n; b++) {
            eng.render(bl, br);
            if (l) l->insert(l->end(), bl, bl + kBlock);
            if (r) r->insert(r->end(), br, br + kBlock);
        }
    }
    q15 last(int blocks = 1) { std::vector<q15> l; run(blocks, &l); return l.back(); }
};

int block_peak_vec(const std::vector<q15> &v) {
    int p = 0;
    for (q15 x : v) { int a = x < 0 ? -static_cast<int>(x) : x; if (a > p) p = a; }
    return p;
}

void to_master(GraphDesc &g, int src_id, int src_port, int master_id) {
    g.connect(src_id, src_port, master_id, Dst::In, 0);
    g.connect(src_id, src_port, master_id, Dst::In, 1);
}

}  // namespace

TEST(heap_alloc_free_coalesce) {
    std::vector<uint8_t> mem(4096);
    Heap h;
    h.init(mem.data(), mem.size());
    void *a = h.alloc(100), *b = h.alloc(200), *c = h.alloc(300);
    CHECK(a && b && c && h.check());
    CHECK_EQ(((uintptr_t)a) % 16, 0);
    h.free(b);
    CHECK(h.check());
    h.free(a);
    h.free(c);
    CHECK(h.check());
    CHECK_EQ(h.used(), 0);
    CHECK(h.largest_free() > 3900);                    // fully coalesced
    CHECK(h.alloc(100000) == nullptr);
    h.free(nullptr);
    h.free(a);                                         // double free ignored
    CHECK(h.check());
}

TEST(compile_errors) {
    Rig rig;
    Registry &reg = rig.eng.registry();
    Plan *pl = nullptr;
    GraphDesc g;
    CHECK(g.add_node(reg, 1, 99) == nullptr);                          // unknown type refused at add
    g.node[0].id = 1; g.node[0].type = 99; g.n_nodes = 1;
    CHECK(compile_plan(g, reg, rig.heap, 4, &pl) == Err::UnknownType);

    GraphDesc d;
    rig.add(d, 1, G_CONST); rig.add(d, 1, G_CONST);
    CHECK(compile_plan(d, reg, rig.heap, 4, &pl) == Err::DupId);

    GraphDesc b;
    rig.add(b, 1, G_CONST); rig.add(b, 2, G_SCALE);
    b.connect(1, 5, 2, Dst::In, 0);                                    // no such output
    CHECK(compile_plan(b, reg, rig.heap, 4, &pl) == Err::BadEdge);

    GraphDesc s;
    rig.add(s, 1, V_CONST); rig.add(s, 2, G_SCALE);
    s.connect(1, 0, 2, Dst::In, 0);
    CHECK(compile_plan(s, reg, rig.heap, 4, &pl) == Err::Scope);

    GraphDesc c;                                                       // loop without delay
    rig.add(c, 1, G_SCALE); rig.add(c, 2, G_SCALE);
    c.connect(1, 0, 2, Dst::In, 0); c.connect(2, 0, 1, Dst::In, 0);
    CHECK(compile_plan(c, reg, rig.heap, 4, &pl) == Err::Cycle);
    c.edge[1].delayed = true;                                          // same loop with a delay compiles
    CHECK(compile_plan(c, reg, rig.heap, 4, &pl) == Err::Ok);
    free_plan(pl);

    GraphDesc f;
    rig.add(f, 1, G_SCALE);
    for (int i = 0; i < kMaxFanIn + 1; i++) { rig.add(f, 10 + i, G_CONST); f.connect(10 + i, 0, 1, Dst::In, 0, 1000); }
    CHECK(compile_plan(f, reg, rig.heap, 4, &pl) == Err::FanIn);

    GraphDesc bl;                                                      // voice input fed by the voice sum
    rig.add(bl, 1, T_BUS_IN); rig.add(bl, 2, V_SCALE);
    bl.connect(1, 0, 2, Dst::In, 0);
    CHECK(compile_plan(bl, reg, rig.heap, 4, &pl) == Err::BusLoop);
    CHECK(rig.heap.check());
    CHECK_EQ(rig.heap.used(), 0);                                      // failed compiles leak nothing
    rig.eng.shutdown();
}

TEST(global_chain_values) {
    Rig rig;
    GraphDesc g;
    rig.add(g, 1, G_CONST)->param[0] = 16384;
    rig.add(g, 2, G_SCALE)->param[0] = 16384;
    rig.add(g, 3, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    to_master(g, 2, 0, 3);
    CHECK(rig.eng.load(g) == Err::Ok);
    std::vector<q15> l, r;
    rig.run(2, &l, &r);
    CHECK_NEAR(l.back(), 8192, 2);
    CHECK_NEAR(r.back(), 8192, 2);
    rig.eng.shutdown();
    CHECK_EQ(rig.heap.used(), 0);
}

TEST(cables_with_depth_are_mixed_and_unity_is_aliased) {
    Rig rig;
    Registry &reg = rig.eng.registry();
    GraphDesc g;
    rig.add(g, 1, G_CONST)->param[0] = 16384;                          // 0.5
    rig.add(g, 2, G_CONST)->param[0] = 8192;                           // 0.25
    rig.add(g, 3, G_SCALE);                                            // unity gain
    rig.add(g, 4, T_MASTER_OUT);
    g.connect(1, 0, 3, Dst::In, 0, 16384);                             // +0.5 * 0.5
    g.connect(2, 0, 3, Dst::In, 0, -16384);                            // -0.5 * 0.25
    to_master(g, 3, 0, 4);
    CHECK(rig.eng.load(g) == Err::Ok);
    CHECK_NEAR(rig.last(2), 4096, 3);                                  // 0.125

    // a single unity cable needs no MIX step: 2 sources + 1 scale + master = 4 steps
    GraphDesc a;
    rig.add(a, 1, G_CONST); rig.add(a, 2, G_SCALE); rig.add(a, 3, T_MASTER_OUT);
    a.connect(1, 0, 2, Dst::In, 0);
    to_master(a, 2, 0, 3);
    Plan *pl = nullptr;
    CHECK(compile_plan(a, reg, rig.heap, 4, &pl) == Err::Ok);
    CHECK_EQ(pl->n_pre + pl->n_voice + pl->n_post, 3);
    free_plan(pl);
    rig.eng.shutdown();
}

TEST(parameter_modulation_cable) {
    Rig rig;
    GraphDesc g;
    rig.add(g, 1, G_CONST)->param[0] = 16384;                          // signal 0.5
    rig.add(g, 2, G_CONST)->param[0] = 8192;                           // modulator 0.25
    rig.add(g, 3, G_SCALE)->param[0] = 16384;                          // base gain 0.5
    rig.add(g, 4, T_MASTER_OUT);
    g.connect(1, 0, 3, Dst::In, 0);
    g.connect(2, 0, 3, Dst::Param, 0, 16384);                          // + 0.25 * 0.5 -> gain 0.625
    to_master(g, 3, 0, 4);
    CHECK(rig.eng.load(g) == Err::Ok);
    CHECK_NEAR(rig.last(2), 10240, 3);                                 // 0.5 * 0.625
    rig.eng.shutdown();
}

TEST(buffer_pool_reuses_by_liveness) {
    Rig rig;
    GraphDesc g;
    rig.add(g, 1, G_CONST)->param[0] = 1000;
    for (int i = 0; i < 10; i++) rig.add(g, 2 + i, G_SCALE);
    g.connect(1, 0, 2, Dst::In, 0);
    for (int i = 0; i < 9; i++) g.connect(2 + i, 0, 3 + i, Dst::In, 0);
    Plan *pl = nullptr;
    CHECK(compile_plan(g, rig.eng.registry(), rig.heap, 4, &pl) == Err::Ok);
    CHECK(pl->n_slots <= 3);                                           // 11 outputs, chain: 2-3 live at once
    free_plan(pl);
    rig.eng.shutdown();
}

TEST(delayed_cable_closes_a_feedback_loop) {
    Rig rig;
    GraphDesc g;
    rig.add(g, 1, G_CONST)->param[0] = 8192;                           // c = 0.25
    rig.add(g, 2, G_SUM);
    rig.add(g, 3, G_SCALE)->param[0] = 16384;                          // g = 0.5
    rig.add(g, 4, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(3, 0, 2, Dst::In, 1, kUnity, true);                      // y = c + g * y(previous block)
    g.connect(2, 0, 3, Dst::In, 0);
    to_master(g, 2, 0, 4);
    CHECK(rig.eng.load(g) == Err::Ok);
    double y = 0;
    for (int k = 0; k < 8; k++) {
        y = 0.25 + 0.5 * y;
        CHECK_NEAR(rig.last(1), y * 32768.0, 4);
    }
    rig.eng.shutdown();
}

TEST(voice_plays_the_right_pitch_and_frees_itself) {
    Rig rig;
    GraphDesc g;
    rig.add(g, 1, T_NOTE_IN);
    rig.add(g, 2, V_OSC);
    rig.add(g, 3, T_VOICE_OUT)->param[VO_TAIL_MS] = 100;
    rig.add(g, 4, T_BUS_IN);
    rig.add(g, 5, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
    g.connect(4, 0, 5, Dst::In, 0);
    g.connect(4, 1, 5, Dst::In, 1);
    CHECK(rig.eng.load(g) == Err::Ok);

    rig.eng.note_on(69);
    std::vector<q15> l;
    rig.run(kSampleRate / kBlock, &l);                                 // one second
    int crossings = 0;
    for (size_t i = 1; i < l.size(); i++) if (l[i - 1] < 0 && l[i] >= 0) crossings++;
    CHECK_NEAR(crossings, 440, 2);
    CHECK_EQ(rig.eng.active_voices(), 1);

    rig.eng.note_on(76);
    rig.run(2, nullptr);
    CHECK_EQ(rig.eng.active_voices(), 2);

    rig.eng.note_off(69);
    rig.eng.note_off(76);
    rig.run(kSampleRate / kBlock / 4, nullptr);                        // 250 ms > 100 ms tail
    CHECK_EQ(rig.eng.active_voices(), 0);
    std::vector<q15> tail;
    rig.run(4, &tail);
    CHECK_EQ(block_peak_vec(tail), 0);
    rig.eng.shutdown();
    CHECK_EQ(rig.heap.used(), 0);
}

TEST(voice_stealing_keeps_polyphony_bounded) {
    Rig rig(3);
    GraphDesc g;
    rig.add(g, 1, T_NOTE_IN); rig.add(g, 2, V_OSC); rig.add(g, 3, T_VOICE_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    for (int n = 60; n < 66; n++) { rig.eng.note_on(n); rig.run(2, nullptr); }
    CHECK_EQ(rig.eng.active_voices(), 3);
    CHECK(rig.eng.voice(0).note >= 60);
    rig.eng.shutdown();
}

TEST(per_voice_feedback_state_is_not_shared) {
    Rig rig(4);
    GraphDesc g;
    rig.add(g, 1, T_NOTE_IN);
    rig.add(g, 2, V_SUM);
    rig.add(g, 3, V_SCALE)->param[0] = 16384;
    rig.add(g, 4, T_VOICE_OUT);
    rig.add(g, 5, T_BUS_IN);
    rig.add(g, 6, T_MASTER_OUT);
    g.connect(1, 2, 2, Dst::In, 0);                                    // velocity
    g.connect(3, 0, 2, Dst::In, 1, kUnity, true);                      // delayed feedback, per voice
    g.connect(2, 0, 3, Dst::In, 0);
    g.connect(2, 0, 4, Dst::In, 0);
    g.connect(5, 0, 6, Dst::In, 0);
    g.connect(5, 1, 6, Dst::In, 1);
    CHECK(rig.eng.load(g) == Err::Ok);

    // Voice A reaches its steady state y = vel + 0.5 y = 2 vel. A fresh voice B must start from zero
    // feedback: its first block is just its own velocity. (If the two shared one feedback buffer, B would
    // see A's state and the total would be off by A's velocity.)
    double va = 20 * 32767 / 127, vb = 10 * 32767 / 127;
    rig.eng.note_on(60, 20);
    CHECK_NEAR(rig.last(60), 2 * va, 30);
    rig.eng.note_on(62, 10);
    CHECK_NEAR(rig.last(1), 2 * va + vb, 30);
    CHECK_NEAR(rig.last(60), 2 * (va + vb), 60);                       // and both settle independently
    rig.eng.shutdown();
    CHECK_EQ(rig.heap.used(), 0);
}

TEST(rebuild_keeps_state_of_unchanged_modules_and_does_not_leak) {
    Rig rig;
    auto make = [&](int counter_id, int level) {
        GraphDesc g;
        rig.add(g, counter_id, G_COUNTER);
        rig.add(g, 2, T_MASTER_OUT)->param[0] = level;
        g.connect(counter_id, 0, 2, Dst::In, 0);
        return g;
    };
    CHECK(rig.eng.load(make(1, kUnity)) == Err::Ok);
    CHECK_EQ(rig.last(1), 0);
    CHECK_EQ(rig.last(1), 1);
    CHECK_EQ(rig.last(1), 2);
    CHECK(rig.eng.load(make(1, 30000)) == Err::Ok);                    // same id: instance survives
    CHECK_EQ(rig.last(1), 3);
    rig.eng.gc();
    size_t baseline = rig.heap.used();

    for (int i = 0; i < 50; i++) {                                     // churn: alternate ids, many loads per render
        CHECK(rig.eng.load(make(5, kUnity)) == Err::Ok);
        CHECK(rig.eng.load(make(1, kUnity)) == Err::Ok);
        CHECK(rig.eng.load(make(5, kUnity)) == Err::Ok);
        rig.run(1, nullptr);
        rig.eng.gc();
    }
    CHECK(rig.heap.check());
    rig.eng.gc();
    CHECK_EQ(rig.heap.used(), baseline);                               // nothing leaked
    CHECK_EQ(rig.last(1), 50);                                         // counter id 5 was created in the churn and ran 50 blocks (0..49)
    rig.eng.shutdown();
    CHECK_EQ(rig.heap.used(), 0);
}
