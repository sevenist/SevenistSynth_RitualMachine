// The RACK tab of the menu: the first declarative screen (see ui_screen.h). One table of elements describes it; the
// strip of module sprites, the selection frame and arrow, the connection lanes and the editing fields are drawn from it, and
// the joystick / encoder events are handled by the generic screen code.
//
//   [header: < RACK >]                              focus: joystick up from the strip
//   [ strip: the modules as large sprites ]         joystick left / right scroll through the slots (no latch needed)
//   ( connection lanes: audio chain, modulator links )
//   one-line description of the selected module
//   [Type]   [Insert]                               Type: what Insert adds;  Insert: a button
//   [Tgt]    [Prm]                                  a modulator's target module and parameter
//   [Dpth            ]                              the modulation depth in the target's unit
//   [Delete          ]                              a button
#include "core/ui_screen.h"
#include "core/sprites.h"
#include "core/module_sprites.h"
#include <stdio.h>

_Static_assert(RACK_PITCH == MODULE_SPRITE_W + 4, "RACK_PITCH (ui_internal.h) must be the module sprite width + 4: regenerate the sprites or change it");

enum { E_STRIP, E_TYPE, E_INSERT, E_TGT, E_PRM, E_DPTH, E_DELETE, E_COUNT };

_Static_assert(E_COUNT == RACK_SCREEN_ELEMENTS, "update RACK_SCREEN_ELEMENTS in ui_internal.h");

#define STRIP_TOP 7                     // room above the sprites for the selection arrow

static const gui_sprite_t *module_sprite(int type) {
    static const gui_sprite_t *const t[MOD_TYPE_COUNT] = {&spr_mod_osc, &spr_mod_filter, &spr_mod_sat,
                                                          &spr_mod_lfo, &spr_mod_mseq, &spr_mod_env, &spr_mod_sampler, &spr_mod_eg, &spr_mod_comb};
    return t[type];
}

static void dotted_h(u8g2_t *g, int x0, int x1, int y) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x += 2) u8g2_DrawPixel(g, x, y);
}
static void dotted_v(u8g2_t *g, int x, int y0, int y1) { for (int y = y0; y <= y1; y += 2) u8g2_DrawPixel(g, x, y); }

/* ---------------- state helpers ---------------- */

static const rack_slot_t *sel_slot(const ui_ctx_t *c) { return c->ui->rack_cur < c->rack->count ? &c->rack->slot[c->ui->rack_cur] : NULL; }
static int sel_target(const ui_ctx_t *c) {
    const rack_slot_t *s = sel_slot(c);
    return (s && rack_can_target((module_type_t)s->type) && s->tgt_id) ? rack_find(c->rack, s->tgt_id) : RACK_NONE;
}
static int positions(const ui_ctx_t *c) { return c->rack->count + (c->rack->count < RACK_MAX ? 1 : 0); }

// Keep the selection and the scroll position valid: the selected cell is always inside the visible window.
static void keep_visible(const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    if (ui->rack_cur >= positions(c)) ui->rack_cur = positions(c) - 1;
    if (ui->rack_cur < ui->rack_scroll) ui->rack_scroll = ui->rack_cur;
    if (ui->rack_cur >= ui->rack_scroll + RACK_VIS) ui->rack_scroll = ui->rack_cur - RACK_VIS + 1;
    if (ui->rack_scroll > RACK_MAX + 1 - RACK_VIS) ui->rack_scroll = RACK_MAX + 1 - RACK_VIS;
    if (ui->rack_scroll < 0) ui->rack_scroll = 0;
}

/* ---------------- elements: value text, edits, buttons ---------------- */

static bool strip_adjust(const ui_ctx_t *c, int dir) {
    c->ui->rack_cur = (c->ui->rack_cur + dir + positions(c)) % positions(c);
    return false;
}

static void type_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", rack_type_code((module_type_t)c->ui->rack_type)); }
static bool type_adjust(const ui_ctx_t *c, int dir) { c->ui->rack_type = (c->ui->rack_type + dir + MOD_TYPE_COUNT) % MOD_TYPE_COUNT; return false; }

static bool insert_enabled(const ui_ctx_t *c) { return c->rack->count < RACK_MAX; }
static void insert_activate(const ui_ctx_t *c) {
    if (rack_insert(c->rack, c->ui->rack_cur, (module_type_t)c->ui->rack_type)) c->ui->rack_dirty = true;
}

