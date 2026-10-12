// The step sequencer in the application (core/seq.h; it plays in the engine, ENGINE_DESIGN.md ADR-042).
//
// Patterns and chain (user, 2026-10-11: 8 patterns kept with the machine, the change at the pattern's end). app->seq is the pattern
// that plays and is edited; app->bank holds all of them. While it runs, the pattern that follows (the chain's next entry, or the
// one picked on the PATTERNS page) is handed to the engine ahead (audio_seq_queue); the engine goes on to it by itself when the
// playing pattern has played its steps, and says so (audio_seq_taken): that pattern then becomes app->seq. Stopped, a pattern is
// taken at once. On the card: system/config/seq.cfg (tempo, current pattern, chain) and pattern1.cfg .. pattern8.cfg, written a
// moment after the last change, one file per pass.
//
// Recording into the step sequencer from the keys (ENGINE_DESIGN.md ADR-042; user, 2026-10-11: step entry + live). Rec is armed by
// Shift + Play or the key function Rec, in the synth section.
//   Stopped (step entry): the notes played go to the cursor's step, those pressed while others are held into the same step (a chord
//                         of SEQ_CHORD at most); when every key is up the cursor moves on. The step's other values stay.
//   Running (live):       a note goes to the step nearest to the moment it was pressed (the next one only when the pattern runs
//                         forward), with the length it was held for; it adds to the step's chord (overdub). It is written once
//                         the playhead is on that step, so the sequencer does not play it a second time under the finger.
// Notes are stored without the pattern's transpose, so they come back at the pitch they were played.
#include "app/app_internal.h"
#include <stdio.h>
#include <string.h>

#include "hal/hal_mem.h"
#include "hal/hal_storage.h"

static seq_pattern_t *s_patterns;               // the stored patterns: bulk memory, taken once

#define SEQ_CFG_FILE STORAGE_DIR_CONFIG "/seq.cfg"
#define SEQ_SAVE_MS  2000
#define SEQ_LOOK_MS  250

/* ---------------- patterns and chain: to the engine ---------------- */

void app_seq_step(app_t *app, bool clock_running) {
    seq_bank_t *b = &app->bank;
    seq_t *q = &app->seq;
    if (!b->pat) { audio_seq_set(q); audio_set_clock(q->bpm, clock_running); return; }   // (no memory for the patterns: the one that plays)
    const int took = audio_seq_taken();
    if (took >= 0 && took < SEQ_PATTERNS) {                 // the engine went on to the queued pattern: it is the one shown and edited now
        seq_bank_select(b, q, took);
        if (b->chain_on) b->chain_pos = (uint8_t)((b->chain_pos + 1) % b->chain_len);
        b->next = -1;
        app->seq_queued = -1;
        app->dirty = true;
    }
    if (q->running && !app->seq_was_running && b->chain_on) {      // a run starts with the chain on: its first entry
        b->chain_pos = 0;
        if (b->chain[0] != b->cur) seq_bank_select(b, q, b->chain[0]);
    }
    if (!q->running && b->next >= 0) {                      // stopped before the picked pattern's turn came: it is taken now
        seq_bank_select(b, q, b->next);
        b->next = -1;
    }
    app->seq_was_running = q->running;
    if (app->ui.cursor >= q->steps) app->ui.cursor = q->steps - 1;

    // the pattern as it is now, then the clock, in this order, so that a Play starts both on the same beat
    audio_seq_set(q);
    audio_set_clock(q->bpm, clock_running);

    int want = -1;                                          // what follows the playing pattern
    if (q->running) want = b->chain_on ? b->chain[(b->chain_pos + 1) % b->chain_len] : b->next;
    if (want == app->seq_queued) return;
    if (want < 0) { if (audio_seq_queue(NULL, 0)) app->seq_queued = -1; return; }
    if (want == b->cur) seq_pattern_get(q, &b->pat[b->cur]);        // (itself again: as it is now)
    if (audio_seq_queue(&b->pat[want], want)) app->seq_queued = (int8_t)want;
}

/* ---------------- patterns and chain: on the card ---------------- */

static void pattern_file(int k, char *out, int n) { snprintf(out, (size_t)n, STORAGE_DIR_CONFIG "/pattern%d.cfg", k + 1); }

static uint32_t cfg_hash(const app_t *app) {
    const seq_bank_t *b = &app->bank;
    uint32_t h = 2166136261u ^ (uint32_t)app->seq.bpm;
    h = (h * 16777619u) ^ b->cur;
    h = (h * 16777619u) ^ b->chain_on;
    for (int i = 0; i < b->chain_len && i < SEQ_CHAIN_MAX; i++) h = (h * 16777619u) ^ (uint32_t)(b->chain[i] + 1);
    return h * 16777619u;
}

