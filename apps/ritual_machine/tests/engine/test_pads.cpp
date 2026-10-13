// The Pads section's player (ADR-041 decisions 14-26): pads.h settings -> engine_synth_pads_build / pad_set -> the Pads module -> 8 Sampler
// voices -> the simulated card. One-shot / gate / toggle, mono restart, choke, Stop all, trim, loop, tune, mute, pan, the filter.
#include <algorithm>
#include <vector>
#include "rig.h"
#include "host/sim_storage.h"
#include "engine/sampler/smp_format.h"
#include "engine/modules/sampler_modules.h"
#include "platform/engine/engine_synth.h"
#include "platform/engine/sample_catalog.h"

extern "C" {
#include "core/pad_seq.h"
#include "core/pads.h"
#include "core/rack.h"
#include "core/synth_config.h"
#include "core/synth_params.h"
}

using namespace tst;

// core/pads.c reads and writes pads.cfg through the storage HAL: no card in the engine tests.
extern "C" int storage_read(const char *, char *, int) { return -1; }
extern "C" bool storage_write(const char *, const char *, int) { return false; }

namespace {
const double kTwoPi = 6.28318530717958647692;

std::vector<int16_t> sine(double hz, double seconds, double amp = 0.5) {
    std::vector<int16_t> x(static_cast<size_t>(seconds * kSampleRate));
    for (size_t i = 0; i < x.size(); i++) x[i] = static_cast<int16_t>(amp * 32767.0 * std::sin(kTwoPi * hz * static_cast<double>(i) / kSampleRate));
    return x;
}

struct Stereo { std::vector<double> l, r; };

struct PadsApp {
    std::vector<uint8_t> fast, bulk;
    SimStorage card{StorageModel{500, 4000000, 0, 0}};
    audio_sample_info_t cat[8];
    int n = 0;
    pads_t pads;
    rack_t rack;
    PadsApp() : fast(6u << 20), bulk(6u << 20) {
        CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
        CHECK(engine_synth_attach_storage(&card));
        pads_init(&pads);
        rack_clear(&rack);
        rack.cfg.volume = 1.0f;
        rack.cfg.mono = 0;                                                    // GENERAL Out = Stereo (the default Mono would hide the pan)
    }
    ~PadsApp() { engine_synth_shutdown(); }

    void add(const char *name, const std::vector<int16_t> &pcm, uint32_t loop_start = 0, uint32_t loop_end = 0) {
        SmpHeader h;
        h.sample_rate = static_cast<uint32_t>(kSampleRate);
        h.frames = static_cast<uint32_t>(pcm.size());
        h.root_note = 60;
        h.loop_start = loop_start; h.loop_end = loop_end;
        h.loop_mode = loop_end > loop_start + 1 ? SMP_LOOP_FWD : SMP_LOOP_OFF;
        card.add_file((std::string(name) + ".smp").c_str(), smp_build(h, pcm.data()));
        audio_sample_info_t &in = cat[n];
        std::memset(&in, 0, sizeof in);
        std::snprintf(in.name, sizeof in.name, "%s", name);
        in.frames = h.frames; in.rate = h.sample_rate; in.root = 60;
        in.loop_mode = h.loop_mode; in.loop_start = loop_start; in.loop_end = loop_end;
        engine_synth_catalog_set(cat, ++n);
    }
    pad_t &pad(int i, const char *file) { std::snprintf(pads.pad[i].file, PAD_NAME_MAX, "%s", file); return pads.pad[i]; }
    void build() { engine_synth_pads_build(&pads, &rack); run(0.1); }    // the heads arrive through the loader

    long frames = 0;                                                          // rendered so far (whole engine blocks: the clock tests count on it)
    Stereo run(double seconds) {
        const int frames = (static_cast<int>(seconds * engine_synth_sample_rate()) + kBlock - 1) / kBlock * kBlock;
        this->frames += frames;
        std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
        for (int done = 0; done < frames;) {
            const int k = std::min(frames - done, 64);
            engine_synth_render(&buf[static_cast<size_t>(done) * 2], k);
            engine_synth_io_pump();
            done += k;
        }
        Stereo s;
        s.l.resize(static_cast<size_t>(frames)); s.r.resize(static_cast<size_t>(frames));
        for (int i = 0; i < frames; i++) { s.l[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2]; s.r[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2 + 1]; }
        return s;
    }
};

double power_at(const std::vector<double> &x, double hz) {
    double re = 0, im = 0;
    for (size_t i = 0; i < x.size(); i++) { const double ph = kTwoPi * hz * static_cast<double>(i) / kSampleRate; re += x[i] * std::cos(ph); im += x[i] * std::sin(ph); }
    const double n = static_cast<double>(x.size());
    return (re * re + im * im) / (n * n);
}

std::vector<double> tail_of(const std::vector<double> &x, double seconds) {
    const size_t k = std::min(x.size(), static_cast<size_t>(seconds * kSampleRate));
    return std::vector<double>(x.end() - static_cast<long>(k), x.end());
}
}  // namespace

TEST(pads_one_shot_plays_its_sample_to_the_end) {
    PadsApp a;
    a.add("tone", sine(440.0, 0.3));
    a.pad(0, "tone").level = 100;
    a.build();
    CHECK(rms(a.run(0.1).l) < 1.0);                                          // nothing before the push
    engine_synth_pad_push(0);
    const Stereo s = a.run(0.15);
    std::printf("    one-shot: rms %.0f, playing mask %llx\n", rms(s.l), static_cast<unsigned long long>(engine_synth_pads_playing()));
    CHECK(rms(tail_of(s.l, 0.1)) > 5000.0);
    CHECK(engine_synth_pads_playing() == 1u);
    engine_synth_pad_release(0);                                              // a one-shot ignores the release
    CHECK(rms(a.run(0.1).l) > 5000.0);
    CHECK(rms(tail_of(a.run(0.3).l, 0.1)) < 1.0);                             // past the end of the 0.3 s sample
    CHECK(engine_synth_pads_playing() == 0u);
}

TEST(pads_gate_stops_on_release_and_toggle_on_the_next_push) {
    PadsApp a;
    a.add("long", sine(330.0, 2.0));
    pad_t &g = a.pad(0, "long");
    g.mode = PAD_GATE; g.tail_ms = 20;
    pad_t &t = a.pad(1, "long");
    t.mode = PAD_TOGGLE; t.tail_ms = 20;
    a.build();
    engine_synth_pad_push(0);
    CHECK(rms(tail_of(a.run(0.2).l, 0.1)) > 3000.0);
    engine_synth_pad_release(0);
    CHECK(rms(tail_of(a.run(0.1).l, 0.05)) < 1.0);                            // faded within its 20 ms tail
    engine_synth_pad_push(1);
    engine_synth_pad_release(1);                                              // a toggle ignores the release ...
    CHECK(rms(tail_of(a.run(0.2).l, 0.1)) > 3000.0);
    CHECK(engine_synth_pads_playing() == 2u);
    engine_synth_pad_push(1);                                                 // ... and stops on the next push
    CHECK(rms(tail_of(a.run(0.1).l, 0.05)) < 1.0);
    CHECK(engine_synth_pads_playing() == 0u);
}

