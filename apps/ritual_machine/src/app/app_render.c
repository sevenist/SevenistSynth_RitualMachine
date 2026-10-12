// Processing a pad's sample into a new version (ADR-041 decisions 36-38, 43): the EDIT page's Apply, and the padding a Grid chop needs.
// The card thread writes the new file (hal_audio.h: audio_render_*); the pad then takes it and the history records it (core/history.h).
#include "app/app_internal.h"
#include "hal/hal_storage.h"
#include <stdio.h>
#include <string.h>

// `op`: an EDIT_* operation and its amount as the page shows it; `chop`: a padding to `amount` frames instead (then the chop follows).
void app_render_start(app_t *app, int pad, int op, int32_t amount, bool chop) {
    pad_render_t *r = &app->render;
    const pad_t *p = &app->pads.pad[pad];
    audio_sample_info_t in;
    const int idx = app_lib_index(p->file);
    if (r->stage || idx < 0 || !audio_sample_info(idx, &in)) return;
    static const char *const shortname[PAD_EDIT_OPS] = {"Norm", "Gain", "FadeIn", "FadeOut", "Trim", "Crop", "Reverse", "DC", "Stretch", "Pitch", "Speed", "Fit", "FitSpd",
                                                        "Paul", "Rate", "Bits", "Drive", "LP", "HP"};
    static const uint8_t proc_of[PAD_EDIT_OPS] = {PROC_NORMALIZE, PROC_GAIN, PROC_FADE_IN, PROC_FADE_OUT, PROC_TRIM, PROC_CROP, PROC_REVERSE, PROC_DC,
                                                  PROC_STRETCH, PROC_PITCH, PROC_RESAMPLE, PROC_STRETCH, PROC_RESAMPLE, PROC_PAUL,
                                                  PROC_DECIMATE, PROC_BITS, PROC_DRIVE, PROC_LOWPASS, PROC_HIGHPASS};
    char amt[14] = "";
    const char *name = "Pad";
    int proc = PROC_PAD_TO;                                                 // what the engine runs (ProcOp) and its amounts
    int32_t arg = amount, arg2 = 0;
    if (!chop) {
        op %= PAD_EDIT_OPS;
        proc = proc_of[op];
        name = shortname[op];
        if (app->ui.edit_tonal && (op == EDIT_STRETCH || op == EDIT_PITCH || op == EDIT_FIT)) {   // Algo: Tonal (the phase vocoder)
            static const char *const tonal[3] = {"StretchT", "PitchT", "FitT"};
            proc = op == EDIT_PITCH ? PROC_PITCH_TONAL : PROC_STRETCH_TONAL;
            name = tonal[op == EDIT_STRETCH ? 0 : op == EDIT_PITCH ? 1 : 2];
        }
        pads_ui_op_amount(op, amount, amt, sizeof amt);
        switch (op) {
            case EDIT_PAUL:                                                 // x1.5 .. x64, as far as the card and the patience go
                arg = amount * 100000;
                if ((uint64_t)in.frames * (uint32_t)amount / 10u > PAUL_FRAMES_MAX) {
                    popup_info(&app->popup, "EDIT", "Too long: 2 min 46 at most", 0, audio_millis(), POPUP_INFO_MS);
                    return;
                }
                break;
            case EDIT_STRETCH: arg = amount * 10000; break;
            case EDIT_SPEED:   arg = 100000000 / (amount > 0 ? amount : 100); break;   // twice the speed = half the length
            case EDIT_CROP:                                                 // the pad's Start..End
                arg = (int32_t)p->start;
                arg2 = (int32_t)pad_end(p, in.frames);
                if (!p->start && (uint32_t)arg2 >= in.frames) {
                    popup_info(&app->popup, "EDIT", "Nothing to change", 0, audio_millis(), POPUP_INFO_MS);
                    return;
                }
                break;
            case EDIT_FIT: case EDIT_FIT_SPEED: {                           // the pad's loop (or Start..End) to `amount` beats at the clock's tempo
                const uint32_t ppm = pad_fit_ppm(pad_fit_region(p, in.frames, in.loop_start, in.loop_end), in.rate, (int)amount, app->seq.bpm);
                if (ppm < PROC_PPM_MIN || ppm > PROC_PPM_MAX) {
                    popup_info(&app->popup, "EDIT", "Too far: x0.25 to x4 only", 0, audio_millis(), POPUP_INFO_MS);
                    return;
                }
                arg = (int32_t)ppm;
                snprintf(amt, sizeof amt, "%db@%d", (int)amount, app->seq.bpm);
            } break;
            default: break;
        }
        if (((proc == PROC_STRETCH || proc == PROC_RESAMPLE || proc == PROC_STRETCH_TONAL) && arg == 1000000) || ((proc == PROC_PITCH || proc == PROC_PITCH_TONAL) && arg == 0)) {
            popup_info(&app->popup, "EDIT", "Nothing to change", 0, audio_millis(), POPUP_INFO_MS);
            return;
        }
    }
    char root[HISTORY_NAME];
    history_root(&app->history, p->file, root);
    history_new_name(&app->history, root, r->name);
    for (int k = 2; app_sample_use(app, r->name, true) >= 0 && k < 100; k++) {       // a file of that name is already on the card (an older history): further on
        char stem[HISTORY_NAME - 5];
        snprintf(stem, sizeof stem, "%s", root);
        snprintf(r->name, sizeof r->name, "%s~%d", stem, k);
    }
    snprintf(r->what, sizeof r->what, "%s%s%s", name, amt[0] ? " " : "", amt);
    snprintf(r->parent, sizeof r->parent, "%s", p->file);
    r->pad = pad; r->op = proc; r->chop = chop; r->frames_in = in.frames;
    audio_dir_close();                                                      // (the browser's folder list: the job wants its memory)
    if (!audio_render_start(p->file, r->name, proc, arg, arg2)) {
        popup_info(&app->popup, "EDIT", "Busy, no card or no memory", 0, audio_millis(), POPUP_INFO_MS);
        return;
    }
    r->stage = 1;
    app->ui.render_pct = 0;
    app->dirty = true;
}

