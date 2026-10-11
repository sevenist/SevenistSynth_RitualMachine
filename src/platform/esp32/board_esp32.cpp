#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "hal/hal_power.h"
#include "platform/esp32/board_esp32.h"
#include "platform/esp32/board_pins.h"

#if defined(HWV1)
#include <esp_ota_ops.h>
#include "platform/esp32/hwv1_layout.h"

// 1 = a board that runs with its power button up (so: fed by the USB cable) starts the card reader (usb_storage_esp32.cpp).
// 0 = it runs the synth, as before 2026-10-11.
#ifndef HWV1_USB_STORAGE
#define HWV1_USB_STORAGE 1
#endif

namespace {
bool g_button_ignored = false;                 // the synth runs with the button up (on the cable): the button counts again once it was seen down
bool g_speaker_on = true;                      // what board_speaker_on was last told (board_audio_off does not change it)
bool g_audio_open = false;                     // board_audio_open was called: the start is over, the DAC is unmuted
constexpr int kOffSettleMs = 300;              // after the latch was let go: a board on the battery alone has stopped by then

bool joystick_held() {
    pinMode(PIN_JOY_SW, INPUT_PULLUP);
    delay(2);
    return digitalRead(PIN_JOY_SW) == (HWV1_JOY_SW_PUSHED ? HIGH : LOW);
}
}  // namespace

// Soft power: PWR_ON_EN HIGH holds the supply on. The power button is a latching switch whose position reads on PWR_SENSE (board_pins.h).
void board_power_init(void) {
    pinMode(PIN_PWR_ON_EN, OUTPUT);
    digitalWrite(PIN_PWR_ON_EN, HIGH);
    pinMode(PIN_PWR_SENSE, INPUT);
}

bool board_power_button_on(void) { return digitalRead(PIN_PWR_SENSE) == (HW_PWR_SENSE_ON ? HIGH : LOW); }

// The serial console is the chip's USB-Serial/JTAG port, which counts the frames a host sends every millisecond.
bool board_usb_host(void) {
#if ARDUINO_USB_MODE
    return HWCDC::isPlugged();
#else
    return false;
#endif
}

bool board_power_started_off(void) {
    for (int i = 0; i < 25; i++) {             // up for 50 ms without a bounce
        if (board_power_button_on()) return false;
        delay(2);
    }
    digitalWrite(PIN_PWR_ON_EN, LOW);          // the machine is off: on the battery alone the board stops here
    delay(kOffSettleMs);
    if (HWV1_USB_STORAGE && !joystick_held()) return true;   // still alive: on the cable
    g_button_ignored = true;                   // the joystick held at the start: the synth all the same
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
    digitalWrite(PIN_PWR_ON_EN, LOW);
    delay(kOffSettleMs);
#if HWV1_USB_STORAGE
    board_card_reader();                       // still alive: the cable feeds the board
#endif
    g_button_ignored = true;                   // no card reader: the synth goes on until the button is used again
    if (g_audio_open) digitalWrite(PIN_I2S_XSMT, HIGH);
    board_speaker_on(g_speaker_on);
}

bool board_boot_slot(bool card_reader) {
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, card_reader ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    return p && esp_ota_set_boot_partition(p) == ESP_OK;      // (it checks the image first: an empty slot is refused)
}

void board_card_reader(void) {
    if (board_boot_slot(true)) ESP.restart();
    Serial.println("[BOARD] no card reader firmware in the second slot (pio run -e card_reader -t upload): the synth goes on");
    g_button_ignored = true;
}

// MAX98357A SD_MODE is not a plain enable: its voltage picks the channel (about 3.3 V = left only, about 1 V = (L+R)/2, low = shutdown), and
// the board's resistors set that level. SynthBox's firmware never drives the pin ("let pull-up handle it" in GrvEP, "pull-down" in Ritual
// Machine), so the default here is to leave it floating. HWV1_SPK_SD_MODE=1 drives it HIGH instead (left channel only).
#ifndef HWV1_SPK_SD_MODE
#define HWV1_SPK_SD_MODE 0
#endif