TEST(pads_restart_is_mono_and_choke_stops_the_group) {
    PadsApp a;
    a.add("long", sine(330.0, 2.0));
    for (int i = 0; i < 3; i++) a.pad(i, "long").choke = i < 2 ? 1 : 0;      // pads 0 and 1 share a choke group, pad 2 has none
    a.build();
    engine_synth_pad_push(0);
    a.run(0.05);
    engine_synth_pad_push(0);                                                 // the same pad again: a restart, still one sound
    a.run(0.05);
    CHECK(engine_synth_pads_playing() == 1u);
    engine_synth_pad_push(2);
    a.run(0.05);
    CHECK(engine_synth_pads_playing() == 5u);
    engine_synth_pad_push(1);                                                 // chokes pad 0, not pad 2
    a.run(0.05);
    CHECK(engine_synth_pads_playing() == 6u);
    engine_synth_pads_stop();
    CHECK(rms(tail_of(a.run(0.2).l, 0.05)) < 1.0);
    CHECK(engine_synth_pads_playing() == 0u);
}

TEST(pads_more_pushes_than_voices_steal_the_oldest) {
    PadsApp a;
    a.add("long", sine(330.0, 2.0));
    for (int i = 0; i < kPadVoices + 2; i++) a.pad(i, "long");
    a.build();
    for (int i = 0; i < kPadVoices + 2; i++) { engine_synth_pad_push(i); a.run(0.01); }
    const uint64_t m = engine_synth_pads_playing();
    int count = 0;
    for (int i = 0; i < 64; i++) count += (m >> i) & 1;
    std::printf("    %d pushes on %d voices: mask %llx\n", kPadVoices + 2, kPadVoices, static_cast<unsigned long long>(m));
    CHECK_EQ(count, kPadVoices);
    CHECK((m & 3u) == 0 && (m >> (kPadVoices + 1) & 1));                     // the two oldest went, the newest plays
}

TEST(pads_trim_loop_and_tune) {
    PadsApp a;
    a.add("tone", sine(261.6256, 1.0));
    pad_t &p = a.pad(0, "tone");
    p.end = static_cast<uint32_t>(kSampleRate / 2);                           // the first half (frames)
    a.build();
    engine_synth_pad_push(0);
    const Stereo s = a.run(0.7);
    CHECK(rms(std::vector<double>(s.l.begin() + static_cast<long>(0.3 * kSampleRate), s.l.begin() + static_cast<long>(0.45 * kSampleRate))) > 3000.0);
    CHECK(rms(tail_of(s.l, 0.15)) < 1.0);                                     // stopped at the end point
    p.loop = PAD_LOOP_TRIM;
    p.tune = 12;
    engine_synth_pad_set(0, &p);
    engine_synth_pad_push(0);
    const Stereo t = a.run(1.5);                                              // an octave up: the half lasts 0.25 s, looped
    const std::vector<double> late = tail_of(t.l, 0.3);
    std::printf("    loop + tune +12: rms late %.0f, power 523 Hz %.2e vs 261 Hz %.2e\n", rms(late), power_at(late, 523.2511), power_at(late, 261.6256));
    CHECK(rms(late) > 3000.0);
    CHECK(power_at(late, 523.2511) > 100.0 * power_at(late, 261.6256));
}

TEST(pads_mute_pan_and_filter) {
    PadsApp a;
    a.add("high", sine(4000.0, 2.0));
    pad_t &p = a.pad(0, "high");
    p.pan = -50;                                                              // hard left
    p.loop = PAD_LOOP_TRIM;
    a.build();
    engine_synth_pad_push(0);
    Stereo s = a.run(0.2);
    const double open = rms(tail_of(s.l, 0.1));
    CHECK(open > 3000.0);
    CHECK(rms(tail_of(s.r, 0.1)) < 1.0);
    p.mute = 1;
    engine_synth_pad_set(0, &p);
    CHECK(rms(tail_of(a.run(0.1).l, 0.05)) < 1.0);
    CHECK(engine_synth_pads_playing() == 1u);                                 // muted, still running
    p.mute = 0;
    p.cut = 30;                                                               // about 160 Hz: the 4 kHz tone is far above it
    engine_synth_pad_set(0, &p);
    const double shut = rms(tail_of(a.run(0.2).l, 0.1));
    std::printf("    4 kHz through the low-pass at %.0f Hz: rms %.0f (open %.0f)\n", pad_cut_hz(&p), shut, open);
    CHECK(shut < open / 100.0);
}

TEST(pads_a_streamed_sample_plays_through_and_the_synth_comes_back) {
    PadsApp a;
    a.add("big", sine(220.0, 5.0));                                           // 5 s: bigger than a resident pad sample at every rate, streamed
    a.pad(0, "big");
    a.build();
    engine_synth_pad_push(0);
    const Stereo s = a.run(2.5);
    uint32_t underruns = 0, reads = 0;
    engine_synth_sampler_stats(&underruns, &reads);
    std::printf("    streamed pad: rms over 0.5..2.5 s %.0f, %u block reads, %u underruns\n", rms(std::vector<double>(s.l.begin() + static_cast<long>(0.5 * kSampleRate), s.l.end())), reads, underruns);
    CHECK(rms(std::vector<double>(s.l.begin() + static_cast<long>(0.5 * kSampleRate), s.l.end())) > 3000.0);
    CHECK(reads > 0);
    CHECK_EQ(underruns, 0);
    const unsigned builds = engine_synth_build_count();
    synth_params_t params;
    synth_params_default(&params);
    a.rack.cfg.volume = 0.5f;
    engine_synth_set_params(&a.rack, &params);                                // in the Pads section only the volume follows: no rebuild
    CHECK_EQ(engine_synth_build_count(), builds);
    rack_init(&a.rack);
    engine_synth_build(&a.rack, &params);                                     // back to the synth
    CHECK(engine_synth_pads_playing() == 0u);
    engine_synth_note_on(60);
    CHECK(rms(tail_of(a.run(0.3).l, 0.1)) > 100.0);
}

