// The step sequencer (engine/core/sequencer.h, ADR-042): step times on the machine clock (rates, swing, no drift), gates, chords,
// velocity, transpose, probability, ratchets, directions, how a run starts and stops, and the whole path core/seq.h -> engine -> sound.
#include <algorithm>
#include <vector>
#include "rig.h"
#include "engine/core/sequencer.h"
#include "platform/engine/engine_synth.h"

extern "C" {
#include "core/rack.h"
#include "core/seq.h"
#include "core/synth_params.h"
}

using namespace tst;

namespace {

struct Ev { double beat; bool on; int note, vel; };

// A sequencer driven by a clock of its own, block by block: its events with the beat of the block they sounded in.
struct SeqBench {
    Sequencer seq;
    Transport tr;
    SeqSetup set;
    std::vector<Ev> ev;
    explicit SeqBench(int bpm = 120) {
        seq.clear();
        tr.bpm = bpm;
        tr.inc_q32 = (static_cast<uint64_t>(bpm) << 32) / (60ull * static_cast<uint64_t>(kSampleRate));
        set.run = 1;
    }
    void step(int k, int note, int len = 1, int vel = 100, int prob = 100, int ratchet = 1) {
        SeqChunk c;
        c.first = static_cast<uint8_t>(k); c.count = 1;
        c.step[0].note[0] = static_cast<uint8_t>(note);
        c.step[0].len = static_cast<uint8_t>(len); c.step[0].vel = static_cast<uint8_t>(vel);
        c.step[0].prob = static_cast<uint8_t>(prob); c.step[0].ratchet = static_cast<uint8_t>(ratchet);
        seq.chunk(c);
    }
    void chord(int k, int a, int b, int c2, int d, int len = 1) {
        SeqChunk c;
        c.first = static_cast<uint8_t>(k); c.count = 1;
        const int n[4] = {a, b, c2, d};
        for (int i = 0; i < 4; i++) c.step[0].note[i] = static_cast<uint8_t>(n[i]);
        c.step[0].len = static_cast<uint8_t>(len);
        seq.chunk(c);
    }
    void start() { seq.setup(set); tr.running = true; tr.beat_q32 = 0; seq.rewind(); }
    void block() {
        SeqEvent e[kSeqEvents];
        const int n = seq.advance(tr, kBlock, e, kSeqEvents);
        const double beat = static_cast<double>(tr.beat_q32) / 4294967296.0;
        for (int i = 0; i < n; i++) ev.push_back({beat, e[i].on != 0, e[i].note, e[i].vel});
        if (tr.running) tr.beat_q32 += tr.inc_q32 * kBlock;
    }
    void run_beats(double beats) {
        const uint64_t until = tr.beat_q32 + static_cast<uint64_t>(beats * 4294967296.0);
        while (tr.beat_q32 < until) block();
    }
    double block_beats() const { return static_cast<double>(tr.inc_q32) * kBlock / 4294967296.0; }
    std::vector<Ev> ons() const { std::vector<Ev> o; for (const Ev &e : ev) if (e.on) o.push_back(e); return o; }
};

double slot_beat(uint32_t k, int rate, int swing) { return static_cast<double>(seq_slot_time(0, k, seq_steps_per_beat(rate), swing)) / 4294967296.0; }

}  // namespace

