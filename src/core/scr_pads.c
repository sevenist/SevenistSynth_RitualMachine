// The Pads section's screens (ADR-041 decision 25): the grid of the bank shown, full screen, and the selected pad's editor (a declarative
// screen, see ui_screen.h). The grid: 4 x 4 cells (pad number, mode / loop, the start of its sample's name, mute and choke marks); a pad
// that sounds is drawn inverted, the selected one has a double frame. The editor: the sample's overview with the trim across the top, then
// the settings in two columns. The app (app.c) owns the pads, plays them and sends every change to the engine.
#include "core/ui_screen.h"
#include "core/fine_step.h"
#include <stdio.h>
#include <string.h>

/* ---------------- the sample library, by name ---------------- */

static int file_index(const char *name) {                // catalog index of a pad's sample, -1 = none / not on this card
    if (!name[0]) return -1;
    const int n = audio_sample_count();
    audio_sample_info_t in;
    for (int i = 0; i < n; i++) if (audio_sample_info(i, &in) && !strcmp(in.name, name)) return i;
    return -1;
}

static bool file_info(const pad_t *p, audio_sample_info_t *in) {
    const int i = file_index(p->file);
    return i >= 0 && audio_sample_info(i, in);
}

static bool file_has_loop(const audio_sample_info_t *in) { return in->loop_mode != 0 && in->loop_end > in->loop_start + 1; }

/* ---------------- the editor ---------------- */

static pad_t *cur_pad(const ui_ctx_t *c) { return &c->ui->pads->pad[pads_ui_pad(c->ui)]; }

static void pad_value(const ui_ctx_t *c, char *out, int n) {
    const pad_t *p = cur_pad(c);
    audio_sample_info_t in;
    const bool have = file_info(p, &in);
    pad_param_format(p, (pad_param_t)c->arg, have ? in.frames : 0, have ? in.rate : 0, out, n);
    if (c->arg == PADP_FILE && p->file[0] && !have) snprintf(out, (size_t)n, "%s?", p->file);   // not on this card
}

static bool file_has_loop(const audio_sample_info_t *in);

// A trim point moved one step (dir +1 / -1) or set by a knob (x 0..1, dir 0), through the pad's Snap (decision 40). Beat and 1/16: the
// next grid point; Attack: the next attack of the onset scan (none while it scans: a plain step); Zero: a plain step, then the zero
// crossing nearest to it in that direction (only where the sample is in memory: elsewhere the plain step). True when it changed.
bool pads_ui_trim(synth_ui_t *ui, int pad, int param, int dir, float x) {
    const pad_param_t k = (pad_param_t)param;
    pad_t *p = &ui->pads->pad[pad];
    audio_sample_info_t in;
    const int fi = file_index(p->file);
    if (fi < 0 || !audio_sample_info(fi, &in) || in.pending || in.frames < 4) return false;
    const pad_t old = *p;
    const uint32_t cur = k == PADP_START ? p->start : k == PADP_END ? pad_end(p, in.frames) : pad_lstart(p, in.frames);
    if (dir) pad_param_adjust(p, k, dir, in.frames);                      // the plain step (fine steps included) ...
    else pad_param_set_norm(p, k, x, in.frames);                          // ... or the knob's place
    const uint32_t raw = k == PADP_START ? p->start : k == PADP_END ? pad_end(p, in.frames) : pad_lstart(p, in.frames);
    int64_t to = -1;
    switch (p->snap) {
        case PAD_SNAP_BEAT: case PAD_SNAP_16TH:
            to = pad_snap_grid(p, k, dir ? cur : raw, dir, in.frames, in.rate, ui->clock_bpm);
            break;
        case PAD_SNAP_ATTACK: {
            uint32_t on[64];
            uint16_t str[64];
            const int n = audio_sample_onsets(fi, on, str, 64);
            for (int i = 0; i < n; i++) {
                const int64_t f = on[i];
                if (dir > 0 && f > cur && (to < 0 || f < to)) to = f;
                if (dir < 0 && f < cur && (to < 0 || f > to)) to = f;
                if (!dir && (to < 0 || (f > raw ? f - raw : raw - f) < (to > raw ? to - raw : raw - to))) to = f;
            }
        } break;
        case PAD_SNAP_ZERO: to = audio_sample_zero(fi, raw, dir); break;
        default: break;
    }
    if (to >= 0) pad_set_trim(p, k, to, in.frames);
    return memcmp(&old, p, sizeof old) != 0;
}

static bool pad_adjust(const ui_ctx_t *c, int dir) {
    pad_t *p = cur_pad(c);
    audio_sample_info_t in;
    const bool have = file_info(p, &in);
    const uint32_t frames = have && !in.pending ? in.frames : 0;
    if (c->arg == PADP_START || c->arg == PADP_END || c->arg == PADP_LSTART) return pads_ui_trim(c->ui, pads_ui_pad(c->ui), (pad_param_t)c->arg, dir, 0.0f);
    if (c->arg == PADP_LOOP) {                           // Off / Trim / File; File only when the file has loop points
        const bool file_loop = have && file_has_loop(&in);
        const uint8_t was = p->loop;
        pad_param_adjust(p, PADP_LOOP, dir, frames);
        if (p->loop == PAD_LOOP_FILE && !file_loop) pad_param_adjust(p, PADP_LOOP, dir, frames);
        return p->loop != was;
    }
    if (c->arg != PADP_FILE) return pad_param_adjust(p, (pad_param_t)c->arg, dir, frames);
    const int n = audio_sample_count();                  // the library in its order, "--" (none) before the first file; versions (name~n)
    int i = file_index(p->file);                         // and deleted entries are skipped (the EDIT page reaches the versions)
    for (int k = 0; k <= n; k++) {
        i += dir > 0 ? 1 : -1;
        if (i < -1) i = n - 1;
        if (i >= n) i = -1;
        if (i < 0 || (audio_sample_info(i, &in) && in.name[0] && !strchr(in.name, '~'))) break;
    }
    if (i >= 0 && audio_sample_info(i, &in)) snprintf(p->file, PAD_NAME_MAX, "%s", in.name);
    else p->file[0] = 0;
    // a file with its own loop points loads them (user, 2026-10-10); without them File is not offered
    const bool file_loop = p->file[0] && file_has_loop(&in);
    if (file_loop) p->loop = PAD_LOOP_FILE;
    else if (p->loop == PAD_LOOP_FILE) p->loop = PAD_LOOP_OFF;
    return true;
}

