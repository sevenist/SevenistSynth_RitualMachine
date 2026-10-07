// The FX RACK tab of the menu: four master effect slots. Slot picks the slot, Type its effect (a new type rebuilds the master chain), then
// the parameters of that effect (their number and names depend on the type; unused rows are hidden). An effect with more than FXR_ROWS
// parameters has a cog under its rows: a push shows the others in place of the first ones (a second push goes back). Edits are live.
// On the right: a sketch of what the effect does. A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include "core/ui_sprites_gen.h"
#include <stdio.h>

static fx_slot_t *slot_of(const ui_ctx_t *c) { return &c->rack->cfg.fxr.slot[c->ui->fx_slot]; }

static void slot_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%d/%d", c->ui->fx_slot + 1, FXR_SLOTS); }
static bool slot_adjust(const ui_ctx_t *c, int dir) { c->ui->fx_slot = (c->ui->fx_slot + dir + FXR_SLOTS) % FXR_SLOTS; c->ui->fx_cog = false; return false; }

static void type_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", fxr_type_code(slot_of(c)->type)); }
static bool type_adjust(const ui_ctx_t *c, int dir) { if (fxr_cycle_type(slot_of(c), dir)) { c->ui->rebuild = true; c->ui->fx_cog = false; } return false; }

// arg = the row; the parameter is arg + FXR_ROWS while the cog is open
static int prm_index(const ui_ctx_t *c) { return c->arg + (c->ui->fx_cog ? FXR_ROWS : 0); }
static bool prm_enabled(const ui_ctx_t *c) { return prm_index(c) < fxr_param_count(slot_of(c)->type); }
static const char *prm_label(const ui_ctx_t *c) { return fxr_label(slot_of(c)->type, prm_index(c)); }
static void prm_value(const ui_ctx_t *c, char *out, int n) { fxr_format(slot_of(c), prm_index(c), out, (size_t)n); }
static bool prm_adjust(const ui_ctx_t *c, int dir) { return fxr_adjust(slot_of(c), prm_index(c), dir); }

static bool cog_enabled(const ui_ctx_t *c) { return fxr_param_count(slot_of(c)->type) > FXR_ROWS; }
static void cog_activate(const ui_ctx_t *c) { c->ui->fx_cog = !c->ui->fx_cog; }
static void cog_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)latched;
    const gui_sprite_t *cog = ui_sprite("8/ui_cog");
    const int cy = r.y + (r.h - GUI_GRID) / 2;
    if (cog && focused) gui_draw_sprite_selected(g, cog, r.x, cy);
    else if (cog) gui_draw_sprite(g, cog, r.x, cy);
    else u8g2_DrawFrame(g, r.x + 1, cy + 1, GUI_GRID - 2, GUI_GRID - 2);
    gui_draw_text_left(g, st, gui_rect(r.x + GUI_GRID + 2, r.y, r.w - GUI_GRID - 2, r.h), c->ui->fx_cog ? "Back" : "More");
}

#define PRM(i) {"", EL_VALUE, 2 + (i), 0, 1, prm_value, prm_adjust, NULL, NULL, prm_enabled, prm_label, (i), EF_HIDE_WHEN_DISABLED}
static const el_def_t elements[3 + FXR_ROWS] = {
    {"Slot", EL_VALUE, 0, 0, 1, slot_value, slot_adjust, NULL, NULL, NULL},
    {"Type", EL_VALUE, 1, 0, 1, type_value, type_adjust, NULL, NULL, NULL},
    PRM(0), PRM(1), PRM(2), PRM(3),
    {"", EL_BUTTON, 2 + FXR_ROWS, 0, 1, NULL, NULL, cog_activate, cog_draw, cog_enabled, NULL, 0, EF_HIDE_WHEN_DISABLED},
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

const screen_def_t scr_fx_screen = {elements, N_ELEMENTS, true, false, layout, NULL, NULL, draw_extra};
