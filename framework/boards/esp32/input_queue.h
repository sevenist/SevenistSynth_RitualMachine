#pragma once
// What the ESP32 boards' input drivers share: the queue of events between their scan task and input_poll (hal/hal_input.h), which this
// module implements, and a joystick axis's scale.
#if defined(ARDUINO_ARCH_ESP32)
#include "hal/hal_input.h"

// One producer: the scan task. Pending IN_VALUE events of a control are overwritten and pending IN_DELTA events are summed, so a stalled UI (a screen
// refresh takes tens of ms) never builds a backlog of stale knob positions: it gets the latest value / the total turn once. Presses and releases are
// never merged.
void input_queue_push(int ctl, input_kind_t kind, int value);

// A raw 12 bit reading as an axis's value 0..INPUT_VALUE_MAX: `centre` (measured at boot) gives INPUT_AXIS_CENTER, and near it the axis reports exactly that.
int input_axis_value(int raw, int centre, bool invert);
#endif
