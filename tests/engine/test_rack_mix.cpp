// The rack's source mixing: every source of the chain is a cable into the next module's input and the plan sums them in one MIX step
// (one pass, one saturation), no chain of Mix4 nodes. More sources than kMaxFanIn fold into one pass-through mixer.
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

// n oscillators (sines and saws, different tunings) into one filter, no effects, a flat amp envelope.
void make_rack(rack_t &r, synth_params_t &p, int n) {
    synth_params_default(&p);
    rack_clear(&r);
    for (int i = 0; i < n; i++) {
        CHECK(rack_insert(&r, i, MOD_OSC));
        r.slot[i].v[MP_OC_WAVE] = i % 2 ? 1.0f : 0.0f;
        r.slot[i].v[MP_OC_COARSE] = static_cast<float>(i * 7 % 13);
        r.slot[i].v[MP_OC_LEVEL] = 0.8f;
    }
    CHECK(rack_insert(&r, n, MOD_FILTER));
    r.slot[n].v[MP_FL_CUT] = 12000;
    r.slot[n].v[MP_FL_ENVAMT] = 0;
    r.cfg.type = SYNTH_MODULAR;
    r.cfg.voices = 2;
    rack_m_clear(r);
    p.amp_env.attack_ms = 1; p.amp_env.decay_ms = 1; p.amp_env.sustain = 1.0f; p.amp_env.release_ms = 50;
    p.amp_env.a_curve = p.amp_env.d_curve = p.amp_env.r_curve = 0;
}

int count_type(const RackGraph &rg, int type) {
    int n = 0;
    for (int i = 0; i < rg.g.n_nodes; i++) n += rg.g.node[i].type == type;
    return n;
}
int cables_into(const RackGraph &rg, int node) {
    int n = 0;
    for (int k = 0; k < rg.g.n_edges; k++) n += rg.g.edge[k].dst_id == node && rg.g.edge[k].dst_kind == Dst::In && rg.g.edge[k].dst_port == 0;
    return n;
}
int filter_node(const RackGraph &rg) {
    for (int i = 0; i < rg.g.n_nodes; i++) if (rg.g.node[i].type == T_FILTER_V) return rg.g.node[i].id;
    return -1;
}

struct App {
    std::vector<uint8_t> fast, bulk;
    App() : fast(6u << 20), bulk(6u << 20) { CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0); }
    ~App() { engine_synth_shutdown(); }
    std::vector<int> play(const rack_t &r, const synth_params_t &p, double seconds) {
        engine_synth_build(&r, &p);
        engine_synth_note_on(57);
        const int frames = static_cast<int>(seconds * engine_synth_sample_rate()) / 256 * 256;   // whole engine blocks: every play starts on a block edge
        std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, 173);
            engine_synth_render(&buf[static_cast<size_t>(done) * 2], n);
            done += n;
        }
        engine_synth_note_off(57);
        std::vector<int16_t> tail(static_cast<size_t>(engine_synth_sample_rate() / 2 / 256 * 256) * 2);   // let the release end before the next play
        engine_synth_render(tail.data(), static_cast<int>(tail.size() / 2));
        std::vector<int> l(static_cast<size_t>(frames));
        for (int i = 0; i < frames; i++) l[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2];
        return l;
    }
};

}  // namespace

TEST(rack_sources_are_summed_by_the_plan_not_by_mixer_nodes) {
    DspRig rig;
    rack_t r; synth_params_t p;
    make_rack(r, p, 8);
    RackGraph rg;
    CHECK(rack_graph_build(r, p, rig.eng.registry(), rg));
    CHECK_EQ(count_type(rg, T_MIX4_V), 0);
    CHECK_EQ(cables_into(rg, filter_node(rg)), 8);
    CHECK(rig.eng.load(rg.g, 2) == Err::Ok);
}

TEST(rack_more_sources_than_fan_in_fold_into_one_mixer) {
    DspRig rig;
    rack_t r; synth_params_t p;
    make_rack(r, p, 9);                                              // 9 oscillators + the filter: one more than a fan-in
    RackGraph rg;
    CHECK(rack_graph_build(r, p, rig.eng.registry(), rg));
    CHECK_EQ(count_type(rg, T_MIX4_V), 1);
    CHECK_EQ(cables_into(rg, filter_node(rg)), 2);                   // the folded eight + the ninth
    CHECK(rig.eng.load(rg.g, 2) == Err::Ok);
    App a;
    std::vector<int> y = a.play(r, p, 0.2);
    double s = 0;
    for (int v : y) s += static_cast<double>(v) * v;
    CHECK(std::sqrt(s / static_cast<double>(y.size())) > 300);
}

TEST(rack_mix_is_the_sum_of_its_sources) {
    // each oscillator alone (the others at level 0, so the 1/sqrt(n) scale stays the same) adds up to all of them together
    App a;
    rack_t r; synth_params_t p;
    const int n = 4;
    make_rack(r, p, n);
    for (int i = 0; i < n; i++) r.slot[i].v[MP_OC_LEVEL] = 0.35f;
    r.slot[n].v[MP_FL_CUT] = 20000;
    std::vector<int> all = a.play(r, p, 0.1);
    std::vector<long> sum(all.size(), 0);
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < n; i++) r.slot[i].v[MP_OC_LEVEL] = i == k ? 0.35f : 0.0f;
        std::vector<int> one = a.play(r, p, 0.1);
        for (size_t j = 0; j < one.size(); j++) sum[j] += one[j];
    }
    long worst = 0; double s = 0;
    for (size_t j = 0; j < all.size(); j++) { worst = std::max(worst, std::labs(sum[j] - all[j])); s += static_cast<double>(all[j]) * all[j]; }
    CHECK(std::sqrt(s / static_cast<double>(all.size())) > 300);
    CHECK(worst <= 8);                                               // rounding of the filter / VCA / master per source
}

TEST(rack_modulating_osc_level_and_mute_stay_live) {
    App a;
    rack_t r; synth_params_t p;
    make_rack(r, p, 2);
    r.slot[1].tgt_id = r.slot[0].id; r.slot[1].tgt_param = 0;        // osc 2 modulates osc 1's pitch and stays in the chain
    engine_synth_build(&r, &p);
    const unsigned builds = engine_synth_build_count();
    r.slot[1].v[MP_OC_LEVEL] = 0.3f;
    engine_synth_set_params(&r, &p);
    r.slot[1].v[MP_OC_MUTE] = 1;
    engine_synth_set_params(&r, &p);
    r.slot[1].v[MP_OC_MUTE] = 0; r.slot[1].v[MP_OC_LEVEL] = 1.0f;
    engine_synth_set_params(&r, &p);
    CHECK_EQ(engine_synth_build_count(), builds);
}
