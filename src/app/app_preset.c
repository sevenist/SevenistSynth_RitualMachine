// The working state and the presets (core/patch.h). The sound that plays is a working copy: it is written to the card a moment after
// its last change (synth.cfg, with the sequencer) and read back at power-on. A preset is a file of the card's presets folder: Load
// copies one into the working copy, Save and Save as write the working copy out, and nothing else ever changes a preset.
// The PRESETS tab (scr_preset.c) asks through ui.preset_req; the browser (scr_browse.c, app_library.c) lists the folder.
#include "app/app_internal.h"
#include "core/patch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATE_LOOK_MS  250              // how often the sound is compared with what the card holds
#define STATE_SAVE_MS  2000             // ... and how long after its last change it is written (not while a knob still turns)
#define PRESET_INFO_MS 2000

static uint32_t sound_hash(const app_t *app) { return patch_hash(&app->rack, &app->params, &app->ui); }
static uint32_t state_hash(const app_t *app) { return sound_hash(app); }      // (the sequencer has its own files: app_seq.c)
static bool edited(const app_t *app) { return app->preset[0] && sound_hash(app) != app->preset_hash; }

// The preset the sound counts as coming from (""  = none), unchanged since (`clean`) or not.
static void set_preset(app_t *app, const char *name, bool clean) {
    if (name != app->preset) snprintf(app->preset, sizeof app->preset, "%s", name);
    app->preset_hash = clean ? sound_hash(app) : ~sound_hash(app);
    snprintf(app->ui.preset_name, sizeof app->ui.preset_name, "%s", app->preset);
    app->ui.preset_edited = edited(app);
}

// The sound was replaced as a whole: no note of the old one stays, the pages are made again, the engine gets the new one.
static void sound_replaced(app_t *app, bool fresh) {
    for (int c = 0; c < CTL_COUNT; c++) {
        if (app->in.held[c] && !in_pads(app)) audio_note_off(app->in.held[c] - 1);
        app->in.held[c] = 0;
        app->in.latched[c] = false;
    }
    if (fresh) synth_ui_sound_new(&app->ui, &app->rack); else synth_ui_sound_loaded(&app->ui, &app->rack);
    if (!in_pads(app)) audio_build(&app->rack, &app->params);
    app_catch_stale = true;
    app->dirty = true;
}

/* ---------------- the working state (synth.cfg) ---------------- */

void app_state_init(app_t *app) {
    app->preset[0] = 0;
    app->preset_tried = false;
    app->state_pending = false;
    app->state_ms = app->state_look_ms = audio_millis();
    set_preset(app, "", false);
    app->state_hash = app->state_seen = state_hash(app);
}

bool app_state_load(app_t *app) {
    char name[PRESET_NAME_MAX] = "";
    bool was_edited = false;
    const bool ok = patch_load(PATCH_STATE_FILE, &app->rack, &app->params, &app->ui, name, &was_edited);
    if (ok) {
        synth_ui_sound_loaded(&app->ui, &app->rack);
        set_preset(app, name, !was_edited);
        app_catch_stale = true;
        app->dirty = true;
    }
    app->state_hash = app->state_seen = state_hash(app);     // read, or nothing to read: what plays is the reference
    app->state_pending = false;
    return ok;
}

bool app_state_save(app_t *app) {
    bool too_big = false;
    const uint32_t h = state_hash(app);
    if (!patch_save(PATCH_STATE_FILE, &app->rack, &app->params, &app->ui, true, app->preset, edited(app), &too_big)) {
        if (!too_big) return false;
        popup_error(&app->popup, "SOUND", "This sound is too large to be saved: take out macros or modules.", NULL, NULL);
        app->dirty = true;                                   // (said once: this very sound is not tried again)
    }
    app->state_hash = app->state_seen = h;
    app->state_pending = false;
    return true;
}

bool app_state_unsaved(const app_t *app) { return state_hash(app) != app->state_hash; }

// Every step: the sound against what the card holds, a few times a second; written when it has been still for a moment.
static void state_poll(app_t *app, uint32_t now) {
    if (now - app->state_look_ms < STATE_LOOK_MS) return;
    app->state_look_ms = now;
    if (app->ui.br_open == BROWSE_PRESET) return;            // a preset is being tried: the working state is what the card holds
    const bool ed = edited(app);
    if (ed != app->ui.preset_edited) { app->ui.preset_edited = ed; app->dirty = true; }
    const uint32_t h = state_hash(app);
    if (h == app->state_hash) { app->state_pending = false; app->state_seen = h; return; }
    if (!app->state_pending || h != app->state_seen) { app->state_pending = true; app->state_seen = h; app->state_ms = now; return; }
    if (now - app->state_ms < STATE_SAVE_MS || audio_sd_state() == SD_NONE) return;     // no card: saved when one shows up (app_check_sd)
    if (!app_state_save(app)) app->state_ms = now;           // try again a bit later
}

/* ---------------- presets ---------------- */