TEST(seq_steps_land_on_the_clock_at_every_rate_and_swing) {
    for (int rate = 0; rate < SEQ_RATES; rate++) for (int swing : {0, 20, 50}) for (int bpm : {60, 133, 240}) {
        SeqBench b(bpm);
        b.set.rate = static_cast<uint8_t>(rate); b.set.swing = static_cast<uint8_t>(swing); b.set.steps = 7;
        for (int k = 0; k < 7; k++) b.step(k, 40 + k);
        b.start();
        b.run_beats(12.0);
        const std::vector<Ev> on = b.ons();
        CHECK(on.size() > 20);
        int bad = 0;
        for (size_t i = 0; i < on.size(); i++) {
            const double want = slot_beat(static_cast<uint32_t>(i), rate, swing);
            if (on[i].note != 40 + static_cast<int>(i % 7)) bad++;                     // the steps in order, around the pattern
            if (std::fabs(on[i].beat - want) > 0.51 * b.block_beats()) bad++;           // in the block nearest to its time
        }
        if (bad) std::printf("    rate %d swing %d bpm %d: %d of %zu steps are off\n", rate, swing, bpm, bad, on.size());
        CHECK_EQ(bad, 0);
    }
    // the rates: steps per beat, and the swing delays every second step by that share of a step
    CHECK_EQ(seq_steps_per_beat(SEQ_RATE_8), 2);
    CHECK_EQ(seq_steps_per_beat(SEQ_RATE_16T), 6);
    CHECK_NEAR(slot_beat(3, SEQ_RATE_16, 50), 0.75 + 0.125, 1e-9);
    CHECK_NEAR(slot_beat(4, SEQ_RATE_16, 50), 1.0, 1e-9);
}

TEST(seq_does_not_drift_over_ten_minutes_of_triplets) {
    SeqBench b(133);
    b.set.rate = SEQ_RATE_16T; b.set.steps = 5;
    b.step(0, 60);
    b.start();
    b.run_beats(133.0 * 10.0);                                               // ten minutes
    const std::vector<Ev> on = b.ons();
    CHECK(on.size() == 1596 || on.size() == 1597);                           // 1330 beats x 6 steps / 5 (step 0 of each pass; the one on the last instant may be in)
    const size_t last = 1595;
    CHECK_NEAR(on[last].beat, slot_beat(static_cast<uint32_t>(last * 5), SEQ_RATE_16T, 0), 0.51 * b.block_beats());
}

TEST(seq_gate_lengths_overlap_and_a_pitch_is_released_before_it_starts_again) {
    SeqBench b(120);
    b.set.steps = 8;
    b.step(0, 60, 3);                                                        // 3 steps long: still held when step 1 starts
    b.step(1, 64, 1);
    b.step(4, 60, 8);                                                        // a whole pass long ...
    b.start();
    b.run_beats(4.0);                                                        // ... so the next pass finds it sounding
    const std::vector<Ev> &e = b.ev;
    auto find = [&](size_t from, bool on, int note) { for (size_t i = from; i < e.size(); i++) if (e[i].on == on && e[i].note == note) return i; return e.size(); };
    const size_t on60 = find(0, true, 60), on64 = find(0, true, 64), off64 = find(0, false, 64), off60 = find(0, false, 60);
    CHECK(on60 < on64 && on64 < off64 && off64 < off60);                     // 60 rings over 64
    CHECK_NEAR(e[off64].beat, 0.5, 0.51 * b.block_beats());                  // one step = a 16th note
    CHECK_NEAR(e[off60].beat, 0.75, 0.51 * b.block_beats());                 // three steps
    // step 4's note lasts 8 steps: at the next pass (beat 2) step 0 plays the same pitch, which is released first, in the same block
    const size_t on4 = find(off60, true, 60);
    CHECK_NEAR(e[on4].beat, 1.0, 0.51 * b.block_beats());
    const size_t off4 = find(on4, false, 60), again = find(on4 + 1, true, 60);
    CHECK(off4 + 1 == again);
    CHECK_NEAR(e[again].beat, 2.0, 0.51 * b.block_beats());
    int held = 0, worst = 0;                                                 // never two notes of the same pitch at once
    for (const Ev &x : e) if (x.note == 60) { held += x.on ? 1 : -1; worst = std::max(worst, held); }
    CHECK_EQ(worst, 1);
}

