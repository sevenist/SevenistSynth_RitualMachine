#pragma once
// PRIVATE to the UI implementation (ui_pages.c, ui_input.c, ui_graphs.c, ui_draw.c): what the four layers share.
// The public interface of the UI is core/synth_ui.h.
//
//   ui_pages.c   what is on screen: the page tables, the generated page list, the menu tabs
//   ui_input.c   what events do: row editing, menu tabs, knobs, macros, synth_ui_handle()
//   ui_graphs.c  the small pictures of the graph box (waveform, envelope, filter response, effect sketches...): functions of (display, box, values)
//   ui_draw.c    the screens: header, parameter lists, the hand-drawn screens, synth_ui_draw()
#include <stdbool.h>
#include <stdint.h>
#include "u8g2.h"
#include "core/synth_ui.h"
#include "core/gui.h"
#include "core/rack.h"
#include "core/seq.h"
#include "core/dx7.h"
#include "core/fxrack.h"
#include "core/synth_params.h"
#include "hal/hal_audio.h"
#include "hal/hal_display.h"

#define PI_F 3.14159265f

// Rack strip: the module sprite is MODULE_SPRITE_W wide (module_sprites.h, generated); RACK_PITCH = sprite + 4 px so the selection frame fits between cells.
// RACK_VIS cells are visible at once (the screen width minus room for the scroll arrows); the strip scrolls over the RACK_MAX + 1 cells (slots and OUT).
#define RACK_PITCH 29
#define RACK_VIS   ((DISPLAY_WIDTH - 8) / RACK_PITCH)

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

typedef enum { GRAPH_WAVE, GRAPH_ENV, GRAPH_FILTER, GRAPH_SAT, GRAPH_AMP_ENV, GRAPH_SEQ, GRAPH_SEQ_CFG, GRAPH_FM, GRAPH_MS_STEPS, GRAPH_MS_LANE, GRAPH_SAMPLE, GRAPH_EG, GRAPH_EG_REL, GRAPH_COMB,
               GRAPH_STR_OSC, GRAPH_STR_LP, GRAPH_STR_FILTER } graph_t;

// A resolved page: what to show for ui->page right now.
typedef struct {
    char    title[24];
    graph_t graph;
    int     count;          // rows
    int     params[6];      // module pages: index into slot.v[]; global: param_id_t / seq_param_id_t
    int     slot;           // rack slot of the module, or GLOBAL_PAGE
    int     def;
} page_t;

#define GLOBAL_PAGE 255

// Pseudo parameter indexes on a modulator's DEST page: the target module and its parameter live in the slot, not in slot.v[].
#define PRM_TGT  100
#define PRM_TPRM 101

typedef enum { TAB_RACK, TAB_GENERAL, TAB_FM_ALGO, TAB_FM_OP, TAB_FM_ENV, TAB_FX, TAB_SAMPLES, TAB_KEYS } tab_t;

/* ---- ui_pages.c ---- */
void get_page(const synth_ui_t *ui, const rack_t *rack, int idx, page_t *out);
int tab_count(const rack_t *r);
tab_t tab_kind(const rack_t *r, int idx);
const char *tab_name(tab_t t);
int tab_rows(tab_t t);
int tab_index_of(const rack_t *r, tab_t t);

/* ---- ui_input.c ---- */
void macros_default(synth_ui_t *ui, const rack_t *rack);
int sampler_count(const rack_t *r);                 // samplers in the rack
int sampler_slot(const rack_t *r, int k);           // slot of the k-th sampler (k from 1)

/* ---- ui_graphs.c: pictures for the graph box ---- */
void draw_wave(u8g2_t *g, gui_rect_t box, int wave, float pulse_width);
void draw_engine_preview(u8g2_t *g, gui_rect_t box, int engine, float timbre, float morph);
float curve_shape(float pct, float p);
void draw_curve_seg(u8g2_t *g, int x0, int y0, int x1, int y1, float pct);
void draw_env(u8g2_t *g, gui_rect_t box, const env_params_t *e);
float filter_gain(int type, float f, float fc, float q);
void draw_filter(u8g2_t *g, gui_rect_t box, int type, float cutoff_hz, float resonance);
void draw_sat(u8g2_t *g, gui_rect_t box, int mode, float drive, float mix);
float ms_value_at(const ms_lane_t *l, int steps, float pos);
void draw_ms_curve(u8g2_t *g, gui_rect_t box, const ms_lane_t *l, int steps);
void draw_comb(u8g2_t *g, gui_rect_t box, float fb_pct, float mix);
void draw_eg_graph(u8g2_t *g, gui_rect_t box, const rack_slot_t *s, int sel);
void draw_sample_graph(u8g2_t *g, gui_rect_t box, const audio_sample_info_t *in, int sel_slice, float start_ms, bool show_loop);
void draw_chorus_graph(u8g2_t *g, gui_rect_t box, int mode, int mix);
void draw_delay_graph(u8g2_t *g, gui_rect_t box, int mix, int ms, int fb);
void draw_reverb_graph(u8g2_t *g, gui_rect_t box, int mix, int decay, int size, int damp);
void draw_comp_graph(u8g2_t *g, gui_rect_t box, int thr, int ratio, int gain);
void draw_eq_graph(u8g2_t *g, gui_rect_t box, int low, int mid, int midf, int high);
void draw_fx_chain(u8g2_t *g, gui_rect_t box, const fxrack_t *fr, int sel);
void draw_fx_picture(u8g2_t *g, gui_rect_t box, const fxrack_t *fr, int sel);
void draw_algo(u8g2_t *g, gui_rect_t box, const dx7_patch_t *p, int sel_op);
void draw_eg_editor(u8g2_t *g, gui_rect_t box, const dx7_op_t *o, int sel);

/* ---- ui_draw.c ---- */
void draw_synth_info(u8g2_t *g, const gui_style_t *st, gui_rect_t box, const rack_t *rack);      // the info box of the GENERAL tab and of the FM page

/* ---- scr_samples.c ---- */
void file_label(int index, char *buf, int n);        // a sample file name for a list row ("kick", "pad_c4*" while pending), "--" for none
