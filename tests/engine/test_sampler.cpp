#include <functional>
#include "rig.h"
#include "engine/modules/sampler_modules.h"
#include "engine/sampler/sample_bank.h"
#include "engine/sampler/sim_storage.h"

using namespace sc;
using namespace tst;

namespace {
const double kPi = 3.14159265358979323846;

// Engine + sample bank + simulated card. The loader is pumped once per block with a clock that follows the audio.
struct SamplerRig {
    std::vector<uint8_t> mem, bulk_mem;
    Heap heap, bulk;
    SimStorage storage;
    SampleBank bank;
    Engine eng;
    uint64_t now_us = 0;
    explicit SamplerRig(const StorageModel &m = StorageModel{}, int voices = 4) : mem(1 << 22), bulk_mem(1 << 24), storage(m) {
        heap.init(mem.data(), mem.size());
        bulk.init(bulk_mem.data(), bulk_mem.size());
        Memory mm{&heap, &bulk, &bank};
        bank.init(storage, mm);
        eng.init(mm, voices);
        register_synth_modules(eng.registry());
        register_sampler_modules(eng.registry());
    }
    ~SamplerRig() { eng.shutdown(); bank.shutdown(); }

    static uint64_t block_us() { return static_cast<uint64_t>(kBlock) * 1000000ull / static_cast<uint64_t>(kSampleRate); }
    NodeDesc *add(GraphDesc &g, int id, int type) { return g.add_node(eng.registry(), id, type); }

    void tick() { now_us += block_us(); bank.pump(now_us); }
    void run(int blocks, std::vector<double> *out = nullptr) {
        q15 l[kBlock], r[kBlock];
        for (int b = 0; b < blocks; b++) {
            tick();
            eng.render(l, r);
            if (out) for (int i = 0; i < kBlock; i++) out->push_back(l[i]);
        }
    }
    // like run(), and prints the sample index of every new underrun (debug aid for streaming tests)
    void run_trace(int blocks, std::vector<double> *out = nullptr) {
        q15 l[kBlock], r[kBlock];
        uint32_t last = bank.stats.underruns.load();
        for (int b = 0; b < blocks; b++) {
            tick();
            eng.render(l, r);
            if (out) for (int i = 0; i < kBlock; i++) out->push_back(l[i]);
            uint32_t u = bank.stats.underruns.load();
            if (u != last) { std::printf("      underrun #%u at block %d (t = %.0f ms)\n", u, b, 1000.0 * b * kBlock / kSampleRate); last = u; }
        }
    }
    bool wait_ready(int id, int max_blocks = 4000) {
        for (int b = 0; b < max_blocks && !bank.ready(id); b++) tick();
        return bank.ready(id);
    }
    int add_and_load(const char *name, const std::vector<uint8_t> &file, int head_ms = 150) {
        storage.add_file(name, file);
        int id = bank.load(name, head_ms);
        CHECK(id >= 0);
        CHECK(wait_ready(id));
        return id;
    }
    static int blocks_for(double seconds) { return static_cast<int>(seconds * kSampleRate / kBlock) + 1; }
};


// A card that is as capable, relative to the stream rate, as the numbers in the comments assume at 48 kHz.
StorageModel card(uint32_t latency_us, uint32_t bytes_per_s, uint32_t stall_every = 0, uint32_t stall_us = 0) {
    const double k = kSampleRate > 48000 ? static_cast<double>(kSampleRate) / 48000.0 : 1.0;   // faster cards for faster streams, never slower
    return StorageModel{static_cast<uint32_t>(latency_us / k), static_cast<uint32_t>(bytes_per_s * k), stall_every, stall_us};
}

std::vector<uint8_t> make_smp(const SmpHeader &h, const std::function<int(uint32_t)> &gen) {
    std::vector<int16_t> pcm(h.frames);
    for (uint32_t i = 0; i < h.frames; i++) pcm[i] = static_cast<int16_t>(gen(i));
    return smp_build(h, pcm.data());
}

SmpHeader header(uint32_t frames, int root = 69) {
    SmpHeader h;
    h.sample_rate = kSampleRate;
    h.frames = frames;
    h.root_note = static_cast<uint16_t>(root);
    return h;
}

std::vector<uint8_t> sine_smp(uint32_t frames, double hz, double amp, int root = 69, SmpHeader *out = nullptr) {
    SmpHeader h = header(frames, root);
    if (out) *out = h;
    return make_smp(h, [=](uint32_t i) { return static_cast<int>(std::lround(amp * std::sin(2 * kPi * hz * i / kSampleRate))); });
}

// NoteIn -> Sampler -> VoiceOut; returns the sampler's node id (2)
void sampler_graph(SamplerRig &rig, GraphDesc &g, const std::function<void(NodeDesc *)> &setup) {
    rig.add(g, 1, T_NOTE_IN);
    NodeDesc *s = rig.add(g, 2, T_SAMPLER);
    setup(s);
    rig.add(g, 3, T_VOICE_OUT)->param[VO_TAIL_MS] = 60000;
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
}

double snr_against(const std::vector<double> &out, const std::function<double(size_t)> &expect, size_t from, size_t to) {
    std::vector<double> a, b;
    for (size_t i = from; i < to && i < out.size(); i++) { a.push_back(expect(i)); b.push_back(out[i]); }
    return snr_db(a, b);
}
}  // namespace

