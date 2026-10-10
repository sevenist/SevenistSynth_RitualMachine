// The Pads section's player (ADR-041 decisions 14-26): pads.h settings -> engine_synth_pads_build / pad_set -> the Pads module -> 8 Sampler
// voices -> the simulated card. One-shot / gate / toggle, mono restart, choke, Stop all, trim, loop, tune, mute, pan, the filter.
#include <algorithm>
#include <vector>
#include "rig.h"
#include "platform/sim/sim_storage.h"
#include "engine/sampler/smp_format.h"
#include "engine/modules/sampler_modules.h"
#include "platform/engine/engine_synth.h"
#include "platform/engine/sample_catalog.h"

extern "C" {
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

    Stereo run(double seconds) {
        const int frames = static_cast<int>(seconds * engine_synth_sample_rate());
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
    p.end = 500;                                                              // the first half
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
    a.add("big", sine(220.0, 3.0));                                           // 3 s: bigger than a resident pad sample, streamed from the card
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
