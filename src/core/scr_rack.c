// The RACK tab of the menu: a declarative screen (see ui_screen.h). The rack's three lanes (ADR-040) are three rows of module icons; a push
// on a cell opens a small menu of what can be done there. The joystick / encoder events are handled by the generic screen code.
//
//   [header: < RACK >]                       focus: joystick up from the first lane
//   [ lane A: its modules, +, its output ]   up / down: the lane;  left / right: the cell;  push: the menu;  Back: delete the module
//   [ lane B ]
//   [ lane C ]
//   one line describing the selection
// The menu (only the rows that apply to the cell): Type, Insert (before the module, or at the lane's end on the + cell), Tgt / Prm / Dpth
// (a modulator), Lane (move the module), Lvl / Pan (the lane's level and pan: on its Sum or its output cell), Delete. Back closes it.
#include "core/ui_screen.h"
#include "core/sprites.h"
#include "core/module_sprites.h"
#include "core/ui_sprites_gen.h"
#include <stdio.h>

_Static_assert(RACK_PITCH == MODULE_SPRITE_W + GUI_GRID, "RACK_PITCH (ui_internal.h) must be the module sprite width + one grid cell: regenerate the sprites or change it");

enum { E_LANE0, E_LANE1, E_LANE2, M_TYPE, M_INSERT, M_TGT, M_PRM, M_DPTH, M_LANE, M_LVL, M_PAN, M_DELETE, E_COUNT };
_Static_assert(RACK_LANES == 3, "one element per lane");

// A lane row on the 8 px grid: 4 px for the selection frame, the 24 px icons, 4 px under them for the links. y from the row's top.
#define LANE_H      RACK_PITCH
#define ICON_TOP    4
#define LINE_Y      (LANE_H - 1)                    // where links that skip cells run (audio solid, modulation dotted)
// The connectors of a 24 x 24 icon (assets/UI_Sprites/README.md), y from its top: mix in and mod in on the left edge, one output on
// the right edge (mix out for a sound module, mod out for a modulator).
#define PIN_MIX_IN_Y 6
#define PIN_MOD_IN_Y 18
#define PIN_OUT_Y    18
// The columns of the 8 px gap after a cell, from that cell's icon edge:
#define COL_OUT     (MODULE_SPRITE_W + 3)           // the left cell's output turns up or down here
#define COL_MIX_IN  (MODULE_SPRITE_W + 4)           // a mix link that skipped cells comes up to the right cell's mix in
#define COL_MOD_IN  (MODULE_SPRITE_W + 7)           // a mod link comes up to the right cell's mod in
#define MENU_ITEM_H 15
#define MENU_W      88

