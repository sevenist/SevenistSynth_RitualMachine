#pragma once
// Motion sequencer (the MS rack module): up to four lanes of 16 steps, each lane a value 0..100 per step that may be
// active or not. It outputs one control signal per lane, so "motion" is just a cable into any parameter.
//
// Clock: it runs its own sample-accurate clock but uses the note sequencer's timing rules exactly (integer-millisecond
// step = 15000 / bpm, swing delays odd steps by swing % of a step), so both stay in step. It is started by the MSP_RESTART
// counter changing (the application bumps it when the note sequencer starts) and is silent while MSP_RUN = 0.
//
// Lane model (DEVELOPING.md): interpolation is always between ACTIVE steps. At position p (in steps, fractional)
//   prev = the last active step at or before floor(p), wrapping;  next = the first active step after it, wrapping.
//   STEP:   value of prev.           LINEAR: lerp prev -> next by (p - prev) / (next - prev), distances modulo the step count.
//   One active step = constant; no active step (within the first `steps` steps) = the lane outputs 0.
// Output: bipolar lanes map 0..100 to -1..+1, unipolar lanes to 0..+1 (q15). Inside a block the output ramps linearly
// between the value at the block start and at the block end, so a step edge never clicks.
#include "engine/core/command_queue.h"
#include "engine/core/module.h"

namespace sc {

constexpr int kMsLanes = 4;
constexpr int kMsSteps = 16;

struct MotionBlob {
    uint8_t val[kMsLanes][kMsSteps];     // 0..100
    uint16_t active[kMsLanes];           // bit i = step i is active
    uint8_t linear[kMsLanes];            // 0 = STEP, 1 = LINEAR
    uint8_t bipolar[kMsLanes];           // 0 = unipolar, 1 = bipolar
};
static_assert(sizeof(MotionBlob) <= kCmdBlobMax, "the lanes must fit in one command");

// MotionSeq: out 0..3 = lane 1..4.   MSP_BPM 40..240, MSP_STEPS 1..16, MSP_SWING 0..50 (%).
enum { MSP_RUN, MSP_BPM, MSP_STEPS, MSP_SWING, MSP_RESTART, MSP_N };

// The lane value (0..65536 * 100 fixed point is avoided: returns value * 65536, i.e. Q16 in 0..100) at a point of the
// pattern; exposed for the tests, which compare the module against it. `pos_q16` is the position in steps (Q16).
int64_t motion_lane_value_q16(const MotionBlob &b, int lane, int steps, uint32_t pos_q16);

void register_motion_module(Registry &reg);
constexpr int T_MSEQ = 58;

}  // namespace sc
