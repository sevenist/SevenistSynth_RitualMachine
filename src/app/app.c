// The application: its start, one frame, and the step that the platform's main loop calls (app.h). The rest is in the other files of
// this folder (app_internal.h lists them).
#include "app/app_internal.h"
#include "app/key_leds.h"
#include "core/gui.h"
#include "core/keymap.h"
#include "core/modifiers.h"
#include "core/curves.h"
#include "core/led_roles.h"
#include "core/ui_settings.h"
#include "core/sprites.h"
#include "core/ui_sprites_gen.h"
#include "hal/hal_display.h"
#include <stdio.h>
#include <string.h>

/* ---------------- the start ---------------- */

void app_init(app_t *app, u8g2_t *display) {
    memset(&app->in, 0, sizeof app->in);
    app->in.axis_x = app->in.axis_y = INPUT_AXIS_CENTER;
    app->dirty = false;
    app->redraw_owed = false;
    app->sd_gen = audio_sd_generation();
    app->keys_notice = false;
    app->menu_open = false;
    app->boot_ms = audio_millis();
    app->boot_anim = GUI_ANIM_INVALID;
    app->started = false;
    app->start_pct = 0;
    app->reset_held = false;
    app->status[0] = 0;
    popup_init(&app->popup);
    keymap_init();
    modifiers_init();
    curves_init();
    led_roles_init();
    keymap_load();                      // the simulator has its card at once; the board's card shows up later (app_check_sd)
    app->keys_notice_ms = 0;
    app->display = display;
    synth_params_default(&app->params);
    seq_init(&app->seq);
    rack_init_startup(&app->rack);
    synth_ui_init(&app->ui, &app->rack);
    pads_init(&app->pads);
    pads_load(&app->pads);              // pads.cfg (the board's card shows up later: app_check_sd)
    app->lib_dirty = true;              // its files go into the catalog one per step (app_pads_resolve)
    app->lib_missing = 0;
    app->lib_tries = 0;
    app->rec_next = 0;
    app->lib_clock = 0;
    memset(app->lib_stamp, 0, sizeof app->lib_stamp);
    pads_fx_load(&app->pads.fx);        // padsfx.cfg: the FX chains
    app->ui.pads = &app->pads;
    pads_ui_init(&app->ui);                 // no chop yet, the EDIT page's amounts
    history_load(&app->history);
    app->ui.history = &app->history;
    memset(&app->render, 0, sizeof app->render);
    app->section = SECTION_SYNTH;
    app->pads_pending = false;
    app->pads_shown = 0;
    app->pads_run = false;
    app->metronome = false;
    memset(&app->tap, 0, sizeof app->tap);
    app->clock_shown = 0;
    curves_load();                      // before the sound and ui.cfg: their mappings name the user curves
    app_state_init(app);
    app_state_load(app);                // synth.cfg: the sound as it was left (the board's card shows up later: app_check_sd)
    ui_settings_load(&app->ui, &app->rack);   // Knob mode, jump slots, the Shift / Mod layers (the board's card shows up later: app_check_sd)
    app->settings_pending = false;
    app->batt_warned = 0;
    app->batt_ms = app->boot_ms;
    audio_build(&app->rack, &app->params);
    if (input_boot_reset()) {           // the reset key was held at power-on (read by the board before its key scan started)
        keymap_reset();
        keymap_save();                  // no card yet: saved when it shows up (app_check_sd)
        app->keys_notice = true;
        app->keys_notice_ms = app->boot_ms;
    }
    app_draw_frame(app);
}

void app_boot_animation(app_t *app) {
    const gui_sprite_t *sp = ui_sprite("64/boot");
    if (!sp || sp->frames < 2 || app->keys_notice || app->boot_anim != GUI_ANIM_INVALID) return;
    app->boot_anim = gui_anim_add(sp, 0, sp->frames - 1, BOOT_FRAME_MS, false);
    if (app->boot_anim != GUI_ANIM_INVALID) app_draw_frame(app);
}

