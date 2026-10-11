// Capture / resampling (ADR-041 decision 50): what the Pads section plays, recorded onto a pad.
#include "app/app_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAPTURE_THRESHOLD 100                   // q15 peak that starts a recording: about -50 dB
#define CAPTURE_MAX_S     300                   // a recording without a length stops by itself after 5 minutes

// The first name "<stem><n>" (n from 1) no sample of the library's root folder has: its files are listed (the card is read), since
// the catalog only knows the files in use.
static void lib_new_name(const char *stem, char *out, int n) {
    int top = 0;
    const size_t sl = strlen(stem);
    const int total = audio_dir_open("");
    audio_dir_entry_t e[BROWSE_PAGE];
    for (int k = 0; k < total;) {
        const int got = audio_dir_read(k, e, BROWSE_PAGE);
        if (got <= 0) break;
        for (int i = 0; i < got; i++)
            if (!e[i].dir && !strncmp(e[i].name, stem, sl) && atoi(e[i].name + sl) > top) top = atoi(e[i].name + sl);
        k += got;
    }
    snprintf(out, (size_t)n, "%s%d", stem, top + 1);
}

// The Rec key: arms a recording, or stops the one that runs. It goes to the selected pad when that is empty, else to the next empty
// one. With a length (Shift + Rec: 1, 2, 4, 8 bars at the tempo) it starts on the next bar line when the clock runs, else at the first
// sound, and stops by itself; the pad then loops it on the bar. Without one it starts at the first sound and stops at the next push.
void app_capture_key(app_t *app) {
    pad_capture_t *c = &app->capture;
    const uint32_t now = audio_millis();
    if (!in_pads(app)) return;
    if (app->in.shift && !app->in.mod) {                    // the length
        static const uint8_t steps[] = {0, 1, 2, 4, 8};
        int k = 0;
        for (int i = 0; i < 5; i++) if (steps[i] == c->bars) k = i;
        c->bars = steps[(k + 1) % 5];
        char t[24];
        if (c->bars) snprintf(t, sizeof t, "Length %d bar%s", c->bars, c->bars > 1 ? "s" : ""); else snprintf(t, sizeof t, "Until Rec again");
        popup_info(&app->popup, "REC", t, 0, now, POPUP_INFO_MS);
        app->dirty = true;
        return;
    }
    if (c->stage == 1) { audio_capture_stop(); return; }    // stop: the card finishes the file, app_capture_poll puts it on its pad
    if (c->stage || app->render.stage || audio_sd_state() != SD_OK) {
        popup_info(&app->popup, "REC", audio_sd_state() != SD_OK ? "No card" : "Busy", 0, now, POPUP_INFO_MS);
        app->dirty = true;
        return;
    }
    int pad = -1;
    for (int k = 0; k < PAD_COUNT && pad < 0; k++) {
        const int i = (pads_ui_pad(&app->ui) + k) % PAD_COUNT;
        if (pad_empty(&app->pads.pad[i])) pad = i;
    }
    if (pad < 0) { popup_info(&app->popup, "REC", "No empty pad", 0, now, POPUP_INFO_MS); app->dirty = true; return; }
    if (!app->rec_next) {                                   // the first free name: the root folder is listed once, then counted
        lib_new_name("rec", c->name, sizeof c->name);
        app->rec_next = (uint16_t)atoi(c->name + 3);
    }
    snprintf(c->name, sizeof c->name, "rec%d", app->rec_next);
    audio_dir_close();                                      // (the list goes: a new sample is coming, and its memory is wanted)
    const uint32_t rate = (uint32_t)audio_rate();
    const int bpm = app->seq.bpm > 0 ? app->seq.bpm : 120;
    const uint32_t limit = c->bars ? (uint32_t)(((uint64_t)c->bars * 4u * 60u * rate + (uint32_t)bpm / 2) / (uint32_t)bpm) : CAPTURE_MAX_S * rate;
    if (!audio_capture_start(c->name, CAPTURE_THRESHOLD, c->bars && app->pads_run, limit)) {
        popup_info(&app->popup, "REC", "Busy, no card or no memory", 0, now, POPUP_INFO_MS);
        app->dirty = true;
        return;
    }
    app->rec_next++;
    c->stage = 1; c->pad = pad; c->loop = c->bars != 0;
    char l[8], t[24];
    pad_label(pad, l, sizeof l);
    snprintf(t, sizeof t, "Ready -> %s", l);
    popup_info(&app->popup, "REC", t, 0, now, POPUP_INFO_MS);
    app->dirty = true;
}

void app_capture_poll(app_t *app, uint32_t now) {
    pad_capture_t *c = &app->capture;
    if (c->stage == 1) {
        uint32_t frames = 0, dropped = 0;
        const int st = audio_capture_poll(&frames, &dropped);
        const uint16_t secs = (uint16_t)(frames / (uint32_t)(audio_rate() > 0 ? audio_rate() : 48000));
        if (st != app->ui.cap_state || secs != app->ui.cap_secs) { app->ui.cap_state = (uint8_t)st; app->ui.cap_secs = secs; app->dirty = true; }
        if (st == CAPTURE_ST_DONE) { c->frames = frames; c->dropped = dropped; c->stage = 2; c->since = now; }
        else if (st == CAPTURE_ST_EMPTY || st == CAPTURE_ST_FAILED || st == CAPTURE_ST_IDLE) {
            audio_capture_done();
            c->stage = 0;
            app->ui.cap_state = CAPTURE_ST_IDLE;
            if (st == CAPTURE_ST_FAILED) popup_error(&app->popup, "REC", "The recording could not be written to the card.", NULL, NULL);
            else popup_info(&app->popup, "REC", "Nothing recorded", 0, now, POPUP_INFO_MS);
            app->dirty = true;
        }
    }
    if (c->stage == 2) {
        if (app_sample_use(app, c->name, true) >= 0) {          // in the catalog: onto its pad, at full level (a copy of a copy must not fade)
            pad_t *p = &app->pads.pad[c->pad];
            pad_default(p);
            snprintf(p->file, PAD_NAME_MAX, "%s", c->name);
            p->level = 100;
            if (c->loop) { p->loop = PAD_LOOP_TRIM; p->grid = PAD_GRID_BAR; p->mode = PAD_TOGGLE; }
            audio_capture_done();
            c->stage = 0;
            app->ui.cap_state = CAPTURE_ST_IDLE;
            app->ui.pad_bank = (uint8_t)(c->pad / PADS_PER_BANK);
            app->ui.pad_cur = (uint8_t)(c->pad % PADS_PER_BANK);
            app_pad_changed(app, c->pad);
            char l[8], t[40];
            pad_label(c->pad, l, sizeof l);
            const uint32_t ds = c->frames * 10u / (uint32_t)(audio_rate() > 0 ? audio_rate() : 48000);
            snprintf(t, sizeof t, "%u.%u s -> %s%s", (unsigned)(ds / 10), (unsigned)(ds % 10), l, c->dropped ? ", with gaps" : "");
            popup_info(&app->popup, "REC", t, 0, now, POPUP_INFO_MS);
            app->dirty = true;
        } else if (now - c->since > 15000) {
            audio_capture_done();
            c->stage = 0;
            app->ui.cap_state = CAPTURE_ST_IDLE;
            popup_error(&app->popup, "REC", "The recording did not show up in the library.", NULL, NULL);
            app->dirty = true;
        }
    }
}
