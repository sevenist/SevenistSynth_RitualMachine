#pragma once
// Declarative screens: a screen is a TABLE of elements. The same table drives the drawing, the focus navigation and the
// editing, so a screen can no longer disagree with itself about what "row 3" means.
//
// An element has a label, a kind, a position in a small grid (used for the joystick's spatial navigation) and callbacks:
//   value()     the text shown for it
//   adjust()    change it by one step (dir = +1 / -1)
//   activate()  do it (buttons)
//   draw()      only for elements that are not a simple field (the module strip of the rack screen)
// The screen supplies one more callback, layout(), that gives every element a rectangle: that is where the anchors live
// (below the strip, in a 2 x 3 grid inside the remaining area, ...).
//
// Interaction model (see screen_event):
//   joystick        moves the focus; on a DIRECT element (the module strip, the page / tab selector) left / right change it
//   joystick push   LATCHES the focused value element: the joystick then changes the value; push again releases. On a button it activates it.
//   encoder A       previous / next element
//   encoder B       changes the focused value without a latch; its push activates a button
#include <stdbool.h>
#include "core/ui_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { synth_ui_t *ui; rack_t *rack; } ui_ctx_t;

typedef enum {
    EL_VALUE,       // a value: joystick moves the focus, push latches, then the joystick (or encoder B at any time) changes it
    EL_DIRECT,      // a value the joystick changes directly with left / right (no latch): a selector such as the slot strip
    EL_BUTTON,      // an action: push activates it
} el_kind_t;

typedef struct {
    const char *label;
    el_kind_t   kind;
    uint8_t     row, col, span;                                   // position in the navigation grid; span = columns covered
    void (*value)(const ui_ctx_t *c, char *out, int n);           // text of the value (NULL: none)
    bool (*adjust)(const ui_ctx_t *c, int dir);                   // true when a sound value changed
    void (*activate)(const ui_ctx_t *c);                          // buttons
    void (*draw)(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched);   // custom drawing (NULL: a field / button)
    bool (*enabled)(const ui_ctx_t *c);                           // NULL = always; a disabled element is drawn dimmed and skipped by the focus
} el_def_t;

typedef struct {
    const el_def_t *el;
    int             n;
    // gives every element its rectangle, inside `area` (what is left under the header)
    void (*layout)(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect);
    bool (*back)(const ui_ctx_t *c);                              // the Back button (NULL: nothing)
    void (*after_edit)(const ui_ctx_t *c);                        // called after anything changed (keep the scroll in view, mark the rack dirty...)
    void (*draw_extra)(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect);   // drawn before the elements (connections, info line)
} screen_def_t;

// Element 0..n-1 is focused when ui->row == index + 1; ui->row == 0 is the header (page / tab selector).
// Handles one UI event on the screen. Returns true when a sound value changed. `header_step` is called for left / right
// on the header (changing the tab or the page).
bool screen_event(const screen_def_t *s, const ui_ctx_t *c, ui_event_t e, void (*header_step)(const ui_ctx_t *c, int dir));

void screen_draw(const screen_def_t *s, u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area);

#ifdef __cplusplus
}
#endif