TEST(pads_file_loop_uses_the_files_loop_points) {
    PadsApp a;
    std::vector<int16_t> pcm = sine(261.6256, 1.0);
    const uint32_t ls = static_cast<uint32_t>(0.5 * kSampleRate);
    for (size_t i = ls; i < pcm.size(); i++) pcm[i] = static_cast<int16_t>(pcm[i] / 4);       // a quieter second half: the loop's part
    a.add("held", pcm, ls, static_cast<uint32_t>(0.8 * kSampleRate));
    pad_t &p = a.pad(0, "held");
    p.loop = PAD_LOOP_FILE;
    pad_t &q = a.pad(1, "held");                                                // the same file, Loop Off
    a.build();
    engine_synth_pad_push(0);
    const Stereo s = a.run(2.0);
    const double first = rms(std::vector<double>(s.l.begin() + static_cast<long>(0.1 * kSampleRate), s.l.begin() + static_cast<long>(0.4 * kSampleRate)));
    const double late = rms(tail_of(s.l, 0.5));
    std::printf("    file loop 0.5..0.8 s: rms before it %.0f, at 1.5..2 s %.0f\n", first, late);
    CHECK(late > 1000.0 && late < first / 2.0);                                 // still sounding after the file's end, inside the quiet loop
    engine_synth_pads_stop();
    a.run(0.2);
    (void)q;
    engine_synth_pad_push(1);
    CHECK(rms(tail_of(a.run(1.3).l, 0.2)) < 1.0);                              // Off: plays to the end
}

namespace {
// The first sample (from `from`) whose level passes `thr`, -1 when none.
long onset(const std::vector<double> &x, size_t from, double thr = 300.0) {
    for (size_t i = from; i < x.size(); i++) if (std::fabs(x[i]) > thr) return static_cast<long>(i);
    return -1;
}
}  // namespace

TEST(pads_clock_runs_in_beats_and_restarts_at_bar_one) {
    PadsApp a;
    a.build();
    engine_synth_set_clock(120, 1);                                    // 120 BPM: a beat is 0.5 s
    a.run(1.25);
    int bar = 0, beat = 0, frac = 0;
    CHECK(engine_synth_transport(&bar, &beat, &frac));
    std::printf("    120 BPM after 1.25 s: bar %d beat %d frac %d\n", bar, beat, frac);
    CHECK(bar == 1 && beat == 3 && frac > 100 && frac < 156);
    a.run(1.0);                                                               // 2.25 s: bar 2, beat 1
    engine_synth_transport(&bar, &beat, &frac);
    CHECK(bar == 2 && beat == 1);
    engine_synth_set_clock(120, 0);
    a.run(0.1);
    CHECK(!engine_synth_transport(&bar, &beat, &frac));
    engine_synth_set_clock(120, 1);                                    // a start is bar 1 beat 1 again
    a.run(0.1);
    engine_synth_transport(&bar, &beat, &frac);
    CHECK(bar == 1 && beat == 1);
}

TEST(pads_start_on_grid_waits_for_the_beat_or_the_bar) {
    PadsApp a;
    a.add("tone", sine(440.0, 3.0));
    a.add("low", sine(97.0, 3.0));                                            // the bar pad at another pitch (two equal tones could cancel)
    a.pad(0, "tone").grid = PAD_GRID_BEAT;
    a.pad(1, "low").grid = PAD_GRID_BAR;
    pad_t &g = a.pad(2, "tone");
    g.grid = PAD_GRID_BEAT; g.mode = PAD_GATE;
    a.build();
    engine_synth_pad_push(0);                                                 // the clock is stopped: at once
    const Stereo s0 = a.run(0.05);
    CHECK(onset(s0.l, 0) >= 0 && onset(s0.l, 0) < kBlock + 64);
    engine_synth_pads_stop();
    a.run(0.1);
    engine_synth_set_clock(120, 1);
    const long t0 = a.frames;                                                 // beat 0: the next block (runs are whole blocks)
    a.run(0.1);
    engine_synth_pad_push(0);                                                 // pushed 0.1 s in: waits for the beat at 0.5 s
    engine_synth_pad_push(1);                                                 // ... and the bar at 2.0 s
    const long from = a.frames;
    const Stereo s = a.run(2.2);
    const long spb = static_cast<long>(kSampleRate / 2);                     // samples per beat at 120 BPM
    const long beat_at = t0 + spb - from, bar_at = t0 + 4 * spb - from;
    const long on = onset(s.l, 0);
    std::printf("    beat pad: onset %ld samples from the beat (block %d)\n", on - beat_at, kBlock);
    CHECK(on >= beat_at - kBlock / 2 - 1 && on <= beat_at + kBlock / 2 + 2);
    CHECK(engine_synth_pads_playing() == 3u);
    const long w = static_cast<long>(0.1 * kSampleRate);
    const std::vector<double> pre(s.l.begin() + bar_at - w, s.l.begin() + bar_at - kBlock), post(s.l.begin() + bar_at + kBlock, s.l.begin() + bar_at + w);
    std::printf("    bar pad: power at 97 Hz before the bar %.2e, after %.2e\n", power_at(pre, 97.0), power_at(post, 97.0));
    CHECK(power_at(post, 97.0) > 100.0 * power_at(pre, 97.0));
    engine_synth_pads_stop();
    a.run(0.3);                                                               // 2.6 s on the clock: the next beat is at 3.0 s
    engine_synth_pad_push(2);                                                 // a gate pad released before its beat never starts
    a.run(0.05);
    engine_synth_pad_release(2);
    CHECK(rms(a.run(1.0).l) < 1.0);
}

TEST(pads_metronome_clicks_on_the_beats) {
    PadsApp a;
    a.build();
    engine_synth_set_metronome(1);
    engine_synth_set_clock(120, 1);
    const Stereo s = a.run(1.2);
    const long half = static_cast<long>(0.5 * kSampleRate), ms20 = static_cast<long>(0.02 * kSampleRate);
    auto win = [&](long from, long len) { return rms(std::vector<double>(s.l.begin() + from, s.l.begin() + from + len)); };
    const double c0 = win(0, ms20), c1 = win(half, ms20), mid = win(half / 2, ms20);
    std::printf("    click rms beat 1 %.0f, beat 2 %.0f, between %.0f\n", c0, c1, mid);
    CHECK(c0 > 500.0 && c1 > 500.0 && mid < 1.0);
    engine_synth_set_metronome(0);
    const Stereo q = a.run(1.0);
    CHECK(rms(q.l) < 1.0);
}