static const char *pad_label_dyn(const ui_ctx_t *c) { return pad_param_label((pad_param_t)c->arg); }

static void chop_refresh(synth_ui_t *ui);

// The loop rows only while the pad loops (LStr for the Trim loop: a file loop has its own points).
static bool en_loop(const ui_ctx_t *c) { return cur_pad(c)->loop != PAD_LOOP_OFF; }
static bool en_trim_loop(const ui_ctx_t *c) { return cur_pad(c)->loop == PAD_LOOP_TRIM; }
static bool en_xfade(const ui_ctx_t *c) { return cur_pad(c)->loop != PAD_LOOP_OFF && cur_pad(c)->xfade_ms > 0; }
static bool en_from(const ui_ctx_t *c) { return cur_pad(c)->loop != PAD_LOOP_OFF && (cur_pad(c)->grid != PAD_GRID_OFF || cur_pad(c)->sync); }

#define ROW(r, col, span, k, en) {"", EL_VALUE, r, col, span, pad_value, pad_adjust, NULL, NULL, en, pad_label_dyn, k, 0}
#define ROWH(r, col, span, k, en) {"", EL_VALUE, r, col, span, pad_value, pad_adjust, NULL, NULL, en, pad_label_dyn, k, EF_HIDE_WHEN_DISABLED}
// PLAY: the sample, how it plays, where it starts and loops, when it starts (decision 42: the editor's pages PLAY / TONE / CHOP)
static const el_def_t elements[] = {
    ROW(0, 0, 2, PADP_FILE, NULL),
    ROW(1, 0, 1, PADP_MODE, NULL), ROW(2, 0, 1, PADP_START, NULL), ROW(3, 0, 1, PADP_END, NULL), ROW(4, 0, 1, PADP_LOOP, NULL),
    ROWH(5, 0, 1, PADP_LSTART, en_trim_loop), ROWH(6, 0, 1, PADP_XFADE, en_loop), ROWH(7, 0, 1, PADP_XPOS, en_xfade),
    ROW(8, 0, 1, PADP_TAIL, NULL),
    ROW(1, 1, 1, PADP_GRID, NULL), ROWH(2, 1, 1, PADP_SYNC, en_loop), ROWH(3, 1, 1, PADP_FROM, en_from), ROW(4, 1, 1, PADP_SNAP, NULL),
    ROW(5, 1, 1, PADP_NEXT, NULL), ROW(6, 1, 1, PADP_CHOKE, NULL), ROW(7, 1, 1, PADP_MUTE, NULL),
};
// TONE: level, pan, pitch, the filter (its response drawn below)
static const el_def_t tone_elements[] = {
    ROW(0, 0, 1, PADP_LEVEL, NULL), ROW(1, 0, 1, PADP_PAN, NULL), ROW(2, 0, 1, PADP_TUNE, NULL), ROW(3, 0, 1, PADP_FINE, NULL),
    ROW(0, 1, 1, PADP_CUT, NULL), ROW(1, 1, 1, PADP_RES, NULL),
};
#undef ROW
#undef ROWH
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))
#define N_TONE ((int)(sizeof tone_elements / sizeof tone_elements[0]))
#define WAVE_H 24                                        // the sample overview across the top (3 grid rows)
#define ROW_H  GUI_GRID                                  // 8 px rows, as on the module pages

// The overview on top, the sample row across the width, then two columns of 8 px rows.
static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)g; (void)st; (void)c;
    const int y0 = area.y + WAVE_H + 2, half = area.w / 2;
    for (int i = 0; i < N_ELEMENTS; i++) {
        const el_def_t *e = &elements[i];
        rect[i] = gui_rect(area.x + e->col * half, y0 + e->row * ROW_H, e->span * half - 1, ROW_H);
    }
}

static void tone_layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)g; (void)st; (void)c;
    const int half = area.w / 2;
    for (int i = 0; i < N_TONE; i++) {
        const el_def_t *e = &tone_elements[i];
        rect[i] = gui_rect(area.x + e->col * half, area.y + 2 + e->row * (ROW_H + 2), half - 1, ROW_H + 1);
    }
}

// The filter's response under the rows ("open" when Cut is off).
static void tone_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)st; (void)rect;
    const gui_rect_t box = gui_rect(area.x, area.y + 4 * (ROW_H + 2) + 6, area.w, area.h - 4 * (ROW_H + 2) - 7);
    const pad_t *p = cur_pad(c);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const float hz = pad_cut_hz(p);
    if (hz <= 0.0f) gui_draw_text_centered(g, box, "filter open");
    else draw_filter(g, box, FILT_LP, hz, p->res / 100.0f);
}

static int x_of(gui_rect_t box, uint64_t frame, uint32_t frames) { return box.x + (int)(frame * (uint64_t)(box.w - 1) / frames); }

