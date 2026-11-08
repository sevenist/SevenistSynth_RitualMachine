// TechnoGen's data and its dice (tg_model.h). The tracks' names, colours and value labels are the design's (the main view's mock-up); the
// sweet spots and the step generators are a first version, to tune by ear.
#include "tg_model.h"
#include <cstring>

namespace tg {

namespace {
// name, short, colour, lanes, lane names, the seven values {label, sweet spot lo, hi, start value}, the first seed
const TrackDef kDefs[kTracks] = {
    {"KICK", "KCK", 0xFF6A2B, 1, {"KICK"},
     {{"PITCH", 30, 60, 45}, {"DROP", 40, 80, 60}, {"PUNCH", 40, 85, 60}, {"DECAY", 25, 70, 45}, {"DRIVE", 10, 60, 30}, {"CLICK", 15, 60, 35}, {"RUMBL", 0, 55, 20}},
     0x7F3A},
    {"BASS", "BAS", 0xD4F000, 1, {"BASS"},
     {{"CUTOF", 20, 70, 40}, {"RESO", 20, 75, 45}, {"ENV", 25, 80, 50}, {"DRIVE", 10, 60, 30}, {"DECAY", 15, 65, 35}, {"GLIDE", 0, 40, 10}, {"ALGO", 0, 100, 0}},
     0x21C4},
    {"DRUM", "DRM", 0x4FD1E8, 6, {"SNARE", "CLAP", "RIM", "HAT", "CYMB", "TOM"},
     {{"TUNE", 30, 70, 50}, {"SNAP", 30, 80, 55}, {"DECAY", 20, 60, 40}, {"NOISE", 30, 75, 50}, {"METAL", 20, 70, 45}, {"CRUSH", 0, 35, 10}, {"SKEW", 0, 30, 5}},
     0xB609},
    {"PERC", "PRC", 0xF2C14E, 4, {"MODAL", "RUST", "BYTE", "CLANK"},
     {{"STRIK", 30, 80, 55}, {"BODY", 25, 75, 50}, {"RING", 20, 70, 45}, {"WARP", 0, 45, 15}, {"PITCH", 30, 75, 50}, {"DRIVE", 0, 50, 20}, {"ALGO", 0, 100, 0}},
     0x4E17},
    {"PAD", "PAD", 0xB08CFF, 1, {"PAD"},
     {{"SPRED", 30, 80, 55}, {"AGE", 10, 60, 30}, {"ATTCK", 30, 85, 60}, {"FILTR", 25, 70, 45}, {"MANGL", 0, 50, 15}, {"FDBK", 0, 45, 15}, {"FREEZ", 0, 30, 0}},
     0xD05E},
    {"TEXT", "TXT", 0x7CE38B, 1, {"TEXT"},
     {{"GRIT", 20, 70, 45}, {"DENS", 20, 65, 40}, {"TONE", 25, 75, 50}, {"DRIFT", 15, 60, 35}, {"GATE", 0, 60, 20}, {"RESON", 10, 55, 30}, {"ALGO", 0, 100, 0}},
     0x93AB},
    {"SAMP", "SMP", 0xFF9EC4, 1, {"SAMP"},
     {{"START", 0, 60, 0}, {"LEN", 30, 90, 60}, {"PITCH", 35, 65, 50}, {"SCAN", 0, 60, 0}, {"FILTR", 30, 85, 70}, {"CRUSH", 0, 40, 0}, {"RESYN", 0, 30, 0}},
     0x1C72},
};
const char *const kDepthNames[D_COUNT] = {"TIGHT", "RAW", "WEIRD"};

uint8_t clamp100(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v)); }
bool chance(Rng &r, int percent) { return r.percent() < percent; }

// The value a roll gives, from one random number x (Q32) and the value before.
uint8_t roll_value(const ParamDef &d, uint8_t before, uint32_t x, int depth) {
    const int64_t span = static_cast<int64_t>(x);                   // 0 .. 2^32 - 1
    if (depth == D_TIGHT) return clamp100(before + static_cast<int>((span * 25 >> 32)) - 12);   // a nudge of -12 .. +12
    if (depth == D_RAW) return clamp100(d.lo + static_cast<int>((span * (d.hi - d.lo + 1)) >> 32));
    // WEIRD: the edges outside the sweet spot, below it for the first half of x, above it for the second
    const int64_t half = span >> 31 ? span - (1ll << 31) : span;    // 0 .. 2^31 - 1
    if (!(span >> 31)) return clamp100(static_cast<int>((half * (d.lo + 1)) >> 31));
    return clamp100(d.hi + static_cast<int>((half * (101 - d.hi)) >> 31));
}