TEST(smp_container_round_trip_and_alignment) {
    SmpHeader h = header(5000, 48);
    h.tune_cents = -17; h.loop_start = 100; h.loop_end = 4000; h.loop_mode = SMP_LOOP_PINGPONG;
    h.slice_count = 3; h.slice[0] = 0; h.slice[1] = 1200; h.slice[2] = 3333;
    std::vector<int16_t> pcm(5000);
    for (int i = 0; i < 5000; i++) pcm[static_cast<size_t>(i)] = static_cast<int16_t>(i - 2500);
    std::vector<uint8_t> f = smp_build(h, pcm.data());
    CHECK_EQ(f.size() % kSmpBlockBytes, 0);
    CHECK_EQ(f.size(), (h.blocks() + 1) * kSmpBlockBytes);
    SmpHeader r;
    CHECK(smp_parse_header(f.data(), kSmpBlockBytes, r));
    CHECK_EQ(r.frames, 5000);
    CHECK_EQ(r.root_note, 48);
    CHECK_EQ(r.tune_cents, -17);
    CHECK_EQ(r.loop_end, 4000);
    CHECK_EQ(r.loop_mode, SMP_LOOP_PINGPONG);
    CHECK_EQ(r.slice[2], 3333);
    CHECK(r.has_loop());
    // frame 3000 lives in block 1 (frames 2048..4095), 4 KB aligned
    const size_t off = kSmpBlockBytes * 2 + 2 * (3000 - kSmpBlockFrames);
    CHECK_EQ(static_cast<int16_t>(get16(&f[off])), 500);
    f[0] = 'X';
    CHECK(!smp_parse_header(f.data(), kSmpBlockBytes, r));
}

TEST(sim_storage_models_latency_bandwidth_and_stalls) {
    SimStorage st(StorageModel{10000, 1000000, 3, 100000});             // 10 ms + 1 MB/s, every 3rd request +100 ms
    st.add_file("f", std::vector<uint8_t>(65536, 7));
    uint8_t buf[3][4096];
    IoDone d{};
    for (uint32_t i = 0; i < 3; i++) CHECK(st.submit(IoRead{0, 0, 4096, buf[i], i}, 0));
    CHECK(!st.poll(d, 5000));                                            // still in flight
    CHECK(st.poll(d, 14096));                                            // 10 ms + 4.096 ms
    CHECK_EQ(d.tag, 0);
    CHECK(d.ok && buf[0][100] == 7);
    CHECK(!st.poll(d, 20000));
    CHECK(st.poll(d, 28192));                                            // serial: the second one finishes one slot later
    CHECK_EQ(d.tag, 1);
    CHECK(!st.poll(d, 100000));
    CHECK(st.poll(d, 28192 + 14096 + 100000));                           // the third one stalled
    CHECK_EQ(d.tag, 2);
    uint8_t bad[16];
    CHECK(st.submit(IoRead{0, 65530, 16, bad, 9}, 0));                   // past the end of the file
    CHECK(st.poll(d, 1000000000));
    CHECK(!d.ok);
}