// The sample's overview (its 64 peaks), dotted outside the trim, Start and End as lines, the tail as a fade after End, the loop as a
// bracket along the top (from the Trim loop's start or the file's loop points) with the crossfade as a filled bar under its end.
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)st; (void)rect;
    const gui_rect_t box = gui_rect(area.x, area.y + 1, area.w, WAVE_H);
    const pad_t *p = cur_pad(c);
    audio_sample_info_t in;
    if (!file_info(p, &in) || in.pending || in.frames < 4) {
        u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
        gui_draw_text_centered(g, box, !p->file[0] ? "empty pad" : in.pending && file_index(p->file) >= 0 ? "not converted yet" : "not on this card");
        return;
    }
    const int mid = box.y + box.h / 2, amp = box.h / 2 - 1;
    const uint32_t end = pad_end(p, in.frames);
    const int xs = x_of(box, p->start, in.frames), xe = x_of(box, end, in.frames);
    for (int x = 0; x < box.w; x++) {
        const int px = box.x + x;
        const bool inside = px >= xs && px <= xe;
        if (!inside && (x & 1)) continue;
        const int h = in.peaks[x * AUDIO_PEAKS / box.w] * amp / 255;
        u8g2_DrawVLine(g, px, mid - h, 2 * h + 1);
    }
    u8g2_DrawVLine(g, xs, box.y, box.h);
    u8g2_DrawVLine(g, xe, box.y, box.h);
    if (p->tail_ms && in.rate) {                         // the tail: a fade from the End point over its length (the real one starts at the stop)
        const uint64_t tf = (uint64_t)p->tail_ms * in.rate / 1000;
        int tw = (int)(tf * (uint64_t)(box.w - 1) / in.frames);
        if (tw < 2) tw = 2;
        const int x1 = xe + tw > box.x + box.w - 1 ? box.x + box.w - 1 : xe + tw;
        const int y1 = mid - (int)((int64_t)amp * (xe + tw - x1) / tw);   // cut at the box edge: where the ramp is there
        u8g2_DrawLine(g, xe, box.y, x1, y1);
        u8g2_DrawLine(g, xe, box.y + box.h - 1, x1, 2 * mid - y1);
    }
    uint32_t ls = 0, le = 0;
    if (p->loop == PAD_LOOP_TRIM) { ls = pad_lstart(p, in.frames); le = end; }
    if (p->loop == PAD_LOOP_FILE && file_has_loop(&in)) { ls = in.loop_start; le = in.loop_end; }
    if (le > ls + 1) {
        const int x0 = x_of(box, ls, in.frames), x1 = x_of(box, le, in.frames);
        u8g2_DrawHLine(g, x0, box.y, x1 - x0 + 1);
        u8g2_DrawVLine(g, x0, box.y, 4);
        u8g2_DrawVLine(g, x1, box.y, 4);
        if (p->xfade_ms && in.rate) {                    // the crossfade: under the loop's end; Post also marks the loop's first part
            uint64_t xf = (uint64_t)p->xfade_ms * in.rate / 1000;
            if (xf > (le - ls) / 2) xf = (le - ls) / 2;
            const int w = (int)(xf * (uint64_t)(box.w - 1) / in.frames) + 1;
            u8g2_DrawBox(g, x1 - w + 1, box.y + 1, w, 2);
            if (p->xpost) u8g2_DrawBox(g, x0, box.y + 1, w, 2);
            else          u8g2_DrawBox(g, x0 - w + 1 < box.x ? box.x : x0 - w + 1, box.y + 1, w, 2);
        }
    }
}

static const screen_def_t pads_screen = {elements, N_ELEMENTS, false, true, layout, NULL, NULL, draw_extra};
static const screen_def_t tone_screen = {tone_elements, N_TONE, false, true, tone_layout, NULL, NULL, tone_extra};

/* ---------------- CHOP (ADR-041 decisions 34-35) ---------------- */

static pad_chop_t *chop(const ui_ctx_t *c) { return &c->ui->chop; }

// The cuts again from the pad's trim and the file (Manual keeps its own); a transient chop asks the file's scan (pending until it is done).
static void chop_refresh(synth_ui_t *ui) {
    const int pad = pads_ui_pad(ui);
    if (ui->chop_pad != pad) { pad_chop_init(&ui->chop, pad); ui->chop_pad = pad; }
    pad_chop_t *c = &ui->chop;
    const pad_t *p = &ui->pads->pad[pad];
    audio_sample_info_t in;
    const int fi = file_index(p->file);
    if (fi < 0 || !audio_sample_info(fi, &in) || in.pending || in.frames < 4) { if (c->mode != CHOP_MANUAL) c->n_cuts = 0; c->pending = false; return; }
    uint32_t on[64];
    uint16_t str[64];
    const int n_on = c->mode == CHOP_TRANSIENT ? audio_sample_onsets(fi, on, str, 64) : 0;
    pad_chop_compute(c, p->start, pad_end(p, in.frames), in.rate, ui->clock_bpm, on, str, n_on);
}

static bool chop_frames(const ui_ctx_t *c, audio_sample_info_t *in) {
    const pad_t *p = cur_pad(c);
    return file_info(p, in) && !in->pending && in->frames >= 4;
}

static void ch_mode_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", pad_chop_mode_name(chop(c)->mode)); }
static bool ch_mode_adjust(const ui_ctx_t *c, int dir) {
    pad_chop_t *ch = chop(c);
    ch->mode = (uint8_t)((ch->mode + CHOP_MODE_COUNT + (dir > 0 ? 1 : -1)) % CHOP_MODE_COUNT);
    chop_refresh(c->ui);
    return false;
}

static const char *ch_amt_label(const ui_ctx_t *c) {
    static const char *const l[CHOP_MODE_COUNT] = {"N", "Sens", "Div", "Cut"};
    return l[chop(c)->mode % CHOP_MODE_COUNT];
}
static void ch_amt_value(const ui_ctx_t *c, char *out, int n) {
    const pad_chop_t *ch = chop(c);
    switch (ch->mode) {
        case CHOP_EQUAL:     snprintf(out, (size_t)n, "%d", ch->n); break;
        case CHOP_TRANSIENT: snprintf(out, (size_t)n, ch->pending ? "scan.." : "%d%%", ch->sens); break;
        case CHOP_GRID:      snprintf(out, (size_t)n, "%s", pad_chop_div_name(ch->div)); break;
        default:             if (ch->n_cuts) snprintf(out, (size_t)n, "%d/%d", ch->sel + 1, ch->n_cuts); else snprintf(out, (size_t)n, "--"); break;
    }
}
static bool ch_amt_adjust(const ui_ctx_t *c, int dir) {
    pad_chop_t *ch = chop(c);
    const int d = dir > 0 ? 1 : -1;
    switch (ch->mode) {
        case CHOP_EQUAL:     ch->n = (uint8_t)(ch->n + d < 2 ? 2 : ch->n + d > PAD_CHOP_MAX ? PAD_CHOP_MAX : ch->n + d); break;
        case CHOP_TRANSIENT: ch->sens = (uint8_t)(ch->sens + 5 * d < 0 ? 0 : ch->sens + 5 * d > 100 ? 100 : ch->sens + 5 * d); break;
        case CHOP_GRID:      ch->div = (uint8_t)(ch->div + d < 0 ? 0 : ch->div + d >= CHOP_DIVS ? CHOP_DIVS - 1 : ch->div + d); break;
        default:             if (ch->n_cuts) ch->sel = (uint8_t)((ch->sel + ch->n_cuts + d) % ch->n_cuts); break;
    }
    chop_refresh(c->ui);
    return false;
}