// A module's icon: assets/UI_Sprites/24/mod_<code>.png when it exists (ui_sprites_gen.h), else the generated one (module_sprites.h).
static const gui_sprite_t *module_sprite(int type) {
    static const gui_sprite_t *const t[MOD_TYPE_COUNT] = {&spr_mod_osc, &spr_mod_filter, &spr_mod_sat,
                                                          &spr_mod_lfo, &spr_mod_mseq, &spr_mod_env, &spr_mod_sampler, &spr_mod_eg, &spr_mod_comb,
                                                          &spr_mod_sum, &spr_mod_trem, &spr_mod_eq, &spr_mod_ring, &spr_mod_phaser, &spr_mod_flanger,
                                                          &spr_mod_comp, &spr_mod_delay, &spr_mod_reverb, &spr_mod_chorus, &spr_mod_spectral,
                                                          &spr_mod_cab, &spr_mod_ensemble};
    static const char *const code[MOD_TYPE_COUNT] = {"osc", "filter", "sat", "lfo", "mseq", "env", "sampler", "eg", "comb",
                                                     "sum", "trem", "eq", "ring", "phaser", "flanger", "comp", "delay", "reverb", "chorus",
                                                     "spectral", "cab", "ensemble"};
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

/* ---------------- the cells of a lane ---------------- */

// A lane's cells: its modules in slot order, then the + cell (while the rack has room), then its output (the implicit Sum: level and pan).
typedef enum { CELL_MODULE, CELL_PLUS, CELL_OUT } cell_kind_t;
typedef struct { cell_kind_t kind; int slot; } cell_t;            // slot: the module (CELL_MODULE), else RACK_NONE

static int lane_modules(const rack_t *r, int lane, int *out) {
    int n = 0;
    for (int i = 0; i < r->count; i++) if (r->slot[i].lane == lane) out[n++] = i;
    return n;
}
static int lane_cells(const rack_t *r, int lane) { int s[RACK_MAX]; return lane_modules(r, lane, s) + (r->count < RACK_MAX ? 1 : 0) + 1; }
static cell_t cell_at(const rack_t *r, int lane, int col) {
    int s[RACK_MAX];
    const int n = lane_modules(r, lane, s);
    if (col < n) return (cell_t){CELL_MODULE, s[col]};
    if (col == n && r->count < RACK_MAX) return (cell_t){CELL_PLUS, RACK_NONE};
    return (cell_t){CELL_OUT, RACK_NONE};
}
static int col_of_slot(const rack_t *r, int slot) {
    int col = 0;
    for (int i = 0; i < slot; i++) col += r->slot[i].lane == r->slot[slot].lane;
    return col;
}
static int out_col(const rack_t *r, int lane) { return lane_cells(r, lane) - 1; }

/* ---------------- state helpers ---------------- */

// The selected lane: the focused lane row, or (menu open, focus in the menu) the lane the menu was opened on.
static int sel_lane(const ui_ctx_t *c) {
    const int row = c->ui->row;
    return (!c->ui->rack_menu && row >= 1 && row <= RACK_LANES) ? row - 1 : c->ui->rack_lane;
}
static cell_t sel_cell(const ui_ctx_t *c) { const int l = sel_lane(c); return cell_at(c->rack, l, c->ui->rack_col[l]); }
static const rack_slot_t *sel_slot(const ui_ctx_t *c) { const cell_t k = sel_cell(c); return k.kind == CELL_MODULE ? &c->rack->slot[k.slot] : NULL; }
static int sel_target(const ui_ctx_t *c) {
    const rack_slot_t *s = sel_slot(c);
    return (s && rack_can_target((module_type_t)s->type) && s->tgt_id) ? rack_find(c->rack, s->tgt_id) : RACK_NONE;
}
// Where Insert puts a module: before the selected one, or after the lane's last module on the + cell (the end of the rack for an empty lane).
static int insert_pos(const ui_ctx_t *c) {
    const cell_t k = sel_cell(c);
    if (k.kind == CELL_MODULE) return k.slot;
    int s[RACK_MAX];
    const int n = lane_modules(c->rack, sel_lane(c), s);
    return n ? s[n - 1] + 1 : c->rack->count;
}

// Keep every lane's column valid, and the selected cell inside the visible window (one horizontal scroll for all lanes, so columns line up).
static void keep_visible(const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    for (int l = 0; l < RACK_LANES; l++) {
        const int n = lane_cells(c->rack, l);
        if (ui->rack_col[l] >= n) ui->rack_col[l] = n - 1;
        if (ui->rack_col[l] < 0) ui->rack_col[l] = 0;
    }
    const int col = ui->rack_col[sel_lane(c)];
    if (col < ui->rack_scroll) ui->rack_scroll = col;
    if (col >= ui->rack_scroll + RACK_VIS) ui->rack_scroll = col - RACK_VIS + 1;
    if (ui->rack_scroll < 0) ui->rack_scroll = 0;
    const cell_t k = sel_cell(c);
    ui->rack_cur = k.kind == CELL_MODULE ? k.slot : c->rack->count;  // the old cursor (jump slots, other screens): the module, else past the end
}

static void menu_close(const ui_ctx_t *c) { c->ui->rack_menu = false; c->ui->row = c->ui->rack_lane + 1; c->ui->latched = false; }

/* ---------------- elements: the lanes ---------------- */

static bool lanes_enabled(const ui_ctx_t *c) { return !c->ui->rack_menu; }
static bool lane_adjust(const ui_ctx_t *c, int dir) {
    const int n = lane_cells(c->rack, c->arg);
    c->ui->rack_col[c->arg] = (c->ui->rack_col[c->arg] + dir + n) % n;
    c->ui->rack_lane = c->arg;
    return false;
}
static int first_menu_row(const ui_ctx_t *c);
static void lane_activate(const ui_ctx_t *c) {
    c->ui->rack_lane = c->arg;
    c->ui->rack_menu = true;
    c->ui->row = first_menu_row(c);
    c->ui->latched = false;
}

/* ---------------- elements: the menu ---------------- */

static bool menu_open(const ui_ctx_t *c) { return c->ui->rack_menu; }

static bool type_enabled(const ui_ctx_t *c) { return menu_open(c) && sel_cell(c).kind != CELL_OUT; }
static void type_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", rack_type_code((module_type_t)c->ui->rack_type)); }
static bool type_adjust(const ui_ctx_t *c, int dir) { c->ui->rack_type = (c->ui->rack_type + dir + MOD_TYPE_COUNT) % MOD_TYPE_COUNT; return false; }

static bool insert_enabled(const ui_ctx_t *c) {
    return type_enabled(c) && rack_can_insert(c->rack, insert_pos(c), sel_lane(c), (module_type_t)c->ui->rack_type);
}
static void insert_activate(const ui_ctx_t *c) {
    if (rack_insert_lane(c->rack, insert_pos(c), sel_lane(c), (module_type_t)c->ui->rack_type)) c->ui->rack_dirty = true;   // the new module takes the cell
    menu_close(c);
}

static bool tgt_enabled(const ui_ctx_t *c) { const rack_slot_t *s = sel_slot(c); return menu_open(c) && s && rack_can_target((module_type_t)s->type); }
static void tgt_value(const ui_ctx_t *c, char *out, int n) {
    const int ti = sel_target(c);
    if (ti == RACK_NONE) snprintf(out, (size_t)n, "--"); else rack_slot_name(c->rack, ti, out, n);
}
static bool tgt_adjust(const ui_ctx_t *c, int dir) { rack_cycle_target(c->rack, sel_cell(c).slot, dir); c->ui->rack_dirty = true; return false; }

static bool prm_enabled(const ui_ctx_t *c) { return tgt_enabled(c) && sel_target(c) != RACK_NONE; }
static void prm_value(const ui_ctx_t *c, char *out, int n) {
    const int ti = sel_target(c);
    snprintf(out, (size_t)n, "%s", ti == RACK_NONE ? "--" : rack_param_name((module_type_t)c->rack->slot[ti].type, sel_slot(c)->tgt_param));
}
static bool prm_adjust(const ui_ctx_t *c, int dir) { rack_cycle_param(c->rack, sel_cell(c).slot, dir); c->ui->rack_dirty = true; return false; }

static bool dpth_enabled(const ui_ctx_t *c) { return prm_enabled(c) && rack_depth_index((module_type_t)sel_slot(c)->type) >= 0; }
static void dpth_value(const ui_ctx_t *c, char *out, int n) { rack_depth_format(c->rack, sel_cell(c).slot, out, n); }
static bool dpth_adjust(const ui_ctx_t *c, int dir) { if (rack_depth_adjust(c->rack, sel_cell(c).slot, dir)) c->ui->rack_dirty = true; return false; }

static bool lane_row_enabled(const ui_ctx_t *c) { return menu_open(c) && sel_slot(c) != NULL; }
static void lane_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%c", 'A' + sel_lane(c)); }
static bool lane_move(const ui_ctx_t *c, int dir) {                 // moves the module to the next / previous lane that accepts it
    const int slot = sel_cell(c).slot;
    for (int k = 1; k < RACK_LANES; k++) {
        const int to = (sel_lane(c) + dir * k + RACK_LANES) % RACK_LANES;
        if (rack_set_lane(c->rack, slot, to)) {
            c->ui->rack_lane = to;
            c->ui->rack_col[to] = col_of_slot(c->rack, slot);
            c->ui->rack_dirty = true;
            return false;
        }
    }
    return false;
}

// The lane's level and pan: on the output cell (the implicit Sum, or the Sum's values when the lane has one) and on a Sum module.
static bool mix_enabled(const ui_ctx_t *c) {
    if (!menu_open(c)) return false;
    const cell_t k = sel_cell(c);
    return k.kind == CELL_OUT || (k.kind == CELL_MODULE && c->rack->slot[k.slot].type == MOD_SUM);
}
static void lvl_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%.0f%%", (double)(*rack_lane_level(c->rack, sel_lane(c)) * 100.0f)); }
static bool lvl_adjust(const ui_ctx_t *c, int dir) {
    float *v = rack_lane_level(c->rack, sel_lane(c));
    float nv = *v + 0.05f * (float)dir;
    nv = nv < 0.0f ? 0.0f : nv > 1.0f ? 1.0f : nv;
    if (nv == *v) return false;
    *v = nv; c->ui->rack_dirty = true;
    return true;
}
static void pan_value(const ui_ctx_t *c, char *out, int n) {
    const float p = *rack_lane_pan(c->rack, sel_lane(c));
    if (p > -0.5f && p < 0.5f) snprintf(out, (size_t)n, "C"); else snprintf(out, (size_t)n, "%c%.0f", p < 0 ? 'L' : 'R', (double)(p < 0 ? -p : p));
}
static bool pan_adjust(const ui_ctx_t *c, int dir) {
    float *v = rack_lane_pan(c->rack, sel_lane(c));
    float nv = *v + 5.0f * (float)dir;
    nv = nv < -100.0f ? -100.0f : nv > 100.0f ? 100.0f : nv;
    if (nv == *v) return false;
    *v = nv; c->ui->rack_dirty = true;
    return true;
}

static bool delete_enabled(const ui_ctx_t *c) { return menu_open(c) && sel_slot(c) != NULL; }
static void delete_activate(const ui_ctx_t *c) {
    if (rack_delete(c->rack, sel_cell(c).slot)) c->ui->rack_dirty = true;   // a Sum that global-only FX need stays (rack_delete refuses)
    menu_close(c);
}

// Back: closes the menu; on a lane, deletes the selected module (as before the lanes).
static bool back(const ui_ctx_t *c) {
    if (c->ui->rack_menu) { menu_close(c); return true; }
    const cell_t k = sel_cell(c);
    if (k.kind == CELL_MODULE && rack_delete(c->rack, k.slot)) c->ui->rack_dirty = true;
    return true;
}

/* ---------------- drawing ---------------- */

// Cell `col` is visible while it is inside the window; its icon's left edge is cell_x(), its top the row's top + ICON_TOP.
#define CELL_GAP ((RACK_PITCH - MODULE_SPRITE_W) / 2)           // 4: from a cell's edge to its icon
static bool cell_vis(int sc, int col) { return col >= sc && col < sc + RACK_VIS; }
static int cell_x(gui_rect_t r, int sc, int col) { return r.x + (r.w - RACK_VIS * RACK_PITCH) / 2 + (col - sc) * RACK_PITCH + CELL_GAP; }

static void lane_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)st; (void)latched;
    const rack_t *rk = c->rack;
    const int lane = c->arg, y = r.y + ICON_TOP, sc = c->ui->rack_scroll, n = lane_cells(rk, lane);
    for (int col = sc; col < sc + RACK_VIS && col < n; col++) {
        const cell_t k = cell_at(rk, lane, col);
        const gui_sprite_t *sp = k.kind == CELL_MODULE ? module_sprite(rk->slot[k.slot].type) : slot_sprite(k.kind == CELL_OUT);
        gui_draw_sprite(g, sp, cell_x(r, sc, col), y);
    }
    if (lane != sel_lane(c)) return;
    const int col = c->ui->rack_col[lane];                          // the selected cell: a frame, doubled while the lane (or its menu) has the focus
    if (cell_vis(sc, col)) {
        const int sx = cell_x(r, sc, col);
        u8g2_DrawFrame(g, (u8g2_uint_t)(sx - 2), (u8g2_uint_t)(y - 2), MODULE_SPRITE_W + 4, MODULE_SPRITE_H + 4);
        if (focused || c->ui->rack_menu) u8g2_DrawFrame(g, (u8g2_uint_t)(sx - 3), (u8g2_uint_t)(y - 3), MODULE_SPRITE_W + 6, MODULE_SPRITE_H + 6);
    }
    const int my = y + MODULE_SPRITE_H / 2;                         // scroll arrows when cells of this lane are hidden
    if (sc > 0) gui_draw_arrow(g, r.x, my, 'l', 3);
    if (sc + RACK_VIS < n) gui_draw_arrow(g, r.x + r.w - 1, my, 'r', 3);
}