TEST(short_sample_lives_in_ram_and_plays_at_the_right_pitch) {
    SamplerRig rig;
    int id = rig.add_and_load("sine", sine_smp(4800, 440.0, 16000.0));                 // 100 ms: shorter than the head
    const SampleSlot *sl = rig.bank.slot(id);
    CHECK(sl->resident);
    GraphDesc g;
    sampler_graph(rig, g, [&](NodeDesc *s) { s->param[SMPR_SAMPLE] = id; s->param[SMPR_INTERP] = 1; });
    CHECK(rig.eng.load(g) == Err::Ok);

    rig.eng.note_on(69);                                                               // root: original speed, exact samples
    std::vector<double> y;
    rig.run(SamplerRig::blocks_for(0.1), &y);
    double snr = snr_against(y, [](size_t i) { return 16000.0 * std::sin(2 * kPi * 440.0 * static_cast<double>(i) / kSampleRate); }, 0, 4400);
    std::printf("    root note: SNR %.1f dB against the source\n", snr);
    CHECK(snr > 70.0);
    rig.run(SamplerRig::blocks_for(0.2));
    CHECK_EQ(rig.bank.stats.underruns.load(), 0);

    // one octave up: twice the frequency, from the same data
    SamplerRig rig2;
    int id2 = rig2.add_and_load("sine", sine_smp(kSampleRate, 440.0, 16000.0), 0);          // resident, one second
    GraphDesc g2;
    sampler_graph(rig2, g2, [&](NodeDesc *s) { s->param[SMPR_SAMPLE] = id2; });
    CHECK(rig2.eng.load(g2) == Err::Ok);
    rig2.eng.note_on(81);
    std::vector<double> z;
    rig2.run(SamplerRig::blocks_for(0.45), &z);
    int cross = 0;
    for (size_t i = 1; i < z.size(); i++) if (z[i - 1] < 0 && z[i] >= 0) cross++;
    double f = cross * static_cast<double>(kSampleRate) / static_cast<double>(z.size());
    std::printf("    note 81 on a 440 Hz sample: %.1f Hz\n", f);
    CHECK_NEAR(f, 880.0, 12.0);
}

TEST(long_sample_streams_over_a_slow_card_without_underruns) {
    SamplerRig rig(card(15000, 2000000));                                // 15 ms latency, 2 MB/s
    int id = rig.add_and_load("long", sine_smp(3 * kSampleRate, 440.0, 16000.0));      // 3 s, 150 ms head
    CHECK(!rig.bank.slot(id)->resident);
    GraphDesc g;
    sampler_graph(rig, g, [&](NodeDesc *s) { s->param[SMPR_SAMPLE] = id; });
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.eng.note_on(69);
    std::vector<double> y;
    rig.run(SamplerRig::blocks_for(3.1), &y);
    CHECK(std::fabs(y[0]) < 1000.0);                                                   // first sample is sin(0): it started immediately
    double snr = snr_against(y, [](size_t i) { return 16000.0 * std::sin(2 * kPi * 440.0 * static_cast<double>(i) / kSampleRate); }, 0, static_cast<size_t>(2.95 * kSampleRate));
    std::printf("    3 s streamed: SNR %.1f dB, %u block reads, %u underruns, card read %.0f KB\n", snr, rig.bank.stats.block_reads.load(),
                rig.bank.stats.underruns.load(), static_cast<double>(rig.storage.total_bytes()) / 1024.0);
    CHECK(snr > 60.0);
    CHECK_EQ(rig.bank.stats.underruns.load(), 0);
    CHECK(rig.bank.stats.block_reads.load() + 16 >= static_cast<uint32_t>(3 * kSampleRate / static_cast<int>(kSmpBlockFrames)));   // all blocks past the heads were read
}

