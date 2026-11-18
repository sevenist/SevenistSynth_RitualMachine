// The Hadamard FDN reverb (engine/modules/fx_hadamard.cpp): what its design claims, measured on its output.
#include <algorithm>
#include <chrono>
#include "rig.h"

using namespace sc;
using namespace tst;

namespace {
const double kPi = 3.14159265358979323846;

class HImp : public Module {                                          // one full-scale sample on trigger
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"HImp", Scope::Global, 0, 1, 1, false, {}, {"out"}, {{"trig", 0, 0, 1}}};
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
constexpr int T_HIMP = 122;
ModuleType himp_type() { static HImp probe; return {&probe.info(), &create_module<HImp>}; }

// impulse (node 1) and noise (node 4) -> the reverb (node 2: Hadamard, or the plate; L only, or both inputs) -> master
struct HBench {
    DspRig rig;
    explicit HBench(bool both_inputs = true, bool sep_bulk = false, int type = T_HADAMARD) : rig(4, sep_bulk) {
        rig.eng.registry().add(T_HIMP, himp_type());
        GraphDesc g;
        rig.add(g, 1, T_HIMP);
        NodeDesc *z = rig.add(g, 4, T_OSC_G);
        z->param[OSC_WAVE] = WAVE_NOISE_; z->param[OSC_LEVEL] = 0;
        NodeDesc *r = rig.add(g, 2, type);
        r->param[type == T_HADAMARD ? static_cast<int>(HDM_MIX) : static_cast<int>(RVB_MIX)] = kUnity;
        rig.add(g, 3, T_MASTER_OUT);
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(4, 0, 2, Dst::In, 0);
        if (both_inputs) { g.connect(1, 0, 2, Dst::In, 1); g.connect(4, 0, 2, Dst::In, 1); }
        g.connect(2, 0, 3, Dst::In, 0);
        g.connect(2, 1, 3, Dst::In, 1);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.run(DspRig::blocks_for(0.6));                             // (the size glides from its default to the node's value)
    }
    void set(int p, int32_t v) { rig.eng.set_param(2, p, v); rig.run(2); }
    void ir(double seconds, std::vector<double> *l, std::vector<double> *r) {
        rig.eng.set_param(1, 0, 1);
        rig.run2(DspRig::blocks_for(seconds), l, r);
    }
    // 0.2 s of noise at `level`, then its tail: what is recorded starts where the noise stops
    void burst(double seconds, std::vector<double> *l, std::vector<double> *r = nullptr, int32_t level = 20000) {
        rig.eng.set_param(4, OSC_LEVEL, level);
        rig.run(DspRig::blocks_for(0.2));
        rig.eng.set_param(4, OSC_LEVEL, 0);
        rig.run2(DspRig::blocks_for(seconds), l, r);
    }
};

bool prime(int n) {
    if (n < 2) return false;
    for (int f = 2; f * f <= n; f++) if (n % f == 0) return false;
    return true;
}

// decay rate in dB per second: least-squares slope of the 50 ms rms envelope from 0.1 s until it falls under `floor` (output rms).
// The lines are int16 and round towards zero: a tail decays faster than its T60 in its last ~25 dB (it dies out instead of
// sitting on a noise floor), so the floor of this fit stays well above the lines' last bit (4 output steps).
double decay_db_per_s(const std::vector<double> &x, double floor = 12.0) {
    const size_t w = static_cast<size_t>(0.05 * kSampleRate);
    std::vector<double> t, d;
    for (size_t i = 0; i + w <= x.size(); i += w) {
        double s = 0;
        for (size_t k = 0; k < w; k++) s += x[i + k] * x[i + k];
        const double r = std::sqrt(s / static_cast<double>(w)), ts = static_cast<double>(i) / kSampleRate;
        if (ts < 0.1) continue;
        if (r < floor) break;
        t.push_back(ts);
        d.push_back(20 * std::log10(r));
    }
    if (t.size() < 4) return 1e9;
    double mt = 0, md = 0;
    for (size_t i = 0; i < t.size(); i++) { mt += t[i]; md += d[i]; }
    mt /= static_cast<double>(t.size()); md /= static_cast<double>(t.size());
    double num = 0, den = 0;
    for (size_t i = 0; i < t.size(); i++) { num += (t[i] - mt) * (d[i] - md); den += (t[i] - mt) * (t[i] - mt); }
    return -num / den;
}

double corr_of(const std::vector<double> &a, const std::vector<double> &b, size_t from, size_t to) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < to && i < a.size(); i++) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / std::sqrt(aa * bb + 1e-12);
}