// A menu row: its box cleared with a margin so the menu reads as one popup over the lanes; the popup's border around the rows.
static void menu_item_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched);

/* ---------------- links between the fixed connectors (assets/UI_Sprites/README.md) ---------------- */

// Where a link meets the cell at `col`: its icon's left edge while visible, else the window border on its side.
static int link_x(gui_rect_t r, int sc, int col, bool *vis) {
    *vis = cell_vis(sc, col);
    return *vis ? cell_x(r, sc, col) : col < sc ? r.x : r.x + r.w - 1;
}

// Mix link inside one lane row, from column a's output to column b's mix in (a < b). Neighbours: out of a, up the gap, into b. Otherwise down
// the gap after a, along the row's link line, up the gap before b.
static void audio_link(u8g2_t *g, gui_rect_t r, int sc, int a, int b, bool solid) {
    bool va, vb;
    const int xa = link_x(r, sc, a, &va), xb = link_x(r, sc, b, &vb), top = r.y + ICON_TOP;
    const int yo = top + PIN_OUT_Y, yi = top + PIN_MIX_IN_Y, yl = r.y + LINE_Y;
    if (!va && !vb && (a < sc) == (b < sc)) return;
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

// Modulation link (dotted) from a module to its target, possibly on another lane: out of the source, down (or up) the gap after it to the
// target row's link line, along it, up the gap before the target into its mod in. A neighbour on the same lane is reached straight across.
static void mod_link(u8g2_t *g, gui_rect_t rs, gui_rect_t rt, int sc, int s, int t) {
    bool vs, vt;
    const int xs = link_x(rs, sc, s, &vs), xt = link_x(rt, sc, t, &vt);
    const int yo = rs.y + ICON_TOP + PIN_OUT_Y, yi = rt.y + ICON_TOP + PIN_MOD_IN_Y, yl = rt.y + LINE_Y;
    if (!vs && !vt && (s < sc) == (t < sc)) return;
    if (rs.y == rt.y && t == s + 1) { line_h(g, vs ? xs + MODULE_SPRITE_W - 1 : xs, xt, yo, false); return; }
    int x0 = xs, x1 = xt;
    if (vs) { x0 = xs + COL_OUT; line_h(g, xs + MODULE_SPRITE_W - 1, x0, yo, false); }
    line_v(g, x0, yo, yl, false);
    if (vt) { x1 = xt - RACK_PITCH + COL_MOD_IN; line_h(g, x1, xt, yi, false); line_v(g, x1, yi, yl, false); }
    line_h(g, x0, x1, yl, false);
}

/* ---------------- layout and what is drawn behind the elements ---------------- */

static bool info_fits(gui_rect_t area) { return RACK_LANES * LANE_H + GUI_GRID <= area.h; }
static const el_def_t elements[E_COUNT];

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)g; (void)st;
    keep_visible(c);
    const bool all = RACK_LANES * LANE_H <= area.h;               // a short screen shows only the selected lane
    for (int l = 0; l < RACK_LANES; l++)
        rect[E_LANE0 + l] = all ? gui_rect(area.x, area.y + l * LANE_H, area.w, LANE_H)
                                : gui_rect(area.x, l == sel_lane(c) ? area.y : area.y + area.h + 8, area.w, LANE_H);
    // the menu: its enabled rows stacked in a box over the lanes; the others out of the way (not drawn: EF_HIDE_WHEN_DISABLED)
    int y = area.y + 2;
    for (int i = M_TYPE; i < E_COUNT; i++) {
        const ui_ctx_t x = {c->ui, c->rack, elements[i].arg};
        if (elements[i].enabled(&x)) { rect[i] = gui_rect(area.x + (area.w - MENU_W) / 2, y, MENU_W, MENU_ITEM_H - 1); y += MENU_ITEM_H; }
        else rect[i] = gui_rect(0, area.y + area.h + 8, MENU_W, MENU_ITEM_H - 1);
    }
}