TEST(a_stalling_card_causes_a_counted_fade_not_a_click) {
    SamplerRig rig(card(5000, 4000000, 40, 400000));                           // every 40th read stalls for 400 ms (a card that garbage-collects)
    int id = rig.add_and_load("long", sine_smp(8 * kSampleRate, 440.0, 16000.0));
    GraphDesc g;
    sampler_graph(rig, g, [&](NodeDesc *s) { s->param[SMPR_SAMPLE] = id; });
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.eng.note_on(69);
    std::vector<double> y;
    rig.run(SamplerRig::blocks_for(7.5), &y);
    double nominal = 2 * kPi * 440.0 / kSampleRate * 16000.0, worst = 0;
    for (size_t i = 1; i < y.size(); i++) worst = std::fmax(worst, std::fabs(y[i] - y[i - 1]));
    std::printf("    stalls: %u underruns, largest sample step %.0f (plain sine %.0f)\n", rig.bank.stats.underruns.load(), worst, nominal);
    CHECK(rig.bank.stats.underruns.load() >= 1);
    CHECK(worst < 1600.0);                                                             // a click would be amplitude sized (16000); the crossfades keep steps near the signal's own slope
    // the timeline kept running, so whenever the data is there the waveform is exactly the right one: most of the
    // recording matches the source, and the parts that do not are the stalls
    int windows = 0, matched = 0;
    for (size_t at = 0; at + 400 < y.size(); at += 400) {
        double snr = snr_against(y, [](size_t i) { return 16000.0 * std::sin(2 * kPi * 440.0 * static_cast<double>(i) / kSampleRate); }, at, at + 400);
        windows++;
        if (snr > 40.0) matched++;
    }
    std::printf("    %d of %d windows match the source exactly\n", matched, windows);
    CHECK(matched > windows * 7 / 10);
    CHECK(matched < windows);
}

TEST(loops_ping_pong_and_reverse_follow_the_loader) {
    auto F = [](double seconds) { return static_cast<uint32_t>(seconds * kSampleRate); };
    const uint32_t N = F(0.6), ls = F(0.25), le = F(0.5);                              // 0.6 s: longer than the head, so the loop is streamed
    auto val = [](double frame) { return std::round(frame * 48000.0 / kSampleRate) - 14000.0; };   // the value tells the frame
    SmpHeader h = header(N);
    h.loop_start = ls; h.loop_end = le; h.loop_mode = SMP_LOOP_FWD;
    SamplerRig rig(card(8000, 3000000));
    int id = rig.add_and_load("ramp", make_smp(h, [&](uint32_t i) { return static_cast<int>(val(i)); }));
    GraphDesc g;
    sampler_graph(rig, g, [&](NodeDesc *s) { s->param[SMPR_SAMPLE] = id; s->param[SMPR_TRACK] = 0; s->param[SMPR_INTERP] = 0; });
    CHECK(rig.eng.load(g) == Err::Ok);

    // forward loop (file default): the value rises to the loop end, wraps to the loop start, and keeps cycling
    rig.eng.note_on(69);
    std::vector<double> y;
    rig.run(SamplerRig::blocks_for(1.2), &y);
    const size_t period = le - ls;
    CHECK_NEAR(y[le - 10], val(le - 10), 2);
    CHECK_NEAR(y[le + 10], val(ls + 10), 3);                                           // wrapped back to the loop start
    CHECK_NEAR(y[le + 10 + 2 * period], val(ls + 10), 3);                              // third lap
    CHECK_EQ(rig.bank.stats.underruns.load(), 0);

    // ping-pong: turns around at both ends
    rig.eng.set_param(2, SMPR_LOOP, SMP_LOOP_PINGPONG);
    rig.eng.all_notes_off();
    rig.run(2);
    rig.eng.note_on(69);
    std::vector<double> p;
    rig.run(SamplerRig::blocks_for(1.2), &p);
    double mx = -1e9, mn = 1e9;
    for (size_t i = le + 1000; i < p.size(); i++) { mx = std::fmax(mx, p[i]); mn = std::fmin(mn, p[i]); }
    std::printf("    ping-pong turns between %.0f and %.0f (loop values %.0f..%.0f)\n", mn, mx, val(ls), val(le));
    CHECK_NEAR(mx, val(le), 6);
    CHECK_NEAR(mn, val(ls), 6);
    CHECK_NEAR(p[le + 2000] - p[le + 1000], -1000.0 * 48000.0 / kSampleRate, 4);       // descending after the first turn
    CHECK_EQ(rig.bank.stats.underruns.load(), 0);

    // reverse from the end, no loop (the sample's tail is kept in RAM, so this starts at once)
    rig.eng.set_param(2, SMPR_LOOP, SMP_LOOP_OFF);
    rig.eng.set_param(2, SMPR_REVERSE, 1);
    rig.eng.all_notes_off();
    rig.run(2);
    rig.eng.note_on(69);
    std::vector<double> r;
    rig.run(SamplerRig::blocks_for(0.3), &r);
    CHECK_NEAR(r[1], val(N - 2), 4);
    CHECK_NEAR(r[5000] - r[0], -5000.0 * 48000.0 / kSampleRate, 6);
    CHECK_EQ(rig.bank.stats.underruns.load(), 0);
}