// A pattern's hash: the stored one, or for the current pattern what app->seq holds now.
static uint32_t pattern_hash(const app_t *app, int k) {
    if (k != app->bank.cur) return app->bank.pat ? seq_pattern_hash(&app->bank.pat[k]) : 0;
    seq_pattern_t live;
    seq_pattern_get(&app->seq, &live);
    return seq_pattern_hash(&live);
}

static void mark_saved(app_t *app) {
    for (int k = 0; k < SEQ_PATTERNS; k++) app->pat_saved[k] = pattern_hash(app, k);
    app->seq_cfg_saved = cfg_hash(app);
    app->seq_seen = app->pat_saved[app->bank.cur] ^ app->seq_cfg_saved;
    app->seq_pending = false;
}

void app_seq_init(app_t *app) {
    if (!s_patterns) s_patterns = (seq_pattern_t *)mem_bulk_alloc(SEQ_PATTERNS * sizeof(seq_pattern_t));
    seq_bank_init(&app->bank, s_patterns, &app->seq);
    app->ui.bank = &app->bank;
    app->seq_queued = -1;
    app->seq_was_running = false;
    app->seq_look_ms = app->seq_ms = 0;
    mark_saved(app);                                        // (what a card without these files stands for)
}

bool app_seq_unsaved(const app_t *app) {
    if (cfg_hash(app) != app->seq_cfg_saved) return true;
    for (int k = 0; k < SEQ_PATTERNS; k++) if (pattern_hash(app, k) != app->pat_saved[k]) return true;
    return false;
}

bool app_seq_load(app_t *app) {
    bool any = false;
    int bpm = app->seq.bpm;
    char file[48];
    if (storage_read(SEQ_CFG_FILE, g_storage_buf, STORAGE_FILE_MAX) >= 0 && seq_bank_from_text(&app->bank, &bpm, g_storage_buf)) { any = true; app->seq.bpm = bpm; }
    for (int k = 0; k < SEQ_PATTERNS && app->bank.pat; k++) {
        pattern_file(k, file, sizeof file);
        if (storage_read(file, g_storage_buf, STORAGE_FILE_MAX) >= 0 && seq_pattern_from_text(&app->bank.pat[k], g_storage_buf)) any = true;
    }
    if (!any) return false;
    if (app->bank.pat) seq_pattern_put(&app->seq, &app->bank.pat[app->bank.cur]); else app->bank.cur = 0;
    if (app->ui.cursor >= app->seq.steps) app->ui.cursor = app->seq.steps - 1;
    mark_saved(app);
    app->dirty = true;
    return true;
}

// Writes the first file that differs from the card. 1: one was written, 0: none had to be, -1: the write failed.
static int save_one(app_t *app) {
    char file[48];
    for (int k = 0; k < SEQ_PATTERNS; k++) {
        const uint32_t h = pattern_hash(app, k);
        if (h == app->pat_saved[k]) continue;
        seq_pattern_t live;
        if (k == app->bank.cur) seq_pattern_get(&app->seq, &live);
        if (k == app->bank.cur && app->bank.pat) app->bank.pat[k] = live;
        pattern_file(k, file, sizeof file);
        const int n = seq_pattern_to_text(k == app->bank.cur ? &live : &app->bank.pat[k], g_storage_buf, STORAGE_FILE_MAX);
        if (n < 0 || !storage_write(file, g_storage_buf, n)) return -1;
        app->pat_saved[k] = h;
        return 1;
    }
    const uint32_t h = cfg_hash(app);
    if (h == app->seq_cfg_saved) return 0;
    const int n = seq_bank_to_text(&app->bank, app->seq.bpm, g_storage_buf, STORAGE_FILE_MAX);
    if (n < 0 || !storage_write(SEQ_CFG_FILE, g_storage_buf, n)) return -1;
    app->seq_cfg_saved = h;
    return 1;
}

bool app_seq_save(app_t *app) {
    int r;
    while ((r = save_one(app)) > 0) {}
    if (r == 0) app->seq_pending = false;
    return r == 0;
}

void app_seq_autosave(app_t *app, uint32_t now) {
    if (now - app->seq_look_ms < SEQ_LOOK_MS) return;
    app->seq_look_ms = now;
    const uint32_t h = pattern_hash(app, app->bank.cur) ^ cfg_hash(app);
    if (h != app->seq_seen) { app->seq_seen = h; app->seq_ms = now; app->seq_pending = true; return; }   // it moved: wait until it is still
    if (!app->seq_pending || now - app->seq_ms < SEQ_SAVE_MS || audio_sd_state() == SD_NONE) return;
    const int r = save_one(app);
    if (r == 0) app->seq_pending = false;
    else if (r < 0) app->seq_ms = now;                      // try again a bit later
}

/* ---------------- recording from the keys ---------------- */

static seq_rec_note_t *slot_of(app_t *app, control_id_t ctl, int state) {
    for (int i = 0; i < SEQ_REC_NOTES; i++)
        if (app->rec.note[i].state == state && (state == SEQ_REC_FREE || app->rec.note[i].ctl == (uint8_t)ctl)) return &app->rec.note[i];
    return NULL;
}

