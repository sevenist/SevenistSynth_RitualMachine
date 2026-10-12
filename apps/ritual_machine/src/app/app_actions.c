// What an action does: app_run_action runs one binding (core/bindings.h) or key function (core/keymap.h) for one input event, mostly by
// giving the UI an event. Also the notes of the keyboard and the tempo.
#include "app/app_internal.h"
#include "core/fine_step.h"
#include "core/keymap.h"
#include <stdio.h>
#include <string.h>

static void set_status(app_t *app, const binding_t *b, int amount) {
    snprintf(app->status, sizeof app->status, "%s > %s %+d", control_name(b->ctl), action_name(b->act), amount);
}

/* ---------------- UI events ---------------- */

// Gives the UI one event; pushes the sound to the audio side when a value changed.
void app_ui_event(app_t *app, ui_event_t ev) {
    if (app->ui.br_open) { browse_ui_event(&app->ui, ev, audio_millis()); app->dirty = true; return; }   // the sample browser is on top
    if (in_pads(app)) { app_pads_event(app, ev); return; }
    if (synth_ui_handle(&app->ui, &app->params, &app->seq, &app->rack, ev)) audio_set_params(&app->rack, &app->params);
    if (app->ui.br_req) { app->ui.br_req = 0; app_browse_open(app, BROWSE_RACK); }                             // the SAMPLES tab's File row
    if (app->ui.seq_msg[0]) {                                                                                  // a sequencer tool ran: what it did
        popup_info(&app->popup, "SEQUENCER", app->ui.seq_msg, 0, audio_millis(), POPUP_INFO_MS);
        app->ui.seq_msg[0] = 0;
    }
    app->dirty = true;
    app_catch_stale = true;
}

// The same event `n` times in the direction of n's sign (an encoder can report several detents at once).
static void ui_event_n(app_t *app, int n, ui_event_t down_or_next, ui_event_t up_or_prev) {
    bool changed = false;
    const ui_event_t ev = n > 0 ? down_or_next : up_or_prev;
    if (n < 0) n = -n;
    if (n > 16) n = 16;
    if (app->ui.br_open || in_pads(app)) { for (int i = 0; i < n; i++) app_ui_event(app, ev); return; }
    for (int i = 0; i < n && !app->ui.br_req; i++) changed |= synth_ui_handle(&app->ui, &app->params, &app->seq, &app->rack, ev);
    if (app->ui.br_req) { app->ui.br_req = 0; app_browse_open(app, BROWSE_RACK); }
    if (changed) audio_set_params(&app->rack, &app->params);
    app->dirty = true;
    app_catch_stale = true;
}

/* ---------------- notes ---------------- */

static void note_on(app_t *app, control_id_t ctl, int semitones) {
    const int note = KEYBOARD_BASE_NOTE + semitones + 12 * app->in.octave;
    if (note < 0 || note > 127 || app->in.held[ctl]) return;
    app->in.held[ctl] = (uint8_t)(note + 1);
    audio_note_on(note);
    app_seq_note_down(app, ctl, note);
}

void app_note_off(app_t *app, control_id_t ctl) {
    if (!app->in.held[ctl] || app->in.latched[ctl]) return;     // a latched note keeps sounding after its key is released
    audio_note_off(app->in.held[ctl] - 1);
    app->in.held[ctl] = 0;
}

// Shift + Back: stops every latched note (see ACT_NOTE). Returns how many.
static int release_latched(app_t *app) {
    int n = 0;
    for (int c = 0; c < CTL_COUNT; c++)
        if (app->in.latched[c]) { app->in.latched[c] = false; app_note_off(app, (control_id_t)c); n++; }
    return n;
}

/* ---------------- a modal popup takes the actions ---------------- */

