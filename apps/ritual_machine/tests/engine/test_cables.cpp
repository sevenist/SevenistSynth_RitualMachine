// The cables beyond the chains (core/rack.h rack_cable_t; apps/ritual_machine/docs/UI.md step 12): an audio cable from a module into a
// later one (it sums what comes in), the chain's own cable cut or at a level, a modulator with more than one target. Each rack below must
// load (the plan refuses a loop or a voice module feeding a shared one) and sound as its cables say.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <string>
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

// Branch 1: an oscillator into a filter; branch 2: a filter alone (no source of its own); no effects, a flat amp envelope.
void make_rack(rack_t &r, synth_params_t &p) {
    synth_params_default(&p);
    rack_clear(&r);
    CHECK(rack_insert_row(&r, 0, 0, MOD_OSC));
    CHECK(rack_insert_row(&r, 1, 0, MOD_FILTER));
    CHECK(rack_insert_row(&r, 2, 1, MOD_FILTER));
    for (int i : {1, 2}) { r.slot[i].v[MP_FL_CUT] = 12000; r.slot[i].v[MP_FL_ENVAMT] = 0; }
    r.slot[0].v[MP_OC_WAVE] = 1;
    r.cfg.type = SYNTH_MODULAR;
    r.cfg.voices = 2;
    p.amp_env.attack_ms = 1; p.amp_env.decay_ms = 1; p.amp_env.sustain = 1.0f; p.amp_env.release_ms = 50;
    p.amp_env.a_curve = p.amp_env.d_curve = p.amp_env.r_curve = 0;
}

int cables_into(const RackGraph &rg, int node, Dst kind) {
    int n = 0;
    for (int k = 0; k < rg.g.n_edges; k++) n += rg.g.edge[k].dst_id == node && rg.g.edge[k].dst_kind == kind;
    return n;
}
int cables_from(const RackGraph &rg, int node, Dst kind) {
    int n = 0;
    for (int k = 0; k < rg.g.n_edges; k++) n += rg.g.edge[k].src_id == node && rg.g.edge[k].dst_kind == kind;
    return n;
}
int node_of_slot(const rack_t &r, int slot) { return RN_MODULES + 2 * (r.slot[slot].id % 100); }

bool loads(const rack_t &r, const synth_params_t &p, RackGraph &rg) {
    DspRig rig;
    return rack_graph_build(r, p, rig.eng.registry(), rg) && rig.eng.load(rg.g, 2) == Err::Ok;
}

double rms(const rack_t &r, const synth_params_t &p) {
    std::vector<uint8_t> fast(6u << 20), bulk(6u << 20);
    CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
    engine_synth_build(&r, &p);
    engine_synth_note_on(57);
    const int frames = static_cast<int>(0.2 * engine_synth_sample_rate()) / 256 * 256;
    std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
    engine_synth_render(buf.data(), frames);
    engine_synth_shutdown();
    double s = 0;
    for (int i = frames / 2; i < frames; i++) s += static_cast<double>(buf[static_cast<size_t>(i) * 2]) * buf[static_cast<size_t>(i) * 2];
    return std::sqrt(s / (frames / 2));
}

}  // namespace

TEST(an_audio_cable_feeds_a_filter_of_the_other_branch) {
    rack_t r; synth_params_t p;
    make_rack(r, p);
    RackGraph rg;
    CHECK(loads(r, p, rg));
    CHECK(cables_into(rg, node_of_slot(r, 2), Dst::In) == 0);          // branch 2's filter: nothing into it, not built
    const double one = rms(r, p);
    CHECK(one > 300);
    CHECK(rack_audio_connect(&r, 0, 2));                                // the oscillator into branch 2's filter too
    CHECK(loads(r, p, rg));
    CHECK_EQ(cables_into(rg, node_of_slot(r, 2), Dst::In), 1);
    const double both = rms(r, p);
    CHECK(both > one * 1.2);                                            // two paths to the MIX now
    r.slot[0].cut = 1;                                                  // its chain cable cut: only the cable remains
    r.cable[0].amt = 0.5f;
    CHECK(loads(r, p, rg));
    const double half = rms(r, p);
    CHECK(half > 100 && half < one);
    r.cable[0].amt = 0;
    CHECK(rms(r, p) < 20);                                              // the cable at level 0: silence
}