TEST(seq_chords_velocity_and_transpose) {
    SeqBench b(120);
    b.set.steps = 8; b.set.transpose = 7;
    b.chord(0, 60, 64, 67, 71, 2);
    b.step(2, 48, 1, 37);
    b.chord(3, 50, 50, 0, 53);                                               // the same note twice: once
    b.start();
    b.run_beats(1.5);
    const std::vector<Ev> on = b.ons();
    CHECK_EQ(on.size(), 4 + 1 + 2);
    CHECK_EQ(on[0].note, 67); CHECK_EQ(on[1].note, 71); CHECK_EQ(on[2].note, 74); CHECK_EQ(on[3].note, 78);
    CHECK_NEAR(on[3].beat, on[0].beat, 1e-9);                                // the chord starts as one
    CHECK_EQ(on[0].vel, 100);
    CHECK_EQ(on[4].note, 55); CHECK_EQ(on[4].vel, 37);
    CHECK_EQ(on[5].note, 57); CHECK_EQ(on[6].note, 60);
    int offs = 0;
    for (const Ev &x : b.ev) offs += !x.on;
    CHECK_EQ(offs, 7);                                                       // every note ended within the pass
    // a transpose out of the MIDI range is clamped, not wrapped
    SeqBench hi(120);
    hi.set.steps = 1; hi.set.transpose = 48;
    hi.step(0, 108);
    hi.start();
    hi.run_beats(0.2);
    CHECK_EQ(hi.ons()[0].note, 127);
}

TEST(seq_probability_plays_the_share_it_says) {
    for (int prob : {0, 25, 50, 100}) {
        SeqBench b(240);
        b.set.steps = 1; b.set.rate = SEQ_RATE_32;
        b.step(0, 60, 1, 100, prob);
        b.start();
        b.run_beats(250.0);                                                  // 2000 steps
        const int n = static_cast<int>(b.ons().size());
        if (prob == 0) CHECK_EQ(n, 0);
        else if (prob == 100) CHECK(n == 2000 || n == 2001);
        else CHECK_NEAR(n, 20 * prob, 110);                                  // about 5 standard deviations at 50 %
    }
}

TEST(seq_ratchet_hits_share_the_step_and_the_last_one_keeps_the_gate) {
    for (int r = 2; r <= kSeqRatchet; r++) {
        SeqBench b(60);                                                      // a 16th note = 0.25 beat = 250 ms
        b.set.steps = 4;
        b.step(0, 60, 2, 100, 100, r);
        b.start();
        b.run_beats(0.99);
        const std::vector<Ev> on = b.ons();
        CHECK_EQ(static_cast<int>(on.size()), r);
        for (int i = 0; i < r && i < static_cast<int>(on.size()); i++) CHECK_NEAR(on[static_cast<size_t>(i)].beat, 0.25 * i / r, 0.51 * b.block_beats());
        std::vector<Ev> off;
        for (const Ev &x : b.ev) if (!x.on) off.push_back(x);
        CHECK_EQ(static_cast<int>(off.size()), r);
        for (int i = 0; i + 1 < r && i < static_cast<int>(off.size()); i++) CHECK_NEAR(off[static_cast<size_t>(i)].beat, 0.25 * (i + 1) / r, 0.51 * b.block_beats());   // a hit ends where the next starts
        if (!off.empty()) CHECK_NEAR(off.back().beat, 0.5, 0.51 * b.block_beats());                                           // the last one: the step's length (2 steps)
    }
}

