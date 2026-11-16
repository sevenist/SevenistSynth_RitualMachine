// ADR-050 step 2: the two signal types in one graph. A module declares Sig::Q15 (every module written before) or Sig::F32; a buffer has
// the type of the module that writes it, and the plan converts a cable only where it joins the two types (one copy for its readers in a
// section). Checked here with small test modules: a float module between q15 ones is transparent bit for bit (q15 -> float is exact,
// float -> q15 rounds back), a graph of one type has no conversion, the float mix applies the cables' depths (live changes too), the
// delayed cables of both types keep each voice's own block, and a global block read by the voices stays valid for every voice.
#include <limits>
#include <map>
#include "rig.h"
#include "engine/modules/builtin.h"
#include "engine/modules/synth_modules.h"
#include "engine/dsp/block.h"

using namespace sc;
using namespace tst;

namespace {

constexpr int T_QTHRU = 118, T_FTHRU = 119, T_FREC = 120, T_FCHK = 121, T_QCHK = 122, T_GCONST = 123, T_VGEN = 124, T_VREAD = 125;

// in -> out, q15 or float; the float one also adds its value's modulation when cabled (out = in + mod)
class QThru : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"QThru", Scope::Voice, 1, 1, 0, false, {"in"}, {"out"}, {}};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override { for (int i = 0; i < ctx.frames; i++) p.out[0][i] = p.in[0][i]; }
};
class FThru : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"FThru", Scope::Voice, 1, 1, 1, false, {"in"}, {"out"}, {{"add", 0, -32768, 32767}}, Rate::Follow, Sig::F32};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        for (int i = 0; i < ctx.frames; i++) p.fout[0][i] = p.fin[0][i] + (p.fmod[0] ? p.fmod[0][i] : 0.0f);
    }
};

// The delayed cables: FRec passes its input through and keeps, per note, the block it wrote before this one; FChk (float) and QChk
// (q15) read FRec's output through a delayed cable and count the samples that are not that previous block (for this voice's note).
std::map<int, std::vector<float>> g_prev, g_cur;
int g_fb_checked = 0, g_fb_wrong = 0;
class FRec : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"FRec", Scope::Voice, 1, 1, 0, false, {"in"}, {"out"}, {}, Rate::Audio, Sig::F32};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        const int note = ctx.voice->note;
        std::vector<float> &cur = g_cur[note];
        g_prev[note] = cur;
        cur.assign(p.fin[0], p.fin[0] + ctx.frames);
        for (int i = 0; i < ctx.frames; i++) p.fout[0][i] = p.fin[0][i];
    }
};
void check_prev(const ProcessCtx &ctx, const float *f, const q15 *q) {
    const std::vector<float> &prev = g_prev[ctx.voice->note];
    if (prev.empty()) return;                                               // (the voice's first block: no previous one)
    for (int i = 0; i < ctx.frames; i++) {
        g_fb_checked++;
        if (f ? f[i] != prev[static_cast<size_t>(i)] : q[i] != f_to_q15(prev[static_cast<size_t>(i)])) g_fb_wrong++;
    }
}
class FChk : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"FChk", Scope::Voice, 1, 1, 0, false, {"in"}, {"out"}, {}, Rate::Audio, Sig::F32};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        check_prev(ctx, p.fin[0], nullptr);
        for (int i = 0; i < ctx.frames; i++) p.fout[0][i] = 0.0f;
    }
};
class QChk : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"QChk", Scope::Voice, 1, 1, 0, false, {"in"}, {"out"}, {}};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        check_prev(ctx, nullptr, p.in[0]);
        for (int i = 0; i < ctx.frames; i++) p.out[0][i] = 0;
    }
};

// A global constant read by the voices (GConst, q15 or float by its value), a voice source of another constant (VGen), and a voice
// module that counts the blocks where its input is not the global constant (VRead).
int g_read_wrong = 0, g_read_blocks = 0;
class GConst : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"GConst", Scope::Global, 0, 1, 0, false, {}, {"out"}, {}, Rate::Audio};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override { for (int i = 0; i < ctx.frames; i++) p.out[0][i] = 1000; }
};
class VGen : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"VGen", Scope::Voice, 0, 1, 0, false, {}, {"out"}, {}};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override { for (int i = 0; i < ctx.frames; i++) p.out[0][i] = 30000; }
};
class VRead : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"VRead", Scope::Voice, 1, 1, 0, false, {"in"}, {"out"}, {}};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        g_read_blocks++;
        bool ok = true;
        for (int i = 0; i < ctx.frames; i++) ok = ok && p.in[0][i] == 1000;
        if (!ok) g_read_wrong++;
        for (int i = 0; i < ctx.frames; i++) p.out[0][i] = 0;
    }
};