TEST(a_cut_chain_cable_ends_the_signal_and_a_chain_level_scales_it) {
    rack_t r; synth_params_t p;
    make_rack(r, p);
    const double full = rms(r, p);
    r.slot[1].out_lvl = 0.5f;                                           // the filter's chain cable (into the MIX) at half
    RackGraph rg;
    CHECK(loads(r, p, rg));
    const double half = rms(r, p);
    CHECK(std::fabs(half / full - 0.5) < 0.1);
    r.slot[1].cut = 1;
    CHECK(loads(r, p, rg));
    CHECK(rms(r, p) < 20);
}

TEST(a_modulator_drives_each_of_its_targets) {
    rack_t r; synth_params_t p;
    make_rack(r, p);
    CHECK(rack_insert_row(&r, 3, 0, MOD_LFO));
    CHECK(rack_mod_connect(&r, 3, 1, 0));                               // its first target: the filter's cutoff
    CHECK_EQ(r.slot[3].tgt_id, r.slot[1].id);
    CHECK_EQ(r.n_cables, 0);
    CHECK(rack_mod_connect(&r, 3, 0, 0));                               // then the oscillator's pitch: a cable
    CHECK_EQ(r.n_cables, 1);
    CHECK_EQ(rack_mod_targets(&r, 3), 2);
    RackGraph rg;
    CHECK(loads(r, p, rg));
    CHECK_EQ(cables_from(rg, node_of_slot(r, 3), Dst::Param), 2);
}

TEST(the_rack_refuses_a_loop_a_source_input_and_too_many_cables) {
    rack_t r; synth_params_t p;
    make_rack(r, p);
    char why[32];
    CHECK(!rack_audio_can(&r, 1, 0, why, sizeof why));                  // into an oscillator: it takes no audio
    CHECK(std::string(why).find("takes no audio") != std::string::npos);
    CHECK(!rack_audio_can(&r, 0, 1, why, sizeof why));                  // already its chain cable
    CHECK(rack_audio_connect(&r, 1, 2));                                // branch 1's filter into branch 2's
    CHECK(!rack_audio_can(&r, 2, 1, why, sizeof why));                  // back: a loop
    CHECK(std::string(why).find("loop") != std::string::npos);
    CHECK(rack_insert_row(&r, 3, ROW_M, MOD_DELAY));
    CHECK(rack_audio_can(&r, 0, 3, why, sizeof why));                   // a voice module into a shared one: through a send bus
    CHECK(rack_insert_row(&r, 4, 0, MOD_SAT));
    for (int i = 0; i < 4; i++) CHECK(rack_insert_row(&r, r.count, 1, MOD_FILTER));
    int made = 0;
    for (int t = 5; t < r.count; t++) made += rack_audio_connect(&r, 0, t);
    CHECK_EQ(rack_audio_outs(&r, 0), RACK_AUDIO_MAX);                   // the chain cable + 3: at most 4 out
    CHECK_EQ(made, RACK_AUDIO_MAX - 1);
    CHECK(rack_insert_row(&r, r.count, 0, MOD_LFO));
    const int lfo = r.count - 1;
    for (int t = 0; t < lfo; t++) rack_mod_connect(&r, lfo, t, 0);
    CHECK_EQ(rack_mod_targets(&r, lfo), RACK_MOD_MAX);
    CHECK(rack_delete(&r, 0));                                          // a deleted module takes its cables with it
    for (int k = 0; k < r.n_cables; k++) CHECK(rack_find(&r, r.cable[k].from) != RACK_NONE && rack_find(&r, r.cable[k].to) != RACK_NONE);
    RackGraph rg;
    CHECK(loads(r, p, rg));
}