// Pos / Add / Del edit the cuts by hand: an automatic chop becomes Manual with what it found.
static void ch_pos_value(const ui_ctx_t *c, char *out, int n) {
    const pad_chop_t *ch = chop(c);
    audio_sample_info_t in;
    if (!ch->n_cuts || !chop_frames(c, &in)) { snprintf(out, (size_t)n, "--"); return; }
    const double s = (double)ch->cut[ch->sel < ch->n_cuts ? ch->sel : 0] / in.rate;
    if (s < 1.0) snprintf(out, (size_t)n, "%dms", (int)(s * 1000.0 + 0.5)); else snprintf(out, (size_t)n, "%.3fs", s);
}
static bool ch_pos_adjust(const ui_ctx_t *c, int dir) {
    pad_chop_t *ch = chop(c);
    audio_sample_info_t in;
    if (!ch->n_cuts || !chop_frames(c, &in)) return false;
    ch->mode = CHOP_MANUAL;
    const pad_t *p = cur_pad(c);
    const int64_t step = g_fine_step ? (in.frames / 2000 ? in.frames / 2000 : 1) : (in.frames / 400 ? in.frames / 400 : 1);
    pad_chop_move(ch, (dir > 0 ? 1 : -1) * step, p->start, pad_end(p, in.frames));
    return false;
}
static bool en_cut(const ui_ctx_t *c) { return chop(c)->n_cuts > 0; }

static void ch_to_value(const ui_ctx_t *c, char *out, int n) {
    const pad_chop_t *ch = chop(c);
    char a[8], b[8];
    int last = ch->to + ch->n_cuts;
    if (last >= PAD_COUNT) last = PAD_COUNT - 1;
    pad_label(ch->to, a, sizeof a);
    pad_label(last, b, sizeof b);
    snprintf(out, (size_t)n, "%s-%s", a, b);
}
static bool ch_to_adjust(const ui_ctx_t *c, int dir) {
    pad_chop_t *ch = chop(c);
    const int t = ch->to + (dir > 0 ? 1 : -1);
    ch->to = (uint8_t)(t < 0 ? 0 : t >= PAD_COUNT ? PAD_COUNT - 1 : t);
    return false;
}

static void ch_add(const ui_ctx_t *c) {
    pad_chop_t *ch = chop(c);
    audio_sample_info_t in;
    if (!chop_frames(c, &in)) return;
    const pad_t *p = cur_pad(c);
    const uint32_t start = p->start, end = pad_end(p, in.frames);
    const int32_t at_play = audio_pad_position(pads_ui_pad(c->ui));                 // while it sounds: where it is (a cut dropped on the beat)
    uint32_t at;
    if (at_play >= 0) at = (uint32_t)at_play;
    else {                                                                          // else the middle of the selected cut's slice
        const int i = ch->n_cuts ? (ch->sel < ch->n_cuts ? ch->sel : ch->n_cuts - 1) : -1;
        const uint32_t lo = i >= 0 ? ch->cut[i] : start, hi = i >= 0 && i + 1 < ch->n_cuts ? ch->cut[i + 1] : end;
        at = lo + (hi - lo) / 2;
    }
    ch->mode = CHOP_MANUAL;
    pad_chop_add(ch, at, start, end);
}
static void ch_del(const ui_ctx_t *c) { chop(c)->mode = CHOP_MANUAL; pad_chop_del(chop(c)); }
static void ch_chop(const ui_ctx_t *c) { if (chop_frames(c, &(audio_sample_info_t){0})) c->ui->pad_chop_req = true; }

static const el_def_t chop_elements[] = {
    {"Mode", EL_VALUE,  0, 0, 1, ch_mode_value, ch_mode_adjust, NULL, NULL, NULL, NULL, 0, 0},
    {"",     EL_VALUE,  0, 1, 1, ch_amt_value, ch_amt_adjust, NULL, NULL, NULL, ch_amt_label, 0, 0},
    {"Pos",  EL_VALUE,  1, 0, 1, ch_pos_value, ch_pos_adjust, NULL, NULL, en_cut, NULL, 0, 0},
    {"To",   EL_VALUE,  1, 1, 1, ch_to_value, ch_to_adjust, NULL, NULL, NULL, NULL, 0, 0},
    {"Add",  EL_BUTTON, 2, 0, 1, NULL, NULL, ch_add, NULL, NULL, NULL, 0, 0},
    {"Del",  EL_BUTTON, 2, 1, 1, NULL, NULL, ch_del, NULL, en_cut, NULL, 0, 0},
    {"Chop to pads", EL_BUTTON, 3, 0, 2, NULL, NULL, ch_chop, NULL, NULL, NULL, 0, 0},
};
#define N_CHOP ((int)(sizeof chop_elements / sizeof chop_elements[0]))
#define CHOP_WAVE_H 40

static void chop_layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)g; (void)st; (void)c;
    const int y0 = area.y + CHOP_WAVE_H + 4, half = area.w / 2, rh = 12;
    for (int i = 0; i < N_CHOP; i++) {
        const el_def_t *e = &chop_elements[i];
        rect[i] = gui_rect(area.x + e->col * half, y0 + e->row * rh, e->span * half - 1, rh - 1);
    }
}

