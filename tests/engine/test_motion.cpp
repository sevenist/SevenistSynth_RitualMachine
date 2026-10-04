// Motion sequencer: the lane rules (STEP / LINEAR between active steps), the module's clock against the note sequencer's
// own timing (core/seq.c), restart / stop, and the whole path rack -> engine -> filter.
#include <algorithm>
#include <utility>
#include <vector>
#include "rig.h"
#include "platform/engine/engine_synth.h"

extern "C" {
#include "core/rack.h"
#include "core/seq.h"
#include "core/synth_config.h"
#include "core/synth_params.h"
}

using namespace tst;

namespace {

MotionBlob blob_with(const int *vals, unsigned active, bool linear, bool bipolar) {
    MotionBlob b{};
    for (int l = 0; l < kMsLanes; l++) {
        for (int i = 0; i < kMsSteps; i++) b.val[l][i] = static_cast<uint8_t>(vals[i]);
        b.active[l] = static_cast<uint16_t>(active);
        b.linear[l] = linear ? 1 : 0;
        b.bipolar[l] = bipolar ? 1 : 0;
    }
    return b;
}

double lane_at(const MotionBlob &b, int steps, double pos) {                  // reference: value 0..100 at a position in steps
    return static_cast<double>(motion_lane_value_q16(b, 0, steps, static_cast<uint32_t>(pos * 65536.0))) / 65536.0;
}

// one motion sequencer (global node 1) with lanes 0 and 1 routed to the master's two inputs
struct MsBench {
    DspRig rig;
    int restart_ = 0;
    explicit MsBench(const MotionBlob &b, int bpm = 120, int steps = 16, int swing = 0) {
        GraphDesc g;
        NodeDesc *m = rig.add(g, 1, T_MSEQ);
        m->param[MSP_BPM] = bpm; m->param[MSP_STEPS] = steps; m->param[MSP_SWING] = swing;
        rig.add(g, 2, T_MASTER_OUT);
        g.connect(1, 0, 2, Dst::In, 0);
        g.connect(1, 1, 2, Dst::In, 1);
        CHECK(rig.eng.load(g) == Err::Ok);
        rig.eng.set_blob(1, &b, sizeof b);
    }
    void start() { rig.eng.set_param(1, MSP_RUN, 1); rig.eng.set_param(1, MSP_RESTART, ++restart_); }
    std::vector<double> run(double seconds) { std::vector<double> x; rig.run(DspRig::blocks_for(seconds), &x); return x; }
};
}  // namespace

TEST(motion_lane_rules_step_linear_wrap_and_edge_cases) {
    int v[16] = {0};
    v[0] = 100; v[4] = 20; v[10] = 60;                                       // three active steps
    const unsigned act = (1u << 0) | (1u << 4) | (1u << 10);
    MotionBlob st = blob_with(v, act, false, false);
    MotionBlob li = blob_with(v, act, true, false);
    CHECK_NEAR(lane_at(st, 16, 0.0), 100, 1e-3);
    CHECK_NEAR(lane_at(st, 16, 3.9), 100, 1e-3);                              // STEP holds the previous active step
    CHECK_NEAR(lane_at(st, 16, 4.0), 20, 1e-3);
    CHECK_NEAR(lane_at(st, 16, 15.9), 60, 1e-3);                              // ... across the wrap too
    CHECK_NEAR(lane_at(li, 16, 2.0), 60, 1e-3);                               // halfway 0 -> 4: (100 + 20) / 2
    CHECK_NEAR(lane_at(li, 16, 4.0), 20, 1e-3);
    CHECK_NEAR(lane_at(li, 16, 7.0), 40, 1e-3);                               // halfway 4 -> 10
    CHECK_NEAR(lane_at(li, 16, 13.0), 80, 1e-3);                              // wrap: 10 -> 0 is 6 steps long; 3 steps in: (60 + 100) / 2
    CHECK_NEAR(lane_at(li, 16, 12.0), 60.0 + 40.0 * 2.0 / 6.0, 1e-3);         // 2 of 6 steps in
    // one active step: constant; none: nothing
    MotionBlob one = blob_with(v, 1u << 4, true, false);
    CHECK_NEAR(lane_at(one, 16, 9.3), 20, 1e-3);
    MotionBlob none = blob_with(v, 0, true, false);
    CHECK(motion_lane_value_q16(none, 0, 16, 0) < 0);
    // the step count shrinks below an active step: that step is ignored
    CHECK_NEAR(lane_at(li, 8, 4.0), 20, 1e-3);
    CHECK_NEAR(lane_at(li, 8, 6.0), 60, 1e-3);                                // 4 -> 0 (wrapping at 8) is 4 steps long, 2 in: (20 + 100) / 2
    MotionBlob hidden = blob_with(v, 1u << 10, false, false);
    CHECK(motion_lane_value_q16(hidden, 0, 8, 0) < 0);                        // its only step is beyond the pattern
}

