// The screens: the header, the parameter lists, the hand-drawn screens (sequencer, rack, effects, FM editor...) and synth_ui_draw().
#include "core/ui_screen.h"
#include "core/bindings.h"
#include "hal/hal_display.h"
#include "core/sprites.h"
#include "core/module_sprites.h"
#include "core/ui_sprites_gen.h"
#include "core/dx7_algos.h"
#include <math.h>
#include <stdio.h>

/* ---------------- graphs ---------------- */

/* ---------------- sequencer ---------------- */

// Piano roll: one column per step of the page the cursor is on (SEQ_PAGE steps; a shorter pattern: wider columns), SEQ_ROLL_NOTES
// semitones high, a note as long as its length. A step's chord: one per note. On the 128 x 128 screen (ADR-042 decision 28) 3 px a
// semitone and each note an unfilled box (roll_note), the selected one larger; a lower screen keeps thin filled bars (dotted when
// it may not play, cut into its ratchet's hits). A note outside the window: a dot on the edge it is beyond. Below it: playhead
// strip, then the info lines (seq_info). All coordinates come from the gui_* layout helpers and the style sheet.
//
// The roll scrolls (user, 2026-10-14): sideways by pages of SEQ_PAGE steps, up and down by octaves. It shows the cursor's page and
// the edited note; while it runs and nothing was edited by hand lately (ui->seq_follow, the app's), the page of the step that plays
// and that step's highest note. The playhead on another page than the one shown: a mark at that end of its strip.

// The lowest note the roll shows: the octave of the lowest note of the page, moved up when the note in focus would be above the window.
static int seq_roll_low(const seq_t *q, int first, int shown, int focus) {
    int low = 0;
    for (int s = first; s < first + shown; s++)
        for (int k = 0; k < SEQ_CHORD; k++) {
            const int n = q->step[s].note[k];
            if (n && (!low || n < low)) low = n;
        }
    if (!low) low = 48;
    low = low / 12 * 12;
    if (focus >= low + SEQ_ROLL_NOTES) low = focus / 12 * 12 - 12;
    else if (focus && focus < low) low = focus / 12 * 12;
    return low;
}

// The pitch the roll's cursor is on: the selected note, or where the empty slot would put one.
static int seq_sel_pitch(const synth_ui_t *ui, const seq_t *q, int cursor, int *voice) {
    *voice = seq_slot_voice(q, cursor, ui->seq_voice);
    const int n = q->step[cursor].note[*voice];
    return n ? n : seq_free_pitch(q, cursor);
}

void synth_ui_seq_view(const synth_ui_t *ui, const seq_t *q, int *first, int *low) {
    const int cursor = ui->cursor < q->steps ? ui->cursor : q->steps - 1;
    const bool follow = ui->seq_follow && q->running && q->pos >= 0 && q->pos < q->steps;
    const int at = follow ? q->pos : cursor;
    int voice;
    int focus = seq_sel_pitch(ui, q, cursor, &voice);
    if (follow) {                                                    // the step that plays: its highest note
        focus = 0;
        for (int k = 0; k < SEQ_CHORD; k++) if (q->step[at].note[k] > focus) focus = q->step[at].note[k];
    }
    const int cols = q->steps < SEQ_PAGE ? q->steps : SEQ_PAGE;
    *first = at / SEQ_PAGE * SEQ_PAGE;
    *low = seq_roll_low(q, *first, q->steps - *first < cols ? q->steps - *first : cols, focus);
}

// One note of the tall roll (ADR-042 decision 28): an unfilled box, its inside filled from the left to the velocity (one pixel at
// least); the top and bottom lines dotted when it may not play (Prob); a pixel of its inside inverted where each ratchet hit after
// the first starts. The selected note is drawn 1 px larger on every side, over what is under it.
static void roll_note(u8g2_t *g, int x, int y, int w, int h, const seq_step_t *sp) {
    if (w < 3) w = 3;
    u8g2_SetDrawColor(g, 0);
    u8g2_DrawBox(g, x, y, w, h);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawVLine(g, x, y, h);
    u8g2_DrawVLine(g, x + w - 1, y, h);
    for (int px = x + 1; px < x + w - 1; px++)
        if (sp->prob >= 100 || (px - x) % 2 == 0) { u8g2_DrawPixel(g, px, y); u8g2_DrawPixel(g, px, y + h - 1); }
    const int in = w - 2;
    int fill = (sp->vel * in + 63) / 127;
    if (fill < 1 && sp->vel > 0) fill = 1;
    if (fill > in) fill = in;
    if (fill > 0) for (int r = y + 1; r < y + h - 1; r++) u8g2_DrawHLine(g, x + 1, r, fill);
    if (sp->ratchet > 1) {
        u8g2_SetDrawColor(g, 2);
        for (int k = 1; k < sp->ratchet; k++) u8g2_DrawVLine(g, x + 1 + k * in / sp->ratchet, y + 1, h - 2);
        u8g2_SetDrawColor(g, 1);
    }
}