// The overview with the trim and the cuts (the selected one solid, the others dotted), and how many slices.
static void chop_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)st; (void)rect;
    const gui_rect_t box = gui_rect(area.x, area.y + 1, area.w, CHOP_WAVE_H);
    const pad_t *p = cur_pad(c);
    const pad_chop_t *ch = chop(c);
    audio_sample_info_t in;
    if (!chop_frames(c, &in)) {
        u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
        gui_draw_text_centered(g, box, !p->file[0] ? "empty pad" : "no sample");
        return;
    }
    const int mid = box.y + box.h / 2, amp = box.h / 2 - 1;
    const uint32_t end = pad_end(p, in.frames);
    const int xs = x_of(box, p->start, in.frames), xe = x_of(box, end, in.frames);
    for (int x = 0; x < box.w; x++) {
        const int px = box.x + x;
        if ((px < xs || px > xe) && (x & 1)) continue;
        const int h = in.peaks[x * AUDIO_PEAKS / box.w] * amp / 255;
        u8g2_DrawVLine(g, px, mid - h, 2 * h + 1);
    }
    u8g2_DrawVLine(g, xs, box.y, box.h);
    u8g2_DrawVLine(g, xe, box.y, box.h);
    for (int i = 0; i < ch->n_cuts; i++) {
        const int x = x_of(box, ch->cut[i], in.frames);
        if (i == ch->sel && ch->mode == CHOP_MANUAL) { u8g2_SetDrawColor(g, 2); u8g2_DrawVLine(g, x, box.y, box.h); u8g2_SetDrawColor(g, 1); }
        else for (int y = box.y; y < box.y + box.h; y += 2) { u8g2_SetDrawColor(g, 2); u8g2_DrawPixel(g, x, y); u8g2_SetDrawColor(g, 1); }
    }
    char t[16];
    snprintf(t, sizeof t, "%d slices", ch->n_cuts + 1);
    u8g2_DrawStr(g, box.x + 1, box.y + box.h + 1 + 7, ch->pending ? "scanning..." : t);
}

static const screen_def_t chop_screen = {chop_elements, N_CHOP, false, true, chop_layout, NULL, NULL, chop_extra};

/* ---------------- EDIT (sample tools: processing into a new version, the version history; ADR-041 decisions 36-38) ---------------- */

static const char *const op_names[PAD_EDIT_OPS] = {"Normalize", "Gain", "Fade in", "Fade out", "Trim sil.", "Crop", "Reverse", "Remove DC",
                                                   "Stretch", "Pitch", "Speed", "Fit beats", "Fit speed", "Paulstr.",
                                                   "Lo-fi rate", "Bits", "Drive", "Low-pass", "High-pass"};
static const char *const op_labels[PAD_EDIT_OPS] = {"Peak", "dB", "Len", "Len", "Thr", "", "", "", "Len", "Tune", "Spd", "Beat", "Beat", "Len",
                                                    "Rate", "Bits", "dB", "Cut", "Cut"};
static const int32_t op_default[PAD_EDIT_OPS] = {-3, 60, 100, 100, -40, 0, 0, 0, 100, 0, 100, 4, 4, 80, 12000, 8, 120, 4000, 200};

const char *pads_ui_op_name(int op) { return op >= 0 && op < PAD_EDIT_OPS ? op_names[op] : "?"; }

void pads_ui_op_amount(int op, int32_t a, char *out, int n) {
    const int m = (int)(a < 0 ? -a : a);
    switch (op) {
        case EDIT_NORMALIZE: case EDIT_GAIN: case EDIT_DRIVE: snprintf(out, (size_t)n, "%s%d.%ddB", a < 0 ? "-" : "+", m / 10, m % 10); break;
        case EDIT_FADE_IN: case EDIT_FADE_OUT: snprintf(out, (size_t)n, "%dms", (int)a); break;
        case EDIT_TRIM: snprintf(out, (size_t)n, "%ddB", (int)a); break;
        case EDIT_STRETCH: case EDIT_SPEED: snprintf(out, (size_t)n, "%d%%", (int)a); break;
        case EDIT_PITCH: snprintf(out, (size_t)n, "%s%d.%02dst", a < 0 ? "-" : "+", m / 100, m % 100); break;
        case EDIT_FIT: case EDIT_FIT_SPEED: snprintf(out, (size_t)n, "%db", (int)a); break;
        case EDIT_PAUL: snprintf(out, (size_t)n, "x%d.%d", m / 10, m % 10); break;
        case EDIT_RATE: case EDIT_LOWPASS: case EDIT_HIGHPASS: snprintf(out, (size_t)n, "%dHz", (int)a); break;
        case EDIT_BITS: snprintf(out, (size_t)n, "%d", (int)a); break;
        default: out[0] = 0; break;
    }
}

void pads_ui_init(synth_ui_t *ui) {
    ui->chop_pad = -1;
    ui->render_pct = -1;
    for (int i = 0; i < PAD_EDIT_OPS; i++) ui->edit_amt[i] = op_default[i];
}

