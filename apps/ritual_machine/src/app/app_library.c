// The samples the application uses: the catalog (core/audio.h: the files in use, each read from the card when asked for) and the sample
// browser (scr_browse.c).
#include "app/app_internal.h"
#include <stdio.h>
#include <string.h>

/* ---------------- the catalog: the files in use ---------------- */

int app_lib_index(const char *name) { return name && name[0] ? audio_sample_find(name) : -1; }

// Does anything refer to catalog entry `index` (called `name`): a pad, a sampler of the rack, the SAMPLES tab, a job in progress.
static bool sample_in_use(const app_t *app, int index, const char *name) {
    for (int k = 0; k < PAD_COUNT; k++) if (!strcmp(app->pads.pad[k].file, name)) return true;
    for (int i = 0; i < app->rack.count; i++)
        if (app->rack.slot[i].type == MOD_SAMPLER && (int)app->rack.slot[i].v[MP_SM_FILE] - 1 == index) return true;
    if (app->ui.smp_cur == index && !in_pads(app)) return true;
    if (app->ui.br_open == BROWSE_PAD && !strcmp(app->ui.br_pad.file, name)) return true;     // (Back puts that pad back)
    if (app->render.stage && (!strcmp(app->render.name, name) || !strcmp(app->render.parent, name))) return true;
    return app->capture.stage && !strcmp(app->capture.name, name);
}

// The entry nothing uses that was asked for longest ago (-1: none), and how many such entries there are. `own`: only among the entries
// this application asked for (lib_stamp != 0; the dev serial commands add entries of their own).
static int lib_oldest_unused(const app_t *app, bool own, int *count) {
    audio_sample_info_t in;
    int best = -1, n = 0;
    for (int i = 0; i < AUDIO_SAMPLES_MAX; i++) {
        if (!audio_sample_info(i, &in) || !in.name[0] || (own && !app->lib_stamp[i]) || sample_in_use(app, i, in.name)) continue;
        n++;
        if (best < 0 || app->lib_stamp[i] < app->lib_stamp[best]) best = i;
    }
    if (count) *count = n;
    return best;
}

void app_lib_drop(app_t *app, int index) {
    app->lib_stamp[index] = 0;
    audio_sample_hide(index);
}

// A sample of the library by name: its catalog index, read from the card when it is not in the catalog yet. The samples nothing uses
// do not pile up (each holds memory: on the board the pads went silent after about 25 of them): only the LIB_SPARE asked for last
// stay (the browser: going back to one of them costs no card read), the older ones leave. When the catalog is full, the oldest unused
// entry leaves first. Returns the index, LIB_MISSING (no such sample), LIB_AGAIN (an entry that left is still being unloaded: ask
// again at the next step) or LIB_FULL (every entry is in use). `wait`: the card is waited for (a header: 20 ms .. 0.4 s on the board);
// without it the answer may be LIB_WAIT (the file is being read: ask again at a later step), which is what anything that follows a
// highlight or runs at every step wants (the browser held the UI for up to a second per row, measured 2026-10-11).
#define LIB_SPARE 3
int app_sample_use(app_t *app, const char *name, bool wait) {
    static uint8_t waits;                                                   // steps spent waiting for an entry that left
    int index = wait ? audio_sample_use(name) : audio_sample_request(name);
    if (index == -3) return LIB_WAIT;
    if (index == -2) {                                                      // the catalog is full
        audio_sample_info_t in;
        bool leaving = false;                                               // an entry that left earlier is still being unloaded
        for (int i = 0; i < AUDIO_SAMPLES_MAX && !leaving; i++) leaving = audio_sample_info(i, &in) && !in.name[0];
        if (leaving && ++waits < 20) return LIB_AGAIN;                      // (longer: a voice still plays it; another entry leaves)
        waits = 0;
        const int old = lib_oldest_unused(app, false, NULL);
        if (old < 0) return leaving ? LIB_AGAIN : LIB_FULL;
        app_lib_drop(app, old);
        index = wait ? audio_sample_use(name) : audio_sample_request(name);   // (request: the answer it kept, no second read)
        if (index == -2) return LIB_AGAIN;
        if (index == -3) return LIB_WAIT;
    }
    waits = 0;
    if (index < 0 || index >= AUDIO_SAMPLES_MAX) return LIB_MISSING;
    app->lib_stamp[index] = ++app->lib_clock;
    int unused = 0;
    const int old = lib_oldest_unused(app, true, &unused);                 // (this one counts as unused: its pad takes it next)
    if (old >= 0 && old != index && unused > LIB_SPARE + 1) app_lib_drop(app, old);
    return index;
}

