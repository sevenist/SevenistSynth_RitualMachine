// The RACK tab of the menu: the first declarative screen (see ui_screen.h). One table of elements describes it; the
// strip of module sprites, the selection frame, the connection lanes and the editing fields are drawn from it, and
// the joystick / encoder events are handled by the generic screen code.
//
// (declarative screen, see ui_screen.h)
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
#include "core/ui_sprites_gen.h"
#include <stdio.h>

_Static_assert(RACK_PITCH == MODULE_SPRITE_W + GUI_GRID, "RACK_PITCH (ui_internal.h) must be the module sprite width + one grid cell: regenerate the sprites or change it");

enum { E_STRIP, E_TYPE, E_INSERT, E_TGT, E_PRM, E_DPTH, E_DELETE, E_COUNT };

// The screen on the 8 px grid (y from the top of the area under the bar; the 128 x 128 screen):
//   row 0      (8 px)   room for the selection frame
//   rows 1..3  (24 px)  the module icons, centred in 32 px cells; links run between their connectors through the 8 px gaps
//   row 4      (8 px)   the lanes: a mix link that skips cells, the modulation links
//   row 5      (8 px)   one line describing the selection
//   rows 6..13 (64 px)  the fields: 4 rows of 16 px
#define STRIP_TOP   GUI_GRID                        // the icons' top
#define STRIP_H     (5 * GUI_GRID)
#define LANE_AUDIO  (STRIP_TOP + MODULE_SPRITE_H + 4)       // under the selection frame (3 px around the icon)
#define LANE_MOD    (STRIP_TOP + MODULE_SPRITE_H + 6)
// The connectors of a 24 x 24 icon (assets/UI_Sprites/README.md), y from its top: mix in and mod in on the left edge, one output on
// the right edge (mix out for a sound module, mod out for a modulator).
#define PIN_MIX_IN_Y 6
#define PIN_MOD_IN_Y 18
#define PIN_OUT_Y    18
// The columns of the 8 px gap after a cell, from that cell's icon edge (the selection frames take +25 / +26 and +29 / +30):
#define COL_OUT     (MODULE_SPRITE_W + 3)           // the left cell's output turns up or down here
#define COL_MIX_IN  (MODULE_SPRITE_W + 4)           // a mix link that skipped cells comes up to the right cell's mix in
#define COL_MOD_IN  (MODULE_SPRITE_W + 7)           // a mod link comes up to the right cell's mod in
#define INFO_Y      STRIP_H
#define FIELDS_Y    (STRIP_H + GUI_GRID)
#define FIELD_PITCH (2 * GUI_GRID)

// A module's icon: assets/UI_Sprites/24/mod_<code>.png when it exists (ui_sprites_gen.h), else the generated one (module_sprites.h).
static const gui_sprite_t *module_sprite(int type) {
    static const gui_sprite_t *const t[MOD_TYPE_COUNT] = {&spr_mod_osc, &spr_mod_filter, &spr_mod_sat,
                                                          &spr_mod_lfo, &spr_mod_mseq, &spr_mod_env, &spr_mod_sampler, &spr_mod_eg, &spr_mod_comb};
    static const char *const code[MOD_TYPE_COUNT] = {"osc", "filter", "sat", "lfo", "mseq", "env", "sampler", "eg", "comb"};
    char name[24];
    snprintf(name, sizeof name, "24/mod_%s", code[type]);
    const gui_sprite_t *art = ui_sprite(name);
    return art ? art : t[type];
}

// The icon of a module on its pages: an oscillator shows its wave's, 24/osc_<wave name in lower case>.png ("osc_karp", "osc_3saw"), when that
// image exists; else the module's.
const gui_sprite_t *ui_module_sprite(const rack_slot_t *s) {
    if (s->type == MOD_OSC) {
        char wave[16], name[24];
        rack_mparam_format(s, MP_OC_WAVE, wave, sizeof wave);
        for (char *c = wave; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        snprintf(name, sizeof name, "24/osc_%s", wave);
        const gui_sprite_t *art = ui_sprite(name);
        if (art) return art;
    }
    return module_sprite(s->type);
}

// The empty slot / OUT cells: 24/slot_empty.png, 24/slot_out.png, or the generated ones.
static const gui_sprite_t *slot_sprite(bool out) {
    const gui_sprite_t *art = ui_sprite(out ? "24/slot_out" : "24/slot_empty");
    return art ? art : out ? &spr_slot_out : &spr_slot_empty;
}

// Lines between two points in any order, solid or dotted (a dotted line has a pixel every second step).
static void line_h(u8g2_t *g, int x0, int x1, int y, bool solid) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x += solid ? 1 : 2) u8g2_DrawPixel(g, x, y);
}
static void line_v(u8g2_t *g, int x, int y0, int y1, bool solid) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y += solid ? 1 : 2) u8g2_DrawPixel(g, x, y);
}

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