static bool tgt_enabled(const ui_ctx_t *c) { const rack_slot_t *s = sel_slot(c); return s && rack_can_target((module_type_t)s->type); }
static void tgt_value(const ui_ctx_t *c, char *out, int n) {
    const int ti = sel_target(c);
    if (ti == RACK_NONE) snprintf(out, (size_t)n, "--"); else rack_slot_name(c->rack, ti, out, n);
}
static bool tgt_adjust(const ui_ctx_t *c, int dir) { rack_cycle_target(c->rack, c->ui->rack_cur, dir); c->ui->rack_dirty = true; return false; }

static bool prm_enabled(const ui_ctx_t *c) { return sel_target(c) != RACK_NONE; }
static void prm_value(const ui_ctx_t *c, char *out, int n) {
    const int ti = sel_target(c);
    snprintf(out, (size_t)n, "%s", ti == RACK_NONE ? "--" : rack_param_name((module_type_t)c->rack->slot[ti].type, sel_slot(c)->tgt_param));
}
static bool prm_adjust(const ui_ctx_t *c, int dir) { rack_cycle_param(c->rack, c->ui->rack_cur, dir); c->ui->rack_dirty = true; return false; }

static bool dpth_enabled(const ui_ctx_t *c) { return prm_enabled(c) && rack_depth_index((module_type_t)sel_slot(c)->type) >= 0; }
static void dpth_value(const ui_ctx_t *c, char *out, int n) { rack_depth_format(c->rack, c->ui->rack_cur, out, n); }
static bool dpth_adjust(const ui_ctx_t *c, int dir) { if (rack_depth_adjust(c->rack, c->ui->rack_cur, dir)) c->ui->rack_dirty = true; return false; }

static bool delete_enabled(const ui_ctx_t *c) { return sel_slot(c) != NULL; }
static void delete_activate(const ui_ctx_t *c) { if (rack_delete(c->rack, c->ui->rack_cur)) c->ui->rack_dirty = true; }
static bool back_delete(const ui_ctx_t *c) { delete_activate(c); return true; }

/* ---------------- the strip ---------------- */

// The strip's geometry: the first visible cell starts at x0; a cell is RACK_PITCH wide and the sprite sits 2 px inside it.
static int strip_x0(gui_rect_t r) { return r.x + (r.w - RACK_VIS * RACK_PITCH) / 2; }

static void strip_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)st; (void)latched;
    const synth_ui_t *ui = c->ui;
    const rack_t *rk = c->rack;
    const int x0 = strip_x0(r), y = r.y + STRIP_TOP, sc = ui->rack_scroll;
#define CELL_VIS(i) ((i) >= sc && (i) < sc + RACK_VIS)
#define SPRITE_X(i) (x0 + ((i) - sc) * RACK_PITCH + 2)
    for (int i = 0; i < rk->count; i++)
        if (CELL_VIS(i)) gui_draw_sprite(g, module_sprite(rk->slot[i].type), SPRITE_X(i), y);
    if (rk->count < RACK_MAX && CELL_VIS(rk->count)) gui_draw_sprite(g, &spr_slot_empty, SPRITE_X(rk->count), y);
    if (CELL_VIS(RACK_MAX)) gui_draw_sprite(g, &spr_slot_out, SPRITE_X(RACK_MAX), y);

    // the selected cell: a frame around it and an arrow pointing at it; the frame is doubled while the strip has the focus
    const int sel = ui->rack_cur;
    if (CELL_VIS(sel)) {
        const int sx = SPRITE_X(sel);
        u8g2_DrawFrame(g, (u8g2_uint_t)(sx - 2), (u8g2_uint_t)(y - 2), MODULE_SPRITE_W + 4, MODULE_SPRITE_H + 4);
        if (focused) u8g2_DrawFrame(g, (u8g2_uint_t)(sx - 3), (u8g2_uint_t)(y - 3), MODULE_SPRITE_W + 6, MODULE_SPRITE_H + 6);
        gui_draw_arrow(g, sx + MODULE_SPRITE_W / 2, y - 4, 'd', 3);
    }
    // scroll arrows at both ends when more cells are hidden
    const int my = y + MODULE_SPRITE_H / 2;
    if (sc > 0) gui_draw_arrow(g, r.x, my, 'l', 3);
    if (sc + RACK_VIS < RACK_MAX + 1) gui_draw_arrow(g, r.x + r.w - 1, my, 'r', 3);
