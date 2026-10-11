// The Pads section (core/pads.h, scr_pads.c; ADR-041 decisions 14-26): the switch Synth <-> Pads, the pad keys, the column knobs, the
// events of its screens, the chop to pads.
#include "app/app_internal.h"
#include "core/keymap.h"
#include <stdio.h>
#include <string.h>

#define SECTION_INFO_MS 1200

// A pad's settings changed: the engine gets them, pads.cfg is saved a moment later (app_card.c: pads_autosave).
void app_pad_changed(app_t *app, int pad) {
    if (pad < 0 || pad >= PAD_COUNT) return;
    audio_pad_set(pad, &app->pads.pad[pad]);
    app->lib_missing &= ~(1ull << pad);                     // (its file may be another one: app_pads_resolve looks)
    app->lib_dirty = true;
    if (!app->pads_pending) { app->pads_pending = true; app->pads_ms = audio_millis(); }
    app->dirty = true;
}

// The pads' FX chains changed (a module, the routing, a value): the engine gets them, padsfx.cfg is saved a moment later.
static void fx_changed(app_t *app) {
    app->ui.rack_dirty = false;
    audio_pads_fx(&app->pads, &app->rack);
    if (!app->fx_pending) { app->fx_pending = true; app->fx_ms = audio_millis(); }
    app->dirty = true;
}

/* ---------------- chop to pads (the CHOP page) ---------------- */

// Chop to pads (CHOP page): the slices replace the pads they go to, so the app asks first.
static void chop_answer(void *ctx, bool yes) {
    app_t *app = (app_t *)ctx;
    if (!yes) return;
    const int src = app->ui.chop_pad;
    audio_sample_info_t in;
    uint32_t frames = 0, rate = 0;
    for (int i = 0; i < audio_sample_count(); i++)
        if (audio_sample_info(i, &in) && !strcmp(in.name, app->pads.pad[src].file)) { frames = in.frames; rate = in.rate; break; }
    if (app->ui.chop.mode == CHOP_GRID && frames) {                         // whole grid units (decision 43)
        pad_t *p = &app->pads.pad[src];
        const uint32_t u = pad_chop_unit(app->ui.chop.div, rate, app->seq.bpm), end = pad_end(p, frames);
        if (u) {
            const uint64_t k = (end - p->start + u - 1) / u, target = p->start + k * u;
            if (target > frames) {                                           // past the file: a padded version first, the chop follows
                app_render_start(app, src, 0, (int32_t)target, true);
                popup_info(&app->popup, "CHOP", "Padding to the grid...", 0, audio_millis(), POPUP_INFO_MS);
                return;
            }
            if (target != end) { p->end = target >= frames ? 0 : (uint32_t)target; pads_ui_chop_refresh(&app->ui); }
        }
    }
    const int n = pad_chop_apply(&app->pads, src, &app->ui.chop, frames);
    for (int k = 0; k < n; k++) app_pad_changed(app, app->ui.chop.to + k);
    char t[32], a[8];
    pad_label(app->ui.chop.to, a, sizeof a);
    snprintf(t, sizeof t, "%d slices from %s", n, a);
    popup_info(&app->popup, "CHOP", t, 0, audio_millis(), POPUP_INFO_MS);
}

static void chop_ask(app_t *app) {
    const pad_chop_t *c = &app->ui.chop;
    char t[POPUP_TEXT_LEN], a[8], b[8];
    int last = c->to + c->n_cuts;
    if (last >= PAD_COUNT) last = PAD_COUNT - 1;
    pad_label(c->to, a, sizeof a);
    pad_label(last, b, sizeof b);
    snprintf(t, sizeof t, "%d slices to %s..%s? Those pads are replaced.", last - c->to + 1, a, b);
    popup_ask(&app->popup, "CHOP", t, false, chop_answer, app);
}

/* ---------------- the section's screens, keys and knobs ---------------- */

// One UI event in the Pads section (scr_pads.c).
void app_pads_event(app_t *app, ui_event_t ev) {
    app->ui.clock_bpm = app->seq.bpm;
    if (pads_ui_handle(&app->ui, &app->rack, ev)) app_pad_changed(app, pads_ui_pad(&app->ui));
    if (app->ui.br_req) { app->ui.br_req = 0; app_browse_open(app, BROWSE_PAD); }   // the sample row: the browser
    if (app->ui.rack_dirty) fx_changed(app);                // the FX pages edited the chains
    if (app->ui.pad_chop_req) { app->ui.pad_chop_req = false; chop_ask(app); }
    if (app->ui.pad_render_req) {
        app->ui.pad_render_req = false;
        app_render_start(app, pads_ui_pad(&app->ui), app->ui.edit_op, app->ui.edit_amt[app->ui.edit_op % PAD_EDIT_OPS], false);
    }
    if (app->ui.pad_ver_changed) { app->ui.pad_ver_changed = false; app_pad_changed(app, pads_ui_pad(&app->ui)); }
    app->dirty = true;
}

