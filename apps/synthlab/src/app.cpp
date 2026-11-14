// SynthLab's controls and screen. The screen is one list: the synth, then its values. Portable: the same file runs on the desktop and on
// the board.
//
//   encoder A              the row (push: back to the top, the synth); it steps over the sections' titles
//   encoder B              the row's value: the synth, or the value (push: its default)
//   the note rows          notes: bottom left = C3 (48), +1 per key to the right, +1 row = + the row's length
//   function keys 1 / 2    octave down / up            (the function keys: the top row of the key matrix)
//   function keys 3 / 4    the previous / next synth
//   PLAY                   holds the last note played (a drone, to turn values while it sounds); again: lets it go. A synth with a
//                          tempo (lab::Options::play_bpm, the Kick: 135) loops it instead, a hit every beat; a key changes its note
//                          (a synth with grooves, DrumLab: its Groove at its Tempo)
//   VOLUME                 the master volume
//
// The board's controls are found in its table (hal/hal_input.h), not by id: encoder A and B are the first two encoders (the first two
// endless pots on a board without encoders), their pushes the buttons at the same place, PLAY and VOLUME by name. So the app runs on
// any board of the library.
#include "app.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "lab.h"
#include "synth.h"
#include "ui/gui.h"

namespace {
u8g2_t *g = nullptr;
gui_style_t style;
const input_control_t *controls = nullptr;  // the board's table
int n_controls = 0;
int key_rows = 0, key_cols = 0;             // the board's key matrix: row 0 is the function row, the last the lowest notes
int enc_a = INPUT_NO_CONTROL, enc_b = INPUT_NO_CONTROL, push_a = INPUT_NO_CONTROL, push_b = INPUT_NO_CONTROL;
int play_btn = INPUT_NO_CONTROL, volume_knob = INPUT_NO_CONTROL;

constexpr int kRowsMax = 8, kColsMax = 8;   // the largest key matrix this app plays (a key outside it does nothing)
uint8_t held[kRowsMax][kColsMax];           // note + 1 a key started, so its release stops the same note after an octave change
int octave = 0;
int last_note = 48;                         // what PLAY holds
int hold_note = -1;                         // the note PLAY holds, -1 = none
int row = 0;                                // the selected row: 0 = the synth, 1.. = its values
int top = 0;                                // the first row on screen
bool dirty = true;                          // the screen shows something else than the state

const lab::Synth &cur() { return *lab::kSynths[synth_selected()]; }
int row_count() { return 1 + cur().n_params; }
bool is_title(int r) { return r > 0 && r < row_count() && cur().params[r - 1].curve == lab::Head; }

// Encoder A: `detents` rows that can be selected (titles do not count), stopping at both ends.
void move_row(int detents) {
    const int dir = detents > 0 ? 1 : -1;
    for (int k = 0; k < (detents > 0 ? detents : -detents); k++) {
        int r = row + dir;
        while (is_title(r)) r += dir;
        if (r < 0 || r >= row_count()) break;
        row = r;
    }
}

int find_control(int kind, int nth) {
    for (int i = 0; i < n_controls; i++)
        if (controls[i].kind == kind && nth-- == 0) return i;
    return INPUT_NO_CONTROL;
}
int find_push(int ctl) {                    // the button at the same place as `ctl`: its push
    if (ctl == INPUT_NO_CONTROL) return INPUT_NO_CONTROL;
    for (int i = 0; i < n_controls; i++)
        if (controls[i].kind == CK_BUTTON && controls[i].px == controls[ctl].px && controls[i].py == controls[ctl].py) return i;
    return INPUT_NO_CONTROL;
}
int find_named(const char *name) {
    for (int i = 0; i < n_controls; i++)
        if (std::strcmp(controls[i].name, name) == 0) return i;
    return INPUT_NO_CONTROL;
}

// A value moved by `detents` of the encoder, as its curve says (lab.h, Curve).
float step_value(const lab::Param &p, float v, int detents) {
    switch (p.curve) {
    case lab::Int: return std::floor(v + 0.5f) + static_cast<float>(detents);
    case lab::Exp:
        if (p.min > 0.f) return v * std::pow(p.max / p.min, static_cast<float>(detents) / 100.f);
        [[fallthrough]];
    default: {
        const float step = (p.max - p.min) / 100.f;
        return p.min + step * std::floor((v - p.min) / step + 0.5f + static_cast<float>(detents));   // (snapped: no drift)
    }
    }
}

// The value as the screen writes it: a label, or a number with as many digits as fit, then the unit.
void format_value(const lab::Param &p, float v, char *out, size_t size) {
    if (p.curve == lab::Int) {
        const int i = static_cast<int>(std::floor(v + 0.5f));
        if (p.labels) std::snprintf(out, size, "%s", p.labels[i - static_cast<int>(p.min)]);
        else std::snprintf(out, size, "%d%s%s", i, p.unit ? " " : "", p.unit ? p.unit : "");
        return;
    }
    const char *unit = p.unit;
    if (unit && std::strcmp(unit, "ms") == 0 && std::fabs(v) >= 1000.f) { v /= 1000.f; unit = "s"; }    // 1.20 s, not 1.20k ms
    const float a = std::fabs(v);
    char num[16];
    if (a < 1000.f && std::fabs(v - std::floor(v + 0.5f)) < 0.005f) std::snprintf(num, sizeof num, "%d", static_cast<int>(std::floor(v + 0.5f)));
    else if (a >= 10000.f) std::snprintf(num, sizeof num, "%.1fk", v / 1000.f);
    else if (a >= 1000.f) std::snprintf(num, sizeof num, "%.2fk", v / 1000.f);
    else if (a >= 100.f) std::snprintf(num, sizeof num, "%.0f", v);
    else if (a >= 10.f) std::snprintf(num, sizeof num, "%.1f", v);
    else std::snprintf(num, sizeof num, "%.2f", v);
    std::snprintf(out, size, "%s%s%s", num, unit ? " " : "", unit ? unit : "");
}

void release_hold() {
    if (hold_note >= 0) synth_note_off(hold_note);
    hold_note = -1;
}

void select_synth(int index) {                      // (beyond the ends of the list: the first or the last synth)
    index = index < 0 ? 0 : (index >= lab::kSynthCount ? lab::kSynthCount - 1 : index);
    if (index == synth_selected()) return;
    hold_note = -1;                                 // (synth_select lets every note go)
    std::memset(held, 0, sizeof held);
    synth_select(index);
    if (row >= row_count()) row = row_count() - 1;
    while (is_title(row)) row--;                    // (row 0, the synth, is never a title)
}

void edit(int detents) {
    if (row == 0) { select_synth(synth_selected() + detents); return; }
    if (is_title(row)) return;
    const lab::Param &p = cur().params[row - 1];
    synth_set_value(row - 1, step_value(p, synth_value(row - 1), detents));
}

void key_event(int r, int c, bool pressed) {
    if (r == 0) {                                   // the function row
        if (!pressed) return;
        if (c == 0 && octave > -3) octave--;
        else if (c == 1 && octave < 3) octave++;
        else if (c == 2) select_synth(synth_selected() - 1);
        else if (c == 3) select_synth(synth_selected() + 1);
        return;
    }
    if (r >= kRowsMax || c >= kColsMax) return;
    uint8_t &h = held[r][c];
    if (pressed) {
        const int note = 48 + 12 * octave + key_cols * (key_rows - 1 - r) + c;
        if (note < 0 || note > 127 || h) return;
        synth_note_on(note);
        h = static_cast<uint8_t>(note + 1);
        last_note = note;
        synth_loop_note(note);                      // (a loop that runs plays it from its next hit)
    } else if (h) {
        if (h - 1 != hold_note) synth_note_off(h - 1);     // (the note PLAY holds keeps sounding)
        h = 0;
    }
}
}  // namespace

