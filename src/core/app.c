#include "core/app.h"
#include "core/gui.h"
#include "core/sprites.h"
#include "hal/hal_audio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void app_init(app_t *app, u8g2_t *display) {
    memset(&app->in, 0, sizeof app->in);
    app->in.axis_x = app->in.axis_y = INPUT_AXIS_CENTER;
    app->dirty = false;
    app->status[0] = 0;
    app->display = display;
    synth_params_default(&app->params);
    seq_init(&app->seq);
    rack_init_startup(&app->rack);
    synth_ui_init(&app->ui, &app->rack);
    audio_build(&app->rack, &app->params);
    synth_ui_draw(&app->ui, &app->params, &app->seq, &app->rack, app->display);
}

/* ---------------- actions ---------------- */

static void set_status(app_t *app, const binding_t *b, int amount) {
    snprintf(app->status, sizeof app->status, "%s > %s %+d", control_name(b->ctl), action_name(b->act), amount);
}

// Gives the UI one event; pushes the sound to the audio side when a value changed.
static void ui_event(app_t *app, ui_event_t ev) {
    if (synth_ui_handle(&app->ui, &app->params, &app->seq, &app->rack, ev)) audio_set_params(&app->rack, &app->params);
    app->dirty = true;
}

// The same event `n` times in the direction of n's sign (an encoder can report several detents at once).
static void ui_event_n(app_t *app, int n, ui_event_t down_or_next, ui_event_t up_or_prev) {
    bool changed = false;
    const ui_event_t ev = n > 0 ? down_or_next : up_or_prev;
    if (n < 0) n = -n;
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) changed |= synth_ui_handle(&app->ui, &app->params, &app->seq, &app->rack, ev);
    if (changed) audio_set_params(&app->rack, &app->params);
    app->dirty = true;
}

static void note_on(app_t *app, control_id_t ctl, int semitones) {
    const int note = KEYBOARD_BASE_NOTE + semitones + 12 * app->in.octave;
    if (note < 0 || note > 127 || app->in.held[ctl]) return;
    app->in.held[ctl] = (uint8_t)(note + 1);
    audio_note_on(note);
}

static void note_off(app_t *app, control_id_t ctl) {
    if (!app->in.held[ctl]) return;
    audio_note_off(app->in.held[ctl] - 1);
    app->in.held[ctl] = 0;
}

// Runs one binding for one event. `e.kind` is IN_RELEASE only for hold actions.
static void app_run_action(app_t *app, const binding_t *b, input_event_t e) {
    const int amount = e.kind == IN_DELTA ? e.value : 1;
    const int n = amount * b->arg;
    bool changed;
    switch (b->act) {
        case ACT_ROW_MOVE:     ui_event_n(app, n, UI_DOWN, UI_UP); set_status(app, b, n); break;
        case ACT_NAV: {
            static const ui_event_t nav[4] = {UI_NAV_LEFT, UI_NAV_RIGHT, UI_NAV_UP, UI_NAV_DOWN};
            ui_event(app, nav[b->arg & 3]);
            snprintf(app->status, sizeof app->status, "%s > Nav", control_name(b->ctl));
            break;
        }
        case ACT_LATCH:        ui_event(app, UI_LATCH); snprintf(app->status, sizeof app->status, "%s > %s", control_name(b->ctl), app->ui.latched ? "Latched" : "Released"); break;
        case ACT_VALUE_ADJUST: ui_event_n(app, n, UI_VALUE_INC, UI_VALUE_DEC); set_status(app, b, n); break;
        case ACT_PAGE_MOVE:    ui_event_n(app, n, UI_PAGE_NEXT, UI_PAGE_PREV); set_status(app, b, n); break;
        case ACT_ROW_TOP:      ui_event(app, UI_ROW_TOP); break;
        case ACT_SELECT:       ui_event(app, UI_SELECT); break;
        case ACT_MENU:         ui_event(app, UI_MENU); break;
        case ACT_BACK:         ui_event(app, UI_BACK); break;
        case ACT_PLAY:         ui_event(app, UI_PLAY); break;
        case ACT_SHIFT:
            app->in.shift = e.kind != IN_RELEASE;
            break;
        case ACT_NOTE:
            if (e.kind == IN_RELEASE) note_off(app, b->ctl); else note_on(app, b->ctl, b->arg);
            break;
        case ACT_OCTAVE:
            app->in.octave += b->arg;
            if (app->in.octave < KEYBOARD_OCTAVE_MIN) app->in.octave = KEYBOARD_OCTAVE_MIN;
            if (app->in.octave > KEYBOARD_OCTAVE_MAX) app->in.octave = KEYBOARD_OCTAVE_MAX;
            snprintf(app->status, sizeof app->status, "Octave %+d", app->in.octave);
            break;
        case ACT_PAGE_KNOB:
            changed = synth_ui_knob_row(&app->ui, &app->params, &app->seq, &app->rack, b->arg, e.value);
            if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; }
            break;
        case ACT_MACRO:
            changed = synth_ui_macro(&app->ui, &app->params, &app->seq, &app->rack, b->arg, e.value);
            if (changed) { audio_set_params(&app->rack, &app->params); app->dirty = true; }
            synth_ui_macro_describe(&app->ui, &app->rack, b->arg, app->status, (int)sizeof app->status);
            break;
        case ACT_MACRO_LEARN:
            synth_ui_macro_learn(&app->ui, &app->rack, b->arg);
            synth_ui_macro_describe(&app->ui, &app->rack, b->arg, app->status, (int)sizeof app->status);
            break;
        case ACT_MASTER_VOLUME:
            if (synth_ui_set_volume(&app->ui, &app->rack, e.value)) { audio_set_params(&app->rack, &app->params); app->dirty = true; }
            break;
        default: break;
    }
}