// The info lines under the tall roll (ADR-042 decision 26): the selected note and its step's values, the tool and its scope, then
// the step and the run state. The value encoder A picked for encoder B is drawn filled.
static void seq_info(u8g2_t *g, const gui_style_t *st, gui_rect_t lines, int count, int only, const synth_ui_t *ui, const seq_t *q,
                     int cursor, int voice) {
    const seq_step_t *cs = &q->step[cursor];
    const int row_h = gui_row_h(g, st);
    char v[5][8];
    if (cs->note[voice]) seq_note_name(cs->note[voice], v[0], sizeof v[0]); else snprintf(v[0], sizeof v[0], "---");
    snprintf(v[1], sizeof v[1], "L%d", cs->len);
    snprintf(v[2], sizeof v[2], "V%d", cs->vel);
    snprintf(v[3], sizeof v[3], "%d%%", cs->prob);
    snprintf(v[4], sizeof v[4], "x%d", cs->ratchet);
    for (int l = 0; l < count; l++) {
        const int which = only >= 0 ? only : l;
        const gui_rect_t line = gui_rect(lines.x, lines.y + l * (row_h + st->gap), lines.w, row_h);
        if (which == 0) {
            for (int i = 0; i < 5; i++) {
                const gui_rect_t c = gui_grid_cell(line, 5, 1, i, 0);
                const bool sel = ui->row == SEQ_ROW_NOTE + i;
                if (sel) { u8g2_DrawBox(g, c.x, c.y, c.w, c.h); u8g2_SetDrawColor(g, 0); }
                gui_draw_text_centered(g, c, v[i]);
                u8g2_SetDrawColor(g, 1);
            }
        } else if (which == 1) {
            char tool[20], scope[12];                                // "Tool RndProb" | "On Page": each read from its left
            snprintf(tool, sizeof tool, "Tool %s", seq_tool_name(ui->seq_tool));
            snprintf(scope, sizeof scope, "On %s", seq_scope_name(ui->seq_scope));
            const int split = line.w * 9 / 16;
            gui_draw_field(g, st, gui_rect(line.x, line.y, split - st->gap, line.h), tool, NULL, ui->row == SEQ_ROW_TOOL);
            gui_draw_field(g, st, gui_rect(line.x + split, line.y, line.w - split, line.h), scope, NULL, ui->row == SEQ_ROW_SCOPE);
        } else {
            char step[24];
            snprintf(step, sizeof step, "%02d/%02d", cursor + 1, q->steps);
            gui_draw_field(g, st, line, step, NULL, false);
            if (q->running && ui->run_anim != GUI_ANIM_INVALID) gui_anim_draw_right(g, st, ui->run_anim, line);
            else gui_draw_sprite_right(g, st, q->running ? &spr_play : &spr_stop, line);
            const char *state = ui->seq_erase ? "Erase" : ui->seq_audio ? "Audio" : ui->seq_rec ? "Rec" : NULL;
            if (state) gui_draw_text_right(g, st, gui_rect(line.x, line.y, line.w - spr_play.w - 2, line.h), state);
        }
    }
}

static void draw_seq(u8g2_t *g, const gui_style_t *st, gui_rect_t area, const synth_ui_t *ui, const seq_t *q) {
    const int row_h = gui_row_h(g, st);
    const int tall_pitch = 3;                                        // decision 28: 3 px a semitone, notes as boxes
    const int tall_h = SEQ_ROLL_NOTES * tall_pitch + 2 * (1 + st->roll_pad) + 2 * st->gap + st->strip_h + 3 * row_h + 2 * st->gap;
    const bool tall = area.h >= tall_h;                              // (a lower screen: thin bars and one info line)
    const int pitch = tall ? tall_pitch : st->roll_pitch_px;
    const int bar_h = tall ? tall_pitch : st->roll_bar_h;
    const int cursor = ui->cursor < q->steps ? ui->cursor : q->steps - 1;
    int first, low;                                                  // the page and the pitches shown: the cursor's, or the playhead's
    synth_ui_seq_view(ui, q, &first, &low);
    const int cols = q->steps < SEQ_PAGE ? q->steps : SEQ_PAGE;
    const int shown = q->steps - first < cols ? q->steps - first : cols;
    int voice;
    const int sel_pitch = seq_sel_pitch(ui, q, cursor, &voice);
    int cell = area.w / cols;
    int roll_h = SEQ_ROLL_NOTES * pitch + 2 * (1 + st->roll_pad);    // frame + padding + notes
    gui_rect_t roll = gui_center_box(gui_rect(area.x, area.y, area.w, roll_h), cell * cols, roll_h);
    roll.y = area.y;
    u8g2_DrawFrame(g, roll.x, roll.y, roll.w, roll.h);
    gui_rect_t inner = gui_inset(roll, 1);

    // the notes; the selected one (or the empty slot) last, over the others
    int sel_x = -1, sel_y = 0, sel_w = 0;
    bool sel_empty = false;
    const bool cursor_shown = cursor >= first && cursor < first + shown;
    for (int i = 0; i < shown; i++) {
        const seq_step_t *sp = &q->step[first + i];
        int x = inner.x + i * cell;
        if (i % st->roll_beat == 0 && i > 0)
            for (int y = inner.y; y < gui_bottom(inner); y += 3) u8g2_DrawPixel(g, x, y);
        int w = sp->len * cell - 2;
        if (x + 1 + w > gui_right(roll) - 1) w = gui_right(roll) - 1 - (x + 1);
        for (int k = 0; k < SEQ_CHORD; k++) {
            const int n = sp->note[k];
            if (!n) continue;
            if (n < low || n >= low + SEQ_ROLL_NOTES) {
                u8g2_DrawPixel(g, x + cell / 2, n < low ? gui_bottom(inner) - 1 : inner.y);
                continue;
            }
            const int y = inner.y + st->roll_pad + (low + SEQ_ROLL_NOTES - 1 - n) * pitch;
            if (cursor_shown && first + i == cursor && k == voice) { sel_x = x + 1; sel_y = y; sel_w = w; continue; }
            if (tall) { roll_note(g, x + 1, y, w, bar_h, sp); continue; }
            if (sp->prob >= 100) u8g2_DrawBox(g, x + 1, y, w, bar_h);
            else for (int px = 0; px < w; px += 2) u8g2_DrawVLine(g, x + 1 + px, y, bar_h);
            if (sp->ratchet > 1) {
                u8g2_SetDrawColor(g, 0);
                for (int h = 1; h < sp->ratchet; h++) u8g2_DrawVLine(g, x + 1 + h * (cell - 2) / sp->ratchet, y, bar_h);
                u8g2_SetDrawColor(g, 1);
            }
        }
    }
    if (cursor_shown && sel_x < 0 && sel_pitch >= low && sel_pitch < low + SEQ_ROLL_NOTES) {     // the empty slot: one step long
        sel_x = inner.x + (cursor - first) * cell + 1;
        sel_y = inner.y + st->roll_pad + (low + SEQ_ROLL_NOTES - 1 - sel_pitch) * pitch;
        sel_w = cell - 2;
        sel_empty = true;
    }
    if (sel_x >= 0) {
        const int w = sel_w + 2, h = bar_h + 2, x = sel_x - 1, y = sel_y - 1;
        if (sel_empty) {
            u8g2_SetDrawColor(g, 0); u8g2_DrawBox(g, x, y, w, h); u8g2_SetDrawColor(g, 1);
            u8g2_DrawFrame(g, x, y, w, h);
        } else if (tall) roll_note(g, x, y, w, h, &q->step[cursor]);
        else { u8g2_DrawBox(g, sel_x, sel_y, sel_w, bar_h); u8g2_DrawFrame(g, x, y, w, h); }
    }

    // playhead strip right under the roll, same column geometry
    gui_rect_t strip = gui_below(roll, st->gap, st->strip_h);
    if (q->running && q->pos >= first && q->pos < first + shown) u8g2_DrawBox(g, inner.x + (q->pos - first) * cell + 1, strip.y, cell - 2, strip.h);
    else if (q->running && q->pos >= 0) u8g2_DrawBox(g, q->pos < first ? inner.x : gui_right(inner) - 2, strip.y, 2, strip.h);   // on another page: that side

    // the info lines; a lower screen: the one encoder A is on
    const int count = tall ? 3 : 1;
    gui_rect_t lines = gui_below(strip, st->gap, count * row_h + (count - 1) * st->gap);
    lines.x = area.x; lines.w = area.w;
    seq_info(g, st, lines, count, tall ? -1 : (ui->row >= SEQ_ROW_TOOL ? 1 : 0), ui, q, cursor, voice);
}

