// The FM editor tab of the menu (ALGORITHM): the DX7 patch being edited, rack->cfg.fm. Edits are live.
// Algo and Fb on the left, the operator tree on the right (the algorithm's diagram); the Op row picks an operator in the tree (left / right,
// the selected one inverted) and a push opens that operator's page in the main view (OPn: Lvl Crs Fine Fix, then OPn ENV), like a module of
// the rack. The operators' values are edited there (ui_pages.c, fm_page_row), where the knobs and learn work on them.
// A declarative screen (see ui_screen.h): list on the left, the patch name under it.
#include "core/ui_screen.h"
#include <stdio.h>

static dx7_patch_t *patch(const ui_ctx_t *c) { return &c->rack->cfg.fm; }

static void algo_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%d", patch(c)->algorithm); }
static bool algo_adjust(const ui_ctx_t *c, int dir) { return dx7_algorithm_adjust(patch(c), dir); }
static void fb_value(const ui_ctx_t *c, char *out, int n) { dx7_feedback_format(patch(c), out, (size_t)n); }
static bool fb_adjust(const ui_ctx_t *c, int dir) { return dx7_feedback_adjust(patch(c), dir); }

static void op_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "OP%d >", c->ui->fm_op + 1); }
static bool op_adjust(const ui_ctx_t *c, int dir) { c->ui->fm_op = (c->ui->fm_op + dir + DX7_OPS) % DX7_OPS; return false; }
static void op_activate(const ui_ctx_t *c) { synth_ui_open_page(c->ui, c->rack, GLOBAL_PAGE, GP_FM_OP_BASE + c->ui->fm_op); }

static const el_def_t algo_elements[] = {
    {"Algo", EL_VALUE,  0, 0, 1, algo_value, algo_adjust, NULL,        NULL, NULL},
    {"Fb",   EL_VALUE,  1, 0, 1, fb_value,   fb_adjust,   NULL,        NULL, NULL},
    {"Op",   EL_DIRECT, 2, 0, 1, op_value,   op_adjust,   op_activate, NULL, NULL},
};
#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

static void layout_algo(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) { (void)c; ui_layout_column(g, st, area, COUNT(algo_elements), rect); }

static void extra_algo(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)rect;
    const int rh = gui_row_h(g, st);
    gui_draw_text_left(g, st, gui_rect(area.x, gui_bottom(area) - rh * 2 + 2, st->list_w, rh), patch(c)->name);
    draw_algo(g, ui_picture_box(st), patch(c), c->ui->fm_op);
}

const screen_def_t scr_fm_algo_screen = {algo_elements, COUNT(algo_elements), true, false, layout_algo, NULL, NULL, extra_algo};