#undef SPRITE_X
#undef CELL_VIS
}

/* ---------------- layout (anchors) and what is drawn behind the elements ---------------- */

static int lane_audio_y(const gui_style_t *st, gui_rect_t strip) { (void)st; return strip.y + STRIP_TOP + MODULE_SPRITE_H + 3; }
static int lane_mod_y(const gui_style_t *st, gui_rect_t strip) { return lane_audio_y(st, strip) + st->rack_lane_gap; }

// The description line only appears when it fits: under it come the 4 rows of fields.
static bool info_fits(u8g2_t *g, const gui_style_t *st, gui_rect_t area) {
    const int rh = gui_row_h(g, st);
    return lane_mod_y(st, area) + 3 + rh + st->gap + 4 * rh + 3 * st->gap <= gui_bottom(area);
}

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect);
static const el_def_t elements[E_COUNT];

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    const int rh = gui_row_h(g, st);
    rect[E_STRIP] = gui_rect(area.x, area.y, area.w, STRIP_TOP + MODULE_SPRITE_H + 1);
    int y = lane_mod_y(st, area) + 3;
    if (info_fits(g, st, area)) y += rh + st->gap;
    const gui_rect_t fields = gui_rect(area.x + 1, y, area.w - 2, 4 * rh + 3 * st->gap);
    for (int i = 1; i < E_COUNT; i++) {                          // a 2 x 4 grid; an element spanning both columns takes the whole row
        const gui_rect_t first = gui_grid_cell(fields, 2, 4, (elements[i].row - 1) * 2 + elements[i].col, st->gap);
        rect[i] = elements[i].span > 1 ? gui_rect(first.x, first.y, fields.w, first.h) : first;
    }
}

static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    const rack_t *r = c->rack;
    const synth_ui_t *ui = c->ui;
    const gui_rect_t strip = rect[E_STRIP];
    const int S = MODULE_SPRITE_W, sc = ui->rack_scroll, x0 = strip_x0(strip);
    const int bottom = strip.y + STRIP_TOP + MODULE_SPRITE_H;
    const int lane_a = lane_audio_y(st, strip) - 1, lane_m = lane_mod_y(st, strip) - 1;
    const int xmin = x0 + 2 + S / 2, xmax = x0 + (RACK_VIS - 1) * RACK_PITCH + 2 + S / 2;
#define CELL_VIS(i) ((i) >= sc && (i) < sc + RACK_VIS)
#define CELL_CX(i) (x0 + ((i) - sc) * RACK_PITCH + 2 + S / 2)
#define LINK_X(i) (CELL_CX(i) < xmin ? xmin : CELL_CX(i) > xmax ? xmax : CELL_CX(i))      // links to cells off screen end at the edge

    // audio chain: solid while a source feeds it, dotted otherwise
    int prev = RACK_NONE;
    for (int i = 0; i < r->count; i++) {
        if (!rack_slot_is_audio(r, i)) continue;
        if (CELL_VIS(i)) u8g2_DrawVLine(g, CELL_CX(i), bottom, lane_a - bottom + 1);
        if (prev != RACK_NONE && (CELL_VIS(i) || CELL_VIS(prev) || (prev < sc && i >= sc + RACK_VIS))) {
            const int px = LINK_X(prev), cx = LINK_X(i);
            if (rack_has_signal(r, prev)) u8g2_DrawHLine(g, px, lane_a, cx - px + 1);
            else dotted_h(g, px, cx, lane_a);
        }
        prev = i;
    }
    if (prev != RACK_NONE) {
        const int px = LINK_X(prev), ox = LINK_X(RACK_MAX);
        if (rack_has_signal(r, prev)) u8g2_DrawHLine(g, px, lane_a, ox - px + 1);
        else dotted_h(g, px, ox, lane_a);
        if (CELL_VIS(RACK_MAX)) u8g2_DrawVLine(g, ox, bottom, lane_a - bottom + 1);
    }

    // modulator links: a stub at each visible end, the lane runs to the edge when the other end is off screen
    for (int i = 0; i < r->count; i++) {
        const rack_slot_t *s = &r->slot[i];
        if (!rack_slot_is_mod(r, i) || !s->tgt_id) continue;
        const int ti = rack_find(r, s->tgt_id);
        if (ti == RACK_NONE) continue;
        const int mx = LINK_X(i) + 1, tx = LINK_X(ti) + 1;
        if (CELL_VIS(i))  dotted_v(g, mx, bottom, lane_m);
        if (CELL_VIS(ti)) dotted_v(g, tx, bottom, lane_m);
        dotted_h(g, mx, tx, lane_m);
    }
    for (int i = 0; i < r->count; i++) {                         // motion sequencer: one link per lane that has a target
        if (r->slot[i].type != MOD_MSEQ) continue;
        const ms_pattern_t *pat = rack_ms_const(r, i);
        for (int l = 0; l < MS_LANES; l++) {
            const int ti = pat->lane[l].tgt_id ? rack_find(r, pat->lane[l].tgt_id) : RACK_NONE;
            if (ti == RACK_NONE) continue;
            const int mx = LINK_X(i) + 1, tx = LINK_X(ti) + 1;
            if (CELL_VIS(i))  dotted_v(g, mx, bottom, lane_m);
            if (CELL_VIS(ti)) dotted_v(g, tx, bottom, lane_m);
            dotted_h(g, mx, tx, lane_m);
        }
    }
