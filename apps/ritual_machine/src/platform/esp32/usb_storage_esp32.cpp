// The card reader (HWV1): the machine is off (its power button is up) and the USB cable feeds it, so it shows its TF card to the
// computer as a drive (USB mass storage) and nothing else runs: no synth, no audio, no keys.
//
// A firmware of its own (platformio.ini env:card_reader, built with HWV1_CARD_READER from this file and the few drivers it needs),
// flashed into the second application slot. Why not a mode of the synth's firmware: the USB device stack (TinyUSB, with every class
// the Arduino core was built with) takes 23 KB of static internal RAM whether it runs or not, and the synth had 24 KB left
// (measured 2026-10-11: the link failed by 10 KB). The synth's firmware starts this one by pointing the bootloader at its slot
// and restarting (board_card_reader); this one points the bootloader back at the synth as its first act, so any later start,
// wanted or not, is the synth's.
//
// The USB port. The chip has one USB transceiver and two users for it. In the synth it belongs to the USB-Serial/JTAG unit: the
// serial console, the dev commands and the flashing. Here USB.begin() hands it to TinyUSB, which presents the drive: no serial
// console, and no flashing without the BOOT button. Before the restart that leaves, the transceiver is given back (the selection
// sits in the RTC domain and would survive the restart).
//
// The card. Nothing here mounts it: the computer's file system owns the sectors, read and written through the card driver's raw
// access (sd_card.h) from the USB task alone. A card that stops answering is taken away from the computer and looked for again.
//
// Leaving: the power button goes down (the machine is switched on), or the joystick is held pushed for 2 s and kept held until the
// synth's boot screen shows (the synth with the button up, for work on the cable: board_power_started_off looks at the joystick).
// Files the computer has not finished writing are lost then: eject the drive first.
//
// On the board (2026-10-11): the synth's firmware started this one, the computer mounted the 32 GB card as a drive and read a 25 MB
// file at 543 KB/s (the port is full-speed USB: about 1 MB/s at best). Not tried yet: writing, the way back to the synth and its
// serial console, a card pulled out while the computer has it.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1) && defined(HWV1_CARD_READER)
#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <soc/rtc_cntl_reg.h>
#include "hal/hal_display.h"
#include "hal/hal_power.h"
#include "app/app.h"                          // BOOT_TITLE, BOOT_VERSION
#include "core/ui_sprites_gen.h"
#include "boards/hwv1/board_esp32.h"
#include "boards/hwv1/board_pins.h"
#include "boards/hwv1/hwv1_layout.h"
#include "boards/hwv1/mux_esp32.h"
#include "boards/hwv1/sd_card.h"

namespace {
constexpr uint32_t kSector = 512;
constexpr int kMaxErrors = 3;                  // failed transfers in a row before the card counts as gone
constexpr uint32_t kQuietMs = 500;             // no transfer for this long: the computer is done for now
constexpr uint32_t kLeaveWaitMs = 3000;        // ... waited for at most this long when leaving
constexpr uint32_t kJoyHoldMs = 2000;

USBMSC msc;
volatile bool g_card = false;                  // the computer may use the card
volatile bool g_host = false;                  // a computer has set the drive up
volatile bool g_ejected = false;               // ... and has ejected it since
volatile int g_errors = 0;
volatile uint32_t g_io_ms = 0;                 // the last transfer

// TinyUSB asks for whole sectors (its buffer, CONFIG_TINYUSB_MSC_BUFSIZE, is a multiple of 512); anything else is refused.
int32_t on_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    if (!g_card || offset || bufsize % kSector) return -1;
    g_io_ms = millis();
    if (!sd_card_raw_read(lba, static_cast<uint8_t *>(buffer), bufsize / kSector)) { g_errors = g_errors + 1; return -1; }
    g_errors = 0;
    return static_cast<int32_t>(bufsize);
}

int32_t on_write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    if (!g_card || offset || bufsize % kSector) return -1;
    g_io_ms = millis();
    if (!sd_card_raw_write(lba, buffer, bufsize / kSector)) { g_errors = g_errors + 1; return -1; }
    g_errors = 0;
    return static_cast<int32_t>(bufsize);
}

bool on_start_stop(uint8_t, bool start, bool load_eject) {
    if (load_eject) g_ejected = !start;
    return true;
}

void on_usb(void *, esp_event_base_t base, int32_t id, void *) {
    if (base != ARDUINO_USB_EVENTS) return;
    if (id == ARDUINO_USB_STARTED_EVENT) { g_host = true; g_ejected = false; }
    if (id == ARDUINO_USB_STOPPED_EVENT || id == ARDUINO_USB_SUSPEND_EVENT) g_host = false;
    if (id == ARDUINO_USB_RESUME_EVENT) g_host = true;
}

// The card for the computer: found and set up, its size told to the drive. Only while the computer has no card (g_card false: its
// transfers are refused, so the USB task is not in the card driver).
bool card_open() {
    if (!sd_card_raw_begin() || !sd_card_raw_sectors()) return false;
    msc.begin(sd_card_raw_sectors(), kSector);
    g_errors = 0;
    g_card = true;
    msc.mediaPresent(true);
    return true;
}