TEST(pads_loop_start_crossfade_and_launch_from_the_loop) {
    PadsApp a;
    // two halves: 300 Hz then 500 Hz; the Trim loop is the second half (its start is the loop start, apart from the sample start)
    std::vector<int16_t> pcm = sine(300.0, 1.0);
    const std::vector<int16_t> hi = sine(500.0, 1.0);
    pcm.insert(pcm.end(), hi.begin(), hi.end());
    a.add("two", pcm);
    const uint32_t half = static_cast<uint32_t>(kSampleRate);
    pad_t &p = a.pad(0, "two");
    p.loop = PAD_LOOP_TRIM; p.lstart = half;                                  // plays 300 Hz once, then loops the 500 Hz half
    a.build();
    engine_synth_pad_push(0);
    const Stereo s = a.run(3.5);
    const std::vector<double> first(s.l.begin(), s.l.begin() + static_cast<long>(0.9 * kSampleRate)), late = tail_of(s.l, 0.5);
    CHECK(power_at(first, 300.0) > 100.0 * power_at(first, 500.0));
    CHECK(power_at(late, 500.0) > 100.0 * power_at(late, 300.0));              // after 3 s: inside the loop, never back to 300 Hz
    // the crossfade: the wrap of a 500 Hz loop that does not end on a whole period clicks; with a 20 ms crossfade the step shrinks
    engine_synth_pads_stop();
    a.run(0.2);
    auto worst_step = [](const std::vector<double> &x) { double w = 0; for (size_t i = 1; i < x.size(); i++) w = std::max(w, std::fabs(x[i] - x[i - 1])); return w; };
    p.end = half + half / 2 + 37;                                             // the loop is 0.5 s + 37 frames: a broken wave at the wrap
    engine_synth_pad_set(0, &p);
    engine_synth_pad_push(0);
    const double hard = worst_step(tail_of(a.run(3.0).l, 1.8));
    engine_synth_pads_stop();
    a.run(0.2);
    p.xfade_ms = 20;
    for (int post = 0; post < 2; post++) {
        p.xpost = static_cast<uint8_t>(post);
        engine_synth_pad_set(0, &p);
        engine_synth_pad_push(0);
        const std::vector<double> t = tail_of(a.run(3.0).l, 1.8);
        std::printf("    loop wrap: largest step %.0f plain, %.0f with a 20 ms %s crossfade (rms %.0f)\n", hard, worst_step(t), post ? "Post" : "Pre", rms(t));
        CHECK(worst_step(t) < hard * 0.6);
        CHECK(rms(t) > 3000.0);
        engine_synth_pads_stop();
        a.run(0.2);
    }
    // a grid launch from the loop start: the first sound is already 500 Hz
    p.xfade_ms = 0; p.grid = PAD_GRID_BEAT; p.from = 1;
    engine_synth_pad_set(0, &p);
    engine_synth_set_clock(120, 1);
    a.run(0.1);
    engine_synth_pad_push(0);
    const Stereo g = a.run(0.8);
    const long on = onset(g.l, 0);
    CHECK(on > 0);
    const std::vector<double> head(g.l.begin() + on, g.l.begin() + on + static_cast<long>(0.2 * kSampleRate));
    std::printf("    grid launch from the loop start: 500 Hz %.2e, 300 Hz %.2e\n", power_at(head, 500.0), power_at(head, 300.0));
    CHECK(power_at(head, 500.0) > 100.0 * power_at(head, 300.0));
}


TEST(pads_next_starts_the_neighbour_when_a_pad_ends_or_is_stopped) {
    PadsApp a;
    a.add("short", sine(440.0, 0.3));
    a.add("long", sine(330.0, 3.0));
    pad_t &p0 = a.pad(0, "short");
    p0.next = PAD_NEXT_RIGHT;                                                 // A1 -> A2 when it ends
    pad_t &p1 = a.pad(1, "long");
    p1.mode = PAD_TOGGLE; p1.next = PAD_NEXT_DOWN;                           // A2 -> A6 when it is toggled off
    a.pad(5, "short");
    pad_t &p3 = a.pad(3, "short");
    p3.next = PAD_NEXT_RIGHT;                                                 // A4 is on the right edge: no neighbour
    a.build();
    engine_synth_pad_push(0);
    a.run(0.2);
    CHECK(engine_synth_pads_playing() == 1u);
    a.run(0.2);                                                               // A1 ended at 0.3 s: A2 plays
    CHECK(engine_synth_pads_playing() == 2u);
    CHECK(engine_synth_pad_position(1) > 0);
    engine_synth_pad_push(1);                                                 // toggled off by hand: A6 starts
    a.run(0.1);
    CHECK((engine_synth_pads_playing() & 0x20u) != 0);
    a.run(0.5);
    engine_synth_pad_push(1);                                                 // A2 again, then Stop all: no Next
    a.run(0.1);
    engine_synth_pads_stop();
    a.run(0.2);
    CHECK(engine_synth_pads_playing() == 0u);
    engine_synth_pad_push(3);
    a.run(0.5);
    CHECK(engine_synth_pads_playing() == 0u);
}

TEST(pads_onset_scan_finds_the_attacks_of_a_drum_loop) {
    PadsApp a;
    // four hits (decaying noise bursts) at 0.1, 0.35, 0.6 and 0.85 s over silence; the third one faint
    std::vector<int16_t> pcm(static_cast<size_t>(kSampleRate), 0);
    uint32_t seed = 1;
    const double at[4] = {0.1, 0.35, 0.6, 0.85}, amp[4] = {0.8, 0.8, 0.05, 0.8};
    for (int h = 0; h < 4; h++)
        for (int i = 0; i < static_cast<int>(0.15 * kSampleRate); i++) {
            seed = seed * 1664525u + 1013904223u;
            const double noise = static_cast<double>(static_cast<int32_t>(seed) >> 16) / 32768.0;
            pcm[static_cast<size_t>(at[h] * kSampleRate) + static_cast<size_t>(i)] = static_cast<int16_t>(32767.0 * amp[h] * noise * std::exp(-i / (0.03 * kSampleRate)));
        }
    a.add("drums", pcm);
    a.build();
    uint32_t f[64];
    uint16_t s[64];
    int n = -1;
    for (int k = 0; k < 200 && n < 0; k++) { n = engine_synth_sample_onsets(0, f, s, 64); if (n < 0) a.run(0.01); }
    std::printf("    onsets:");
    for (int i = 0; i < n; i++) std::printf(" %.3f s (%d)", static_cast<double>(f[i]) / kSampleRate, s[i]);
    std::printf("\n");
    CHECK(n >= 4);
    int found = 0;
    for (int h = 0; h < 4; h++)
        for (int i = 0; i < n; i++) if (std::fabs(static_cast<double>(f[i]) / kSampleRate - at[h]) < 0.012) { found++; break; }
    CHECK_EQ(found, 4);
}