void app_boot_draw_text(u8g2_t *g) {
    const int w = u8g2_GetDisplayWidth(g), h = u8g2_GetDisplayHeight(g);
    if (h < 112) return;
    u8g2_SetFont(g, u8g2_font_7x13B_tr);
    u8g2_DrawStr(g, (w - u8g2_GetStrWidth(g, BOOT_TITLE)) / 2, 22, BOOT_TITLE);
    u8g2_SetFont(g, u8g2_font_5x7_tr);
    u8g2_DrawStr(g, (w - u8g2_GetStrWidth(g, BOOT_VERSION)) / 2, h - 3, BOOT_VERSION);
}

static void boot_anim_end(app_t *app) {
    gui_anim_remove(app->boot_anim);
    app->boot_anim = GUI_ANIM_INVALID;
    app->dirty = true;
}

// The start is over (app.h): told to the platform once, whose boot animation then ends and lets the screen show. Until then the UI
// runs unseen: the card shows up a moment after power-on, its settings replace the defaults and the pads' files are read one by one.
// Its progress (display_boot_progress, a loading bar on the board): START_SETTINGS_PCT once the card's settings are read (the step
// that reads them reports nothing: it is one call), the rest by the pads' files that are in the catalog or known to be missing;
// without a card, the wait for one.
#define START_SETTINGS_PCT 30
static void start_poll(app_t *app, uint32_t now) {
    if (app->started) return;
    const uint32_t t = now - app->boot_ms;
    const bool card = audio_sd_state() == SD_OK;
    const bool loading = card ? app->lib_dirty : t < START_CARD_WAIT_MS;
    if (loading && t < START_MAX_MS) {
        int pct = (int)(t * START_SETTINGS_PCT / START_CARD_WAIT_MS);
        if (card) {
            int files = 0, in = 0;
            for (int k = 0; k < PAD_COUNT; k++) {
                if (!app->pads.pad[k].file[0]) continue;
                files++;
                if ((app->lib_missing >> k & 1) || app_lib_index(app->pads.pad[k].file) >= 0) in++;
            }
            pct = START_SETTINGS_PCT + (files ? (100 - START_SETTINGS_PCT) * in / files : 0);
        }
        if (pct > app->start_pct) { app->start_pct = (uint8_t)pct; display_boot_progress(pct); }
        return;
    }
    app->started = true;
    app->dirty = true;
    display_boot_progress(100);
    display_boot_end();
}

/* ---------------- notices: the key layout was reset ---------------- */

#define KEYS_NOTICE_MS 3000

