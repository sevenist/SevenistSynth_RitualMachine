#include "core/ui_screen.h"
#include <stdio.h>

#define MAX_ELEMENTS 16

// The context for the callbacks of element i: the same, with that element's `arg`.
static ui_ctx_t ctx_for(const screen_def_t *s, const ui_ctx_t *c, int i) { ui_ctx_t x = *c; x.arg = s->el[i].arg; return x; }

static bool el_enabled(const screen_def_t *s, const ui_ctx_t *c, int i) {
    if (!s->el[i].enabled) return true;
    const ui_ctx_t x = ctx_for(s, c, i);
    return s->el[i].enabled(&x);
}

/* ---------------- focus navigation ---------------- */

// Spatial move from element `cur` (-1 = the header). dir: 'l', 'r', 'u', 'd'. Returns the new element, -1 for the header, or `cur` when there is nowhere to go.
static int nav(const screen_def_t *s, const ui_ctx_t *c, int cur, char dir) {
    if (cur < 0) {                                              // from the header: down to the first enabled element
        if (dir != 'd') return -1;
        for (int i = 0; i < s->n; i++) if (el_enabled(s, c, i)) return i;
        return -1;
    }
    const el_def_t *e = &s->el[cur];
    int best = cur, best_score = 1 << 20;
    if (dir == 'l' || dir == 'r') {
        for (int i = 0; i < s->n; i++) {
            const el_def_t *o = &s->el[i];
            if (i == cur || o->row != e->row || !el_enabled(s, c, i)) continue;
            const int gap = dir == 'r' ? o->col - (e->col + e->span) : e->col - (o->col + o->span);
            if (gap >= 0 && gap < best_score) { best = i; best_score = gap; }
        }
        return best;
    }
    // up / down: the nearest row in that direction that has an enabled element, then the element closest in column
    int target_row = -1;
    for (int i = 0; i < s->n; i++) {
        const int r = s->el[i].row;
        if (!el_enabled(s, c, i)) continue;
        if (dir == 'd' && r > e->row && (target_row < 0 || r < target_row)) target_row = r;
        if (dir == 'u' && r < e->row && (target_row < 0 || r > target_row)) target_row = r;
    }
    if (target_row < 0) return dir == 'u' ? -1 : cur;           // above the first row: the header
    for (int i = 0; i < s->n; i++) {
        const el_def_t *o = &s->el[i];
        if (o->row != target_row || !el_enabled(s, c, i)) continue;
        int dist = 0;                                           // 0 when the columns overlap
        if (o->col + o->span <= e->col) dist = e->col - (o->col + o->span) + 1;
        else if (o->col >= e->col + e->span) dist = o->col - (e->col + e->span) + 1;
        if (dist < best_score) { best = i; best_score = dist; }
    }
    return best;
}

// Linear move for encoder A: through the header and the enabled elements in table order.
static int linear(const screen_def_t *s, const ui_ctx_t *c, int row, int step) {
    for (int k = 0; k <= s->n; k++) {
        row = (row + step + s->n + 1) % (s->n + 1);
        if (row == 0 || el_enabled(s, c, row - 1)) return row;
    }
    return row;
}

/* ---------------- events ---------------- */

// After an edit the focused element may have become unavailable (an effect with fewer parameters): move the focus back to the nearest one before it.
static void fix_focus(const screen_def_t *s, const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    while (ui->row >= 1 && !el_enabled(s, c, ui->row - 1)) { ui->row--; ui->latched = false; }
}

static bool adjust_el(const screen_def_t *s, const ui_ctx_t *c, int i, int dir) {
    const el_def_t *e = &s->el[i];
    if (!el_enabled(s, c, i) || !e->adjust || e->kind == EL_BUTTON) return false;
    const ui_ctx_t x = ctx_for(s, c, i);
    const bool changed = e->adjust(&x, dir);
    if (s->after_edit) s->after_edit(c);
    fix_focus(s, c);
    return changed;
}

static void activate_el(const screen_def_t *s, const ui_ctx_t *c, int i) {
    const el_def_t *e = &s->el[i];
    if (!el_enabled(s, c, i) || !e->activate) return;
    const ui_ctx_t x = ctx_for(s, c, i);
    e->activate(&x);
    if (s->after_edit) s->after_edit(c);
    fix_focus(s, c);
}

// Left / right on the header: the next / previous tab of the menu.
static void header_step(const ui_ctx_t *c, int dir) {
    const int tabs = tab_count(c->rack);
    c->ui->menu_tab = (c->ui->menu_tab + dir + tabs) % tabs;
}

