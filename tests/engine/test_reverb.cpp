#include <chrono>
#include "rig.h"
#include "engine/dsp/phase.h"

using namespace sc;
using namespace tst;

namespace {
const double kPi = 3.14159265358979323846;

class TImp : public Module {                                          // one full-scale sample on trigger
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"Imp", Scope::Global, 0, 1, 1, false, {}, {"out"}, {{"trig", 0, 0, 1}}};
        return i;
    }
    void set_param(int, int32_t v) override { if (v) fire_ = true; }
    void process(const ProcessCtx &ctx, const Ports &p) override {
        for (int i = 0; i < ctx.frames; i++) p.out[0][i] = 0;
        if (fire_) { p.out[0][0] = 32767; fire_ = false; }
    }
private:
    bool fire_ = false;
};
constexpr int T_IMP = 121;
ModuleType imp_type() { static TImp probe; return {&probe.info(), &create_module<TImp>}; }

// impulse -> reverb -> master; returns L and R impulse responses (dry removed by mix = 100 %)
struct ReverbBench {
    DspRig rig;
    explicit ReverbBench(bool sep_bulk = false) : rig(4, sep_bulk) {
        rig.eng.registry().add(T_IMP, imp_type());
        GraphDesc g;
        rig.add(g, 1, T_IMP);
        NodeDesc *r = rig.add(g, 2, T_REVERB);
        r->param[RVB_MIX] = kUnity;
        rig.add(g, 3, T_MASTER_OUT);
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(1, 0, 2, Dst::In, 1);
        g.connect(2, 0, 3, Dst::In, 0);
        g.connect(2, 1, 3, Dst::In, 1);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.run(4);
    }
    void ir(double seconds, std::vector<double> *l, std::vector<double> *r) {
        rig.eng.set_param(1, 0, 1);
        rig.run2(DspRig::blocks_for(seconds), l, r);
    }
};

// decay rate in dB per second: least-squares slope of the 50 ms rms envelope, from 0.3 s until it gets within a
// factor 6 of the 16-bit rounding floor (about 2 LSB rms), so the measurement never touches the noise floor
double decay_rate_db(const std::vector<double> &x) {
    const size_t w = static_cast<size_t>(0.05 * kSampleRate);
    std::vector<double> t, d;
    for (size_t i = 0; i + w <= x.size(); i += w) {
        double s = 0;
        for (size_t k = 0; k < w; k++) s += x[i + k] * x[i + k];
        double r = std::sqrt(s / static_cast<double>(w));
        double ts = static_cast<double>(i) / kSampleRate;
        if (ts < 0.3) continue;
        if (r < 12.0) break;
        t.push_back(ts);
        d.push_back(20 * std::log10(r));
    }
    if (t.size() < 4) return 1e9;                                     // decayed so fast that there is nothing to fit: report "very fast"
    double mt = 0, md = 0;
    for (size_t i = 0; i < t.size(); i++) { mt += t[i]; md += d[i]; }
    mt /= static_cast<double>(t.size()); md /= static_cast<double>(t.size());
    double num = 0, den = 0;
    for (size_t i = 0; i < t.size(); i++) { num += (t[i] - mt) * (d[i] - md); den += (t[i] - mt) * (t[i] - mt); }
    return -num / den;
}

double corr(const std::vector<double> &a, const std::vector<double> &b, size_t from, size_t to) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < to && i < a.size(); i++) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / std::sqrt(aa * bb + 1e-12);
}

double seg_power(const std::vector<double> &x, double t0, double t1, double f0, double f1) {
    std::vector<double> seg(x.begin() + static_cast<long>(t0 * kSampleRate), x.begin() + static_cast<long>(t1 * kSampleRate));
    seg.resize(4096);
    std::vector<double> w(seg.size());
    for (size_t i = 0; i < seg.size(); i++) w[i] = seg[i] * (0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(seg.size())));
    std::vector<double> p = dft_power(w);
    double s = 0;
    for (size_t k = 0; k < p.size(); k++) { double f = static_cast<double>(k) * kSampleRate / static_cast<double>(seg.size()); if (f >= f0 && f <= f1) s += p[k]; }
    return s;
}
}  // namespace

