// The MACHINE tab (SETUP view; apps/ritual_machine/docs/DESIGN.md ADR-046 decision 4: Out, Spk and Knob came from SOUND): the machine's own settings, the
// output, the speaker, the knobs' mode, and Storage (the USB card reader: the app asks, saves everything, then restarts). The picture
// on the right: the battery. A declarative screen (see ui_screen.h): list on the left, picture on the right.
#include "core/ui_screen.h"
#include "hal/hal_power.h"
#include <stdio.h>

// arg = the setting (cfg_param_id_t); all change live.
static const char *cfg_label(const ui_ctx_t *c) { return synth_config_label((cfg_param_id_t)c->arg); }
static void cfg_value(const ui_ctx_t *c, char *out, int n) { synth_config_format(&c->rack->cfg, (cfg_param_id_t)c->arg, out, (size_t)n); }
static bool cfg_adjust(const ui_ctx_t *c, int dir) { return synth_config_adjust(&c->rack->cfg, (cfg_param_id_t)c->arg, dir) == CFG_LIVE; }

static void storage_activate(const ui_ctx_t *c) { c->ui->storage_req = true; }

static const el_def_t elements[] = {
    {"Out",     EL_VALUE,  0, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL, cfg_label, CFGP_OUTPUT, 0},
    {"Spk",     EL_VALUE,  1, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL, cfg_label, CFGP_SPEAKER, 0},
    {"Knob",    EL_VALUE,  2, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL, cfg_label, CFGP_KNOB_MODE, 0},
    {"Storage", EL_BUTTON, 3, 0, 1, NULL,      NULL,       storage_activate, NULL, NULL, NULL, 0, 0},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

int scr_machine_setting(int row) { return row >= 1 && row <= N_ELEMENTS && elements[row - 1].kind == EL_VALUE ? elements[row - 1].arg : -1; }

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

// The battery's voltage and charge, or that none is measured; on Storage what it does.
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)area; (void)rect;
    const gui_rect_t box = ui_picture_box(st);
    const int rh = gui_row_h(g, st);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    char buf[32];
    if (c->ui->row == 4) {
        gui_draw_text_centered(g, gui_rect(box.x, box.y + 2, box.w, rh), "USB card");
        gui_draw_text_centered(g, gui_rect(box.x, box.y + 2 + rh, box.w, rh), "reader");
        return;
    }
    const int pct = power_battery_percent();
    gui_draw_text_centered(g, gui_rect(box.x, box.y + 2, box.w, rh), "Battery");
    if (pct < 0) snprintf(buf, sizeof buf, "none");
    else { const int mv = power_battery_mv(); snprintf(buf, sizeof buf, "%d.%dV %d%%", mv / 1000, mv % 1000 / 100, pct); }
    gui_draw_text_centered(g, gui_rect(box.x, box.y + 2 + rh, box.w, rh), buf);
}

const screen_def_t scr_machine_screen = {elements, N_ELEMENTS, true, false, layout, NULL, NULL, draw_extra};