TEST(motion_module_outputs_unipolar_and_bipolar_steps_and_follows_run) {
    int v[16] = {100, 0, 50, 25};
    for (int i = 4; i < 16; i++) v[i] = 50;
    MotionBlob b = blob_with(v, 0xFFFF, false, false);
    b.bipolar[1] = 1;
    MsBench m(b, 120, 4);                                                    // 125 ms steps = 6000 frames
    std::vector<double> idle = m.run(0.05);
    CHECK_EQ(idle.back(), 0);                                                // stopped: nothing
    m.start();
    std::vector<double> l, r;
    m.rig.run2(DspRig::blocks_for(0.6), &l, &r);
    auto at = [&](double s) { return static_cast<size_t>(s * kSampleRate); };
    CHECK_NEAR(l[at(0.06)], 32767.0, 40);                                    // step 0: 100 %
    CHECK_NEAR(l[at(0.185)], 0.0, 40);                                       // step 1: 0 %
    CHECK_NEAR(l[at(0.31)], 16384.0, 40);                                    // step 2: 50 %
    CHECK_NEAR(l[at(0.435)], 8192.0, 40);                                    // step 3: 25 %
    CHECK_NEAR(l[at(0.56)], 32767.0, 40);                                    // wrapped
    CHECK_NEAR(r[at(0.06)], 32767.0, 40);                                    // bipolar: 100 % = +1
    CHECK_NEAR(r[at(0.185)], -32767.0, 40);                                  // 0 % = -1
    CHECK_NEAR(r[at(0.31)], 0.0, 40);                                        // 50 % = 0
    m.rig.eng.set_param(1, MSP_RUN, 0);
    std::vector<double> off = m.run(0.05);
    CHECK_EQ(off.back(), 0);
}

TEST(motion_clock_matches_the_note_sequencer_with_swing_and_odd_lengths) {
    for (int swing : {0, 20, 50}) for (int steps : {4, 7, 16}) for (int bpm : {90, 110, 133, 240}) {
        // each step has its own value so the step boundaries can be read from the output
        int v[16];
        for (int i = 0; i < 16; i++) v[i] = 5 + 6 * i;
        MsBench m(blob_with(v, 0xFFFF, false, false), bpm, steps, swing);
        m.start();
        std::vector<double> y = m.run(4.0);
        // the note sequencer's step times, polled every millisecond like the application's loop
        seq_t q;
        seq_init(&q);
        q.bpm = bpm; q.steps = steps; q.swing = swing;
        seq_set_running(&q, true);
        std::vector<std::pair<uint32_t, int>> ev;                           // (ms, step)
        seq_event_t se;
        for (uint32_t t = 0; t < 4000; t++) if (seq_tick(&q, t, &se)) ev.push_back({t, q.pos});
        int bad = 0;
        for (auto &e : ev) {
            const size_t first = static_cast<size_t>(e.first) * static_cast<size_t>(kSampleRate) / 1000 + 2 * kBlock;     // just after the start of that step
            if (first >= y.size()) break;
            const double want = (5 + 6 * e.second) / 100.0 * 32767.0;
            if (std::fabs(y[first] - want) > 60.0) bad++;
        }
        if (bad) std::printf("    swing %d steps %d bpm %d: %d of %zu step starts disagree\n", swing, steps, bpm, bad, ev.size());
        CHECK_EQ(bad, 0);
    }
}

