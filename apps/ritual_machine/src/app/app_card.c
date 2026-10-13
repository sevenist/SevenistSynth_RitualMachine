// The card: the settings files follow it (read when it shows up, saved a moment after a change), and what the platform found on it is
// told with popups (hal_storage.h).
#include "app/app_internal.h"
#include "core/keymap.h"
#include "core/curves.h"
#include "core/ui_settings.h"
#include "core/card_layout.h"
#include <stdio.h>
#include <string.h>

#define SETTINGS_SAVE_MS 2000           // after the first change: a few edits in a row make one write (ui.cfg, curves.cfg, pads.cfg)

// The card changed (inserted, removed, or its files were listed): the sampler modules look their file up again.
// The key layout follows the card: a card that shows up gives its keys.cfg, unless the keys were changed meanwhile (then they are written to it).
void app_check_sd(app_t *app) {
    const uint32_t gen = audio_sd_generation();
    if (gen == app->sd_gen) return;
    app->sd_gen = gen;
    if (app->ui.br_open) app_browse_close(app, false);                      // (the folder it shows may be gone)
    app->lib_missing = 0;                                               // the pads' files are looked for again (app_pads_resolve)
    app->lib_dirty = true;
    app->rec_next = 0;
    memset(app->lib_stamp, 0, sizeof app->lib_stamp);
    if (audio_sd_state() != SD_NONE) {
        if (keymap_dirty()) keymap_save(); else keymap_load();
        if (curves_changed()) curves_save(); else curves_load();
        if (app_state_unsaved(app)) app_state_save(app); else app_state_load(app);   // (before ui.cfg: its jump slots name the sound's modules)
        if (app_seq_unsaved(app)) app_seq_save(app); else app_seq_load(app);         // the sequencer's patterns
        if (app_pseq_unsaved(app)) app_pseq_save(app); else app_pseq_load(app);      // ... and the Pads section's
        if (ui_settings_changed(&app->ui, &app->rack)) ui_settings_save(&app->ui, &app->rack); else ui_settings_load(&app->ui, &app->rack);
        if (app->pads_pending) { if (pads_save(&app->pads)) app->pads_pending = false; } else pads_load(&app->pads);
        if (app->fx_pending) { if (pads_fx_save(&app->pads.fx)) app->fx_pending = false; } else pads_fx_load(&app->pads.fx);
        history_load(&app->history);
    }
    if (in_pads(app)) audio_pads_build(&app->pads, &app->rack);         // the samples are looked up again by name
    else audio_build(&app->rack, &app->params);
    app->dirty = true;
}

/* ---------------- the settings files: saved by themselves a moment after they change ---------------- */

// pads.cfg: written a moment after the last change (like ui.cfg); no card: written when one shows up (app_check_sd). Not while the browser
// tries samples on a pad: that file is not the pad's yet, and each write held the card and the UI for a moment (every two seconds).
static void pads_autosave(app_t *app, uint32_t now) {
    if (!app->pads_pending || now - app->pads_ms < SETTINGS_SAVE_MS || audio_sd_state() == SD_NONE) return;
    if (app->ui.br_open == BROWSE_PAD) { app->pads_ms = now; return; }
    if (pads_save(&app->pads)) app->pads_pending = false;
    else app->pads_ms = now;                                         // try again a bit later
}

static void fx_autosave(app_t *app, uint32_t now) {
    if (!app->fx_pending || now - app->fx_ms < SETTINGS_SAVE_MS || audio_sd_state() == SD_NONE) return;
    if (pads_fx_save(&app->pads.fx)) app->fx_pending = false;
    else app->fx_ms = now;
}

static void settings_autosave(app_t *app, uint32_t now) {
    const bool ui = ui_settings_changed(&app->ui, &app->rack), cv = curves_changed();      // ui.cfg and curves.cfg
    if (!ui && !cv) { app->settings_pending = false; return; }
    if (!app->settings_pending) { app->settings_pending = true; app->settings_ms = now; return; }
    if (now - app->settings_ms < SETTINGS_SAVE_MS || audio_sd_state() == SD_NONE) return;   // no card: saved when one shows up (app_check_sd)
    const bool ok = (!cv || curves_save()) & (!ui || ui_settings_save(&app->ui, &app->rack));
    if (ok) app->settings_pending = false;
    else app->settings_ms = now;                                    // try again a bit later
}

