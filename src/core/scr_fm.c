// The three FM editor tabs of the menu (ALGORITHM, OPERATOR, ENVELOPE): the DX7 patch being edited, rack->cfg.fm. Edits are live.
// Each tab is a declarative screen (see ui_screen.h): list on the left, the algorithm diagram or the operator envelope on the right,
// and the patch name under the list.
#include "core/ui_screen.h"
#include <stdio.h>

_Static_assert(DXP_COUNT == 4, "the OPERATOR tab has one element per operator parameter: update op_elements");

static dx7_patch_t *patch(const ui_ctx_t *c) { return &c->rack->cfg.fm; }

// Operator selector, shared by the three tabs
static void op_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "OP%d", c->ui->fm_op + 1); }
static bool op_adjust(const ui_ctx_t *c, int dir) { c->ui->fm_op = (c->ui->fm_op + dir + DX7_OPS) % DX7_OPS; return false; }

/* ---- ALGORITHM: Algo, Fb, Op ---- */
static void algo_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%d", patch(c)->algorithm); }
static bool algo_adjust(const ui_ctx_t *c, int dir) { return dx7_algorithm_adjust(patch(c), dir); }
static void fb_value(const ui_ctx_t *c, char *out, int n) { dx7_feedback_format(patch(c), out, (size_t)n); }
static bool fb_adjust(const ui_ctx_t *c, int dir) { return dx7_feedback_adjust(patch(c), dir); }

static const el_def_t algo_elements[] = {
    {"Algo", EL_VALUE, 0, 0, 1, algo_value, algo_adjust, NULL, NULL, NULL},
    {"Fb",   EL_VALUE, 1, 0, 1, fb_value,   fb_adjust,   NULL, NULL, NULL},
    {"Op",   EL_VALUE, 2, 0, 1, op_value,   op_adjust,   NULL, NULL, NULL},
};

/* ---- OPERATOR: Op, then the parameters of that operator (arg = dx7_op_param_t) ---- */
static const char *opp_label(const ui_ctx_t *c) { return dx7_op_label((dx7_op_param_t)c->arg); }
static void opp_value(const ui_ctx_t *c, char *out, int n) { dx7_op_format(patch(c), c->ui->fm_op, (dx7_op_param_t)c->arg, out, (size_t)n); }
static bool opp_adjust(const ui_ctx_t *c, int dir) { return dx7_op_adjust(patch(c), c->ui->fm_op, (dx7_op_param_t)c->arg, dir); }

#define OPP(i) {"", EL_VALUE, 1 + (i), 0, 1, opp_value, opp_adjust, NULL, NULL, NULL, opp_label, (i)}
static const el_def_t op_elements[] = {
    {"Op", EL_VALUE, 0, 0, 1, op_value, op_adjust, NULL, NULL, NULL},
    OPP(0), OPP(1), OPP(2), OPP(3),
};

/* ---- ENVELOPE: Op, Pt, Lvl, Time (the selected point of the selected operator; arg = dx7_eg_field_t) ---- */
static void pt_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%d/4", c->ui->fm_pt + 1); }
static bool pt_adjust(const ui_ctx_t *c, int dir) { c->ui->fm_pt = (c->ui->fm_pt + dir + 4) % 4; return false; }
static void egf_value(const ui_ctx_t *c, char *out, int n) { dx7_eg_format(patch(c), c->ui->fm_op, c->ui->fm_pt, (dx7_eg_field_t)c->arg, out, (size_t)n); }
static bool egf_adjust(const ui_ctx_t *c, int dir) { return dx7_eg_adjust(patch(c), c->ui->fm_op, c->ui->fm_pt, (dx7_eg_field_t)c->arg, dir); }

static const el_def_t env_elements[] = {
    {"Op",   EL_VALUE, 0, 0, 1, op_value,  op_adjust,  NULL, NULL, NULL},
    {"Pt",   EL_VALUE, 1, 0, 1, pt_value,  pt_adjust,  NULL, NULL, NULL},
    {"Lvl",  EL_VALUE, 2, 0, 1, egf_value, egf_adjust, NULL, NULL, NULL, NULL, DXE_LEVEL},
    {"Time", EL_VALUE, 3, 0, 1, egf_value, egf_adjust, NULL, NULL, NULL, NULL, DXE_TIME},
};

/* ---- layout and pictures ---- */
#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

static void layout_algo(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) { (void)c; ui_layout_column(g, st, area, COUNT(algo_elements), rect); }
static void layout_op(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect)   { (void)c; ui_layout_column(g, st, area, COUNT(op_elements), rect); }
static void layout_env(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect)  { (void)c; ui_layout_column(g, st, area, COUNT(env_elements), rect); }

// the patch name at the bottom of the list column
static void patch_name(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area) {
    const int rh = gui_row_h(g, st);
    gui_draw_text_left(g, st, gui_rect(area.x, gui_bottom(area) - rh * 2 + 2, st->list_w, rh), patch(c)->name);
}

static void extra_algo(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)rect;
    patch_name(g, st, c, area);
    draw_algo(g, ui_picture_box(st), patch(c), c->ui->fm_op);
}
static void extra_env(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)rect;
    patch_name(g, st, c, area);
    draw_eg_editor(g, ui_picture_box(st), &patch(c)->op[c->ui->fm_op], c->ui->fm_pt);
}

const screen_def_t scr_fm_algo_screen = {algo_elements, COUNT(algo_elements), true, layout_algo, NULL, NULL, extra_algo};
const screen_def_t scr_fm_op_screen   = {op_elements,   COUNT(op_elements),   true, layout_op,   NULL, NULL, extra_algo};     // the algorithm diagram, with the selected operator marked
const screen_def_t scr_fm_env_screen  = {env_elements,  COUNT(env_elements),  true, layout_env,  NULL, NULL, extra_env};