TEST(pads_sync_lands_the_loop_start_on_the_grid) {
    PadsApp a;
    std::vector<int16_t> pcm = sine(300.0, 0.5);                              // a 0.5 s pre-roll at 300 Hz ...
    const std::vector<int16_t> body = sine(500.0, 1.5);                       // ... then the loop at 500 Hz
    pcm.insert(pcm.end(), body.begin(), body.end());
    a.add("swell", pcm);
    pad_t &p = a.pad(0, "swell");
    p.loop = PAD_LOOP_TRIM; p.lstart = static_cast<uint32_t>(kSampleRate / 2); p.sync = 1; p.grid = PAD_GRID_BAR;
    a.build();
    engine_synth_set_clock(120, 1);                                    // a bar = 2 s
    const long t0 = a.frames;
    a.run(0.1);
    engine_synth_pad_push(0);                                                 // 0.1 s in: the loop start can make the bar at 2.0 s
    const long from = a.frames;
    const Stereo s = a.run(2.4);
    const long bar = t0 + static_cast<long>(2 * kSampleRate) - from, pre = bar - static_cast<long>(kSampleRate / 2);
    const long on = onset(s.l, 0);
    auto win = [&](long x0, long x1) { return std::vector<double>(s.l.begin() + x0, s.l.begin() + x1); };
    const std::vector<double> roll = win(pre + 1000, bar - 1000), loop = win(bar + 1000, bar + static_cast<long>(0.3 * kSampleRate));
    std::printf("    sync: pre-roll starts %ld samples from 1.5 s; 300 Hz %.1e / 500 Hz %.1e before the bar, 500 Hz %.1e / 300 Hz %.1e after\n",
                on - pre, power_at(roll, 300.0), power_at(roll, 500.0), power_at(loop, 500.0), power_at(loop, 300.0));
    CHECK(on >= pre - kBlock / 2 - 1 && on <= pre + kBlock / 2 + 2);
    CHECK(power_at(roll, 300.0) > 100.0 * power_at(roll, 500.0));
    CHECK(power_at(loop, 500.0) > 100.0 * power_at(loop, 300.0));
}

TEST(pads_switching_sections_frees_the_old_graph) {
    PadsApp a;
    a.add("tone", sine(440.0, 1.0));
    a.pad(0, "tone");
    synth_params_t params;
    synth_params_default(&params);
    rack_init(&a.rack);
    engine_synth_build(&a.rack, &params);
    a.run(0.1);
    engine_synth_housekeeping();
    size_t fast0 = 0, bulk0 = 0;
    engine_synth_heap_stats(&fast0, nullptr, nullptr, &bulk0, nullptr);
    size_t bulk_r1 = 0;
    for (int round = 0; round < 3; round++) {
        engine_synth_pads_build(&a.pads, &a.rack);
        a.run(0.1);
        engine_synth_housekeeping();
        engine_synth_build(&a.rack, &params);
        a.run(0.1);
        engine_synth_housekeeping();
        if (!round) engine_synth_heap_stats(nullptr, nullptr, nullptr, &bulk_r1, nullptr);   // (the pad's sample is loaded now, and stays)
    }
    size_t fast1 = 0, bulk1 = 0;
    engine_synth_heap_stats(&fast1, nullptr, nullptr, &bulk1, nullptr);
    std::printf("    synth -> pads -> synth x3: fast heap %zu -> %zu, bulk %zu -> %zu bytes\n", fast0, fast1, bulk0, bulk1);
    CHECK(fast1 <= fast0 + 256);
    CHECK_EQ(bulk1, bulk_r1);                                                 // more switches: nothing more
}

/* ---------------- the FX chains (ADR-041 decisions 23, 47-49): a pad's bus, the chains, the four routings ---------------- */

namespace {
std::vector<double> part(const std::vector<double> &x, double from_s, double to_s) {
    const size_t a = std::min(x.size(), static_cast<size_t>(from_s * kSampleRate)), b = std::min(x.size(), static_cast<size_t>(to_s * kSampleRate));
    return std::vector<double>(x.begin() + static_cast<long>(a), x.begin() + static_cast<long>(b));
}
// A looping 3 kHz tone on pad `i`, sent to `bus`.
void tone_pad(PadsApp &a, int i, int bus) {
    pad_t &p = a.pad(i, "hi");
    p.level = 100; p.loop = PAD_LOOP_TRIM; p.mode = PAD_TOGGLE; p.bus = static_cast<uint8_t>(bus);
}
double level_3k(PadsApp &a) { a.run(0.25); return std::sqrt(power_at(a.run(0.2).l, 3000.0)); }
}  // namespace

TEST(pads_fx_a_pad_sent_to_a_chain_goes_through_its_effect) {
    PadsApp a;
    a.add("tone", sine(440.0, 0.3));
    a.pad(0, "tone").level = 100;                                            // Dry
    pad_t &p1 = a.pad(1, "tone");
    p1.level = 100; p1.bus = 1;                                              // into FXA
    CHECK(rack_insert_row(&a.pads.fx, 0, 0, MOD_DELAY));                     // FXA: one echo 200 ms later, half as loud
    a.pads.fx.slot[0].v[MP_DL_TIME] = 200; a.pads.fx.slot[0].v[MP_DL_FB] = 0; a.pads.fx.slot[0].v[MP_DL_MIX] = 0.5f; a.pads.fx.slot[0].v[MP_DL_PONG] = 0;
    a.build();
    engine_synth_pad_push(0);
    const Stereo dry = a.run(0.7);
    engine_synth_pad_push(1);
    const Stereo wet = a.run(0.7);
    const double d_note = rms(part(dry.l, 0.05, 0.25)), d_after = rms(part(dry.l, 0.36, 0.48));
    const double w_note = rms(part(wet.l, 0.05, 0.18)), w_after = rms(part(wet.l, 0.36, 0.48));
    std::printf("    dry pad: %.0f then %.1f; pad on FXA (a 200 ms echo): %.0f then %.0f\n", d_note, d_after, w_note, w_after);
    CHECK(d_note > 5000.0 && d_after < 2.0);                                 // the dry pad: its 0.3 s and nothing more
    CHECK(w_note > 2000.0 && w_after > 2000.0);                              // the other one: the echo sounds after the sample ended
}

TEST(pads_fx_the_four_routings) {
    PadsApp a;
    a.add("hi", sine(3000.0, 0.5));
    tone_pad(a, 0, 1);                                                       // pad 1 into A
    CHECK(rack_insert_row(&a.pads.fx, 0, 1, MOD_FILTER));                    // chain B: a low-pass at 200 Hz; A and C are empty
    a.pads.fx.slot[0].v[MP_FL_CUT] = 200; a.pads.fx.slot[0].v[MP_FL_RES] = 0.7f;
    a.build();
    engine_synth_pad_push(0);
    double lv[RACK_ROUTE_COUNT];
    for (int r = 0; r < RACK_ROUTE_COUNT; r++) {
        a.pads.fx.fx_route = static_cast<uint8_t>(r);
        engine_synth_pads_fx(&a.pads, &a.rack);
        lv[r] = level_3k(a);
        CHECK(engine_synth_pads_playing() == 1u);                            // the pad plays on through the new graph
    }
    std::printf("    a 3 kHz pad on A, a 200 Hz low-pass in B: A/B/C %.0f, A+B/C %.0f, A+B+C %.0f, A+C/A+B %.0f\n", lv[0], lv[1], lv[2], lv[3]);
    CHECK(lv[RACK_ROUTE_A_B_C] > 4000.0);                                    // A beside B: untouched
    CHECK(lv[RACK_ROUTE_AB_C] < lv[0] / 30.0);                               // A into B: filtered
    CHECK(lv[RACK_ROUTE_ABC] < lv[0] / 30.0);                                // A into B into C: filtered
    CHECK(lv[RACK_ROUTE_AC_AB] > 0.8 * lv[0]);                               // A into C and into B: C lets it through
    // where a pad enters: on B it is filtered in every routing; on C, in A+B+C, it comes after the filter
    a.pads.pad[0].bus = 2;
    engine_synth_pad_set(0, &a.pads.pad[0]);
    CHECK(level_3k(a) < lv[0] / 30.0);
    a.pads.fx.fx_route = RACK_ROUTE_ABC;
    engine_synth_pads_fx(&a.pads, &a.rack);
    a.pads.pad[0].bus = 3;
    engine_synth_pad_set(0, &a.pads.pad[0]);
    CHECK(level_3k(a) > 0.8 * lv[0]);
}

