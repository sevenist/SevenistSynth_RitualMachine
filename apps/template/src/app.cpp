// What the controls do, and the screen. Portable: the same file runs on the desktop and on the first prototype.
//
//   the four note rows     notes: bottom left = C3 (48), +1 per key to the right, +8 per row up
//   function keys 1 / 2    octave down / up
//   function keys 3 / 4    filter cutoff down / up
//   encoder B              filter cutoff
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
uint8_t held[KEY_ROWS][KEY_COLS];           // note + 1 a key started, so its release stops the same note after an octave change
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
    uint8_t &h = held[row][col];
    if (pressed) {
        const int note = 48 + 12 * octave + 8 * (KEY_ROWS - 1 - row) + col;
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
    gui_style_init(&style, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    dirty = true;
}

void app_event(const input_event_t &e) {
    if (e.ctl >= CTL_KEY_FIRST && e.ctl <= CTL_KEY_LAST && (e.kind == IN_PRESS || e.kind == IN_RELEASE)) {
        const int k = e.ctl - CTL_KEY_FIRST;
        key_event(k / KEY_COLS, k % KEY_COLS, e.kind == IN_PRESS);
    } else if (e.ctl == CTL_ENC_B && e.kind == IN_DELTA) {
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
