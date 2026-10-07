#include "core/ui_settings.h"
#include "core/ui_internal.h"
#include "core/modifiers.h"
#include "core/led_roles.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// What is saved, field by field (the structs of synth_ui_t have padding, so they are compared by field, not with memcmp).
typedef struct {
    uint8_t     knob_mode;
    jump_slot_t jump[SYNTH_UI_JUMP_SLOTS];
    unsigned    mods_rev;       // the modifier layers: their edit counter (core/modifiers.h)
    unsigned    leds_rev;       // the key LED colours (core/led_roles.h)
    macro_def_t macro[SYNTH_UI_MACROS];   // bytes only (no padding): compared with memcmp
    mapping_t   joy[2];                   // the joystick axes (bytes only too)
} saved_t;

static saved_t g_saved;
static bool    g_have_saved;

static void capture(const synth_ui_t *ui, const rack_t *rack, saved_t *s) {
    memset(s, 0, sizeof *s);
    s->knob_mode = rack->cfg.knob_mode;
    for (int i = 0; i < SYNTH_UI_JUMP_SLOTS; i++) {
        const jump_slot_t *j = &ui->jump[i];          // what the slot points at; `at` is derived from it, not saved
        if (j->valid) s->jump[i] = (jump_slot_t){.valid = true, .in_rack = j->in_rack, .mod_id = j->mod_id, .mod_type = j->mod_type, .def = j->def, .tab = j->tab, .row = j->row};
    }
    s->mods_rev = modifiers_rev();
    s->leds_rev = led_roles_rev();
    memcpy(s->macro, ui->macro, sizeof s->macro);
    memcpy(s->joy, ui->joy, sizeof s->joy);
}

static bool same(const saved_t *a, const saved_t *b) {
    if (a->knob_mode != b->knob_mode) return false;
    for (int i = 0; i < SYNTH_UI_JUMP_SLOTS; i++) {
        const jump_slot_t *x = &a->jump[i], *y = &b->jump[i];
        if (x->valid != y->valid || (x->valid && (x->in_rack != y->in_rack || x->mod_id != y->mod_id || x->mod_type != y->mod_type || x->def != y->def ||
                                                  x->tab != y->tab || x->row != y->row))) return false;
    }
    return a->mods_rev == b->mods_rev && a->leds_rev == b->leds_rev && !memcmp(a->macro, b->macro, sizeof a->macro) &&
           !memcmp(a->joy, b->joy, sizeof a->joy);
}

// The word of a GENERAL setting in the file: its label in lower case ("Vol" -> "vol").
static void cfg_token(cfg_param_id_t id, char *out, int n) {
    snprintf(out, (size_t)n, "%s", synth_config_label(id));
    for (char *c = out; *c; c++) *c = (char)tolower((unsigned char)*c);
}

int ui_settings_to_text(const synth_ui_t *ui, const rack_t *rack, char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "# SynthCore UI settings (ui.cfg)\nknob %s\n", rack->cfg.knob_mode ? "direct" : "catch");
    for (int i = 0; i < SYNTH_UI_JUMP_SLOTS && n < cap; i++) {
        const jump_slot_t *j = &ui->jump[i];
        if (!j->valid) continue;
        if (j->in_rack)      n += snprintf(buf + n, (size_t)(cap - n), "jump %d tab %d row %d\n", i + 1, j->tab, j->row);
        else if (!j->mod_id) n += snprintf(buf + n, (size_t)(cap - n), "jump %d global %d row %d\n", i + 1, j->def, j->row);
        else                 n += snprintf(buf + n, (size_t)(cap - n), "jump %d mod %d type %d def %d row %d\n", i + 1, j->mod_id, j->mod_type, j->def, j->row);
    }
    if (n < cap) n += snprintf(buf + n, (size_t)(cap - n), "macros\n");                 // macro K <target> [range MIN MAX] [curve NAME], one line per destination
    for (int k = 0; k < SYNTH_UI_MACROS && n < cap; k++)
        for (int i = 0; i < ui->macro[k].n && n < cap; i++) {
            char t[64];
            if (mapping_to_text(&ui->macro[k].dest[i], t, sizeof t)) n += snprintf(buf + n, (size_t)(cap - n), "macro %d %s\n", k + 1, t);
        }
    if (n < cap) n += snprintf(buf + n, (size_t)(cap - n), "joy\n");                    // joy x|y <target> [range MIN MAX] [curve NAME]
    for (int a = 0; a < 2 && n < cap; a++) {
        char t[64];
        if (mapping_to_text(&ui->joy[a], t, sizeof t)) n += snprintf(buf + n, (size_t)(cap - n), "joy %s %s\n", a ? "y" : "x", t);
    }
    if (n < cap) n += modifiers_to_text(buf + n, cap - n);
    if (n < cap) n += led_roles_to_text(buf + n, cap - n);
    return n < cap ? n : cap - 1;
}