static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)st;
    const rack_t *r = c->rack;
    const int sc = c->ui->rack_scroll;
    // each lane's audio chain, ending at its output cell: solid while a source feeds it, dotted otherwise
    for (int l = 0; l < RACK_LANES; l++) {
        const gui_rect_t row = rect[E_LANE0 + l];
        if (row.y > area.y + area.h) continue;
        int prev = RACK_NONE;
        for (int i = 0; i < r->count; i++) {
            if (r->slot[i].lane != l || !rack_slot_is_audio(r, i)) continue;
            if (prev != RACK_NONE) audio_link(g, row, sc, col_of_slot(r, prev), col_of_slot(r, i), rack_has_signal(r, prev));
            prev = i;
        }
        if (prev != RACK_NONE) audio_link(g, row, sc, col_of_slot(r, prev), out_col(r, l), rack_has_signal(r, prev));
    }
    // modulator links, across lanes too
    for (int i = 0; i < r->count; i++) {
        const rack_slot_t *s = &r->slot[i];
        int tg[1 + MS_LANES], nt = 0;
        if (rack_slot_is_mod(r, i) && s->tgt_id) tg[nt++] = rack_find(r, s->tgt_id);
        if (s->type == MOD_MSEQ) {
            const ms_pattern_t *pat = rack_ms_const(r, i);
            for (int l = 0; l < MS_LANES; l++) if (pat->lane[l].tgt_id) tg[nt++] = rack_find(r, pat->lane[l].tgt_id);
        }
        for (int k = 0; k < nt; k++) {
            if (tg[k] == RACK_NONE) continue;
            const gui_rect_t rs = rect[E_LANE0 + s->lane], rt = rect[E_LANE0 + r->slot[tg[k]].lane];
            if (rs.y > area.y + area.h || rt.y > area.y + area.h) continue;
            mod_link(g, rs, rt, sc, col_of_slot(r, i), col_of_slot(r, tg[k]));
        }
    }
    // one line describing the selection, under the lanes
    if (info_fits(area)) {
        char buf[32], what[28];
        const cell_t k = sel_cell(c);
        const int lane = sel_lane(c);
        if (k.kind == CELL_MODULE) rack_describe(r, k.slot, what, sizeof what);
        else if (k.kind == CELL_PLUS) snprintf(what, sizeof what, "+ %s", rack_type_name((module_type_t)c->ui->rack_type));
        else snprintf(what, sizeof what, "%s", rack_lane_sum(r, lane) != RACK_NONE ? "out (Sum)" : "out, implicit Sum");
        snprintf(buf, sizeof buf, "%c %s", 'A' + lane, what);
        gui_draw_text_centered(g, gui_rect(area.x, area.y + RACK_LANES * LANE_H, area.w, GUI_GRID), buf);
    }
}