TEST(seq_directions) {
    auto order = [](int dir, int steps, double beats) {
        SeqBench b(120);
        b.set.steps = static_cast<uint8_t>(steps); b.set.dir = static_cast<uint8_t>(dir);
        for (int k = 0; k < steps; k++) b.step(k, 60 + k);
        b.start();
        b.run_beats(beats);
        std::vector<int> o;
        for (const Ev &e : b.ons()) o.push_back(e.note - 60);
        return o;
    };
    const std::vector<int> fwd = order(SEQ_FORWARD, 4, 1.4), rev = order(SEQ_REVERSE, 4, 1.4), pp = order(SEQ_PINGPONG, 4, 2.4);
    CHECK(fwd == std::vector<int>({0, 1, 2, 3, 0, 1}));
    CHECK(rev == std::vector<int>({3, 2, 1, 0, 3, 2}));
    CHECK(pp == std::vector<int>({0, 1, 2, 3, 2, 1, 0, 1, 2, 3}));           // the ends are not played twice
    CHECK(order(SEQ_PINGPONG, 1, 0.4) == std::vector<int>({0, 0}));
    CHECK(order(SEQ_PINGPONG, 2, 0.9) == std::vector<int>({0, 1, 0, 1}));
    const std::vector<int> rnd = order(SEQ_RANDOM, 8, 50.0);
    int seen[8] = {};
    for (int k : rnd) { CHECK(k >= 0 && k < 8); if (k >= 0 && k < 8) seen[k]++; }
    for (int k = 0; k < 8; k++) CHECK(seen[k] > 5);                          // 200 steps: every step comes up (25 expected)
}

TEST(seq_starts_with_the_clock_or_on_the_next_bar_and_stops_clean) {
    SeqBench b(120);
    b.set.steps = 4;
    b.step(0, 60, 4);
    b.step(1, 62, 4);
    // armed before the clock starts: step 0 on the first beat
    b.start();
    b.run_beats(0.3);
    CHECK_EQ(b.ons().size(), 2);
    CHECK_NEAR(b.ons()[0].beat, 0.0, 1e-9);
    CHECK_EQ(b.seq.step(), 1);
    // the clock stops: both held notes end, nothing plays
    b.tr.running = false;
    b.block();
    int offs = 0;
    for (const Ev &x : b.ev) offs += !x.on;
    CHECK_EQ(offs, 2);
    CHECK_EQ(b.seq.step(), -1);
    CHECK(!b.seq.pos().running);
    // started again: the first step again
    b.ev.clear();
    b.start();
    b.run_beats(0.1);
    CHECK_EQ(b.ons().size(), 1);
    CHECK_EQ(b.ons()[0].note, 60);
    // run switched off while the clock goes on, then on again: it waits for the next bar line (beat 4) and starts at step 0
    b.set.run = 0; b.seq.setup(b.set);
    b.run_beats(1.2);
    b.ev.clear();
    b.set.run = 1; b.seq.setup(b.set);
    b.run_beats(2.0);
    CHECK_EQ(b.ons().size(), 0);
    CHECK_EQ(b.seq.step(), -1);                                              // armed, not playing yet
    b.run_beats(1.0);
    CHECK(b.ons().size() >= 1);
    if (!b.ons().empty()) { CHECK_EQ(b.ons()[0].note, 60); CHECK_NEAR(b.ons()[0].beat, 4.0, 0.51 * b.block_beats()); }
}

TEST(seq_follows_a_shorter_pattern_another_rate_and_live_step_edits) {
    SeqBench b(120);
    b.set.steps = 16;
    for (int k = 0; k < 16; k++) b.step(k, 40 + k);
    b.start();
    b.run_beats(2.6);                                                        // it is on step 10
    CHECK_EQ(b.seq.step(), 10);
    b.set.steps = 4; b.seq.setup(b.set);                                     // the pattern gets shorter than the playhead: back to its start
    b.ev.clear();
    b.run_beats(1.0);
    std::vector<int> o;
    for (const Ev &e : b.ons()) o.push_back(e.note - 40);
    CHECK(o == std::vector<int>({0, 1, 2, 3}));
    // another rate: the next step keeps its time, the following ones are spaced by the new rate
    b.ev.clear();
    const double at = static_cast<double>(b.tr.beat_q32) / 4294967296.0;
    b.set.rate = SEQ_RATE_8; b.seq.setup(b.set);
    b.run_beats(2.0);
    const std::vector<Ev> on = b.ons();
    CHECK_EQ(on.size(), 4);
    if (on.size() == 4) {
        CHECK(on[0].beat - at < 0.26);                                       // within the 16th note that was running
        for (size_t i = 1; i < 4; i++) CHECK_NEAR(on[i].beat - on[i - 1].beat, 0.5, 1.02 * b.block_beats());
    }
    // a step edited while it plays is heard at its next turn
    b.step(1, 99);
    b.ev.clear();
    b.run_beats(2.0);
    bool heard = false;
    for (const Ev &e : b.ons()) heard |= e.note == 99;
    CHECK(heard);
}