void card_close() {
    msc.mediaPresent(false);
    g_card = false;
}

// The screen (user, 2026-10-11): the firmware's name and version at the top, the card's picture (64/ui_sdcard of assets/UI_Sprites, a
// placeholder to paint over), what this is, and the battery's charge. Without a card the second line says so.
void draw(u8g2_t *g) {
    char l[40];
    const int w = u8g2_GetDisplayWidth(g), h = u8g2_GetDisplayHeight(g);
    u8g2_ClearBuffer(g);
    u8g2_SetFont(g, u8g2_font_5x7_tr);
    u8g2_DrawStr(g, (w - u8g2_GetStrWidth(g, BOOT_TITLE " " BOOT_VERSION)) / 2, 8, BOOT_TITLE " " BOOT_VERSION);
    const gui_sprite_t *sp = ui_sprite("64/ui_sdcard");
    if (sp) {
        const int x0 = (w - sp->w) / 2, y0 = 14, row_bytes = (sp->w + 7) / 8;
        for (int y = 0; y < sp->h; y++)
            for (int x = 0; x < sp->w; x++)
                if (sp->data[y * row_bytes + x / 8] & (0x80 >> (x & 7))) u8g2_DrawPixel(g, x0 + x, y0 + y);
    }
    u8g2_SetFont(g, u8g2_font_7x13B_tr);
    u8g2_DrawStr(g, (w - u8g2_GetStrWidth(g, "Storage mode")) / 2, 94, "Storage mode");
    u8g2_SetFont(g, u8g2_font_4x6_tr);
    const char *line = g_card ? "Eject before disconnecting" : "No card";
    u8g2_DrawStr(g, (w - u8g2_GetStrWidth(g, line)) / 2, 105, line);
    const int pct = power_battery_percent();
    if (pct >= 0) {
        u8g2_SetFont(g, u8g2_font_5x7_tr);
        snprintf(l, sizeof l, "%d %%", pct);
        u8g2_DrawStr(g, (w - u8g2_GetStrWidth(g, l)) / 2, h - 3, l);
    }
    display_send(g);
}

// The computer loses the drive, the transceiver goes back to the USB-Serial/JTAG unit, the board restarts (into the synth: setup()).
[[noreturn]] void leave() {
    const uint32_t t0 = millis();
    while (millis() - g_io_ms < kQuietMs && millis() - t0 < kLeaveWaitMs) delay(20);   // a transfer in progress ends first
    card_close();
    delay(200);
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
    ESP.restart();
    for (;;) delay(1000);
}
}  // namespace

void setup() {
    board_power_init();
    board_boot_slot(false);                    // from here on every start is the synth's
    // Started by the synth's firmware (a software restart), or left over: the board lost its power on the way here and is now
    // switched on. Then the synth, at once.
    if (board_power_button_on() && esp_reset_reason() != ESP_RST_SW) ESP.restart();
    board_audio_off();
    u8g2_t *const g = display_init();
    mux_init();
    pinMode(PIN_JOY_SW, INPUT_PULLUP);

    msc.vendorID("Sevenist");
    msc.productID("Ritual Machine");
    msc.productRevision("1.0");
    msc.onRead(on_read);
    msc.onWrite(on_write);
    msc.onStartStop(on_start_stop);
    msc.mediaPresent(false);
    msc.begin(1, kSector);                     // (a size is needed before the card is known: card_open gives the real one)
    card_open();
    USB.onEvent(on_usb);
    USB.manufacturerName("Sevenist");
    USB.productName("Ritual Machine");
    USB.begin();

    bool was_on = board_power_button_on();     // (down already: the synth's serial command `storage` asked for this)
    uint32_t joy_since = 0, card_try_ms = millis(), sensor_ms = 0, shown = ~0u;
    for (;;) {
        const uint32_t now = millis();
        const bool on = board_power_button_on();
        if (on && !was_on) leave();
        was_on = on;
        board_power_hold(on);
        const bool joy = digitalRead(PIN_JOY_SW) == (HWV1_JOY_SW_PUSHED ? HIGH : LOW);
        if (!joy) joy_since = 0;
        else if (!joy_since) joy_since = now ? now : 1;
        else if (now - joy_since >= kJoyHoldMs) leave();

        if (g_card && g_errors >= kMaxErrors) card_close();              // pulled out (or dead): the computer sees an empty reader
        if (!g_card && now - card_try_ms >= 1000) { card_try_ms = now; card_open(); }
        if (now - sensor_ms >= HWV1_SENSOR_PERIOD_MS) {                  // the battery, as the input task reads it in the synth
            sensor_ms = now;
            const uint16_t mv = mux_read_mv(HWV1_MUX_BATTERY);
            board_battery_sample(mux_read(HWV1_MUX_BATTERY), mv);
        }
        const uint32_t state = (g_card ? 1u : 0u) | static_cast<uint32_t>(power_battery_percent() + 1) << 8;
        if (state != shown) { shown = state; draw(g); }
        delay(20);
    }
}

void loop() {}
#endif // ARDUINO_ARCH_ESP32 && HWV1 && HWV1_CARD_READER
