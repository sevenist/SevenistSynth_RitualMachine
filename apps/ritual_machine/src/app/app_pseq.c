// The Pads section's sequencer in the application (core/pad_seq.h; it plays in the engine, ENGINE_DESIGN.md ADR-042 stage 3; user,
// 2026-10-12: a step grid with live record, one pattern over all 64 pads, patterns as on the synth, step mode by Select + Play).
//
// It plays while the Pads clock runs (Play) and it is on. The pattern that plays is edited in place (app->pbank.pat[cur], in bulk
// memory). What follows it (the chain's next entry, or the pattern picked while it runs) is handed to the engine ahead, which goes on
// to it at the pattern's end and says so, as for the synth's sequencer (app_seq.c). On the card: system/config/padseq.cfg and
// padpat1.cfg .. padpat8.cfg, written a moment after the last change, one file per pass.
//
// Step mode (Select + Play): the 16 pad keys are the selected pad's steps of the page shown (app_pads.c, scr_pads.c).
// Live record (Select + Rec, while it runs): a pad pushed goes to the step nearest to the push, a Gate / Toggle pad with the length
// it was held for; it is written once the playhead is on that step, so the sequencer does not play it again under the finger.
#include "app/app_internal.h"
#include "hal/hal_mem.h"
#include "core/card_layout.h"
#include <stdio.h>
#include <string.h>

#define PSEQ_CFG_FILE STORAGE_DIR_CONFIG "/padseq.cfg"
#define PSEQ_SAVE_MS  2000
#define PSEQ_LOOK_MS  250

static pseq_pattern_t *s_patterns;              // the patterns: bulk memory, taken once

static bool running(const app_t *app) { return in_pads(app) && app->pads_run && app->pbank.on; }

/* ---------------- to the engine ---------------- */

void app_pseq_step(app_t *app) {
    pseq_bank_t *b = &app->pbank;
    if (!b->pat) return;
    const bool run = running(app);
    const int took = audio_pseq_taken();
    if (took >= 0 && took < PSEQ_PATTERNS) {                // the engine went on to the queued pattern
        b->cur = (uint8_t)took;
        if (b->chain_on) b->chain_pos = (uint8_t)((b->chain_pos + 1) % b->chain_len);
        b->next = -1;
        app->pseq_queued = -1;
        app->dirty = true;
    }
    if (run && !app->pseq_was_running && b->chain_on) { b->chain_pos = 0; b->cur = b->chain[0]; }   // a run starts with the chain on
    if (!run && b->next >= 0) { b->cur = (uint8_t)b->next; b->next = -1; }                            // picked, then stopped: taken now
    app->pseq_was_running = run;
    audio_pseq_set(&b->pat[b->cur], run);                   // (before the clock is set: app_seq_step follows)

    int want = -1;
    if (run) want = b->chain_on ? b->chain[(b->chain_pos + 1) % b->chain_len] : b->next;
    if (want != app->pseq_queued) {
        if (want < 0) { if (audio_pseq_queue(NULL, 0)) app->pseq_queued = -1; }
        else if (audio_pseq_queue(&b->pat[want], want)) app->pseq_queued = (int8_t)want;
    }
    const int pos = run ? audio_pseq_pos() : -1;
    if (pos != app->ui.pseq_pos || run != app->ui.pseq_run) { app->ui.pseq_pos = (int8_t)pos; app->ui.pseq_run = run; if (in_pads(app)) app->dirty = true; }
}

/* ---------------- on the card ---------------- */

static void pattern_file(int k, char *out, int n) { snprintf(out, (size_t)n, STORAGE_DIR_CONFIG "/padpat%d.cfg", k + 1); }

static uint32_t cfg_hash(const app_t *app) {
    const pseq_bank_t *b = &app->pbank;
    uint32_t h = 2166136261u ^ b->on;
    h = (h * 16777619u) ^ b->cur;
    h = (h * 16777619u) ^ b->chain_on;
    for (int i = 0; i < b->chain_len && i < SEQ_CHAIN_MAX; i++) h = (h * 16777619u) ^ (uint32_t)(b->chain[i] + 1);
    return h * 16777619u;
}

static uint32_t all_hash(const app_t *app) {
    uint32_t h = cfg_hash(app);
    for (int k = 0; k < PSEQ_PATTERNS; k++) h = (h * 16777619u) ^ pseq_hash(&app->pbank.pat[k]);
    return h;
}