struct SigRig : DspRig {
    explicit SigRig(int voices = 2) : DspRig(voices) {
        CHECK(eng.registry().add(T_QTHRU, module_type_of<QThru>()));
        CHECK(eng.registry().add(T_FTHRU, module_type_of<FThru>()));
        CHECK(eng.registry().add(T_FREC, module_type_of<FRec>()));
        CHECK(eng.registry().add(T_FCHK, module_type_of<FChk>()));
        CHECK(eng.registry().add(T_QCHK, module_type_of<QChk>()));
        CHECK(eng.registry().add(T_GCONST, module_type_of<GConst>()));
        CHECK(eng.registry().add(T_VGEN, module_type_of<VGen>()));
        CHECK(eng.registry().add(T_VREAD, module_type_of<VRead>()));
    }
    // the output of two notes held for `blocks`
    std::vector<double> play(const GraphDesc &g, int blocks = 40) {
        CHECK(eng.load(g) == Err::Ok);
        eng.note_on(57);
        eng.note_on(64);
        std::vector<double> out;
        run(blocks, &out);
        return out;
    }
};

// the steps of each kind in a compiled plan
struct Kinds { int mix = 0, mixf = 0, tof = 0, toq = 0, copyf = 0, slots = 0, fslots = 0; };
Kinds kinds_of(SigRig &r, const GraphDesc &g) {
    Plan *pl = nullptr;
    Kinds k;
    CHECK(compile_plan(g, r.eng.registry(), r.heap, 2, &pl) == Err::Ok);
    if (!pl) return k;
    for (int i = 0; i < pl->n_pre + pl->n_voice + pl->n_post; i++) {
        switch (pl->steps[i].kind) {
            case StepKind::Mix: k.mix++; break;
            case StepKind::MixF: k.mixf++; break;
            case StepKind::ToF: k.tof++; break;
            case StepKind::ToQ: k.toq++; break;
            case StepKind::CopyF: k.copyf++; break;
            default: break;
        }
    }
    k.slots = pl->n_slots;
    k.fslots = pl->n_fslots;
    free_plan(pl);
    return k;
}

// osc -> [middle] -> VoiceOut, `middle` a list of thru modules
GraphDesc chain(SigRig &r, std::initializer_list<int> middle) {
    GraphDesc g;
    r.add(g, 1, T_OSC);
    int prev = 1, id = 10;
    for (int t : middle) { r.add(g, id, t); g.connect(prev, 0, id, Dst::In, 0); prev = id++; }
    r.add(g, 2, T_VOICE_OUT);
    g.connect(prev, 0, 2, Dst::In, 0);
    return g;
}

}  // namespace

TEST(a_float_module_between_q15_ones_is_transparent_bit_for_bit) {
    SigRig a, b, c;
    const std::vector<double> q = a.play(chain(a, {T_QTHRU}));
    const std::vector<double> f = b.play(chain(b, {T_FTHRU}));
    const std::vector<double> ff = c.play(chain(c, {T_FTHRU, T_FTHRU, T_QTHRU, T_FTHRU}));
    CHECK(rms(q) > 1000.0);
    int diff = 0, diff2 = 0;
    for (size_t i = 0; i < q.size(); i++) { diff += q[i] != f[i]; diff2 += q[i] != ff[i]; }
    std::printf("    %zu samples: q15 -> float -> q15 differs in %d, through float, float, q15, float in %d\n", q.size(), diff, diff2);
    CHECK_EQ(diff, 0);
    CHECK_EQ(diff2, 0);
}