static void lib_full_popup(app_t *app) {
    char t[40];
    snprintf(t, sizeof t, "Full: %d samples in use", AUDIO_SAMPLES_MAX);
    popup_info(&app->popup, "LIBRARY", t, 0, audio_millis(), POPUP_INFO_MS);
    app->dirty = true;
}

// The pads' files that are not in the catalog (after a start, a card change, pads.cfg, a version or a file picked): one per step,
// then every pad that plays it is sent to the engine again (it had no sample to give it). A file that is not on the card is not asked
// for again until its pad changes or the card does.
void app_pads_resolve(app_t *app) {
    if (!app->lib_dirty || audio_sd_state() != SD_OK) return;
    for (int k = 0; k < PAD_COUNT; k++) {
        const char *file = app->pads.pad[k].file;
        if (!file[0] || (app->lib_missing >> k & 1) || app_lib_index(file) >= 0) continue;
        const int index = app_sample_use(app, file, false);
        if (index == LIB_WAIT) return;                                      // its header is being read: the UI goes on meanwhile
        if (index == LIB_AGAIN && ++app->lib_tries < 100) return;
        app->lib_tries = 0;
        for (int j = k; j < PAD_COUNT; j++) {
            if (strcmp(app->pads.pad[j].file, file)) continue;
            if (index < 0) app->lib_missing |= 1ull << j;
            else if (in_pads(app)) audio_pad_set(j, &app->pads.pad[j]);
        }
        if (index == LIB_FULL || index == LIB_AGAIN) lib_full_popup(app);
        app->dirty = true;
        return;
    }
    app->lib_dirty = false;
}

/* ---------------- the sample browser ---------------- */

// A new file on a pad: the whole file, with its own loop points when it has some (user, 2026-10-10), the pad's other settings kept.
static void pad_take_file(app_t *app, int pad, const audio_sample_info_t *in) {
    pad_t *p = &app->pads.pad[pad];
    snprintf(p->file, PAD_NAME_MAX, "%s", in->name);
    p->start = p->end = p->lstart = 0;
    if (in->loop_mode != 0 && in->loop_end > in->loop_start + 1) p->loop = PAD_LOOP_FILE;
    else if (p->loop == PAD_LOOP_FILE) p->loop = PAD_LOOP_OFF;
    app_pad_changed(app, pad);
}

void app_browse_open(app_t *app, int what) {
    synth_ui_t *ui = &app->ui;
    char cur[PAD_NAME_MAX] = "";
    audio_sample_info_t in;
    const int pad = pads_ui_pad(ui);
    const bool presets = what == BROWSE_PRESET;
    const char *const title = presets ? "PRESETS" : "SAMPLES";
    if (presets) snprintf(cur, sizeof cur, "%s", app->preset);
    else if (what == BROWSE_PAD) snprintf(cur, sizeof cur, "%s", app->pads.pad[pad].file);
    else if (audio_sample_info(ui->smp_cur, &in)) snprintf(cur, sizeof cur, "%s", in.name);
    if (audio_sd_state() == SD_OK) {                        // a long folder takes a moment on the board: the screen says so first
        ui->br_open = (uint8_t)what;
        ui->br_dir[0] = 0;
        ui->br_total = ui->br_n = ui->br_first = ui->br_cur = 0;
        ui->br_state = BR_READING;
        app_draw_frame(app);
    }
    if (audio_sd_state() != SD_OK || (presets && !app_preset_browse_begin(app)) || !browse_ui_open(ui, what, cur, audio_millis())) {
        ui->br_open = 0;
        popup_info(&app->popup, title, audio_sd_state() != SD_OK ? "No card" : presets ? "No presets folder" : "No samples folder", 0, audio_millis(), POPUP_INFO_MS);
        app->dirty = true;
        return;
    }
    ui->br_target = (int8_t)pad;
    ui->br_pad = app->pads.pad[pad];
    app->dirty = true;
}

