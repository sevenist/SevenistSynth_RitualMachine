// The second prototype's input driver. Reports the board's controls only (see hal/hal_input.h); what they do is the app's business.
//
// A scan task on core 0 reads the key scanner on I2C (U14, a CH32V006: the 20 keys and the 2 encoders with their pushes) and, through the analog mux,
// the 8 pots, the joystick and the 4 buttons, and turns them into the controls of the board's table (hwv2_controls.h; the wiring: hwv2_layout.h).
// Every HWV2_SENSOR_PERIOD_MS it also reads the power switch and the battery on channel 7, for board_esp32.cpp: the mux is this task's alone.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV2)
#include <Arduino.h>
#include <Wire.h>
#include "hal/hal_input.h"
#include "boards/esp32/board_esp32.h"
#include "boards/esp32/board_pins.h"
#include "boards/esp32/input_queue.h"
#include "boards/esp32/mux_esp32.h"
#include "boards/hwv2/hwv2_layout.h"

namespace {
// ---- the scanner: keys, encoders, their pushes ----
const int kEncoderControl[2] = {HWV2_ENC_A, HWV2_ENC_B};
const int kEncoderSign[2] = {HWV2_ENC_A_SIGN, HWV2_ENC_B_SIGN};
bool scanner_seen = false;
int scanner_failures = 0;
uint32_t scanner_inputs = 0;
uint8_t scanner_counts[2];
int encoder_counts[2];                         // quarter steps not reported yet
bool scanner_warned = false;

bool scanner_read(uint8_t *frame) {
    if (Wire.requestFrom((uint8_t)HW_SCANNER_ADDR, (size_t)HWV2_SCANNER_FRAME) != HWV2_SCANNER_FRAME) return false;
    uint8_t check = 0xA5;
    for (int i = 0; i < HWV2_SCANNER_FRAME; i++) {
        frame[i] = (uint8_t)Wire.read();
        check ^= frame[i];
    }
    return check == 0;
}

void report_input(int bit, bool pressed) {
    const int ctl = hwv2_scanner_control(bit);
#ifdef HWV1_DEBUG_INPUT
    Serial.printf("[KEY] scanner bit %d %s -> ctl %d\n", bit, pressed ? "down" : "up", ctl);
#endif
    if (ctl != INPUT_NO_CONTROL) input_queue_push(ctl, pressed ? IN_PRESS : IN_RELEASE, 0);
}

// The scanner went away with keys held: they are let go, or their notes would ring on.
void scanner_lost() {
    Serial.printf("[KBD] the key scanner does not answer at 0x%02X\n", HW_SCANNER_ADDR);
    for (int bit = 0; bit < HWV2_SCANNER_INPUTS; bit++) if (scanner_inputs >> bit & 1) report_input(bit, false);
    scanner_inputs = 0;
    scanner_seen = false;
}

void scan_scanner() {
    uint8_t frame[HWV2_SCANNER_FRAME];
    if (!scanner_read(frame)) {
        if (scanner_failures < HWV2_SCANNER_LOST && ++scanner_failures == HWV2_SCANNER_LOST && (scanner_seen || !scanner_warned)) { scanner_warned = true; scanner_lost(); }
        return;
    }
    scanner_failures = 0;
    const uint32_t inputs = frame[0] | (uint32_t)frame[1] << 8 | (uint32_t)frame[2] << 16;
    if (!scanner_seen) {                       // the first frame: its counts start anywhere, and what is held now has made no press
        scanner_seen = true;
        scanner_inputs = inputs;
        scanner_counts[0] = frame[3];
        scanner_counts[1] = frame[4];
        Serial.println("[KBD] key scanner found");
    }
    for (int bit = 0; bit < HWV2_SCANNER_INPUTS; bit++)
        if ((inputs ^ scanner_inputs) >> bit & 1) report_input(bit, inputs >> bit & 1);
    scanner_inputs = inputs;
    for (int e = 0; e < 2; e++) {
        encoder_counts[e] += (int8_t)(frame[3 + e] - scanner_counts[e]) * kEncoderSign[e];   // (free running counters: the difference survives their wrap)
        scanner_counts[e] = frame[3 + e];
        const int steps = encoder_counts[e] / HWV2_ENC_COUNTS_PER_DETENT;
        if (steps) {
            encoder_counts[e] -= steps * HWV2_ENC_COUNTS_PER_DETENT;
            input_queue_push(kEncoderControl[e], IN_DELTA, steps);
        }
    }
}

// ---- the pots ----
struct Knob {
    int ctl;
    uint8_t channel;
    float smooth;
    int value;                                 // the value last reported (at the start: where the knob is)
    int dir;                                   // the way of the last report (+1 / -1, 0 = none yet)
    uint32_t last_ms;                          // ... and when it was
};
Knob knobs[] = {
    {HWV2_VOLUME,     HWV2_MUX_VOLUME, 0, 0, 0, 0},
    {HWV2_COL_KNOB_0, HWV2_MUX_K1,     0, 0, 0, 0},
    {HWV2_COL_KNOB_1, HWV2_MUX_K2,     0, 0, 0, 0},
    {HWV2_COL_KNOB_2, HWV2_MUX_K3,     0, 0, 0, 0},
    {HWV2_COL_KNOB_3, HWV2_MUX_K4,     0, 0, 0, 0},
    {HWV2_KNOB_R1,    HWV2_MUX_R1,     0, 0, 0, 0},
    {HWV2_KNOB_R2,    HWV2_MUX_R2,     0, 0, 0, 0},
    {HWV2_KNOB_R3,    HWV2_MUX_R3,     0, 0, 0, 0},
};

void knobs_init() {
    for (Knob &k : knobs) {
        k.smooth = (float)mux_read_settled(k.channel, 4);
        k.value = hwv2_knob_value((int)k.smooth);
    }
}

// A knob reports only when it is moved: where it rests says nothing to the app until then (a value it holds, or a knob to catch).
void scan_knobs() {
    const uint32_t now = millis();
    for (Knob &k : knobs) {
        k.smooth += ((float)mux_read_settled(k.channel, 4) - k.smooth) * 0.25f;
        const int v = hwv2_knob_value((int)k.smooth);
        const int d = v - k.value;
        if (!d) continue;
        const int way = d > 0 ? 1 : -1;
        const bool moving = k.dir == way && now - k.last_ms < HWV2_KNOB_IDLE_MS;
        if (abs(d) < (moving ? HWV2_KNOB_STEP : HWV2_KNOB_START) && v != 0 && v != INPUT_VALUE_MAX) continue;   // (the ends are always reached)
        k.value = v;
        k.dir = way;
        k.last_ms = now;
        input_queue_push(k.ctl, IN_VALUE, v);
    }
}

// ---- the joystick's axes ----
const uint8_t kAxisChannel[2] = {HWV2_MUX_JOY_X, HWV2_MUX_JOY_Y};
const int kAxisControl[2] = {HWV2_JOY_X, HWV2_JOY_Y};
const bool kAxisInvert[2] = {HWV2_JOY_INVERT_X != 0, HWV2_JOY_INVERT_Y != 0};
int joy_centre[2] = {2048, 2048};
int joy_last[2] = {INPUT_AXIS_CENTER, INPUT_AXIS_CENTER};
constexpr int kJoyMinStep = 12;                // axis counts (0..1023) before a new value is reported

void joystick_init() {
    for (int a = 0; a < 2; a++) {
        int sum = 0;
        for (int i = 0; i < 16; i++) { sum += mux_read_settled(kAxisChannel[a], 1); delay(1); }
        const int c = sum / 16;
        if (c > 2048 - 192 && c < 2048 + 192) joy_centre[a] = c;
        else Serial.printf("[JOY] axis %d rests at %d (expected ~2048): joystick damaged or held at boot, using 2048\n", a, c);
    }
}

void scan_joystick() {
    for (int a = 0; a < 2; a++) {
        const int v = input_axis_value(mux_read_settled(kAxisChannel[a], 4), joy_centre[a], kAxisInvert[a]);
        const bool centred = v == INPUT_AXIS_CENTER && joy_last[a] != INPUT_AXIS_CENTER;
        if (abs(v - joy_last[a]) >= kJoyMinStep || centred) { joy_last[a] = v; input_queue_push(kAxisControl[a], IN_VALUE, v); }
    }
}

// ---- the buttons on the mux: high = pushed ----
// A button counts once it has been seen up. On rev A, SW19, SW21 and SW22 read stuck high until they get their bodge (REV-B-NOTES.md, bodge 1), and a Shift or a
// Menu held for ever would make the whole UI useless: so one that never lets go stays silent, and the log says which.
struct Button {
    int ctl;
    uint8_t channel;
    const char *name;
    bool down;
    uint8_t stable;
    bool seen_up;
    uint16_t raw;                              // the last reading (dev: the log below)
};
Button buttons[] = {
    {HWV2_PLAY,   HWV2_MUX_SW23,   "PLAY (SW23)", false, 0, false},
    {HWV2_BTN_1,  HWV2_MUX_SW22,   "B1 (SW22)",   false, 0, false},
    {HWV2_BTN_2,  HWV2_MUX_SW19,   "B2 (SW19)",   false, 0, false},
    {HWV2_BTN_3,  HWV2_MUX_SW21,   "B3 (SW21)",   false, 0, false},
    {HWV2_JOY_SW, HWV2_MUX_JOY_SW, "JOY PUSH",    false, 0, false},
};

void buttons_init() {
    for (Button &b : buttons) {
        b.seen_up = mux_read_settled(b.channel, 1) <= 2048;
        if (!b.seen_up) Serial.printf("[INPUT] %s reads pushed at the start: ignored until it is seen up (REV-B-NOTES.md, bodge 1)\n", b.name);
    }
}

void scan_buttons() {
    bool changed = false;
    for (Button &b : buttons) {
        b.raw = mux_read_settled(b.channel, 1);
        const bool now = b.raw > 2048;
        if (!b.seen_up) {
            if (now) continue;
            b.seen_up = true;
            Serial.printf("[INPUT] %s is up: it counts from now on\n", b.name);
        }
        if (now == b.down) { b.stable = 0; continue; }
        if (++b.stable >= 2) {                 // 2 scans of the new level
            b.down = now;
            b.stable = 0;
            changed = true;
            input_queue_push(b.ctl, now ? IN_PRESS : IN_RELEASE, 0);
        }
    }
#ifdef DEV_SERIAL_CMD
    if (changed) {                             // dev: every button's raw reading at each change, to tell a stuck, floating or shorted line from the app
        Serial.print("[INPUT]");
        for (const Button &b : buttons) Serial.printf(" %s %s %u |", b.name, b.down ? "DOWN" : "up", (unsigned)b.raw);
        Serial.println();
    }
#endif
}

// ---- the power switch and the battery: one reading of channel 7 (board_esp32.cpp tells the switch and keeps the battery's level) ----
void scan_sensors() {
    const uint16_t mv = mux_read_mv(HWV2_MUX_PWR);
    board_battery_sample(mux_read(HWV2_MUX_PWR), mv);
}

void scan_task(void *) {
    TickType_t wake = xTaskGetTickCount();
    uint32_t tick = 0;
    for (;;) {
        scan_scanner();
        if ((tick % (HWV2_SENSOR_PERIOD_MS / HWV2_KEY_PERIOD_MS)) == 0) scan_sensors();
        if ((tick++ % (HWV2_ANALOG_PERIOD_MS / HWV2_KEY_PERIOD_MS)) == 0) { scan_knobs(); scan_joystick(); scan_buttons(); }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(HWV2_KEY_PERIOD_MS));
    }
}
}  // namespace

void input_esp32_init(void) {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, HW_I2C_HZ);
    Wire.setTimeOut(10);                       // a bus that hangs must not hold up the pots
    mux_init();
    knobs_init();
    joystick_init();
    buttons_init();
    xTaskCreatePinnedToCore(scan_task, "input", 4096, nullptr, 8, nullptr, 0);
}

int input_esp32_aux_raw(void) { return 0; }
bool input_esp32_aux_pushed(void) { return false; }

extern "C" const input_control_t *input_controls(int *count) { if (count) *count = HWV2_CONTROL_COUNT; return kHwv2Controls; }
extern "C" const char *input_board_name(void) { return "hwv2"; }
extern "C" void input_control_mode(int, input_kind_t, int) {}   // no endless pots on this board
extern "C" bool input_boot_reset(void) { return false; }
#endif
