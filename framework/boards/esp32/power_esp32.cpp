// The soft power the ESP32 boards share (board_esp32.h; what each board gives it: power_esp32.h).
//
// PWR_ON_EN high keeps the supply on once the firmware runs, so it can finish (save) before the board goes off. The power switch says whether the
// machine is on. The USB cable feeds the board whatever the switch says: a board that runs switched off is on the cable, and becomes the card reader of
// its TF card (HW_USB_STORAGE, board_pins.h) until the switch is turned on, or runs the synth with the switch ignored until it is next used.
#if defined(ARDUINO_ARCH_ESP32) && (defined(HWV1) || defined(HWV2))
#include <Arduino.h>
#include <esp_ota_ops.h>
#include "hal/hal_power.h"
#include "boards/esp32/board_esp32.h"
#include "boards/esp32/board_pins.h"
#include "boards/esp32/power_esp32.h"
#ifndef HWV1_CARD_READER
#include "boards/esp32/leds_esp32.h"
#endif

namespace {
bool g_button_ignored = false;                 // the synth runs switched off (on the cable): the switch counts again once it was seen on
constexpr int kOffSettleMs = 300;              // after the latch was let go: a board on the battery alone has stopped by then

// The LEDs dark before the synth lets go of the board: chain LEDs the cable keeps powered would keep their colours, and start with them next time.
// (The card reader's own firmware has no LED driver: it never lights them.)
void leds_dark() {
#ifndef HWV1_CARD_READER
    leds_esp32_blank();
#endif
}
}  // namespace

void board_power_init(void) {
    pinMode(PIN_PWR_ON_EN, OUTPUT);
    digitalWrite(PIN_PWR_ON_EN, HIGH);
    power_switch_init();
}

// The serial console is the chip's USB-Serial/JTAG port, which counts the frames a host sends every millisecond.
bool board_usb_host(void) {
#if ARDUINO_USB_MODE
    return HWCDC::isPlugged();
#else
    return false;
#endif
}

bool board_power_started_off(void) {
    for (int i = 0; i < 25; i++) {             // on for 50 ms without a bounce
        if (power_switch_direct()) return false;
        delay(2);
    }
    digitalWrite(PIN_PWR_ON_EN, LOW);          // the machine is off: on the battery alone the board stops here
    delay(kOffSettleMs);
    if (HW_USB_STORAGE && !power_joystick_held()) return true;   // still alive: on the cable
    g_button_ignored = true;                   // no card reader at the start, or the joystick held: the synth all the same
    Serial.println("[BOARD] the power switch reads off: the synth runs on the cable, the switch counts once it is seen on");
    return false;
}

bool board_power_poll(void) {
    static uint32_t last = 0;
    static bool told = false;
    const uint32_t now = millis();
    if (now - last < 500) return false;
    last = now;
    if (board_power_button_on()) {
        if (g_button_ignored) { g_button_ignored = false; digitalWrite(PIN_PWR_ON_EN, HIGH); }   // used again: it counts from here
        told = false;
        return false;
    }
    if (g_button_ignored || told) return false;
    delay(50);
    if (board_power_button_on()) return false;
    told = true;
    return true;
}

void board_power_hold(bool on) { digitalWrite(PIN_PWR_ON_EN, on ? HIGH : LOW); }

void board_power_off(void) {
    board_audio_off();                         // no thump in the speaker
    leds_dark();
    digitalWrite(PIN_PWR_ON_EN, LOW);
    delay(kOffSettleMs);
#if HW_USB_STORAGE
    board_card_reader();                       // still alive: the cable feeds the board
#endif
    g_button_ignored = true;                   // no card reader: the synth goes on until the switch is used again
    Serial.println("[BOARD] switched off, but the cable feeds the board: the synth goes on");
    power_audio_resume();
}

bool board_boot_slot(bool card_reader) {
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, card_reader ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    return p && esp_ota_set_boot_partition(p) == ESP_OK;      // (it checks the image first: an empty slot is refused)
}

void board_card_reader(void) {
    if (board_boot_slot(true)) { leds_dark(); ESP.restart(); }   // (the serial command `storage` comes here too, with the LEDs lit)
    Serial.println("[BOARD] no card reader firmware in the second slot (.\\upload.ps1 -Env " HW_CARD_READER_ENV "): the synth goes on");
    g_button_ignored = true;
}

// ---- the battery (hal/hal_power.h): the divider's voltage, smoothed over about 1.6 s (a sample every 100 ms from the input task or the card reader) ----
// Below HW_BATT_PRESENT_MV (no battery, or one the switch cuts off) none is measured; above it again, the smoothing starts over from the first reading.
namespace {
constexpr int kBattSettle = 8;                 // readings before a level is reported
volatile int g_batt_mv = 0, g_batt_raw = 0, g_batt_pin_mv = 0;
volatile int g_batt_n = 0;
}  // namespace

void board_battery_sample(int raw, int pin_mv) {
    static int acc = 0;                        // 16 x the smoothed millivolts
    power_sense_sample(pin_mv);
    g_batt_raw = raw;
    g_batt_pin_mv = pin_mv;
    const int mv = pin_mv * HW_BATT_FULL_SCALE_MV / 3300;
    if (mv < HW_BATT_PRESENT_MV) { g_batt_n = 0; g_batt_mv = 0; return; }
    if (g_batt_n == 0) acc = mv * 16; else acc += mv - acc / 16;
    g_batt_mv = acc / 16;
    if (g_batt_n < kBattSettle) g_batt_n = g_batt_n + 1;
}

void board_battery_debug(int *raw, int *pin_mv) { *raw = g_batt_raw; *pin_mv = g_batt_pin_mv; }

extern "C" int power_battery_mv(void) { return g_batt_n >= kBattSettle ? g_batt_mv : 0; }

extern "C" int power_battery_percent(void) {
    const int mv = power_battery_mv();
    if (!mv) return -1;
    const int pct = (mv - HW_BATT_EMPTY_MV) * 100 / (HW_BATT_FULL_MV - HW_BATT_EMPTY_MV);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}
#endif