TEST(pads_fx_a_value_moves_without_a_reload_and_twelve_modules_fit) {
    PadsApp a;
    a.add("hi", sine(3000.0, 0.5));
    tone_pad(a, 0, 1);
    CHECK(rack_insert_row(&a.pads.fx, 0, 0, MOD_FILTER));
    a.pads.fx.slot[0].v[MP_FL_CUT] = 200; a.pads.fx.slot[0].v[MP_FL_RES] = 0.7f;
    a.build();
    engine_synth_pad_push(0);
    const double closed = level_3k(a);
    const int builds = engine_synth_build_count();
    a.pads.fx.slot[0].v[MP_FL_CUT] = 12000;                                  // the cutoff opens: a parameter, not a new graph
    engine_synth_pads_fx(&a.pads, &a.rack);
    const double open = level_3k(a);
    std::printf("    low-pass 200 Hz %.0f -> 12 kHz %.0f, graph builds %d -> %d\n", closed, open, builds, engine_synth_build_count());
    CHECK_EQ(engine_synth_build_count(), builds);
    CHECK(open > 20.0 * closed && open > 4000.0);
    // four modules in each chain, the chains in a row: it builds and the pad still comes out
    for (int row = 0; row < RACK_ROWS; row++)
        while (rack_insert_row(&a.pads.fx, a.pads.fx.count, row, MOD_EQ)) {}
    CHECK_EQ(a.pads.fx.count, 3 * RACK_FX_SLOTS);
    CHECK(!rack_can_insert(&a.pads.fx, 0, 0, MOD_EQ));                       // a fifth one in a chain: refused
    CHECK(!rack_can_insert(&a.pads.fx, 0, 0, MOD_OSC));
    a.pads.fx.fx_route = RACK_ROUTE_ABC;
    engine_synth_pads_fx(&a.pads, &a.rack);
    CHECK(engine_synth_build_count() > builds);
    CHECK(level_3k(a) > 3000.0);
    CHECK(engine_synth_pads_playing() == 1u);
}

/* ---------------- capture / resampling: what plays, recorded into a new sample on the card ---------------- */
#include <filesystem>
#include <fstream>

namespace {
struct CardDir {                                                             // a folder that plays the card for the capture job
    std::filesystem::path root;
    CardDir() : root(std::filesystem::temp_directory_path() / "sc_capture_test") {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / STORAGE_DIR_SAMPLES);
        engine_synth_set_card_root(root.string().c_str());
    }
    ~CardDir() { std::filesystem::remove_all(root); }
    std::vector<double> read(const char *name, SmpHeader &h) const {
        std::ifstream in(root / STORAGE_DIR_SAMPLES / (std::string(name) + ".smp"), std::ios::binary);
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<double> x;
        if (raw.size() < kSmpBlockBytes || !smp_parse_header(raw.data(), static_cast<uint32_t>(raw.size()), h)) return x;
        CHECK_EQ(raw.size(), h.file_bytes());
        for (uint32_t i = 0; i < h.frames; i++) x.push_back(static_cast<int16_t>(get16(&raw[kSmpBlockBytes + 2 * i])));
        return x;
    }
    bool has(const char *file) const { return std::filesystem::exists(root / STORAGE_DIR_SAMPLES / file); }
};
// The audio runs and the card thread takes what was recorded, in turns.
void play(PadsApp &a, double seconds) {
    for (int k = 0; k < static_cast<int>(seconds / 0.02 + 0.5); k++) { a.run(0.02); while (engine_synth_capture_step()) {} }
}
}  // namespace

TEST(capture_records_what_plays_from_the_first_sound) {
    CardDir card;
    PadsApp a;
    a.rack.cfg.volume = 0.25f;                                               // the volume knob does not reach the recording
    a.add("tone", sine(440.0, 0.4));
    a.pad(0, "tone").level = 100;
    a.build();
    uint32_t frames = 7, lost = 7;
    CHECK(engine_synth_capture_start("rec1", 300, 0, 0));
    CHECK(!engine_synth_capture_start("rec2", 300, 0, 0));                  // one at a time
    play(a, 0.2);                                                            // armed: silence does not start it
    CHECK_EQ(engine_synth_capture_poll(&frames, &lost), CAPTURE_ARMED);
    CHECK_EQ(frames, 0u);
    engine_synth_pad_push(0);
    play(a, 0.6);
    CHECK_EQ(engine_synth_capture_poll(&frames, &lost), CAPTURE_RECORDING);
    engine_synth_capture_stop();
    play(a, 0.1);
    CHECK_EQ(engine_synth_capture_poll(&frames, &lost), CAPTURE_DONE);
    CHECK_EQ(lost, 0u);
    SmpHeader h;
    const std::vector<double> x = card.read("rec1", h);
    const double sr = kSampleRate;
    std::printf("    recorded %u frames (%.2f s) at %u Hz; the note %.0f rms, after it %.1f\n", h.frames, h.frames / sr, h.sample_rate,
                rms(part(x, 0.02, 0.35)), rms(part(x, 0.45, 0.55)));
    CHECK_EQ(h.sample_rate, static_cast<uint32_t>(kSampleRate));
    CHECK(h.frames > 0.55 * sr && h.frames < 0.65 * sr);                     // from the push to the stop
    CHECK_EQ(h.frames, frames);
    CHECK(h.has_peaks() && h.peaks[2] > 100 && h.peaks[60] < 5);            // the overview: loud at the start, silent at the end
    CHECK_NEAR(rms(part(x, 0.02, 0.35)), 0.5 * 32767.0 / std::sqrt(2.0), 400.0);   // the pad's own level, not the quarter volume
    CHECK(std::sqrt(power_at(part(x, 0.02, 0.35), 440.0)) > 7500.0);         // and its pitch
    CHECK(rms(part(x, 0.45, 0.55)) < 2.0);
    CHECK(std::fabs(x[0]) + std::fabs(x[1]) + std::fabs(x[2]) > 0.0 || rms(part(x, 0.0, 0.002)) > 50.0);   // no silence in front
    CHECK(!card.has("rec1.smp.tmp"));
    engine_synth_capture_done();
    CHECK_EQ(engine_synth_capture_poll(nullptr, nullptr), CAPTURE_IDLE);
    // armed and stopped before any sound: no file
    CHECK(engine_synth_capture_start("rec2", 300, 0, 0));
    play(a, 0.1);
    engine_synth_capture_stop();
    play(a, 0.1);
    CHECK_EQ(engine_synth_capture_poll(nullptr, nullptr), CAPTURE_EMPTY);
    engine_synth_capture_done();
    CHECK(!card.has("rec2.smp") && !card.has("rec2.smp.tmp"));
}