static void draw_keys_notice(app_t *app) {
    u8g2_t *g = app->display;
    u8g2_ClearBuffer(g);
    u8g2_SetFont(g, u8g2_font_5x7_tr);
    u8g2_DrawFrame(g, 0, 0, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    u8g2_DrawBox(g, 0, 0, u8g2_GetDisplayWidth(g), 10);
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawStr(g, 4, 8, "KEYS RESET");
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawStr(g, 4, 21, "The keys are back to");
    char l[32];
    snprintf(l, sizeof l, "the layout %s.", keymap_layout_name(0));
    u8g2_DrawStr(g, 4, 30, l);
    u8g2_DrawStr(g, 4, 42, "Release the key.");
}

/* ---------------- one frame ---------------- */

// One frame: the full-screen notice or the UI, the popup on top, then the buffer goes to the display.
void app_draw_frame(app_t *app) {
    u8g2_t *g = app->display;
    if (app->keys_notice) draw_keys_notice(app);   // the full-screen notice hides the popups until it goes (they wait in their queue)
    else if (app->boot_anim != GUI_ANIM_INVALID) {  // the boot animation, alone on the screen (popups wait too)
        u8g2_ClearBuffer(g);
        gui_anim_draw_centered(g, app->boot_anim, gui_rect(0, 0, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g)));
        app_boot_draw_text(g);
    } else {
        if (app->ui.br_open) browse_ui_draw(&app->ui, g);
        else if (in_pads(app)) pads_ui_draw(&app->ui, &app->rack, g, app->pads_shown, app->seq.bpm);
        else synth_ui_draw(&app->ui, &app->params, &app->seq, &app->rack, g);
        gui_style_t st;
        gui_style_init(&st, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
        popup_draw(&app->popup, g, &st);
    }
    display_send(g);
}

/* ---------------- the step ---------------- */

// The machine clock (ADR-041 decision 24) runs with the Pads section's Play, or with the synth's sequencer.
static bool clock_running(const app_t *app) { return in_pads(app) ? app->pads_run : app->seq.running; }

bool app_step(app_t *app, input_event_t e) {
    if (e.quit) return false;
    app->dirty = false;
    const uint32_t now = audio_millis();

    app_check_sd(app);
    app_card_events(app);
    if (app->keys_notice && (e.kind == IN_PRESS || e.kind == IN_RELEASE || now - app->keys_notice_ms > KEYS_NOTICE_MS)) {
        app->keys_notice = false;                                       // a key (the reset key let go) or a few seconds
        app->dirty = true;
    }

    if (app->boot_anim != GUI_ANIM_INVALID && (e.kind == IN_PRESS || e.kind == IN_DELTA || gui_anim_finished(app->boot_anim) || app->keys_notice))
        boot_anim_end(app);                                              // played, or skipped by a key, a push or an encoder (not by a knob's noise)

    if (e.kind != IN_NONE) app_input_event(app, e, now);
    app_input_poll(app, now);
    app_autosave(app, now);
    app_power_poll(app, now);
    app_preset_poll(app, now);
    app_browse_poll(app, now);
    app_pads_resolve(app);
    start_poll(app, now);
    app_pads_poll(app, now);

    if (app->menu_open && !app->ui.in_rack && keymap_dirty() && !keymap_save())        // the menu was closed: the key layout goes to the card
        snprintf(app->status, sizeof app->status, "Keys not saved (no card)");
    app->menu_open = app->ui.in_rack;

    if (app->ui.rebuild) {              // the rack editor was just left: rebuild the synth from it
        app->ui.rebuild = false;
        audio_build(&app->rack, &app->params);
    }

    app_learn_poll(app, now);
    app_catch_poll(app);
    if (popup_tick(&app->popup, now)) app->dirty = true;

    audio_update();

    // Run indicator animation: created when the sequencer starts, deleted when it stops.
    if (app->seq.running && app->ui.run_anim == GUI_ANIM_INVALID)
        app->ui.run_anim = gui_anim_add(&spr_eq, 0, 3, 120, true);
    else if (!app->seq.running && app->ui.run_anim != GUI_ANIM_INVALID) {
        gui_anim_remove(app->ui.run_anim);
        app->ui.run_anim = GUI_ANIM_INVALID;
    }
    bool animated = gui_anim_tick(audio_millis());

    audio_set_clock(app->seq.bpm, app->seq.steps, app->seq.swing, clock_running(app));  // the machine clock; the motion sequencers follow it
    if (app->seq.running && app->seq.fresh) audio_motion_restart();                     // a run starts on this tick: the lanes start with it
    seq_event_t se;
    bool stepped = seq_tick(&app->seq, audio_millis(), &se);
    for (int i = 0; i < se.n_off; i++) audio_note_off(se.off[i]);
    if (se.on)  audio_note_on(se.on);

    // Redraw only when something visible changed. Notes and the modifier do not change the screen. While more input is queued the redraw
    // is owed, not done: every pending note and knob turn is handled first and the screen is drawn once after them.
    const bool want = app->dirty || app->redraw_owed || ((stepped || animated) && synth_ui_shows_playhead(&app->ui, &app->rack)) ||
                      (animated && app->boot_anim != GUI_ANIM_INVALID);
    if (want && input_pending()) {
        app->redraw_owed = true;
    } else if (want) {
        app->redraw_owed = false;
        app_draw_frame(app);
    }
    key_leds_update(app, e.kind != IN_NONE, now);
    return true;
}
