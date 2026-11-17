#include "core/ui_screen.h"
#include <stdio.h>
#include <string.h>

#define MAX_ELEMENTS 40                 // the pad editor has 19, a node page (scr_node.c) 34

// The context for the callbacks of element i: the same, with that element's `arg`.
static ui_ctx_t ctx_for(const screen_def_t *s, const ui_ctx_t *c, int i) { ui_ctx_t x = *c; x.arg = s->el[i].arg; return x; }

static bool el_enabled(const screen_def_t *s, const ui_ctx_t *c, int i) {
    if (!s->el[i].enabled) return true;
    const ui_ctx_t x = ctx_for(s, c, i);
    return s->el[i].enabled(&x);
}

// What the focus can land on: an enabled element that is not a static widget (EF_STATIC: a label, a picture).
static bool el_focusable(const screen_def_t *s, const ui_ctx_t *c, int i) {
    return !(s->el[i].flags & EF_STATIC) && el_enabled(s, c, i);
}

/* ---------------- focus navigation ---------------- */

// Spatial move from element `cur` (-1 = the header). dir: 'l', 'r', 'u', 'd'. Returns the new element, -1 for the header, or `cur` when there is nowhere to go.
static int nav(const screen_def_t *s, const ui_ctx_t *c, int cur, char dir) {
    if (cur < 0) {                                              // from the header: down to the first enabled element
        if (dir != 'd') return -1;
        for (int i = 0; i < s->n; i++) if (el_focusable(s, c, i)) return i;
        return -1;
    }
    const el_def_t *e = &s->el[cur];
    int best = cur, best_score = 1 << 20;
    if (dir == 'l' || dir == 'r') {
        for (int i = 0; i < s->n; i++) {
            const el_def_t *o = &s->el[i];
            if (i == cur || o->row != e->row || !el_focusable(s, c, i)) continue;
            const int gap = dir == 'r' ? o->col - (e->col + e->span) : e->col - (o->col + o->span);
            if (gap >= 0 && gap < best_score) { best = i; best_score = gap; }
        }
        return best;
    }
    // up / down: the nearest row in that direction that has an enabled element, then the element closest in column
    int target_row = -1;
    for (int i = 0; i < s->n; i++) {
        const int r = s->el[i].row;
        if (!el_focusable(s, c, i)) continue;
        if (dir == 'd' && r > e->row && (target_row < 0 || r < target_row)) target_row = r;
        if (dir == 'u' && r < e->row && (target_row < 0 || r > target_row)) target_row = r;
    }
    if (target_row < 0) return dir == 'u' ? -1 : cur;           // above the first row: the header
    for (int i = 0; i < s->n; i++) {
        const el_def_t *o = &s->el[i];
        if (o->row != target_row || !el_focusable(s, c, i)) continue;
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
        if (row == 0 || el_focusable(s, c, row - 1)) return row;
    }
    return row;
}

// The joystick's move: the screen's own (`move`), or the grid.
static int go(const screen_def_t *s, const ui_ctx_t *c, int cur, char dir) { return s->move ? s->move(c, cur, dir) : nav(s, c, cur, dir); }

/* ---------------- events ---------------- */

