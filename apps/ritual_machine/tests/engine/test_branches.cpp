// The rack's two branches (ADR-041 phase 2, step 1): each on its own voice bus with its own Para split and amp, meeting in row M's MIX
// (level / pan per branch); through the production path (rack_t -> mapper -> engine -> stereo samples).
#include <algorithm>
#include <cmath>
#include <vector>
#include "rig.h"
#include "rack_m.h"
#include "platform/engine/engine_synth.h"
#include "platform/engine/rack_graph.h"

extern "C" {
#include "core/rack.h"
#include "core/synth_config.h"
#include "core/synth_params.h"
}

using namespace tst;

namespace {

struct BrApp {
    std::vector<uint8_t> fast, bulk;
    rack_t rack;
    synth_params_t params;
    BrApp() : fast(6u << 20), bulk(6u << 20) {
        CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
        synth_params_default(&params);
        params.amp_env.attack_ms = 1; params.amp_env.decay_ms = 1; params.amp_env.sustain = 1.0f; params.amp_env.release_ms = 60;
        params.amp_env.a_curve = params.amp_env.d_curve = params.amp_env.r_curve = 0;
        rack_clear(&rack);
        rack.cfg.voices = 4;
        rack.cfg.mono = 0;
    }
    ~BrApp() { engine_synth_shutdown(); }
    // a sine oscillator `coarse` semitones up into a wide-open filter, on branch `row`; returns the filter's slot
    int chain(int row, int coarse) {
        const int o = rack.count;
        CHECK(rack_insert_row(&rack, o, row, MOD_OSC));
        rack.slot[o].v[MP_OC_WAVE] = 0;
        rack.slot[o].v[MP_OC_COARSE] = static_cast<float>(coarse);
        CHECK(rack_insert_row(&rack, o + 1, row, MOD_FILTER));
        rack.slot[o + 1].v[MP_FL_CUT] = 15000;
        rack.slot[o + 1].v[MP_FL_ENVAMT] = 0;
        return o + 1;
    }
    void build() { engine_synth_build(&rack, &params); }
    std::vector<double> run(double seconds, std::vector<double> *right = nullptr) {
        const int frames = static_cast<int>(seconds * engine_synth_sample_rate());
        std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, 173);
            engine_synth_render(&buf[static_cast<size_t>(done) * 2], n);
            done += n;
        }
        std::vector<double> l(static_cast<size_t>(frames));
        if (right) right->assign(static_cast<size_t>(frames), 0.0);
        for (int i = 0; i < frames; i++) { l[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2]; if (right) (*right)[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2 + 1]; }
        return l;
    }
};

double tone(const std::vector<double> &x, double hz) {                   // single-bin DFT magnitude, normalised to the length
    const double w = 2 * 3.14159265358979323846 * hz / engine_synth_sample_rate();
    double re = 0, im = 0;
    for (size_t i = 0; i < x.size(); i++) { re += x[i] * std::cos(w * static_cast<double>(i)); im -= x[i] * std::sin(w * static_cast<double>(i)); }
    return std::sqrt(re * re + im * im) / static_cast<double>(x.size());
}
double hz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

int count_type(const RackGraph &rg, int type) {
    int n = 0;
    for (int i = 0; i < rg.g.n_nodes; i++) n += rg.g.node[i].type == type;
    return n;
}

}  // namespace

TEST(both_branches_sound_and_stay_independent) {
    BrApp a;
    a.chain(0, 0);                                                           // branch 1 plays the key, branch 2 a fifth up
    a.chain(1, 7);
    a.build();
    engine_synth_note_on(57);
    a.run(0.1);
    const std::vector<double> y = a.run(0.3);
    const double root = tone(y, hz(57)), fifth = tone(y, hz(64));
    std::printf("    branch 1 (A3) %.0f, branch 2 (E4) %.0f\n", root, fifth);
    CHECK(root > 500.0 && fifth > 500.0);
    CHECK(root < 2.0 * fifth && fifth < 2.0 * root);                           // the same level through the MIX at 100 %
    engine_synth_note_off(57);
    a.run(0.3);
    BrApp b;                                                                  // branch 2 alone: it plays, and its voice ends after the release
    b.chain(1, 7);
    b.build();
    engine_synth_note_on(57);
    b.run(0.1);
    CHECK(tone(b.run(0.2), hz(64)) > 500.0);
    engine_synth_note_off(57);
    b.run(0.4);
    CHECK(rms(b.run(0.1)) < 1.0);
}