// power spectrum of x[t0, t0 + 4096) (Hann window), bins 0..2048
std::vector<double> spectrum(const std::vector<double> &x, double t0) {
    std::vector<double> seg(4096);
    const size_t s = static_cast<size_t>(t0 * kSampleRate);
    for (size_t i = 0; i < seg.size(); i++) seg[i] = x[s + i] * (0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / 4096.0));
    return dft_power(seg);
}
double band(const std::vector<double> &p, double f0, double f1) {
    double s = 0;
    for (size_t k = 0; k < p.size(); k++) { const double f = static_cast<double>(k) * kSampleRate / 4096.0; if (f >= f0 && f <= f1) s += p[k]; }
    return s;
}
}  // namespace

TEST(hadamard_delays_are_distinct_primes_at_every_size) {
    int first[8], last[8];
    hadamard_delays(0, first);
    hadamard_delays(kUnity, last);
    for (int32_t s = 0; s <= kUnity; s += 257) {
        int d[8];
        hadamard_delays(s, d);
        for (int k = 0; k < 8; k++) {
            CHECK(prime(d[k]));
            CHECK(d[k] > 16);                                         // longer than a pass of the tank
            if (k) CHECK(d[k] > d[k - 1]);                            // rising: distinct, so pairwise coprime
        }
    }
    std::printf("    delays at size 0: %d .. %d, at size 1: %d %d %d %d %d %d %d %d (tank samples at %d Hz)\n", first[0], first[7],
                last[0], last[1], last[2], last[3], last[4], last[5], last[6], last[7], kSampleRate / 2);
    CHECK(last[7] > 4 * first[7]);                                    // the size spans x0.2 .. x1
}

TEST(hadamard_tail_follows_its_t60) {
    const int t60[2] = {1000, 3000};
    double rate[2];
    for (int k = 0; k < 2; k++) {
        HBench b;
        b.set(HDM_DECAY, t60[k]);
        b.set(HDM_DAMP, 0); b.set(HDM_MOD, 0);
        std::vector<double> l;
        b.burst(t60[k] * 0.0015, &l);
        rate[k] = decay_db_per_s(l, 300.0);
    }
    std::printf("    decay rate: T60 1 s -> %.1f dB/s (60), T60 3 s -> %.1f dB/s (20)\n", rate[0], rate[1]);
    CHECK(rate[0] > 45.0 && rate[0] < 75.0);
    CHECK(rate[1] > 15.0 && rate[1] < 25.0);
}

TEST(hadamard_is_dense_stereo_and_decorrelated_from_one_input) {
    HBench b(false);                                                  // the impulse on L only
    std::vector<double> l, r;
    b.ir(1.2, &l, &r);
    const size_t s0 = static_cast<size_t>(0.2 * kSampleRate), s1 = static_cast<size_t>(1.0 * kSampleRate);
    const double c = corr_of(l, r, s0, s1);
    double peak = 0, ss = 0, er = 0, early = 0;
    for (size_t i = s0; i < s1; i++) { peak = std::fmax(peak, std::fabs(l[i])); ss += l[i] * l[i]; er += r[i] * r[i]; }
    for (size_t i = 0; i < s0; i++) early = std::fmax(early, std::fabs(l[i]));
    const double crest = peak / std::sqrt(ss / static_cast<double>(s1 - s0));
    std::printf("    L/R correlation %.2f, crest factor %.1f, early peak %.0f, R / L energy %.2f\n", c, crest, early, er / ss);
    CHECK(std::fabs(c) < 0.4);                                        // (as the plate's test; 0.06 .. 0.31 over the rates)
    CHECK(crest < 12.0);                                              // a noise-like tail, no stand-out echoes
    CHECK(early > 200.0 && early < 32767.0);
    CHECK(er / ss > 0.5 && er / ss < 2.0);                            // L alone in fills both sides
}

// The symmetry broken: a late tail with no mode standing out (a network with coinciding modes rings: tall, narrow peaks)
TEST(hadamard_late_tail_has_no_ringing_mode) {
    HBench b;
    b.set(HDM_DECAY, 3000);
    b.set(HDM_DAMP, 0);
    b.set(HDM_MOD, 0);                                                // the network alone, no modulation smearing it
    std::vector<double> l;
    b.ir(1.2, &l, nullptr);
    const std::vector<double> p = spectrum(l, 0.8);
    std::vector<double> v;
    for (size_t k = 0; k < p.size(); k++) { const double f = static_cast<double>(k) * kSampleRate / 4096.0; if (f >= 200 && f <= 4000) v.push_back(p[k]); }
    std::vector<double> sorted = v;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(sorted.size() / 2), sorted.end());
    const double med = sorted[sorted.size() / 2], mx = *std::max_element(v.begin(), v.end());
    std::printf("    late tail 200 Hz .. 4 kHz: tallest bin %.1f dB above the median (noise: ~10)\n", 10 * std::log10(mx / med));
    CHECK(10 * std::log10(mx / med) < 16.0);
}