// A voice module into a shared one: through the voice's amp and a send bus (VoiceOut's input 3 / 4, the voices summed), at most two shared
// modules fed that way.
TEST(a_voice_module_cabled_into_row_m_sounds_through_a_send_bus) {
    rack_t r; synth_params_t p;
    make_rack(r, p);
    CHECK(rack_insert_row(&r, 3, ROW_M, MOD_FILTER));                   // row M: a filter (shared, mono)
    r.slot[3].v[MP_FL_CUT] = 12000; r.slot[3].v[MP_FL_ENVAMT] = 0;
    r.slot[0].cut = 1;                                                  // the oscillator's chain cable cut: silence ...
    RackGraph rg;
    CHECK(loads(r, p, rg));
    CHECK(rms(r, p) < 20);
    CHECK(rack_audio_connect(&r, 0, 3));                                // ... then its cable straight into row M's filter
    CHECK(loads(r, p, rg));
    CHECK_EQ(cables_into(rg, node_of_slot(r, 3), Dst::In), 2);         // (the MIX, silent here, and the send bus)
    CHECK(rms(r, p) > 300);
    CHECK(rack_insert_row(&r, 4, ROW_M, MOD_FILTER));                   // two more shared modules: the second takes the other bus,
    CHECK(rack_insert_row(&r, 5, ROW_M, MOD_FILTER));                   // the third is refused
    CHECK(rack_audio_connect(&r, 1, 4));
    char why[48];
    CHECK(!rack_audio_can(&r, 0, 5, why, sizeof why));
    CHECK(std::string(why).find("2 shared") != std::string::npos);
    CHECK(rack_audio_can(&r, 1, 3, why, sizeof why));                   // (one already fed: its bus takes more)
    CHECK(loads(r, p, rg));
}

// A loose module (the map's Add): outside its row's chain, its neighbours chain past it; fed by a cable, it sounds; the rack loads either way.
TEST(a_loose_module_joins_no_chain_until_cabled) {
    rack_t r; synth_params_t p;
    make_rack(r, p);
    const double before = rms(r, p);
    const int fl = rack_add_loose(&r, 0, MOD_FILTER);                    // branch 1's end: after its filter, outside the chain
    CHECK(fl != RACK_NONE);
    if (fl == RACK_NONE) return;
    CHECK(r.slot[fl].loose);
    CHECK_EQ(rack_audio_next(&r, 1), RACK_OUT);
    RackGraph rg;
    CHECK(loads(r, p, rg));
    CHECK_EQ(cables_into(rg, node_of_slot(r, fl), Dst::In), 0);         // nothing into it: not built
    CHECK(std::fabs(rms(r, p) - before) < before * 0.05);               // the sound as before
    int b2 = RACK_NONE;                                                 // (branch 2's filter: one slot further now)
    for (int i = 0; i < r.count; i++) if (r.slot[i].row == 1 && r.slot[i].type == MOD_FILTER) b2 = i;
    CHECK(rack_audio_connect(&r, 0, fl));                               // the oscillator into it, it into branch 2's filter
    CHECK(rack_audio_connect(&r, fl, b2));
    CHECK(loads(r, p, rg));
    CHECK_EQ(cables_into(rg, node_of_slot(r, fl), Dst::In), 1);
    CHECK(rms(r, p) > before * 1.2);                                    // a second path to the MIX
    const int dl = rack_add_loose(&r, ROW_M, MOD_DELAY);                // row M: loose too, the MIX goes on to the output past it
    CHECK(dl != RACK_NONE);
    CHECK(loads(r, p, rg));
}

// The master volume knob (5 % steps, 0 .. 2, through the boost at 1): a live write of MasterOut's level, never a rebuild, whatever the
// rack's cables (a rebuild stalls the audio 100 .. 170 ms on the board).
TEST(the_master_volume_never_rebuilds_the_graph_whatever_the_cables) {
    for (int k = 0; k < 4; k++) {
        rack_t r; synth_params_t p;
        if (k == 0) { synth_params_default(&p); rack_init(&r); }            // the demo rack, its row M effects on
        else make_rack(r, p);
        if (k >= 2) {                                                       // a send bus: a voice module into row M
            CHECK(rack_insert_row(&r, 3, ROW_M, MOD_FILTER));
            CHECK(rack_audio_connect(&r, 0, 3));
        }
        if (k == 3) { CHECK(rack_add_loose(&r, ROW_M, MOD_DELAY) != RACK_NONE); r.slot[0].cut = 1; }
        std::vector<uint8_t> fast(6u << 20), bulk(6u << 20);
        CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
        engine_synth_build(&r, &p);
        int16_t buf[2 * 256];
        engine_synth_render(buf, 256);
        const unsigned before = engine_synth_build_count();
        for (int pass = 0; pass < 2; pass++)
            for (int s = 0; s <= 40; s++) {
                r.cfg.volume = static_cast<float>(pass ? 40 - s : s) / 20.0f;
                engine_synth_set_params(&r, &p);
                engine_synth_render(buf, 64);
            }
        std::printf("    rack %d: %u rebuilds (last: %s)\n", k, engine_synth_build_count() - before, engine_synth_build_reason());
        CHECK_EQ(engine_synth_build_count(), before);
        engine_synth_shutdown();
    }
}