TEST(capture_on_the_clock_starts_on_a_bar_and_lasts_its_bars) {
    CardDir card;
    PadsApp a;
    a.add("hi", sine(3000.0, 0.5));
    tone_pad(a, 0, 0);
    a.build();
    engine_synth_pad_push(0);                                                // a loop that sounds all along
    engine_synth_set_clock(120, 1);                                   // 120 BPM: a bar = 2 s
    play(a, 0.5);
    const uint32_t bar = static_cast<uint32_t>(2 * kSampleRate);
    CHECK(engine_synth_capture_start("loop1", 300, 1, bar));                 // one bar, from the next bar line
    uint32_t frames = 0, lost = 0;
    play(a, 1.0);
    CHECK_EQ(engine_synth_capture_poll(&frames, &lost), CAPTURE_ARMED);      // the pad sounds, but the bar line is at 2 s
    play(a, 3.2);                                                            // past the end of the second bar
    CHECK_EQ(engine_synth_capture_poll(&frames, &lost), CAPTURE_DONE);       // it stopped by itself
    SmpHeader h;
    const std::vector<double> x = card.read("loop1", h);
    std::printf("    one bar at 120 BPM: %u frames (a bar is %u), lost %u, level %.0f\n", h.frames, bar, lost, rms(x));
    CHECK_EQ(h.frames, bar);                                                 // exactly a bar
    CHECK_EQ(lost, 0u);
    CHECK(rms(x) > 8000.0);
    engine_synth_capture_done();
    engine_synth_set_clock(120, 0);
}

// The catalog holds the files in use (core/audio.h audio_sample_use): an entry nothing uses any more is taken out, its sample stays in
// memory while a voice still plays it and is unloaded afterwards (engine_synth_housekeeping), and then the next file takes its index.
TEST(pads_catalog_entry_leaves_and_its_sample_is_unloaded_once_nothing_plays_it) {
    PadsApp a;
    a.add("low", sine(220.0, 0.5));
    a.add("high", sine(880.0, 0.5));
    a.pad(0, "low").level = 100;
    a.build();
    auto card_file = [&](const char *name, double hz) {                        // a file on the card that is not in the catalog
        const std::vector<int16_t> pcm = sine(hz, 0.3);
        SmpHeader h;
        h.sample_rate = static_cast<uint32_t>(kSampleRate);
        h.frames = static_cast<uint32_t>(pcm.size());
        h.root_note = 60;
        a.card.add_file((std::string(name) + ".smp").c_str(), smp_build(h, pcm.data()));
        audio_sample_info_t in;
        std::memset(&in, 0, sizeof in);
        std::snprintf(in.name, sizeof in.name, "%s", name);
        in.frames = h.frames; in.rate = h.sample_rate; in.root = 60;
        return in;
    };
    auto bulk_used = [] { size_t f, c, hi, b, sp; engine_synth_heap_stats(&f, &c, &hi, &b, &sp); return b; };
    auto settle = [&](double seconds) { for (double t = 0; t < seconds; t += 0.01) { a.run(0.01); engine_synth_housekeeping(); } };

    engine_synth_pad_push(0);
    a.run(0.05);                                                               // "low" sounds
    a.pad(0, "high");                                                          // the pad takes another file; "low" leaves the catalog
    engine_synth_pad_set(0, &a.pads.pad[0]);
    const int low = engine_synth_catalog_find("low");
    CHECK(low >= 0);
    engine_synth_catalog_remove(low);
    CHECK_EQ(engine_synth_catalog_find("low"), -1);
    std::vector<double> heard;
    for (int k = 0; k < 10; k++) { const Stereo s = a.run(0.01); heard.insert(heard.end(), s.l.begin(), s.l.end()); engine_synth_housekeeping(); }
    const audio_sample_info_t mid = card_file("mid", 440.0);
    const int mid_index = engine_synth_catalog_add(&mid);
    const size_t with_low = bulk_used();
    std::printf("    while it sounds: 220 Hz power %.0f, the next file gets index %d (the entry that left: %d)\n", power_at(heard, 220.0), mid_index, low);
    CHECK(power_at(heard, 220.0) > 1e5);                                       // the voice was not cut: its sample is still there
    CHECK(mid_index >= 0 && mid_index != low);                                 // ... so its entry is not given away yet
    settle(0.7);                                                               // the one-shot ends: now it is unloaded
    const size_t without = bulk_used();
    std::printf("    bulk memory: %zu with it, %zu after\n", with_low, without);
    CHECK(without + static_cast<size_t>(0.4 * kSampleRate) * sizeof(int16_t) < with_low);   // 0.5 s of 16-bit mono came back (48 KB at 48 kHz)
    const audio_sample_info_t top = card_file("top", 1760.0);
    CHECK_EQ(engine_synth_catalog_add(&top), low);                             // the next file takes the free index
    a.pad(1, "top").level = 100;
    engine_synth_pad_set(1, &a.pads.pad[1]);
    settle(0.1);                                                               // it loads into the slot that was freed
    engine_synth_pad_push(1);
    engine_synth_pad_push(0);
    const Stereo s = a.run(0.15);
    std::printf("    after: 1760 Hz power %.0f, 880 Hz power %.0f\n", power_at(s.l, 1760.0), power_at(s.l, 880.0));
    CHECK(power_at(s.l, 1760.0) > 1e5);
    CHECK(power_at(s.l, 880.0) > 1e5);
    CHECK(power_at(s.l, 220.0) * 1000.0 < power_at(s.l, 880.0));                // the old sample is not heard (what is left: the DFT's leakage)
}

// ---- the pads' sequencer (ADR-042 stage 3): a pattern of hits pushes the pads on the machine clock ----

namespace {
// The onsets of a recording (level above thr after a quiet stretch), in seconds.
std::vector<double> onsets_of(const std::vector<double> &x, double thr = 300.0, double quiet_s = 0.02) {
    std::vector<double> t;
    const size_t quiet = static_cast<size_t>(quiet_s * kSampleRate);
    size_t i = 0;
    while (i < x.size()) {
        if (std::fabs(x[i]) <= thr) { i++; continue; }
        t.push_back(static_cast<double>(i) / kSampleRate);
        size_t still = 0;                                                      // on to the next quiet stretch
        while (i < x.size() && still < quiet) { still = std::fabs(x[i]) <= thr ? still + 1 : 0; i++; }
    }
    return t;
}
}  // namespace