static int first_menu_row(const ui_ctx_t *c) {
    for (int i = M_TYPE; i < E_COUNT; i++) { const ui_ctx_t x = {c->ui, c->rack, elements[i].arg}; if (elements[i].enabled(&x)) return i + 1; }
    return c->ui->rack_lane + 1;
}

static void menu_item_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    // which menu row this is, and whether it is the first / last one shown
    int me = -1, first = -1, last = -1;
    for (int i = M_TYPE; i < E_COUNT; i++) {
        const ui_ctx_t x = {c->ui, c->rack, elements[i].arg};
        if (!elements[i].enabled(&x)) continue;
        if (first < 0) first = i;
        last = i;
    }
    for (int i = M_TYPE; i < E_COUNT; i++) if (elements[i].draw == menu_item_draw && elements[i].arg == c->arg) me = i;
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawBox(g, (u8g2_uint_t)(r.x - 3), (u8g2_uint_t)(r.y - (me == first ? 3 : 0)), (u8g2_uint_t)(r.w + 6), (u8g2_uint_t)(r.h + 1 + (me == first ? 3 : 0) + (me == last ? 2 : 0)));
    u8g2_SetDrawColor(g, 1);
    const int top = r.y - (me == first ? 3 : 0), bot = r.y + r.h + (me == last ? 2 : 0);
    line_v(g, r.x - 3, top, bot, true);
    line_v(g, r.x + r.w + 2, top, bot, true);
    if (me == first) line_h(g, r.x - 3, r.x + r.w + 2, top, true);
    if (me == last) line_h(g, r.x - 3, r.x + r.w + 2, bot, true);
    const el_def_t *e = &elements[me];
    const gui_state_t state = latched ? GUI_LATCHED : focused ? GUI_FOCUSED : GUI_PLAIN;
    if (e->kind == EL_BUTTON) { gui_draw_button(g, st, r, e->label, state); return; }
    char val[24] = "";
    if (e->value) e->value(c, val, sizeof val);
    gui_draw_field_state(g, st, r, e->label, val, state);
}