TEST(mix_cell_sets_each_branch_level_and_pan_live) {
    BrApp a;
    a.chain(0, 0);
    a.chain(1, 7);
    a.build();
    engine_synth_note_on(57);
    a.run(0.1);
    const double fifth_before = tone(a.run(0.3), hz(64));
    const unsigned builds = engine_synth_build_count();
    a.rack.br_lvl[1] = 0.0f;                                                  // branch 2 off
    a.rack.br_pan[0] = -100.0f;                                               // branch 1 hard left
    engine_synth_set_params(&a.rack, &a.params);
    a.run(0.05);
    std::vector<double> r, l = a.run(0.3, &r);
    std::printf("    left: A3 %.0f E4 %.0f; right: A3 %.0f\n", tone(l, hz(57)), tone(l, hz(64)), tone(r, hz(57)));
    CHECK_EQ(engine_synth_build_count(), builds);                             // gains on the MIX cables, no rebuild
    CHECK(tone(l, hz(57)) > 500.0);
    CHECK(tone(l, hz(64)) < 0.01 * fifth_before);                            // (what is left is the A3's leakage in this short DFT)
    CHECK(tone(r, hz(57)) < 5.0);
}

TEST(para_in_either_branch_or_both) {
    // 0: no Para; 1: Para on branch 1's filter; 2: on branch 2's; 3: both. The shared filters are global nodes, the others per voice;
    // every combination plays both branches.
    for (int combo = 0; combo < 4; combo++) {
        BrApp a;
        const int f1 = a.chain(0, 0), f2 = a.chain(1, 7);
        if (combo & 1) CHECK(rack_set_para(&a.rack, f1, true));
        if (combo & 2) CHECK(rack_set_para(&a.rack, f2, true));
        static RackGraph rg;
        DspRig rig;                                                         // (its registry has every module type)
        CHECK(rack_graph_build(a.rack, a.params, rig.eng.registry(), rg));
        const int shared = (combo & 1) + ((combo >> 1) & 1);
        CHECK_EQ(count_type(rg, T_FILTER_G), shared);
        CHECK_EQ(count_type(rg, T_FILTER_V), 2 - shared);
        CHECK_EQ(count_type(rg, T_BUS_IN), 2);
        a.build();
        engine_synth_note_on(57);
        engine_synth_note_on(60);
        a.run(0.1);
        const std::vector<double> y = a.run(0.3);
        const double root = tone(y, hz(57)), fifth = tone(y, hz(64)), c4 = tone(y, hz(60)), g4 = tone(y, hz(67));
        std::printf("    para %s: A3 %.0f E4 %.0f C4 %.0f G4 %.0f\n", combo == 0 ? "none  " : combo == 1 ? "1     " : combo == 2 ? "2     " : "1 + 2 ", root, fifth, c4, g4);
        CHECK(root > 200.0 && fifth > 200.0 && c4 > 200.0 && g4 > 200.0);         // both keys on both branches
        engine_synth_note_off(57);
        engine_synth_note_off(60);
        a.run(0.5);
        CHECK(rms(a.run(0.1)) < 1.0);                                           // and everything stops after the release
    }
}

namespace {
// The graph the mapper makes of a rack (a rig's registry has every module type).
const RackGraph &graph_of(const rack_t &r, const synth_params_t &p) {
    static RackGraph rg;
    DspRig rig;
    CHECK(rack_graph_build(r, p, rig.eng.registry(), rg));
    return rg;
}
int cables_from(const RackGraph &rg, int node, Dst kind) {
    int n = 0;
    for (int k = 0; k < rg.g.n_edges; k++) n += rg.g.edge[k].src_id == node && rg.g.edge[k].dst_kind == kind;
    return n;
}
int node_of_slot(const rack_t &r, int slot) { return RN_MODULES + 2 * (r.slot[slot].id % 100); }
}  // namespace