static bool preset_write(app_t *app, const char *name) {
    char file[PRESET_NAME_MAX + 24], t[POPUP_TEXT_LEN];
    bool too_big = false;
    if (!preset_file(name, file, sizeof file) || !patch_save(file, &app->rack, &app->params, &app->ui, false, NULL, false, &too_big)) {
        popup_error(&app->popup, "PRESETS", too_big ? "This sound is too large for a preset: take out macros or modules." : "The preset could not be written to the card.",
                    NULL, NULL);
        app->dirty = true;
        return false;
    }
    audio_dir_close();                                       // (the folder's list is no longer what the card holds)
    set_preset(app, name, true);
    app_state_save(app);                                     // (the working state names it at once)
    snprintf(t, sizeof t, "Saved: %s", pad_file_base(name));
    popup_info(&app->popup, "PRESETS", t, 0, audio_millis(), PRESET_INFO_MS);
    app->dirty = true;
    return true;
}

// The first name "Preset N" (N from 1) no preset has in the folder of the current preset (the presets folder itself without one).
static bool preset_new_name(const app_t *app, char *out, int n) {
    char dir[PRESET_NAME_MAX] = "", path[PRESET_NAME_MAX + 2];
    const char *base = pad_file_base(app->preset);
    if (base > app->preset) snprintf(dir, sizeof dir, "%.*s", (int)(base - app->preset - 1), app->preset);
    snprintf(path, sizeof path, ":%s", dir);
    int total = audio_dir_open(path);
    if (total < 0 && dir[0]) { dir[0] = 0; total = audio_dir_open(":"); }    // its folder is gone: the presets folder
    if (total < 0) return false;
    int top = 0;
    audio_dir_entry_t e[BROWSE_PAGE];
    for (int k = 0; k < total;) {
        const int got = audio_dir_read(k, e, BROWSE_PAGE);
        if (got <= 0) break;
        for (int i = 0; i < got; i++)
            if (!e[i].dir && !strncmp(e[i].name, "Preset ", 7) && atoi(e[i].name + 7) > top) top = atoi(e[i].name + 7);
        k += got;
    }
    const int len = snprintf(out, (size_t)n, dir[0] ? "%s/Preset %d" : "%sPreset %d", dir, top + 1);
    return len > 0 && len < n;
}

static void save_as(app_t *app) {
    char name[PRESET_NAME_MAX];
    if (preset_new_name(app, name, sizeof name)) { preset_write(app, name); return; }
    popup_info(&app->popup, "PRESETS", "No presets folder", 0, audio_millis(), PRESET_INFO_MS);
    app->dirty = true;
}

static void save_answer(void *ctx, bool yes) {
    app_t *app = (app_t *)ctx;
    if (yes) preset_write(app, app->preset);
}

static void init_answer(void *ctx, bool yes) {
    app_t *app = (app_t *)ctx;
    if (!yes) return;
    const synth_config_t machine = app->rack.cfg;            // the machine's settings stay (ui.cfg)
    rack_init_startup(&app->rack);
    app->rack.cfg.volume = machine.volume; app->rack.cfg.mono = machine.mono; app->rack.cfg.speaker = machine.speaker; app->rack.cfg.knob_mode = machine.knob_mode;
    synth_params_default(&app->params);
    sound_replaced(app, true);
    set_preset(app, "", false);
}

bool app_preset_browse_begin(app_t *app) {
    app->preset_tried = false;
    return app_state_save(app);                              // what plays now is on the card: Back reads it again
}

bool app_preset_try(app_t *app, const char *name) {
    char file[PRESET_NAME_MAX + 24];
    if (!preset_file(name, file, sizeof file) || !patch_load(file, &app->rack, &app->params, &app->ui, NULL, NULL)) return false;
    app->preset_tried = true;
    sound_replaced(app, false);
    return true;
}

void app_preset_browse_end(app_t *app, const char *chosen) {
    if (chosen) { set_preset(app, chosen, true); app_state_save(app); }
    else if (app->preset_tried && app_state_load(app)) sound_replaced(app, false);
    app->preset_tried = false;
    app->dirty = true;
}

void app_preset_poll(app_t *app, uint32_t now) {
    state_poll(app, now);
    const int req = app->ui.preset_req;
    if (req == PRESET_REQ_NONE) return;
    app->ui.preset_req = PRESET_REQ_NONE;
    app->dirty = true;
    if (req == PRESET_REQ_INIT) {
        popup_ask(&app->popup, "PRESETS", "Start a new sound? What is not saved is lost.", false, init_answer, app);
        return;
    }
    if (audio_sd_state() != SD_OK) { popup_info(&app->popup, "PRESETS", "No card", 0, now, PRESET_INFO_MS); return; }
    if (req == PRESET_REQ_LOAD) app_browse_open(app, BROWSE_PRESET);
    else if (req == PRESET_REQ_SAVE && app->preset[0]) {
        char t[POPUP_TEXT_LEN];
        snprintf(t, sizeof t, "Save over %s?", pad_file_base(app->preset));
        popup_ask(&app->popup, "PRESETS", t, true, save_answer, app);
    } else save_as(app);                                     // Save as, or Save on a sound that has no preset yet
}