// Every step: ui.cfg and curves.cfg, pads.cfg, padsfx.cfg.
void app_autosave(app_t *app, uint32_t now) {
    settings_autosave(app, now);
    pads_autosave(app, now);
    fx_autosave(app, now);
    app_seq_autosave(app, now);
    app_pseq_autosave(app, now);
}

void app_save_now(app_t *app) {
    if (audio_sd_state() == SD_NONE) return;
    if (keymap_dirty()) keymap_save();
    if (curves_changed()) curves_save();
    if (app_state_unsaved(app)) app_state_save(app);
    if (app_seq_unsaved(app)) app_seq_save(app);
    if (app_pseq_unsaved(app)) app_pseq_save(app);
    if (ui_settings_changed(&app->ui, &app->rack)) ui_settings_save(&app->ui, &app->rack);
    if (app->pads_pending && pads_save(&app->pads)) app->pads_pending = false;
    if (app->fx_pending && pads_fx_save(&app->pads.fx)) app->fx_pending = false;
    app->settings_pending = false;
}

/* ---------------- card events (hal_storage.h): what the platform found, told with popups ---------------- */

#define CARD_INFO_MS 2000
#define CARD_QUIET_MS 5000              // no "Card inserted" in the first seconds after start (the boot-time mount); errors and questions still show

static void answer_folders(void *ctx, bool yes) {
    (void)ctx;
    if (yes) storage_make_folders();                    // STORAGE_EV_FOLDERS_DONE follows
}

void app_card_events(app_t *app) {
    storage_event_t ev;
    char t[POPUP_TEXT_LEN];
    while (storage_poll_event(&ev)) {
        app->dirty = true;
        switch (ev.kind) {
        case STORAGE_EV_INSERTED:
            if (ev.slow) {
                snprintf(t, sizeof t, "A read takes %u ms (max %u). Samples are off for this card.",
                         (unsigned)((ev.read_us + 500) / 1000), (unsigned)(ev.read_limit_us / 1000));
                popup_error(&app->popup, "SD CARD TOO SLOW", t, NULL, NULL);
            } else if (audio_millis() - app->boot_ms >= CARD_QUIET_MS) {   // a card that was in at power-on is no news
                popup_info(&app->popup, "SD CARD", "Card inserted", 0, audio_millis(), CARD_INFO_MS);
            }
            if (ev.missing) {                           // the card is not set up (or only partly): ask before writing to it
                static const char *const dirs[STORAGE_FOLDER_COUNT] = STORAGE_FOLDERS;
                int n = 0, all = (1 << STORAGE_FOLDER_COUNT) - 1;
                if ((ev.missing & all) == all) n = snprintf(t, sizeof t, "No SynthCore folders on this card. Create them?");
                else {
                    n = snprintf(t, sizeof t, "Missing:");
                    for (int i = 0; i < STORAGE_FOLDER_COUNT && n < (int)sizeof t; i++)
                        if (ev.missing & (1 << i)) n += snprintf(t + n, sizeof t - (size_t)n, " %s", dirs[i]);
                    if (n < (int)sizeof t) snprintf(t + n, sizeof t - (size_t)n, ". Create?");
                }
                popup_ask(&app->popup, "SD CARD", t, false, answer_folders, app);
            }
            break;
        case STORAGE_EV_REMOVED:
            popup_info(&app->popup, "SD CARD", "Card removed", 0, audio_millis(), CARD_INFO_MS);
            break;
        case STORAGE_EV_FOLDERS_DONE:
            if (ev.ok) popup_info(&app->popup, "SD CARD", ev.moved ? "Folders created, old files moved to /system" : "Folders created", 0, audio_millis(), CARD_INFO_MS);
            else popup_error(&app->popup, NULL, "The folders could not be created on the card.", NULL, NULL);
            break;
        default: break;
        }
    }
}