// While a modal popup is up it gets the actions (so the key layout works as elsewhere). Notes, Shift and the master volume still act;
// everything else is ignored, so nothing edits the screen behind the popup. Returns true when the popup took the action.
static bool popup_action(app_t *app, const binding_t *b, input_event_t e) {
    const int amount = e.kind == IN_DELTA ? e.value * b->arg : b->arg;
    switch (b->act) {
        case ACT_NOTE: case ACT_SHIFT: case ACT_MOD: case ACT_MASTER_VOLUME: case ACT_VOLUME_STEP:
            return false;
        case ACT_NAV:
            if (b->arg == NAV_LEFT || b->arg == NAV_RIGHT) popup_move(&app->popup, b->arg == NAV_LEFT ? -1 : 1);
            break;
        case ACT_VALUE_ADJUST: case ACT_VALUE_FINE: case ACT_ROW_MOVE: case ACT_PAGE_MOVE:
            popup_move(&app->popup, amount);
            break;
        case ACT_LATCH: case ACT_SELECT:
            if (e.kind != IN_RELEASE) popup_confirm(&app->popup);
            break;
        case ACT_BACK:
            if (e.kind != IN_RELEASE) popup_cancel(&app->popup);
            break;
        default: break;
    }
    app->dirty = true;
    return true;
}

/* ---------------- the machine clock (ADR-041 decision 24): one tempo, the sequencer's BPM ---------------- */

// The tempo; a popup only for Tap (the Pads header shows the BPM: user, 2026-10-10) and in the synth section, which has no BPM on screen.
static void set_tempo(app_t *app, int bpm, const char *title) {
    app->seq.bpm = bpm < 40 ? 40 : bpm > 240 ? 240 : bpm;
    char t[16];
    snprintf(t, sizeof t, "%d BPM", app->seq.bpm);
    if (!in_pads(app) || !strcmp(title, "TAP")) popup_info(&app->popup, title, t, 0, audio_millis(), POPUP_INFO_MS);
    app->dirty = true;
    app_catch_stale = true;                                     // a knob on the BPM row re-measures it
}

/* ---------------- actions ---------------- */

