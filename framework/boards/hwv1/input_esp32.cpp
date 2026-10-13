// ESP32 input driver. Reports the board's controls only (see hal/hal_input.h); what they do is the app's business.
//
// HWV1 (old prototype): a scan task on core 0 reads the TCA8418 keyboard (kbd_tca8418.cpp), the 7 relative knobs through the analog
// mux (mux_esp32.cpp) and the joystick, turns them into the controls of the board's table (hwv1_controls.h, the wiring: hwv1_layout.h)
// and pushes events into a ring buffer. input_poll() pops one event per call, so a slow screen refresh in loop() never delays the scan. The task also reads the
// mux's two other channels now and then: the battery and a spare input (scan_sensors).
//
// Other revisions: add an #elif block with their own driver; until then input_poll() reports nothing.
#if defined(ARDUINO_ARCH_ESP32)
#include "hal/hal_input.h"
#include "boards/hwv1/board_esp32.h"

#if defined(HWV1)
#include <Arduino.h>
#include <math.h>
#include "boards/hwv1/board_pins.h"
#include "boards/hwv1/hwv1_layout.h"
#include "boards/hwv1/kbd_tca8418.h"
#include "boards/hwv1/mux_esp32.h"

namespace {
// ---- event ring buffer (one producer: the scan task, one consumer: input_poll) ----
// Pending IN_VALUE events of a control are overwritten and pending IN_DELTA events are summed, so a stalled UI (a screen refresh takes tens of
// ms) never builds a backlog of stale knob positions: it gets the latest value / the total turn once. Presses and releases are never merged.
constexpr int kRing = 64;
input_event_t ring[kRing];
uint8_t head = 0, tail = 0;                    // guarded by lock
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

void push(int ctl, input_kind_t kind, int value) {
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

// ---- keyboard ----
void scan_keys() {
    kbd_event_t ev[8];
    const int n = kbd_read_events(ev, 8);
    for (int i = 0; i < n; i++) {
        int pr = HW_KBD_ROWS - 1 - ev[i].row;  // physical row from the top (TCA row 4 is the top row)
        int pc = ev[i].col;
        if (HWV1_FLIP_ROWS) pr = HW_KBD_ROWS - 1 - pr;
        if (HWV1_FLIP_COLS) pc = HW_KBD_COLS - 1 - pc;
        const int ctl = HWV1_KEY(pr, pc);
#ifdef HWV1_DEBUG_INPUT
        Serial.printf("[KEY] tca r%d c%d %s -> ctl %d\n", ev[i].row, ev[i].col, ev[i].pressed ? "down" : "up", (int)ctl);
#endif
        if (ctl != INPUT_NO_CONTROL) push(ctl, ev[i].pressed ? IN_PRESS : IN_RELEASE, 0);
    }
}

// ---- relative knobs: sin / cos pair on two mux channels, tracked by angle ----
// The angle is accumulated every scan (the noise of consecutive readings cancels in the sum: it telescopes to angle_now - angle_start), and a step is
// only reported once the knob has moved a whole quantum away from the last reported position. Jitter smaller than a quantum therefore never
// produces an event, and reversing direction needs a full quantum too (the hysteresis). Same idea as Quadrature::getDelta in
// github.com/sevenist/SevenistController-VTM-Firmware, which ignores accumulated deltas under 0.008 rad.
struct KnobState {
    float prev = 0;
    float moved = 0;                           // radians turned since the last reported step (signed, already multiplied by the knob's sign)
    int   value = 0;                           // position reading: the reported value (it starts where input_control_mode said)
    int   dir = 0;                             // position reading: the way of the last report (+1 / -1, 0 = none yet)
    uint32_t last_ms = 0;                      // ... and when it was
    bool  init = false;
};
KnobState knob[HWV1_KNOB_COUNT];
volatile uint8_t knob_position[HWV1_KNOB_COUNT];   // 1: the pot reports a position (IN_VALUE), 0: steps (IN_DELTA); set by the app (input_control_mode)
constexpr float kPi = 3.14159265f;
constexpr float kMinRadius = 80.0f;            // ADC counts from the centre; below this the pair is not a valid signal (knob not connected)
constexpr float kEncoderQuantum = 2 * kPi * HWV1_COUNTS_PER_DETENT / HWV1_COUNTS_PER_REV;     // radians per detent
constexpr float kAbsoluteQuantum = 2 * kPi * HWV1_ABS_STEP / INPUT_VALUE_MAX;                // radians per value step while moving
constexpr float kAbsoluteStart = 2 * kPi * HWV1_ABS_START / INPUT_VALUE_MAX;                 // radians to start or reverse (the hysteresis)

void scan_knobs() {
    for (int i = 0; i < HWV1_KNOB_COUNT; i++) {
        KnobState &k = knob[i];
        const float x = (float)mux_read(HWV1_MUX_KNOB_BASE + 2 * i) - 2048.0f;
        const float y = (float)mux_read(HWV1_MUX_KNOB_BASE + 2 * i + 1) - 2048.0f;
        if (sqrtf(x * x + y * y) < kMinRadius) continue;
        const float a = atan2f(y, x);
        if (!k.init) { k.prev = a; k.init = true; continue; }
        float d = a - k.prev;
        if (d > kPi) d -= 2 * kPi;
        if (d < -kPi) d += 2 * kPi;
        k.prev = a;
        k.moved += d * (float)kHwv1KnobSign[i];
        const bool enc = !knob_position[i];
        if (enc) {
            const int n = (int)(k.moved / kEncoderQuantum);    // whole quanta, truncated towards zero
            if (!n) continue;
            k.moved -= (float)n * kEncoderQuantum;
            push(HWV1_POT_0 + i, IN_DELTA, n);
            continue;
        }
        // A position: fine steps while the knob keeps turning one way, a larger threshold from rest or to reverse (noise stays under it).
        const uint32_t now = millis();
        const int way = k.moved > 0 ? 1 : -1;
        const bool moving = k.dir == way && now - k.last_ms < HWV1_ABS_IDLE_MS;
        if (fabsf(k.moved) < (moving ? kAbsoluteQuantum : kAbsoluteStart)) continue;
        const int n = (int)(k.moved / kAbsoluteQuantum);
        k.moved -= (float)n * kAbsoluteQuantum;
        k.dir = way; k.last_ms = now;
        int v = k.value + n * HWV1_ABS_STEP;
        if (v < 0) v = 0;
        if (v > INPUT_VALUE_MAX) v = INPUT_VALUE_MAX;
        if (v == k.value) { k.moved = 0; continue; }       // at an end stop: do not wind up, the way back starts at once
        k.value = v;
        push(HWV1_POT_0 + i, IN_VALUE, v);
    }
}

// ---- joystick: two ADC1 axes (centre measured at boot) and a push switch (its pushed level: HWV1_JOY_SW_PUSHED, hwv1_layout.h) ----
int joy_centre[2] = {2048, 2048};
int joy_last[2] = {INPUT_AXIS_CENTER, INPUT_AXIS_CENTER};
constexpr int kJoyMinStep = 12;                // axis counts (0..1023) before a new value is reported
constexpr int kJoySnap = 16;                   // within this of the centre the axis reports exactly the centre

int read_axis(int pin) {
    int sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(pin);
    return sum / 8;
}

void joystick_init() {
    pinMode(PIN_JOY_X, INPUT);
    pinMode(PIN_JOY_Y, INPUT);
    pinMode(PIN_JOY_SW, INPUT_PULLUP);
    analogReadResolution(12);
    delay(20);
    const int pins[2] = {PIN_JOY_X, PIN_JOY_Y};
    for (int a = 0; a < 2; a++) {
        int sum = 0;
        for (int i = 0; i < 16; i++) { sum += analogRead(pins[a]); delay(1); }
        const int c = sum / 16;
        if (c > 2048 - 192 && c < 2048 + 192) joy_centre[a] = c;
        else Serial.printf("[JOY] axis %d rests at %d (expected ~2048): joystick damaged or held at boot, using 2048\n", a, c);
    }
}

int scale_axis(int raw, int centre, bool invert) {
    int v = raw <= centre ? INPUT_AXIS_CENTER - (centre - raw) * INPUT_AXIS_CENTER / centre
                          : INPUT_AXIS_CENTER + (raw - centre) * (INPUT_VALUE_MAX - INPUT_AXIS_CENTER) / (4095 - centre);
    if (v < 0) v = 0;
    if (v > INPUT_VALUE_MAX) v = INPUT_VALUE_MAX;
    if (abs(v - INPUT_AXIS_CENTER) <= kJoySnap) v = INPUT_AXIS_CENTER;
    return invert ? INPUT_VALUE_MAX - v : v;
}

void scan_joystick() {
    static const int pins[2] = {PIN_JOY_X, PIN_JOY_Y};
    static const int ctl[2] = {HWV1_JOY_X, HWV1_JOY_Y};
    const bool inv[2] = {HWV1_JOY_INVERT_X != 0, HWV1_JOY_INVERT_Y != 0};
    for (int a = 0; a < 2; a++) {
        const int v = scale_axis(read_axis(pins[a]), joy_centre[a], inv[a]);
        const bool centred = v == INPUT_AXIS_CENTER && joy_last[a] != INPUT_AXIS_CENTER;
        if (abs(v - joy_last[a]) >= kJoyMinStep || centred) { joy_last[a] = v; push(ctl[a], IN_VALUE, v); }
    }
    static bool sw = false;
    static uint8_t stable = 0;
    const bool now = digitalRead(PIN_JOY_SW) == (HWV1_JOY_SW_PUSHED ? HIGH : LOW);
    if (now == sw) stable = 0;
    else if (++stable >= 2) { sw = now; stable = 0; push(HWV1_JOY_SW, sw ? IN_PRESS : IN_RELEASE, 0); }   // 2 scans of the new level
}

// ---- the mux's two other channels: the battery (board_esp32.cpp keeps its level) and a spare input, read as a button ----
volatile uint16_t aux_raw = 0;
volatile bool aux_pushed = false;

void scan_sensors() {
    const uint16_t mv = mux_read_mv(HWV1_MUX_BATTERY);
    board_battery_sample(mux_read(HWV1_MUX_BATTERY), mv);
    static uint8_t stable = 0;
    const uint16_t raw = mux_read(HWV1_MUX_AUX);
    const bool now = (raw > 2048) == (HWV1_AUX_PUSHED != 0);
    aux_raw = raw;
    if (now == aux_pushed) stable = 0;
    else if (++stable >= 2) { aux_pushed = now; stable = 0; }     // 2 reads of the new level (not a control of the board's table yet: nothing is reported)
}

void scan_task(void *) {
    TickType_t wake = xTaskGetTickCount();
    uint32_t tick = 0;
    for (;;) {
        scan_keys();                           // the TCA8418 debounces in hardware: one FIFO read per key period
        if ((tick % (HWV1_SENSOR_PERIOD_MS / HWV1_KEY_PERIOD_MS)) == 0) scan_sensors();
        if ((tick++ % (HWV1_ANALOG_PERIOD_MS / HWV1_KEY_PERIOD_MS)) == 0) { scan_knobs(); scan_joystick(); }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(HWV1_KEY_PERIOD_MS));
    }
}
}  // namespace