// Cell i (0..RACK_MAX: the slots, then OUT) is visible while it is inside the window. Its icon is centred in its 32 px cell: the icon's left
// edge is cell_x(), its top r.y + STRIP_TOP.
#define CELL_GAP ((RACK_PITCH - MODULE_SPRITE_W) / 2)           // 4: from a cell's edge to its icon
static bool cell_vis(int sc, int i) { return i >= sc && i < sc + RACK_VIS; }
static int cell_x(gui_rect_t r, int sc, int i) { return r.x + (r.w - RACK_VIS * RACK_PITCH) / 2 + (i - sc) * RACK_PITCH + CELL_GAP; }

static void strip_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)st; (void)latched;
    const synth_ui_t *ui = c->ui;
    const rack_t *rk = c->rack;
    const int y = r.y + STRIP_TOP, sc = ui->rack_scroll;
    for (int i = 0; i < rk->count; i++)
        if (cell_vis(sc, i)) gui_draw_sprite(g, module_sprite(rk->slot[i].type), cell_x(r, sc, i), y);
    if (rk->count < RACK_MAX && cell_vis(sc, rk->count)) gui_draw_sprite(g, slot_sprite(false), cell_x(r, sc, rk->count), y);
    if (cell_vis(sc, RACK_MAX)) gui_draw_sprite(g, slot_sprite(true), cell_x(r, sc, RACK_MAX), y);

    // the selected cell: a frame around its icon, doubled while the strip has the focus
    const int sel = ui->rack_cur;
    if (cell_vis(sc, sel)) {
        const int sx = cell_x(r, sc, sel);
        u8g2_DrawFrame(g, (u8g2_uint_t)(sx - 2), (u8g2_uint_t)(y - 2), MODULE_SPRITE_W + 4, MODULE_SPRITE_H + 4);
        if (focused) u8g2_DrawFrame(g, (u8g2_uint_t)(sx - 3), (u8g2_uint_t)(y - 3), MODULE_SPRITE_W + 6, MODULE_SPRITE_H + 6);
    }
    // scroll arrows in the screen's side margins when more cells are hidden
    const int my = y + MODULE_SPRITE_H / 2;
    if (sc > 0) gui_draw_arrow(g, r.x, my, 'l', 3);
    if (sc + RACK_VIS < RACK_MAX + 1) gui_draw_arrow(g, r.x + r.w - 1, my, 'r', 3);
}

/* ---------------- links between the fixed connectors (assets/UI_Sprites/README.md) ---------------- */

// Where a link meets module i: its icon's left edge while visible, else the window border on its side.
static int link_x(gui_rect_t r, int sc, int i, bool *vis) {
    *vis = cell_vis(sc, i);
    return *vis ? cell_x(r, sc, i) : i < sc ? r.x : r.x + r.w - 1;
}

// Mix link from a's output to b's mix in, a before b. Neighbours: out of a, up the gap to the mix-in height, into b. Otherwise down the gap after
// a, along the audio lane, up the gap before b. A module off screen joins at the window border.
static void audio_link(u8g2_t *g, gui_rect_t r, int sc, int a, int b, bool solid) {
    bool va, vb;
    const int xa = link_x(r, sc, a, &va), xb = link_x(r, sc, b, &vb), top = r.y + STRIP_TOP;
    const int yo = top + PIN_OUT_Y, yi = top + PIN_MIX_IN_Y, yl = r.y + LANE_AUDIO;
    if (!va && !vb && (a < sc) == (b < sc)) return;                // both off screen on the same side
    int x0 = xa, x1 = xb;
    if (b == a + 1) {
        if (va) { x0 = xa + COL_OUT; line_h(g, xa + MODULE_SPRITE_W - 1, x0, yo, solid); line_v(g, x0, yo, yi, solid); }
        line_h(g, x0, xb, yi, solid);
        return;
    }
    if (va) { x0 = xa + COL_OUT; line_h(g, xa + MODULE_SPRITE_W - 1, x0, yo, solid); line_v(g, x0, yo, yl, solid); }
    if (vb) { x1 = xb - RACK_PITCH + COL_MIX_IN; line_h(g, x1, xb, yi, solid); line_v(g, x1, yi, yl, solid); }
    line_h(g, x0, x1, yl, solid);
}

