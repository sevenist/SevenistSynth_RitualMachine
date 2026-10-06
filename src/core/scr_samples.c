// The SAMPLES tab of the menu: the library browser. File picks a file of the card, Tgt the sampler it goes to (New = a new sampler at the end
// of the rack), Assign does it, Scan reads the card again. On the right: the overview of the highlighted file.
// A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include <stdio.h>

int sampler_count(const rack_t *r) { int n = 0; for (int i = 0; i < r->count; i++) n += r->slot[i].type == MOD_SAMPLER; return n; }
int sampler_slot(const rack_t *r, int k) {           // slot of the k-th sampler (k from 1)
    for (int i = 0; i < r->count; i++) if (r->slot[i].type == MOD_SAMPLER && --k == 0) return i;
    return RACK_NONE;
}

void file_label(int index, char *buf, int n) {
    audio_sample_info_t in;
    if (index >= 0 && audio_sample_info(index, &in)) snprintf(buf, (size_t)n, "%.8s%s", in.name, in.pending ? "*" : "");
    else snprintf(buf, (size_t)n, "--");
}

static void file_value(const ui_ctx_t *c, char *out, int n) {
    audio_sample_info_t in;
    file_label(audio_sample_info(c->ui->smp_cur, &in) ? c->ui->smp_cur : -1, out, n);
}
static bool file_adjust(const ui_ctx_t *c, int dir) {
    const int files = audio_sample_count();
    if (files > 0) c->ui->smp_cur = (c->ui->smp_cur + dir + files) % files;
    return false;
}

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
    if (audio_sample_count() <= 0 || !audio_sample_prepare(ui->smp_cur)) return;      // a .wav / .mp3 is converted now
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

static void scan_value(const ui_ctx_t *c, char *out, int n) { (void)c; snprintf(out, (size_t)n, "Scan (%d)", audio_sample_count()); }
static void scan_activate(const ui_ctx_t *c) { (void)c; audio_samples_rescan(); }

static const el_def_t elements[] = {
    {"File",   EL_VALUE,  0, 0, 1, file_value, file_adjust, NULL, NULL, NULL},
    {"Tgt",    EL_VALUE,  1, 0, 1, tgt_value,  tgt_adjust,  NULL, NULL, NULL},
    {"Assign", EL_BUTTON, 2, 0, 1, NULL,       NULL,        assign_activate, NULL, NULL},
    {"Scan",   EL_BUTTON, 3, 0, 1, scan_value, NULL,        scan_activate, NULL, NULL},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)rect;
    audio_sample_info_t in;
    const bool have = audio_sample_info(c->ui->smp_cur, &in);
    const int rh = gui_row_h(g, st);
    const gui_rect_t list = gui_rect(area.x, area.y, st->list_w, area.h);
    if (have && !in.pending) {                                // length, root note and slices of the highlighted file, under the list
        char buf[24], nm[8];
        snprintf(buf, sizeof buf, "%.1fs", (double)in.frames / (double)(in.rate ? in.rate : 1));
        gui_draw_text_left(g, st, gui_rect(list.x, gui_bottom(list) - 2 * rh, list.w, rh), buf);
        seq_note_name(in.root, nm, sizeof nm);
        snprintf(buf, sizeof buf, "root %s  %d sl", nm, in.slices);
        gui_draw_text_left(g, st, gui_rect(list.x, gui_bottom(list) - rh, list.w, rh), buf);
    }
    draw_sample_graph(g, ui_picture_box(st), have ? &in : NULL, -1, 0.0f, true);
}

// Keep the highlighted file inside the library (a rescan can shrink it).
static void after_edit(const ui_ctx_t *c) { if (c->ui->smp_cur >= audio_sample_count()) c->ui->smp_cur = 0; }

const screen_def_t scr_samples_screen = {elements, N_ELEMENTS, false, false, layout, NULL, after_edit, draw_extra};