// After an edit the focused element may have become unavailable (an effect with fewer parameters): move the focus back to the nearest one before it.
static void fix_focus(const screen_def_t *s, const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    if (ui->row > s->n) { ui->row = s->n; ui->latched = false; }        // a focus past this screen's last element (a shorter page)
    while (ui->row >= 1 && !el_focusable(s, c, ui->row - 1)) { ui->row--; ui->latched = false; }
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

// Left / right on the header: the next / previous tab of the view (ADR-046 phase 3).
static void header_step(const ui_ctx_t *c, int dir) { synth_ui_view_step(c->ui, c->rack, dir); }

bool screen_event(const screen_def_t *s, const ui_ctx_t *c, ui_event_t ev) {
    synth_ui_t *ui = c->ui;
    int cur = ui->row - 1;                                      // -1 = header
    if (cur >= s->n) cur = s->n - 1;

    switch (ev) {
        case UI_NAV_LEFT: case UI_NAV_RIGHT: case UI_NAV_UP: case UI_NAV_DOWN: {
            const char dir = ev == UI_NAV_LEFT ? 'l' : ev == UI_NAV_RIGHT ? 'r' : ev == UI_NAV_UP ? 'u' : 'd';
            if (cur < 0) {                                      // header: left / right change the tab
                if (dir == 'l' || dir == 'r') { header_step(c, dir == 'r' ? 1 : -1); return false; }
            } else {
                const el_def_t *e = &s->el[cur];
                if (dir == 'l' || dir == 'r') {
                    if (e->kind == EL_DIRECT)
                        return adjust_el(s, c, cur, dir == 'r' ? 1 : -1);
                    if (e->kind == EL_VALUE) {
                        if (s->use_latch && ui->latched)             // latch screens: L/R edits while latched
                            return adjust_el(s, c, cur, dir == 'r' ? 1 : -1);
                        const int next = go(s, c, cur, dir);
                        if (next == cur) return adjust_el(s, c, cur, dir == 'r' ? 1 : -1);  // no neighbour: adjust directly
                        ui->row = next + 1; ui->latched = false; return false;
                    }
                }
            }
            const int next = go(s, c, cur, dir);
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
            if (s->use_latch && s->el[cur].kind == EL_VALUE && el_enabled(s, c, cur)) ui->latched = !ui->latched;
            else if (s->el[cur].kind != EL_VALUE) activate_el(s, c, cur);          // a button, or a selector that opens what it selects
            return false;
        case UI_SELECT:
            if (cur >= 0 && s->el[cur].kind != EL_VALUE) activate_el(s, c, cur);
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
    if (s->layout) s->layout(g, st, c, area, rect);
    else memset(rect, 0, sizeof rect);
    ui_layout_boxes(g, st, area, s->el, s->n, rect);                // the elements placed by data (their `box`)
    // a list (EF_SCROLL rows): moved up together until the focused row is in view; rows moved above the list's top are not drawn
    int list_top = 1 << 20;
    for (int i = 0; i < s->n; i++) if ((s->el[i].flags & EF_SCROLL) && el_enabled(s, c, i) && rect[i].y < list_top) list_top = rect[i].y;
    const int f = c->ui->row - 1;
    if (f >= 0 && f < s->n && (s->el[f].flags & EF_SCROLL) && rect[f].y + rect[f].h > area.y + area.h) {
        const int dy = rect[f].y + rect[f].h - (area.y + area.h);
        for (int i = 0; i < s->n; i++) if (s->el[i].flags & EF_SCROLL) rect[i].y -= dy;
    }
    if (s->draw_extra) s->draw_extra(g, st, c, area, rect);
    u8g2_SetClipWindow(g, area.x, area.y, area.x + area.w, area.y + area.h);    // rows scrolled off the list do not draw over the header
    for (int i = 0; i < s->n; i++) {
        const el_def_t *e = &s->el[i];
        const bool enabled = el_enabled(s, c, i);
        if (!enabled && (e->flags & EF_HIDE_WHEN_DISABLED)) continue;
        if (!s->layout && !e->box.place) continue;                  // nothing placed it
        if ((e->flags & EF_SCROLL) && rect[i].y < list_top) continue;          // scrolled out above the list
        const ui_ctx_t x = ctx_for(s, c, i);
        const bool focused = c->ui->row == i + 1 && !(e->flags & EF_STATIC), latched = focused && c->ui->latched;
        const gui_state_t state = latched ? GUI_LATCHED : focused ? GUI_FOCUSED : GUI_PLAIN;
        if (e->draw) { e->draw(g, st, &x, rect[i], focused, latched); continue; }
        const char *label = e->label_dyn ? e->label_dyn(&x) : e->label;
        char val[24] = "";
        if (!enabled) snprintf(val, sizeof val, "n/a");
        else if (e->value) e->value(&x, val, sizeof val);
        if (e->kind == EL_BUTTON) { gui_draw_button(g, st, rect[i], (enabled && val[0]) ? val : label, state); continue; }
        gui_draw_field_state(g, st, rect[i], label, val, enabled ? state : GUI_PLAIN);
    }
    u8g2_SetMaxClipWindow(g);
}

/* ---------------- widget geometry: the `box` of an element ---------------- */

// One length in pixels. parent: the parent's extent on that axis; rest: from where the element starts to the parent's far edge; same: the ref element's.
static int len_px(ui_len_t l, int parent, int rest, int same, int row_h, int gap) {
    int v;
    switch (l.unit) {
        case UN_PCT:  v = parent * l.v / 100; break;
        case UN_CELL: v = l.v * GUI_GRID; break;
        case UN_FILL: v = rest - l.v; break;
        case UN_ROW:  v = l.v * row_h; break;
        case UN_GAP:  v = l.v * gap; break;
        case UN_SAME: v = same; break;
        default:      v = l.v; break;
    }
    return v + l.adj;
}

void ui_layout_boxes(u8g2_t *g, const gui_style_t *st, gui_rect_t area, const el_def_t *el, int n, gui_rect_t *rect) {
    const int row_h = gui_row_h(g, st), gap = st->gap;
    for (int i = 0; i < n; i++) {
        const ui_box_t *b = &el[i].box;
        if (b->place == PL_NONE) continue;
        gui_rect_t ref = area;                                      // what it is placed against
        if (b->ref && b->ref - 1 < i) ref = rect[b->ref - 1];
        else if (b->place != PL_IN && i > 0) ref = rect[i - 1];
        const gui_rect_t parent = b->place == PL_IN ? ref : area;
        const bool before = b->place == PL_ABOVE || b->place == PL_LEFT_OF;     // placed from the far side: its size comes first
        int x = 0, y = 0, w = 0, h = 0;
        if (before) {
            w = len_px(b->w, parent.w, 0, ref.w, row_h, gap);
            h = len_px(b->h, parent.h, 0, ref.h, row_h, gap);
        }
        switch (b->place) {
            case PL_IN:       x = ref.x + len_px(b->x, parent.w, 0, 0, row_h, gap);  y = ref.y + len_px(b->y, parent.h, 0, 0, row_h, gap); break;
            case PL_BELOW:    x = ref.x + len_px(b->x, parent.w, 0, 0, row_h, gap);  y = ref.y + ref.h + len_px(b->y, parent.h, 0, 0, row_h, gap); break;
            case PL_RIGHT_OF: x = ref.x + ref.w + len_px(b->x, parent.w, 0, 0, row_h, gap);  y = ref.y + len_px(b->y, parent.h, 0, 0, row_h, gap); break;
            case PL_ABOVE:    x = ref.x + len_px(b->x, parent.w, 0, 0, row_h, gap);  y = ref.y - len_px(b->y, parent.h, 0, 0, row_h, gap) - h; break;
            default:          x = ref.x - len_px(b->x, parent.w, 0, 0, row_h, gap) - w;  y = ref.y + len_px(b->y, parent.h, 0, 0, row_h, gap); break;   // PL_LEFT_OF
        }
        if (!before) {
            w = len_px(b->w, parent.w, parent.x + parent.w - x, ref.w, row_h, gap);
            h = len_px(b->h, parent.h, parent.y + parent.h - y, ref.h, row_h, gap);
        }
        rect[i] = gui_rect(x, y, w < 0 ? 0 : w, h < 0 ? 0 : h);
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
        case TAB_SOUND:   return &scr_sound_screen;
        case TAB_KEYS:    return &scr_keys_screen;
        case TAB_MODS:    return &scr_mods_screen;
        case TAB_MACROS:  return &scr_macros_screen;
        case TAB_CURVES:  return &scr_curves_screen;
        case TAB_LEDS:    return &scr_leds_screen;
        case TAB_JOY:     return &scr_joy_screen;
        case TAB_PRESET:  return &scr_preset_screen;
        case TAB_MACHINE: return &scr_machine_screen;
        default:          return &scr_map_screen;          // RACK (and MAP, its name for a while: jump slots may hold it): the rack as a map
    }
}

const screen_def_t *screen_shown(const synth_ui_t *ui, const rack_t *rack, tab_t t) {
    if ((t != TAB_RACK && t != TAB_MAP) || !ui->map_at) return screen_for_tab(t);
    const int node = ui->map_at - 1;                                // a node's page while that node is a module of the rack
    if (node >= rack->count) return &scr_map_screen;
    return ui->map_pick ? &scr_pick_screen : &scr_node_screen;
}