// Runs one binding for one event. `e.kind` is IN_RELEASE only for hold actions.
void app_run_action(app_t *app, const binding_t *b, input_event_t e) {
    if (popup_modal(&app->popup) && popup_action(app, b, e)) return;
    if (app->ui.learn_wait && e.kind != IN_RELEASE && app_learn_push(app, b->act)) return;
    const int amount = e.kind == IN_DELTA ? e.value : 1;
    const int n = amount * b->arg;
    bool changed;
    switch (b->act) {
        case ACT_ROW_MOVE:     ui_event_n(app, n, UI_DOWN, UI_UP); set_status(app, b, n); break;
        case ACT_NAV: {
            static const ui_event_t nav[4] = {UI_NAV_LEFT, UI_NAV_RIGHT, UI_NAV_UP, UI_NAV_DOWN};
            app_ui_event(app, nav[b->arg & 3]);
            snprintf(app->status, sizeof app->status, "%s > Nav", control_name(b->ctl));
            break;
        }
        case ACT_LATCH:
            if (!in_pads(app) && !app->ui.in_rack && app_joy_bind(app)) break;   // the pages: a push on a parameter row binds it to a joystick axis
            app_ui_event(app, UI_LATCH); snprintf(app->status, sizeof app->status, "%s > %s", control_name(b->ctl), app->ui.latched ? "Latched" : "Released");
            break;
        case ACT_JOY_MODE:
            if (!in_pads(app)) app_joy_mode(app, !app->ui.joy_xy);
            break;
        case ACT_VALUE_ADJUST: ui_event_n(app, n, UI_VALUE_INC, UI_VALUE_DEC); set_status(app, b, n); break;
        case ACT_VALUE_FINE:                                // the same in fine steps (core/fine_step.h)
            g_fine_step = true;
            ui_event_n(app, n, UI_VALUE_INC, UI_VALUE_DEC);
            g_fine_step = false;
            set_status(app, b, n);
            break;
        case ACT_PAGE_MOVE:    ui_event_n(app, n, UI_PAGE_NEXT, UI_PAGE_PREV); set_status(app, b, n); break;
        case ACT_ROW_TOP:      app_ui_event(app, UI_ROW_TOP); break;
        case ACT_SELECT:       app_ui_event(app, UI_SELECT); break;
        case ACT_MENU:                                      // Shift + Menu (its Shift entry Default): Synth <-> Pads
            if (app->ui.br_open) app_browse_close(app, false);  // (the sample browser goes first, nothing chosen)
            if (app->in.shift && !app->in.mod) app_set_section(app, !in_pads(app));
            else if (in_pads(app)) { pads_ui_fx_open(&app->ui, !app->ui.pad_fx); app->dirty = true; }   // Pads: the FX chains
            else app_ui_event(app, UI_MENU);
            break;
        case ACT_SECTION:      app_set_section(app, !in_pads(app)); break;
        case ACT_PAD:
            if (e.kind == IN_RELEASE) app_pad_key_up(app, b->ctl); else app_pad_key_down(app, b->ctl, b->arg);
            break;
        case ACT_PAD_BANK:
            if (in_pads(app) && b->arg >= 0 && b->arg < PAD_BANKS) { app->ui.pad_bank = (uint8_t)b->arg; app->dirty = true; }
            break;
        case ACT_PAD_STOP:                                  // (the same as Shift + Back, with the same popup)
            if (in_pads(app)) {
                audio_pads_stop();
                popup_info(&app->popup, "PADS", "Stop all", 0, audio_millis(), LEARN_INFO_MS);
                app->dirty = true;
            }
            break;
        case ACT_PAD_SELECT:   app->in.pad_select = e.kind != IN_RELEASE; break;
        case ACT_PAD_MUTE:     app->in.pad_mute = e.kind != IN_RELEASE; break;
        case ACT_BACK:                                      // Shift + Back (its Shift entry Default): release all latched notes (Pads: stop every pad), Shift only like the latch
            if (app->in.shift && !app->in.mod && in_pads(app)) {
                audio_pads_stop();
                popup_info(&app->popup, "PADS", "Stop all", 0, audio_millis(), LEARN_INFO_MS);
                app->dirty = true;
            } else if (app->in.shift && !app->in.mod) {
                snprintf(app->status, sizeof app->status, "Released %d", release_latched(app));
                popup_info(&app->popup, "LATCH", app->status, 0, audio_millis(), LEARN_INFO_MS);
                app->dirty = true;
            } else app_ui_event(app, UI_BACK);
            break;
        case ACT_PLAY:                                      // Pads: the clock (a start is bar 1); the synth: the sequencer, the clock with it
            // Pads: Select + Play = step mode (user, 2026-10-12); Shift + Play too: Shift is what a board without the right block
            // (and the simulator) has in place of Select, as for Shift + pad
            if (in_pads(app) && (app->in.pad_select || (app->in.shift && !app->in.mod))) app_pseq_steps_set(app, !app->ui.pad_steps);
            else if (in_pads(app)) { app->pads_run = !app->pads_run; app->dirty = true; }
            else if (app->in.shift && !app->in.mod) app_seq_rec_set(app, !app->rec.armed);   // Shift + Play (its Shift entry Default): Rec
            else app_ui_event(app, UI_PLAY);
            break;
        case ACT_TEMPO:        set_tempo(app, app->seq.bpm + n, "TEMPO"); break;
        case ACT_TAP: {
            const int bpm = seq_tap(&app->tap, audio_millis());
            if (bpm) set_tempo(app, bpm, "TAP");
            else { popup_info(&app->popup, "TAP", "Tap again", 0, audio_millis(), POPUP_INFO_MS); app->dirty = true; }
            break;
        }
        case ACT_PAD_REC:                                   // Rec: the Pads section's capture; the synth: the sequencer's recording
            if (in_pads(app) && app->in.pad_select) app_pseq_rec_set(app, !app->prec.armed);       // Select + Rec: the pattern's live record
            else if (in_pads(app)) app_capture_key(app);
            else app_seq_rec_set(app, !app->rec.armed);
            break;
        case ACT_METRONOME:
            app->metronome = !app->metronome;
            audio_set_metronome(app->metronome);
            popup_info(&app->popup, "CLICK", app->metronome ? "On" : "Off", 0, audio_millis(), POPUP_INFO_MS);
            app->dirty = true;
            break;
        case ACT_SHIFT:
            app->in.shift = e.kind != IN_RELEASE;
            break;
        case ACT_MOD:
            app->in.mod = e.kind != IN_RELEASE;
            break;
        case ACT_NOTE:                                      // Shift + note (its Shift entry Default) latches the note; any next press of the key releases it
            if (e.kind == IN_RELEASE) app_note_off(app, b->ctl);
            else if (app->in.latched[b->ctl]) { app->in.latched[b->ctl] = false; app_note_off(app, b->ctl); }
            else {
                note_on(app, b->ctl, b->arg);
                app->in.latched[b->ctl] = app->in.shift && !app->in.mod && app->in.held[b->ctl];   // Shift only: Mod + note plays as without modifier
            }
            break;
        case ACT_OCTAVE:
            app->in.octave += n;
            if (app->in.octave < KEYBOARD_OCTAVE_MIN) app->in.octave = KEYBOARD_OCTAVE_MIN;
            if (app->in.octave > KEYBOARD_OCTAVE_MAX) app->in.octave = KEYBOARD_OCTAVE_MAX;
            snprintf(app->status, sizeof app->status, "Octave %+d", app->in.octave);
            break;
        case ACT_PAGE_KNOB:
            if (in_pads(app)) { app_pads_knob(app, b->arg, e.value); break; }
            changed = synth_ui_knob_row(&app->ui, &app->params, &app->seq, &app->rack, b->arg, e.value);
            if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; }
            break;
        case ACT_JUMP: {
            const int slot = b->arg;
            if (slot < 0 || slot >= SYNTH_UI_JUMP_SLOTS) break;
            if (app->in.shift || app->in.mod) {               // Shift / Mod + jump key saves (when its layer entry is Default)
                synth_ui_jump_save(&app->ui, &app->rack, slot);
                snprintf(app->status, sizeof app->status, "Saved jump %d", slot + 1);
                app->dirty = true;
            } else {
                app_ui_event(app, (ui_event_t)(UI_JUMP_1 + slot));
                const bool ready = synth_ui_jump_ready(&app->ui, &app->rack, slot);
                snprintf(app->status, sizeof app->status, ready ? "Jump %d" : app->ui.jump[slot].valid ? "Jump %d: not in this synth" : "Jump %d (empty)", slot + 1);
                if (!ready && app->ui.jump[slot].valid) {           // saved, but its page or tab is not shown by this synth type (it comes back with the type)
                    char t[24];
                    snprintf(t, sizeof t, "Jump %d", slot + 1);
                    popup_info(&app->popup, t, "Not in this synth", 0, audio_millis(), POPUP_INFO_MS);
                }
            }
            break;
        }
        case ACT_MACRO:
            changed = synth_ui_macro(&app->ui, &app->params, &app->seq, &app->rack, SYNTH_UI_KNOB_MACRO + (b->arg % SYNTH_UI_MACROS), b->arg, e.value);
            if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; app_catch_stale = true; }
            synth_ui_macro_describe(&app->ui, &app->rack, b->arg, app->status, (int)sizeof app->status);
            break;
        case ACT_MACRO_LEARN: {                             // adds the row under the cursor to the macro
            char t[24], name[24], x[40];
            macro_t m;
            bool repeat = false;                            // a knob sends many events per turn: "Already in it" only once it was not just added
            snprintf(t, sizeof t, "MACRO %d", b->arg + 1);
            if (!synth_ui_target_at_cursor(&app->ui, &app->rack, &m)) snprintf(x, sizeof x, "Not a parameter");
            else {
                synth_ui_target_describe(NULL, NULL, &app->rack, &m, name, sizeof name, NULL, 0);
                if (synth_ui_macro_add(&app->ui, b->arg, &m)) snprintf(x, sizeof x, "+ %s (%d of %d)", name, app->ui.macro[b->arg].n, SYNTH_UI_MACRO_DESTS);
                else { snprintf(x, sizeof x, "%s", app->ui.macro[b->arg].n >= SYNTH_UI_MACRO_DESTS ? "Full (8 destinations)" : "Already in it"); repeat = e.kind == IN_VALUE; }
            }
            if (!repeat || !popup_info_showing(&app->popup)) popup_info(&app->popup, t, x, 0, audio_millis(), LEARN_INFO_MS);
            synth_ui_macro_describe(&app->ui, &app->rack, b->arg, app->status, (int)sizeof app->status);
            app->dirty = true;
            break;
        }
        case ACT_MASTER_VOLUME:
            if (synth_ui_set_volume(&app->ui, &app->rack, e.value)) { audio_set_params(&app->rack, &app->params); app->dirty = true; }
            break;
        case ACT_VOLUME_STEP: {
            changed = false;
            for (int k = 0; k < (n < 0 ? -n : n) && k < 40; k++) changed |= synth_config_adjust(&app->rack.cfg, CFGP_VOLUME, n < 0 ? -1 : 1) != CFG_UNCHANGED;
            if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; }
            snprintf(app->status, sizeof app->status, "Volume %.2f", (double)app->rack.cfg.volume);
            break;
        }
        default: break;
    }
}
