// The FX RACK tab of the menu: four master effect slots. Slot picks the slot, Type its effect (a new type rebuilds the master chain), then
// the parameters of that effect (their number and names depend on the type; unused rows are hidden). Edits are live. On the right: a
// sketch of what the effect does. A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include <stdio.h>

static fx_slot_t *slot_of(const ui_ctx_t *c) { return &c->rack->cfg.fxr.slot[c->ui->fx_slot]; }

static void slot_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%d/%d", c->ui->fx_slot + 1, FXR_SLOTS); }
static bool slot_adjust(const ui_ctx_t *c, int dir) { c->ui->fx_slot = (c->ui->fx_slot + dir + FXR_SLOTS) % FXR_SLOTS; return false; }

static void type_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", fxr_type_code(slot_of(c)->type)); }
static bool type_adjust(const ui_ctx_t *c, int dir) { if (fxr_cycle_type(slot_of(c), dir)) c->ui->rebuild = true; return false; }

// arg = the parameter index
static bool prm_enabled(const ui_ctx_t *c) { return c->arg < fxr_param_count(slot_of(c)->type); }
static const char *prm_label(const ui_ctx_t *c) { return fxr_label(slot_of(c)->type, c->arg); }
static void prm_value(const ui_ctx_t *c, char *out, int n) { fxr_format(slot_of(c), c->arg, out, (size_t)n); }
static bool prm_adjust(const ui_ctx_t *c, int dir) { return fxr_adjust(slot_of(c), c->arg, dir); }

#define PRM(i) {"", EL_VALUE, 2 + (i), 0, 1, prm_value, prm_adjust, NULL, NULL, prm_enabled, prm_label, (i), EF_HIDE_WHEN_DISABLED}
static const el_def_t elements[2 + FXR_PARAMS] = {
    {"Slot", EL_VALUE, 0, 0, 1, slot_value, slot_adjust, NULL, NULL, NULL},
    {"Type", EL_VALUE, 1, 0, 1, type_value, type_adjust, NULL, NULL, NULL},
    PRM(0), PRM(1), PRM(2), PRM(3),
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)area; (void)rect;
    draw_fx_picture(g, ui_picture_box(st), &c->rack->cfg.fxr, c->ui->fx_slot);
}

const screen_def_t scr_fx_screen = {elements, N_ELEMENTS, true, layout, NULL, NULL, draw_extra};