// The steps of a roll, track by track. TIGHT keeps the steps (the caller does not come here).
void roll_steps(Track &tr, int t, Rng &r, int depth) {
    std::memset(tr.step, S_OFF, sizeof tr.step);
    uint8_t *s = tr.step[0];
    const bool weird = depth == D_WEIRD;
    switch (t) {
    case T_KICK:                                                    // four on the floor, a ghost now and then
        for (int p = 0; p < kSteps; p++) {
            const bool ghost = chance(r, weird ? 14 : 5);
            if (p % 4 == 0) s[p] = S_ACCENT;
            else if (ghost) s[p] = S_HIT;
        }
        break;
    case T_BASS:                                                    // the off-beat, and sixteenths around it
        for (int p = 0; p < kSteps; p++) {
            const int x = r.percent();
            if (p % 4 == 2) s[p] = x < 90 ? S_ACCENT : S_OFF;
            else if (p % 4 != 0 && x < (weird ? 45 : 30)) s[p] = S_HIT;
        }
        break;
    case T_DRUM: {                                                  // SNARE CLAP RIM HAT CYMB TOM
        const int back = chance(r, 50) ? 0 : 1;                     // the backbeat on the snare or the clap ...
        tr.step[back][4] = tr.step[back][12] = S_ACCENT;
        if (chance(r, 35)) tr.step[1 - back][12] = S_HIT;           // ... the other one doubles the second, sometimes
        for (int p = 0; p < kSteps; p++) {
            const int rim = r.percent(), hat = r.percent(), tom = r.percent();
            if (p % 2 == 1 && rim < (weird ? 30 : 12)) tr.step[2][p] = S_HIT;
            if (p % 4 == 2) tr.step[3][p] = hat < 70 ? S_ACCENT : S_HIT;        // the off-beat hat (an accent: open)
            else if (hat < (weird ? 60 : 40)) tr.step[3][p] = S_HIT;             // closed sixteenths
            if (tom < (weird ? 20 : 6)) tr.step[5][p] = S_HIT;
        }
        if (chance(r, 25)) tr.step[4][0] = S_ACCENT;                // a cymbal on the one
        break;
    }
    case T_PERC:                                                    // a Euclidean rhythm on two lanes (all four at WEIRD)
        for (int lane = 0; lane < 4; lane++) {
            const int k = 3 + r.below(5), rot = r.below(kSteps), on = r.percent();
            if (!weird && on >= 50) continue;
            for (int p = 0; p < kSteps; p++)
                if ((p * k) % kSteps < k) tr.step[lane][(p + rot) % kSteps] = S_HIT;
        }
        break;
    case T_PAD: {                                                   // one chord a bar, or two
        const bool split = chance(r, 50);
        for (int p = 0; p < kSteps; p++) s[p] = (p == 0 || (split && p == 8)) ? S_ACCENT : S_HOLD;
        break;
    }
    case T_TEXT: {                                                  // a gate that opens at random, denser with the depth
        const int density = 20 + depth * 15;
        for (int p = 0; p < kSteps; p++) s[p] = chance(r, density) ? S_HIT : S_OFF;
        break;
    }
    default:                                                        // SAMP: the slice on the one, others now and then
        for (int p = 0; p < kSteps; p++) {
            const bool hit = chance(r, 20);
            s[p] = p == 0 ? S_ACCENT : (hit ? S_HIT : S_OFF);
        }
        break;
    }
}
}  // namespace

const TrackDef &track_def(int t) { return kDefs[t < 0 || t >= kTracks ? 0 : t]; }
const char *depth_name(int d) { return kDepthNames[d < 0 || d >= D_COUNT ? 0 : d]; }

Rng::Rng(uint16_t seed) : s(static_cast<uint32_t>(seed) * 0x9E3779B1u) { if (!s) s = 1; }
uint32_t Rng::next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

void tg_rules(Track &tr, int t) {
    uint8_t *v = tr.value;
    if (t == T_KICK && v[6] > 100 - v[3] && !tr.lock[6]) v[6] = static_cast<uint8_t>(100 - v[3]);    // a long decay leaves less room for the rumble
    if (t == T_BASS && v[1] * v[3] > 3600) {                                                          // reso x drive (60 x 60 at most)
        if (!tr.lock[3]) v[3] = static_cast<uint8_t>(3600 / (v[1] ? v[1] : 1));
        else if (!tr.lock[1]) v[1] = static_cast<uint8_t>(3600 / (v[3] ? v[3] : 1));
    }
    if (t == T_PAD && v[4] * v[5] > 2500 && !tr.lock[5]) v[5] = static_cast<uint8_t>(2500 / (v[4] ? v[4] : 1));   // mangle x feedback
}

void tg_roll(Track &tr, int t, uint16_t seed, int depth) {
    const TrackDef &d = track_def(t);
    Rng r(seed);
    tr.seed = seed;
    for (int k = 0; k < kParams; k++) {
        const uint32_t x = r.next();                                // taken even when the value is locked
        if (!tr.lock[k]) tr.value[k] = roll_value(d.param[k], tr.value[k], x, depth);
    }
    if (depth != D_TIGHT) roll_steps(tr, t, r, depth);
    tg_rules(tr, t);
}

void tg_init_pattern(Pattern &p) {
    for (int t = 0; t < kTracks; t++) {
        p.track[t] = Track{};
        for (int k = 0; k < kParams; k++) p.track[t].value[k] = track_def(t).param[k].def;
        tg_roll(p.track[t], t, track_def(t).seed, D_RAW);
    }
}

bool tg_lane_has(const Track &tr, int lane) {
    for (int p = 0; p < kSteps; p++) if (tr.step[lane][p] != S_OFF) return true;
    return false;
}

int tg_step_max(const Track &tr, int step) {
    static const uint8_t rank[S_KINDS] = {0, 2, 3, 1};              // off < hold < hit < accent
    int best = S_OFF;
    for (int lane = 0; lane < kLanes; lane++)
        if (rank[tr.step[lane][step]] > rank[best]) best = tr.step[lane][step];
    return best;
}

void Undo::push(const Pattern &p) {
    saved[head] = p;
    head = (head + 1) % kUndo;
    if (n < kUndo) n++;
}

bool Undo::pop(Pattern &p) {
    if (!n) return false;
    head = (head + kUndo - 1) % kUndo;
    p = saved[head];
    n--;
    return true;
}

}  // namespace tg