// engine_synth_set_master (the volume alone, no graph built) sends what set_params would: the same render, sample for sample, through
// the boost at 1 and back, with a note held.
TEST(the_master_volume_alone_renders_as_the_full_parameter_push) {
    std::vector<int16_t> out[2];
    for (int way = 0; way < 2; way++) {
        rack_t r; synth_params_t p;
        synth_params_default(&p); rack_init(&r);
        std::vector<uint8_t> fast(6u << 20), bulk(6u << 20);
        CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
        engine_synth_build(&r, &p);
        engine_synth_note_on(57);
        const unsigned before = engine_synth_build_count();
        int16_t buf[2 * 64];
        for (int s = 0; s <= 80; s++) {
            const int k = s <= 20 ? 20 + s : s <= 60 ? 60 - s : s - 60;     // in 5 % steps: 1.0 -> 2.0 -> 0 -> 1.0
            r.cfg.volume = static_cast<float>(k) / 20.0f;
            if (way) engine_synth_set_master(&r); else engine_synth_set_params(&r, &p);
            engine_synth_render(buf, 64);
            out[way].insert(out[way].end(), buf, buf + 128);
        }
        engine_synth_set_params(&r, &p);                                    // (after set_master: nothing left to send, no rebuild)
        CHECK_EQ(engine_synth_build_count(), before);
        engine_synth_shutdown();
    }
    CHECK(out[0].size() == out[1].size());
    size_t diff = 0, loud = 0;
    for (size_t i = 0; i < out[0].size() && i < out[1].size(); i++) { diff += out[0][i] != out[1][i]; loud += out[0][i] != 0; }
    std::printf("    %zu samples, %zu not silent, %zu differ\n", out[0].size(), loud, diff);
    CHECK(loud > out[0].size() / 4);
    CHECK_EQ(diff, static_cast<size_t>(0));
}

// With the fast heap apart (internal RAM on the board), a patch loaded whole is built after the old graph is freed: it lands in the fast heap
// instead of spilling into the bulk one behind the old graph's modules (HWV2, 2026-11-19). A rack edit (engine_synth_build) keeps playing.
TEST(a_new_patch_is_built_in_the_fast_heap_not_behind_the_old_one) {
    std::vector<uint8_t> fast(96u << 10), bulk(6u << 20);
    CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
    std::atomic<bool> stop{false};
    std::thread audio([&] {                                           // the audio thread takes each new graph at its next block
        int16_t buf[64 * 2];
        while (!stop.load()) { engine_synth_render(buf, 64); std::this_thread::sleep_for(std::chrono::microseconds(200)); }
    });
    auto settle = [] {
        for (int i = 0; i < 400 && engine_synth_switching(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        engine_synth_housekeeping();
    };
    rack_t a, b; synth_params_t pa, pb;
    synth_params_default(&pa); rack_init(&a); rack_m_dry(a);         // the demo rack, dry
    make_rack(b, pb);                                                 // another: an oscillator into a filter, a filter alone
    engine_synth_build(&a, &pa);
    settle();
    size_t spilled0 = 0, used_a = 0, spilled1 = 0, used_b = 0;
    engine_synth_heap_stats(&used_a, nullptr, nullptr, nullptr, &spilled0);
    engine_synth_build_patch(&b, &pb);                                // a new patch: the old graph freed first
    settle();
    engine_synth_heap_stats(&used_b, nullptr, nullptr, nullptr, &spilled1);
    std::printf("    fast heap: A %zu bytes, B %zu bytes, spilled by B %zu\n", used_a, used_b, spilled1 - spilled0);
    CHECK(used_b > 0);
    // (B whole behind A would spill all of it; the empty graph loaded in between, while A still fills the heap, may spill its few bytes)
    CHECK(spilled1 - spilled0 < 1024u && spilled1 - spilled0 < used_b / 8);
    stop = true;
    audio.join();
    engine_synth_shutdown();
}