TEST(reverb_tail_length_follows_the_decay_setting) {
    double r[3];
    int decays[3] = {9800, 16400, 26000};                             // 0.30, 0.50, 0.79
    for (int k = 0; k < 3; k++) {
        ReverbBench b;
        b.rig.eng.set_param(2, RVB_DECAY, decays[k]);
        std::vector<double> l;
        b.ir(12.0, &l, nullptr);
        r[k] = decay_rate_db(l);
    }
    // loop gain of the figure-8 is decay^4 per ~0.73 s: 0.5 -> 33 dB/s, 0.79 -> 11 dB/s
    std::printf("    decay rate: 0.30 -> %.1f dB/s, 0.50 -> %.1f dB/s, 0.79 -> %.1f dB/s\n", r[0], r[1], r[2]);
    CHECK(r[0] > r[1] && r[1] > r[2]);
    CHECK(r[1] > 20.0 && r[1] < 60.0);
    CHECK(r[2] > 4.0 && r[2] < 20.0);
}

TEST(reverb_is_diffuse_and_stereo_decorrelated) {
    ReverbBench b;
    std::vector<double> l, r;
    b.ir(1.2, &l, &r);
    const size_t s0 = static_cast<size_t>(0.2 * kSampleRate), s1 = static_cast<size_t>(1.0 * kSampleRate);
    double c = corr(l, r, s0, s1);
    // crest factor of the late response: a dense reverb tail looks like noise (a few echoes would give a large peak / rms)
    double peak = 0, ss = 0;
    for (size_t i = s0; i < s1; i++) { peak = std::fmax(peak, std::fabs(l[i])); ss += l[i] * l[i]; }
    double crest = peak / std::sqrt(ss / static_cast<double>(s1 - s0));
    // level: the response is audible and not exploding
    double early = 0;
    for (size_t i = 0; i < 6000; i++) early = std::fmax(early, std::fabs(l[i]));
    std::printf("    L/R correlation %.2f, crest factor %.1f, early peak %.0f\n", c, crest, early);
    CHECK(std::fabs(c) < 0.4);
    CHECK(crest < 12.0);                                              // noise-like (a decaying envelope inflates this a little)
    CHECK(early > 200.0 && early < 32767.0);
}

TEST(reverb_damping_darkens_the_tail) {
    double ratio[2];
    int damp[2] = {130 * 256, 55 * 256};                              // bright, dark (about 15 kHz vs 200 Hz)
    for (int k = 0; k < 2; k++) {
        ReverbBench b;
        b.rig.eng.set_param(2, RVB_DECAY, 24000);
        b.rig.eng.set_param(2, RVB_DAMP, damp[k]);
        std::vector<double> l;
        b.ir(1.6, &l, nullptr);
        double hi = seg_power(l, 0.6, 1.0, 3000.0, 9000.0), lo = seg_power(l, 0.6, 1.0, 100.0, 800.0);
        ratio[k] = 10 * std::log10((hi + 1e-9) / (lo + 1e-9));
    }
    std::printf("    late tail high/low band ratio: bright %.1f dB, dark %.1f dB\n", ratio[0], ratio[1]);
    CHECK(ratio[1] < ratio[0] - 10.0);
}