// The highlighted sample, once the highlight rests: into the catalog (the screen shows its overview) and, for a pad, onto that pad, so
// that the pad's key plays it. False while it has to be asked again (the card is reading it, or an unused sample is still being
// unloaded). `wait`: the card is waited for (the sample was chosen or its key pushed before it arrived); else the list stays free.
bool app_browse_load(app_t *app, bool wait) {
    synth_ui_t *ui = &app->ui;
    char name[PAD_NAME_MAX];
    ui->br_state = BR_NONE;
    if (!browse_ui_file(ui, name, sizeof name)) return true;
    if (ui->br_open == BROWSE_PRESET) {                                      // a preset: in place of the sound, so the keys play it
        ui->br_state = app_preset_try(app, name) ? BR_SHOWN : BR_NONE;
        return true;
    }
    const int index = app_sample_use(app, name, wait);
    if (index == LIB_WAIT) { ui->br_state = BR_FRESH; return false; }
    if (index == LIB_AGAIN && ++ui->br_tries < 100) { ui->br_state = BR_FRESH; return false; }
    ui->br_tries = 0;
    if (index == LIB_FULL || index == LIB_AGAIN) lib_full_popup(app);
    if (index < 0 || !audio_sample_info(index, &ui->br_info)) return true;
    ui->br_state = BR_SHOWN;
    if (ui->br_open == BROWSE_PAD && strcmp(app->pads.pad[ui->br_target].file, name)) {
        pad_take_file(app, ui->br_target, &ui->br_info);
        audio_sample_info(index, &ui->br_info);              // (a .wav / .mp3 was converted for the pad: its length is known now)
    }
    return true;
}

// The browser closes: on a choice the pad keeps the sample (the SAMPLES tab: it becomes its File), else the pad is as it was.
void app_browse_close(app_t *app, bool chosen) {
    synth_ui_t *ui = &app->ui;
    const int what = ui->br_open, pad = ui->br_target;
    ui->br_done = 0;
    if (chosen && ui->br_state == BR_FRESH) app_browse_load(app, true);         // chosen before the highlight rested, or before the card answered
    if (chosen && ui->br_state != BR_SHOWN) {                               // nothing to take: the browser stays
        popup_info(&app->popup, what == BROWSE_PRESET ? "PRESETS" : "SAMPLES", what == BROWSE_PRESET ? "This preset cannot be read" : "This sample cannot be read", 0,
                   audio_millis(), POPUP_INFO_MS);
        app->dirty = true;
        return;
    }
    char name[PAD_NAME_MAX] = "";
    if (what == BROWSE_PRESET && chosen) browse_ui_file(ui, name, sizeof name);
    ui->br_open = 0;
    if (what == BROWSE_PRESET) {
        app_preset_browse_end(app, chosen && name[0] ? name : NULL);
    } else if (what == BROWSE_PAD) {
        if (!chosen && memcmp(&app->pads.pad[pad], &ui->br_pad, sizeof(pad_t))) { app->pads.pad[pad] = ui->br_pad; app_pad_changed(app, pad); }
        if (ui->pad_page == 2) pads_ui_chop_refresh(ui);
    } else if (chosen) {
        const int index = app_lib_index(ui->br_info.name);
        if (index >= 0) ui->smp_cur = index;
    }
    app->dirty = true;
}

void app_browse_poll(app_t *app, uint32_t now) {
    synth_ui_t *ui = &app->ui;
    if (!ui->br_open) return;
    if (ui->br_done) { app_browse_close(app, ui->br_done == 1); return; }
    if (ui->br_state == BR_FRESH && now - ui->br_ms >= BROWSE_REST_MS && app_browse_load(app, false)) app->dirty = true;
}