static void mark_saved(app_t *app) {
    for (int k = 0; k < PSEQ_PATTERNS; k++) app->ppat_saved[k] = pseq_hash(&app->pbank.pat[k]);
    app->pseq_cfg_saved = cfg_hash(app);
    app->pseq_seen = all_hash(app);
    app->pseq_pending = false;
}

void app_pseq_init(app_t *app) {
    if (!s_patterns) s_patterns = (pseq_pattern_t *)mem_bulk_alloc(PSEQ_PATTERNS * sizeof(pseq_pattern_t));
    memset(&app->pbank, 0, sizeof app->pbank);
    memset(&app->prec, 0, sizeof app->prec);
    app->pseq_queued = -1;
    app->pseq_was_running = false;
    app->pseq_look_ms = app->pseq_ms = 0;
    if (!s_patterns) return;                                // no memory: the Pads section has no sequencer
    pseq_bank_init(&app->pbank, s_patterns);
    mark_saved(app);
}

bool app_pseq_unsaved(const app_t *app) {
    if (!app->pbank.pat) return false;
    if (cfg_hash(app) != app->pseq_cfg_saved) return true;
    for (int k = 0; k < PSEQ_PATTERNS; k++) if (pseq_hash(&app->pbank.pat[k]) != app->ppat_saved[k]) return true;
    return false;
}

bool app_pseq_load(app_t *app) {
    bool any = false;
    char file[48];
    if (!app->pbank.pat) return false;
    if (storage_read(PSEQ_CFG_FILE, g_storage_buf, STORAGE_FILE_MAX) >= 0 && pseq_bank_from_text(&app->pbank, g_storage_buf)) any = true;
    for (int k = 0; k < PSEQ_PATTERNS; k++) {
        pattern_file(k, file, sizeof file);
        if (storage_read(file, g_storage_buf, STORAGE_FILE_MAX) >= 0 && pseq_from_text(&app->pbank.pat[k], g_storage_buf)) any = true;
    }
    if (!any) return false;
    mark_saved(app);
    app->dirty = true;
    return true;
}

// Writes the first file that differs from the card. 1: one was written, 0: none had to be, -1: the write failed.
static int save_one(app_t *app) {
    char file[48];
    for (int k = 0; k < PSEQ_PATTERNS; k++) {
        const uint32_t h = pseq_hash(&app->pbank.pat[k]);
        if (h == app->ppat_saved[k]) continue;
        pattern_file(k, file, sizeof file);
        const int n = pseq_to_text(&app->pbank.pat[k], g_storage_buf, STORAGE_FILE_MAX);
        if (n < 0 || !storage_write(file, g_storage_buf, n)) return -1;
        app->ppat_saved[k] = h;
        return 1;
    }
    const uint32_t h = cfg_hash(app);
    if (h == app->pseq_cfg_saved) return 0;
    const int n = pseq_bank_to_text(&app->pbank, g_storage_buf, STORAGE_FILE_MAX);
    if (n < 0 || !storage_write(PSEQ_CFG_FILE, g_storage_buf, n)) return -1;
    app->pseq_cfg_saved = h;
    return 1;
}

bool app_pseq_save(app_t *app) {
    int r;
    if (!app->pbank.pat) return true;
    while ((r = save_one(app)) > 0) {}
    if (r == 0) app->pseq_pending = false;
    return r == 0;
}

void app_pseq_autosave(app_t *app, uint32_t now) {
    if (!app->pbank.pat || now - app->pseq_look_ms < PSEQ_LOOK_MS) return;
    app->pseq_look_ms = now;
    const uint32_t h = all_hash(app);
    if (h != app->pseq_seen) { app->pseq_seen = h; app->pseq_ms = now; app->pseq_pending = true; return; }   // it moved: wait until it is still
    if (!app->pseq_pending || now - app->pseq_ms < PSEQ_SAVE_MS || audio_sd_state() == SD_NONE) return;
    const int r = save_one(app);
    if (r == 0) app->pseq_pending = false;
    else if (r < 0) app->pseq_ms = now;
}

/* ---------------- step mode, live record ---------------- */

void app_pseq_steps_set(app_t *app, bool on) {
    if (!in_pads(app) || !app->pbank.pat) on = false;
    if (on == app->ui.pad_steps) return;
    app->ui.pad_steps = on;
    if (on) { app->ui.pad_edit = false; pads_ui_fx_open(&app->ui, false); }
    popup_info(&app->popup, "PADS", on ? "Steps: the pad keys are steps" : "Steps off", 0, audio_millis(), POPUP_INFO_MS);
    app->dirty = true;
}