TEST(seq_position_runs_through_the_step_and_backwards_in_reverse) {
    SeqBench b(60);                                                          // a step = 250 ms
    b.set.steps = 4;
    b.start();
    b.run_beats(0.125);                                                      // half of step 0
    CHECK(b.seq.pos().running);
    CHECK_EQ(b.seq.pos().pos_q16 >> 16, 0);
    CHECK_NEAR((b.seq.pos().pos_q16 & 0xFFFF) / 65536.0, 0.5, 0.05);
    b.run_beats(0.25);                                                       // half of step 1
    CHECK_EQ(b.seq.pos().pos_q16 >> 16, 1);
    CHECK_NEAR((b.seq.pos().pos_q16 & 0xFFFF) / 65536.0, 0.5, 0.05);
    SeqBench r(60);
    r.set.steps = 4; r.set.dir = SEQ_REVERSE;
    r.start();
    r.run_beats(0.0625);                                                     // a quarter of the first step, which is step 3
    CHECK_EQ(r.seq.pos().pos_q16 >> 16, 3);
    CHECK_NEAR((r.seq.pos().pos_q16 & 0xFFFF) / 65536.0, 0.75, 0.05);        // the fraction runs down
}

// ---- through the engine: the commands, the voices, the published step ----

TEST(seq_in_the_engine_plays_the_voices_and_reports_its_step) {
    DspRig rig(4);
    GraphDesc g;
    rig.add(g, 1, T_NOTE_IN);
    NodeDesc *o = rig.add(g, 2, T_OSC);
    o->param[OSC_WAVE] = WAVE_SAW_;
    rig.add(g, 3, T_VOICE_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    SeqStep st[kSeqChunk];
    for (int k = 0; k < kSeqChunk; k += 2) { st[k].note[0] = static_cast<uint8_t>(48 + k); st[k].note[1] = static_cast<uint8_t>(55 + k); }
    CHECK(rig.eng.seq_steps(0, kSeqChunk, st));
    CHECK(!rig.eng.seq_steps(60, 8, st));                                   // past the pattern's end
    SeqSetup su;
    su.run = 1; su.steps = 16;
    CHECK(rig.eng.seq_setup(su));
    CHECK_EQ(rig.eng.seq_step(), -1);
    CHECK(rig.rms_over(0.2) < 1.0);                                          // the clock is stopped: silence
    rig.eng.set_transport(120, true, false);
    rig.run(DspRig::blocks_for(0.02));
    CHECK_EQ(rig.eng.seq_step(), 0);
    CHECK_EQ(rig.eng.active_voices(), 2);                                    // the chord of step 0
    CHECK_EQ(rig.eng.voice(0).velocity, 100 * 32767 / 127);
    CHECK(rig.rms_over(0.05) > 500.0);
    rig.run(DspRig::blocks_for(0.45 - 0.07));                                // 450 ms: step 3 (125 ms each)
    CHECK_EQ(rig.eng.seq_step(), 3);
    int w_step = -1, w_frac = -1;                                            // for recording: the step, how far into it, the steps before it
    uint32_t w_count = 99;
    CHECK(rig.eng.seq_where(&w_step, &w_frac, &w_count));
    CHECK_EQ(w_step, 3); CHECK_EQ(w_count, 3);
    CHECK_NEAR(w_frac / 256.0, (0.45 - 0.375) / 0.125, 0.08);                // 75 ms into a 125 ms step
    rig.eng.set_transport(120, false, false);
    rig.run(4);
    CHECK_EQ(rig.eng.seq_step(), -1);
    int gates = 0;
    for (int v = 0; v < 4; v++) gates += rig.eng.voice(v).active && rig.eng.voice(v).gate;
    CHECK_EQ(gates, 0);                                                      // stopped: no key is left down
}

namespace {
struct SeqApp {
    std::vector<uint8_t> fast, bulk;
    SeqApp() : fast(6u << 20), bulk(6u << 20) { CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0); }
    ~SeqApp() { engine_synth_shutdown(); }
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
}  // namespace

TEST(seq_of_the_application_plays_its_pattern_and_follows_its_edits) {
    SeqApp a;
    rack_t rack;
    synth_params_t params;
    synth_params_default(&params);
    params.amp_env.attack_ms = 2; params.amp_env.release_ms = 50;
    rack_clear(&rack);
    rack_insert(&rack, 0, MOD_OSC);                                          // one oscillator: no tail
    rack.slot[0].v[MP_OC_WAVE] = 3;                                         // SawUp
    engine_synth_build(&rack, &params);
    seq_t q;
    seq_init(&q);                                                            // the demo pattern: notes on steps 0, 2, 4 ...
    q.bpm = 120;
    engine_synth_seq_set(&q);
    engine_synth_set_clock(q.bpm, 0);
    CHECK(rms(a.run(0.2)) < 1.0);                                            // not running: silence
    CHECK_EQ(engine_synth_seq_pos(), -1);
    seq_set_running(&q, true);
    engine_synth_seq_set(&q);                                                // the sequencer first, then the clock: step 0 on beat 1
    engine_synth_set_clock(q.bpm, 1);
    const std::vector<double> y = a.run(0.5);
    CHECK(rms(y) > 200.0);
    CHECK_EQ(engine_synth_seq_pos(), 3);                                     // 500 ms of 125 ms steps: the end of step 3
    // every step of the 64 goes over: one far from the start is heard
    q.steps = 64;
    for (int k = 0; k < SEQ_MAX_STEPS; k++) seq_step_clear(&q.step[k]);
    engine_synth_seq_set(&q);
    a.run(2.0);                                                              // the notes are over
    CHECK(rms(a.run(0.2)) < 5.0);
    const int at = engine_synth_seq_pos();
    CHECK(at >= 16 && at < 24);
    q.step[at + 4].note[0] = 60;
    q.step[at + 4].len = 2;
    engine_synth_seq_set(&q);
    CHECK(rms(a.run(0.75)) > 100.0);
    // stopped: the note ends, the step is none
    seq_set_running(&q, false);
    engine_synth_seq_set(&q);
    engine_synth_set_clock(q.bpm, 0);
    a.run(1.0);
    CHECK(rms(a.run(0.2)) < 5.0);
    CHECK_EQ(engine_synth_seq_pos(), -1);
}

// ---- several patterns: the queued one is taken at the end of the playing one ----

namespace {
void queue_pattern(SeqBench &b, const int *notes, int steps, int id, int rate = SEQ_RATE_16) {
    SeqChunk c;
    c.first = 0; c.count = static_cast<uint8_t>(steps); c.queue = 1;
    for (int i = 0; i < steps; i++) c.step[i].note[0] = static_cast<uint8_t>(notes[i]);
    b.seq.chunk(c);
    SeqSetup su;
    su.steps = static_cast<uint8_t>(steps); su.rate = static_cast<uint8_t>(rate);
    su.queue = SEQ_QUEUE; su.id = static_cast<uint8_t>(id);
    b.seq.setup(su);
}
std::vector<int> notes_of(const SeqBench &b) { std::vector<int> o; for (const Ev &e : b.ons()) o.push_back(e.note); return o; }
}  // namespace

TEST(seq_queued_pattern_is_taken_when_the_playing_one_has_played_its_steps) {
    SeqBench b(120);
    b.set.steps = 4;
    for (int k = 0; k < 4; k++) b.step(k, 60 + k);
    b.start();
    b.run_beats(0.3);                                                        // on its second step
    CHECK_EQ(b.seq.taken(), 0u);
    const int next[3] = {70, 71, 72};
    queue_pattern(b, next, 3, 5);
    b.run_beats(0.6);                                                        // the first pattern goes on to its end (beat 1)
    CHECK(notes_of(b) == std::vector<int>({60, 61, 62, 63}));
    CHECK_EQ(b.seq.taken(), 0u);
    b.run_beats(1.4);                                                        // then the queued one, from its first step, around its 3 steps
    CHECK(notes_of(b) == std::vector<int>({60, 61, 62, 63, 70, 71, 72, 70, 71, 72}));
    CHECK_EQ(b.seq.taken(), (1u << 8) | 5u);                                 // taken once, its id
    const std::vector<Ev> on = b.ons();
    CHECK_NEAR(on[4].beat, 1.0, 0.51 * b.block_beats());                     // on time: where the next pass would have started
    CHECK_EQ(b.seq.step(), 2);
    // a pattern at another rate: its steps are spaced by it from the change on
    b.ev.clear();
    const int slow[2] = {40, 41};
    queue_pattern(b, slow, 2, 6, SEQ_RATE_8);
    b.run_beats(2.4);
    const std::vector<Ev> s = b.ons();
    size_t first = 0;
    while (first < s.size() && s[first].note != 40) first++;
    CHECK(first + 3 < s.size());
    if (first + 3 < s.size()) {
        CHECK_EQ(s[first + 1].note, 41); CHECK_EQ(s[first + 2].note, 40);
        CHECK_NEAR(s[first + 1].beat - s[first].beat, 0.5, 1.02 * b.block_beats());
        CHECK_NEAR(s[first + 2].beat - s[first + 1].beat, 0.5, 1.02 * b.block_beats());
    }
    CHECK_EQ(b.seq.taken(), (2u << 8) | 6u);
    // queued, then dropped: the playing pattern loops
    b.ev.clear();
    queue_pattern(b, next, 3, 7);
    SeqSetup un;
    un.queue = SEQ_UNQUEUE;
    b.seq.setup(un);
    b.run_beats(3.0);
    for (int n : notes_of(b)) CHECK(n == 40 || n == 41);
    CHECK_EQ(b.seq.taken(), (2u << 8) | 6u);
    // a random pattern: a pass is as many steps as it has
    SeqBench r(120);
    r.set.steps = 4; r.set.dir = SEQ_RANDOM;
    for (int k = 0; k < 4; k++) r.step(k, 60 + k);
    r.start();
    queue_pattern(r, next, 3, 1);
    r.run_beats(1.4);
    const std::vector<int> o = notes_of(r);
    CHECK_EQ(o.size(), 6u);
    if (o.size() == 6) { for (int i = 0; i < 4; i++) CHECK(o[static_cast<size_t>(i)] < 70); CHECK_EQ(o[4], 70); CHECK_EQ(o[5], 71); }
}

TEST(seq_of_the_application_goes_on_to_a_queued_pattern_and_says_so) {
    SeqApp a;
    rack_t rack;
    synth_params_t params;
    synth_params_default(&params);
    params.amp_env.attack_ms = 2; params.amp_env.release_ms = 50;
    rack_clear(&rack);
    rack_insert(&rack, 0, MOD_OSC);
    rack.slot[0].v[MP_OC_WAVE] = 3;
    engine_synth_build(&rack, &params);
    seq_t q;
    seq_init(&q);
    q.bpm = 120; q.steps = 4;                                                // a pass = 0.5 s
    for (int k = 0; k < SEQ_MAX_STEPS; k++) seq_step_clear(&q.step[k]);     // silent
    seq_set_running(&q, true);
    CHECK_EQ(engine_synth_seq_taken(), -1);
    engine_synth_seq_set(&q);
    engine_synth_set_clock(q.bpm, 1);
    seq_pattern_t p;
    seq_pattern_clear(&p);
    p.steps = 8;
    for (int k = 0; k < 8; k++) { p.step[k].note[0] = 60; p.step[k].len = 1; }
    CHECK(engine_synth_seq_queue(&p, 3) != 0);
    CHECK(rms(a.run(0.45)) < 5.0);                                           // the playing pattern is silent to its end
    CHECK_EQ(engine_synth_seq_taken(), -1);
    CHECK(rms(a.run(0.3)) > 100.0);                                          // then the queued one sounds
    CHECK_EQ(engine_synth_seq_taken(), 3);
    CHECK_EQ(engine_synth_seq_taken(), -1);                                  // said once
    engine_synth_seq_set(&q);                                                // (what the application hands over until it has made the pattern its own:
    seq_pattern_put(&q, &p);                                                 //  the old one, sent whole again, then the new one)
    engine_synth_seq_set(&q);
    a.run(0.5);
    const double full = rms(a.run(2.0));                                     // two passes, every step sounds
    // the application now hands over that pattern, with an edit: the engine ends up with exactly it (step 7 only)
    seq_pattern_put(&q, &p);
    for (int k = 0; k < 7; k++) seq_step_clear(&q.step[k]);
    engine_synth_seq_set(&q);
    a.run(1.0);
    const double one = rms(a.run(2.0));                                      // two passes: one step of eight sounds
    std::printf("    eight steps sounding: rms %.0f; after the edit, one of eight: %.0f\n", full, one);
    CHECK(one > 5.0 && one < 0.6 * full);
    CHECK(engine_synth_seq_queue(NULL, 0) != 0);
}

// ---- the Velocity module: a step's Vel reaches the sound (user, 2026-10-11: velocity as a source in the rack) ----

TEST(velocity_module_makes_a_loud_step_louder_than_a_soft_one) {
    for (int with = 0; with < 2; with++) {
        SeqApp a;
        rack_t rack;
        synth_params_t params;
        synth_params_default(&params);
        params.amp_env.attack_ms = 2; params.amp_env.release_ms = 30;
        rack_clear(&rack);
        rack_insert(&rack, 0, MOD_OSC);
        rack.slot[0].v[MP_OC_WAVE] = 3;                                     // SawUp
        rack.slot[0].v[MP_OC_LEVEL] = 0.2f;
        if (with) {
            CHECK(rack_insert(&rack, 1, MOD_VEL));
            rack.slot[1].tgt_id = rack.slot[0].id;
            rack.slot[1].tgt_param = 1;                                     // the oscillator's level
            rack.slot[1].v[MP_VL_DEPTH] = 80.0f;                           // (a level's depth is in %)
        }
        engine_synth_build(&rack, &params);
        seq_t q;
        seq_init(&q);
        q.bpm = 120; q.steps = 8;                                            // 125 ms steps
        for (int k = 0; k < SEQ_MAX_STEPS; k++) seq_step_clear(&q.step[k]);
        q.step[0].note[0] = 57; q.step[0].vel = 127; q.step[0].len = 2;
        q.step[4].note[0] = 57; q.step[4].vel = 20; q.step[4].len = 2;
        seq_set_running(&q, true);
        engine_synth_seq_set(&q);
        engine_synth_set_clock(q.bpm, 1);
        a.run(0.05);
        const double loud = rms(a.run(0.15));                                // inside step 1's note
        a.run(0.3);
        a.run(0.05);
        const double soft = rms(a.run(0.15));                                // inside step 5's note
        std::printf("    %s the Velocity module: Vel 127 rms %.0f, Vel 20 rms %.0f\n", with ? "with" : "without", loud, soft);
        CHECK(soft > 50.0);
        if (with) CHECK(loud > 2.0 * soft);                                  // level 0.2 + 0.8 x velocity: about 1.0 against 0.33
        else CHECK_NEAR(loud / soft, 1.0, 0.05);                             // nothing listens to the velocity
    }
}