void app_start(u8g2_t *display) {
    g = display;
    controls = input_controls(&n_controls);
    for (int i = 0; i < n_controls; i++) {
        if (controls[i].kind != CK_KEY) continue;
        if (controls[i].row >= key_rows) key_rows = controls[i].row + 1;
        if (controls[i].col >= key_cols) key_cols = controls[i].col + 1;
    }
    enc_a = find_control(CK_ENCODER, 0);
    enc_b = find_control(CK_ENCODER, 1);
    if (enc_a == INPUT_NO_CONTROL) { enc_a = find_control(CK_ENDLESS, 0); enc_b = find_control(CK_ENDLESS, 1); }
    push_a = find_push(enc_a);
    push_b = find_push(enc_b);
    play_btn = find_named("PLAY");
    volume_knob = find_named("VOLUME");
    gui_style_init(&style, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    dirty = true;
}

void app_event(const input_event_t &e) {
    if (e.ctl < 0 || e.ctl >= n_controls) return;
    const input_control_t &c = controls[e.ctl];
    if (c.kind == CK_KEY && (e.kind == IN_PRESS || e.kind == IN_RELEASE)) {
        key_event(c.row, c.col, e.kind == IN_PRESS);
    } else if (e.kind == IN_DELTA && e.ctl == enc_a) {
        move_row(e.value);
    } else if (e.kind == IN_DELTA && e.ctl == enc_b) {
        edit(e.value);
    } else if (e.kind == IN_PRESS && e.ctl == push_a) {
        row = 0;
    } else if (e.kind == IN_PRESS && e.ctl == push_b) {
        if (row > 0 && !is_title(row)) synth_set_value(row - 1, cur().params[row - 1].def);
    } else if (e.kind == IN_PRESS && e.ctl == play_btn) {
        if (cur().opt.play_bpm > 0) {               // the synth's loop
            if (synth_looping()) synth_loop_stop();
            else synth_loop_start(last_note, cur().opt.play_bpm);
        } else if (hold_note >= 0) {
            release_hold();
        } else {
            hold_note = last_note;
            synth_note_on(hold_note);
        }
    } else if (e.kind == IN_VALUE && e.ctl == volume_knob) {
        synth_set_volume(static_cast<float>(e.value) / INPUT_VALUE_MAX);
        return;                                     // (nothing on screen changes)
    } else {
        return;
    }
    dirty = true;
}

void app_draw() {
    if (!dirty || !g) return;
    dirty = false;
    u8g2_ClearBuffer(g);
    gui_begin(g, &style);
    gui_rect_t area = gui_screen(g);

    // the bar: the app, the octave, the held note
    char text[40];
    const gui_rect_t bar = gui_take_top(&area, GUI_BAR_H);
    gui_draw_text_left(g, &style, bar, "SynthLab");
    if (synth_looping()) std::snprintf(text, sizeof text, "%d BPM  oct %+d", synth_loop_bpm(), octave);
    else std::snprintf(text, sizeof text, "%soct %+d", hold_note >= 0 ? "HOLD  " : "", octave);
    gui_draw_text_right(g, &style, bar, text);
    u8g2_DrawHLine(g, 0, GUI_BAR_H - 1, static_cast<u8g2_uint_t>(bar.w));

    // the list: as many rows as fit, scrolled to keep the selected one in view
    area = gui_inset(area, style.margin);
    const int row_h = gui_row_h(g, &style) + style.gap;
    const int fit = area.h / row_h > 0 ? area.h / row_h : 1;
    const int rows = row_count();
    if (row < top) top = row;
    if (is_title(row - 1) && row - 1 < top) top = row - 1;      // a section's first value shows its title
    if (row >= top + fit) top = row - fit + 1;
    if (top > rows - fit) top = rows - fit > 0 ? rows - fit : 0;
    if (rows > fit) {                               // a scroll bar on the right
        const int bar_h = area.h * fit / rows, bar_y = area.y + (area.h - bar_h) * top / (rows - fit);
        u8g2_DrawVLine(g, static_cast<u8g2_uint_t>(gui_right(area) - 1), static_cast<u8g2_uint_t>(bar_y), static_cast<u8g2_uint_t>(bar_h));
        area.w -= 3;
    }
    for (int i = top; i < rows && i < top + fit; i++) {
        const gui_rect_t r = gui_take_top(&area, row_h);
        const gui_rect_t field = gui_rect(r.x, r.y, r.w, row_h - style.gap);
        if (i == 0) {
            gui_draw_field(g, &style, field, "Synth", cur().name, i == row);
            continue;
        }
        const lab::Param &p = cur().params[i - 1];
        if (p.curve == lab::Head) {                 // a section's title: centred between two rules
            const int tw = gui_text_w(g, p.name), mid = field.y + field.h / 2, gap = 3;
            const int tx = field.x + (field.w - tw) / 2;
            gui_draw_text_centered(g, field, p.name);
            if (tx - gap > field.x) u8g2_DrawHLine(g, static_cast<u8g2_uint_t>(field.x), static_cast<u8g2_uint_t>(mid), static_cast<u8g2_uint_t>(tx - gap - field.x));
            if (tx + tw + gap < gui_right(field)) u8g2_DrawHLine(g, static_cast<u8g2_uint_t>(tx + tw + gap), static_cast<u8g2_uint_t>(mid), static_cast<u8g2_uint_t>(gui_right(field) - tx - tw - gap));
            continue;
        }
        format_value(p, synth_value(i - 1), text, sizeof text);
        gui_draw_field(g, &style, field, p.name, text, i == row);
    }
    display_send(g);
}