TEST(reverb_stays_stable_at_maximum_decay_and_loud_input) {
    DspRig rig;
    GraphDesc g;
    NodeDesc *n = rig.add(g, 1, T_OSC_G);
    n->param[OSC_WAVE] = WAVE_NOISE_; n->param[OSC_LEVEL] = 20000;
    NodeDesc *r = rig.add(g, 2, T_REVERB);
    r->param[RVB_DECAY] = 32400; r->param[RVB_MIX] = kUnity;
    rig.add(g, 3, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(1, 0, 2, Dst::In, 1);
    g.connect(2, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    std::vector<double> y;
    rig.run(DspRig::blocks_for(2.0), &y);
    rig.eng.set_param(1, OSC_LEVEL, 0);
    std::vector<double> a, b, c;
    rig.run(DspRig::blocks_for(2.0), &a);
    rig.run(DspRig::blocks_for(2.0), &b);
    rig.run(DspRig::blocks_for(4.0), &c);
    double pk = 0;
    for (double v : y) pk = std::fmax(pk, std::fabs(v));
    std::printf("    loud noise: rms %.0f; tail rms after the input stops: %.0f -> %.0f -> %.0f\n", rms(y), rms(a), rms(b), rms(c));
    CHECK(pk <= 32767.0);
    CHECK(rms(b) < rms(a));
    CHECK(rms(c) < rms(b));
    CHECK(rms(c) < rms(y) * 0.8);                                     // it decays, it does not sit at a limit cycle
}

TEST(reverb_predelay_dry_mix_and_memory_placement) {
    ReverbBench b(true);                                              // separate bulk heap
    std::printf("    reverb memory: %.0f KB fast (tank), %.0f KB bulk (pre-delay)\n", static_cast<double>(b.rig.heap.used()) / 1024.0, static_cast<double>(b.rig.bulk.used()) / 1024.0);
    CHECK(b.rig.bulk.used() >= static_cast<size_t>(kSampleRate / 5) * sizeof(q15));
    CHECK(b.rig.heap.used() > 40 * 1024);
    b.rig.eng.set_param(2, RVB_PREDELAY, 100);
    b.rig.run(4);
    std::vector<double> l;
    b.ir(0.5, &l, nullptr);
    double before = 0, after = 0;
    const size_t pre = static_cast<size_t>(0.100 * kSampleRate);
    for (size_t i = 0; i < pre - 40; i++) before = std::fmax(before, std::fabs(l[i]));
    for (size_t i = pre; i < pre + 3000; i++) after = std::fmax(after, std::fabs(l[i]));
    std::printf("    pre-delay 100 ms: peak before %.0f, after %.0f\n", before, after);
    CHECK(before < 20.0);
    CHECK(after > 300.0);

    b.rig.eng.set_param(2, RVB_MIX, 0);                               // mix 0: dry only
    b.rig.run(4);
    std::vector<double> d;
    b.ir(0.1, &d, nullptr);
    CHECK(d[0] > 32000);
    double rest = 0;
    for (size_t i = 1; i < d.size(); i++) rest = std::fmax(rest, std::fabs(d[i]));
    CHECK(rest < 2.0);
    b.rig.eng.shutdown();
    CHECK_EQ(b.rig.heap.used(), 0);
    CHECK_EQ(b.rig.bulk.used(), 0);
}

TEST(reverb_size_and_modulation_changes_are_smooth) {
    DspRig rig;
    GraphDesc g;
    NodeDesc *n = rig.add(g, 1, T_OSC_G);
    n->param[OSC_WAVE] = WAVE_SINE_; n->param[OSC_PITCH] = hz_to_pitch(300.0); n->param[OSC_LEVEL] = 8000;
    NodeDesc *r = rig.add(g, 2, T_REVERB);
    r->param[RVB_MIX] = kUnity; r->param[RVB_DECAY] = 20000;
    rig.add(g, 3, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(1, 0, 2, Dst::In, 1);
    g.connect(2, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.run(DspRig::blocks_for(1.5));
    std::vector<double> before, during;
    rig.run(DspRig::blocks_for(0.5), &before);
    rig.eng.set_param(2, RVB_SIZE, 0);                                // shrink to 0.5 x : the tank glides
    rig.run(DspRig::blocks_for(0.1), &during);
    rig.eng.set_param(2, RVB_SIZE, kUnity);                           // and back to 1.25 x
    rig.run(DspRig::blocks_for(1.5), &during);
    auto max_step = [](const std::vector<double> &v) { double m = 0; for (size_t i = 1; i < v.size(); i++) m = std::fmax(m, std::fabs(v[i] - v[i - 1])); return m; };
    std::printf("    largest sample step: steady %.0f, while the size glides %.0f\n", max_step(before), max_step(during));
    CHECK(max_step(during) < 4.0 * max_step(before) + 50.0);

    rig.eng.set_param(2, RVB_MOD, 0);                                 // modulation off / full: both stable and different
    std::vector<double> m0, m1;
    rig.run(DspRig::blocks_for(1.0));
    rig.run(DspRig::blocks_for(0.3), &m0);
    rig.eng.set_param(2, RVB_MOD, kUnity);
    rig.run(DspRig::blocks_for(1.0));
    rig.run(DspRig::blocks_for(0.3), &m1);
    CHECK(rms(m0) > 100.0 && rms(m1) > 100.0);
    CHECK(rms(m0) < 32000.0 && rms(m1) < 32000.0);

    auto t0 = std::chrono::steady_clock::now();
    const int blocks = 3000;
    rig.run(blocks);
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / blocks;
    double budget = 1e6 * kBlock / kSampleRate;
    std::printf("    host cost: %.1f us per block (%.1f %% of real time on this PC)\n", us, 100.0 * us / budget);
}

namespace {
// Estimate the delay (samples) of `wet` relative to `x` in a chunk, by normalised cross-correlation + parabolic peak.
double chunk_lag(const std::vector<double> &x, const std::vector<double> &wet, size_t at, size_t len, int lo, int hi) {
    double best = -1e9, bl = lo;
    std::vector<double> c(static_cast<size_t>(hi - lo + 1));
    for (int lag = lo; lag <= hi; lag++) {
        double ab = 0, aa = 0, bb = 0;
        for (size_t i = at; i < at + len; i++) { double a = x[i - static_cast<size_t>(lag)], b = wet[i]; ab += a * b; aa += a * a; bb += b * b; }
        double v = ab / std::sqrt(aa * bb + 1e-9);
        c[static_cast<size_t>(lag - lo)] = v;
        if (v > best) { best = v; bl = lag; }
    }
    size_t k = static_cast<size_t>(bl - lo);
    if (k > 0 && k + 1 < c.size()) {
        double d = c[k - 1] - 2 * c[k] + c[k + 1];
        if (d != 0) bl += 0.5 * (c[k - 1] - c[k + 1]) / d;
    }
    return bl;
}
}  // namespace

TEST(chorus_modes_swing_the_delay_like_the_juno) {
    auto render = [](int mode, bool with_chorus, std::vector<double> *l, std::vector<double> *r) {
        l->clear();
        r->clear();
        DspRig rig;
        GraphDesc g;
        NodeDesc *n = rig.add(g, 1, T_OSC_G);
        n->param[OSC_WAVE] = WAVE_NOISE_; n->param[OSC_LEVEL] = 8000;
        int src = 1;
        if (with_chorus) {
            NodeDesc *c = rig.add(g, 2, T_CHORUS);
            c->param[CHR_MODE] = mode; c->param[CHR_MIX] = kUnity;
            g.connect(1, 0, 2, Dst::In, 0);
            g.connect(1, 0, 2, Dst::In, 1);
            src = 2;
        }
        rig.add(g, 3, T_MASTER_OUT);
        g.connect(src, 0, 3, Dst::In, 0);
        g.connect(src, with_chorus ? 1 : 0, 3, Dst::In, 1);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.run2(DspRig::blocks_for(6.0), l, r);                       // 3 cycles of the slowest LFO
    };
    std::vector<double> x, xr, yl, yr;
    render(0, false, &x, &xr);

    // off: bit-exact dry
    render(CHRM_OFF, true, &yl, &yr);
    double worst = 0;
    for (size_t i = 0; i < x.size(); i++) worst = std::fmax(worst, std::fabs(yl[i] - x[i]));
    CHECK(worst < 1.0);

    struct M { int mode; double min_ms, max_ms, period_s; size_t chunk; };
    for (M m : {M{CHRM_I, 1.54, 5.15, 1.0 / 0.513, 1024}, M{CHRM_II, 1.54, 5.15, 1.0 / 0.863, 1024}, M{CHRM_I_II, 3.1, 3.6, 1.0 / 9.75, 256}}) {
        render(m.mode, true, &yl, &yr);
        std::vector<double> wl(x.size()), wr(x.size());
        for (size_t i = 0; i < x.size(); i++) { wl[i] = yl[i] - x[i]; wr[i] = yr[i] - x[i]; }
        std::vector<double> lagl, lagr;
        const int lo = static_cast<int>(0.001 * kSampleRate), hi = static_cast<int>(0.007 * kSampleRate);
        for (size_t at = 4000; at + m.chunk < x.size() - 100; at += m.chunk) {
            lagl.push_back(chunk_lag(x, wl, at, m.chunk, lo, hi) * 1000.0 / kSampleRate);
            lagr.push_back(chunk_lag(x, wr, at, m.chunk, lo, hi) * 1000.0 / kSampleRate);
        }
        double mn = 1e9, mx = -1e9;
        for (double v : lagl) { mn = std::fmin(mn, v); mx = std::fmax(mx, v); }
        // opposite phase between the channels: the two lags always add up to twice the centre delay
        double sum_dev = 0;
        for (size_t i = 0; i < lagl.size(); i++) sum_dev = std::fmax(sum_dev, std::fabs(lagl[i] + lagr[i] - 2 * 3.345));
        // period from the up-crossings of the centre delay
        std::vector<double> cross;
        for (size_t i = 1; i < lagl.size(); i++) if (lagl[i - 1] < 3.345 && lagl[i] >= 3.345) cross.push_back((static_cast<double>(i) - 0.5) * static_cast<double>(m.chunk) / static_cast<double>(kSampleRate));
        double period = cross.size() >= 2 ? (cross.back() - cross.front()) / static_cast<double>(cross.size() - 1) : 0.0;
        std::printf("    mode %d: delay %.2f..%.2f ms (spec %.2f..%.2f), period %.3f s (spec %.3f), L+R centre error %.2f ms\n",
                    m.mode, mn, mx, m.min_ms, m.max_ms, period, m.period_s, sum_dev);
        CHECK(mn < m.min_ms + 0.35 && mn > m.min_ms - 0.35);
        CHECK(mx > m.max_ms - 0.35 && mx < m.max_ms + 0.35);
        CHECK_NEAR(period, m.period_s, m.period_s * 0.2);
        CHECK(sum_dev < 0.5);
    }
}
