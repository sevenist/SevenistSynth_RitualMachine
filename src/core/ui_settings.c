#include "core/ui_settings.h"
#include "core/ui_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// What is saved, field by field (the structs of synth_ui_t have padding, so they are compared by field, not with memcmp).
typedef struct {
    uint8_t     knob_mode;
    jump_slot_t jump[SYNTH_UI_JUMP_SLOTS];
    macro_t     shift[SYNTH_UI_COL_KNOBS];
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
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) s->shift[k] = ui->knob_shift[k];
}

static bool same(const saved_t *a, const saved_t *b) {
    if (a->knob_mode != b->knob_mode) return false;
    for (int i = 0; i < SYNTH_UI_JUMP_SLOTS; i++) {
        const jump_slot_t *x = &a->jump[i], *y = &b->jump[i];
        if (x->valid != y->valid || (x->valid && (x->in_rack != y->in_rack || x->mod_id != y->mod_id || x->mod_type != y->mod_type || x->def != y->def ||
                                                  x->tab != y->tab || x->row != y->row))) return false;
    }
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++)
        if (a->shift[k].kind != b->shift[k].kind || a->shift[k].id != b->shift[k].id || a->shift[k].prm != b->shift[k].prm) return false;
    return true;
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
    for (int k = 0; k < SYNTH_UI_COL_KNOBS && n < cap; k++) {
        const macro_t *m = &ui->knob_shift[k];
        char tok[16];
        if (m->kind == MACRO_GLOBAL && m->prm < CFGP_COUNT) {
            cfg_token((cfg_param_id_t)m->prm, tok, sizeof tok);
            n += snprintf(buf + n, (size_t)(cap - n), "shift %d cfg %s\n", k + 1, tok);
        } else {
            n += snprintf(buf + n, (size_t)(cap - n), "shift %d none\n", k + 1);
        }
    }
    return n < cap ? n : cap - 1;
}

bool ui_settings_from_text(synth_ui_t *ui, rack_t *rack, const char *txt) {
    bool any = false;
    int knob_mode = rack->cfg.knob_mode;
    jump_slot_t jump[SYNTH_UI_JUMP_SLOTS] = {{0}};
    macro_t shift[SYNTH_UI_COL_KNOBS];
    memcpy(shift, ui->knob_shift, sizeof shift);
    for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        char w1[16], w2[16], w3[16];
        int a = 0, b = 0;
        const int got = sscanf(line, "%15s %15s %15s", w1, w2, w3);
        if (got >= 2 && !strcmp(w1, "knob")) {
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
            macro_t *m = &shift[a - 1];
            if (!strcmp(w3, "none")) { *m = (macro_t){MACRO_NONE, 0, 0}; any = true; }
            else if (!strcmp(w3, "cfg")) {
                char want[16], tok[16];
                if (sscanf(line, "%*s %*s %*s %15s", want) != 1) continue;
                for (int id = 0; id < CFGP_COUNT; id++) {
                    cfg_token((cfg_param_id_t)id, tok, sizeof tok);
                    if (!strcmp(tok, want)) { *m = (macro_t){MACRO_GLOBAL, 0, (uint8_t)id}; any = true; }
                }
            }
        }
    }
    if (!any) return false;                                      // not a settings file
    rack->cfg.knob_mode = (uint8_t)knob_mode;
    memcpy(ui->jump, jump, sizeof jump);
    memcpy(ui->knob_shift, shift, sizeof shift);
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
