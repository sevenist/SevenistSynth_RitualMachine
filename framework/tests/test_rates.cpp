// ADR-050: the source decides whether a modulation is read per sample. The plan marks a value (Ports::mod_slow) or an input (in_slow)
// slow when only slow sources feed it (Rate::Control, a Follow processor of slow signals, an LFO under 100 Hz at run time); the
// modules then read it once a block along a straight line. Checked here: the marks, through a probe module that records what it is
// given, and the sound of the block-rate paths against the per-sample ones (a source at depth 0 in the same cable makes it fast).
#include "rig.h"
#include "engine/modules/builtin.h"

using namespace sc;
using namespace tst;

namespace {

constexpr int T_PROBE = 126;

uint16_t g_probe_slow = 0;           // what the probe was given in its last block
uint8_t g_probe_in_slow = 0;
int g_probe_blocks = 0;

// Passes its input through and records the marks: two values, one input
class Probe : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Probe", Scope::Voice, 1, 1, 2, false, {"in"}, {"out"}, {{"a", 0, -32768, 32767}, {"b", 0, -32768, 32767}},
                                     Rate::Follow};
        return i;
    }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        g_probe_slow = p.mod_slow;
        g_probe_in_slow = p.in_slow;
        g_probe_blocks++;
        for (int i = 0; i < ctx.frames; i++) p.out[0][i] = p.in[0][i];
    }
};

struct RateRig : DspRig {
    RateRig() : DspRig(2) { eng.registry().add(T_PROBE, module_type_of<Probe>()); }
    // the marks the probe saw after a note and a few blocks
    void play(GraphDesc &g, int blocks = 4) {
        g_probe_slow = 0xFFFF;
        g_probe_in_slow = 0xFF;
        g_probe_blocks = 0;
        CHECK(eng.load(g) == Err::Ok);
        eng.note_on(60);
        run(blocks);
    }
};

NodeDesc *lfo(RateRig &r, GraphDesc &g, int id, double hz) {
    NodeDesc *n = r.add(g, id, T_LFO_V);
    n->param[LFO_RATE] = static_cast<int32_t>(std::lround(std::log2(hz) * 12 * 256));
    return n;
}

}  // namespace

TEST(the_source_decides_whether_a_value_is_read_per_sample) {
    {                                                                   // an envelope: slow; the note's pitch on the input: slow
        RateRig r;
        GraphDesc g;
        r.add(g, 1, T_NOTE_IN);
        r.add(g, 2, T_ENV);
        r.add(g, 3, T_PROBE);
        r.add(g, 4, T_VOICE_OUT);
        g.connect(1, 1, 2, Dst::In, 0);                                 // gate -> env
        g.connect(1, 0, 3, Dst::In, 0);                                 // pitch -> probe's input
        g.connect(2, 0, 3, Dst::Param, 0, 20000);                       // env -> a (a cable with depth: a mix step)
        g.connect(3, 0, 4, Dst::In, 0);
        r.play(g);
        CHECK(g_probe_blocks > 0);
        CHECK_EQ(g_probe_slow & 3, 3);                                  // a: the envelope; b: no cable (slow)
        CHECK_EQ(g_probe_in_slow & 1, 1);
    }
    {                                                                   // an oscillator: fast; with an envelope on the same value: fast
        RateRig r;
        GraphDesc g;
        r.add(g, 1, T_NOTE_IN);
        r.add(g, 2, T_ENV);
        r.add(g, 5, T_OSC);
        r.add(g, 3, T_PROBE);
        r.add(g, 4, T_VOICE_OUT);
        g.connect(1, 1, 2, Dst::In, 0);
        g.connect(5, 0, 3, Dst::In, 0);                                 // audio on the input
        g.connect(5, 0, 3, Dst::Param, 0, 1000);
        g.connect(2, 0, 3, Dst::Param, 0, 20000);
        g.connect(2, 0, 3, Dst::Param, 1);
        g.connect(3, 0, 4, Dst::In, 0);
        r.play(g);
        CHECK_EQ(g_probe_slow & 3, 2);                                  // a: fast (the oscillator), b: the envelope alone, slow
        CHECK_EQ(g_probe_in_slow & 1, 0);
    }
    {                                                                   // a processor follows what feeds it: an envelope through a VCA that an oscillator moves is fast
        RateRig r;
        GraphDesc g;
        r.add(g, 1, T_NOTE_IN);
        r.add(g, 2, T_ENV);
        r.add(g, 5, T_OSC);
        r.add(g, 6, T_VCA_V);
        r.add(g, 7, T_VCA_V);
        r.add(g, 3, T_PROBE);
        r.add(g, 4, T_VOICE_OUT);
        g.connect(1, 1, 2, Dst::In, 0);
        g.connect(2, 0, 6, Dst::In, 0);                                 // env -> vca 6 (slow) -> a
        g.connect(2, 0, 7, Dst::In, 0);                                 // env -> vca 7, its level moved by the oscillator (fast) -> b
        g.connect(5, 0, 7, Dst::Param, 0, 8000);
        g.connect(6, 0, 3, Dst::Param, 0);
        g.connect(7, 0, 3, Dst::Param, 1);
        g.connect(3, 0, 4, Dst::In, 0);
        r.play(g);
        CHECK_EQ(g_probe_slow & 3, 1);
    }
    {                                                                   // an LFO decides at run time: under 100 Hz slow, above fast, its rate moved by audio fast
        const double hz[3] = {5.0, 200.0, 5.0};
        for (int k = 0; k < 3; k++) {
            RateRig r;
            GraphDesc g;
            lfo(r, g, 2, hz[k]);
            r.add(g, 3, T_PROBE);
            r.add(g, 4, T_VOICE_OUT);
            r.add(g, 5, T_OSC);
            g.connect(2, 0, 3, Dst::Param, 0);
            if (k == 2) g.connect(5, 0, 2, Dst::Param, LFO_RATE, 2000);
            g.connect(3, 0, 4, Dst::In, 0);
            r.play(g);
            std::printf("    LFO %.0f Hz%s: value %s\n", hz[k], k == 2 ? " (rate moved by an oscillator)" : "", g_probe_slow & 1 ? "slow" : "per sample");
            CHECK_EQ(g_probe_slow & 1, k == 0 ? 1 : 0);
        }
    }
}

