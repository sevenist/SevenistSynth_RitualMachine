// What the controls do, and the screen. Portable: the same file runs on the desktop and on the first prototype.
//
//   the note rows          notes: bottom left = C3 (48), +1 per key to the right, +8 per row up
//   function keys 1 / 2    octave down / up            (the function keys: the top row of the key matrix)
//   function keys 3 / 4    filter cutoff down / up
//   anything that turns    filter cutoff               (an encoder; a pot without end stops: the first prototype's seven)
//
// Every board has its own controls and lists them in a table (hal/hal_input.h, input_controls): an event names a control by its
// place in that table, and the table says what the control is (a key and where in the matrix, an encoder, a knob ...). This app
// reads the kind and the place only, so it runs on any board; to give one particular control a job, use its id from the board's
// header (framework/boards/hwv1/hwv1_controls.h, framework/boards/desktop/panel_controls.h).
//
// On the desktop the keys are the PC keyboard's block F1.., 1.., Q.., A.., Z.. (by position, whatever the keyboard's language), and
// the panel window's controls work with the mouse (framework/boards/desktop/input_sim.c).
#include "app.h"
#include <cstdio>
#include "synth.h"
#include "ui/gui.h"

namespace {
u8g2_t *g = nullptr;
gui_style_t style;
int octave = 0;                             // added to every key, in octaves
int sounding = 0;                           // keys held
constexpr int kRowsMax = 8, kColsMax = 8;   // the largest key matrix this app plays (a key outside it does nothing)
uint8_t held[kRowsMax][kColsMax];           // note + 1 a key started, so its release stops the same note after an octave change
const input_control_t *controls = nullptr;  // the board's table
int n_controls = 0;
int key_rows = 0;                           // rows of the board's key matrix: the last one is the lowest note row
bool dirty = true;                          // the screen shows something else than the state

void key_event(int row, int col, bool pressed) {
    if (row == 0) {                         // the function row
        if (!pressed) return;
        if (col == 0 && octave > -3) octave--;
        else if (col == 1 && octave < 3) octave++;
        else if (col == 2) synth_set_cutoff(synth_cutoff() - 4);
        else if (col == 3) synth_set_cutoff(synth_cutoff() + 4);
        return;
    }
    if (row >= kRowsMax || col >= kColsMax) return;
    uint8_t &h = held[row][col];
    if (pressed) {
        const int note = 48 + 12 * octave + 8 * (key_rows - 1 - row) + col;
        if (note < 0 || note > 127 || h) return;
        synth_note_on(note);
        h = static_cast<uint8_t>(note + 1);
        sounding++;
    } else if (h) {
        synth_note_off(h - 1);
        h = 0;
        sounding--;
    }
}
}  // namespace

void app_start(u8g2_t *display) {
    g = display;
    controls = input_controls(&n_controls);
    for (int i = 0; i < n_controls; i++)
        if (controls[i].kind == CK_KEY && controls[i].row >= key_rows) key_rows = controls[i].row + 1;
    gui_style_init(&style, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    dirty = true;
}

void app_event(const input_event_t &e) {
    if (e.ctl < 0 || e.ctl >= n_controls) return;
    const input_control_t &c = controls[e.ctl];         // what the board says about the control that spoke
    if (c.kind == CK_KEY && (e.kind == IN_PRESS || e.kind == IN_RELEASE)) {
        key_event(c.row, c.col, e.kind == IN_PRESS);
    } else if (e.kind == IN_DELTA) {                    // steps: an encoder, or an endless pot (it reports steps unless input_control_mode says a position)
        synth_set_cutoff(synth_cutoff() + 2 * e.value);
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
    const gui_rect_t bar = gui_take_top(&area, GUI_BAR_H);
    gui_draw_text_centered(g, bar, APP_NAME);
    u8g2_DrawHLine(g, 0, GUI_BAR_H - 1, static_cast<u8g2_uint_t>(bar.w));
    area = gui_inset(area, style.margin);
    char value[16];
    const int row_h = gui_row_h(g, &style) + style.gap;
    std::snprintf(value, sizeof value, "%+d", octave);
    gui_draw_field(g, &style, gui_take_top(&area, row_h), "Octave", value, false);
    std::snprintf(value, sizeof value, "%d", synth_cutoff());
    gui_draw_field(g, &style, gui_take_top(&area, row_h), "Cutoff", value, false);
    std::snprintf(value, sizeof value, "%d", sounding);
    gui_draw_field(g, &style, gui_take_top(&area, row_h), "Keys", value, false);
    display_send(g);
}
