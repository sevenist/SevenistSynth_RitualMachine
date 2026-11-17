#pragma once
// The node GUI's widgets (apps/ritual_machine/docs/GUI_NODES.md; the look of apps/gui_mockup): what the MAP, NODE and PICK screens are made of.
// Each draws itself in a rectangle it is given (a screen's element rect: ui_screen.h places it, by data or by layout()), in colour on a colour
// board (core/ui_theme.h) and as a high-contrast one-colour replacement elsewhere, saying the same thing:
//
//   widget        colour                                              one colour
//   tile          tile fill, the family's band at the left, a value   a frame (a modulator: corners cut), the value as a 1 px line; the
//                 bar under the name; the cursor a white outline       cursor fills it
//   cable         audio solid text colour, modulation dashed amber,  audio solid, modulation and gate dotted
//                 gate dotted magenta
//   chip          a framed label in a colour                          a framed label
//   title         a band in the family's colour                       a filled band, the text cut out
//   tabs          the selected one filled                             the same
//   value row     the value as a bar in the family's dim colour       the field as gui_draw_field_state, the value as a 1 px line under it;
//                 behind the text; modulated: an amber line under it  modulated: "~" after the label
//   port row      the focused one on a highlight, a modulation in     the focused one filled
//                 amber
//   list row      a family band, the focused one on a highlight       a band, the focused one filled
//   panel         a strip's background                                a rule on its top
// Every widget leaves the ink at UC_TEXT and the style's font.
#include <stdbool.h>
#include "ui/gui.h"
#include "core/ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { UW_CABLE_AUDIO, UW_CABLE_MOD, UW_CABLE_GATE } uw_cable_t;

void uw_line(u8g2_t *g, int x0, int y0, int x1, int y1, uw_cable_t k);         // a horizontal or vertical cable segment, ends included
void uw_tile(u8g2_t *g, gui_rect_t r, const char *name, ui_colour_t family, float bar, bool focused, bool modulator);   // bar < 0: none
void uw_chip(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *text, ui_colour_t c);
int  uw_chip_w(u8g2_t *g, const gui_style_t *st, const char *text);             // the width a chip needs for its text
void uw_title(u8g2_t *g, gui_rect_t r, const char *left, const char *right, ui_colour_t family);
void uw_tabs(u8g2_t *g, gui_rect_t r, const char *const *names, int n, int sel, bool focused);
void uw_value_row(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *label, const char *value, float frac, ui_colour_t family,
                  gui_state_t state, bool modulated);                            // frac < 0: no bar (a list value)
void uw_port_row(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *left, const char *right, ui_colour_t right_c, bool focused);
void uw_list_row(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *name, const char *info, ui_colour_t family, bool focused);
void uw_panel(u8g2_t *g, gui_rect_t r);
void uw_text(u8g2_t *g, int x, int baseline, const char *s, ui_colour_t c);      // text in a colour (one colour: plain), transparent over fills

#ifdef __cplusplus
}
#endif