static void ed_op_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", pads_ui_op_name(c->ui->edit_op)); }
static bool ed_op_adjust(const ui_ctx_t *c, int dir) { c->ui->edit_op = (uint8_t)((c->ui->edit_op + PAD_EDIT_OPS + (dir > 0 ? 1 : -1)) % PAD_EDIT_OPS); return false; }
static const char *ed_amt_label(const ui_ctx_t *c) { return op_labels[c->ui->edit_op % PAD_EDIT_OPS]; }
static bool en_amt(const ui_ctx_t *c) { return c->ui->edit_op != EDIT_CROP && c->ui->edit_op != EDIT_REVERSE && c->ui->edit_op != EDIT_DC; }
static void ed_amt_value(const ui_ctx_t *c, char *out, int n) {
    const int op = c->ui->edit_op % PAD_EDIT_OPS;
    const int32_t a = c->ui->edit_amt[op];
    pads_ui_op_amount(op, a, out, n);
    audio_sample_info_t in;
    if ((op == EDIT_FIT || op == EDIT_FIT_SPEED) && file_info(cur_pad(c), &in) && !in.pending) {   // and what it does to the length
        const uint32_t ppm = pad_fit_ppm(pad_fit_region(cur_pad(c), in.frames, in.loop_start, in.loop_end), in.rate, (int)a, c->ui->clock_bpm);
        if (ppm < PROC_PPM_MIN || ppm > PROC_PPM_MAX) snprintf(out, (size_t)n, "%db too far", (int)a);
        else snprintf(out, (size_t)n, "%db x%u.%02u", (int)a, (unsigned)(ppm / 1000000), (unsigned)(ppm / 10000 % 100));
    }
}
static bool ed_amt_adjust(const ui_ctx_t *c, int dir) {
    const int op = c->ui->edit_op % PAD_EDIT_OPS, d = dir > 0 ? 1 : -1;
    const bool fine = g_fine_step;
    int32_t a = c->ui->edit_amt[op];
    switch (op) {
        case EDIT_NORMALIZE: a = clampi(a + d * (fine ? 1 : 5), -120, 0); break;
        case EDIT_GAIN: a = clampi(a + d * (fine ? 1 : 10), -240, 240); break;
        case EDIT_FADE_IN: case EDIT_FADE_OUT: a = clampi(a + d * (fine ? 1 : a < 200 || (d < 0 && a <= 200) ? 10 : 50), 0, 5000); break;
        case EDIT_TRIM: a = clampi(a + d, -90, -6); break;
        case EDIT_STRETCH: case EDIT_SPEED: a = clampi(a + d * (fine ? 1 : 5), 25, 400); break;
        case EDIT_PITCH: a = clampi(a + d * (fine ? 1 : 100), -2400, 2400); break;       // a semitone, fine: a cent
        case EDIT_FIT: case EDIT_FIT_SPEED: a = clampi(a + d, 1, 128); break;
        case EDIT_PAUL: a = clampi(a + d * (fine ? 1 : a < 40 || (d < 0 && a <= 40) ? 5 : 10), 15, 640); break;   // x1.5 .. x64
        case EDIT_RATE: a = clampi(a + d * (fine ? 100 : 1000), 1000, 32000); break;
        case EDIT_BITS: a = clampi(a + d, 2, 15); break;
        case EDIT_DRIVE: a = clampi(a + d * (fine ? 1 : 10), 0, 360); break;
        case EDIT_LOWPASS: case EDIT_HIGHPASS: {                                         // about a semitone, fine: 1 %
            const int32_t step = fine ? a / 100 : a * 6 / 100;
            a = clampi(a + d * (step < 1 ? 1 : step), 20, 20000);
        } break;
        default: break;
    }
    c->ui->edit_amt[op] = a;
    return false;
}
// Beat / Tonal: which stretch runs under Stretch, Pitch and Fit beats.
static bool en_algo(const ui_ctx_t *c) { return c->ui->edit_op == EDIT_STRETCH || c->ui->edit_op == EDIT_PITCH || c->ui->edit_op == EDIT_FIT; }
static void ed_algo_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", c->ui->edit_tonal ? "Tonal" : "Beat"); }
static bool ed_algo_adjust(const ui_ctx_t *c, int dir) { (void)dir; c->ui->edit_tonal = !c->ui->edit_tonal; return false; }
static void ed_apply_value(const ui_ctx_t *c, char *out, int n) {
    if (c->ui->render_pct >= 0) snprintf(out, (size_t)n, "Working %d%%", c->ui->render_pct);
    else snprintf(out, (size_t)n, "Apply");
}
static void ed_apply(const ui_ctx_t *c) {
    audio_sample_info_t in;
    if (c->ui->render_pct < 0 && file_info(cur_pad(c), &in) && !in.pending) c->ui->pad_render_req = true;
}

// The versions of the pad's sample: Ver steps through them (the root first, then by age), Undo = the parent, Redo = the newest child.
static void set_version(const ui_ctx_t *c, const char *name) {
    if (!name[0] || file_index(name) < 0) return;
    pad_t *p = cur_pad(c);
    uint32_t t[3] = {0, 0, 0};                                            // the trim points go with the version (history.h)
    if (c->ui->history) history_trim_for(c->ui->history, p->file, name, t);
    p->start = t[0]; p->end = t[1]; p->lstart = t[2];
    snprintf(p->file, PAD_NAME_MAX, "%s", name);
}
static void ed_ver_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", cur_pad(c)->file[0] ? cur_pad(c)->file : "--"); }
static bool ed_ver_adjust(const ui_ctx_t *c, int dir) {
    const history_t *h = c->ui->history;
    pad_t *p = cur_pad(c);
    if (!h || !p->file[0]) return false;
    char root[HISTORY_NAME], vers[HISTORY_MAX + 1][HISTORY_NAME];
    history_root(h, p->file, root);
    const int n = history_versions(h, root, vers, HISTORY_MAX + 1);
    int at = 0;
    for (int i = 0; i < n; i++) if (!strcmp(vers[i], p->file)) at = i;
    for (int k = 1; k < n; k++) {                                         // the next one still in the library
        const int i = (at + n + (dir > 0 ? k : -k)) % n;
        if (file_index(vers[i]) >= 0) { set_version(c, vers[i]); return true; }
    }
    return false;
}
static void ed_undo(const ui_ctx_t *c) {
    char to[HISTORY_NAME] = "";
    if (c->ui->history) history_parent(c->ui->history, cur_pad(c)->file, to);
    if (to[0] && file_index(to) >= 0) { set_version(c, to); c->ui->pad_render_req = false; c->ui->pad_ver_changed = true; }
}
static void ed_redo(const ui_ctx_t *c) {
    char to[HISTORY_NAME] = "";
    if (c->ui->history) history_newest_child(c->ui->history, cur_pad(c)->file, to);
    if (to[0] && file_index(to) >= 0) { set_version(c, to); c->ui->pad_ver_changed = true; }
}

static const el_def_t edit_elements[] = {
    {"Op",    EL_VALUE,  0, 0, 2, ed_op_value, ed_op_adjust, NULL, NULL, NULL, NULL, 0, 0},
    {"",      EL_VALUE,  1, 0, 2, ed_amt_value, ed_amt_adjust, NULL, NULL, en_amt, ed_amt_label, 0, EF_HIDE_WHEN_DISABLED},
    {"Algo",  EL_VALUE,  2, 0, 2, ed_algo_value, ed_algo_adjust, NULL, NULL, en_algo, NULL, 0, EF_HIDE_WHEN_DISABLED},
    {"Apply", EL_BUTTON, 3, 0, 2, ed_apply_value, NULL, ed_apply, NULL, NULL, NULL, 0, 0},
    {"Ver",   EL_VALUE,  4, 0, 2, ed_ver_value, ed_ver_adjust, NULL, NULL, NULL, NULL, 0, 0},
    {"Undo",  EL_BUTTON, 5, 0, 1, NULL, NULL, ed_undo, NULL, NULL, NULL, 0, 0},
    {"Redo",  EL_BUTTON, 5, 1, 1, NULL, NULL, ed_redo, NULL, NULL, NULL, 0, 0},
};
#define N_EDIT ((int)(sizeof edit_elements / sizeof edit_elements[0]))

