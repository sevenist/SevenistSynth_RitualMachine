// The input drivers' queue and axis scale (input_queue.h).
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "boards/esp32/input_queue.h"

namespace {
constexpr int kRing = 64;
input_event_t ring[kRing];
uint8_t head = 0, tail = 0;                    // guarded by lock
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
constexpr int kJoySnap = 16;                   // within this of the centre the axis reports exactly the centre
}  // namespace

void input_queue_push(int ctl, input_kind_t kind, int value) {
    portENTER_CRITICAL(&lock);
    bool merged = false;
    if (kind == IN_VALUE || kind == IN_DELTA) {
        for (uint8_t i = tail; i != head; i = (i + 1) % kRing) {
            if (ring[i].ctl != ctl || ring[i].kind != kind) continue;
            if (kind == IN_VALUE) ring[i].value = value; else ring[i].value += value;
            merged = true;
            break;
        }
    }
    const uint8_t next = (head + 1) % kRing;
    if (!merged && next != tail) { ring[head] = input_event_t{ctl, kind, value, false}; head = next; }   // full: drop
    portEXIT_CRITICAL(&lock);
}

int input_axis_value(int raw, int centre, bool invert) {
    int v = raw <= centre ? INPUT_AXIS_CENTER - (centre - raw) * INPUT_AXIS_CENTER / centre
                          : INPUT_AXIS_CENTER + (raw - centre) * (INPUT_VALUE_MAX - INPUT_AXIS_CENTER) / (4095 - centre);
    if (v < 0) v = 0;
    if (v > INPUT_VALUE_MAX) v = INPUT_VALUE_MAX;
    if (abs(v - INPUT_AXIS_CENTER) <= kJoySnap) v = INPUT_AXIS_CENTER;
    return invert ? INPUT_VALUE_MAX - v : v;
}

extern "C" bool input_pending(void) { return head != tail; }   // a racy read is fine: a hint for the redraw

extern "C" input_event_t input_poll(void) {
    input_event_t e{INPUT_NO_CONTROL, IN_NONE, 0, false};
    portENTER_CRITICAL(&lock);
    if (head != tail) { e = ring[tail]; tail = (tail + 1) % kRing; }
    portEXIT_CRITICAL(&lock);
    return e;
}
#endif
