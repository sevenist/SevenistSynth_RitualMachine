#pragma once
// TechnoGen's data: seven tracks, each with seven values, its steps (one row a lane), its seed; eight patterns of them; the rolls of the dice
// and the undo of BACK. Portable and plain (no engine, no screen): the app edits it, synth.cpp turns it into the engine's hits and values,
// the tests drive it alone.
//
// The dice (apps/technogen/docs/DESIGN.md ADR-048, "the genome", first version): a track's roll is drawn from its seed by one random sequence, the same
// on every machine. The seven values come first, then the steps. A locked value still takes its random number, so locking one never changes
// how the others roll. Each value has a sweet spot (lo..hi, in 0..100) that RAW stays inside; TIGHT nudges the values and keeps the steps;
// WEIRD goes to the edges. Rules then link values (tg_rules): a long kick decay caps its rumble, reso and drive of the bass stay under a limit.
#include <cstdint>

namespace tg {

constexpr int kTracks = 7, kParams = 7, kSteps = 16, kLanes = 6, kPatterns = 8, kUndo = 16;

enum TrackId : uint8_t { T_KICK, T_BASS, T_DRUM, T_PERC, T_PAD, T_TEXT, T_SAMP };
enum Depth : uint8_t { D_TIGHT, D_RAW, D_WEIRD, D_COUNT };
// A step: off, a hit, an accent, or (the pad) the chord held on from the step before.
enum StepKind : uint8_t { S_OFF, S_HIT, S_ACCENT, S_HOLD, S_KINDS };

struct ParamDef { const char *label; uint8_t lo, hi, def; };   // label: 5 letters at most (the screen's knob legend)
struct TrackDef {
    const char *name, *shortname;       // "KICK", "KCK"
    uint32_t color;                     // 0xRRGGBB: the row, the keys, the knob's LED
    uint8_t lanes;                      // rows of steps (DRUM: 6 instruments, PERC: 4 algorithms)
    const char *lane_name[kLanes];
    ParamDef param[kParams];
    uint16_t seed;                      // the seed of the machine's first start
};
const TrackDef &track_def(int t);
const char *depth_name(int d);          // "TIGHT", "RAW", "WEIRD"

struct Track {
    uint16_t seed = 1;                  // the last roll's seed (shown in hex)
    uint8_t value[kParams] = {};        // 0..100
    bool lock[kParams] = {};            // the dice leaves it as it is
    uint8_t step[kLanes][kSteps] = {};  // StepKind
    uint8_t level = 60;                 // 0..100: the track's knob in the main view (its macro: for now its level in the mix)
    bool muted = false;
};
struct Pattern { Track track[kTracks]; };

// The random sequence of a seed (xorshift32), the same as the design's mock-up. next() is 0 .. 1 - 2^-32 in Q32 (x / 2^32).
struct Rng {
    uint32_t s;
    explicit Rng(uint16_t seed);
    uint32_t next();
    int below(int n) { return static_cast<int>((static_cast<uint64_t>(next()) * static_cast<uint32_t>(n)) >> 32); }   // 0 .. n-1
    int percent() { return below(100); }                                                                                  // 0 .. 99
};

// Rolls track `t` of `p` from `seed` at `depth`: its values (the locked ones kept) and, but at TIGHT, its steps. The rules apply after.
void tg_roll(Track &tr, int t, uint16_t seed, int depth);
void tg_rules(Track &tr, int t);        // the links between values (also after a knob moved one)
void tg_init_pattern(Pattern &p);       // every track rolled at RAW from its first seed
bool tg_lane_has(const Track &tr, int lane);   // the lane has a step that is not off
int tg_step_max(const Track &tr, int step);    // the strongest kind of a step over the lanes (the main view's row): S_ACCENT > S_HIT > S_HOLD

// BACK: the last kUndo states of a pattern, before each roll.
struct Undo {
    Pattern saved[kUndo];
    int n = 0, head = 0;                // how many are kept; where the next goes
    void push(const Pattern &p);
    bool pop(Pattern &p);               // false: none left
};

}  // namespace tg