// Step 2: an effect before a branch's Para point runs per voice (its mono version); a heavy one starts the shared part, which turns stereo
// there (the filter after it is a pair, as is the shared amp); an effect after a Para point runs once.
TEST(effects_inside_a_branch_per_voice_or_shared) {
    {
        BrApp a;                                                              // OC PH FL: the phaser per voice
        const int f = a.chain(0, 0);
        CHECK(rack_insert_row(&a.rack, f, 0, MOD_PHASER));
        a.rack.slot[f].v[MP_PH_MIX] = 1.0f;
        const RackGraph &rg = graph_of(a.rack, a.params);
        CHECK_EQ(count_type(rg, T_PHASER_V), 1);
        CHECK_EQ(count_type(rg, T_PHASER), 0);
        a.build();
        engine_synth_note_on(57);
        a.run(0.1);
        CHECK(rms(a.run(0.3)) > 1000.0);
    }
    {
        BrApp a;                                                              // OC RV FL: the reverb starts the shared part, the filter after it is a pair
        const int f = a.chain(0, 0);
        CHECK(rack_insert_row(&a.rack, f, 0, MOD_REVERB));
        CHECK_EQ(rack_shared_start(&a.rack, 0), f);
        const RackGraph &rg = graph_of(a.rack, a.params);
        CHECK_EQ(count_type(rg, T_REVERB), 1);
        CHECK_EQ(count_type(rg, T_FILTER_G), 2);
        CHECK_EQ(count_type(rg, T_FILTER_V), 0);
        CHECK_EQ(count_type(rg, T_VCA_G), 2);                                // the shared amp: a pair too
        a.build();
        engine_synth_note_on(57);
        a.run(0.1);
        std::vector<double> r, l = a.run(0.4, &r);
        double diff = 0;
        for (size_t i = 0; i < l.size(); i++) diff = std::fmax(diff, std::fabs(l[i] - r[i]));
        std::printf("    OC RV FL: rms %.0f, largest L / R difference %.0f\n", rms(l), diff);
        CHECK(rms(l) > 500.0);
        CHECK(diff > 100.0);                                                  // the reverb made it stereo
    }
    {
        BrApp a;                                                              // OC FL(Para) TR: the tremolo runs once, stereo
        const int f = a.chain(0, 0);
        CHECK(rack_set_para(&a.rack, f, true));
        CHECK(rack_insert_row(&a.rack, f + 1, 0, MOD_TREM));
        const RackGraph &rg = graph_of(a.rack, a.params);
        CHECK_EQ(count_type(rg, T_TREMOLO), 1);
        CHECK_EQ(count_type(rg, T_TREMOLO_V), 0);
        CHECK_EQ(count_type(rg, T_FILTER_G), 1);                             // still mono up to the tremolo
    }
}

// Modulators reach row M (global, into both nodes of a pair) and row M's filter has its envelope, gated by the keyboard.
TEST(modulators_and_the_filter_envelope_reach_row_m) {
    BrApp a;
    a.chain(0, 0);
    const int fm = rack_add_m(&a.rack, MOD_FILTER);
    CHECK(fm != RACK_NONE);
    a.rack.slot[fm].v[MP_FL_CUT] = 400;
    a.rack.slot[fm].v[MP_FL_ENVAMT] = 4;
    const int lf = a.rack.count;
    CHECK(rack_insert_row(&a.rack, lf, 0, MOD_LFO));
    a.rack.slot[lf].tgt_id = a.rack.slot[fm].id; a.rack.slot[lf].tgt_param = 0;
    const RackGraph &rg = graph_of(a.rack, a.params);
    CHECK_EQ(count_type(rg, T_LFO_G), 1);
    CHECK_EQ(cables_from(rg, node_of_slot(a.rack, lf), Dst::Param), 2);       // into the left and the right filter
    CHECK_EQ(count_type(rg, T_GATE_IN), 1);
    CHECK_EQ(count_type(rg, T_ENV_G), 1);                                     // row M's filter envelope
    a.rack.slot[lf].v[MP_LF_DEPTH] = 0;                                       // the envelope alone: bright at the attack, darker later
    a.rack.slot[0].v[MP_OC_WAVE] = 3;                                         // a saw: harmonics for the filter to take away
    a.rack.slot[fm].v[MP_FL_D] = 150; a.rack.slot[fm].v[MP_FL_S] = 0;
    a.build();
    engine_synth_note_on(45);                                                 // A2, 110 Hz: its 8th harmonic at 880 Hz
    const std::vector<double> early = a.run(0.05);
    a.run(0.4);
    const std::vector<double> late = a.run(0.2);
    const double e = tone(early, 880.0), l = tone(late, 880.0);
    std::printf("    row M filter (400 Hz, envelope 4 oct): 880 Hz early %.0f, late %.0f\n", e, l);
    CHECK(e > 2.5 * l);                                                       // (3.4 at 48 kHz: the 12 dB slope two octaves up, after the decay)
}
