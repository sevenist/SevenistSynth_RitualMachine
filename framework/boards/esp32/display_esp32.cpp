// ESP32 display driver: what every panel shares. The panel itself is the board's (boards/esp32/display_panel.h).
//
// A full frame takes about 120 ms on the first prototype's I2C bus (measured, 400 kHz), and the UI loop used to wait for it: a key or a knob turn
// arriving meanwhile was handled up to 120 ms late (late notes, steppy cutoff sweeps). Now display_send() only copies the frame and wakes the display
// task, which sends the tiles (8 x 8 pixels) that differ from the last frame it sent. The task alone uses the bus after init; the application keeps
// drawing into the u8g2 buffer. If frames come faster than the bus, the task skips to the newest one.
//
// Colour frames (display_send_rgb565, hal_display.h) go the same way on a board with a colour screen (HW_DISPLAY_RGB565 in board_pins.h): three
// frames of 41 KB (128 x 160) in PSRAM, the newest, the one being sent and what the panel shows; the panel compares tiles and sends the
// changed ones through its own small DMA buffer in internal RAM (display_panel_show_rgb565). Elsewhere a colour frame is made one-colour
// here (a pixel is lit when it is bright) and shown as any other.
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "hal/hal_display.h"
#include "boards/esp32/board_esp32.h"
#include "boards/esp32/board_pins.h"
#include "boards/esp32/display_panel.h"

#ifndef HW_DISPLAY_RGB565
#define HW_DISPLAY_RGB565 0            // (board_pins.h: 1 on a board with a colour screen)
#endif

namespace {
constexpr int kTilesX = DISPLAY_WIDTH / 8, kTilesY = DISPLAY_HEIGHT / 8, kBytes = kTilesX * kTilesY * 8;
uint8_t pending[kBytes];                       // the newest frame from the application (guarded by mtx)
uint8_t work[kBytes], sent[kBytes];            // the frame being sent, and what the panel shows
SemaphoreHandle_t mtx;
TaskHandle_t task;
volatile bool have_frame = false;              // the application has sent a frame
volatile bool boot_playing = false;            // the boot task owns the panel (and `work`, `sent`): the display task waits
bool sent_valid = true;                        // `sent` is what the panel shows (false after colour frames: the next one-colour frame goes whole)

// Sends the tiles of `work` that differ from what the panel shows.
void show_work() {
    if (!sent_valid) {                         // every tile differs from `work`: all of it is sent
        for (int i = 0; i < kBytes; i++) sent[i] = (uint8_t)~work[i];
        sent_valid = true;
    }
    display_panel_show(work, sent);
    memcpy(sent, work, kBytes);
}

#if HW_DISPLAY_RGB565
constexpr size_t kColorBytes = (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
uint16_t *pending565 = nullptr, *work565 = nullptr, *sent565 = nullptr;   // PSRAM; made at the first colour frame (none: frames go one-colour)
volatile bool color_frame = false;             // the newest frame the application sent is a colour one (guarded by mtx with pending565)
bool sent565_valid = false;                    // `sent565` is what the panel shows (false: the next colour frame goes whole)

bool color_buffers() {
    if (pending565) return true;
    uint16_t *b[3];
    for (uint16_t *&p : b) p = static_cast<uint16_t *>(heap_caps_malloc_prefer(kColorBytes, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT));
    if (!b[0] || !b[1] || !b[2]) {
        for (uint16_t *p : b) heap_caps_free(p);
        return false;
    }
    work565 = b[1];
    sent565 = b[2];
    pending565 = b[0];                         // (last: the task looks at it, under the mutex, only once color_frame is set)
    return true;
}
#endif

// A colour frame made one-colour, into a buffer of u8g2's layout: a pixel is lit when it is bright (the boards without a colour screen).
void color_to_mono(const uint16_t *frame, uint8_t *out) {
    memset(out, 0, kBytes);
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            const uint16_t v = frame[y * DISPLAY_WIDTH + x];
            const int luma = ((v >> 11) * 2 * 77 + ((v >> 5) & 0x3F) * 150 + (v & 0x1F) * 2 * 29) >> 8;   // 0..63
            if (luma >= 24) out[(y / 8) * DISPLAY_WIDTH + x] |= (uint8_t)(1 << (y & 7));
        }
}

void display_task(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);                  // pdTRUE: several frames sent meanwhile = one wake-up, the newest frame
        if (boot_playing || !have_frame) continue;                // (the boot task wakes this one when it is done)
#if HW_DISPLAY_RGB565
        if (color_frame) {
            xSemaphoreTake(mtx, portMAX_DELAY);
            memcpy(work565, pending565, kColorBytes);
            xSemaphoreGive(mtx);
            display_panel_show_rgb565(work565, sent565_valid ? sent565 : nullptr);
            uint16_t *const shown = work565;                      // what the panel shows now: no copy, the two frames swap
            work565 = sent565;
            sent565 = shown;
            sent565_valid = true;
            sent_valid = false;
            continue;
        }
        sent565_valid = false;
#endif
        xSemaphoreTake(mtx, portMAX_DELAY);
        memcpy(work, pending, kBytes);
        xSemaphoreGive(mtx);
        show_work();
    }
}