TEST(hadamard_damping_darkens_the_tail) {
    double ratio[2];
    const int32_t damp[2] = {0, 26000};
    for (int k = 0; k < 2; k++) {
        HBench b;
        b.set(HDM_DECAY, 3000);
        b.set(HDM_DAMP, damp[k]);
        std::vector<double> l;
        b.burst(0.6, &l);
        const std::vector<double> p = spectrum(l, 0.3);
        ratio[k] = 10 * std::log10((band(p, 3000, 9000) + 1e-9) / (band(p, 100, 800) + 1e-9));
    }
    std::printf("    late tail high/low band ratio: bright %.1f dB, dark %.1f dB\n", ratio[0], ratio[1]);
    CHECK(ratio[1] < ratio[0] - 10.0);
}

TEST(hadamard_stays_stable_and_dies_out_to_silence) {
    DspRig rig;
    GraphDesc g;
    NodeDesc *n = rig.add(g, 1, T_OSC_G);
    n->param[OSC_WAVE] = WAVE_NOISE_; n->param[OSC_LEVEL] = 32767;
    NodeDesc *r = rig.add(g, 2, T_HADAMARD);
    r->param[HDM_DECAY] = 30000; r->param[HDM_MIX] = kUnity; r->param[HDM_MOD] = kUnity;
    rig.add(g, 3, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(1, 0, 2, Dst::In, 1);
    g.connect(2, 0, 3, Dst::In, 0);
    g.connect(2, 1, 3, Dst::In, 1);
    CHECK(rig.eng.load(g) == Err::Ok);
    std::vector<double> y;
    rig.run(DspRig::blocks_for(4.0), &y);
    double pk = 0;
    for (double v : y) pk = std::fmax(pk, std::fabs(v));
    rig.eng.set_param(1, OSC_LEVEL, 0);
    rig.eng.set_param(2, HDM_DECAY, 400);
    std::vector<double> a, z;
    rig.run(DspRig::blocks_for(1.0), &a);
    rig.run(DspRig::blocks_for(6.0));
    rig.run(DspRig::blocks_for(0.5), &z);
    double zmax = 0;
    for (double v : z) zmax = std::fmax(zmax, std::fabs(v));
    std::printf("    full-scale noise, T60 30 s, modulation 1: rms %.0f, peak %.0f; 7 s after (T60 0.4 s): rms %.0f, then largest sample %.0f\n",
                rms(y), pk, rms(a), zmax);
    CHECK(pk <= 32767.0 && rms(y) > 1000.0);
    CHECK(zmax == 0.0);                                               // exactly silent: no limit cycle on the lines' last bit
}

// A click is an outlier of the first difference against its rms (a noise-like tail stays around 6 .. 8, as the same tail before the glide).
TEST(hadamard_size_change_glides_without_a_click) {
    HBench b;
    b.set(HDM_DECAY, 4000);
    b.set(HDM_MOD, 0);
    b.set(HDM_SIZE, 26000);
    b.rig.run(DspRig::blocks_for(0.5));
    auto crest_of_steps = [](const std::vector<double> &x) {
        double m = 0, ss = 0;
        for (size_t i = 1; i < x.size(); i++) { const double d = x[i] - x[i - 1]; m = std::fmax(m, std::fabs(d)); ss += d * d; }
        return m / std::sqrt(ss / static_cast<double>(x.size() - 1));
    };
    std::vector<double> before, during, back;
    b.burst(0.3, &before);
    b.rig.eng.set_param(2, HDM_SIZE, 0);                              // the largest jump: x0.85 .. x0.2
    b.rig.run2(DspRig::blocks_for(0.5), &during, nullptr);
    b.rig.eng.set_param(2, HDM_SIZE, kUnity);                         // and back up past it
    b.rig.run2(DspRig::blocks_for(0.5), &back, nullptr);
    std::printf("    largest step / rms of the steps: ringing %.1f, gliding down %.1f, gliding up %.1f (rms %.0f, %.0f)\n", crest_of_steps(before),
                crest_of_steps(during), crest_of_steps(back), rms(during), rms(back));
    CHECK(crest_of_steps(during) < 1.5 * crest_of_steps(before));      // a click: 20 and more
    CHECK(crest_of_steps(back) < 1.5 * crest_of_steps(before));
    CHECK(rms(back) > 30.0);                                          // still ringing: the test saw the glide
}

TEST(hadamard_mix_zero_is_the_dry_signal_and_memory_stays_small) {
    HBench b(true, true);                                             // separate bulk heap: everything is in the fast one
    const double kb = static_cast<double>(b.rig.heap.used()) / 1024.0;
    std::printf("    Hadamard memory: %.1f KB fast, %.1f KB bulk (with the graph's own)\n", kb, static_cast<double>(b.rig.bulk.used()) / 1024.0);
    if (kSampleRate == 44100) CHECK(kb < 26.0);                       // 16 KB of lines + 4 KB of diffusers at the boards' rate, + the test nodes
    std::vector<double> mod0, mod1;
    b.set(HDM_MOD, 0);
    b.ir(0.8, &mod0, nullptr);
    b.rig.eng.set_param(2, HDM_MIX, 0);
    b.rig.run(4);
    std::vector<double> d;
    b.ir(0.1, &d, nullptr);
    CHECK(d[0] > 32000);
    double rest = 0;
    for (size_t i = 1; i < d.size(); i++) rest = std::fmax(rest, std::fabs(d[i]));
    CHECK(rest == 0.0);                                               // the tank skipped and emptied
    b.rig.eng.set_param(2, HDM_MIX, kUnity);
    b.set(HDM_MOD, kUnity);
    b.ir(0.8, &mod1, nullptr);
    const size_t s0 = static_cast<size_t>(0.4 * kSampleRate), s1 = static_cast<size_t>(0.8 * kSampleRate);
    const double c = corr_of(mod0, mod1, s0, s1);
    std::printf("    the late response with modulation 1 against 0: correlation %.2f\n", c);
    CHECK(c < 0.9);                                                   // the moving lines change the tail
    b.rig.eng.shutdown();
    CHECK_EQ(b.rig.heap.used(), 0);
}

// The wet level, kept close to the plate's for the same input and the same decay rate (a swap in the rack keeps its balance).
TEST(hadamard_wet_level_matches_the_plate) {
    HBench plate(true, false, T_REVERB);
    plate.set(RVB_DAMP, 135 * 256);                                   // both bright: their dampings differ
    std::vector<double> pl;
    plate.burst(2.0, &pl);
    const double rate = decay_db_per_s(pl, 300.0);
    HBench h;
    h.set(HDM_DECAY, static_cast<int32_t>(std::lround(60000.0 / rate)));
    h.set(HDM_DAMP, 0);
    std::vector<double> hl;
    h.burst(2.0, &hl);
    const size_t a = static_cast<size_t>(0.05 * kSampleRate), z = static_cast<size_t>(0.5 * kSampleRate);
    const double rp = rms(std::vector<double>(pl.begin() + static_cast<long>(a), pl.begin() + static_cast<long>(z)));
    const double rh = rms(std::vector<double>(hl.begin() + static_cast<long>(a), hl.begin() + static_cast<long>(z)));
    std::printf("    plate decay %.1f dB/s -> Hadamard T60 %.2f s (measured %.1f dB/s); tail rms plate %.0f, Hadamard %.0f (%+.1f dB)\n", rate, 60.0 / rate,
                decay_db_per_s(hl, 300.0), rp, rh, db(rh / rp));
    CHECK(std::fabs(db(rh / rp)) < 3.0);
}

// Host timing against the plate (not the board's numbers: the board is measured with tools/perf_view; this only flags a gross regression)
TEST(hadamard_host_cost_against_the_plate) {
    auto time_of = [](int type, int mix_param) {
        DspRig rig;
        GraphDesc g;
        NodeDesc *n = rig.add(g, 1, T_OSC_G);
        n->param[OSC_WAVE] = WAVE_NOISE_; n->param[OSC_LEVEL] = 8000;
        NodeDesc *r = rig.add(g, 2, type);
        r->param[mix_param] = 16000;
        rig.add(g, 3, T_MASTER_OUT);
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(1, 0, 2, Dst::In, 1);
        g.connect(2, 0, 3, Dst::In, 0);
        g.connect(2, 1, 3, Dst::In, 1);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.run(200);
        const auto t0 = std::chrono::steady_clock::now();
        rig.run(4000);
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 4000.0;
    };
    const double plate = time_of(T_REVERB, RVB_MIX), fdn = time_of(T_HADAMARD, HDM_MIX);
    std::printf("    host: noise + plate %.2f us a block, noise + Hadamard %.2f us a block\n", plate, fdn);
    CHECK(fdn < plate);
}
