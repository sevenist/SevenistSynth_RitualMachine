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
int ui_stub_builds;                                                  // audio_build calls (the UI tests check the section switch)
void audio_build(const rack_t *r, const synth_params_t *p) { (void)r; (void)p; ui_stub_builds++; }
void audio_set_params(const rack_t *r, const synth_params_t *p) { (void)r; (void)p; }
// The library: demo0..2, then the files the processing "wrote" (stub_extra: their names and lengths; a hidden one has no name).
#define STUB_EXTRA 24
static char stub_extra[STUB_EXTRA][24];
static uint32_t stub_extra_frames[STUB_EXTRA];
static int stub_n_extra, stub_listed;                                 // written / listed by the last rescan
int audio_sample_count(void) { return 3 + stub_listed; }
int audio_osc_preview(int m, float t, float mo, float h, int period, int16_t *out, int n) {   // no engine here: the UI shows the model name
    (void)m; (void)t; (void)mo; (void)h; (void)period; (void)out; (void)n;
    return 0;
}
bool audio_sample_info(int i, audio_sample_info_t *o) {
    if (i < 0 || i >= 3 + stub_listed) return false;
    memset(o, 0, sizeof *o);
    if (i < 3) snprintf(o->name, 24, "demo%d", i); else snprintf(o->name, 24, "%s", stub_extra[i - 3]);
    o->frames = i < 3 ? 96000 : stub_extra_frames[i - 3]; o->rate = 48000; o->root = 60; o->slices = i == 2 ? 8 : 0;
    for (int k = 0; k < 8; k++) o->slice[k] = k * 12000;
    for (int k = 0; k < 64; k++) o->peaks[k] = 255 - k * 3;
    o->loop_start = 20000; o->loop_end = 70000; o->loop_mode = i == 1 ? 1 : 0;     // demo1 has loop points, demo0 / demo2 only the numbers
    return true;
}
int audio_samples_rescan(void) { stub_listed = stub_n_extra; return 3 + stub_listed; }
// The processing: done at once, a new library entry of the source's length (Pad to: the amount) once the app rescans.
int ui_stub_renders, ui_stub_render_op, ui_stub_render_amount, ui_stub_removed;
char ui_stub_render_src[24], ui_stub_removed_last[64];
static int stub_render_state;
bool audio_render_start(const char *src, const char *dst, int op, int amount) {
    if (stub_render_state == 1 || stub_n_extra >= STUB_EXTRA) return false;
    audio_sample_info_t in;
    uint32_t frames = 96000;
    for (int i = 0; i < 3 + stub_listed; i++) if (audio_sample_info(i, &in) && !strcmp(in.name, src)) frames = in.frames;
    snprintf(stub_extra[stub_n_extra], 24, "%s", dst);
    stub_extra_frames[stub_n_extra] = op == 7 && (uint32_t)amount > frames ? (uint32_t)amount : frames;
    stub_n_extra++;
    snprintf(ui_stub_render_src, sizeof ui_stub_render_src, "%s", src);
    ui_stub_renders++; ui_stub_render_op = op; ui_stub_render_amount = amount;
    stub_render_state = 2;
    return true;
}
int audio_render_poll(int *percent) { if (percent) *percent = stub_render_state == 2 ? 100 : 0; return stub_render_state; }
void audio_render_done(void) { stub_render_state = 0; }
void audio_sample_hide(int index) { if (index >= 3 && index < 3 + stub_listed) stub_extra[index - 3][0] = 0; }
void ui_stub_library_reset(void) { stub_n_extra = stub_listed = 0; stub_render_state = 0; }
sd_state_t audio_sd_state(void) { return getenv("UI_DUMP_SD_SLOW") ? SD_SLOW : SD_OK; }
uint32_t audio_sd_read_us(void) { return 81000; }
uint32_t audio_sd_generation(void) { static int calls; return getenv("UI_DUMP_SD_SLOW") && calls++ > 0 ? 1 : 0; }   // slow card: the first call (app_init) sees 0, the card "arrives" afterwards
bool audio_sample_prepare(int i) { (void)i; return true; }
// The machine clock: what the app last asked for (the UI tests read it); audio_transport reports bar 1 beat 1 while it "runs".
int ui_stub_clock_bpm, ui_stub_clock_run, ui_stub_metronome;
void audio_set_clock(int b, int s, int w, int r) { (void)s; (void)w; ui_stub_clock_bpm = b; ui_stub_clock_run = r; }
void audio_set_metronome(bool on) { ui_stub_metronome = on; }
bool audio_transport(int *bar, int *beat, int *frac) { *bar = 1; *beat = 1; *frac = 0; return ui_stub_clock_run != 0; }
void audio_motion_restart(void) {}
void audio_note_on(int n) { (void)n; }
void audio_note_off(int n) { (void)n; }
// The Pads section: what the app asked for, for the UI tests (pushes / releases / stops counted; a pushed pad "plays" until released or
// stopped, so the grid and the LEDs have something to show).
int ui_stub_pads_builds, ui_stub_pad_sets, ui_stub_pad_pushes, ui_stub_pad_releases, ui_stub_pads_stops, ui_stub_last_pad = -1;
static uint64_t stub_playing;
void audio_pads_build(const pads_t *p, const rack_t *r) { (void)p; (void)r; ui_stub_pads_builds++; stub_playing = 0; }
void audio_pad_set(int pad, const pad_t *p) { (void)pad; (void)p; ui_stub_pad_sets++; }
void audio_pad_push(int pad) { ui_stub_pad_pushes++; ui_stub_last_pad = pad; if (pad >= 0 && pad < 64) stub_playing |= 1ull << pad; }
void audio_pad_release(int pad) { ui_stub_pad_releases++; if (pad >= 0 && pad < 64) stub_playing &= ~(1ull << pad); }
void audio_pads_stop(void) { ui_stub_pads_stops++; stub_playing = 0; }
uint64_t audio_pads_playing(void) { return stub_playing; }
int32_t audio_pad_position(int pad) { (void)pad; return -1; }
// zero crossings of the demo files: every 1000 frames
int64_t audio_sample_zero(int index, uint32_t frame, int dir) {
    if (index < 0 || index > 2) return -1;
    if (dir > 0) return (frame / 1000 + 1) * 1000;
    if (dir < 0) return frame % 1000 ? frame / 1000 * 1000 : (int64_t)frame - 1000;
    return (frame + 500) / 1000 * 1000;
}
// the transient scan of a demo file: 4 attacks, two strong (60 % of the way at sensitivity 50) and two faint
int audio_sample_onsets(int index, uint32_t *frames, uint16_t *strength, int max) {
    static const uint32_t f[4] = {12000, 30000, 48000, 72000};
    static const uint16_t s[4] = {2500, 400, 2000, 600};
    if (index < 0 || index > 2) return 0;
    int n = 0;
    for (; n < 4 && n < max; n++) { frames[n] = f[n]; strength[n] = s[n]; }
    return n;
}
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
bool storage_remove(const char *name) {
    ui_stub_removed++;
    snprintf(ui_stub_removed_last, sizeof ui_stub_removed_last, "%s", name);
    for (int i = 0; i < RAMCARD_FILES; i++) if (ramcard[i].name[0] && !strcmp(ramcard[i].name, name)) { memset(&ramcard[i], 0, sizeof ramcard[i]); return true; }
    return ramcard_on;
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