#ifndef HWV1_CARD_READER   // (the card reader firmware has no application and no animation: usb_storage_esp32.cpp draws its one screen)
// The boot animation: the frames the app handed over (display_boot_config) in the middle of the screen, frame_ms each, back and forth (first .. last .. first ..) until the application says its start is over (display_boot_end: the card read,
// the settings and the pads' samples in), so the screen never shows a half-loaded UI (user, 2026-10-11). Its own task, started by
// display_init (the first thing setup() does after the power), at a priority above everything else on core 0: the frames keep their
// rhythm while the rest of the start goes on under it. Once the end is asked for, it heads for the nearer end of the animation (its
// first and last frames are the empty ones: half a pass at most; the first pass is always played whole), hands the panel to the
// display task, which shows the newest frame the application has sent, and deletes itself. While it plays, the card's work runs at
// full priority (display_boot_playing, samples_esp32.cpp: nobody watches the UI yet). It gives up waiting after kBootMaxMs. No
// image: no task. (The simulator plays the same frames once, in the application: app_boot_animation.)
//
// The machine's name and version around it (user, 2026-10-11): the application's own words (the config's draw_text), drawn once into
// `boot_text` before the task starts and laid over every frame (a buffer from the heap, given back when the animation ends).
//
// A loading bar under it (user, 2026-10-11): setup() fills its first kBootSetupPct per cent as its steps end (display_esp32_boot_stage),
// the application the rest (display_boot_progress: its settings, then each pad's file). The bar glides to its target, a few pixels a frame.
constexpr UBaseType_t kBootPriority = 10;      // the input scan has 8, the card 5, the UI loop and the display task 1
constexpr uint32_t kBootMaxMs = 12000;         // an application that never reports its start: the screen is its own after this
constexpr int kBootSetupPct = 30;              // measured: the application exists 1.5 s into a start of 5.1 s
constexpr int kBarW = 96, kBarH = 7, kBarX = (DISPLAY_WIDTH - kBarW) / 2, kBarY = DISPLAY_HEIGHT - 20;   // outline, a gap, 3 pixels of fill
volatile bool boot_end_asked = false;
volatile bool loading = true;                  // the loading screen is up: no sound, no key LEDs (user, 2026-10-11)
uint8_t *boot_text = nullptr;                  // the words of the boot screen, in the buffer's layout (none: no memory for them)
volatile int boot_setup_pct = 0, boot_app_pct = 0;   // 0..kBootSetupPct, and the application's 0..100
const display_boot_t *boot_cfg = nullptr;      // display_boot_config: what to play (none: no animation)

// The loading screen has ended (the animation's task is done, or there was none and the application's start is over): the sound
// and the key LEDs from here on.
void loading_over() {
    loading = false;
    board_audio_open();
}

inline void work_px(int x, int y) { work[(y / 8) * DISPLAY_WIDTH + x] |= (uint8_t)(1 << (y & 7)); }

void draw_bar(int fill) {                       // fill: 0 .. kBarW - 4 pixels
    for (int x = kBarX + 1; x < kBarX + kBarW - 1; x++) { work_px(x, kBarY); work_px(x, kBarY + kBarH - 1); }
    for (int y = kBarY + 1; y < kBarY + kBarH - 1; y++) { work_px(kBarX, y); work_px(kBarX + kBarW - 1, y); }
    for (int x = 0; x < fill; x++) for (int y = 2; y < kBarH - 2; y++) work_px(kBarX + 2 + x, kBarY + y);
}

void boot_task(void *arg) {
    const display_boot_t *sp = static_cast<const display_boot_t *>(arg);
    const int frames = sp->count ? sp->count : 1, row_bytes = (sp->w + 7) / 8;
    const int x0 = (DISPLAY_WIDTH - sp->w) / 2, y0 = (DISPLAY_HEIGHT - sp->h) / 2;
#ifdef BOOT_TRACE
    const uint32_t t0 = millis();
    uint32_t worst = 0, asked_ms = 0;
    int shown = 0;
#endif
    const TickType_t start = xTaskGetTickCount();
    TickType_t wake = start;
    bool passed = false;                                          // the first pass is over: frame 0 is an end to stop at too
    bool turned = false;                                          // the end was asked for: heading for the nearer end
    int bar = 0;                                                  // the bar's fill in pixels, on its way to the target
    for (int f = 0, dir = 1;;) {
        memset(work, 0, kBytes);                                  // the u8g2 buffer layout: a byte = 8 pixels of a column, bit 0 on top
        const uint8_t *src = sp->frames + f * sp->h * row_bytes;
        for (int y = 0; y < sp->h; y++)
            for (int x = 0; x < sp->w; x++)
                if (src[y * row_bytes + x / 8] & (0x80 >> (x & 7))) work_px(x0 + x, y0 + y);
        for (int i = 0; boot_text && i < kBytes; i++) work[i] |= boot_text[i];
        const int pct = boot_end_asked ? 100 : boot_setup_pct + boot_app_pct * (100 - kBootSetupPct) / 100;
        const int target = (kBarW - 4) * (pct > 100 ? 100 : pct) / 100;
        if (target > bar) bar += boot_end_asked ? (target - bar + 1) / 2 : (target - bar > 12 ? 4 : target - bar > 4 ? 2 : 1);   // (it never goes back)
        draw_bar(bar);
#ifdef BOOT_TRACE
        const uint32_t a = micros();
        show_work();
        if (micros() - a > worst) worst = micros() - a;
        shown++;
        if (boot_end_asked && !asked_ms) asked_ms = millis();
#else
        show_work();
#endif
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(sp->frame_ms));
        const bool at_end = frames < 2 || f == frames - 1 || (f == 0 && passed);
        if (at_end && (boot_end_asked || xTaskGetTickCount() - start > pdMS_TO_TICKS(kBootMaxMs))) break;
        if (frames < 2) continue;
        if (boot_end_asked && passed && !turned) { turned = true; dir = f < frames / 2 ? -1 : 1; }   // the nearer end: half a pass at most
        else if (f == frames - 1) { dir = -1; passed = true; } else if (f == 0) dir = 1;
        f += dir;
    }
