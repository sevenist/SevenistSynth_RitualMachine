// The HWV2 board for host tests (stub_hwv2.h): the display, the control table, the LEDs, the card.
#include "tests/stubs/stub_hwv2.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include "hal/hal_storage.h"
#include "host/png.h"

int stub_frames = 0;
uint16_t stub_frame[DISPLAY_WIDTH * DISPLAY_HEIGHT];

extern "C" void display_send_rgb565(const uint16_t *frame) {
    std::memcpy(stub_frame, frame, sizeof stub_frame);
    stub_frames++;
}
extern "C" void display_send(u8g2_t *) { stub_frames++; }
extern "C" const input_control_t *input_controls(int *count) { if (count) *count = HWV2_CONTROL_COUNT; return kHwv2Controls; }

led_color_t stub_leds[HWV2_CONTROL_COUNT];
int stub_led_shows = 0;
extern "C" void leds_init(void) { std::memset(stub_leds, 0, sizeof stub_leds); }
extern "C" void leds_set_brightness(uint8_t) {}
extern "C" void leds_clear(void) { std::memset(stub_leds, 0, sizeof stub_leds); }
extern "C" void leds_set(int ctl, led_color_t c) { if (ctl >= 0 && ctl < HWV2_CONTROL_COUNT) stub_leds[ctl] = c; }
extern "C" void leds_set_all(led_color_t c) { for (led_color_t &l : stub_leds) l = c; }
extern "C" void leds_show(void) { stub_led_shows++; }

bool stub_write_png(const char *path, int scale) { return png_write_rgb565(path, stub_frame, DISPLAY_WIDTH, DISPLAY_HEIGHT, scale); }

namespace {
std::map<std::string, std::string> g_files;
bool g_card_in = false;
std::deque<storage_event_t> g_events;
}  // namespace

int stub_card_reads = 0;
void stub_card_put(const char *name, const char *text) { g_files[name] = text; }
void stub_card_clear() { g_files.clear(); }
void stub_card_insert() { storage_event_t e = {}; e.kind = STORAGE_EV_INSERTED; g_card_in = true; g_events.push_back(e); }
void stub_card_remove() { storage_event_t e = {}; e.kind = STORAGE_EV_REMOVED; g_card_in = false; g_events.push_back(e); }

extern "C" void storage_set_folders(const char *const *, int) {}
extern "C" void storage_set_moves(const storage_move_t *, int) {}
extern "C" int storage_read(const char *name, char *buf, int cap) {
    stub_card_reads++;
    const auto it = g_files.find(name);
    if (!g_card_in || it == g_files.end() || cap <= 0) return -1;
    const int n = static_cast<int>(std::min(it->second.size(), static_cast<size_t>(cap - 1)));
    std::memcpy(buf, it->second.data(), static_cast<size_t>(n));
    buf[n] = 0;
    return n;
}
extern "C" bool storage_write(const char *name, const char *data, int len) {
    if (!g_card_in) return false;
    g_files[name].assign(data, static_cast<size_t>(len));
    return true;
}
extern "C" bool storage_remove(const char *name) { return g_card_in && g_files.erase(name) > 0; }
extern "C" bool storage_poll_event(storage_event_t *e) {
    if (g_events.empty()) return false;
    *e = g_events.front();
    g_events.pop_front();
    return true;
}
extern "C" void storage_make_folders(void) {}
extern "C" bool storage_card_reader(void) { return false; }
