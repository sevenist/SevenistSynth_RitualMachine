// The SOUND tab of the menu (apps/ritual_machine/docs/DESIGN.md ADR-044: GENERAL, SAMPLES and ALGORITHM in one screen): synth type, FM patch, voices,
// master volume (output, speaker and knob mode: SETUP > MACHINE since ADR-046, scr_machine.c); then, Modular only, a sample for the rack (File opens the browser, scr_browse.c, Tgt is
// the sampler it goes to, New = a new sampler at the end of the rack, Assign does it); then, FM only, the DX7 patch's Algo and Fb and
// the Op row (left / right pick an operator in the tree, a push opens its page in the main view: OPn, then OPn ENV; ui_pages.c,
// fm_page_row). The picture on the right follows the part the focus is in: the info box, the file's overview, the operator tree.
// A declarative screen (see ui_screen.h): list on the left, picture on the right; the list scrolls.
#include "core/ui_screen.h"
#include <stdio.h>

/* ---------------- the synth's settings (GENERAL) ---------------- */

// arg = the setting (cfg_param_id_t). Structural changes (type, voices, the patch in FM mode) are applied when the menu closes; the volume is live.
static const char *cfg_label(const ui_ctx_t *c) { return synth_config_label((cfg_param_id_t)c->arg); }
static void cfg_value(const ui_ctx_t *c, char *out, int n) { synth_config_format(&c->rack->cfg, (cfg_param_id_t)c->arg, out, (size_t)n); }
static bool cfg_adjust(const ui_ctx_t *c, int dir) {
    const cfg_effect_t fx = synth_config_adjust(&c->rack->cfg, (cfg_param_id_t)c->arg, dir);
    if (fx == CFG_REBUILD) c->ui->rack_dirty = true;
    if (c->arg == CFGP_TYPE) c->ui->menu_tab = tab_index_of(c->rack, TAB_SOUND);     // the tab list depends on the synth type
    return fx == CFG_LIVE;
}

// Only the rows that apply are shown: Patch, Algo, Fb, Op for FM; Glide and Legato when it plays mono (Voices = 1, ADR-041); the
// sample rows for a Modular synth.
static bool en_fm(const ui_ctx_t *c)      { return synth_type_is_fm(c->rack->cfg.type); }
static bool en_mono(const ui_ctx_t *c)    { return synth_config_is_mono(&c->rack->cfg); }
static bool en_modular(const ui_ctx_t *c) { return !synth_type_is_fm(c->rack->cfg.type) && !synth_type_is_strings(c->rack->cfg.type); }

/* ---------------- a sample for the rack (SAMPLES) ---------------- */

static void file_value(const ui_ctx_t *c, char *out, int n) {
    audio_sample_info_t in;
    file_label(audio_sample_info(c->ui->smp_cur, &in) ? c->ui->smp_cur : -1, out, n);
}
static bool file_adjust(const ui_ctx_t *c, int dir) { (void)dir; c->ui->br_req = BROWSE_RACK; return false; }   // the app opens the browser

static void tgt_value(const ui_ctx_t *c, char *out, int n) {
    const int slot = c->ui->smp_tgt > 0 ? sampler_slot(c->rack, c->ui->smp_tgt) : RACK_NONE;
    if (slot == RACK_NONE) snprintf(out, (size_t)n, "New"); else rack_slot_name(c->rack, slot, out, n);
}
static bool tgt_adjust(const ui_ctx_t *c, int dir) {
    const int targets = sampler_count(c->rack) + 1;
    c->ui->smp_tgt = (c->ui->smp_tgt + dir + targets) % targets;
    return false;
}

static void assign_activate(const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    rack_t *rack = c->rack;
    audio_sample_info_t in;
    if (!audio_sample_info(ui->smp_cur, &in) || !in.name[0] || !audio_sample_prepare(ui->smp_cur)) return;   // a .wav / .mp3 is converted now
    int slot = ui->smp_tgt > 0 ? sampler_slot(rack, ui->smp_tgt) : RACK_NONE;
    if (slot == RACK_NONE) {                                  // a new sampler at the end of the rack
        if (!rack_insert(rack, rack->count, MOD_SAMPLER)) return;
        slot = rack->count - 1;
        ui->smp_tgt = sampler_count(rack);
    }
    rack->slot[slot].v[MP_SM_FILE] = (float)(ui->smp_cur + 1);
    rack->slot[slot].v[MP_SM_SLICE] = 0;
    ui->rack_dirty = true;                                    // applied when the menu closes, like every rack edit
}

/* ---------------- the DX7 patch (ALGORITHM): rack->cfg.fm, edits are live ---------------- */

static dx7_patch_t *patch(const ui_ctx_t *c) { return &c->rack->cfg.fm; }

static void algo_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%d", patch(c)->algorithm); }
static bool algo_adjust(const ui_ctx_t *c, int dir) { return dx7_algorithm_adjust(patch(c), dir); }
static void fb_value(const ui_ctx_t *c, char *out, int n) { dx7_feedback_format(patch(c), out, (size_t)n); }
static bool fb_adjust(const ui_ctx_t *c, int dir) { return dx7_feedback_adjust(patch(c), dir); }

static void op_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "OP%d >", c->ui->fm_op + 1); }
static bool op_adjust(const ui_ctx_t *c, int dir) { c->ui->fm_op = (c->ui->fm_op + dir + DX7_OPS) % DX7_OPS; return false; }
static void op_activate(const ui_ctx_t *c) { synth_ui_open_page(c->ui, c->rack, GLOBAL_PAGE, GP_FM_OP_BASE + c->ui->fm_op); }