// Timing preview for SEQ SETUP: one tick per step of the first page; every second tick moves right with swing.
static void draw_seq_cfg(u8g2_t *g, const gui_style_t *st, gui_rect_t box, const seq_t *q) {
    gui_rect_t in = gui_inset(box, 2 + st->padding);
    char buf[16], rate[8], dir[8];
    int step_w = in.w / SEQ_PAGE;
    for (int s = 0; s < q->steps && s < SEQ_PAGE; s++) {
        int x = in.x + s * step_w + ((s & 1) ? step_w * q->swing / 100 : 0);
        int h = (s % 4 == 0) ? 8 : 4;
        u8g2_DrawVLine(g, x, in.y + 10 - h / 2 + 4, h);
    }
    snprintf(buf, sizeof buf, "%d steps", q->steps);
    gui_draw_text_centered(g, gui_rect(in.x, in.y + 2 * gui_row_h(g, st), in.w, gui_row_h(g, st)), buf);
    snprintf(buf, sizeof buf, "%d BPM", q->bpm);
    gui_draw_text_centered(g, gui_rect(in.x, in.y + 3 * gui_row_h(g, st) + 1, in.w, gui_row_h(g, st)), buf);
    seq_param_format(q, SQP_RATE, rate, sizeof rate);
    seq_param_format(q, SQP_DIR, dir, sizeof dir);
    snprintf(buf, sizeof buf, "%s %s", rate, dir);
    gui_draw_text_centered(g, gui_rect(in.x, in.y + 4 * gui_row_h(g, st) + 2, in.w, gui_row_h(g, st)), buf);
}

// PATTERNS: the chain, four entries a row, each the number of its pattern; the entry that plays is inverted (only while the chain
// is on), the one the Slot row shows is framed. Under it the pattern that plays.
static void draw_seq_pat(u8g2_t *g, const gui_style_t *st, gui_rect_t box, const synth_ui_t *ui, const seq_t *q) {
    const seq_bank_t *b = ui->bank;
    if (!b) return;
    gui_rect_t in = gui_inset(box, 2 + st->padding);
    const int cw = in.w / 4, ch = gui_row_h(g, st) + 2;
    char t[16];
    for (int i = 0; i < b->chain_len && i < SEQ_CHAIN_MAX; i++) {
        const gui_rect_t c = gui_rect(in.x + (i % 4) * cw, in.y + (i / 4) * ch, cw - 1, ch - 1);
        const bool playing = b->chain_on && i == b->chain_pos;
        snprintf(t, sizeof t, "%d", b->chain[i] + 1);
        if (playing) { u8g2_DrawBox(g, c.x, c.y, c.w, c.h); u8g2_SetDrawColor(g, 0); }
        gui_draw_text_centered(g, c, t);
        u8g2_SetDrawColor(g, 1);
        if (i == ui->seq_chain_cur && !playing) u8g2_DrawFrame(g, c.x, c.y, c.w, c.h);
    }
    snprintf(t, sizeof t, "Pat %d%s", b->cur + 1, q->running ? " >" : "");
    gui_draw_text_centered(g, gui_rect(in.x, in.y + 4 * ch + 2, in.w, gui_row_h(g, st)), t);
    snprintf(t, sizeof t, "%d steps", q->steps);
    gui_draw_text_centered(g, gui_rect(in.x, in.y + 4 * ch + 3 + gui_row_h(g, st), in.w, gui_row_h(g, st)), t);
}