// The DAC stays muted here: board_audio_open unmutes it when the loading screen has ended.
void board_audio_enable(void) {
    pinMode(PIN_I2S_XSMT, OUTPUT);
    digitalWrite(PIN_I2S_XSMT, LOW);           // PCM5102A soft mute on
    pinMode(PIN_SPK_SD, INPUT);                // first look at what the board sets by itself
    delay(5);
    const int floating = digitalRead(PIN_SPK_SD);
#if HWV1_SPK_SD_MODE == 1
    pinMode(PIN_SPK_SD, OUTPUT);
    digitalWrite(PIN_SPK_SD, HIGH);
#endif
    Serial.printf("[BOARD] XSMT (GPIO %d) muted until the start is over; SPK_SD (GPIO %d) floating reads %d, mode %d (0 = left floating, 1 = driven HIGH)\n", PIN_I2S_XSMT,
                  PIN_SPK_SD, floating, HWV1_SPK_SD_MODE);
}

#ifndef HWV1_CARD_READER
void audio_esp32_open(void);                   // audio_esp32.cpp: the audio task's own gate

void board_audio_open(void) {
    if (g_audio_open) return;
    g_audio_open = true;
    digitalWrite(PIN_I2S_XSMT, HIGH);          // PCM5102A soft mute off
    audio_esp32_open();
}
#endif

void board_audio_off(void) {
    pinMode(PIN_I2S_XSMT, OUTPUT);
    digitalWrite(PIN_I2S_XSMT, LOW);           // PCM5102A soft mute
    pinMode(PIN_SPK_SD, OUTPUT);
    digitalWrite(PIN_SPK_SD, LOW);             // amplifier shutdown
}

// Off drives SD_MODE low (amplifier shutdown: silent, no hiss, the headphones keep playing); on gives the pin back to its boot setting.
void board_speaker_on(bool on) {
    g_speaker_on = on;
    if (!on) { pinMode(PIN_SPK_SD, OUTPUT); digitalWrite(PIN_SPK_SD, LOW); return; }
#if HWV1_SPK_SD_MODE == 1
    pinMode(PIN_SPK_SD, OUTPUT);
    digitalWrite(PIN_SPK_SD, HIGH);
#else
    pinMode(PIN_SPK_SD, INPUT);
#endif
}

// ---- battery (hal/hal_power.h): the divider's voltage, smoothed over about 1.6 s (a sample every HWV1_SENSOR_PERIOD_MS) ----
namespace {
constexpr int kBattSettle = 8;                 // readings before a level is reported
volatile int g_batt_mv = 0, g_batt_raw = 0, g_batt_pin_mv = 0;
volatile int g_batt_n = 0;
}  // namespace

void board_battery_sample(int raw, int pin_mv) {
    static int acc = 0;                        // 16 x the smoothed millivolts
    const int mv = pin_mv * HW_BATT_FULL_SCALE_MV / 3300;
    g_batt_raw = raw;
    g_batt_pin_mv = pin_mv;
    if (g_batt_n == 0) acc = mv * 16; else acc += mv - acc / 16;
    g_batt_mv = acc / 16;
    if (g_batt_n < kBattSettle) g_batt_n = g_batt_n + 1;
}

void board_battery_debug(int *raw, int *pin_mv) { *raw = g_batt_raw; *pin_mv = g_batt_pin_mv; }

extern "C" int power_battery_mv(void) { return g_batt_n >= kBattSettle && g_batt_mv >= HW_BATT_PRESENT_MV ? g_batt_mv : 0; }

extern "C" int power_battery_percent(void) {
    const int mv = power_battery_mv();
    if (!mv) return -1;
    const int pct = (mv - HW_BATT_EMPTY_MV) * 100 / (HW_BATT_FULL_MV - HW_BATT_EMPTY_MV);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}
#else
void board_power_init(void) {}
void board_audio_enable(void) {}
void board_audio_open(void) {}
void board_audio_off(void) {}
void board_speaker_on(bool) {}
bool board_power_button_on(void) { return true; }
bool board_usb_host(void) { return false; }
bool board_power_started_off(void) { return false; }
bool board_power_poll(void) { return false; }
void board_power_hold(bool) {}
void board_power_off(void) {}
bool board_boot_slot(bool) { return false; }
void board_card_reader(void) {}
void board_battery_sample(int, int) {}
void board_battery_debug(int *raw, int *pin_mv) { *raw = 0; *pin_mv = 0; }
extern "C" int power_battery_mv(void) { return 0; }
extern "C" int power_battery_percent(void) { return -1; }
#endif
#endif // ARDUINO_ARCH_ESP32
