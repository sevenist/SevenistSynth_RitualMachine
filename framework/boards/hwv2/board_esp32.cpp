// The second prototype's board services (boards/esp32/board_esp32.h): how its power switch and joystick are read for the shared soft power
// (boards/esp32/power_esp32.cpp), the speaker's amplifier, what the card reader reads.
//
// The power circuit (power.kicad_sch): the supply's enable (U15, TPS562201, pin 5) is fed by two diodes, D40 from the switch SW37 and D37 from PWR_EN
// (GP10). Switched on, SW37 puts the battery, through R20 (10 k), on D40 and on the divider R21 / R22 (150 k / 68 k) to mux channel 7; switched off it
// grounds them. So the switch has no pin of its own: channel 7 reads the battery while it is on, and 0 V while it is off. The same reading is the switch
// and the battery. With no battery, it reads 0 V on either side of the switch: hence HWV2_USB_STORAGE (board_pins.h).
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV2)
#include <Arduino.h>
#include "boards/esp32/board_esp32.h"
#include "boards/esp32/board_pins.h"
#include "boards/esp32/mux_esp32.h"
#include "boards/esp32/power_esp32.h"
#include "boards/hwv2/hwv2_layout.h"

namespace {
bool g_speaker_on = true;                      // what board_speaker_on was last told (board_audio_off does not change it)
constexpr int kSpkOn = LOW, kSpkShutdown = HIGH;   // the LM4871's shutdown pin, as on the first prototype
// Switched on, the pin reads the battery through the divider: below the voltage of a battery that is there (HW_BATT_PRESENT_MV), it counts as off.
constexpr int kOnPinMv = HW_BATT_PRESENT_MV * 3300 / HW_BATT_FULL_SCALE_MV;

// The switch, from channel 7. The input task reads it in the synth (board_battery_sample), the card reader's loop in its own firmware; before either
// has, a direct read (setup(): no task owns the mux yet).
volatile bool g_switch_known = false;
volatile bool g_switch_on = false;
uint8_t g_switch_stable = 0;

void mux_ready() {
    static bool done = false;
    if (!done) { mux_init(); done = true; }
}

void speaker_pin(int level) {
    pinMode(PIN_SPK_SD, OUTPUT);
    digitalWrite(PIN_SPK_SD, level);
}
}  // namespace

// ---- the power switch, for power_esp32.cpp ----
void power_switch_init(void) {}

bool power_switch_direct(void) {
    mux_ready();
    g_switch_on = mux_read_mv(HWV2_MUX_PWR) >= kOnPinMv;
    g_switch_known = true;
    g_switch_stable = 0;
    return g_switch_on;
}

void power_sense_sample(int pin_mv) {
    const bool on = pin_mv >= kOnPinMv;
    if (!g_switch_known) { g_switch_on = on; g_switch_known = true; g_switch_stable = 0; return; }
    if (on == g_switch_on) { g_switch_stable = 0; return; }
    if (++g_switch_stable >= 2) { g_switch_on = on; g_switch_stable = 0; }   // 2 readings of the new side in a row
}

bool board_power_button_on(void) { return g_switch_known ? g_switch_on : power_switch_direct(); }

bool power_joystick_held(void) {
    mux_ready();
    return mux_read_settled(HWV2_MUX_JOY_SW, 4) > 2048;
}

void power_audio_resume(void) { board_speaker_on(g_speaker_on); }

// ---- the card reader (a firmware of its own: no input task) ----
void board_reader_init(void) { mux_ready(); }
bool board_reader_joystick(void) { return mux_read_settled(HWV2_MUX_JOY_SW, 1) > 2048; }

void board_reader_sensors(void) {               // the switch and the battery, as the input task reads them in the synth
    const uint16_t mv = mux_read_mv(HWV2_MUX_PWR);
    board_battery_sample(mux_read(HWV2_MUX_PWR), mv);
}

// ---- audio: the speaker's amplifier (the ES8388 is not fitted yet) ----
void board_audio_enable(void) { speaker_pin(g_speaker_on ? kSpkOn : kSpkShutdown); }

#ifndef HWV1_CARD_READER
void audio_esp32_open(void);                   // audio_out_esp32.cpp: the audio task's own gate

void board_audio_open(void) { audio_esp32_open(); }
#endif

void board_audio_off(void) { speaker_pin(kSpkShutdown); }

void board_speaker_on(bool on) {
    g_speaker_on = on;
    speaker_pin(on ? kSpkOn : kSpkShutdown);
}
#endif
