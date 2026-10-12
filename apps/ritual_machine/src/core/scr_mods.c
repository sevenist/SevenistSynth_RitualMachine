// The MODIFIERS tab of the menu: what a control does while Shift or Mod is held (core/modifiers.h). Layer picks Shift or Mod; Ctrl picks a
// control (pressing a key or turning a column / right-hand knob does the same, and with Shift or Mod held it picks that layer too); Func gives
// it a function; for a parameter, Min / Max (% of its range, Min > Max inverts) and Curve shape the knob's travel (core/curves.h);
// Learn (knobs only) goes back to the pages and the next push on a row makes that row's parameter the knob's target; Reset
// puts both layers back to the defaults. The chord Shift + Mod + turning a knob learns too, for the layer shown here.
// On the right: the controls with their function in the layer, the selected one highlighted. Saved in ui.cfg by itself (core/ui_settings.c).
// A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include "core/modifiers.h"
#include "core/curves.h"
#include <stdio.h>

static control_id_t cur(const ui_ctx_t *c) {
    const control_id_t k = (control_id_t)c->ui->mods_ctl;
    return modifiers_ctl_index(k) >= 0 ? k : modifiers_ctl_at(0);
}

static void layer_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", modifiers_layer_name(c->ui->mods_layer)); }
static bool layer_adjust(const ui_ctx_t *c, int dir) {
    (void)dir;
    c->ui->mods_layer = c->ui->mods_layer == MODL_SHIFT ? MODL_MOD : MODL_SHIFT;
    return false;
}

static void ctl_value(const ui_ctx_t *c, char *out, int n) { modifiers_ctl_name(cur(c), out, n); }
static bool ctl_adjust(const ui_ctx_t *c, int dir) {
    const int count = modifiers_ctl_count();
    if (count > 0) c->ui->mods_ctl = modifiers_ctl_at((modifiers_ctl_index(cur(c)) + dir + count) % count);
    return false;
}

static void fn_value(const ui_ctx_t *c, char *out, int n) { modifiers_entry_name(modifiers_get(c->ui->mods_layer, cur(c)), c->rack, out, n); }
static bool fn_adjust(const ui_ctx_t *c, int dir) {
    const control_id_t k = cur(c);
    const int count = modifiers_fn_count(k);
    int i = modifiers_fn_index(k, modifiers_get(c->ui->mods_layer, k));
    i = i < 0 ? (dir > 0 ? 0 : count - 1) : (i + dir + count) % count;      // a learned parameter: stepping leaves it for the list
    modifiers_set(c->ui->mods_layer, k, modifiers_fn_at(k, i));
    return false;
}

// Min / Max / Curve of a Param entry (arg 0 = Min, 1 = Max, 2 = Curve).
static bool map_enabled(const ui_ctx_t *c) { return modifiers_get(c->ui->mods_layer, cur(c)).kind == ME_PARAM; }
static void map_value(const ui_ctx_t *c, char *out, int n) {
    const mod_entry_t e = modifiers_get(c->ui->mods_layer, cur(c));
    if (c->arg == 2) snprintf(out, (size_t)n, "%s", curve_name(e.curve));
    else snprintf(out, (size_t)n, "%d%%", c->arg == 0 ? e.min : 100 - e.max_off);
}
static bool map_adjust(const ui_ctx_t *c, int dir) {
    mod_entry_t e = modifiers_get(c->ui->mods_layer, cur(c));
    if (e.kind != ME_PARAM) return false;
    if (c->arg == 2) e.curve = (uint8_t)curve_step(e.curve, dir);
    else {
        int v = (c->arg == 0 ? e.min : 100 - e.max_off) + 5 * dir;
        v = v < 0 ? 0 : v > 100 ? 100 : v;
        if (c->arg == 0) e.min = (uint8_t)v; else e.max_off = (uint8_t)(100 - v);
    }
    modifiers_set(c->ui->mods_layer, cur(c), e);
    return false;
}

static bool learn_enabled(const ui_ctx_t *c) { return modifiers_ctl_is_knob(cur(c)); }
static void learn_activate(const ui_ctx_t *c) { c->ui->mods_ctl = cur(c); c->ui->learn_macro = -1; c->ui->learn_req = true; }   // the app closes the menu

static void reset_activate(const ui_ctx_t *c) { (void)c; modifiers_reset(); }

static const el_def_t elements[] = {
    {"Layer", EL_VALUE,  0, 0, 1, layer_value, layer_adjust, NULL, NULL, NULL},
    {"Ctrl",  EL_VALUE,  1, 0, 1, ctl_value,   ctl_adjust,   NULL, NULL, NULL},
    {"Func",  EL_VALUE,  2, 0, 1, fn_value,    fn_adjust,    NULL, NULL, NULL},
    {"Min",   EL_VALUE,  3, 0, 1, map_value,   map_adjust,   NULL, NULL, map_enabled, NULL, 0},
    {"Max",   EL_VALUE,  4, 0, 1, map_value,   map_adjust,   NULL, NULL, map_enabled, NULL, 1},
    {"Curve", EL_VALUE,  5, 0, 1, map_value,   map_adjust,   NULL, NULL, map_enabled, NULL, 2},
    {"Learn", EL_BUTTON, 6, 0, 1, NULL,        NULL,         learn_activate, NULL, learn_enabled},
    {"Reset", EL_BUTTON, 7, 0, 1, NULL,        NULL,         reset_activate, NULL, NULL},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

// The control list in the picture box: one line per control the board has, scrolled so the selected one stays in view (a third from the top).
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    const gui_rect_t box = ui_picture_box(st);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int lh = gui_text_h(g) + 1;
    const int lines = (box.h - 2) / lh;
    const int n = modifiers_ctl_count();
    const int sel = modifiers_ctl_index(cur(c));
    int first = sel - lines / 3;
    if (first > n - lines) first = n - lines;
    if (first < 0) first = 0;
    for (int i = 0; i < lines && first + i < n; i++) {
        const control_id_t k = modifiers_ctl_at(first + i);
        char name[8], fn[16], line[24];
        modifiers_ctl_name(k, name, sizeof name);
        const mod_entry_t e = modifiers_get(c->ui->mods_layer, k);
        if (e.kind == ME_DEFAULT) snprintf(fn, sizeof fn, ".");             // Default: dotted, so the set entries stand out
        else modifiers_entry_name(e, c->rack, fn, sizeof fn);
        snprintf(line, sizeof line, "%-4s%s", name, fn);
        const gui_rect_t r = gui_rect(box.x + 1, box.y + 1 + i * lh, box.w - 2, lh);
        if (first + i == sel) { u8g2_DrawBox(g, r.x, r.y, r.w, r.h); u8g2_SetDrawColor(g, 0); }
        u8g2_DrawStr(g, r.x + 2, r.y + lh - 2, line);
        u8g2_SetDrawColor(g, 1);
    }
    // under the rows on the left: how to pick a control and to learn
    const int rh = gui_row_h(g, st);
    const gui_rect_t list = gui_rect(area.x, area.y, st->list_w, area.h);
    if (gui_bottom(rect[N_ELEMENTS - 1]) + 2 * rh <= gui_bottom(list)) {
        gui_draw_text_left(g, st, gui_rect(list.x, gui_bottom(list) - 2 * rh, list.w, rh), "Press / turn it");
        gui_draw_text_left(g, st, gui_rect(list.x, gui_bottom(list) - rh, list.w, rh), "Sh+Mod+K: learn");
    }
}

const screen_def_t scr_mods_screen = {elements, N_ELEMENTS, false, false, layout, NULL, NULL, draw_extra};
