// The Pads section's screens (ADR-041 decision 25): the grid of the bank shown, full screen, and the selected pad's editor (a declarative
// screen, see ui_screen.h). The grid: 4 x 4 cells (pad number, mode / loop, the start of its sample's name, mute and choke marks); a pad
// that sounds is drawn inverted, the selected one has a double frame. The editor: the sample's overview with the trim across the top, then
// the settings in two columns. The app (app.c) owns the pads, plays them and sends every change to the engine.
#include "core/ui_screen.h"
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

static bool pad_adjust(const ui_ctx_t *c, int dir) {
    pad_t *p = cur_pad(c);
    audio_sample_info_t in;
    if (c->arg == PADP_LOOP) {                           // Off / Trim / File; File only when the file has loop points
        const bool file_loop = file_info(p, &in) && file_has_loop(&in);
        const uint8_t was = p->loop;
        pad_param_adjust(p, PADP_LOOP, dir);
        if (p->loop == PAD_LOOP_FILE && !file_loop) pad_param_adjust(p, PADP_LOOP, dir);
        return p->loop != was;
    }
    if (c->arg != PADP_FILE) return pad_param_adjust(p, (pad_param_t)c->arg, dir);
    const int n = audio_sample_count();                  // the library in its order, "--" (none) before the first file
    int i = file_index(p->file) + (dir > 0 ? 1 : -1);
    if (i < -1) i = n - 1;
    if (i >= n) i = -1;
    if (i >= 0 && audio_sample_info(i, &in)) snprintf(p->file, PAD_NAME_MAX, "%s", in.name);
    else p->file[0] = 0;
    // a file with its own loop points loads them (user, 2026-10-10); without them File is not offered
    const bool file_loop = p->file[0] && file_has_loop(&in);
    if (file_loop) p->loop = PAD_LOOP_FILE;
    else if (p->loop == PAD_LOOP_FILE) p->loop = PAD_LOOP_OFF;
    return true;
}

static const char *pad_label_dyn(const ui_ctx_t *c) { return pad_param_label((pad_param_t)c->arg); }

#define ROW(r, col, span, k) {"", EL_VALUE, r, col, span, pad_value, pad_adjust, NULL, NULL, NULL, pad_label_dyn, k, 0}
static const el_def_t elements[] = {
    ROW(0, 0, 2, PADP_FILE),
    ROW(1, 0, 1, PADP_MODE), ROW(2, 0, 1, PADP_START), ROW(3, 0, 1, PADP_END), ROW(4, 0, 1, PADP_LOOP), ROW(5, 0, 1, PADP_TAIL),
    ROW(6, 0, 1, PADP_TUNE), ROW(7, 0, 1, PADP_FINE),
    ROW(1, 1, 1, PADP_LEVEL), ROW(2, 1, 1, PADP_PAN), ROW(3, 1, 1, PADP_CUT), ROW(4, 1, 1, PADP_RES), ROW(5, 1, 1, PADP_CHOKE),
    ROW(6, 1, 1, PADP_MUTE),
};
#undef ROW
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))
#define WAVE_H 32                                        // the sample overview across the top (4 grid rows)
#define ROW_H  9

// The overview on top, the sample row across the width, then two columns of 9 px rows.
static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)g; (void)st; (void)c;
    const int y0 = area.y + WAVE_H + 2, half = area.w / 2;
    for (int i = 0; i < N_ELEMENTS; i++) {
        const el_def_t *e = &elements[i];
        rect[i] = gui_rect(area.x + e->col * half, y0 + e->row * ROW_H, e->span * half - 1, ROW_H);
    }
}

// The sample's overview (its 64 peaks), dotted outside the trim, the start and end as lines, a loop as marks at the top of both.
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)st; (void)rect;
    const gui_rect_t box = gui_rect(area.x, area.y + 1, area.w, WAVE_H);
    const pad_t *p = cur_pad(c);
    audio_sample_info_t in;
    if (!file_info(p, &in) || in.pending) {
        u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
        gui_draw_text_centered(g, box, !p->file[0] ? "empty pad" : in.pending && file_index(p->file) >= 0 ? "not converted yet" : "not on this card");
        return;
    }
    const int mid = box.y + box.h / 2, amp = box.h / 2 - 1;
    const int xs = box.x + p->start * (box.w - 1) / 1000, xe = box.x + p->end * (box.w - 1) / 1000;
    for (int x = 0; x < box.w; x++) {
        const int px = box.x + x;
        const bool inside = px >= xs && px <= xe;
        if (!inside && (x & 1)) continue;
        const int h = in.peaks[x * AUDIO_PEAKS / box.w] * amp / 255;
        u8g2_DrawVLine(g, px, mid - h, 2 * h + 1);
    }
    u8g2_DrawVLine(g, xs, box.y, box.h);
    u8g2_DrawVLine(g, xe, box.y, box.h);
    if (p->loop == PAD_LOOP_TRIM) { u8g2_DrawHLine(g, xs, box.y, 4); u8g2_DrawHLine(g, xe - 3, box.y, 4); }
    if (p->loop == PAD_LOOP_FILE && file_has_loop(&in) && in.frames) {    // the file's loop points: a bracket along the top
        const int ls = box.x + (int)((uint64_t)in.loop_start * (uint64_t)(box.w - 1) / in.frames);
        const int le = box.x + (int)((uint64_t)in.loop_end * (uint64_t)(box.w - 1) / in.frames);
        u8g2_DrawHLine(g, ls, box.y, le - ls + 1);
        u8g2_DrawVLine(g, ls, box.y, 3);
        u8g2_DrawVLine(g, le, box.y, 3);
    }
}

