// The page list (Shift + Menu; ENGINE_DESIGN.md ADR-044): the machine's screens grouped under headings, MODES (Synth, Pads), MAIN
// (Rack or Pad FX, Presets, Sound, Sequencer or Steps, Macros) and ADVANCED >, a submenu of the settings tabs. The entries follow
// the mode: Rack <-> Pad FX, Sequencer <-> Steps, Sound only in the synth; the synth's tabs picked in Pads go to the synth.
// And the jump list (Menu held MENU_LONG_MS, the synth only: in Pads a long press is a short one, user 2026-10-29): the main view's
// pages; a short Menu press acts at its release, as it did at the press.
#include "app/app_internal.h"
#include "hal/hal_storage.h"
#include <stdio.h>

#define MENU_LONG_MS 500

typedef enum {
    LE_HEAD_MODES, LE_HEAD_MAIN,                           // headings
    LE_SYNTH, LE_PADS, LE_RACK, LE_PRESETS, LE_SOUND, LE_SEQ, LE_MACROS, LE_ADVANCED,
    LE_KEYS, LE_LEDS, LE_MODS, LE_JOY, LE_CURVES, LE_STORAGE,   // the ADVANCED submenu
    LE_COUNT
} list_entry_t;

static const char *entry_name(const app_t *app, list_entry_t e) {
    static const char *const n[LE_COUNT] = {
        [LE_HEAD_MODES] = "MODES", [LE_HEAD_MAIN] = "MAIN", [LE_SYNTH] = "Synth", [LE_PADS] = "Pads", [LE_RACK] = "Rack",
        [LE_PRESETS] = "Presets", [LE_SOUND] = "Sound", [LE_SEQ] = "Sequencer", [LE_MACROS] = "Macros", [LE_ADVANCED] = "Advanced >",
        [LE_KEYS] = "Keys", [LE_LEDS] = "LEDs", [LE_MODS] = "Modifiers", [LE_JOY] = "Joy", [LE_CURVES] = "Curves", [LE_STORAGE] = "Storage",
    };
    if (in_pads(app) && e == LE_RACK) return "Pad FX";
    if (in_pads(app) && e == LE_SEQ) return "Steps";
    return n[e];
}

static bool list_label(void *ctx, int i, char *out, int n) {
    const app_t *app = ctx;
    const list_entry_t e = i < app->list_n ? (list_entry_t)app->list_ent[i] : LE_HEAD_MAIN;
    snprintf(out, (size_t)n, "%s", entry_name(app, e));
    return e == LE_HEAD_MODES || e == LE_HEAD_MAIN;
}

// The entry of the screen on show.
static list_entry_t entry_on_show(const app_t *app) {
    if (in_pads(app)) return app->ui.pad_fx ? LE_RACK : app->ui.pad_steps ? LE_SEQ : LE_PADS;
    switch (synth_ui_where(&app->ui, &app->rack)) {
        case SYNTH_GO_RACK:    return LE_RACK;
        case SYNTH_GO_PRESETS: return LE_PRESETS;
        case SYNTH_GO_SOUND:   return LE_SOUND;
        case SYNTH_GO_SEQ:     return LE_SEQ;
        case SYNTH_GO_MACROS:  return LE_MACROS;
        case SYNTH_GO_KEYS:    return LE_KEYS;
        case SYNTH_GO_LEDS:    return LE_LEDS;
        case SYNTH_GO_MODS:    return LE_MODS;
        case SYNTH_GO_JOY:     return LE_JOY;
        case SYNTH_GO_CURVES:  return LE_CURVES;
        default:               return LE_SYNTH;
    }
}

static void open_list(app_t *app, bool advanced, list_entry_t want);

// A synth screen: the synth section first when Pads is on show.
static void go_synth(app_t *app, synth_go_t where) {
    if (in_pads(app)) app_set_section(app, false);
    synth_ui_go(&app->ui, &app->rack, where);
}

// Storage: the USB card reader (hal_storage.h), once asked; everything saved first, since the machine restarts.
static void on_storage(void *ctx, bool yes) {
    app_t *app = ctx;
    app->dirty = true;
    if (!yes) return;
    app_save_now(app);
    if (!storage_card_reader()) popup_info(&app->popup, "STORAGE", "No card reader on this platform", 0, audio_millis(), POPUP_INFO_MS);
}

void app_storage_ask(app_t *app) {
    popup_ask(&app->popup, "STORAGE", "Restart as a USB card reader? Back: the joystick held 2 s", false, on_storage, app);
    app->dirty = true;
}

