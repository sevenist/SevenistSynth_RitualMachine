#pragma once
// SynthLab's modules for the engine.
//   LabVoice: one per voice, it plays the Voice of the synth selected (lab.h). The engine gives it the voice's note, gate and velocity;
//             it hands them to the synth's start / release / render and writes the result as the voice's sound.
//   LabFx:    one, between the voices' sum and the master: the synth's Fx (lab.h), or the sum as it is when the synth has none.
//
// Their one parameter, LV_SYNTH, is the synth's index in lab::kSynths. Changing it builds the new synth's Voice (or Fx) in place (no
// allocation), takes the synth's default values and silences the voice. The values come as a blob (Engine::set_blob, a LabValues): `count` floats
// from index `first`, in the units the screen shows. Both travel in the engine's command queue, so a value sent after a synth change
// reaches the new synth.
#include <cstdint>
#include "engine/core/module.h"
#include "lab.h"

namespace lab {

constexpr int kTypeLabVoice = 120;      // type ids in the engine's registry (the library's own stop below 100; TechnoGen uses 112)
constexpr int kTypeLabFx = 121;
constexpr int LV_SYNTH = 0;

constexpr int kBlobValues = 63;         // values in one LabValues: the engine's command carries 256 bytes (kCmdBlobMax)

struct LabValues {
    uint8_t first = 0, count = 0;           // values first .. first + count - 1 (count <= kBlobValues; more: several blobs)
    uint8_t pad[2] = {};
    float v[kBlobValues] = {};
    size_t bytes() const { return 4 + sizeof(float) * count; }      // what to send: the header and the values in use
};
static_assert(sizeof(LabValues) <= 256, "a LabValues travels in one engine command (kCmdBlobMax)");

void register_lab_modules(sc::Registry &reg);

}  // namespace lab