static void edit_layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)g; (void)st;
    const int y0 = area.y + WAVE_H + 12, half = area.w / 2, rh = 11;
    for (int i = 0; i < N_EDIT; i++) {
        const el_def_t *e = &edit_elements[i];
        int row = e->row;                                                 // the rows close up over a hidden one (the amount, Algo)
        for (int k = 0; k < N_EDIT; k++) {
            const el_def_t *h = &edit_elements[k];
            if (h->row < e->row && (h->flags & EF_HIDE_WHEN_DISABLED) && h->enabled && !h->enabled(c)) row--;
        }
        rect[i] = gui_rect(area.x + e->col * half, y0 + row * rh, e->span * half - 1, rh - 1);
    }
}

// The overview of the pad's sample, then where this version comes from ("from kick~2: Gain +6.0dB").
static void edit_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    draw_extra(g, st, c, area, rect);
    const pad_t *p = cur_pad(c);
    const hist_ver_t *v = c->ui->history ? history_find(c->ui->history, p->file) : NULL;
    char t[48];
    if (v) snprintf(t, sizeof t, "from %.10s: %s", v->parent[0] ? v->parent : "?", v->what);
    else snprintf(t, sizeof t, p->file[0] ? "original" : "");
    u8g2_DrawStr(g, area.x + 1, area.y + WAVE_H + 10, t);
}

static const screen_def_t edit_screen = {edit_elements, N_EDIT, false, true, edit_layout, NULL, NULL, edit_extra};

#define PAD_PAGES 4                                      // PLAY, TONE, CHOP, EDIT
static const screen_def_t *editor_screen(const synth_ui_t *ui) {
    return ui->pad_page == 3 ? &edit_screen : ui->pad_page == 2 ? &chop_screen : ui->pad_page == 1 ? &tone_screen : &pads_screen;
}

int pads_ui_row(int page, int param) {
    const screen_def_t *s = page == 3 ? &edit_screen : page == 2 ? &chop_screen : page == 1 ? &tone_screen : &pads_screen;
    for (int i = 0; i < s->n; i++) if (s->el[i].arg == param && s->el[i].kind == EL_VALUE) return i + 1;
    return -1;
}

void pads_ui_chop_refresh(synth_ui_t *ui) { chop_refresh(ui); }

bool pads_ui_tick(synth_ui_t *ui) {
    if (!ui->pads || !ui->pad_edit || ui->pad_page != 2 || !ui->chop.pending) return false;
    chop_refresh(ui);
    return !ui->chop.pending;
}

/* ---------------- events ---------------- */

static bool grid_event(synth_ui_t *ui, ui_event_t e) {
    const int col = ui->pad_cur % 4, row = ui->pad_cur / 4;
    switch (e) {
        case UI_NAV_LEFT:  if (col > 0) ui->pad_cur--; break;
        case UI_NAV_RIGHT: if (col < 3) ui->pad_cur++; break;
        case UI_NAV_UP:    if (row > 0) ui->pad_cur -= 4; break;
        case UI_NAV_DOWN:  if (row < 3) ui->pad_cur += 4; break;
        case UI_UP:        ui->pad_cur = (uint8_t)((ui->pad_cur + PADS_PER_BANK - 1) % PADS_PER_BANK); break;
        case UI_DOWN:      ui->pad_cur = (uint8_t)((ui->pad_cur + 1) % PADS_PER_BANK); break;
        case UI_VALUE_INC: case UI_RIGHT: case UI_PAGE_NEXT: ui->pad_bank = (uint8_t)((ui->pad_bank + 1) % PAD_BANKS); break;
        case UI_VALUE_DEC: case UI_LEFT:  case UI_PAGE_PREV: ui->pad_bank = (uint8_t)((ui->pad_bank + PAD_BANKS - 1) % PAD_BANKS); break;
        case UI_LATCH: case UI_SELECT: ui->pad_edit = true; ui->pad_page = 0; ui->pad_row = 1; ui->pad_latched = false; break;
        default: break;
    }
    return false;
}

bool pads_ui_handle(synth_ui_t *ui, rack_t *rack, ui_event_t e) {
    if (!ui->pads) return false;
    if (!ui->pad_edit) return grid_event(ui, e);
    if (e == UI_BACK) {                                                           // TONE / CHOP -> PLAY -> the grid
        if (ui->pad_page) { ui->pad_page = 0; ui->pad_row = 1; }
        else ui->pad_edit = false;
        ui->pad_latched = false;
        return false;
    }
    if (e == UI_ROW_TOP) { ui->pad_row = 0; ui->pad_latched = false; return false; }
    // the header (and Page from anywhere): the pages PLAY / TONE / CHOP; the edited pad is the pad key pushed (decision 42)
    int step = e == UI_PAGE_NEXT ? 1 : e == UI_PAGE_PREV ? -1 : 0;
    if (ui->pad_row == 0 && (e == UI_NAV_LEFT || e == UI_VALUE_DEC || e == UI_LEFT)) step = -1;
    if (ui->pad_row == 0 && (e == UI_NAV_RIGHT || e == UI_VALUE_INC || e == UI_RIGHT)) step = 1;
    if (step) {
        ui->pad_page = (uint8_t)((ui->pad_page + PAD_PAGES + step) % PAD_PAGES);
        if (e == UI_PAGE_NEXT || e == UI_PAGE_PREV) { ui->pad_row = 1; ui->pad_latched = false; }
        if (ui->pad_page == 2) chop_refresh(ui);
        return false;
    }
    if (ui->pad_page == 2 && ui->chop_pad != pads_ui_pad(ui)) chop_refresh(ui);   // another pad (its key): a new chop, before the event acts
    const int row = ui->row;                             // the screen helpers use the synth UI's focus: lend them the editor's
    const bool latched = ui->latched;
    ui->row = ui->pad_row; ui->latched = ui->pad_latched;
    const ui_ctx_t ctx = {ui, rack, 0};
    const bool changed = screen_event(editor_screen(ui), &ctx, e);
    ui->pad_row = ui->row; ui->pad_latched = ui->latched;
    ui->row = row; ui->latched = latched;
    if (ui->pad_page == 2) chop_refresh(ui);                                     // (the trim may have moved)
    return changed;
}