TEST(zones_pick_samples_by_note_and_velocity_and_voices_share_the_card) {
    SamplerRig rig(card(5000, 1500000), 8);
    auto level_smp = [&](uint32_t frames, int level) { return make_smp(header(frames), [=](uint32_t) { return level; }); };
    int a = rig.add_and_load("a", level_smp(2 * kSampleRate, 1000));
    int b = rig.add_and_load("b", level_smp(2 * kSampleRate, 2000));
    int c = rig.add_and_load("c", level_smp(2 * kSampleRate, 4000));
    int d = rig.add_and_load("d", level_smp(2 * kSampleRate, 8000));
    int inst = rig.bank.add_instrument();
    Zone z;
    z.lo_note = 60; z.hi_note = 60; z.lo_vel = 1; z.hi_vel = 63; z.sample = static_cast<int16_t>(a); CHECK(rig.bank.add_zone(inst, z));
    z.lo_vel = 64; z.hi_vel = 127; z.sample = static_cast<int16_t>(b); CHECK(rig.bank.add_zone(inst, z));
    z.lo_note = 62; z.hi_note = 62; z.lo_vel = 1; z.sample = static_cast<int16_t>(c); CHECK(rig.bank.add_zone(inst, z));
    z.lo_note = 64; z.hi_note = 127; z.sample = static_cast<int16_t>(d); z.gain = 16384; CHECK(rig.bank.add_zone(inst, z));   // half gain
    CHECK(rig.bank.pick(inst, 60, 10)->sample == a);
    CHECK(rig.bank.pick(inst, 60, 100)->sample == b);
    CHECK(rig.bank.pick(inst, 61, 100) == nullptr);

    GraphDesc g;
    sampler_graph(rig, g, [&](NodeDesc *s) { s->param[SMPR_INSTRUMENT] = inst; s->param[SMPR_TRACK] = 0; s->param[SMPR_INTERP] = 0; });
    CHECK(rig.eng.load(g) == Err::Ok);
    auto level_of = [&](int note, int vel) {
        rig.eng.all_notes_off();
        rig.run(2);
        rig.eng.note_on(note, vel);
        std::vector<double> y;
        rig.run(8, &y);
        return y.back();
    };
    CHECK_NEAR(level_of(60, 30), 1000, 2);
    CHECK_NEAR(level_of(60, 100), 2000, 2);
    CHECK_NEAR(level_of(62, 90), 4000, 2);
    CHECK_NEAR(level_of(70, 90), 4000, 3);                                              // 8000 x half gain
    CHECK_NEAR(level_of(61, 90), 0, 1);                                                 // unmapped: silent

    // four voices streaming different 2 s samples from a 1.5 MB/s card: 4 x 96 KB/s fits
    rig.eng.all_notes_off();
    rig.run(2);
    for (int n : {60, 62, 64, 66}) rig.eng.note_on(n, 100);
    std::vector<double> y;
    rig.run(SamplerRig::blocks_for(1.9), &y);
    std::printf("    4 voices: %u underruns, %u block reads\n", rig.bank.stats.underruns.load(), rig.bank.stats.block_reads.load());
    CHECK_EQ(rig.bank.stats.underruns.load(), 0);
    CHECK_NEAR(y.back(), 2000 + 4000 + 4000 + 4000, 12);                                // b + c + d/2 (x2: notes 64 and 66)

    // the same voices on a card that cannot keep up: the loader runs dry and the player reports it
    SamplerRig slow(card(20000, 120000), 8);
    int a2 = slow.add_and_load("a", level_smp(2 * kSampleRate, 1000), 40);
    int b2 = slow.add_and_load("b", level_smp(2 * kSampleRate, 2000), 40);
    int i2 = slow.bank.add_instrument();
    Zone q;
    q.lo_note = 0; q.hi_note = 63; q.sample = static_cast<int16_t>(a2); slow.bank.add_zone(i2, q);
    q.lo_note = 64; q.hi_note = 127; q.sample = static_cast<int16_t>(b2); slow.bank.add_zone(i2, q);
    GraphDesc g2;
    sampler_graph(slow, g2, [&](NodeDesc *s) { s->param[SMPR_INSTRUMENT] = i2; s->param[SMPR_TRACK] = 0; });
    CHECK(slow.eng.load(g2) == Err::Ok);
    for (int n : {60, 62, 64, 66}) slow.eng.note_on(n, 100);
    slow.run(SamplerRig::blocks_for(1.9));
    std::printf("    same voices on a 120 KB/s card: %u underruns\n", slow.bank.stats.underruns.load());
    CHECK(slow.bank.stats.underruns.load() > 0);
}