// Looks the event up in the binding table and runs every row that matches.
static void app_dispatch(app_t *app, input_event_t e) {
    const uint8_t mods = app->in.shift ? MODS_SHIFT : MODS_NONE;
    for (int i = 0; i < bindings_count; i++) {
        const binding_t *b = &bindings[i];
        if (b->ctl != e.ctl) continue;
        if (e.kind == IN_RELEASE) {                         // only hold actions care, and the modifier state does not matter
            if (action_is_hold(b->act) && b->on == IN_PRESS) app_run_action(app, b, e);
        } else if (b->on == e.kind && (b->mods & mods)) {
            app_run_action(app, b, e);
        }
    }
}

/* ---------------- joystick as four buttons ---------------- */

static control_id_t joy_direction(const input_state_t *in) {
    const int dx = in->axis_x - INPUT_AXIS_CENTER, dy = in->axis_y - INPUT_AXIS_CENTER;
    const bool x_out = in->axis_x < JOY_THRESHOLD_LOW || in->axis_x > JOY_THRESHOLD_HIGH;
    const bool y_out = in->axis_y < JOY_THRESHOLD_LOW || in->axis_y > JOY_THRESHOLD_HIGH;
    if (!x_out && !y_out) return CTL_NONE;
    if (x_out && (!y_out || abs(dx) >= abs(dy))) return dx < 0 ? CTL_JOY_LEFT : CTL_JOY_RIGHT;
    return dy < 0 ? CTL_JOY_UP : CTL_JOY_DOWN;                  // low axis value = pushed up
}

static void joy_update(app_t *app, uint32_t now) {
    const control_id_t dir = joy_direction(&app->in);
    if (dir == app->in.joy_dir) return;
    if (app->in.joy_dir != CTL_NONE) app_dispatch(app, (input_event_t){app->in.joy_dir, IN_RELEASE, 0, false});
    app->in.joy_dir = dir;
    if (dir != CTL_NONE) {
        app_dispatch(app, (input_event_t){dir, IN_PRESS, 0, false});
        app->in.joy_next_ms = now + JOY_REPEAT_FIRST_MS;
    }
}

static void joy_repeat(app_t *app, uint32_t now) {
    if (app->in.joy_dir == CTL_NONE || (int32_t)(now - app->in.joy_next_ms) < 0) return;
    app_dispatch(app, (input_event_t){app->in.joy_dir, IN_PRESS, 0, false});
    app->in.joy_next_ms = now + JOY_REPEAT_MS;
}

/* ---------------- the step ---------------- */

bool app_step(app_t *app, input_event_t e) {
    if (e.quit) return false;
    app->dirty = false;
    const uint32_t now = audio_millis();

    if (e.kind != IN_NONE) {
        if (e.ctl == CTL_JOY_X || e.ctl == CTL_JOY_Y) {
            if (e.ctl == CTL_JOY_X) app->in.axis_x = e.value; else app->in.axis_y = e.value;
            joy_update(app, now);
        } else {
            app_dispatch(app, e);
        }
    }
    joy_repeat(app, now);

    if (app->ui.rebuild) {              // the rack editor was just left: rebuild the synth from it
        app->ui.rebuild = false;
        audio_build(&app->rack, &app->params);
    }

    audio_update();

    // Run indicator animation: created when the sequencer starts, deleted when it stops.
    if (app->seq.running && app->ui.run_anim == GUI_ANIM_INVALID)
        app->ui.run_anim = gui_anim_add(&spr_eq, 0, 3, 120, true);
    else if (!app->seq.running && app->ui.run_anim != GUI_ANIM_INVALID) {
        gui_anim_remove(app->ui.run_anim);
        app->ui.run_anim = GUI_ANIM_INVALID;
    }
    bool animated = gui_anim_tick(audio_millis());

    audio_set_clock(app->seq.bpm, app->seq.steps, app->seq.swing, app->seq.running);   // the motion sequencers follow the note sequencer's timing
    if (app->seq.running && app->seq.fresh) audio_motion_restart();                     // a run starts on this tick: the lanes start with it
    seq_event_t se;
    bool stepped = seq_tick(&app->seq, audio_millis(), &se);
    for (int i = 0; i < se.n_off; i++) audio_note_off(se.off[i]);
    if (se.on)  audio_note_on(se.on);

    // Redraw only when something visible changed: a full-frame flush over I2C is slow and must not starve the audio loop.
    // Notes and the modifier do not change the screen.
    if (app->dirty || ((stepped || animated) && synth_ui_shows_playhead(&app->ui, &app->rack)))
        synth_ui_draw(&app->ui, &app->params, &app->seq, &app->rack, app->display);
    return true;
}