/* ---------------- motion sequencer ---------------- */

// Bars for the selected lane: one column per sequencer step (same geometry as the piano roll), a filled bar for an active step,
// a single dot at the stored value for an inactive one; bipolar lanes grow from the centre line. Playhead strip, cursor, fields.
static void draw_ms_steps(u8g2_t *g, const gui_style_t *st, gui_rect_t area, const synth_ui_t *ui, const seq_t *q, const rack_t *r, int slot) {
    const ms_lane_t *l = &rack_ms_const(r, slot)->lane[ui->ms_lane];
    const int steps = seq_motion_steps(q);                           // (a longer pattern plays the lane again every SEQ_PAGE steps)
    const int cell = area.w / steps;
    const int roll_h = SEQ_ROLL_NOTES * st->roll_pitch_px + 2 * (1 + st->roll_pad);
    gui_rect_t roll = gui_center_box(gui_rect(area.x, area.y, area.w, roll_h), cell * steps, roll_h);
    roll.y = area.y;
    u8g2_DrawFrame(g, roll.x, roll.y, roll.w, roll.h);
    gui_rect_t inner = gui_inset(roll, 1);
    const int top = inner.y + 1, bottom = gui_bottom(inner) - 2, mid = (top + bottom) / 2;
    if (l->bipolar) for (int x = inner.x; x < gui_right(inner); x += 3) u8g2_DrawPixel(g, x, mid);
    int px = -1, py = 0;
    for (int s = 0; s < steps; s++) {
        const int x = inner.x + s * cell;
        if (s % st->roll_beat == 0 && s > 0 && !l->bipolar) for (int y = inner.y; y < gui_bottom(inner); y += 3) u8g2_DrawPixel(g, x, y);
        const int y = bottom - (int)l->val[s] * (bottom - top) / 100;
        const bool on = l->active >> s & 1u;
        if (on) {
            const int from = l->bipolar ? mid : bottom;
            const int y0 = y < from ? y : from, h = (y < from ? from - y : y - from) + 1;
            u8g2_DrawBox(g, x + 1, y0, cell - 2 < 1 ? 1 : cell - 2, h);
            if (l->linear && px >= 0) u8g2_DrawLine(g, px, py, x + cell / 2, y);
            px = x + cell / 2; py = y;
        } else {
            u8g2_DrawPixel(g, x + cell / 2, y);
        }
    }
    gui_rect_t strip = gui_below(roll, st->gap, st->strip_h);
    if (q->running && q->pos >= 0) u8g2_DrawBox(g, inner.x + q->pos % steps * cell + 1, strip.y, cell - 2, strip.h);
    const int cx = inner.x + ui->ms_step * cell;
    if (ui->row == 2) u8g2_DrawFrame(g, cx, roll.y, cell + 1, roll.h);
    else              for (int y = roll.y; y < gui_bottom(roll); y += 2) u8g2_DrawPixel(g, cx + cell / 2, y);

    gui_rect_t fields = gui_below(strip, st->gap, 2 * gui_row_h(g, st) + st->gap);
    fields.x = area.x; fields.w = area.w;
    char lane[8], step[16], val[8];
    snprintf(lane, sizeof lane, "%d/%d", ui->ms_lane + 1, MS_LANES);
    snprintf(step, sizeof step, "%02d/%02d", ui->ms_step + 1, steps);
    snprintf(val, sizeof val, "%d%%", l->val[ui->ms_step]);
    const char *label[4] = {"Lane", "Step", "Val", "On"};
    const char *value[4] = {lane, step, val, (l->active >> ui->ms_step & 1u) ? "Yes" : "No"};
    for (int i = 0; i < 4; i++)
        gui_draw_field(g, st, gui_grid_cell(fields, 2, 2, i, st->gap), label[i], value[i], ui->row == i + 1);
}

// Lane settings: Lane, Tgt, Mode, Pol, Dpth (five compact rows) next to the lane's curve.
static void draw_ms_lane(u8g2_t *g, const gui_style_t *st0, gui_rect_t list, gui_rect_t box, const synth_ui_t *ui, const seq_t *q, const rack_t *r, int slot) {
    gui_style_t st = *st0;
    st.padding = 0; st.gap = 0;
    const ms_lane_t *l = &rack_ms_const(r, slot)->lane[ui->ms_lane];
    char lane[8], tgt[16], dp[12];
    snprintf(lane, sizeof lane, "%d/%d", ui->ms_lane + 1, MS_LANES);
    rack_ms_target_name(r, l, tgt, sizeof tgt);
    rack_ms_depth_format(r, (ms_lane_t *)l, dp, sizeof dp);
    const char *label[5] = {"Lane", "Tgt", "Mode", "Pol", "Dpth"};
    const char *value[5] = {lane, tgt, l->linear ? "Lin" : "Step", l->bipolar ? "Bi" : "Uni", dp};
    const int row_h = gui_row_h(g, &st);
    gui_rect_t row = gui_rect(list.x, list.y + st.list_top, list.w - 1, row_h);
    for (int i = 0; i < 5; i++) {
        gui_draw_field(g, &st, row, label[i], value[i], ui->row == i + 1);
        row = gui_below(row, st.gap, row_h);
    }
    draw_ms_curve(g, box, l, seq_motion_steps(q));
}

/* ---------------- resonator ---------------- */

/* ---------------- multi-stage envelope ---------------- */

