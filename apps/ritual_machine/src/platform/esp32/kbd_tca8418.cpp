// TCA8418 keyboard driver for HWV1. Behaviour follows github.com/clement-chupin/SynthBox src/keyboard.cpp (which is proven on this hardware):
// CFG is written directly with only OVR_FLOW_M set (never K_LCK_EN), a watchdog re-applies the configuration if the chip lost it, and a
// run of failed reads triggers an I2C bus recovery (9 clock pulses + STOP). Differences: events are forwarded in FIFO order instead of
// being collapsed per poll, and the spare GPIO pins are left alone (SynthBox wrote its pull-up bits to 0x1A / 0x1C, which by the Adafruit
// library's register list are GPIO interrupt-enable registers; the pull-up registers are 0x2C-0x2E. Unverified against the datasheet).
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1)
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_TCA8418.h>
#include "platform/esp32/kbd_tca8418.h"
#include "platform/esp32/board_pins.h"

namespace {
constexpr uint8_t REG_CFG = 0x01, REG_INT_STAT = 0x02;
constexpr uint8_t CFG_OVR_FLOW_M = 0x04;       // FIFO wraps (newest overwrites oldest). 0x20 is K_LCK_EN: never set, it locks the keyboard.
constexpr uint8_t INT_OVR_FLOW = 0x08;
constexpr uint32_t kWatchdogMs = 500;
constexpr uint8_t kFailThreshold = 5;

Adafruit_TCA8418 tca;
bool present = false;
uint8_t fail_count = 0;
uint32_t last_watchdog = 0;
bool down[HW_KBD_ROWS][HW_KBD_COLS];

uint8_t read_reg(uint8_t reg) {
    Wire1.beginTransmission(HW_TCA_ADDR);
    Wire1.write(reg);
    Wire1.endTransmission(false);
    if (Wire1.requestFrom((uint8_t)HW_TCA_ADDR, (uint8_t)1) == 0) {
        if (fail_count < 255) fail_count++;
        return 0;
    }
    fail_count = 0;
    return Wire1.read();
}

void write_reg(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(HW_TCA_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

void apply_config() {
    tca.matrix(HW_KBD_ROWS, HW_KBD_COLS);
    tca.enableDebounce();                      // 4 stable scans, so events arrive clean
    write_reg(REG_CFG, CFG_OVR_FLOW_M);
}

void drain_fifo() {
    int n = tca.available();
    if (n > 10) n = 10;
    for (int i = 0; i < n; i++) tca.getEvent();
    write_reg(REG_INT_STAT, 0xFF);
}

// Releases a slave that holds SDA low, then restarts the bus and the chip.
void recover_bus() {
    Wire1.end();
    pinMode(PIN_KBD_SCL, OUTPUT);
    pinMode(PIN_KBD_SDA, INPUT_PULLUP);
    for (int i = 0; i < 9; i++) {
        digitalWrite(PIN_KBD_SCL, HIGH); delayMicroseconds(10);
        digitalWrite(PIN_KBD_SCL, LOW);  delayMicroseconds(10);
        if (digitalRead(PIN_KBD_SDA)) break;
    }
    pinMode(PIN_KBD_SDA, OUTPUT);              // STOP condition
    digitalWrite(PIN_KBD_SDA, LOW);  delayMicroseconds(10);
    digitalWrite(PIN_KBD_SCL, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_KBD_SDA, HIGH); delayMicroseconds(10);
    vTaskDelay(pdMS_TO_TICKS(20));
    Wire1.begin(PIN_KBD_SDA, PIN_KBD_SCL, HW_KBD_I2C_HZ);
    Wire1.setTimeout(3);
    vTaskDelay(pdMS_TO_TICKS(10));
    tca.begin(HW_TCA_ADDR, &Wire1);
    apply_config();
    drain_fifo();
    fail_count = 0;
}
}  // namespace

bool kbd_init(void) {
    Wire1.begin(PIN_KBD_SDA, PIN_KBD_SCL, HW_KBD_I2C_HZ);
    Wire1.setTimeout(3);
    delay(200);                                // pull-ups and the chip's supply settle
    for (int attempt = 0; attempt < 5 && !present; attempt++) {
        present = tca.begin(HW_TCA_ADDR, &Wire1);
        if (!present) delay(200);
    }
    if (!present) { Serial.println("[KBD] TCA8418 not found"); return false; }
    apply_config();
    drain_fifo();
    memset(down, 0, sizeof down);
    return true;
}

int kbd_read_events(kbd_event_t *out, int max) {
    if (!present) return 0;
    int n = 0;
    const uint32_t now = millis();

    if (fail_count >= kFailThreshold) {        // bus stuck: report every held key as released, then restart
        recover_bus();
        for (int r = 0; r < HW_KBD_ROWS; r++)
            for (int c = 0; c < HW_KBD_COLS; c++)
                if (down[r][c] && n < max) { down[r][c] = false; out[n++] = {(uint8_t)r, (uint8_t)c, false}; }
        return n;
    }
    if (now - last_watchdog >= kWatchdogMs) {  // the chip can lose its configuration after a supply glitch; CFG.OVR_FLOW_M is the canary
        last_watchdog = now;
        if ((read_reg(REG_CFG) & CFG_OVR_FLOW_M) == 0) apply_config();
    }
    if (read_reg(REG_INT_STAT) & INT_OVR_FLOW) write_reg(REG_INT_STAT, INT_OVR_FLOW);

    int avail = tca.available();
    if (avail > 10) avail = 10;
    for (int i = 0; i < avail && n < max; i++) {
        const int k = tca.getEvent();
        const bool press = (k & 0x80) != 0;
        const int code = (k & 0x7F) - 1;       // key number = row * 10 + column + 1
        const int row = code / 10, col = code % 10;
        if (code < 0 || row >= HW_KBD_ROWS || col >= HW_KBD_COLS) continue;
        if (down[row][col] == press) continue;
        down[row][col] = press;
        out[n++] = {(uint8_t)row, (uint8_t)col, press};
    }
    return n;
}
#endif