enum { EL_SAMPLES = 6, EL_ALGO = 9 };                        // the first element of each part (the picture follows them)

static const el_def_t elements[] = {
    {"Type",   EL_VALUE,  0, 0, 1, cfg_value,  cfg_adjust,  NULL, NULL, NULL,       cfg_label, CFGP_TYPE,   0},
    {"Patch",  EL_VALUE,  1, 0, 1, cfg_value,  cfg_adjust,  NULL, NULL, en_fm,      cfg_label, CFGP_PATCH,  EF_HIDE_WHEN_DISABLED},
    {"Voices", EL_VALUE,  2, 0, 1, cfg_value,  cfg_adjust,  NULL, NULL, NULL,       cfg_label, CFGP_VOICES, 0},
    {"Glide",  EL_VALUE,  3, 0, 1, cfg_value,  cfg_adjust,  NULL, NULL, en_mono,    cfg_label, CFGP_GLIDE,  EF_HIDE_WHEN_DISABLED},
    {"Legato", EL_VALUE,  4, 0, 1, cfg_value,  cfg_adjust,  NULL, NULL, en_mono,    cfg_label, CFGP_LEGATO, EF_HIDE_WHEN_DISABLED},
    {"Vol",    EL_VALUE,  5, 0, 1, cfg_value,  cfg_adjust,  NULL, NULL, NULL,       cfg_label, CFGP_VOLUME, 0},
    {"File",   EL_VALUE,  6, 0, 1, file_value, file_adjust, NULL, NULL, en_modular, NULL, 0, EF_HIDE_WHEN_DISABLED},
    {"Tgt",    EL_VALUE,  7, 0, 1, tgt_value,  tgt_adjust,  NULL, NULL, en_modular, NULL, 0, EF_HIDE_WHEN_DISABLED},
    {"Assign", EL_BUTTON, 8, 0, 1, NULL,       NULL,        assign_activate, NULL, en_modular, NULL, 0, EF_HIDE_WHEN_DISABLED},
    {"Algo",   EL_VALUE,  9, 0, 1, algo_value, algo_adjust, NULL, NULL, en_fm,      NULL, 0, EF_HIDE_WHEN_DISABLED},
    {"Fb",     EL_VALUE, 10, 0, 1, fb_value,   fb_adjust,   NULL, NULL, en_fm,      NULL, 0, EF_HIDE_WHEN_DISABLED},
    {"Op",     EL_DIRECT,11, 0, 1, op_value,   op_adjust,   op_activate, NULL, en_fm, NULL, 0, EF_HIDE_WHEN_DISABLED},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

int scr_general_setting(int row) { return row >= 1 && row <= EL_SAMPLES ? elements[row - 1].arg : -1; }

// The visible rows follow each other without gaps (a hidden row takes no room).
static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    gui_rect_t slot[N_ELEMENTS];
    ui_layout_column(g, st, area, N_ELEMENTS, slot);
    const int stride = slot[1].y - slot[0].y;
    const int fit = stride > 0 ? (area.y + area.h - slot[0].y) / stride : N_ELEMENTS;
    int shown_n = 0, focus_k = 0;
    for (int i = 0; i < N_ELEMENTS; i++) {
        const bool shown = !(elements[i].flags & EF_HIDE_WHEN_DISABLED) || !elements[i].enabled || elements[i].enabled(c);
        if (!shown) continue;
        if (c->ui->row == i + 1) focus_k = shown_n;
        shown_n++;
    }
    const int first = shown_n > fit && focus_k >= fit ? focus_k - fit + 1 : 0;      // the list scrolls to keep the focused row on screen
    int k = 0;
    for (int i = 0; i < N_ELEMENTS; i++) {
        const bool shown = !(elements[i].flags & EF_HIDE_WHEN_DISABLED) || !elements[i].enabled || elements[i].enabled(c);
        if (!shown) { rect[i] = slot[N_ELEMENTS - 1]; rect[i].y = -100; continue; }   // a hidden row is not drawn: out of the way
        const int pos = k++ - first;
        rect[i] = slot[pos < 0 ? 0 : pos];
        if (pos < 0 || pos >= fit) rect[i].y = -100;                   // scrolled out: clipped away
    }
}

static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)area; (void)rect;
    const gui_rect_t box = ui_picture_box(st);
    const int el = c->ui->row - 1;
    if (el >= EL_ALGO) { draw_algo(g, box, patch(c), c->ui->fm_op); return; }
    if (el >= EL_SAMPLES) {
        audio_sample_info_t in;
        const bool have = audio_sample_info(c->ui->smp_cur, &in) && in.name[0];
        draw_sample_graph(g, box, have ? &in : NULL, -1, 0.0f, true);
        return;
    }
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    draw_synth_info(g, st, box, c->rack);                               // (the battery: the header, and the MACHINE tab's picture)
}

// Keep the file inside the catalog.
static void after_edit(const ui_ctx_t *c) { if (c->ui->smp_cur >= audio_sample_count()) c->ui->smp_cur = 0; }

const screen_def_t scr_sound_screen = {elements, N_ELEMENTS, true, false, layout, NULL, after_edit, draw_extra};   // compact: up to 6 rows fit on a 64 px screen