// EG page 1: Pt, Time, Lvl, Crv of the selected point next to the shape.
static void draw_eg_page(u8g2_t *g, const gui_style_t *st, gui_rect_t list, gui_rect_t box, const synth_ui_t *ui, const rack_slot_t *s) {
    char pt[8], tm[16], lv[16], cv[16];
    snprintf(pt, sizeof pt, "%d/4", ui->eg_pt + 1);
    rack_mparam_format(s, 3 * ui->eg_pt, tm, sizeof tm);
    rack_mparam_format(s, 3 * ui->eg_pt + 1, lv, sizeof lv);
    rack_mparam_format(s, 3 * ui->eg_pt + 2, cv, sizeof cv);
    const char *label[4] = {"Pt", "Time", "Lvl", "Crv"};
    const char *value[4] = {pt, tm, lv, cv};
    const int row_h = gui_row_h(g, st);
    gui_rect_t row = gui_rect(list.x, list.y + st->list_top, list.w - 1, row_h);
    for (int i = 0; i < 4; i++) {
        gui_draw_field(g, st, row, label[i], value[i], ui->row == i + 1);
        row = gui_below(row, st->gap, row_h);
    }
    draw_eg_graph(g, box, s, ui->eg_pt);
}

/* ---------------- samples ---------------- */

/* ---------------- general settings ---------------- */

// Info box next to the general parameters: which engine builds the sound.

/* ---------------- FM editor ---------------- */

void draw_synth_info(u8g2_t *g, const gui_style_t *st, gui_rect_t box, const rack_t *rack) {
    char buf[24];
    int rh = gui_row_h(g, st);
    gui_rect_t r = gui_rect(box.x, box.y + rh - 1, box.w, rh);
    gui_draw_text_centered(g, r, synth_type_name((synth_type_t)rack->cfg.type));
    if (synth_type_is_fm(rack->cfg.type)) {
        gui_draw_text_centered(g, gui_below(r, st->gap + 2, rh), rack->cfg.fm.name);
        synth_config_format(&rack->cfg, CFGP_PATCH, buf, sizeof buf);
        gui_draw_text_centered(g, gui_below(r, st->gap + 2 + 2 * rh, rh), buf);
    } else if (synth_type_is_strings(rack->cfg.type)) {
        gui_draw_text_centered(g, gui_below(r, st->gap + 2, rh), "pads, chords");
        gui_draw_text_centered(g, gui_below(r, st->gap + 2 + 2 * rh, rh), "2 osc/voice");
    } else {
        snprintf(buf, sizeof buf, "%d modules", rack->count);
        gui_draw_text_centered(g, gui_below(r, st->gap + 2, rh), buf);
        gui_draw_text_centered(g, gui_below(r, st->gap + 2 + 2 * rh, rh),
                               rack_shared_start(rack, 0) != RACK_NONE || rack_shared_start(rack, 1) != RACK_NONE ? "paraphonic" : "rack synth");
    }
    if (synth_config_is_mono(&rack->cfg)) snprintf(buf, sizeof buf, "mono");
    else snprintf(buf, sizeof buf, "%d voices", synth_config_voices(&rack->cfg));
    gui_draw_text_centered(g, gui_below(r, st->gap + 2 + 4 * rh, rh), buf);
}

// Label and value of row `row` of an FM page (fm_page_row).
static const char *fm_row_text(const synth_ui_t *ui, const rack_t *rack, const page_t *pg, int row, char *val, int n) {
    fm_row_t r;
    if (!fm_page_row(ui, pg, row, &r)) { snprintf(val, (size_t)n, "?"); return "?"; }
    if (r.selector) { snprintf(val, (size_t)n, "%d/4", (ui->fm_pt & 3) + 1); return "Pt"; }
    if (r.cfg >= 0) { synth_config_format(&rack->cfg, (cfg_param_id_t)r.cfg, val, (size_t)n); return synth_config_label((cfg_param_id_t)r.cfg); }
    dx7_value_format(&rack->cfg.fm, r.op, r.v, val, (size_t)n);
    if (pg->graph == GRAPH_FM_ENV) return r.v >= DXV_EG_T1 ? "Time" : "Lvl";
    return dx7_value_label(r.op, r.v);
}

/* ---------------- screen ---------------- */

static env_params_t module_env(const rack_slot_t *s) {
    if (s->type == MOD_FILTER)
        return (env_params_t){s->v[MP_FL_A], s->v[MP_FL_D], s->v[MP_FL_S], s->v[MP_FL_R], 0, s->v[MP_FL_ACV], s->v[MP_FL_DCV], s->v[MP_FL_RCV]};
    return (env_params_t){s->v[MP_EN_A], s->v[MP_EN_D], s->v[MP_EN_S], s->v[MP_EN_R], s->v[MP_EN_HOLD], s->v[MP_EN_ACV], s->v[MP_EN_DCV], s->v[MP_EN_RCV]};
}