bool ui_settings_from_text(synth_ui_t *ui, rack_t *rack, const char *txt) {
    bool any = false;
    int knob_mode = rack->cfg.knob_mode;
    jump_slot_t jump[SYNTH_UI_JUMP_SLOTS] = {{0}};
    bool mods = false, legacy = false, macros = false, leds = false, joy = false;                          // the modifier layers are in the file; the first format's "shift N" lines
    mod_entry_t old[SYNTH_UI_COL_KNOBS];
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) old[k] = modifiers_get(MODL_SHIFT, (control_id_t)(CTL_COL_KNOB_0 + k));
    for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        char w1[16], w2[16], w3[16];
        int a = 0, b = 0;
        const int got = sscanf(line, "%15s %15s %15s", w1, w2, w3);
        if (got >= 1 && !strcmp(w1, "modifiers")) {
            mods = any = true;
        } else if (got >= 1 && !strcmp(w1, "macros")) {
            macros = any = true;
        } else if (got >= 1 && !strcmp(w1, "leds")) {
            leds = any = true;
        } else if (got >= 1 && !strcmp(w1, "joy")) {                 // the "joy" header (or an axis line): the file holds the axes
            joy = any = true;
        } else if (got >= 2 && !strcmp(w1, "knob")) {
            if (!strcmp(w2, "catch")) { knob_mode = 0; any = true; }
            else if (!strcmp(w2, "direct")) { knob_mode = 1; any = true; }
        } else if (got >= 1 && !strcmp(w1, "jump")) {
            int row = -1, type = 0, def = 0;
            if (sscanf(line, "%*s %d %15s %d", &a, w2, &b) != 3 || a < 1 || a > SYNTH_UI_JUMP_SLOTS || b < 0 || b > 255) continue;
            jump_slot_t n0 = {.valid = true};
            if (!strcmp(w2, "mod")) {                                     // a module's page: jump N mod ID type T def D row R
                if (b == 0 || sscanf(line, "%*s %*d %*s %*d type %d def %d row %d", &type, &def, &row) != 3) continue;
                n0.mod_id = (uint8_t)b; n0.mod_type = (uint8_t)type; n0.def = (uint8_t)def;
            } else if (!strcmp(w2, "global")) {                          // jump N global D row R
                sscanf(line, "%*s %*d %*s %*d row %d", &row);
                n0.def = (uint8_t)b;
            } else if (!strcmp(w2, "tab")) {                             // jump N tab K row R
                sscanf(line, "%*s %*d %*s %*d row %d", &row);
                n0.in_rack = true; n0.tab = (uint8_t)b;
            } else if (!strcmp(w2, "page") || !strcmp(w2, "menu")) {   // the first format stored positions: take what is there now
                sscanf(line, "%*s %*d %*s %*d row %d", &row);
                if (w2[0] == 'm') {
                    if (b >= tab_count(rack)) continue;
                    n0.in_rack = true; n0.tab = (uint8_t)tab_kind(rack, b);
                } else {
                    if (b >= ui->page_count) continue;
                    const int slot = ui->pg_slot[b];
                    if (slot != GLOBAL_PAGE) { n0.mod_id = rack->slot[slot].id; n0.mod_type = rack->slot[slot].type; }
                    n0.def = ui->pg_def[b];
                }
            } else continue;
            if (row < 0 || row > 255) continue;
            n0.row = (uint8_t)row;
            jump[a - 1] = n0;
            any = true;
        } else if (got >= 3 && !strcmp(w1, "shift") && sscanf(w2, "%d", &a) == 1 && a >= 1 && a <= SYNTH_UI_COL_KNOBS) {
            mod_entry_t *m = &old[a - 1];                            // shift N none | shift N cfg <setting>: Shift + col knob N
            if (!strcmp(w3, "none")) { *m = (mod_entry_t){ME_DEFAULT, ACT_NONE, 0, {MACRO_NONE, 0, 0}}; legacy = any = true; }
            else if (!strcmp(w3, "cfg")) {
                char want[16], tok[16];
                if (sscanf(line, "%*s %*s %*s %15s", want) != 1) continue;
                for (int id = 0; id < CFGP_COUNT; id++) {
                    cfg_token((cfg_param_id_t)id, tok, sizeof tok);
                    if (!strcmp(tok, want)) { *m = (mod_entry_t){ME_PARAM, ACT_NONE, 0, {MACRO_CFG, 0, (uint8_t)id}}; legacy = any = true; }
                }
            }
        }
    }
    if (!any) return false;                                      // not a settings file
    rack->cfg.knob_mode = (uint8_t)knob_mode;
    memcpy(ui->jump, jump, sizeof jump);
    if (leds) {                                                 // the key LED colours: the file lists the roles that differ from the defaults
        led_roles_from_text_begin();
        for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) led_roles_from_line(line);
    }
    if (macros) {                                               // the file holds the macros: they replace the defaults
        memset(ui->macro, 0, sizeof ui->macro);
        for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
            int k = 0, at = 0;
            mapping_t mp;
            if (sscanf(line, "macro %d %n", &k, &at) != 1 || at == 0 || k < 1 || k > SYNTH_UI_MACROS || !mapping_parse(line + at, &mp)) continue;
            macro_def_t *md = &ui->macro[k - 1];
            if (md->n < SYNTH_UI_MACRO_DESTS) md->dest[md->n++] = mp;
        }
        synth_ui_macros_prune(ui, rack);
    }
    if (joy) {                                                  // the joystick axes: the file holds them (an axis not listed is unbound)
        memset(ui->joy, 0, sizeof ui->joy);
        ui->joy_fresh = -1;
        for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
            char ax[4];
            int at = 0;
            mapping_t mp;
            if (sscanf(line, "joy %3s %n", ax, &at) != 1 || at == 0 || (strcmp(ax, "x") && strcmp(ax, "y")) || !mapping_parse(line + at, &mp)) continue;
            ui->joy[ax[0] == 'y'] = mp;
        }
        synth_ui_macros_prune(ui, rack);
    }
    if (mods) {
        modifiers_from_text_begin();
        for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) modifiers_from_line(line);
    } else if (legacy) {
        for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) modifiers_set(MODL_SHIFT, (control_id_t)(CTL_COL_KNOB_0 + k), old[k]);
    }
    synth_ui_jump_resolve(ui, rack);                             // finds their pages now (a slot whose module is not in this rack is dropped)
    return true;
}

static char g_buf[STORAGE_FILE_MAX];

bool ui_settings_load(synth_ui_t *ui, rack_t *rack) {
    const bool ok = storage_read(UI_SETTINGS_FILE, g_buf, sizeof g_buf) >= 0 && ui_settings_from_text(ui, rack, g_buf);
    capture(ui, rack, &g_saved);                                 // loaded, or nothing to load: the current state is the reference
    g_have_saved = true;
    return ok;
}

bool ui_settings_save(const synth_ui_t *ui, const rack_t *rack) {
    const int n = ui_settings_to_text(ui, rack, g_buf, sizeof g_buf);
    if (!storage_write(UI_SETTINGS_FILE, g_buf, n)) return false;
    capture(ui, rack, &g_saved);
    g_have_saved = true;
    return true;
}

bool ui_settings_changed(const synth_ui_t *ui, const rack_t *rack) {
    if (!g_have_saved) return false;
    saved_t now;
    capture(ui, rack, &now);
    return !same(&now, &g_saved);
}