#undef LINK_X
#undef CELL_CX
#undef CELL_VIS

    // one line describing the selection
    if (info_fits(g, st, area)) {
        char buf[32];
        if (ui->rack_cur < r->count) rack_describe(r, ui->rack_cur, buf, sizeof buf);
        else snprintf(buf, sizeof buf, "+ %s", rack_type_name((module_type_t)ui->rack_type));
        gui_draw_text_centered(g, gui_rect(area.x, lane_m + 3, area.w, gui_row_h(g, st)), buf);
    }
}

/* ---------------- the table ---------------- */

static const el_def_t elements[E_COUNT] = {
    [E_STRIP]  = {"Slot",   EL_DIRECT, 0, 0, 2, NULL,       strip_adjust,  NULL,            strip_draw, NULL},
    [E_TYPE]   = {"Type",   EL_VALUE,  1, 0, 1, type_value, type_adjust,   NULL,            NULL, NULL},
    [E_INSERT] = {"Insert", EL_BUTTON, 1, 1, 1, NULL,       NULL,          insert_activate, NULL, insert_enabled},
    [E_TGT]    = {"Tgt",    EL_VALUE,  2, 0, 1, tgt_value,  tgt_adjust,    NULL,            NULL, tgt_enabled},
    [E_PRM]    = {"Prm",    EL_VALUE,  2, 1, 1, prm_value,  prm_adjust,    NULL,            NULL, prm_enabled},
    [E_DPTH]   = {"Dpth",   EL_VALUE,  3, 0, 2, dpth_value, dpth_adjust,   NULL,            NULL, dpth_enabled},
    [E_DELETE] = {"Delete", EL_BUTTON, 4, 0, 2, NULL,       NULL,          delete_activate, NULL, delete_enabled},
};

// After every edit: keep the selection in view, and move the focus to the strip when the focused element just became unavailable.
static void after_edit(const ui_ctx_t *c) {
    keep_visible(c);
    const int row = c->ui->row;
    if (row >= 1 && elements[row - 1].enabled && !elements[row - 1].enabled(c)) { c->ui->row = 1; c->ui->latched = false; }
}

static const screen_def_t rack_screen = {elements, E_COUNT, layout, back_delete, after_edit, draw_extra};

static void header_step(const ui_ctx_t *c, int dir) {
    const int tabs = tab_count(c->rack);
    c->ui->menu_tab = (c->ui->menu_tab + dir + tabs) % tabs;
}

bool scr_rack_event(synth_ui_t *ui, rack_t *rack, ui_event_t e) {
    const ui_ctx_t c = {ui, rack};
    const bool changed = screen_event(&rack_screen, &c, e, header_step);
    keep_visible(&c);
    return changed;
}

void scr_rack_draw(u8g2_t *g, const gui_style_t *st, gui_rect_t area, const synth_ui_t *ui, rack_t *rack) {
    const ui_ctx_t c = {(synth_ui_t *)ui, rack};         // drawing only reads
    screen_draw(&rack_screen, g, st, &c, area);
}
