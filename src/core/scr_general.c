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

// Only the rows that apply are shown: Patch for the FM types; Voices for the polyphonic ones; Glide and Legato for the Mono ones.
static bool en_fm(const ui_ctx_t *c)     { return synth_type_is_fm(c->rack->cfg.type); }
static bool en_voices(const ui_ctx_t *c) { return !synth_type_is_mono(c->rack->cfg.type); }
static bool en_mono(const ui_ctx_t *c)   { return synth_type_is_mono(c->rack->cfg.type); }

static const el_def_t elements[] = {
    {"Type",   EL_VALUE, 0, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL,      cfg_label, CFGP_TYPE,   0},
    {"Patch",  EL_VALUE, 1, 0, 1, cfg_value, cfg_adjust, NULL, NULL, en_fm,     cfg_label, CFGP_PATCH,  EF_HIDE_WHEN_DISABLED},
    {"Voices", EL_VALUE, 2, 0, 1, cfg_value, cfg_adjust, NULL, NULL, en_voices, cfg_label, CFGP_VOICES, EF_HIDE_WHEN_DISABLED},
    {"Glide",  EL_VALUE, 3, 0, 1, cfg_value, cfg_adjust, NULL, NULL, en_mono,   cfg_label, CFGP_GLIDE,  EF_HIDE_WHEN_DISABLED},
    {"Legato", EL_VALUE, 4, 0, 1, cfg_value, cfg_adjust, NULL, NULL, en_mono,   cfg_label, CFGP_LEGATO, EF_HIDE_WHEN_DISABLED},
    {"Vol",    EL_VALUE, 5, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL,      cfg_label, CFGP_VOLUME, 0},
    {"Out",    EL_VALUE, 6, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL,      cfg_label, CFGP_OUTPUT, 0},
    {"Spk",    EL_VALUE, 7, 0, 1, cfg_value, cfg_adjust, NULL, NULL, NULL,      cfg_label, CFGP_SPEAKER, 0},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

// The visible rows follow each other without gaps (a hidden row takes no room).
static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    gui_rect_t slot[N_ELEMENTS];
    ui_layout_column(g, st, area, N_ELEMENTS, slot);
    int k = 0;
    for (int i = 0; i < N_ELEMENTS; i++) {
        const bool shown = !(elements[i].flags & EF_HIDE_WHEN_DISABLED) || !elements[i].enabled || elements[i].enabled(c);
        rect[i] = shown ? slot[k++] : slot[N_ELEMENTS - 1];             // a hidden row is not drawn: its rectangle does not matter
    }
}

static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)area; (void)rect;
    const gui_rect_t box = ui_picture_box(st);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    draw_synth_info(g, st, box, c->rack);
}

const screen_def_t scr_general_screen = {elements, N_ELEMENTS, true, layout, NULL, NULL, draw_extra};   // compact: up to 6 rows fit on a 64 px screen