// The same voice twice: the modulation slow (the block-rate line), and made fast by an oscillator at depth 0 in the same cable (the
// per-sample reading, the values identical). How far apart they sound.
static double null_db(const std::vector<double> &a, const std::vector<double> &b) {
    double s = 0, d = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) { s += a[i] * a[i]; d += (a[i] - b[i]) * (a[i] - b[i]); }
    return d <= 0 ? 200.0 : 10.0 * std::log10(s / d);
}

static std::vector<double> render_voice(int target_type, int target_param, bool force_fast, int lfo_hz, double seconds) {
    RateRig r;
    GraphDesc g;
    r.add(g, 1, T_NOTE_IN);
    NodeDesc *src;
    if (lfo_hz) {
        src = lfo(r, g, 2, lfo_hz);
    } else {
        src = r.add(g, 2, T_ENV);                                       // a pitch / drive envelope: 30 ms up, 120 ms down
        src->param[ENV_ATTACK] = 30; src->param[ENV_DECAY] = 120; src->param[ENV_SUSTAIN] = 0;
        g.connect(1, 1, 2, Dst::In, 0);
    }
    NodeDesc *osc = r.add(g, 5, T_OSC);
    osc->param[OSC_WAVE] = WAVE_SINE_;
    g.connect(1, 0, 5, Dst::In, 0);
    int dst = 5;
    if (target_type == T_SHAPER_V) {
        NodeDesc *sh = r.add(g, 6, T_SHAPER_V);
        sh->param[SHP_DRIVE] = 256;
        g.connect(5, 0, 6, Dst::In, 0);
        dst = 6;
    }
    r.add(g, 7, T_OSC)->param[OSC_WAVE] = WAVE_SAW_;                     // the depth-0 source that makes the cable fast
    g.connect(2, 0, dst, Dst::Param, target_param, 24000);
    if (force_fast) g.connect(7, 0, dst, Dst::Param, target_param, 0);
    r.add(g, 4, T_VOICE_OUT);
    g.connect(dst, 0, 4, Dst::In, 0);
    CHECK(r.eng.load(g) == Err::Ok);
    r.eng.note_on(57);
    std::vector<double> x;
    r.run(DspRig::blocks_for(seconds), &x);
    return x;
}

TEST(a_slow_modulation_read_once_a_block_sounds_as_the_per_sample_one) {
    struct Case { const char *what; int type, param, lfo_hz; double min_db; };
    const Case cases[] = {
        {"Shaper drive, an envelope", T_SHAPER_V, SHP_DRIVE, 0, 50.0},
    };
    for (const Case &c : cases) {
        const std::vector<double> slow = render_voice(c.type, c.param, false, c.lfo_hz, 0.4), fast = render_voice(c.type, c.param, true, c.lfo_hz, 0.4);
        const double d = null_db(fast, slow);
        std::printf("    %-26s null %.1f dB\n", c.what, d);
        CHECK(d >= c.min_db, "%s: %.1f dB", c.what, d);
    }
}

