// TechnoGen's sound (src/synth.h) with the engine: the pattern reaches the track modules through the pads' sequencer routed to them
// (Engine::pseq_route), on time, and only the tracks that have hits sound.
#include <cstdlib>
#include <cstring>
#include <vector>
#include "synth.h"
#include "tests/test.h"

using namespace tg;

namespace {
constexpr size_t kFast = 1u << 20, kBulk = 1u << 20;

struct Rig {
    void *fast = std::malloc(kFast), *bulk = std::malloc(kBulk);
    bool ok = synth_init(fast, kFast, bulk, kBulk);
    ~Rig() { synth_shutdown(); std::free(fast); std::free(bulk); }
};

// An empty pattern (every track silent: no steps, and the text track, which runs free while the clock runs, at level 0), the values at
// their start.
Pattern empty() {
    Pattern p;
    tg_init_pattern(p);
    for (Track &t : p.track) std::memset(t.step, S_OFF, sizeof t.step);
    p.track[T_TEXT].level = 0;
    return p;
}

void send(const Pattern &p) {
    for (int t = 0; t < kTracks; t++) synth_set_track(t, p.track[t]);
    synth_set_steps(p);
}

// Renders `frames` and returns the peak of each run of `chunk` frames (left channel).
std::vector<int> peaks(int frames, int chunk) {
    std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
    synth_render(buf.data(), frames);
    std::vector<int> out;
    for (int at = 0; at < frames; at += chunk) {
        int pk = 0;
        for (int i = at; i < at + chunk && i < frames; i++) pk = std::max(pk, std::abs(static_cast<int>(buf[static_cast<size_t>(i) * 2])));
        out.push_back(pk);
    }
    return out;
}
int peak_of(const std::vector<int> &v) { int m = 0; for (int x : v) m = std::max(m, x); return m; }
int samples_per_step(int bpm) { return synth_sample_rate() * 60 / bpm / 4; }
}  // namespace

TEST(synth_starts_silent_and_stopped) {
    Rig r;
    CHECK(r.ok);
    send(empty());
    synth_set_transport(120, false);
    CHECK_EQ(synth_step(), -1);
    CHECK_EQ(peak_of(peaks(4096, 4096)), 0);
}

TEST(synth_a_kick_on_the_one_sounds_on_the_one) {
    Rig r;
    Pattern p = empty();
    p.track[T_KICK].step[0][0] = S_ACCENT;
    p.track[T_KICK].step[0][8] = S_ACCENT;
    send(p);
    synth_set_transport(120, true);
    const int step = samples_per_step(120);
    const std::vector<int> pk = peaks(step * 16, step);             // the peak of each step
    CHECK(pk[0] > 2000);                                            // step 1: the kick
    CHECK(pk[8] > 2000);                                            // step 9: the second one
    CHECK(pk[8] > pk[7] * 3 / 2);                                   // ... a new hit there, louder than the first one's tail
    CHECK(synth_step() >= 0);
    CHECK_EQ(synth_hits(), 2);
}

TEST(synth_every_track_sounds_through_its_own_lanes) {
    for (int t = 0; t < kTracks; t++)
        for (int lane = 0; lane < track_def(t).lanes; lane++) {
            Rig r;
            Pattern p = empty();
            p.track[t].step[lane][0] = S_ACCENT;
            p.track[t].level = 100;
            send(p);
            synth_set_transport(120, true);
            const int pk = peak_of(peaks(samples_per_step(120) * 2, 256));
            if (pk < 300) std::printf("    track %d lane %d: peak %d\n", t, lane, pk);
            CHECK(pk >= 300);
        }
}

TEST(synth_the_text_track_runs_free_while_the_clock_runs) {
    Rig r;
    Pattern p = empty();
    p.track[T_TEXT].level = 100;
    p.track[T_TEXT].value[4] = 60;                                  // GATE: a floor while the gate is closed
    send(p);
    synth_set_transport(120, false);
    CHECK_EQ(peak_of(peaks(8192, 8192)), 0);                        // stopped: silent
    synth_set_transport(120, true);
    peaks(4096, 4096);
    CHECK(peak_of(peaks(8192, 8192)) > 100);                        // running, with no step: its floor
}

TEST(synth_a_muted_or_silent_track_does_not_sound) {
    {
        Rig r;
        Pattern p = empty();
        p.track[T_KICK].step[0][0] = S_ACCENT;
        p.track[T_KICK].muted = true;
        send(p);
        synth_set_transport(120, true);
        CHECK_EQ(peak_of(peaks(samples_per_step(120) * 2, 4096)), 0);
        CHECK_EQ(synth_hits(), 0);
    }
    {
        Rig r;
        Pattern p = empty();
        p.track[T_BASS].step[0][0] = S_ACCENT;
        p.track[T_BASS].level = 0;
        send(p);
        synth_set_transport(120, true);
        CHECK_EQ(peak_of(peaks(samples_per_step(120) * 2, 4096)), 0);
    }
}

TEST(synth_a_full_pattern_keeps_to_the_sequencers_room) {
    Rig r;
    Pattern p = empty();
    for (int t = 0; t < kTracks; t++)
        for (int lane = 0; lane < track_def(t).lanes; lane++)
            for (int s = 0; s < kSteps; s++) p.track[t].step[lane][s] = S_HIT;
    send(p);
    CHECK_EQ(synth_hits(), 240);                                    // 15 lanes x 16 steps: all fit (ENGINE_PADSEQ_HITS=255)
    CHECK(synth_max_hits() >= 240);
    synth_set_transport(132, true);
    CHECK(peak_of(peaks(4096, 4096)) > 0);
}

TEST(synth_the_dice_never_reloads_the_graph) {
    Rig r;
    Pattern p;
    tg_init_pattern(p);
    send(p);
    synth_set_transport(132, true);
    peaks(2048, 2048);
    for (int i = 0; i < 20; i++) {                                  // twenty rolls while it plays: values and hits only
        tg_roll(p.track[i % kTracks], i % kTracks, static_cast<uint16_t>(500 + i), D_WEIRD);
        send(p);
        peaks(256, 256);
        synth_update();
    }
    CHECK(peak_of(peaks(8192, 8192)) > 0);
}
