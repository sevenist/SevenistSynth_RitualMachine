// TechnoGen's data and its dice (src/tg_model.h).
#include <cstring>
#include "tests/test.h"
#include "tg_model.h"

using namespace tg;

namespace {
Track fresh(int t) {
    Track tr;
    for (int k = 0; k < kParams; k++) tr.value[k] = track_def(t).param[k].def;
    return tr;
}
bool same_steps(const Track &a, const Track &b) { return std::memcmp(a.step, b.step, sizeof a.step) == 0; }
bool same_values(const Track &a, const Track &b) { return std::memcmp(a.value, b.value, sizeof a.value) == 0; }
}  // namespace

TEST(model_the_design_tracks) {
    const char *names[kTracks] = {"KICK", "BASS", "DRUM", "PERC", "PAD", "TEXT", "SAMP"};
    for (int t = 0; t < kTracks; t++) {
        const TrackDef &d = track_def(t);
        CHECK(std::strcmp(d.name, names[t]) == 0);
        CHECK(std::strlen(d.shortname) == 3);
        CHECK(d.lanes >= 1 && d.lanes <= kLanes);
        for (int k = 0; k < kParams; k++) {
            CHECK(std::strlen(d.param[k].label) <= 5);
            CHECK(d.param[k].lo <= d.param[k].def && d.param[k].def <= d.param[k].hi && d.param[k].hi <= 100);
        }
    }
    CHECK_EQ(track_def(T_DRUM).lanes, 6);
    CHECK_EQ(track_def(T_PERC).lanes, 4);
}

TEST(model_a_seed_rolls_the_same_every_time) {
    for (int t = 0; t < kTracks; t++)
        for (int depth = 0; depth < D_COUNT; depth++) {
            Track a = fresh(t), b = fresh(t);
            tg_roll(a, t, 0x1234, depth);
            tg_roll(b, t, 0x1234, depth);
            CHECK(same_values(a, b));
            CHECK(same_steps(a, b));
            CHECK_EQ(a.seed, 0x1234);
        }
    Track a = fresh(T_BASS), b = fresh(T_BASS);
    tg_roll(a, T_BASS, 1, D_RAW);
    tg_roll(b, T_BASS, 2, D_RAW);
    CHECK(!same_values(a, b) || !same_steps(a, b));                 // another seed, another roll
}

TEST(model_raw_stays_in_the_sweet_spot_weird_goes_outside) {
    int outside = 0, total = 0;
    for (uint16_t seed = 1; seed < 200; seed++)
        for (int t = 0; t < kTracks; t++) {
            Track raw = fresh(t), weird = fresh(t);
            tg_roll(raw, t, seed, D_RAW);
            tg_roll(weird, t, seed, D_WEIRD);
            for (int k = 0; k < kParams; k++) {
                const ParamDef &d = track_def(t).param[k];
                // RAW: inside lo..hi, but where a rule pulled a value down
                CHECK(raw.value[k] <= d.hi);
                if (!(t == T_KICK && k == 6) && !(t == T_BASS && (k == 1 || k == 3)) && !(t == T_PAD && k == 5)) CHECK(raw.value[k] >= d.lo);
                const bool ruled = (t == T_KICK && k == 6) || (t == T_BASS && (k == 1 || k == 3)) || (t == T_PAD && k == 5);
                if ((d.lo > 0 || d.hi < 100) && !ruled) { total++; outside += weird.value[k] <= d.lo || weird.value[k] >= d.hi; }
            }
        }
    CHECK_EQ(outside, total);                                       // WEIRD is at or past an edge of the sweet spot, always
}

TEST(model_tight_nudges_the_values_and_keeps_the_steps) {
    for (int t = 0; t < kTracks; t++) {
        Track tr = fresh(t);
        tg_roll(tr, t, 77, D_RAW);
        const Track before = tr;
        tg_roll(tr, t, 999, D_TIGHT);
        CHECK(same_steps(tr, before));
        for (int k = 0; k < kParams; k++) {
            const int d = tr.value[k] - before.value[k];
            const bool ruled = (t == T_KICK && k == 6) || (t == T_BASS && k == 3) || (t == T_PAD && k == 5);
            if (!ruled) CHECK(d >= -12 && d <= 12);
        }
    }
}

TEST(model_a_locked_value_stays_and_the_others_roll_the_same) {
    for (int t = 0; t < kTracks; t++) {
        Track open = fresh(t), locked = fresh(t);
        locked.lock[2] = true;
        locked.value[2] = 99;
        tg_roll(open, t, 4242, D_RAW);
        tg_roll(locked, t, 4242, D_RAW);
        CHECK_EQ(locked.value[2], 99);
        for (int k = 0; k < kParams; k++) {
            const bool ruled = (t == T_KICK && k == 6) || (t == T_BASS && (k == 1 || k == 3)) || (t == T_PAD && k == 5);
            if (k != 2 && !ruled) CHECK_EQ(locked.value[k], open.value[k]);
        }
        CHECK(same_steps(open, locked));                            // the steps come after the values: the same random numbers
    }
}

TEST(model_the_rules_link_values) {
    Track k = fresh(T_KICK);
    k.value[3] = 80;                                                // a long decay
    k.value[6] = 60;
    tg_rules(k, T_KICK);
    CHECK_EQ(k.value[6], 20);
    Track b = fresh(T_BASS);
    b.value[1] = 90;                                                // reso
    b.value[3] = 90;                                                // drive
    tg_rules(b, T_BASS);
    CHECK(b.value[1] * b.value[3] <= 3600);
    CHECK_EQ(b.value[1], 90);                                       // the drive gives way first
    b.value[3] = 90;
    b.lock[3] = true;
    tg_rules(b, T_BASS);
    CHECK_EQ(b.value[3], 90);                                       // ... unless it is locked
    CHECK(b.value[1] * b.value[3] <= 3600);
}

TEST(model_the_steps_have_their_genre) {
    for (uint16_t seed = 1; seed < 100; seed++) {
        Track kick = fresh(T_KICK);
        tg_roll(kick, T_KICK, seed, D_RAW);
        for (int p = 0; p < kSteps; p += 4) CHECK_EQ(kick.step[0][p], S_ACCENT);   // four on the floor
        Track drum = fresh(T_DRUM);
        tg_roll(drum, T_DRUM, seed, D_RAW);
        CHECK(drum.step[0][4] == S_ACCENT || drum.step[1][4] == S_ACCENT);         // a backbeat
        Track pad = fresh(T_PAD);
        tg_roll(pad, T_PAD, seed, D_RAW);
        CHECK_EQ(pad.step[0][0], S_ACCENT);
        CHECK_EQ(tg_step_max(pad, 1), S_HOLD);
        for (int lane = 1; lane < kLanes; lane++) CHECK(!tg_lane_has(kick, lane));
    }
}

TEST(model_back_undoes_sixteen_rolls) {
    Pattern p;
    tg_init_pattern(p);
    Undo u;
    Pattern first = p;
    for (int i = 0; i < 20; i++) {
        u.push(p);
        tg_roll(p.track[T_BASS], T_BASS, static_cast<uint16_t>(100 + i), D_RAW);
    }
    CHECK_EQ(u.n, kUndo);
    int popped = 0;
    Pattern q;
    while (u.pop(q)) popped++;
    CHECK_EQ(popped, kUndo);
    CHECK_EQ(q.track[T_BASS].seed, 100 + 20 - kUndo - 1);          // the oldest kept: the state before roll 4, its seed from roll 3
    CHECK(!u.pop(q));
    (void)first;
}