TEST(a_steady_pitch_is_bit_identical_on_the_block_rate_path) {
    // the note's pitch alone on the oscillator's input: slow; with a muted oscillator mixed in: fast. The same samples.
    std::vector<double> out[2];
    for (int k = 0; k < 2; k++) {
        RateRig r;
        GraphDesc g;
        r.add(g, 1, T_NOTE_IN);
        r.add(g, 5, T_OSC)->param[OSC_WAVE] = WAVE_SAW_;
        r.add(g, 7, T_OSC);
        g.connect(1, 0, 5, Dst::In, 0);
        if (k) g.connect(7, 0, 5, Dst::In, 0, 0);
        r.add(g, 4, T_VOICE_OUT);
        g.connect(5, 0, 4, Dst::In, 0);
        CHECK(r.eng.load(g) == Err::Ok);
        r.eng.note_on(64);
        r.run(40, &out[k]);
    }
    size_t differ = 0;
    for (size_t i = 0; i < out[0].size(); i++) differ += out[0][i] != out[1][i];
    CHECK_EQ(differ, 0);
}

// An oscillator's pitch read once a block, against an exact double-precision oscillator, and the per-sample reading against it too.
// (A null between the two readings is the wrong ruler: the phase sums every difference, and the per-sample reading itself drifts from
// the exact pitch by its 1/256-semitone steps: both null at only 24..27 dB against the exact oscillator.) The ruler is the pitch:
// the phase offset to the exact oscillator in 20 ms windows, its change per window in cents (the window's own limit on a sweeping
// pitch included: the per-sample reading shows the same).
static double worst_cents(int kind, bool slow) {
    DspRig r;
    Memory mem{&r.heap, &r.heap, nullptr};
    Module *m = r.eng.registry().get(T_OSC)->create(mem);
    m->set_param(OSC_WAVE, WAVE_SINE_);
    m->reset();
    q15 cv[kBlock], mp[kBlock], out[kBlock];
    Ports p{};
    for (int j = 0; j < kMaxParams; j++) p.mod[j] = nullptr;
    p.in[0] = cv;
    p.mod[OSC_PITCH] = mp;
    p.out[0] = out;
    p.in_slow = slow ? 1 : 0;
    p.mod_slow = slow ? static_cast<uint16_t>(1u << OSC_PITCH) : 0;
    ProcessCtx ctx{kBlock, 0, nullptr, nullptr, nullptr, nullptr, nullptr};
    const double sr = kSampleRate, two_pi = 2 * 3.14159265358979;
    const int win = static_cast<int>(sr / 50), blocks = static_cast<int>(0.6 * sr / kBlock);
    double ph = 0, re = 0, im = 0, hz_sum = 0, prev = 0, worst = 0;
    int k = 0, w = 0;
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < kBlock; i++) {
            const double t = (b * kBlock + i) / sr;
            const double v = kind == 0 ? 0.73 * std::sin(two_pi * 6 * t)                 // a 6 Hz vibrato of +-8.8 semitones
                           : kind == 1 ? 0.73 * (1 - std::exp(-t / 0.03))                 // a fast rise, as an envelope's
                           : 0.73 * std::exp(-t / 0.05);                                  // a kick's pitch drop
            mp[i] = static_cast<q15>(std::lround(v * 32767));
            cv[i] = 0;
        }
        m->process(ctx, p);
        for (int i = 0; i < kBlock; i++) {
            const double hz = 440.0 * std::pow(2.0, (60 * 256 + mp[i] * (12.0 * 256) / 32768.0 - 69 * 256) / 3072.0);
            re += out[i] * std::sin(two_pi * ph);                 // the output against the exact oscillator's sine and cosine:
            im += out[i] * std::cos(two_pi * ph);                 // its phase offset over the window
            hz_sum += hz;
            ph += hz / sr;
            if (++k == win) {
                const double th = std::atan2(im, re), f = hz_sum / win;
                double d = th - prev;
                if (d > 3.14159265) d -= two_pi;
                if (d < -3.14159265) d += two_pi;
                const double cents = d / (two_pi * f * win / sr) * 1200.0 / std::log(2.0);   // the offset's change: a pitch error
                if (w > 0 && std::fabs(cents) > worst) worst = std::fabs(cents);
                prev = th;
                re = im = hz_sum = 0;
                k = 0;
                w++;
            }
        }
    }
    destroy_module(r.heap, m);
    return worst;
}

TEST(a_slow_pitch_read_once_a_block_is_as_close_to_the_exact_pitch_as_the_per_sample_one) {
    const char *what[3] = {"a 6 Hz vibrato, +-8.8 st", "a 30 ms rise of 8.8 st", "a 50 ms drop of 8.8 st"};
    for (int kind = 0; kind < 3; kind++) {
        const double exact = worst_cents(kind, false), block = worst_cents(kind, true);
        std::printf("    %-26s worst pitch error: per sample %.3f cents, once a block %.3f cents\n", what[kind], exact, block);
        CHECK(block < exact + 0.25, "%s: %.3f cents against %.3f", what[kind], block, exact);   // (a quarter of a cent: inaudible)
    }
}