// A module's page, the right half under the bar (8 px grid): the module's 24 px icon, three info lines under it and, on the first page of a module
// with hidden settings, the cog in the top-right grid cell (focused: inverted; its row is the one after the last parameter row).
static void draw_module_head(u8g2_t *g, const gui_style_t *st, const synth_ui_t *ui, const rack_t *rack, const page_t *pg, gui_rect_t r) {
    const rack_slot_t *ms = &rack->slot[pg->slot];
    gui_draw_sprite(g, ui_module_sprite(ms), r.x + GUI_GRID, r.y);
    char line[3][24] = {"", "", ""};
    if (ms->type == MOD_OSC) {                                     // the wave / engine, the tuning, the quality
        rack_mparam_format(ms, MP_OC_WAVE, line[0], sizeof line[0]);
        snprintf(line[1], sizeof line[1], "%+dst %+dct", (int)ms->v[MP_OC_COARSE], (int)ms->v[MP_OC_FINE]);
        char q[12];
        rack_mparam_format(ms, MP_OC_QUAL, q, sizeof q);
        snprintf(line[2], sizeof line[2], "Q %s", q);
    } else {
        snprintf(line[0], sizeof line[0], "%s", rack_type_name((module_type_t)ms->type));
    }
    for (int i = 0; i < 3; i++) gui_draw_text_left(g, st, gui_rect(r.x, r.y + (3 + i) * GUI_GRID, r.w, GUI_GRID), line[i]);
    if (!page_has_cog(rack, pg)) return;
    const int cx = r.x + r.w - GUI_GRID, cy = r.y;
    const bool focus = ui->row == pg->count + 1;
    const gui_sprite_t *cog = ui_sprite("8/ui_cog");
    if (cog && focus) gui_draw_sprite_selected(g, cog, cx, cy);
    else if (cog) gui_draw_sprite(g, cog, cx, cy);
    else {                                                         // no image yet: a small square with a dot
        if (focus) u8g2_DrawBox(g, cx, cy, GUI_GRID, GUI_GRID); else u8g2_DrawFrame(g, cx + 1, cy + 1, GUI_GRID - 2, GUI_GRID - 2);
        u8g2_SetDrawColor(g, focus ? 0 : 1);
        u8g2_DrawBox(g, cx + 3, cy + 3, 2, 2);
        u8g2_SetDrawColor(g, 1);
    }
}

// A page row's label, and its value as text in val (the rows of every page that is a list of values; i = 1..count).
static const char *row_text(const synth_ui_t *ui, const synth_params_t *p, const seq_t *seq, const rack_t *rack, const page_t *pg, int row,
                            char *val, int n) {
    const int i = row - 1;
    const rack_slot_t *ms = pg->slot != GLOBAL_PAGE ? &rack->slot[pg->slot] : NULL;
    const char *label;
    if (ms && (pg->params[i] == PRM_TGT || pg->params[i] == PRM_TPRM)) {
        const int ti = ms->tgt_id ? rack_find(rack, ms->tgt_id) : RACK_NONE;
        if (pg->params[i] == PRM_TGT) {
            label = "Tgt";
            if (ti == RACK_NONE) snprintf(val, (size_t)n, "--"); else rack_slot_name(rack, ti, val, n);
        } else {
            label = "Prm";
            if (ti == RACK_NONE) snprintf(val, (size_t)n, "--");
            else snprintf(val, (size_t)n, "%s", rack_param_name((module_type_t)rack->slot[ti].type, ms->tgt_param));
        }
    } else if (ms) {
        label = rack_mparam_label((module_type_t)ms->type, pg->params[i]);
        if (ms->type == MOD_OSC && pg->params[i] == MP_OC_PW && ms->v[MP_OC_WAVE] >= OC_FIRST_ENGINE) label = "Timb";
        rack_mparam_format(ms, pg->params[i], val, n);
        if (pg->params[i] == rack_depth_index((module_type_t)ms->type)) rack_depth_format(rack, pg->slot, val, n);
        if (ms->type == MOD_SAMPLER && pg->params[i] == MP_SM_FILE) file_label((int)ms->v[MP_SM_FILE] - 1, val, n);
    } else if (graph_is_fm(pg->graph)) {
        label = fm_row_text(ui, rack, pg, row, val, n);
    } else if (pg->graph == GRAPH_SEQ_PAT) {
        label = seq_pat_row_text(ui, seq, row, val, n);
    } else if (pg->graph == GRAPH_SEQ_CFG) {
        label = seq_param_label((seq_param_id_t)pg->params[i]);
        seq_param_format(seq, (seq_param_id_t)pg->params[i], val, n);
    } else {
        label = param_label((param_id_t)pg->params[i]);
        param_format(p, (param_id_t)pg->params[i], val, n);
    }
    return label;
}

void synth_ui_legend(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, legend_cell_t cell[SYNTH_UI_COL_KNOBS]) {
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) ui_legend_cell_none(&cell[k]);
    if (ui->in_rack) {                                                    // the menu: only the CURVES tab's point (CURVES_KNOB_X / _Y)
        if (!synth_ui_on_curves_tab(ui, rack)) return;
        const int kx = CURVES_KNOB_X - CTL_COL_KNOB_0, ky = CURVES_KNOB_Y - CTL_COL_KNOB_0;
        if (kx >= 0 && kx < SYNTH_UI_COL_KNOBS) { snprintf(cell[kx].label, sizeof cell[kx].label, "Pt X"); cell[kx].state = 1; }
        if (ky >= 0 && ky < SYNTH_UI_COL_KNOBS) { snprintf(cell[ky].label, sizeof cell[ky].label, "Pt Y"); cell[ky].state = 1; }
        return;
    }
    page_t pg;
    get_page(ui, rack, ui->page, &pg);
    if (pg.graph == GRAPH_SEQ) {                                          // the step under the cursor: each knob must move before it writes
        static const char *const name[SYNTH_UI_COL_KNOBS] = {"Note", "Len", "Vel", "Prob"};
        for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
            snprintf(cell[k].label, sizeof cell[k].label, "%s", name[k]);
            cell[k].state = (int8_t)(ui->seq_knob_live >> k & 1);
        }
        return;
    }
    char val[24];
    for (int k = 0; k < SYNTH_UI_COL_KNOBS; k++) {
        float v;
        const int s = synth_ui_col_knob_state(ui, params, seq, rack, k, &v);
        if (s < 0) continue;
        snprintf(cell[k].label, sizeof cell[k].label, "%s", row_text(ui, params, seq, rack, &pg, k + 1, val, sizeof val));
        cell[k].state = (int8_t)s;
        cell[k].value = v;
        cell[k].knob = ui->knob_val[k] >= 0 ? (float)ui->knob_val[k] / (float)INPUT_VALUE_MAX : -1.0f;
    }
}