TEST(slices_start_instantly_inside_a_long_sample_and_stop_at_the_next_one) {
    const uint32_t seg = kSampleRate / 2;                                               // four half-second segments with different levels
    SmpHeader h = header(4 * seg);
    h.slice_count = 4; h.slice[0] = 0; h.slice[1] = seg; h.slice[2] = 2 * seg; h.slice[3] = 3 * seg;
    SamplerRig rig(card(40000, 1000000));                                 // 40 ms of latency per read
    int id = rig.add_and_load("loop", make_smp(h, [=](uint32_t i) { return 1000 * static_cast<int>(1 + i / seg); }), 150);
    CHECK(rig.bank.slot(id)->n_heads == 6);                                             // sample head, one head per slice, tail head
    GraphDesc g;
    sampler_graph(rig, g, [&](NodeDesc *s) { s->param[SMPR_SAMPLE] = id; s->param[SMPR_TRACK] = 0; s->param[SMPR_INTERP] = 0; });
    CHECK(rig.eng.load(g) == Err::Ok);
    for (int slice = 0; slice < 4; slice++) {
        rig.eng.set_param(2, SMPR_SLICE, slice);
        rig.eng.all_notes_off();
        rig.run_trace(SamplerRig::blocks_for(0.4));                                           // let the reads of the previous trigger finish: the card is serial
        rig.eng.note_on(69);
        std::vector<double> y;
        rig.run_trace(1, &y);                                                                 // the very first block, before any block could arrive from the card
        CHECK_NEAR(y[0], 1000.0 * (slice + 1), 2);
        CHECK_NEAR(y[kBlock - 1], 1000.0 * (slice + 1), 2);
    }
    // stop at the next slice boundary: slice 1 lasts one segment, then silence
    rig.eng.set_param(2, SMPR_SLICE, 1);
    rig.eng.set_param(2, SMPR_SLICE_MODE, 1);
    rig.eng.all_notes_off();
    rig.run_trace(SamplerRig::blocks_for(0.4));
    rig.eng.note_on(69);
    std::vector<double> y;
    rig.run_trace(SamplerRig::blocks_for(0.6), &y);
    CHECK_NEAR(y[seg - 10], 2000, 2);
    CHECK_NEAR(y[seg + 200], 0, 1);
    // a start offset (in 1/16 ms): 20 ms into slice 2 (still inside the slice's RAM head)
    rig.eng.set_param(2, SMPR_SLICE_MODE, 0);
    rig.eng.set_param(2, SMPR_SLICE, 2);
    rig.eng.set_param(2, SMPR_START, 20 * 16);
    rig.eng.all_notes_off();
    rig.run_trace(SamplerRig::blocks_for(0.7));
    rig.eng.note_on(69);
    std::vector<double> o;
    rig.run_trace(2, &o);
    CHECK_NEAR(o[0], 3000, 2);
    rig.run_trace(SamplerRig::blocks_for(0.5));
    CHECK_EQ(rig.bank.stats.underruns.load(), 0);
}

