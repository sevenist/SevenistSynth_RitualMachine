// Stand-ins for the platform (no audio, no SDL, no card) so the application core runs on its own: used by tools/ui_dump.c and
// the UI tests (tests/ui). Environment switches: UI_DUMP_SD_SLOW, UI_DUMP_CARD=new|slow|part, UI_DUMP_BOARD=hwv1 (see below).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/app.h"
#include "hal/hal_audio.h"
#include "hal/hal_display.h"
#include "hal/hal_leds.h"
#include "hal/hal_storage.h"

// no audio in this tool
void audio_init(void) {}
void audio_shutdown(void) {}
void audio_build(const rack_t *r, const synth_params_t *p) { (void)r; (void)p; }
void audio_set_params(const rack_t *r, const synth_params_t *p) { (void)r; (void)p; }
int audio_sample_count(void) { return 3; }
bool audio_sample_info(int i, audio_sample_info_t *o) {
    if (i < 0 || i > 2) return false;
    memset(o, 0, sizeof *o);
    snprintf(o->name, 24, "demo%d", i);
    o->frames = 96000; o->rate = 48000; o->root = 60; o->slices = i == 2 ? 8 : 0;
    for (int k = 0; k < 8; k++) o->slice[k] = k * 12000;
    for (int k = 0; k < 64; k++) o->peaks[k] = 255 - k * 3;
    o->loop_start = 20000; o->loop_end = 70000;
    return true;
}
int audio_samples_rescan(void) { return 3; }
sd_state_t audio_sd_state(void) { return getenv("UI_DUMP_SD_SLOW") ? SD_SLOW : SD_OK; }
uint32_t audio_sd_read_us(void) { return 81000; }
uint32_t audio_sd_generation(void) { static int calls; return getenv("UI_DUMP_SD_SLOW") && calls++ > 0 ? 1 : 0; }   // slow card: the first call (app_init) sees 0, the card "arrives" afterwards
bool audio_sample_prepare(int i) { (void)i; return true; }
void audio_set_clock(int b, int s, int w, int r) { (void)b; (void)s; (void)w; (void)r; }
void audio_motion_restart(void) {}
void audio_note_on(int n) { (void)n; }
void audio_note_off(int n) { (void)n; }
uint32_t audio_millis(void) { static uint32_t t; return t += 10; }
void audio_update(void) {}
bool input_key_present(int row, int col) { return row > 0 || col < 4; }      // the first prototype: 4 function keys, 4 x 8 note keys
// UI_DUMP_BOARD=hwv1 in the environment: the controls of the prototype (no R1..R3, no B1..B3 / Play, no encoder pushes)
bool input_control_present(control_id_t c) {
    const char *b = getenv("UI_DUMP_BOARD");
    if (!b || !*b) return true;
    return !((c >= CTL_KNOB_R1 && c <= CTL_KNOB_R3) || (c >= CTL_BTN_1 && c <= CTL_BTN_3) || c == CTL_PLAY || c == CTL_ENC_A_SW || c == CTL_ENC_B_SW);
}
bool input_boot_reset(void) { return false; }
// No card by default (the dump starts from the built-in layout). ui_stub_ramcard(true): a card in RAM that keeps what is written (the UI
// tests save and load ui.cfg / keys.cfg with it); false empties it and takes it out.
#define RAMCARD_FILES 8
static struct { char name[64]; char data[STORAGE_FILE_MAX]; int len; } ramcard[RAMCARD_FILES];
static bool ramcard_on;
void ui_stub_ramcard(bool on) { ramcard_on = on; if (!on) memset(ramcard, 0, sizeof ramcard); }
int storage_read(const char *name, char *buf, int cap) {
    if (!ramcard_on || cap <= 0) return -1;
    for (int i = 0; i < RAMCARD_FILES; i++)
        if (ramcard[i].name[0] && !strcmp(ramcard[i].name, name)) {
            const int n = ramcard[i].len < cap - 1 ? ramcard[i].len : cap - 1;
            memcpy(buf, ramcard[i].data, (size_t)n);
            buf[n] = 0;
            return n;
        }
    return -1;
}
bool storage_write(const char *name, const char *data, int len) {
    if (!ramcard_on || len < 0 || len >= STORAGE_FILE_MAX) return false;
    int at = -1;
    for (int i = 0; i < RAMCARD_FILES && at < 0; i++) if (!strcmp(ramcard[i].name, name)) at = i;
    for (int i = 0; i < RAMCARD_FILES && at < 0; i++) if (!ramcard[i].name[0]) at = i;
    if (at < 0) return false;
    snprintf(ramcard[at].name, sizeof ramcard[at].name, "%s", name);
    memcpy(ramcard[at].data, data, (size_t)len);
    ramcard[at].len = len;
    return true;
}
// UI_DUMP_CARD=new|slow|part in the environment: one card event at the start (a card without folders, a slow one, one missing presets/)
bool storage_poll_event(storage_event_t *e) {
    static bool sent;
    const char *c = getenv("UI_DUMP_CARD");
    if (sent || !c) return false;
    sent = true;
    memset(e, 0, sizeof *e);
    e->kind = STORAGE_EV_INSERTED;
    e->read_limit_us = 15000;
    e->read_us = !strcmp(c, "slow") ? 81000 : 900;
    e->slow = !strcmp(c, "slow");
    e->missing = !strcmp(c, "part") ? 1u << 4 : !strcmp(c, "new") ? 0x1f : 0;
    return true;
}
void storage_make_folders(void) { printf("(storage_make_folders)\n"); }
void display_send(u8g2_t *g) { (void)g; }
bool input_pending(void) { return false; }
led_color_t ui_stub_led[CTL_COUNT];                                  // the last colour each LED was given (the UI tests read it)
void leds_set(control_id_t ctl, led_color_t color) { if (ctl > CTL_NONE && ctl < CTL_COUNT) ui_stub_led[ctl] = color; }
void leds_show(void) {}