void synth_ui_draw(const synth_ui_t *ui, const synth_params_t *p, const seq_t *seq, const rack_t *rack, u8g2_t *g) {
    gui_style_t style;
    const int content_h = u8g2_GetDisplayHeight(g) - (ui_legend_shown(ui, rack, false) ? ui_legend_h(g) : 0);   // the knob legend's zone: the app draws it
    gui_style_init(&style, u8g2_GetDisplayWidth(g), content_h);
    const gui_style_t *st = &style;
    page_t pg;
    char buf[32], val[24];

    u8g2_ClearBuffer(g);
    gui_begin(g, st);

    gui_rect_t screen = gui_inset(gui_rect(0, 0, u8g2_GetDisplayWidth(g), content_h), st->margin);
    int row_h = gui_row_h(g, st);
    gui_rect_t bar = gui_take_top(&screen, GUI_BAR_H);                     // the top bar: one 16 px grid row, its last pixel row is the rule
    const gui_rect_t header = gui_rect(bar.x, bar.y, bar.w, bar.h - 1);

    // header (row 0): page selector with arrows, or the menu tab while the menu is open (with its 16/tab_<name>.png icon at the left)
    const gui_sprite_t *icon = NULL;
    if (ui->in_rack) {
        const tab_t tk = tab_kind(rack, ui->menu_tab);
        snprintf(buf, sizeof buf, "< %s >", tab_name(tk));
        icon = ui_sprite(tab_icon(tk));
    } else {
        get_page(ui, rack, ui->page, &pg);
        snprintf(buf, sizeof buf, "< %s >", pg.title);
    }
    u8g2_DrawHLine(g, bar.x, gui_bottom(bar) - 1, bar.w);
    if (ui->row == 0) { u8g2_DrawBox(g, header.x, header.y, header.w, header.h); u8g2_SetDrawColor(g, 0); }
    if (icon) gui_draw_sprite(g, icon, bar.x, bar.y);                   // drawn over the rule: keep the icon's last row transparent
    gui_draw_text_centered(g, icon ? gui_rect(header.x + icon->w, header.y, header.w - icon->w, header.h) : header, buf);
    if (!ui->in_rack && ui->page_count > 1) {   // where the page is in the list: "3/22" at the left
        char n[24];
        snprintf(n, sizeof n, "%d/%d", ui->page + 1, ui->page_count);
        u8g2_DrawStr(g, header.x + 1, gui_text_center(g, header, n).y, n);
    }
    const int used = ui_header_marks(g, header.x + header.w, header.y, header.h, ui);   // the battery, the modifier held
    if (ui->joy_xy) {                            // joystick XY mode is on: 8/ui_xy.png left of the marks (text when there is no image)
        const gui_sprite_t *xy = ui_sprite("8/ui_xy");
        if (xy) gui_draw_sprite(g, xy, header.x + header.w - used - xy->w, bar.y + (GUI_BAR_H - xy->h) / 2);
        else u8g2_DrawStr(g, header.x + header.w - used - u8g2_GetStrWidth(g, "XY"), gui_text_center(g, header, "XY").y, "XY");
    }
    u8g2_SetDrawColor(g, 1);

    if (ui->in_rack) {                           // every tab of the menu is a declarative screen (ui_screen.h)
        const ui_ctx_t ctx = {(synth_ui_t *)ui, (rack_t *)rack, 0};         // drawing only reads
        screen_draw(screen_for_tab(tab_kind(rack, ui->menu_tab)), g, st, &ctx, screen);
        return;
    }
    if (pg.graph == GRAPH_SEQ) {
        draw_seq(g, st, screen, ui, seq);
        return;
    }
    if (pg.graph == GRAPH_MS_STEPS) {
        draw_ms_steps(g, st, screen, ui, seq, rack, pg.slot);
        return;
    }
    if (pg.graph == GRAPH_EG) {
        gui_rect_t l3 = gui_take_left(&screen, st->list_w);
        draw_eg_page(g, st, l3, gui_rect(st->graph.x, st->graph.y, st->graph.w, st->graph.h), ui, &rack->slot[pg.slot]);
        return;
    }
    if (pg.graph == GRAPH_MS_LANE) {
        gui_rect_t l2 = gui_take_left(&screen, st->list_w);
        draw_ms_lane(g, st, l2, gui_rect(st->graph.x, st->graph.y, st->graph.w, st->graph.h), ui, seq, rack, pg.slot);
        return;
    }

    // left: parameter rows, right: graph box. A module's page (on the 8 px grid): the rows in 8 px rows on the left, the icon / infos (and on its
    // first page the cog) on the right (draw_module_head), the graph across the full width at the bottom.
    const rack_slot_t *ms = pg.slot != GLOBAL_PAGE ? &rack->slot[pg.slot] : NULL;
    gui_rect_t list, box;
    int row_gap = st->gap, row_top = st->list_top;
    if (ms) {
        const gui_rect_t top = gui_take_top(&screen, 6 * GUI_GRID);
        list = gui_rect(top.x, top.y, top.w / 2, top.h);
        draw_module_head(g, st, ui, rack, &pg, gui_rect(top.x + top.w / 2, top.y, top.w / 2, top.h));
        box = screen;
        row_h = GUI_GRID; row_gap = 0; row_top = 0;
    } else {
        list = gui_take_left(&screen, st->list_w);
        box = gui_rect(st->graph.x, st->graph.y, st->graph.w, st->graph.h);
    }

    gui_rect_t row = gui_rect(list.x, list.y + row_top, list.w - 1, row_h);
    for (int i = 0; i < pg.count; i++) {
        bool sel = ui->row == i + 1;
        const char *label = row_text(ui, p, seq, rack, &pg, i + 1, val, sizeof val);
        gui_draw_field(g, st, row, label, val, sel);
        // Catch indicator: a 3x2 px chevron at the right edge of the row when the col knob is not yet caught.
        // Not while a modifier sends the knob to its Shift / Mod target: the popup shows that catch state.
        if (i < SYNTH_UI_COL_KNOBS && !(ui->knob_away & (1 << i))) {
            const int8_t cd = ui->knob_catch_dir[i];
            if (cd != 0) {
                const int ax = gui_right(row) - 1;      // right edge of the row (inside the list width)
                const int ay = row.y + row.h / 2 - 1;  // vertically centred, 2 px tall
                if (sel) u8g2_SetDrawColor(g, 0); else u8g2_SetDrawColor(g, 1);
                if (cd > 0) {                           // ► knob needs to go higher (clockwise on HWV1: right)
                    u8g2_DrawPixel(g, ax - 1, ay + 1);
                    u8g2_DrawPixel(g, ax - 1, ay);   u8g2_DrawPixel(g, ax,     ay);
                    u8g2_DrawPixel(g, ax - 1, ay - 1);
                    
                } else {                                // ◄ knob needs to go lower (counter-clockwise: left)
                    
                    u8g2_DrawPixel(g, ax, ay + 1);   
                    u8g2_DrawPixel(g, ax, ay);u8g2_DrawPixel(g, ax,     ay - 1);
                    u8g2_DrawPixel(g, ax, ay - 1);
                }
                u8g2_SetDrawColor(g, 1);
            }
        }
        row = gui_below(row, row_gap, row_h);
    }

    // graph of the current page
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    switch (pg.graph) {
        case GRAPH_WAVE:
            if (ms->type == MOD_LFO) {
                static const int lfo_wave[4] = {WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW_DOWN, WAVE_PULSE};
                draw_wave(g, box, lfo_wave[(int)ms->v[MP_LF_SHAPE]], 0.5f, st->wave_cycles);
            } else {
                if (ms->v[MP_OC_WAVE] >= OC_FIRST_MI) {
                    char nm[16];
                    rack_mparam_format(ms, MP_OC_WAVE, nm, sizeof nm);
                    draw_mi_preview(g, box, (int)ms->v[MP_OC_WAVE] - OC_FIRST_MI, ms->v[MP_OC_PW], ms->v[MP_OC_MORPH], ms->v[MP_OC_HARM], st->wave_cycles, nm);
                } else if (ms->v[MP_OC_WAVE] >= OC_FIRST_ENGINE) draw_engine_preview(g, box, (int)ms->v[MP_OC_WAVE] - OC_FIRST_ENGINE, ms->v[MP_OC_PW], ms->v[MP_OC_MORPH], st->wave_cycles);
                else draw_wave(g, box, (int)ms->v[MP_OC_WAVE], ms->v[MP_OC_PW], st->wave_cycles);
            }
            break;
        case GRAPH_ENV:     { env_params_t e = module_env(ms); draw_env(g, box, &e); } break;
        case GRAPH_EG_REL:  draw_eg_graph(g, box, ms, -1); break;
        case GRAPH_COMB:    draw_comb(g, box, ms->v[MP_RS_FB], ms->v[MP_RS_MIX]); break;
        case GRAPH_FX:      draw_fx_module(g, box, ms); break;
        case GRAPH_SAMPLE: {
            audio_sample_info_t in;
            const bool have = audio_sample_info((int)ms->v[MP_SM_FILE] - 1, &in);
            draw_sample_graph(g, box, have ? &in : NULL, (int)ms->v[MP_SM_SLICE] - 1, ms->v[MP_SM_START], ms->v[MP_SM_LOOP] != 1.0f);
        } break;
        case GRAPH_FILTER:  draw_filter(g, box, (int)ms->v[MP_FL_TYPE], ms->v[MP_FL_CUT], ms->v[MP_FL_RES]); break;
        case GRAPH_SAT:     draw_sat(g, box, (int)ms->v[MP_SA_MODE], ms->v[MP_SA_DRIVE], ms->v[MP_SA_MIX]); break;
        case GRAPH_AMP_ENV: draw_env(g, box, &p->amp_env); break;
        case GRAPH_STR_OSC: {
            static const int wave[STRW_COUNT] = {WAVE_SAW_UP, WAVE_PULSE, WAVE_TRIANGLE};
            draw_wave(g, box, wave[p->str.wave < STRW_COUNT ? p->str.wave : 0], p->str.pw, st->wave_cycles);
        } break;
        case GRAPH_STR_LP:
            if (p->str.lp_on) draw_filter(g, box, FILT_LP, p->str.lp_cut, 0.7f);
            else gui_draw_text_centered(g, gui_rect(box.x, box.y + box.h / 2 - 4, box.w, 9), "off");
            break;
        case GRAPH_STR_FILTER: draw_filter(g, box, p->str.ftype, p->str.fcut, p->str.fres); break;
        case GRAPH_SEQ_CFG: draw_seq_cfg(g, st, box, seq); break;
        case GRAPH_SEQ_PAT: draw_seq_pat(g, st, box, ui, seq); break;
        case GRAPH_FM:      draw_algo(g, box, &rack->cfg.fm, -1); break;                                   // the operator tree
        case GRAPH_FM_OP:   draw_algo(g, box, &rack->cfg.fm, pg.def - GP_FM_OP_BASE); break;               // ... with this operator marked
        case GRAPH_FM_ENV:  draw_eg_editor(g, box, &rack->cfg.fm.op[pg.def - GP_FM_ENV_BASE], ui->fm_pt & 3); break;
        default: break;
    }
}
