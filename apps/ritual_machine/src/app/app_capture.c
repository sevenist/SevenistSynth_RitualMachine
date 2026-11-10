// Capture / resampling (ADR-041 decision 50): what the Pads section plays, recorded onto a pad. And the synth's pattern recorded as a
// sample (apps/ritual_machine/docs/DESIGN.md ADR-042 decision 24), at the end of this file.
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

static void rec_info(app_t *app, const char *text);

// Arms a recording with the length c->bars, or stops the one that runs (the Rec key, the Pads Rec list).
static void capture_arm(app_t *app) {
    pad_capture_t *c = &app->capture;
    const uint32_t now = audio_millis();
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

// The Rec key: arms a recording, or stops the one that runs. It goes to the selected pad when that is empty, else to the next empty
// one. With a length (Shift + Rec: 1, 2, 4, 8 bars at the tempo) it starts on the next bar line when the clock runs, else at the first
// sound, and stops by itself; the pad then loops it on the bar. Without one it starts at the first sound and stops at the next push.
void app_capture_key(app_t *app) {
    pad_capture_t *c = &app->capture;
    if (!in_pads(app)) return;
    if (app->in.shift && !app->in.mod) {                    // the length
        static const uint8_t steps[] = {0, 1, 2, 4, 8};
        int k = 0;
        for (int i = 0; i < 5; i++) if (steps[i] == c->bars) k = i;
        c->bars = steps[(k + 1) % 5];
        char t[24];
        if (c->bars) snprintf(t, sizeof t, "Length %d bar%s", c->bars, c->bars > 1 ? "s" : ""); else snprintf(t, sizeof t, "Until Rec again");
        popup_info(&app->popup, "REC", t, 0, audio_millis(), POPUP_INFO_MS);
        app->dirty = true;
        return;
    }
    capture_arm(app);
}

/* ---------------- the Pads Rec list: Shift + Play in the Pads section (ADR-046 decision 7) ---------------- */

// What the Rec key, Shift + Rec and Select + Rec / Play do, in a list, for a board without the right block (the panel's 4 columns):
//   Capture             what plays, until Shift + Play (or Rec) again
//   Capture 1 .. 8 bars that length at the tempo (the same as Shift + Rec's length, which it sets)
//   Pattern rec         the pads' pattern recorded live (Select + Rec)
//   Steps on / off      step mode (Select + Play)
//   Click: Off / On     the metronome (the key function Click)
// The list opens on the option chosen last (Click excepted). While a capture or the pattern's record runs, Shift + Play stops it at once.
enum { PREC_CAPTURE, PREC_BAR1, PREC_BAR2, PREC_BAR4, PREC_BAR8, PREC_PATTERN, PREC_STEPS, PREC_CLICK, PREC_CHOICES };

static bool pads_rec_label(void *ctx, int i, char *out, int n) {
    const app_t *app = ctx;
    static const char *const names[PREC_CHOICES] = {"Capture", "Capture 1 bar", "Capture 2 bars", "Capture 4 bars", "Capture 8 bars",
                                                    "Pattern rec", "Steps on", "Click: Off"};
    if (i == PREC_STEPS && app->ui.pad_steps) snprintf(out, (size_t)n, "Steps off");
    else if (i == PREC_CLICK) snprintf(out, (size_t)n, "Click: %s", app->metronome ? "On" : "Off");
    else snprintf(out, (size_t)n, "%s", i >= 0 && i < PREC_CHOICES ? names[i] : "");
    return false;
}

static void on_pads_rec(void *ctx, int choice) {
    app_t *app = ctx;
    static const uint8_t bars[] = {0, 1, 2, 4, 8};
    if (choice < 0 || choice >= PREC_CHOICES || !in_pads(app)) return;
    if (choice != PREC_CLICK) app->pads_rec_choice = (uint8_t)choice;
    switch (choice) {
        case PREC_PATTERN:
            if (!app->pbank.pat) rec_info(app, "No pattern");
            else app_pseq_rec_set(app, true);
            break;
        case PREC_STEPS: app_pseq_steps_set(app, !app->ui.pad_steps); break;
        case PREC_CLICK: app_click_toggle(app); break;
        default: app->capture.bars = bars[choice - PREC_CAPTURE]; capture_arm(app); break;
    }
    app->dirty = true;
}

void app_capture_pads_list(app_t *app) {
    if (!in_pads(app)) return;
    if (app->capture.stage == 1 && !app->capture.seq_mode) { audio_capture_stop(); rec_info(app, "Stopped"); return; }
    if (app->prec.armed) { app_pseq_rec_set(app, false); return; }
    popup_choose_labels(&app->popup, "REC", PREC_CHOICES, app->pads_rec_choice, pads_rec_label, on_pads_rec, app);
    app->dirty = true;
}

void app_capture_poll(app_t *app, uint32_t now) {
    pad_capture_t *c = &app->capture;
    if (c->seq_mode) return;                                // (the synth's pattern: app_capture_synth_poll)
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

/* ---------------- the synth's pattern recorded as a sample (user, 2026-10-14) ---------------- */

// Shift + Play (or the key function Rec) in the synth section asks, in a list, what to record: the notes (the step sequencer's Rec,
// app_seq.c), or the sound of one pass of the pattern, from its first step to its last (the capture, engine_synth.h capture_start_seq):
//   Audio, stop / play on   one pass; then the sequencer stops, or plays on
//   Audio + tail            one pass, the sequencer stops, the recording goes on until the sound has died away
//   Loop, stop / play on    the pattern plays twice and the second pass is kept: the first one's tail is under its start, it loops
// The capture opens its file first; once it listens the sequencer is asked for the pass (audio_seq_record) and started if it was
// stopped. When the file is in the library a second list asks where it goes: the selected pad, the next empty one, or the library only.
// The last option, Click: Off / On, is the metronome (ADR-046 phase 5: for a board without the key function Click; not remembered).
enum { REC_NOTES, REC_AUDIO_STOP, REC_AUDIO_ON, REC_AUDIO_TAIL, REC_LOOP_STOP, REC_LOOP_ON, REC_CLICK, REC_CHOICES };
enum { REC_PH_OPENING = 1, REC_PH_PASS };
static const char *const rec_options[REC_CLICK] = {"Notes", "Audio, stop", "Audio, play on", "Audio + tail", "Loop, stop", "Loop, play on"};

static bool rec_label(void *ctx, int i, char *out, int n) {
    const app_t *app = ctx;
    if (i == REC_CLICK) snprintf(out, (size_t)n, "Click: %s", app->metronome ? "On" : "Off");
    else snprintf(out, (size_t)n, "%s", i >= 0 && i < REC_CLICK ? rec_options[i] : "");
    return false;
}

static bool rec_loop(int mode) { return mode == REC_LOOP_STOP || mode == REC_LOOP_ON; }
static bool rec_stops(int mode) { return mode == REC_AUDIO_STOP || mode == REC_AUDIO_TAIL || mode == REC_LOOP_STOP; }

static void rec_info(app_t *app, const char *text) { popup_info(&app->popup, "REC", text, 0, audio_millis(), POPUP_INFO_MS); app->dirty = true; }

static void synth_rec_end(app_t *app) {
    pad_capture_t *c = &app->capture;
    c->stage = 0; c->seq_mode = 0; c->seq_phase = 0;
    app->ui.seq_audio = false;
    app->dirty = true;
}

// The grid a looping pad of this pattern gets: a pass of whole bars plays on the bar, of whole beats on the beat, else free.
static uint8_t pattern_grid(const seq_t *q) {
    static const uint8_t per_beat[SQR_COUNT] = {2, 3, 4, 6, 8};             // steps per beat at each rate (core/seq.h seq_rate_t)
    const int spb = per_beat[q->rate >= 0 && q->rate < SQR_COUNT ? q->rate : SQR_16];
    if (q->steps % (4 * spb) == 0) return PAD_GRID_BAR;
    return q->steps % spb == 0 ? PAD_GRID_BEAT : PAD_GRID_OFF;
}

static void synth_rec_start(app_t *app, int mode) {
    pad_capture_t *c = &app->capture;
    if (c->stage || app->render.stage) { rec_info(app, "Busy"); return; }
    if (audio_sd_state() != SD_OK) { rec_info(app, "No card"); return; }
    bool any = false;
    for (int s = 0; s < app->seq.steps && !any; s++) any = !seq_step_is_rest(&app->seq.step[s]);
    if (!any) { rec_info(app, "The pattern is empty"); return; }
    if (!app->rec_next) {                                   // (the same names as the Pads section's recordings)
        lib_new_name("rec", c->name, sizeof c->name);
        app->rec_next = (uint16_t)atoi(c->name + 3);
    }
    snprintf(c->name, sizeof c->name, "rec%d", app->rec_next);
    audio_dir_close();
    if (!audio_capture_start_seq(c->name, mode == REC_AUDIO_TAIL)) { rec_info(app, "Busy, no card or no memory"); return; }
    app->rec_next++;
    c->stage = 1; c->seq_mode = (uint8_t)mode; c->seq_phase = REC_PH_OPENING;
    c->halted = false;
    c->loop = rec_loop(mode);
    c->grid = pattern_grid(&app->seq);
    app->ui.seq_audio = true;
    rec_info(app, rec_loop(mode) ? "Two passes, the second kept" : app->seq.running ? "From the next step 1" : "From step 1");
}

static void on_rec_choice(void *ctx, int choice) {
    app_t *app = (app_t *)ctx;
    if (choice < 0 || choice >= REC_CHOICES) return;
    if (choice == REC_CLICK) { app_click_toggle(app); return; }
    app->rec_choice = (uint8_t)choice;
    if (choice == REC_NOTES) app_seq_rec_set(app, true);
    else synth_rec_start(app, choice);
    app->dirty = true;
}

void app_capture_synth_key(app_t *app) {
    pad_capture_t *c = &app->capture;
    if (in_pads(app)) return;
    if (app->rec.armed) { app_seq_rec_set(app, false); return; }            // the notes' Rec off
    if (c->seq_mode && c->stage == 1) {                                     // the pattern's recording: stopped, what it has is kept
        audio_capture_stop();
        audio_seq_record(-1, false);
        rec_info(app, "Stopped");
        return;
    }
    popup_choose_labels(&app->popup, "REC", REC_CHOICES, app->rec_choice, rec_label, on_rec_choice, app);
    app->dirty = true;
}

// Where the recording goes: the second list's options (built when it opens; the popup keeps pointers to them).
enum { DEST_SELECTED, DEST_EMPTY, DEST_LIBRARY };
static char dest_text[3][24];
static const char *dest_ptr[3];
static uint8_t dest_what[3];
static int dest_empty_pad;

static void place_on_pad(app_t *app, int pad) {
    pad_capture_t *c = &app->capture;
    pad_t *p = &app->pads.pad[pad];
    pad_default(p);
    snprintf(p->file, PAD_NAME_MAX, "%s", c->name);
    p->level = 100;
    if (c->loop) { p->loop = PAD_LOOP_TRIM; p->grid = c->grid; p->mode = PAD_TOGGLE; }
    app->ui.pad_bank = (uint8_t)(pad / PADS_PER_BANK);
    app->ui.pad_cur = (uint8_t)(pad % PADS_PER_BANK);
    app_pad_changed(app, pad);
}

static void on_dest_choice(void *ctx, int choice) {
    app_t *app = (app_t *)ctx;
    pad_capture_t *c = &app->capture;
    const int what = choice >= 0 && choice < 3 && dest_ptr[choice] ? dest_what[choice] : DEST_LIBRARY;   // Back: the library only
    char t[40], l[8];
    const uint32_t rate = (uint32_t)(audio_rate() > 0 ? audio_rate() : 48000), ds = c->frames * 10u / rate;
    if (what == DEST_LIBRARY) snprintf(t, sizeof t, "%s in the library", c->name);
    else {
        const int pad = what == DEST_SELECTED ? pads_ui_pad(&app->ui) : dest_empty_pad;
        place_on_pad(app, pad);
        pad_label(pad, l, sizeof l);
        snprintf(t, sizeof t, "%u.%u s -> %s%s", (unsigned)(ds / 10), (unsigned)(ds % 10), l, c->dropped ? ", with gaps" : "");
    }
    synth_rec_end(app);
    rec_info(app, t);
}

static void ask_destination(app_t *app) {
    pad_capture_t *c = &app->capture;
    const int sel = pads_ui_pad(&app->ui);
    char l[8];
    int n = 0;
    pad_label(sel, l, sizeof l);
    snprintf(dest_text[n], sizeof dest_text[n], pad_empty(&app->pads.pad[sel]) ? "Pad %s" : "Pad %s (replace)", l);
    dest_what[n++] = DEST_SELECTED;
    dest_empty_pad = -1;
    for (int k = 1; k < PAD_COUNT && dest_empty_pad < 0; k++) {
        const int i = (sel + k) % PAD_COUNT;
        if (pad_empty(&app->pads.pad[i])) dest_empty_pad = i;
    }
    if (dest_empty_pad >= 0 && !pad_empty(&app->pads.pad[sel])) {          // (the selected pad empty: it is the first empty one already)
        pad_label(dest_empty_pad, l, sizeof l);
        snprintf(dest_text[n], sizeof dest_text[n], "Next empty: %s", l);
        dest_what[n++] = DEST_EMPTY;
    }
    snprintf(dest_text[n], sizeof dest_text[n], "Library only");
    dest_what[n++] = DEST_LIBRARY;
    for (int i = 0; i < 3; i++) dest_ptr[i] = i < n ? dest_text[i] : NULL;
    const uint32_t rate = (uint32_t)(audio_rate() > 0 ? audio_rate() : 48000), ds = c->frames * 10u / rate;
    char title[20];
    snprintf(title, sizeof title, "%s %u.%u s", c->name, (unsigned)(ds / 10), (unsigned)(ds % 10));
    c->stage = 3;
    if (!popup_choose(&app->popup, title, dest_ptr, n, 0, on_dest_choice, app)) on_dest_choice(app, -1);   // (no room for a popup: the library)
}

void app_capture_synth_poll(app_t *app, uint32_t now) {
    pad_capture_t *c = &app->capture;
    if (!c->seq_mode) return;
    if (c->stage == 1) {
        if (c->seq_phase == REC_PH_OPENING && audio_capture_listening()) {   // the file is open, the tap waits: the pass now
            if (!audio_seq_record(rec_loop(c->seq_mode) ? 1 : 0, rec_stops(c->seq_mode))) return;
            c->halts = audio_seq_halts();
            c->seq_phase = REC_PH_PASS;
            if (!app->seq.running) { seq_set_running(&app->seq, true); app->dirty = true; }
        } else if (c->seq_phase == REC_PH_PASS) {
            const uint32_t h = audio_seq_halts();
            if (h != c->halts) {                                            // the engine stopped the run where the pass ended
                c->halts = h;
                c->halted = true;
                if (app->seq.running) { seq_set_running(&app->seq, false); app->dirty = true; }
            } else if (!app->seq.running && !c->halted) {                    // stopped by hand (or the section left): it ends there
                audio_capture_stop();
                audio_seq_record(-1, false);
                c->halted = true;
            }
        }
        uint32_t frames = 0, dropped = 0;
        const int st = audio_capture_poll(&frames, &dropped);
        if (st == CAPTURE_ST_DONE) { c->frames = frames; c->dropped = dropped; c->stage = 2; c->since = now; app->ui.seq_audio = false; app->dirty = true; }
        else if (st == CAPTURE_ST_EMPTY || st == CAPTURE_ST_FAILED || st == CAPTURE_ST_IDLE) {
            audio_capture_done();
            audio_seq_record(-1, false);
            synth_rec_end(app);
            if (st == CAPTURE_ST_FAILED) popup_error(&app->popup, "REC", "The recording could not be written to the card.", NULL, NULL);
            else rec_info(app, "Nothing recorded");
        }
    }
    if (c->stage == 2) {
        if (app_sample_use(app, c->name, true) >= 0) {                      // in the catalog: where does it go?
            audio_capture_done();
            ask_destination(app);
        } else if (now - c->since > 15000) {
            audio_capture_done();
            synth_rec_end(app);
            popup_error(&app->popup, "REC", "The recording did not show up in the library.", NULL, NULL);
        }
    }
}