// Step mode: pad key k of the page shown toggles the selected pad's hit on that step.
void app_pseq_step_key(app_t *app, int k) {
    pseq_bank_t *b = &app->pbank;
    if (!b->pat) return;
    pseq_pattern_t *p = &b->pat[b->cur];
    const int cursor = app->ui.pseq_step < p->steps ? app->ui.pseq_step : p->steps - 1;
    const int step = cursor / SEQ_PAGE * SEQ_PAGE + k;
    if (step >= p->steps) return;                           // past the pattern's end
    app->ui.pseq_step = (uint8_t)step;
    if (!pseq_toggle(p, pads_ui_pad(&app->ui), step)) popup_info(&app->popup, "PADS", "The pattern is full", 0, audio_millis(), POPUP_INFO_MS);
    app->dirty = true;
}

void app_pseq_rec_set(app_t *app, bool on) {
    if (!in_pads(app) || !app->pbank.pat) on = false;
    if (on == app->prec.armed) return;
    memset(&app->prec, 0, sizeof app->prec);
    app->prec.armed = on;
    app->ui.pseq_rec = on;
    popup_info(&app->popup, "PADS", on ? "Pattern rec on: play the pads" : "Pattern rec off", 0, audio_millis(), POPUP_INFO_MS);
    app->dirty = true;
}

void app_pseq_pad_down(app_t *app, int pad) {
    pseq_rec_t *rc = &app->prec;
    if (!rc->armed || !running(app)) return;
    const pseq_pattern_t *p = &app->pbank.pat[app->pbank.cur];
    int step = 0, frac = 0;
    uint32_t count = 0;
    if (!audio_pseq_where(&step, &frac, &count)) return;
    for (int i = 0; i < PSEQ_REC_NOTES; i++) {
        pseq_rec_note_t *r = &rc->note[i];
        if (r->state) continue;
        const bool next = frac >= 128 && p->dir == SQD_FORWARD;
        r->pad = (uint8_t)pad;
        r->step = (uint8_t)(next ? (step + 1) % p->steps : step);
        r->count = count + (next ? 1u : 0u);
        r->state = PSEQ_REC_DOWN;
        return;
    }
}

void app_pseq_pad_up(app_t *app, int pad) {
    pseq_rec_t *rc = &app->prec;
    for (int i = 0; i < PSEQ_REC_NOTES; i++) {
        pseq_rec_note_t *r = &rc->note[i];
        if (r->state != PSEQ_REC_DOWN || r->pad != pad) continue;
        int step = 0, frac = 0, len = 1;
        uint32_t count = r->count;
        if (audio_pseq_where(&step, &frac, &count)) len = ((int32_t)((count - r->count) * 256u) + frac + 128) / 256;
        r->len = (uint8_t)(len < 1 ? 1 : len > 64 ? 64 : len);
        r->state = PSEQ_REC_WAIT;
    }
}

// Every step: the recorded pushes whose step the playhead has reached are written (at once when it has stopped).
void app_pseq_poll(app_t *app) {
    pseq_rec_t *rc = &app->prec;
    if (!rc->armed || !app->pbank.pat) return;
    pseq_pattern_t *p = &app->pbank.pat[app->pbank.cur];
    int step = 0, frac = 0;
    uint32_t count = 0;
    const bool playing = running(app) && audio_pseq_where(&step, &frac, &count);
    for (int i = 0; i < PSEQ_REC_NOTES; i++) {
        pseq_rec_note_t *r = &rc->note[i];
        if (r->state == PSEQ_REC_DOWN && !playing) { r->len = 1; r->state = PSEQ_REC_WAIT; }
        if (r->state != PSEQ_REC_WAIT) continue;
        if (playing && (int32_t)(count - r->count) < 0) continue;       // its step has not started yet
        if (r->step < p->steps) {
            const int at = pseq_find(p, r->pad, r->step);
            if (at >= 0) p->hit[at].len = (uint32_t)(r->len - 1);        // a hit that is there keeps its level, chance and ratchet
            else if (!pseq_put(p, r->pad, r->step, PSEQ_LEVEL_MAX + 1, 100, 1, r->len))
                popup_info(&app->popup, "PADS", "The pattern is full", 0, audio_millis(), POPUP_INFO_MS);
            app->dirty = true;
        }
        r->state = PSEQ_REC_FREE;
    }
}
