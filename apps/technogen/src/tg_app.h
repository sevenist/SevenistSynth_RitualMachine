#pragma once
// TechnoGen's application state: what the controls change and the screen and LEDs show (app.cpp). Here so the tests can look at it.
#include <cstdint>
#include "tg_model.h"

namespace tg {

enum View : uint8_t { V_MAIN, V_ENGINE };              // the seven tracks / the selected track's engine (encoder A's push)
enum KeyPage : uint8_t { K_STEP, K_TRACK, K_PERF };     // what the 16 keys are (the function keys STEP, TRK, PERF)

constexpr int kKnobs = 7;                               // K1..K4, R1..R3: the tracks' levels in the main view, the values in the engine view
constexpr int kDefaultBpm = 132;

// A modifier (SHFT, the fourth function key; MOD, B1): held = on while held; tapped alone = latched on until tapped again.
struct Modifier {
    bool held = false, latched = false, used = false;   // used: another control was used while it was held (its release does not latch)
    bool on() const { return held || latched; }
};

struct AppState {
    Pattern pattern[kPatterns];
    int cur = 0;                                        // the pattern that plays and that the controls edit
    int queued = -1;                                    // the pattern to go on to at the end of the bar (-1: none)
    uint32_t taken = 0;                                 // the synth's count of queued patterns taken (synth_taken), last seen
    Undo undo;
    int selected = 0;                                   // the track
    uint8_t lane[kTracks] = {};                         // the lane the step keys edit, per track
    View view = V_MAIN;
    KeyPage keys = K_STEP;
    Modifier shift, mod;
    int depth = D_RAW;
    bool playing = false;
    int bpm = kDefaultBpm;
    int step = -1;                                      // the step that plays (the screen's playhead)
    int last_value = -1;                                // the value a knob moved last (the footer shows it), -1: none
    char message[24] = {};                              // the footer's second line after an action ("" = the default)
    // the knobs: a knob drives its value only once it has reached it (no jump when it is given another value to drive)
    int knob_pos[kKnobs] = {-1, -1, -1, -1, -1, -1, -1};   // where each one is, 0..100 (-1: not read yet)
    bool knob_caught[kKnobs] = {};
    Rng rng{0xBEEF};                                    // the dice's new seeds, stirred by every control used

    Track &track(int t) { return pattern[cur].track[t]; }
    Track &sel() { return track(selected); }
};

AppState &app_state();

}  // namespace tg