TEST(pads_sequencer_pushes_its_hits_on_the_clock_with_their_level) {
    PadsApp a;
    a.add("blip", sine(880.0, 0.05));                                          // 50 ms: over before the next step (125 ms at 120 BPM)
    a.pad(0, "blip").grid = PAD_GRID_BAR;                                      // its own grid setting is ignored by the sequencer
    a.pad(1, "blip");
    a.build();
    pseq_pattern_t p;
    pseq_clear(&p);
    p.steps = 8;
    CHECK(pseq_put(&p, 0, 0, 16, 100, 1, 1));                                  // pad 1 on steps 1 and 5, pad 2 on step 3 at a quarter of its level
    CHECK(pseq_put(&p, 0, 4, 16, 100, 1, 1));
    CHECK(pseq_put(&p, 1, 2, 4, 100, 1, 1));
    engine_synth_pseq_set(&p, 1);                                              // the sequencer first, then the clock: step 1 on beat 1
    engine_synth_set_clock(120, 1);
    const Stereo s = a.run(0.99);                                              // one pass of 8 steps = 1 s
    const std::vector<double> on = onsets_of(s.l);
    CHECK_EQ(on.size(), 3u);
    if (on.size() == 3) {
        const double tol = 1.5 * kBlock / kSampleRate + 0.002;
        CHECK_NEAR(on[0], 0.0, tol); CHECK_NEAR(on[1], 0.25, tol); CHECK_NEAR(on[2], 0.5, tol);
    }
    auto level_at = [&](double t) { std::vector<double> w(s.l.begin() + static_cast<long>(t * kSampleRate), s.l.begin() + static_cast<long>((t + 0.04) * kSampleRate)); return rms(w); };
    const double full = level_at(0.005), quarter = level_at(0.255);
    std::printf("    a hit at 16/16: rms %.0f, at 4/16: %.0f\n", full, quarter);
    CHECK_NEAR(quarter / full, 0.25, 0.03);
    CHECK_EQ(engine_synth_pseq_pos(), 7);
    // a hit added while it plays is heard at its turn; the sequencer switched off (the clock goes on): silence
    CHECK(pseq_put(&p, 1, 1, 16, 100, 1, 1));
    engine_synth_pseq_set(&p, 1);
    const std::vector<double> on2 = onsets_of(a.run(1.0).l);
    CHECK_EQ(on2.size(), 4u);
    engine_synth_pseq_set(&p, 0);
    a.run(0.2);
    CHECK(rms(a.run(1.0).l) < 1.0);
    CHECK_EQ(engine_synth_pseq_pos(), -1);
    int bar = 0, beat = 0, frac = 0;
    CHECK(engine_synth_transport(&bar, &beat, &frac));
}

TEST(pads_sequencer_ratchets_holds_gate_pads_for_their_length_and_takes_a_queued_pattern) {
    PadsApp a;
    a.add("blip", sine(880.0, 0.008));                                         // 8 ms: the ratchet's hits (31 ms apart) stay apart
    a.add("tone", sine(440.0, 3.0));
    a.pad(0, "blip");
    pad_t &g = a.pad(1, "tone");
    g.mode = PAD_GATE; g.tail_ms = 5;
    pad_t &t = a.pad(2, "tone");
    t.mode = PAD_TOGGLE; t.tail_ms = 5;
    a.build();
    pseq_pattern_t p;
    pseq_clear(&p);
    p.steps = 16;                                                              // 2 s a pass at 120 BPM
    CHECK(pseq_put(&p, 0, 0, 16, 100, 4, 1));                                  // a ratchet of 4 in step 1: every 31 ms
    CHECK(pseq_put(&p, 1, 4, 16, 100, 1, 2));                                  // the gate pad from step 5, two steps long: 0.5 s .. 0.75 s
    engine_synth_pseq_set(&p, 1);
    engine_synth_set_clock(120, 1);
    const Stereo s = a.run(1.0);
    const std::vector<double> on = onsets_of(s.l, 2000.0, 0.01);
    CHECK(on.size() >= 5);
    if (on.size() >= 5) {
        for (int i = 0; i < 4; i++) CHECK_NEAR(on[static_cast<size_t>(i)], 0.125 * i / 4, 1.5 * kBlock / kSampleRate + 0.003);
        CHECK_NEAR(on[4], 0.5, 1.5 * kBlock / kSampleRate + 0.003);
    }
    auto level = [&](const std::vector<double> &x, double from, double to) { std::vector<double> w(x.begin() + static_cast<long>(from * kSampleRate), x.begin() + static_cast<long>(to * kSampleRate)); return rms(w); };
    CHECK(level(s.l, 0.55, 0.72) > 3000.0);                                    // held for its two steps ...
    CHECK(level(s.l, 0.80, 0.98) < 5.0);                                       // ... then let go (a 3 s sample: it would still sound)
    // a Toggle pad is started by its hit and stopped after its length, not flipped; a queued pattern follows at this one's end
    pseq_pattern_t q;
    pseq_clear(&q);
    q.steps = 4;
    CHECK(pseq_put(&q, 2, 0, 16, 100, 1, 1));
    CHECK(engine_synth_pseq_queue(&q, 5) != 0);
    CHECK_EQ(engine_synth_pseq_taken(), -1);
    const Stereo s2 = a.run(1.5);                                              // 1.0 .. 2.5 s: the first pattern ends at 2.0 s
    CHECK(level(s2.l, 0.1, 0.95) < 5.0);
    CHECK_EQ(engine_synth_pseq_taken(), 5);
    CHECK_EQ(engine_synth_pseq_taken(), -1);
    CHECK(level(s2.l, 1.02, 1.11) > 3000.0);                                   // its hit at 2.0 s: the toggle pad sounds for one step (125 ms)
    CHECK(level(s2.l, 1.20, 1.45) < 5.0);
    engine_synth_pseq_set(&q, 1);                                              // (the application hands it over from now on)
    const Stereo s3 = a.run(0.5);                                              // 2.5 .. 3.0: the next pass starts it again (it was not left on)
    CHECK(level(s3.l, 0.02, 0.11) > 3000.0);
    CHECK(level(s3.l, 0.20, 0.45) < 5.0);
    // the clock stops under a held pad: it is let go
    pseq_clear(&q);
    q.steps = 16;
    CHECK(pseq_put(&q, 1, 0, 16, 100, 1, 16));
    engine_synth_pseq_set(&q, 1);
    a.run(2.3);                                                                // into the pass that plays it (passes of the 4-step pattern end every 0.5 s)
    engine_synth_set_clock(120, 0);
    a.run(0.1);
    CHECK(rms(a.run(0.3).l) < 5.0);
}