TEST(motion_linear_lane_is_smooth_and_restart_rewinds) {
    int v[16] = {0};
    v[0] = 0; v[8] = 100;
    MsBench m(blob_with(v, (1u << 0) | (1u << 8), true, false), 120, 16);   // ramps 0 -> 100 % over 8 steps = 1 s, back over the next second
    m.start();
    std::vector<double> y = m.run(2.0);
    double worst = 0;
    for (size_t i = 1; i < y.size(); i++) worst = std::fmax(worst, std::fabs(y[i] - y[i - 1]));
    std::printf("    linear lane: largest sample-to-sample step %.1f of 32767\n", worst);
    CHECK(worst < 40.0);
    CHECK_NEAR(y[static_cast<size_t>(0.5 * kSampleRate)], 16384.0, 200);
    CHECK_NEAR(y[static_cast<size_t>(1.0 * kSampleRate)], 32767.0, 200);
    CHECK_NEAR(y[static_cast<size_t>(1.5 * kSampleRate)], 16384.0, 200);
    // restart in the middle of the pattern: the lane is back at its first step
    m.run(0.3);
    m.rig.eng.set_param(1, MSP_RESTART, ++m.restart_);
    std::vector<double> z = m.run(0.1);
    CHECK_NEAR(z[static_cast<size_t>(0.05 * kSampleRate)], 0.05 * 32767.0, 250);       // 50 ms into a 1 s ramp (without the restart it would be near 35 %)
}

namespace {
struct SynthApp {
    std::vector<uint8_t> fast, bulk;
    SynthApp() : fast(6u << 20), bulk(6u << 20) { CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0); }
    ~SynthApp() { engine_synth_shutdown(); }
    std::vector<double> run(double seconds) {
        const int frames = static_cast<int>(seconds * engine_synth_sample_rate());
        std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
        int done = 0;
        while (done < frames) { int n = std::min(frames - done, 173); engine_synth_render(&buf[static_cast<size_t>(done) * 2], n); done += n; }
        std::vector<double> l(static_cast<size_t>(frames));
        for (int i = 0; i < frames; i++) l[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2];
        return l;
    }
};
double roughness_between(const std::vector<double> &y, double t0, double t1) {
    double s = 0;
    const size_t i0 = static_cast<size_t>(t0 * kSampleRate), i1 = static_cast<size_t>(t1 * kSampleRate);
    for (size_t i = i0 + 1; i + 1 < i1; i++) s += std::fabs(y[i + 1] - 2 * y[i] + y[i - 1]);
    return s / static_cast<double>(i1 - i0);
}
}  // namespace

TEST(motion_sequencer_in_the_rack_moves_the_filter_in_time) {
    SynthApp a;
    rack_t rack;
    synth_params_t params;
    synth_params_default(&params);
    params.amp_env.attack_ms = 2; params.amp_env.sustain = 1.0f;
    rack_clear(&rack);
    rack_insert(&rack, 0, MOD_OSC);
    rack.slot[0].v[MP_OC_WAVE] = 3;                                         // SawUp
    rack_insert(&rack, 1, MOD_FILTER);
    rack.slot[1].v[MP_FL_CUT] = 1000.0f;
    CHECK(rack_insert(&rack, 2, MOD_MSEQ));
    ms_lane_t *ln = &rack_ms(&rack, 2)->lane[0];
    ln->tgt_id = rack.slot[1].id; ln->tgt_param = 0;                        // filter cutoff
    for (int i = 0; i < MS_STEPS; i++) ln->val[i] = (i & 1) ? 100 : 0;
    ln->active = 0xFFFF; ln->linear = 0; ln->bipolar = 1; ln->depth = 2.0f;                  // octaves of cutoff
    CHECK(rack_insert(&rack, 3, MOD_MSEQ));                                 // a second MS fits (pool of 2)
    CHECK(!rack_insert(&rack, 4, MOD_MSEQ));                                // a third does not
    rack_delete(&rack, 3);
    engine_synth_build(&rack, &params);
    engine_synth_set_clock(120, 16, 0, 1);                                  // 125 ms steps
    engine_synth_motion_restart();
    engine_synth_note_on(45);
    std::vector<double> y = a.run(1.0);
    const double dark = roughness_between(y, 0.3, 0.37), bright = roughness_between(y, 0.43, 0.49);   // steps 2 (0 %) and 3 (100 %) after the filter has settled
    std::printf("    filter driven by the lane: roughness %.0f on a 0%% step, %.0f on a 100%% step\n", dark, bright);
    CHECK(bright > 2.0 * dark);
    // a lane edit goes live; stopping the note sequencer silences the lane (the cutoff goes back to its own value)
    ln->val[1] = 100; ln->val[3] = 0;
    engine_synth_set_params(&rack, &params);
    a.run(0.5);
    engine_synth_set_clock(120, 16, 0, 0);
    std::vector<double> s = a.run(0.5);
    const double idle = roughness_between(s, 0.2, 0.5);
    std::printf("    sequencer stopped: roughness %.0f (neutral cutoff)\n", idle);
    CHECK(idle < bright && idle > dark * 0.5);
}