#ifdef BOOT_TRACE
    Serial.printf("[BOOT] animation: %d frames shown (%d in a pass) from %u to %u ms, slowest sent in %u us, the end asked at %u ms%s\n", shown, frames, (unsigned)t0,
                  (unsigned)millis(), (unsigned)worst, (unsigned)asked_ms, boot_end_asked ? "" : " (never: gave up waiting)");
#endif
    boot_playing = false;
    loading_over();
    heap_caps_free(boot_text);
    boot_text = nullptr;
    xTaskNotifyGive(task);                                        // the application's newest frame, if it has sent one
    vTaskDelete(nullptr);
}
#endif // !HWV1_CARD_READER
}  // namespace

extern "C" u8g2_t *display_init(void) {
    u8g2_t *const g = display_panel_init();
    memset(sent, 0, kBytes);                // the panel is blank
    mtx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(display_task, "display", 4096, nullptr, 1, &task, 0);   // core 0 with the UI; the I2C waits let the UI loop run
#ifndef HWV1_CARD_READER
    const display_boot_t *boot = boot_cfg;
    if (boot && boot->frames && boot->frame_ms && boot->w <= DISPLAY_WIDTH && boot->h <= DISPLAY_HEIGHT) {
        if (boot->draw_text) boot_text = static_cast<uint8_t *>(heap_caps_malloc_prefer(kBytes, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT));
        if (boot_text) {
            boot->draw_text(g);                  // (into the application's buffer, which is blank again below)
            memcpy(boot_text, u8g2_GetBufferPtr(g), kBytes);
            u8g2_ClearBuffer(g);
        }
        boot_playing = true;
        if (xTaskCreatePinnedToCore(boot_task, "boot", 3072, const_cast<display_boot_t *>(boot), kBootPriority, nullptr, 0) != pdPASS) boot_playing = false;
    }
#endif
    return g;
}

extern "C" void display_send(u8g2_t *g) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    memcpy(pending, u8g2_GetBufferPtr(g), kBytes);
    xSemaphoreGive(mtx);
    have_frame = true;
#if HW_DISPLAY_RGB565
    color_frame = false;
#endif
    xTaskNotifyGive(task);
}

extern "C" void display_send_rgb565(const uint16_t *frame) {
    if (!frame) return;
#if HW_DISPLAY_RGB565
    if (color_buffers()) {
        xSemaphoreTake(mtx, portMAX_DELAY);
        memcpy(pending565, frame, kColorBytes);
        color_frame = true;
        xSemaphoreGive(mtx);
        have_frame = true;
        xTaskNotifyGive(task);
        return;
    }
#endif
    xSemaphoreTake(mtx, portMAX_DELAY);
    color_to_mono(frame, pending);
#if HW_DISPLAY_RGB565
    color_frame = false;
#endif
    xSemaphoreGive(mtx);
    have_frame = true;
    xTaskNotifyGive(task);
}

#ifndef HWV1_CARD_READER
extern "C" void display_boot_config(const display_boot_t *boot) { boot_cfg = boot; }
extern "C" void display_boot_end(void) {      // the boot task ends at the animation's nearer end
    boot_end_asked = true;
    if (!boot_playing) loading_over();        // (no animation, or it gave up waiting)
}
bool display_esp32_loading(void) { return loading; }
extern "C" void display_boot_progress(int percent) { boot_app_pct = percent < 0 ? 0 : percent > 100 ? 100 : percent; }
extern "C" bool display_boot_playing(void) { return boot_playing; } // for the card task: the UI is not seen yet
void display_esp32_boot_stage(int percent) {                         // setup(): how far it is, 0..100 of its own part of the bar
    boot_setup_pct = (percent < 0 ? 0 : percent > 100 ? 100 : percent) * kBootSetupPct / 100;
}
#endif // !HWV1_CARD_READER
#endif // ARDUINO_ARCH_ESP32
