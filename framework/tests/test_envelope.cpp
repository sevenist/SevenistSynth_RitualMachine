// Envelope curves, hold, start level and the multi-stage Eg.
#include <algorithm>
#include <vector>
#include "rig.h"

using namespace tst;

namespace {
// NoteIn -> envelope (type) -> VoiceOut; returns the envelope's output while a note is held `hold_s` seconds and released, `total_s` in all
struct EnvBench {
    DspRig rig;
    int node = 2;
    explicit EnvBench(int type) {
        GraphDesc g;
        rig.add(g, 1, T_NOTE_IN);
        rig.add(g, 2, type);
        rig.add(g, 3, T_VOICE_OUT)->param[VO_TAIL_MS] = 3000;
        g.connect(1, 1, 2, Dst::In, 0);
        g.connect(2, 0, 3, Dst::In, 0);
        CHECK(rig.eng.load(g) == Err::Ok);
    }
    std::vector<double> play(double hold_s, double total_s) {
        std::vector<double> y;
        rig.eng.note_on(60);
        rig.run(DspRig::blocks_for(hold_s), &y);
        rig.eng.note_off(60);
        rig.run(DspRig::blocks_for(total_s - hold_s), &y);
        return y;
    }
};
size_t at(double s) { return static_cast<size_t>(s * kSampleRate); }
}  // namespace

TEST(env_curves_bend_the_segments_and_keep_their_length) {
    double mid[3];
    const q15 curves[3] = {-26000, 0, 26000};
    for (int k = 0; k < 3; k++) {
        EnvBench b(T_ENV);
        b.rig.eng.set_param(2, ENV_ATTACK, 100); b.rig.eng.set_param(2, ENV_DECAY, 100); b.rig.eng.set_param(2, ENV_SUSTAIN, 0);
        b.rig.eng.set_param(2, ENV_A_CURVE, curves[k]); b.rig.eng.set_param(2, ENV_D_CURVE, curves[k]);
        std::vector<double> y = b.play(0.5, 0.7);
        mid[k] = y[at(0.050)] / 32767.0;                                    // halfway through the attack
        CHECK(y[at(0.100) + 8] > 32000.0);                                  // every attack ends at its time
        CHECK(y[at(0.215)] < 400.0);                                        // and so does the decay (to the sustain level 0)
        if (k == 1) CHECK_NEAR(mid[k], 0.5, 0.02);                          // curve 0 is a straight line
    }
    std::printf("    attack at half time: curve -0.8 -> %.2f, 0 -> %.2f, +0.8 -> %.2f\n", mid[0], mid[1], mid[2]);
    CHECK(mid[0] < 0.2 && mid[2] > 0.8);                                   // slow start / fast start
}

TEST(env_hold_and_start_level) {
    EnvBench b(T_ENV);
    b.rig.eng.set_param(2, ENV_ATTACK, 10); b.rig.eng.set_param(2, ENV_HOLD, 100); b.rig.eng.set_param(2, ENV_DECAY, 100);
    b.rig.eng.set_param(2, ENV_SUSTAIN, 0); b.rig.eng.set_param(2, ENV_START, 16384);
    b.rig.eng.set_param(2, ENV_D_CURVE, 0);
    std::vector<double> y = b.play(0.6, 0.8);
    CHECK(y[1] > 16000.0 && y[1] < 20000.0);                                // starts at the start level, not at 0
    CHECK(y[at(0.060)] > 32500.0);                                          // held at the top
    CHECK(y[at(0.100)] > 32500.0);
    CHECK_NEAR(y[at(0.160)], 32767.0 * 0.5, 2500.0);                        // halfway down 50 ms into the decay (110 .. 210 ms)
    CHECK(y[at(0.300)] < 100.0);
}

TEST(eg_plays_its_points_sustains_and_releases) {
    EnvBench b(T_EG);
    auto &e = b.rig.eng;
    // a kick-like pitch shape: 2 ms up to 100 %, then 100 ms down to 20 % with a strong fast-start curve, then 100 ms more down to 0
    e.set_param(2, EG_T1, 2); e.set_param(2, EG_L1, 32767); e.set_param(2, EG_C1, 0);
    e.set_param(2, EG_T2, 100); e.set_param(2, EG_L2, 6554); e.set_param(2, EG_C2, 26000);
    e.set_param(2, EG_T3, 100); e.set_param(2, EG_L3, 0); e.set_param(2, EG_C3, 0);
    e.set_param(2, EG_SUSTAIN, 3); e.set_param(2, EG_ONESHOT, 0);
    std::vector<double> y = b.play(1.0, 1.2);
    CHECK(y[at(0.0025)] > 31000.0);                                         // peak after point 1
    const double mid = y[at(0.052)] / 32767.0;                              // halfway through point 2 on a fast-start curve: already close to its target
    std::printf("    eg point 2 at half time: %.2f (target 0.20)\n", mid);
    CHECK(mid < 0.40 && mid > 0.2);
    CHECK_NEAR(y[at(0.1025)], 6554.0, 800.0);                               // end of point 2
    CHECK_NEAR(y[at(0.150)], 6554.0 * 0.5, 1500.0);                         // point 3 is a line down
    CHECK(y[at(0.30)] < 60.0);                                              // sustain at point 3's level (0)
}

TEST(eg_gate_off_releases_early_but_oneshot_plays_out) {
    for (int oneshot = 0; oneshot < 2; oneshot++) {
        EnvBench b(T_EG);
        auto &e = b.rig.eng;
        e.set_param(2, EG_T1, 300); e.set_param(2, EG_L1, 32767); e.set_param(2, EG_C1, 0);       // a slow rise to the top
        e.set_param(2, EG_T2, 0);
        e.set_param(2, EG_SUSTAIN, 1); e.set_param(2, EG_RELEASE, 50); e.set_param(2, EG_RCURVE, 0); e.set_param(2, EG_ONESHOT, oneshot);
        std::vector<double> y = b.play(0.1, 0.6);                           // the key is released after 100 ms, a third of the way up
        const double peak = *std::max_element(y.begin(), y.end()) / 32767.0;
        std::printf("    oneshot %d: peak %.2f\n", oneshot, peak);
        if (oneshot) CHECK(peak > 0.97); else CHECK(peak < 0.5);
    }
}
