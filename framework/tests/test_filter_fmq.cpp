// The Filter's quality setting under audio-rate cutoff FM (FLT_FM_Q): the coefficients at every 2nd / 4th sample, interpolated. Close
// to the exact output, stable at the most violent modulation, and nothing changes where the modulation is smooth (the per-block paths).
#include <cmath>
#include <cstdio>
#include <vector>
#include "rig.h"
#include "engine/dsp/svf.h"

namespace {
using namespace sc;
using namespace tst;

// The filter alone: a saw plus noise in, its cutoff modulated by `mod(n)` (per sample; `slow`: marked slow), `blocks` blocks out.
template <typename F>
std::vector<double> render(int type, int fmq, int sections, int32_t res, F mod, bool slow = false, int blocks = 300) {
    DspRig rig;
    const ModuleType *t = rig.eng.registry().get(type);
    Memory mem{&rig.heap, &rig.heap, nullptr};
    Module *m = t->create(mem);
    const ModuleInfo &info = m->info();
    for (int j = 0; j < info.n_param; j++) m->set_param(j, info.param[j].def);
    m->set_param(FLT_MODE, FLTM_LP);
    m->set_param(FLT_SECTIONS, sections);
    m->set_param(FLT_RES, res);
    m->set_param(FLT_CUTOFF, 84 * 256);
    m->set_param(FLT_CUT_MOD, 96 * 256);                              // full scale = 8 octaves, as the rack sets it
    m->set_param(FLT_FM_Q, fmq);
    m->reset();
    q15 in[kBlock], md[kBlock], out[kBlock], zero[kBlock] = {};
    VoiceState vs;
    vs.note = 60; vs.pitch = vs.target_pitch = 60 * 256; vs.gate = vs.active = vs.started = true;
    ProcessCtx ctx{kBlock, 0, &vs, nullptr, nullptr, nullptr, nullptr};
    ctx.silence = zero;
    uint32_t noise = 777u;
    std::vector<double> y;
    int64_t n = 0;
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < kBlock; i++, n++) {
            noise = noise * 1664525u + 1013904223u;
            in[i] = static_cast<q15>(static_cast<int>((n * 37) % 400) * 40 - 8000 + (static_cast<int32_t>(noise >> 16) - 32768) / 8);
            md[i] = mod(n);
        }
        Ports p{};
        p.in[0] = in;
        p.mod[FLT_CUTOFF] = md;
        p.mod_slow = slow ? 0xFFFF : 0;
        p.out[0] = out;
        m->process(ctx, p);
        ctx.time++;
        vs.started = false;
        for (int i = 0; i < kBlock; i++) y.push_back(out[i]);
    }
    destroy_module(rig.heap, m);
    return y;
}

double err_db(const std::vector<double> &a, const std::vector<double> &ref) {
    double e = 0, r = 0;
    for (size_t i = 0; i < a.size(); i++) { e += (a[i] - ref[i]) * (a[i] - ref[i]); r += ref[i] * ref[i]; }
    return 10 * std::log10((e + 1e-9) / (r + 1e-9));
}

// svf_a1_batch4 with the section count at run time, as full sets: c[j][s]
template <int N>
void batch4_n(const float *g, const float *k, SvfCoefF (*c)[4]) {
    float a1[4][N];
    svf_a1_batch4<N>(g, k, a1);
    for (int j = 0; j < 4; j++)
        for (int s = 0; s < N; s++) { c[j][s].a1 = a1[j][s]; c[j][s].a2 = g[j] * a1[j][s]; c[j][s].a3 = g[j] * c[j][s].a2; c[j][s].k = k[s]; }
}
void svf_coef_batch4(const float *g, const float *k, int n, SvfCoefF (*c)[4]) {
    if (n == 1) batch4_n<1>(g, k, c);
    else if (n == 2) batch4_n<2>(g, k, c);
    else if (n == 3) batch4_n<3>(g, k, c);
    else batch4_n<4>(g, k, c);
}

}  // namespace