TEST(a_graph_of_one_type_has_no_conversion_and_a_converted_copy_is_shared) {
    SigRig r;
    const Kinds q = kinds_of(r, chain(r, {T_QTHRU, T_QTHRU}));
    CHECK_EQ(q.tof + q.toq, 0);
    CHECK_EQ(q.fslots, 0);                                                   // (no float buffer at all)
    const Kinds f = kinds_of(r, chain(r, {T_FTHRU, T_FTHRU, T_FTHRU}));
    CHECK_EQ(f.tof, 1);                                                      // in from the oscillator, out to VoiceOut: nothing between
    CHECK_EQ(f.toq, 1);
    GraphDesc g;                                                             // one oscillator read by three float modules: one conversion
    r.add(g, 1, T_OSC);
    r.add(g, 2, T_VOICE_OUT);
    for (int k = 0; k < 3; k++) {
        r.add(g, 10 + k, T_FTHRU);
        g.connect(1, 0, 10 + k, Dst::In, 0);
        g.connect(10 + k, 0, 2, Dst::In, 0, 8000);                           // (into VoiceOut through a q15 mix: three conversions back)
    }
    const Kinds s = kinds_of(r, g);
    std::printf("    shared: %d to float, %d to q15, %d q15 mixes\n", s.tof, s.toq, s.mix);
    CHECK_EQ(s.tof, 1);
    CHECK_EQ(s.toq, 3);
    CHECK_EQ(s.mix, 1);
}

// Two cables into a float input are mixed in float: unity cables exactly as the q15 mix (the sum of two q15 values is exact in float),
// others within the q15 mix's own rounding (one per cable); a live depth change reaches the float mix.
TEST(the_float_mix_applies_the_cable_depths) {
    auto graph = [](SigRig &r, int mid, q15 d1, q15 d2) {
        GraphDesc g;
        r.add(g, 1, T_OSC);
        r.add(g, 3, T_OSC)->param[OSC_PITCH] = 7 * 256;                       // (a fifth up: another signal)
        r.add(g, 10, mid);
        r.add(g, 2, T_VOICE_OUT);
        g.connect(1, 0, 10, Dst::In, 0, d1);
        g.connect(3, 0, 10, Dst::In, 0, d2);
        g.connect(10, 0, 2, Dst::In, 0);
        return g;
    };
    for (int pass = 0; pass < 2; pass++) {
        const q15 d1 = pass ? q15{12000} : kUnity, d2 = pass ? q15{-9000} : kUnity;
        SigRig a, b;
        const GraphDesc ga = graph(a, T_QTHRU, d1, d2), gb = graph(b, T_FTHRU, d1, d2);
        CHECK_EQ(kinds_of(b, gb).mixf, 1);
        const std::vector<double> q = a.play(ga), f = b.play(gb);
        double worst = 0;
        for (size_t i = 0; i < q.size(); i++) worst = std::max(worst, std::fabs(q[i] - f[i]));
        std::printf("    depths %d, %d: worst difference to the q15 mix %.0f LSB\n", d1, d2, worst);
        CHECK(worst <= (pass ? 2.0 : 0.0));                                  // (the q15 mix rounds each cable: up to 1 LSB from each)
        CHECK(a.eng.set_edge_depth(1, 4000));                                // live: the second cable at another depth
        CHECK(b.eng.set_edge_depth(1, 4000));
        std::vector<double> q2, f2;
        a.run(20, &q2);
        b.run(20, &f2);
        worst = 0;
        for (size_t i = 0; i < q2.size(); i++) worst = std::max(worst, std::fabs(q2[i] - f2[i]));
        CHECK(worst <= 2.0);
        SigRig c;                                                            // (and the sound did change: the same render, no change)
        c.play(graph(c, T_FTHRU, d1, d2));
        std::vector<double> f3;
        c.run(20, &f3);
        double moved = 0;
        for (size_t i = 0; i < f2.size() && i < f3.size(); i++) moved = std::max(moved, std::fabs(f2[i] - f3[i]));
        CHECK(moved > 1000.0);
    }
}