/* ---------------- drawing ---------------- */

static void draw_cell(u8g2_t *g, gui_rect_t r, int index, const pad_t *p, bool on, bool sel) {
    const gui_rect_t in = gui_inset(r, 1);
    if (on) u8g2_DrawBox(g, in.x, in.y, in.w, in.h); else u8g2_DrawFrame(g, in.x, in.y, in.w, in.h);
    if (sel) { u8g2_DrawFrame(g, r.x, r.y, r.w, r.h); if (!on) u8g2_DrawFrame(g, in.x + 1, in.y + 1, in.w - 2, in.h - 2); }
    u8g2_SetDrawColor(g, on ? 0 : 1);
    char t[8];
    snprintf(t, sizeof t, "%d", index + 1);
    u8g2_DrawStr(g, in.x + 2, in.y + 8, t);
    if (!pad_empty(p)) {
        snprintf(t, sizeof t, "%s%s", p->mode == PAD_GATE ? "G" : p->mode == PAD_TOGGLE ? "T" : "", p->loop ? "L" : "");
        u8g2_DrawStr(g, in.x + in.w - 2 - u8g2_GetStrWidth(g, t), in.y + 8, t);
        snprintf(t, sizeof t, "%.5s", p->file);
        u8g2_DrawStr(g, in.x + 2, in.y + 17, t);
        int x = in.x + 2;
        if (p->mute) { u8g2_DrawStr(g, x, in.y + 25, "M"); x += 7; }
        if (p->choke) { snprintf(t, sizeof t, "c%d", p->choke); u8g2_DrawStr(g, x, in.y + 25, t); }
    }
    u8g2_SetDrawColor(g, 1);
}

void pads_ui_draw(const synth_ui_t *ui, const rack_t *rack, u8g2_t *g, uint64_t playing, int bpm) {
    gui_style_t style;
    gui_style_init(&style, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    const gui_style_t *st = &style;
    u8g2_ClearBuffer(g);
    gui_begin(g, st);
    if (!ui->pads) return;
    gui_rect_t screen = gui_inset(gui_screen(g), st->margin);
    const gui_rect_t bar = gui_take_top(&screen, GUI_BAR_H);
    const gui_rect_t header = gui_rect(bar.x, bar.y, bar.w, bar.h - 1);
    char t[40], l[8];
    const int sel = pads_ui_pad(ui);
    if (ui->pad_edit) {
        pad_label(sel, l, sizeof l);
        const pad_t *p = &ui->pads->pad[sel];
        static const char *const page[PAD_PAGES] = {"PLAY", "TONE", "CHOP", "EDIT"};
        if (ui->pad_page) snprintf(t, sizeof t, "< %s %s >", l, page[ui->pad_page % PAD_PAGES]);
        else snprintf(t, sizeof t, "< %s %.9s >", l, p->file[0] ? p->file : "PLAY");
    }
    u8g2_DrawHLine(g, bar.x, gui_bottom(bar) - 1, bar.w);
    const bool head_focus = ui->pad_edit && ui->pad_row == 0;
    if (head_focus) { u8g2_DrawBox(g, header.x, header.y, header.w, header.h); u8g2_SetDrawColor(g, 0); }
    // the tempo at the top left on every Pads screen (user, 2026-10-10: to see the BPM keys act), the bar.beat at the right of the grid
    const int ty = gui_text_center(g, header, "B").y;
    char bt[8];
    snprintf(bt, sizeof bt, "%d", bpm);
    const int bw = u8g2_GetStrWidth(g, bt) + 4;
    u8g2_DrawStr(g, header.x + 1, ty, bt);
    u8g2_DrawVLine(g, header.x + bw - 2, header.y + 2, header.h - 4);
    if (ui->pad_edit) gui_draw_text_centered(g, gui_rect(header.x + bw, header.y, header.w - bw, header.h), t);
    else {                                                   // the grid: "BANK A", then the position at the right
        snprintf(t, sizeof t, "BANK %c", 'A' + ui->pad_bank);
        u8g2_DrawStr(g, header.x + bw + 2, ty, t);
        int bar_n = 0, beat = 0, frac = 0;
        const bool run = audio_transport(&bar_n, &beat, &frac);
        char pos[24];
        if (run) snprintf(pos, sizeof pos, "%d.%d", bar_n > 99 ? 99 : bar_n, beat); else snprintf(pos, sizeof pos, "--");
        const int pw = u8g2_GetStrWidth(g, pos), px = header.x + header.w - 2 - pw;
        if (run && frac < 64) { u8g2_DrawBox(g, px - 1, header.y + 2, pw + 2, header.h - 4); u8g2_SetDrawColor(g, 0); }   // the beat flashes
        u8g2_DrawStr(g, px, ty, pos);
        u8g2_SetDrawColor(g, 1);
    }
    if (playing & (1ull << sel)) u8g2_DrawStr(g, header.x + header.w - 6, gui_text_center(g, header, ">").y, ">");   // the selected pad sounds
    u8g2_SetDrawColor(g, 1);

    if (ui->pad_edit) {
        synth_ui_t *w = (synth_ui_t *)ui;                    // drawing reads the focus the way pads_ui_handle lends it
        const int row = w->row;
        const bool latched = w->latched;
        w->row = ui->pad_row; w->latched = ui->pad_latched;
        const ui_ctx_t ctx = {w, (rack_t *)rack, 0};
        screen_draw(editor_screen(ui), g, st, &ctx, screen);
        w->row = row; w->latched = latched;
        return;
    }
    for (int i = 0; i < PADS_PER_BANK; i++) {
        const int pad = ui->pad_bank * PADS_PER_BANK + i;
        draw_cell(g, gui_grid_cell(screen, 4, 4, i, 0), i, &ui->pads->pad[pad], (playing >> pad) & 1, i == ui->pad_cur);
    }
}