static void run_entry(app_t *app, list_entry_t e) {
    switch (e) {
        case LE_SYNTH:    go_synth(app, SYNTH_GO_MAIN); break;
        case LE_PADS:                                           // the section as it was left, the FX chains and the steps closed
            app_set_section(app, true);
            app_pseq_steps_set(app, false);
            if (app->ui.pad_fx) pads_ui_fx_open(&app->ui, false);
            break;
        case LE_RACK:
            if (!in_pads(app)) { go_synth(app, SYNTH_GO_RACK); break; }
            app_pseq_steps_set(app, false);
            if (!app->ui.pad_fx) pads_ui_fx_open(&app->ui, true);
            break;
        case LE_SEQ:
            if (in_pads(app)) app_pseq_steps_set(app, true);
            else go_synth(app, SYNTH_GO_SEQ);
            break;
        case LE_PRESETS:  go_synth(app, SYNTH_GO_PRESETS); break;
        case LE_SOUND:    go_synth(app, SYNTH_GO_SOUND); break;
        case LE_MACROS:   go_synth(app, SYNTH_GO_MACROS); break;
        case LE_KEYS:     go_synth(app, SYNTH_GO_KEYS); break;
        case LE_LEDS:     go_synth(app, SYNTH_GO_LEDS); break;
        case LE_MODS:     go_synth(app, SYNTH_GO_MODS); break;
        case LE_JOY:      go_synth(app, SYNTH_GO_JOY); break;
        case LE_CURVES:   go_synth(app, SYNTH_GO_CURVES); break;
        case LE_STORAGE:  app_storage_ask(app); break;
        case LE_ADVANCED: open_list(app, true, entry_on_show(app)); return;
        default: return;
    }
    app->dirty = true;
    app_catch_stale = true;
}

static bool showing_advanced(const app_t *app) { return app->list_n && app->list_ent[0] == LE_KEYS; }

static void on_pick(void *ctx, int i) {
    app_t *app = ctx;
    if (i < 0) {                                                // Back: from the submenu, the list again on ADVANCED
        if (showing_advanced(app)) open_list(app, false, LE_ADVANCED);
        app->dirty = true;
        return;
    }
    if (i < app->list_n) run_entry(app, (list_entry_t)app->list_ent[i]);
}

// The list (or its submenu) on the entry `want`; a settings tab wanted in the list is its ADVANCED entry.
static void open_list(app_t *app, bool advanced, list_entry_t want) {
    static const uint8_t main_list[] = {LE_HEAD_MODES, LE_SYNTH, LE_PADS, LE_HEAD_MAIN, LE_RACK, LE_PRESETS, LE_SOUND, LE_SEQ, LE_MACROS, LE_ADVANCED};
    static const uint8_t adv_list[] = {LE_KEYS, LE_LEDS, LE_MODS, LE_JOY, LE_CURVES, LE_STORAGE};
    const uint8_t *src = advanced ? adv_list : main_list;
    const int n = advanced ? (int)sizeof adv_list : (int)sizeof main_list;
    if (app->ui.br_open) app_browse_close(app, false);          // (the sample browser goes first, nothing chosen)
    app->list_n = 0;
    for (int k = 0; k < n; k++) {
        if (src[k] == LE_SOUND && in_pads(app)) continue;       // Sound is the synth's
        app->list_ent[app->list_n++] = src[k];
    }
    if (!advanced && want >= LE_KEYS) want = LE_ADVANCED;      // (the submenu's entries)
    int focus = 0;
    for (int k = 0; k < app->list_n; k++) if (app->list_ent[k] == want) focus = k;
    popup_choose_labels(&app->popup, advanced ? "ADVANCED" : "PAGES", app->list_n, focus, list_label, on_pick, app);
    app->dirty = true;
}

void app_list_open(app_t *app, bool advanced) { open_list(app, advanced, entry_on_show(app)); }

/* ---------------- the jump list: Menu held ---------------- */

static bool page_label(void *ctx, int i, char *out, int n) {
    const app_t *app = ctx;
    synth_ui_page_title(&app->ui, &app->rack, i, out, n);
    return false;
}

static void on_page(void *ctx, int i) {
    app_t *app = ctx;
    if (i < 0) { app->dirty = true; return; }
    synth_ui_go_page(&app->ui, &app->rack, i);
    app->dirty = true;
    app_catch_stale = true;
}

static void jump_list_open(app_t *app) {
    if (app->ui.br_open) app_browse_close(app, false);
    if (app->ui.in_rack && app->ui.rack_dirty) synth_ui_go(&app->ui, &app->rack, SYNTH_GO_MAIN);   // the rack was edited: the pages it makes
    const int focus = app->ui.in_rack ? 0 : app->ui.page;
    popup_choose_labels(&app->popup, "MAIN VIEW", app->ui.page_count, focus, page_label, on_page, app);
    app->dirty = true;
}

void app_menu_down(app_t *app, control_id_t ctl, uint32_t now) {
    app->menu_ctl = (uint8_t)ctl;
    app->menu_long = false;
    app->menu_ms = now;
}

void app_menu_poll(app_t *app, uint32_t now) {
    if (app->menu_ctl == CTL_NONE || app->menu_long || in_pads(app) || (int32_t)(now - app->menu_ms) < MENU_LONG_MS) return;   // (signed: the step's time may be older than the press)
    app->menu_long = true;
    if (!popup_modal(&app->popup)) jump_list_open(app);
}

void app_menu_up(app_t *app, control_id_t ctl) {
    if (app->menu_ctl == CTL_NONE || app->menu_ctl != (uint8_t)ctl) return;
    app->menu_ctl = CTL_NONE;
    if (app->menu_long || popup_modal(&app->popup)) return;
    if (in_pads(app)) { pads_ui_fx_open(&app->ui, !app->ui.pad_fx); app->dirty = true; }   // Pads: the FX chains
    else app_ui_event(app, UI_MENU);
}
