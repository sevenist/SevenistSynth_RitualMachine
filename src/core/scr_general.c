// The GENERAL tab of the menu: synth type, FM patch, voices, master volume, and an info box on the right.
// A declarative screen (see ui_screen.h): list on the left, picture on the right.
#include "core/ui_screen.h"
#include <stdio.h>

// arg = the setting (cfg_param_id_t). Structural changes (type, voices, the patch in FM mode) are applied when the menu closes; the volume is live.
static const char *cfg_label(const ui_ctx_t *c) { return synth_config_label((cfg_param_id_t)c->arg); }
static void cfg_value(const ui_ctx_t *c, char *out, int n) { synth_config_format(&c->rack->cfg, (cfg_param_id_t)c->arg, out, (size_t)n); }
static bool cfg_adjust(const ui_ctx_t *c, int dir) {
    const cfg_effect_t fx = synth_config_adjust(&c->rack->cfg, (cfg_param_id_t)c->arg, dir);
    if (fx == CFG_REBUILD) c->ui->rack_dirty = true;
    if (c->arg == CFGP_TYPE) c->ui->menu_tab = tab_index_of(c->rack, TAB_GENERAL);   // the tab list depends on the synth type
    return fx == CFG_LIVE;
}

static const el_def_t elements[] = {
    {"Type",   EL_VALUE, 0, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL, cfg_label, CFGP_TYPE},
    {"Patch",  EL_VALUE, 1, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL, cfg_label, CFGP_PATCH},
    {"Voices", EL_VALUE, 2, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL, cfg_label, CFGP_VOICES},
    {"Vol",    EL_VALUE, 3, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL, cfg_label, CFGP_VOLUME},
    {"Out",    EL_VALUE, 4, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL, cfg_label, CFGP_OUTPUT},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)area; (void)rect;
    const gui_rect_t box = ui_picture_box(st);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    draw_synth_info(g, st, box, c->rack);
}

const screen_def_t scr_general_screen = {elements, N_ELEMENTS, false, layout, NULL, NULL, draw_extra};
