// The LEDS tab of the menu: the colour of each key role (core/led_roles.h). Role picks a role; Idle / Active pick a named colour, the % rows
// its brightness (Active only for the roles that have an active state); Reset puts every role back to the defaults. While this tab is on
// screen only the keys of the role shown light (in the Idle colour on the Idle rows, else the Active one) and every other key is off, so the
// choice can be judged on the keys themselves (key_leds.c; it reads the row numbers of `elements`).
// On the right: the keys of the board, the ones with this role filled. Saved in ui.cfg by itself.
// A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include "core/led_roles.h"
#include "hal/hal_input.h"
#include <stdio.h>

static int role(const ui_ctx_t *c) { return c->ui->led_role % LR_COUNT; }

static void role_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", led_role_name(role(c))); }
static bool role_adjust(const ui_ctx_t *c, int dir) { c->ui->led_role = (role(c) + dir + LR_COUNT) % LR_COUNT; return false; }

// arg: 0 Idle colour, 1 Idle %, 2 Active colour, 3 Active %.
static bool has_active(const ui_ctx_t *c) { return led_role_has_active(role(c)); }
static void shade_value(const ui_ctx_t *c, char *out, int n) {
    const led_role_def_t *d = led_role(role(c));
    const led_shade_t s = c->arg < 2 ? d->idle : d->active;
    if (c->arg & 1) snprintf(out, (size_t)n, "%d%%", s.pct);
    else            snprintf(out, (size_t)n, "%s", led_palette_name(s.color));
}
static bool shade_adjust(const ui_ctx_t *c, int dir) {
    led_role_def_t d = *led_role(role(c));
    led_shade_t *s = c->arg < 2 ? &d.idle : &d.active;
    if (c->arg & 1) { const int p = s->pct + 5 * dir; s->pct = (uint8_t)(p < 0 ? 0 : p > 100 ? 100 : p); }
    else s->color = (uint8_t)((s->color + dir + LED_PALETTE) % LED_PALETTE);
    led_role_set(role(c), d);
    return false;
}

static void reset_activate(const ui_ctx_t *c) { (void)c; led_roles_init(); }

static const el_def_t elements[] = {
    {"Role",   EL_VALUE,  0, 0, 1, role_value,  role_adjust,  NULL, NULL, NULL},
    {"Idle",   EL_VALUE,  1, 0, 1, shade_value, shade_adjust, NULL, NULL, NULL,       NULL, 0},
    {"Idle %", EL_VALUE,  2, 0, 1, shade_value, shade_adjust, NULL, NULL, NULL,       NULL, 1},
    {"Active", EL_VALUE,  3, 0, 1, shade_value, shade_adjust, NULL, NULL, has_active, NULL, 2},
    {"Act %",  EL_VALUE,  4, 0, 1, shade_value, shade_adjust, NULL, NULL, has_active, NULL, 3},
    {"Reset",  EL_BUTTON, 5, 0, 1, NULL,        NULL,         reset_activate, NULL, NULL},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

// The key grid (the rows and columns the board has), the keys whose role is the one shown filled.
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)area; (void)rect;
    const gui_rect_t box = ui_picture_box(st);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    int cols = 0, rows = 0;
    for (int r = 0; r < KEY_ROWS; r++)
        for (int k = 0; k < KEY_COLS; k++)
            if (input_key_present(r, k)) { if (k + 1 > cols) cols = k + 1; if (r + 1 > rows) rows = r + 1; }
    if (!cols || !rows) return;
    const int cw = (box.w - 4) / cols, ch = (box.h - 4) / rows < cw ? (box.h - 4) / rows : cw;   // square cells
    const int x0 = box.x + (box.w - cw * cols) / 2, y0 = box.y + (box.h - ch * rows) / 2;
    for (int r = 0; r < rows; r++)
        for (int k = 0; k < cols; k++) {
            if (!input_key_present(r, k)) continue;
            const int x = x0 + k * cw, y = y0 + r * ch;
            if (led_role_of(keymap_get(r * KEY_COLS + k)) == role(c)) u8g2_DrawBox(g, x + 1, y + 1, cw - 2, ch - 2);
            else u8g2_DrawFrame(g, x + 1, y + 1, cw - 2, ch - 2);
        }
}

const screen_def_t scr_leds_screen = {elements, N_ELEMENTS, false, false, layout, NULL, NULL, draw_extra};