static const screen_def_t pads_screen = {elements, N_ELEMENTS, false, true, layout, NULL, NULL, draw_extra};

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
        case UI_LATCH: case UI_SELECT: ui->pad_edit = true; ui->pad_row = 1; ui->pad_latched = false; break;
        default: break;
    }
    return false;
}

bool pads_ui_handle(synth_ui_t *ui, rack_t *rack, ui_event_t e) {
    if (!ui->pads) return false;
    if (!ui->pad_edit) return grid_event(ui, e);
    if (e == UI_BACK) { ui->pad_edit = false; ui->pad_latched = false; return false; }
    if (e == UI_ROW_TOP) { ui->pad_row = 0; ui->pad_latched = false; return false; }
    // the header (and Page from anywhere): the previous / next pad of the bank
    int step = e == UI_PAGE_NEXT ? 1 : e == UI_PAGE_PREV ? -1 : 0;
    if (ui->pad_row == 0 && (e == UI_NAV_LEFT || e == UI_VALUE_DEC || e == UI_LEFT)) step = -1;
    if (ui->pad_row == 0 && (e == UI_NAV_RIGHT || e == UI_VALUE_INC || e == UI_RIGHT)) step = 1;
    if (step) { ui->pad_cur = (uint8_t)((ui->pad_cur + PADS_PER_BANK + step) % PADS_PER_BANK); return false; }
    const int row = ui->row;                             // the screen helpers use the synth UI's focus: lend them the editor's
    const bool latched = ui->latched;
    ui->row = ui->pad_row; ui->latched = ui->pad_latched;
    const ui_ctx_t ctx = {ui, rack, 0};
    const bool changed = screen_event(&pads_screen, &ctx, e);
    ui->pad_row = ui->row; ui->pad_latched = ui->latched;
    ui->row = row; ui->latched = latched;
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

void pads_ui_draw(const synth_ui_t *ui, const rack_t *rack, u8g2_t *g, uint64_t playing) {
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
        snprintf(t, sizeof t, "< %s %s >", l, p->file[0] ? p->file : "empty");
    } else {
        snprintf(t, sizeof t, "PADS  BANK %c", 'A' + ui->pad_bank);
    }
    u8g2_DrawHLine(g, bar.x, gui_bottom(bar) - 1, bar.w);
    const bool head_focus = ui->pad_edit && ui->pad_row == 0;
    if (head_focus) { u8g2_DrawBox(g, header.x, header.y, header.w, header.h); u8g2_SetDrawColor(g, 0); }
    gui_draw_text_centered(g, header, t);
    if (playing & (1ull << sel)) u8g2_DrawStr(g, header.x + header.w - 6, gui_text_center(g, header, ">").y, ">");   // the selected pad sounds
    u8g2_SetDrawColor(g, 1);

    if (ui->pad_edit) {
        synth_ui_t *w = (synth_ui_t *)ui;                    // drawing reads the focus the way pads_ui_handle lends it
        const int row = w->row;
        const bool latched = w->latched;
        w->row = ui->pad_row; w->latched = ui->pad_latched;
        const ui_ctx_t ctx = {w, (rack_t *)rack, 0};
        screen_draw(&pads_screen, g, st, &ctx, screen);
        w->row = row; w->latched = latched;
        return;
    }
    for (int i = 0; i < PADS_PER_BANK; i++) {
        const int pad = ui->pad_bank * PADS_PER_BANK + i;
        draw_cell(g, gui_grid_cell(screen, 4, 4, i, 0), i, &ui->pads->pad[pad], (playing >> pad) & 1, i == ui->pad_cur);
    }
}