void app_seq_rec_set(app_t *app, bool on) {
    if (in_pads(app)) on = false;
    if (on == app->rec.armed) return;
    memset(&app->rec, 0, sizeof app->rec);
    app->rec.armed = on;
    app->ui.seq_rec = on;
    popup_info(&app->popup, "SEQUENCER", on ? "Rec on: play the notes" : "Rec off", 0, audio_millis(), POPUP_INFO_MS);
    app->dirty = true;
}

// A note of a live recording into its step: into the chord (a note that is there keeps its place), with its length.
static void write_live(app_t *app, const seq_rec_note_t *r) {
    seq_t *q = &app->seq;
    if (r->step >= q->steps) return;
    seq_step_t *st = &q->step[r->step];
    int at = -1;
    for (int k = 0; k < SEQ_CHORD && at < 0; k++) if (st->note[k] == r->note) at = k;
    for (int k = 0; k < SEQ_CHORD && at < 0; k++) if (!st->note[k]) at = k;
    if (at < 0) at = 0;                                     // a full chord: its first note gives way
    st->note[at] = r->note;
    st->len = r->len;
    q->last_note = r->note;
    app->dirty = true;
}

void app_seq_note_down(app_t *app, control_id_t ctl, int note) {
    seq_rec_t *rc = &app->rec;
    seq_t *q = &app->seq;
    if (!rc->armed || in_pads(app)) return;
    note -= q->transpose;
    if (note < SEQ_NOTE_MIN || note > SEQ_NOTE_MAX) return;
    seq_rec_note_t *r = slot_of(app, ctl, SEQ_REC_FREE);
    if (!r) return;
    int step = 0, frac = 0;
    uint32_t count = 0;
    r->ctl = (uint8_t)ctl;
    r->note = (uint8_t)note;
    if (q->running && audio_seq_where(&step, &frac, &count)) {            // live: the nearest step
        const bool next = frac >= 128 && q->dir == SQD_FORWARD;
        r->step = (uint8_t)(next ? (step + 1) % q->steps : step);
        r->count = count + (next ? 1u : 0u);
        r->state = SEQ_REC_DOWN;
        return;
    }
    if (q->running) return;                                               // (it has not played its first step yet)
    if (app->ui.cursor >= q->steps) app->ui.cursor = q->steps - 1;        // step entry: the cursor's step
    seq_step_t *st = &q->step[app->ui.cursor];
    if (rc->entry_held == 0) {                                            // a new step: what it held goes
        memset(st->note, 0, sizeof st->note);
        rc->entry_n = 0;
    }
    if (rc->entry_n < SEQ_CHORD) st->note[rc->entry_n++] = (uint8_t)note;
    q->last_note = (uint8_t)note;
    rc->entry_held++;
    r->state = SEQ_REC_ENTRY;
    app->dirty = true;
}

void app_seq_note_up(app_t *app, control_id_t ctl) {
    seq_rec_t *rc = &app->rec;
    seq_rec_note_t *r = slot_of(app, ctl, SEQ_REC_ENTRY);
    if (r) {                                                              // step entry: the last key up moves the cursor on
        r->state = SEQ_REC_FREE;
        if (rc->entry_held > 0 && --rc->entry_held == 0) {
            app->ui.cursor = (app->ui.cursor + 1) % app->seq.steps;
            app->dirty = true;
        }
        return;
    }
    r = slot_of(app, ctl, SEQ_REC_DOWN);
    if (!r) return;
    int step = 0, frac = 0;
    uint32_t count = r->count;
    int len = 1;
    if (audio_seq_where(&step, &frac, &count)) {                          // how long it was held, in steps, to the nearest one
        const int32_t held = (int32_t)((count - r->count) * 256u) + frac;
        len = (held + 128) / 256;
    }
    r->len = (uint8_t)(len < 1 ? 1 : len > SEQ_LEN_MAX ? SEQ_LEN_MAX : len);
    r->state = SEQ_REC_WAIT;
}

// Every step: the live notes whose step the playhead has reached are written (at once when the sequencer has stopped).
void app_seq_poll(app_t *app) {
    seq_rec_t *rc = &app->rec;
    if (!rc->armed) return;
    int step = 0, frac = 0;
    uint32_t count = 0;
    const bool playing = app->seq.running && audio_seq_where(&step, &frac, &count);
    for (int i = 0; i < SEQ_REC_NOTES; i++) {
        seq_rec_note_t *r = &rc->note[i];
        if (r->state == SEQ_REC_DOWN && !playing) { r->len = 1; r->state = SEQ_REC_WAIT; }      // stopped under the finger
        if (r->state != SEQ_REC_WAIT) continue;
        if (playing && (int32_t)(count - r->count) < 0) continue;       // its step has not started yet
        write_live(app, r);
        r->state = SEQ_REC_FREE;
    }
}
