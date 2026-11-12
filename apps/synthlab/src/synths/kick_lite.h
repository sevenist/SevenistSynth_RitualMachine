#pragma once
// Kick Lite (kick_lite.cpp): the Kick with 8 values, the others fixed at the Kick's defaults. Its presets, and which row of the Kick
// each of its values is: tests/test_null.cpp renders every preset with both synths and null-tests them (one minus the other).
namespace kick_lite {

constexpr int kValues = 8;

struct Preset {
    const char *name;
    float v[kValues];                   // in the order of Kick Lite's list: Tune, Punch, Body, Grit, Tone, Click, Rumble, Room
};
extern const Preset kPresets[];
extern const int kPresetCount;

extern const char *const kKickRows[kValues];    // the Kick's row ("SECTION/Name") each value of Kick Lite stands for

}  // namespace kick_lite