void input_esp32_init(void) {
    mux_init();
    joystick_init();
    kbd_init();                                // on failure the keys are dead but the knobs and the joystick still work
    xTaskCreatePinnedToCore(scan_task, "input", 4096, nullptr, 8, nullptr, 0);
}

int input_esp32_aux_raw(void) { return aux_raw; }
bool input_esp32_aux_pushed(void) { return aux_pushed; }

extern "C" const input_control_t *input_controls(int *count) { if (count) *count = HWV1_CONTROL_COUNT; return kHwv1Controls; }
extern "C" const char *input_board_name(void) { return "hwv1"; }
// Read by the scan task: a byte per pot, and the position's start goes in before the pot's first move.
extern "C" void input_control_mode(int ctl, input_kind_t reading, int start) {
    if (ctl < HWV1_POT_0 || ctl >= HWV1_POT_0 + HWV1_KNOB_COUNT) return;
    const int i = ctl - HWV1_POT_0;
    if (reading == IN_VALUE) knob[i].value = start < 0 ? 0 : start > INPUT_VALUE_MAX ? INPUT_VALUE_MAX : start;
    knob_position[i] = reading == IN_VALUE;
}
extern "C" bool input_boot_reset(void) { return false; }   // the power-on key read was reverted (2026-10-07): the app's own check (F1 within 4 s of start, held 2 s) is the reset

extern "C" bool input_pending(void) { return head != tail; }   // a racy read is fine: a hint for the redraw

extern "C" input_event_t input_poll(void) {
    input_event_t e{INPUT_NO_CONTROL, IN_NONE, 0, false};
    portENTER_CRITICAL(&lock);
    if (head != tail) { e = ring[tail]; tail = (tail + 1) % kRing; }
    portEXIT_CRITICAL(&lock);
    return e;
}

#else  // another hardware revision: no driver yet
void input_esp32_init(void) {}
int input_esp32_aux_raw(void) { return 0; }
bool input_esp32_aux_pushed(void) { return false; }
extern "C" input_event_t input_poll(void) { return input_event_t{INPUT_NO_CONTROL, IN_NONE, 0, false}; }
extern "C" bool input_pending(void) { return false; }
extern "C" const input_control_t *input_controls(int *count) { if (count) *count = 0; return nullptr; }
extern "C" const char *input_board_name(void) { return "none"; }
extern "C" void input_control_mode(int, input_kind_t, int) {}
extern "C" bool input_boot_reset(void) { return false; }
#endif // HWV1
#endif // ARDUINO_ARCH_ESP32