// A delayed cable reads its source's previous block, per voice: a float output read by a float module and by a q15 one (through a
// conversion of the voice's own feedback buffer), and the q15 output of a float chain read by a float module.
TEST(delayed_cables_between_the_types_keep_each_voice_s_block) {
    for (int reader : {T_FCHK, T_QCHK}) {
        SigRig r(3);
        GraphDesc g;
        r.add(g, 1, T_OSC);
        r.add(g, 10, T_FREC);
        r.add(g, 11, reader);
        r.add(g, 2, T_VOICE_OUT);
        g.connect(1, 0, 10, Dst::In, 0);
        g.connect(10, 0, 11, Dst::In, 0, kUnity, true);                     // delayed
        g.connect(10, 0, 2, Dst::In, 0);
        g.connect(11, 0, 2, Dst::In, 0);
        g_prev.clear(); g_cur.clear(); g_fb_checked = g_fb_wrong = 0;
        r.play(g, 30);
        std::printf("    %s reads a float output through a delayed cable: %d of %d samples not the voice's previous block\n",
                    reader == T_FCHK ? "a float module" : "a q15 module", g_fb_wrong, g_fb_checked);
        CHECK(g_fb_checked >= 40 * kBlock);                                  // (two voices, 29 blocks after their first)
        CHECK_EQ(g_fb_wrong, 0);
    }
}

// A global block read by voice modules must stay as it is for every voice: a voice block made after the last reader may not take its
// slot (the voice section runs once per voice, so the readers run again after it).
TEST(a_global_block_read_by_the_voices_is_the_same_for_every_voice) {
    for (int mid : {T_QTHRU, T_FTHRU}) {
        SigRig r(3);
        GraphDesc g;
        r.add(g, 1, T_GCONST);
        r.add(g, 5, mid);                                                   // (a float one: the global block's conversion in the voices)
        r.add(g, 10, T_VREAD);
        r.add(g, 11, T_VGEN);
        r.add(g, 12, T_QTHRU);
        r.add(g, 2, T_VOICE_OUT);
        g.connect(1, 0, 5, Dst::In, 0);
        g.connect(5, 0, 10, Dst::In, 0);
        g.connect(11, 0, 12, Dst::In, 0);
        g.connect(10, 0, 2, Dst::In, 0);
        g.connect(12, 0, 2, Dst::In, 0);
        g_read_wrong = g_read_blocks = 0;
        r.play(g, 20);
        std::printf("    the global constant through a %s module: wrong in %d of %d voice blocks\n", mid == T_FTHRU ? "float" : "q15", g_read_wrong, g_read_blocks);
        CHECK(g_read_blocks >= 20);
        CHECK_EQ(g_read_wrong, 0);
    }
}

// The conversions themselves: every q15 value through float and back, the clamps, the infinities, NaN (silence), the rounding.
TEST(q15_to_float_and_back_is_exact_and_float_to_q15_clamps) {
    int wrong = 0;
    for (int v = -32768; v <= 32767; v++) {
        const q15 q = static_cast<q15>(v);
        float f;
        block_q15_to_f(&f, &q, 1);
        if (f_to_q15(f) != q) wrong++;
    }
    CHECK_EQ(wrong, 0);
    CHECK_EQ(f_to_q15(1.0f), 32767);
    CHECK_EQ(f_to_q15(7.5f), 32767);
    CHECK_EQ(f_to_q15(-1.0f), -32768);
    CHECK_EQ(f_to_q15(-7.5f), -32768);
    CHECK_EQ(f_to_q15(std::numeric_limits<float>::infinity()), 32767);
    CHECK_EQ(f_to_q15(-std::numeric_limits<float>::infinity()), -32768);
    CHECK_EQ(f_to_q15(std::numeric_limits<float>::quiet_NaN()), 0);
    CHECK_EQ(f_to_q15(100.4f / 32768.0f), 100);
    CHECK_EQ(f_to_q15(100.6f / 32768.0f), 101);
    CHECK_EQ(f_to_q15(-100.4f / 32768.0f), -100);
    CHECK_EQ(f_to_q15(-100.6f / 32768.0f), -101);
    CHECK_EQ(f_to_q15(100.5f / 32768.0f), 100);                              // (ties to even, as the ESP32-S3's ROUND.S)
    CHECK_EQ(f_to_q15(101.5f / 32768.0f), 102);
    CHECK_EQ(f_to_q15(-100.5f / 32768.0f), -100);
    CHECK_EQ(f_to_q15(-101.5f / 32768.0f), -102);
}