TEST(filter_fm_quality_stays_close_to_exact_and_stable) {
    const double fs = kSampleRate;
    // (max2 / max4: the largest difference from exact allowed, dB. A slow FM is not heard; a fast, deep one loses its brightest sidebands and
    // some level: the trade the setting names, 2026-11-20: 1.3 kHz resonant -14 / -5 dB, 5 kHz over 8 octaves about all of it)
    struct Case { const char *name; double hz, amp; int sections; int32_t res; double max2, max4; };
    const Case cases[] = {{"a 220 Hz FM, 2 octaves", 220, 8192, 2, 0, -50.0, -40.0}, {"a 1.3 kHz FM, 4 octaves, resonant", 1300, 16384, 1, 20000, -10.0, -3.0},
                          {"a 5 kHz FM, 8 octaves, 4 sections", 5000, 32767, 4, 32767, 0.0, 0.0}};
    for (const Case &c : cases) {
        auto mod = [&](int64_t n) { return static_cast<q15>(std::lround(c.amp * std::sin(6.283185307 * c.hz * static_cast<double>(n) / fs))); };
        const std::vector<double> ex = render(T_FILTER_V, FLTQ_EXACT, c.sections, c.res, mod);
        const std::vector<double> e2 = render(T_FILTER_V, FLTQ_EVERY2, c.sections, c.res, mod);
        const std::vector<double> e4 = render(T_FILTER_V, FLTQ_EVERY4, c.sections, c.res, mod);
        double pk = 0;
        for (double v : e4) pk = std::fmax(pk, std::fabs(v));
        std::printf("    %-36s every 2: %.1f dB, every 4: %.1f dB from exact; rms %.0f / %.0f / %.0f\n", c.name, err_db(e2, ex), err_db(e4, ex), rms(ex),
                    rms(e2), rms(e4));
        // stable at every rate: no runaway, no collapse (the most violent case at 96 kHz sweeps a full resonance to near Nyquist, where
        // the exact filter rings loudest: every 4 keeps 26 % of its level)
        CHECK(rms(e4) > 0.15 * rms(ex) && rms(e4) < 1.5 * rms(ex) && pk < 32767.0);
        if (kSampleRate == 48000) {                                     // the trade as measured and named, at 48 kHz (at 32 kHz the same
            CHECK(err_db(e2, ex) < c.max2);                             // FM is faster per sample: more lost; at 96 kHz less)
            CHECK(err_db(e4, ex) < c.max4);
            CHECK(err_db(e2, ex) < err_db(e4, ex));
            CHECK(rms(e4) > 0.25 * rms(ex));
        }
    }
    // smooth modulation never takes the per-sample path: the setting changes nothing there (1 Hz: a straight line within 6/256 semitone
    // over a block of 256 at 32 kHz, the longest block of the matrix)
    auto lfo = [&](int64_t n) { return static_cast<q15>(std::lround(12000.0 * std::sin(6.283185307 * 1.0 * static_cast<double>(n) / fs))); };
    for (bool slow : {false, true}) {
        const std::vector<double> a = render(T_FILTER_V, FLTQ_EXACT, 2, 0, lfo, slow), b = render(T_FILTER_V, FLTQ_EVERY4, 2, 0, lfo, slow);
        size_t diff = 0;
        for (size_t i = 0; i < a.size(); i++) diff += a[i] != b[i];
        CHECK_EQ(diff, static_cast<size_t>(0));
    }
}

// The per-sample path's four sets per divide (svf_a1_batch4) against one divide per set (svf_coef_batch): the same to float rounding,
// across the whole range of g (to tan(0.45 pi)) and every section count, and a resonant two-section SVF fed by each stays within a hair.
TEST(filter_four_sets_per_divide_equal_the_single_ones_to_rounding) {
    uint32_t r = 4242u;
    auto rnd = [&]() { r = r * 1664525u + 1013904223u; return static_cast<float>(r >> 8) * (1.0f / 16777216.0f); };
    double worst = 0;
    for (int n = 1; n <= 4; n++) {
        const float k[4] = {1.85f, 0.42f, 1.1f, 0.05f};
        for (int t = 0; t < 20000; t++) {
            float g[4];
            for (float &x : g) { const float u = rnd(); x = 0.0005f * std::pow(12600.0f, u); }      // 0.0005 .. 6.3, even in log
            SvfCoefF c4[4][4];
            svf_coef_batch4(g, k, n, c4);
            for (int j = 0; j < 4; j++) {
                SvfCoefF c1[4];
                svf_coef_batch(g[j], k, n, c1);
                for (int s = 0; s < n; s++) {
                    worst = std::fmax(worst, std::fabs(c4[j][s].a1 - c1[s].a1) / c1[s].a1);
                    worst = std::fmax(worst, std::fabs(c4[j][s].a3 - c1[s].a3) / std::fmax(c1[s].a3, 1e-30f));
                }
            }
        }
    }
    // the filter: a resonant two-section low-pass, its cutoff jumping every sample over 8 octaves, fed by the two kinds of sets
    const float k[2] = {1.85f, 0.05f};
    SvfStateF s1[2], s4[2];
    double e = 0, ref = 0;
    for (int i = 0; i < 48000; i += 4) {
        float g[4];
        for (float &x : g) x = 0.005f * std::pow(256.0f, rnd());
        SvfCoefF c4[4][4];
        svf_coef_batch4(g, k, 2, c4);
        for (int j = 0; j < 4; j++) {
            SvfCoefF c1[2];
            svf_coef_batch(g[j], k, 2, c1);
            float x1 = rnd() - 0.5f, x4 = x1, lp, bp, hp;
            for (int s = 0; s < 2; s++) { svf_tick_f(c1[s], s1[s], x1, lp, bp, hp); x1 = lp; svf_tick_f(c4[j][s], s4[s], x4, lp, bp, hp); x4 = lp; }
            e += (x4 - x1) * static_cast<double>(x4 - x1);
            ref += x1 * static_cast<double>(x1);
        }
    }
    const double db = 10 * std::log10((e + 1e-30) / ref);
    std::printf("    four sets per divide: worst coefficient difference %.2g (relative), the filter's output %.0f dB from one divide per set\n", worst, db);
    CHECK(worst < 1e-6);                                    // a few float steps (one step is 6e-8)
    CHECK(db < -110.0);                                     // far below the 16-bit output's step (-96 dB at full scale)
}