// Modulation link (dotted) from s's output to t's mod in: down the gap after s, along the mod lane, up the gap before t (beside the icon) and
// into t. A neighbour on the right is reached straight across (output and mod in are at the same height). A module off screen joins the lane
// at the window border.
static void mod_link(u8g2_t *g, gui_rect_t r, int sc, int s, int t) {
    bool vs, vt;
    const int xs = link_x(r, sc, s, &vs), xt = link_x(r, sc, t, &vt), top = r.y + STRIP_TOP;
    const int yo = top + PIN_OUT_Y, yi = top + PIN_MOD_IN_Y, yl = r.y + LANE_MOD;
    if (!vs && !vt && (s < sc) == (t < sc)) return;
    if (t == s + 1) { line_h(g, vs ? xs + MODULE_SPRITE_W - 1 : xs, xt, yo, false); return; }
    int x0 = xs, x1 = xt;
    if (vs) { x0 = xs + COL_OUT; line_h(g, xs + MODULE_SPRITE_W - 1, x0, yo, false); line_v(g, x0, yo, yl, false); }
    if (vt) { x1 = xt - RACK_PITCH + COL_MOD_IN; line_h(g, x1, xt, yi, false); line_v(g, x1, yi, yl, false); }
    line_h(g, x0, x1, yl, false);
}

/* ---------------- layout (anchors) and what is drawn behind the elements ---------------- */

// The description line only appears when it fits: under it come the 4 rows of fields.
static bool info_fits(gui_rect_t area) { return FIELDS_Y + 4 * FIELD_PITCH <= area.h; }

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect);
static const el_def_t elements[E_COUNT];

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)g; (void)st; (void)c;
    rect[E_STRIP] = gui_rect(area.x, area.y, area.w, STRIP_H);
    // the fields: a 2 x 4 grid of 16 px rows (15 px boxes, 1 px apart) in two 64 px columns; an element spanning both columns takes the whole row
    const gui_rect_t fields = gui_rect(area.x, area.y + (info_fits(area) ? FIELDS_Y : INFO_Y), area.w, 4 * FIELD_PITCH - 1);
    for (int i = 1; i < E_COUNT; i++) {
        const gui_rect_t first = gui_grid_cell(fields, 2, 4, (elements[i].row - 1) * 2 + elements[i].col, 1);
        rect[i] = elements[i].span > 1 ? gui_rect(first.x, first.y, fields.w, first.h) : first;
    }
}

static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)st;
    const rack_t *r = c->rack;
    const synth_ui_t *ui = c->ui;
    const gui_rect_t strip = rect[E_STRIP];
    const int sc = ui->rack_scroll;
    // Every link is always drawn; a module scrolled off screen joins its lane at the window border on its side.

    // audio chain, ending at OUT: solid while a source feeds it, dotted otherwise
    int prev = RACK_NONE;
    for (int i = 0; i < r->count; i++) {
        if (!rack_slot_is_audio(r, i)) continue;
        if (prev != RACK_NONE) audio_link(g, strip, sc, prev, i, rack_has_signal(r, prev));
        prev = i;
    }
    if (prev != RACK_NONE) audio_link(g, strip, sc, prev, RACK_MAX, rack_has_signal(r, prev));

    // modulator links
    for (int i = 0; i < r->count; i++) {
        const rack_slot_t *s = &r->slot[i];
        if (!rack_slot_is_mod(r, i) || !s->tgt_id) continue;
        const int ti = rack_find(r, s->tgt_id);
        if (ti != RACK_NONE) mod_link(g, strip, sc, i, ti);
    }
    for (int i = 0; i < r->count; i++) {                         // motion sequencer: one link per lane that has a target
        if (r->slot[i].type != MOD_MSEQ) continue;
        const ms_pattern_t *pat = rack_ms_const(r, i);
        for (int l = 0; l < MS_LANES; l++) {
            const int ti = pat->lane[l].tgt_id ? rack_find(r, pat->lane[l].tgt_id) : RACK_NONE;
            if (ti != RACK_NONE) mod_link(g, strip, sc, i, ti);
        }
    }

    // one line describing the selection, in its own 8 px row
    if (info_fits(area)) {
        char buf[32];
        if (ui->rack_cur < r->count) rack_describe(r, ui->rack_cur, buf, sizeof buf);
        else snprintf(buf, sizeof buf, "+ %s", rack_type_name((module_type_t)ui->rack_type));
        gui_draw_text_centered(g, gui_rect(area.x, area.y + INFO_Y, area.w, GUI_GRID), buf);
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

const screen_def_t scr_rack_screen = {elements, E_COUNT, false, true, layout, back_delete, after_edit, draw_extra};