// Synth <-> Pads. The full synth stops in the Pads section (its notes, latched ones too, and the sequencer); the pads stop when it is left
// (the engine loads the other graph). The keys take the section's layout (core/keymap.h).
void app_set_section(app_t *app, bool pads) {
    if (pads == in_pads(app)) return;
    if (app->ui.br_open) app_browse_close(app, false);
    for (int c = 0; c < CTL_COUNT; c++) {
        if (app->in.held[c]) audio_note_off(app->in.held[c] - 1);
        app->in.held[c] = 0;
        app->in.latched[c] = false;
        app->in.pad_held[c] = 0;
    }
    app->in.pad_select = app->in.pad_mute = false;
    if (app->seq.running) seq_set_running(&app->seq, false);
    app->pads_run = false;                                  // the clock stops with the section's music
    if (app->capture.stage == 1) audio_capture_stop();      // (a recording ends with the section's music too)
    app_joy_xy_off(app);
    app->section = pads ? SECTION_PADS : SECTION_SYNTH;
    pads_ui_fx_open(&app->ui, false);                       // (the FX pages borrow the RACK page's selection: start clean on both sides)
    app->ui.rack_dirty = false;
    keymap_set_pads(pads);
    if (pads) audio_pads_build(&app->pads, &app->rack);
    else      audio_build(&app->rack, &app->params);
    char t[24];
    snprintf(t, sizeof t, pads ? "Bank %c" : "Back to the synth", 'A' + app->ui.pad_bank);
    popup_info(&app->popup, pads ? "PADS" : "SYNTH", t, 0, audio_millis(), SECTION_INFO_MS);
    app->dirty = true;
}

// A pad key (ACT_PAD, k = 0..15 of the bank shown): selects that pad; plays it, or with Mute held mutes / unmutes it, with Select held
// opens its editor, with Shift held only selects it. Its release reaches the same pad (pad_held), so a gate pad stops even after a bank change.
void app_pad_key_down(app_t *app, control_id_t ctl, int k) {
    if (!in_pads(app) || k < 0 || k >= PADS_PER_BANK) return;
    if (app->ui.br_open == BROWSE_PAD) {                     // the browser: every pad key plays the highlighted sample (on the pad it is for)
        if (app->ui.br_state == BR_FRESH) app_browse_load(app, true);
        audio_pad_push(app->ui.br_target);
        app->in.pad_held[ctl] = (uint8_t)(app->ui.br_target + 1);
        return;
    }
    const int pad = app->ui.pad_bank * PADS_PER_BANK + k;
    app->ui.pad_cur = (uint8_t)k;
    app->dirty = true;
    if (app->in.pad_mute) {
        app->pads.pad[pad].mute = !app->pads.pad[pad].mute;
        app_pad_changed(app, pad);
        return;
    }
    if (app->in.pad_select) {                                // Select + pad: its editor (decision 25), no sound
        if (!app->ui.pad_edit) { app->ui.pad_edit = true; app->ui.pad_row = 1; app->ui.pad_latched = false; }
        return;
    }
    if (app->in.shift && !app->in.mod) return;               // Shift + pad: selected, no sound (the 4-column board's Select)
    audio_pad_push(pad);
    app->in.pad_held[ctl] = (uint8_t)(pad + 1);
}

void app_pad_key_up(app_t *app, control_id_t ctl) {
    if (!app->in.pad_held[ctl]) return;
    audio_pad_release(app->in.pad_held[ctl] - 1);
    app->in.pad_held[ctl] = 0;
}

// The column knobs in the Pads section: Start, End, Tune and Cut of the selected pad, over their whole range.
void app_pads_knob(app_t *app, int row, int value) {
    static const pad_param_t knob[SYNTH_UI_COL_KNOBS] = {PADP_START, PADP_END, PADP_TUNE, PADP_CUT};
    if (row < 1 || row > SYNTH_UI_COL_KNOBS) return;
    char fl[24], fv[16];
    if (pads_ui_fx_knob(&app->ui, row - 1, (float)value / INPUT_VALUE_MAX, fl, sizeof fl, fv, sizeof fv)) {   // a module's values page: its first four
        if (app->ui.rack_dirty) fx_changed(app);
        popup_info(&app->popup, fl, fv, 0, audio_millis(), POPUP_INFO_MS);
        app->dirty = true;
        return;
    }
    const int pad = pads_ui_pad(&app->ui);
    pad_t *p = &app->pads.pad[pad];
    const pad_param_t k = knob[row - 1];
    audio_sample_info_t in;                                 // the trim needs the sample's length
    uint32_t frames = 0, rate = 0;
    for (int i = 0; p->file[0] && i < audio_sample_count(); i++)
        if (audio_sample_info(i, &in) && !strcmp(in.name, p->file)) { if (!in.pending) { frames = in.frames; rate = in.rate; } break; }
    app->ui.clock_bpm = app->seq.bpm;
    const bool trim = k == PADP_START || k == PADP_END || k == PADP_LSTART;
    if (trim ? pads_ui_trim(&app->ui, pad, k, 0, (float)value / INPUT_VALUE_MAX) : pad_param_set_norm(p, k, (float)value / INPUT_VALUE_MAX, frames))
        app_pad_changed(app, pad);
    char t[24], v[16];
    pad_label(pad, t, sizeof t);
    snprintf(t + strlen(t), sizeof t - strlen(t), " %s", pad_param_label(k));
    pad_param_format(p, k, frames, rate, v, sizeof v);
    popup_info(&app->popup, t, v, 0, audio_millis(), POPUP_INFO_MS);
    app->dirty = true;
}

// Every step in the Pads section: its jobs go on; the grid shows which pads sound and the header the clock, so a change of either
// redraws.
void app_pads_poll(app_t *app, uint32_t now) {
    if (!in_pads(app)) return;
    app->ui.clock_bpm = app->seq.bpm;
    if (pads_ui_tick(&app->ui)) app->dirty = true;                       // a transient scan finished
    app_render_poll(app, now);
    app_capture_poll(app, now);
    const uint64_t pm = audio_pads_playing();
    if (pm != app->pads_shown) { app->pads_shown = pm; app->dirty = true; }
    int bar = 0, beat = 0, frac = 0;
    const bool run = audio_transport(&bar, &beat, &frac);
    const uint32_t cs = run ? (uint32_t)bar << 4 | (uint32_t)beat << 1 | (frac < 64) | 1u << 31 : 0;
    if (cs != app->clock_shown) { app->clock_shown = cs; app->dirty = true; }
}