// The new file is in the library: the pad takes it, the history records it and trims the old versions (their files deleted).
static void render_adopt(app_t *app) {
    pad_render_t *r = &app->render;
    pad_t *p = &app->pads.pad[r->pad];
    const uint32_t before[3] = {p->start, p->end, p->lstart};             // the trim points on the parent: Undo puts them back
    snprintf(p->file, PAD_NAME_MAX, "%s", r->name);
    if (r->op == PROC_TRIM || r->op == PROC_REVERSE) { p->start = p->end = p->lstart = 0; }   // the audio moved: the old points mean nothing
    if (r->op == PROC_CROP) {                                                // the new file is the old Start..End
        p->lstart = p->lstart > p->start ? p->lstart - p->start : 0;
        p->start = p->end = 0;
    }
    if (r->op == PROC_PAD_TO) p->end = 0;                                    // padded: to the new end
    audio_sample_info_t made;
    const int made_idx = app_lib_index(r->name);
    if ((r->op == PROC_STRETCH || r->op == PROC_RESAMPLE || r->op == PROC_STRETCH_TONAL || r->op == PROC_PAUL) && r->frames_in && made_idx >= 0 &&
        audio_sample_info(made_idx, &made)) {
        const uint64_t f = made.frames, was = r->frames_in;                  // the length changed: the trim points go with the audio
        p->start = (uint32_t)((p->start * f + was / 2) / was);
        p->lstart = (uint32_t)((p->lstart * f + was / 2) / was);
        if (p->end) p->end = (uint32_t)((p->end * f + was / 2) / was);
    }
    const uint32_t after[3] = {p->start, p->end, p->lstart};
    history_add(&app->history, r->name, r->parent, r->what);
    history_set_trim(&app->history, r->name, before, after);
    const char *used[PAD_COUNT];
    for (int k = 0; k < PAD_COUNT; k++) used[k] = app->pads.pad[k].file;
    char root[HISTORY_NAME], gone[HISTORY_MAX][HISTORY_NAME];
    history_root(&app->history, r->name, root);
    const int n = history_trim(&app->history, root, used, PAD_COUNT, gone, HISTORY_MAX);
    for (int i = 0; i < n; i++) {
        char path[64];
        snprintf(path, sizeof path, STORAGE_DIR_SAMPLES "/%s.smp", gone[i]);
        storage_remove(path);
        const int idx = app_lib_index(gone[i]);
        if (idx >= 0) app_lib_drop(app, idx);
    }
    history_save(&app->history);
    audio_render_done();
    r->stage = 0;
    app->ui.render_pct = -1;
    app_pad_changed(app, r->pad);
    char t[48];
    snprintf(t, sizeof t, "%s: %s", r->what, r->name);
    popup_info(&app->popup, "EDIT", t, 0, audio_millis(), POPUP_INFO_MS);
    if (r->chop) {                                                           // a Grid chop that needed the padding: now on the padded version
        app->ui.chop_pad = -1;
        const pad_chop_t keep = app->ui.chop;
        pads_ui_chop_refresh(&app->ui);
        app->ui.chop.mode = keep.mode; app->ui.chop.div = keep.div; app->ui.chop.to = keep.to;
        pads_ui_chop_refresh(&app->ui);
        audio_sample_info_t in;
        const int idx = app_lib_index(p->file);
        if (idx >= 0 && audio_sample_info(idx, &in)) {
            const int w = pad_chop_apply(&app->pads, r->pad, &app->ui.chop, in.frames);
            for (int k = 0; k < w; k++) app_pad_changed(app, app->ui.chop.to + k);
        }
    }
    app->dirty = true;
}

void app_render_poll(app_t *app, uint32_t now) {
    pad_render_t *r = &app->render;
    if (r->stage == 1) {
        int pct = 0;
        const int st = audio_render_poll(&pct);
        if (pct != app->ui.render_pct) { app->ui.render_pct = pct; app->dirty = true; }
        if (st == RENDER_STATE_DONE) { r->stage = 2; r->since = now; }
        else if (st == RENDER_STATE_FAILED) {
            audio_render_done();
            r->stage = 0;
            app->ui.render_pct = -1;
            popup_error(&app->popup, "EDIT", "The new sample could not be written to the card.", NULL, NULL);
            app->dirty = true;
        }
    }
    if (r->stage == 2) {
        if (app_sample_use(app, r->name, true) >= 0) render_adopt(app);                // the new file, into the catalog
        else if (now - r->since > 15000) {                                   // it cannot be read back (or the catalog stays full)
            audio_render_done();
            r->stage = 0;
            app->ui.render_pct = -1;
            popup_error(&app->popup, "EDIT", "The new sample did not show up in the library.", NULL, NULL);
            app->dirty = true;
        }
    }
}