/* ---------------- the table ---------------- */

#define LANE(l)  {"", EL_DIRECT, (l), 0, 1, NULL, lane_adjust, lane_activate, lane_draw, lanes_enabled, NULL, (l), 0}
#define ITEM(label, kind, row, value, adjust, activate, enabled, arg) \
    {label, kind, (row), 0, 1, value, adjust, activate, menu_item_draw, enabled, NULL, (arg), EF_HIDE_WHEN_DISABLED}
static const el_def_t elements[E_COUNT] = {
    [E_LANE0]  = LANE(0), [E_LANE1] = LANE(1), [E_LANE2] = LANE(2),
    [M_TYPE]   = ITEM("Type",   EL_VALUE,  3,  type_value, type_adjust, NULL,            type_enabled,     M_TYPE),
    [M_INSERT] = ITEM("Insert", EL_BUTTON, 4,  NULL,       NULL,        insert_activate, insert_enabled,   M_INSERT),
    [M_TGT]    = ITEM("Tgt",    EL_VALUE,  5,  tgt_value,  tgt_adjust,  NULL,            tgt_enabled,      M_TGT),
    [M_PRM]    = ITEM("Prm",    EL_VALUE,  6,  prm_value,  prm_adjust,  NULL,            prm_enabled,      M_PRM),
    [M_DPTH]   = ITEM("Dpth",   EL_VALUE,  7,  dpth_value, dpth_adjust, NULL,            dpth_enabled,     M_DPTH),
    [M_LANE]   = ITEM("Lane",   EL_VALUE,  8,  lane_value, lane_move,   NULL,            lane_row_enabled, M_LANE),
    [M_LVL]    = ITEM("Lvl",    EL_VALUE,  9,  lvl_value,  lvl_adjust,  NULL,            mix_enabled,      M_LVL),
    [M_PAN]    = ITEM("Pan",    EL_VALUE,  10, pan_value,  pan_adjust,  NULL,            mix_enabled,      M_PAN),
    [M_DELETE] = ITEM("Delete", EL_BUTTON, 11, NULL,       NULL,        delete_activate, delete_enabled,   M_DELETE),
};

// After every edit: keep the selection valid and in view.
static void after_edit(const ui_ctx_t *c) { keep_visible(c); }

const screen_def_t scr_rack_screen = {elements, E_COUNT, false, false, layout, back, after_edit, draw_extra};