TEST(granular_clouds_hold_pitch_and_density) {
    SamplerRig rig;
    int id = rig.add_and_load("tone", sine_smp(kSampleRate, 440.0, 16000.0), 0);             // fully resident
    GraphDesc g;
    NodeDesc *n = rig.add(g, 1, T_GRANULAR_G);
    n->param[GRN_SAMPLE] = id; n->param[GRN_SPEED] = 0; n->param[GRN_POSITION] = 8000; n->param[GRN_DENSITY] = 60;
    n->param[GRN_SIZE] = 60; n->param[GRN_JITTER] = 0;
    rig.add(g, 2, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.run(SamplerRig::blocks_for(0.3));
    std::vector<double> y;
    rig.run(SamplerRig::blocks_for(0.5), &y);
    auto peak_hz = [](std::vector<double> v) {
        v.resize(4096);
        std::vector<double> w(v.size());
        for (size_t i = 0; i < v.size(); i++) w[i] = v[i] * (0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / 4096.0));
        std::vector<double> p = dft_power(w);
        size_t best = 1;
        for (size_t k = 2; k + 1 < p.size(); k++) if (p[k] > p[best]) best = k;
        const double a = std::log(p[best - 1] + 1e-9), b = std::log(p[best] + 1e-9), c = std::log(p[best + 1] + 1e-9);
        const double delta = 0.5 * (a - c) / (a - 2 * b + c);                              // parabolic interpolation of the peak
        return (static_cast<double>(best) + delta) * kSampleRate / 4096.0;
    };
    double f = peak_hz(y);
    double r = rms(y);
    std::printf("    frozen cloud of a 440 Hz tone: %.1f Hz, rms %.0f\n", f, r);
    CHECK_NEAR(f, 440.0, 15.0);
    CHECK(r > 800.0 && r < 16000.0);

    rig.eng.set_param(1, GRN_PITCH, 12 * 256);                                          // an octave up
    rig.run(SamplerRig::blocks_for(0.2));
    std::vector<double> z;
    rig.run(SamplerRig::blocks_for(0.5), &z);
    f = peak_hz(z);
    std::printf("    pitch +12 st: %.1f Hz\n", f);
    CHECK_NEAR(f, 880.0, 25.0);

    // moving through the sample (speed 1x) and a sparse cloud: still bounded, no runaway
    rig.eng.set_param(1, GRN_SPEED, 256);
    rig.eng.set_param(1, GRN_DENSITY, 5);
    rig.eng.set_param(1, GRN_JITTER, 20000);
    std::vector<double> w;
    rig.run(SamplerRig::blocks_for(1.0), &w);
    double pk = 0;
    for (double v : w) pk = std::fmax(pk, std::fabs(v));
    CHECK(pk < 32767.0);
    CHECK(rms(w) > 100.0);
}

TEST(sampler_releases_every_byte) {
    SamplerRig rig(StorageModel{1000, 5000000, 0, 0});
    int id = rig.add_and_load("long", sine_smp(2 * kSampleRate, 440.0, 8000.0));
    GraphDesc g;
    sampler_graph(rig, g, [&](NodeDesc *s) { s->param[SMPR_SAMPLE] = id; });
    CHECK(rig.eng.load(g) == Err::Ok);
    rig.eng.note_on(69);
    rig.run(SamplerRig::blocks_for(0.5));
    CHECK(rig.bulk.used() > 100 * 1024);                                                // heads + rings live in the bulk heap
    rig.eng.shutdown();
    rig.bank.shutdown();
    CHECK_EQ(rig.heap.used(), 0);
    CHECK_EQ(rig.bulk.used(), 0);
    CHECK(rig.heap.check() && rig.bulk.check());
}