bool screen_event(const screen_def_t *s, const ui_ctx_t *c, ui_event_t ev) {
    synth_ui_t *ui = c->ui;
    int cur = ui->row - 1;                                      // -1 = header
    if (cur >= s->n) cur = s->n - 1;

    switch (ev) {
        case UI_NAV_LEFT: case UI_NAV_RIGHT: case UI_NAV_UP: case UI_NAV_DOWN: {
            const char dir = ev == UI_NAV_LEFT ? 'l' : ev == UI_NAV_RIGHT ? 'r' : ev == UI_NAV_UP ? 'u' : 'd';
            if (cur < 0) {                                      // header: left / right change the tab
                if (dir == 'l' || dir == 'r') { header_step(c, dir == 'r' ? 1 : -1); ui->latched = false; return false; }
            } else {
                const el_def_t *e = &s->el[cur];
                if (ui->latched && e->kind == EL_VALUE)         // latched: the joystick edits the value
                    return adjust_el(s, c, cur, (dir == 'r' || dir == 'u') ? 1 : -1);
                if (e->kind == EL_DIRECT && (dir == 'l' || dir == 'r'))
                    return adjust_el(s, c, cur, dir == 'r' ? 1 : -1);
            }
            const int next = nav(s, c, cur, dir);
            if (next != cur) { ui->row = next + 1; ui->latched = false; }
            return false;
        }
        case UI_VALUE_INC: case UI_VALUE_DEC: case UI_RIGHT: case UI_LEFT: {
            const int dir = (ev == UI_VALUE_INC || ev == UI_RIGHT) ? 1 : -1;
            if (cur < 0) { header_step(c, dir); ui->latched = false; return false; }
            return adjust_el(s, c, cur, dir);
        }
        case UI_UP: case UI_DOWN:
            ui->row = linear(s, c, ui->row, ev == UI_DOWN ? 1 : -1);
            ui->latched = false;
            return false;
        case UI_LATCH:
            if (cur < 0) return false;
            if (s->el[cur].kind == EL_VALUE && el_enabled(s, c, cur)) ui->latched = !ui->latched;
            else if (s->el[cur].kind == EL_BUTTON) activate_el(s, c, cur);
            return false;
        case UI_SELECT:
            if (cur >= 0 && s->el[cur].kind == EL_BUTTON) activate_el(s, c, cur);
            return false;
        case UI_BACK:
            if (s->back && s->back(c) && s->after_edit) s->after_edit(c);
            fix_focus(s, c);
            return false;
        default:
            return false;
    }
}

/* ---------------- drawing ---------------- */

void screen_draw(const screen_def_t *s, u8g2_t *g, const gui_style_t *st0, const ui_ctx_t *c, gui_rect_t area) {
    gui_style_t compact = *st0;
    if (s->compact && u8g2_GetDisplayHeight(g) < 100) { compact.padding = 0; compact.gap = 0; }       // compact rows only on a short screen
    const gui_style_t *st = &compact;
    gui_rect_t rect[MAX_ELEMENTS];
    s->layout(g, st, c, area, rect);
    if (s->draw_extra) s->draw_extra(g, st, c, area, rect);
    for (int i = 0; i < s->n; i++) {
        const el_def_t *e = &s->el[i];
        const bool enabled = el_enabled(s, c, i);
        if (!enabled && (e->flags & EF_HIDE_WHEN_DISABLED)) continue;
        const ui_ctx_t x = ctx_for(s, c, i);
        const bool focused = c->ui->row == i + 1, latched = focused && c->ui->latched;
        const gui_state_t state = latched ? GUI_LATCHED : focused ? GUI_FOCUSED : GUI_PLAIN;
        if (e->draw) { e->draw(g, st, &x, rect[i], focused, latched); continue; }
        const char *label = e->label_dyn ? e->label_dyn(&x) : e->label;
        char val[24] = "";
        if (!enabled) snprintf(val, sizeof val, "n/a");
        else if (e->value) e->value(&x, val, sizeof val);
        if (e->kind == EL_BUTTON) { gui_draw_button(g, st, rect[i], (enabled && val[0]) ? val : label, state); continue; }
        gui_draw_field_state(g, st, rect[i], label, val, enabled ? state : GUI_PLAIN);
    }
}

/* ---------------- shared layout and the screen of each tab ---------------- */

gui_rect_t ui_picture_box(const gui_style_t *st) { return gui_rect(st->graph.x, st->graph.y, st->graph.w, st->graph.h); }

void ui_layout_column(u8g2_t *g, const gui_style_t *st, gui_rect_t area, int n, gui_rect_t *rect) {
    const gui_rect_t list = gui_take_left(&area, st->list_w);
    const int rh = gui_row_h(g, st);
    gui_rect_t row = gui_rect(list.x, list.y + st->list_top, list.w - 1, rh);
    for (int i = 0; i < n; i++) { rect[i] = row; row = gui_below(row, st->gap, rh); }
}

const screen_def_t *screen_for_tab(tab_t t) {
    switch (t) {
        case TAB_GENERAL: return &scr_general_screen;
        case TAB_SAMPLES: return &scr_samples_screen;
        case TAB_FX:      return &scr_fx_screen;
        case TAB_FM_ALGO: return &scr_fm_algo_screen;
        case TAB_FM_OP:   return &scr_fm_op_screen;
        case TAB_FM_ENV:  return &scr_fm_env_screen;
        default:          return &scr_rack_screen;
    }
}
