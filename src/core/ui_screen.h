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
// (below the strip, in a column on the left, in a grid inside the remaining area, ...). ui_layout_column() is the layout of
// the "list on the left, picture on the right" screens.
//
// Interaction model (see screen_event):
//   joystick        moves the focus; on a DIRECT element (the module strip) left / right change it; on the header they change the tab
//   joystick push   LATCHES the focused value element: the joystick then changes the value; push again releases. On a button it activates it.
//   encoder A       previous / next element
//   encoder B       changes the focused value without a latch; its push activates a button
//
// Screens are written in scr_*.c, each exporting one screen_def_t, and are found by menu tab through screen_for_tab().
#include <stdbool.h>
#include "core/ui_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// What every callback receives. `arg` is the `arg` of the element the callback belongs to (so one function can serve a family of rows).
typedef struct { synth_ui_t *ui; rack_t *rack; int arg; } ui_ctx_t;

typedef enum {
    EL_VALUE,       // a value: joystick moves the focus, push latches, then the joystick (or encoder B at any time) changes it
    EL_DIRECT,      // a value the joystick changes directly with left / right (no latch): a selector such as the slot strip; push runs activate() if set
    EL_BUTTON,      // an action: push activates it
} el_kind_t;

enum { EF_HIDE_WHEN_DISABLED = 1 };      // do not draw the element at all while enabled() is false (default: drawn as "n/a")

typedef struct {
    const char *label;
    el_kind_t   kind;
    uint8_t     row, col, span;                                   // position in the navigation grid; span = columns covered
    void (*value)(const ui_ctx_t *c, char *out, int n);           // text of the value (NULL: none); a button with a value shows it as its caption
    bool (*adjust)(const ui_ctx_t *c, int dir);                   // true when a sound value changed
    void (*activate)(const ui_ctx_t *c);                          // buttons
    void (*draw)(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched);   // custom drawing (NULL: a field / button)
    bool (*enabled)(const ui_ctx_t *c);                           // NULL = always; a disabled element is drawn as n/a and skipped by the focus
    const char *(*label_dyn)(const ui_ctx_t *c);                  // NULL = `label`; otherwise the label depends on the state (an effect's parameter names)
    int         arg;                                              // handed to the callbacks as ctx->arg
    uint8_t     flags;                                            // EF_*
} el_def_t;

typedef struct {
    const el_def_t *el;
    int             n;
    bool            compact;                                      // on a short screen (under 100 px) draw with padding 0 and gap 0 so more rows fit
    bool            use_latch;                                    // joystick push latches EL_VALUE elements so L/R edits them (for dense multi-column screens like the rack)
    // gives every element its rectangle, inside `area` (what is left under the header)
    void (*layout)(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect);
    bool (*back)(const ui_ctx_t *c);                              // the Back button (NULL: nothing)
    void (*after_edit)(const ui_ctx_t *c);                        // called after anything changed (keep the scroll in view, mark the rack dirty...)
    void (*draw_extra)(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect);   // drawn before the elements (pictures, connections)
} screen_def_t;

// Element 0..n-1 is focused when ui->row == index + 1; ui->row == 0 is the header (page / tab selector).
// Handles one UI event on the screen. Returns true when a sound value changed. Left / right on the header change the menu tab.
bool screen_event(const screen_def_t *s, const ui_ctx_t *c, ui_event_t e);

void screen_draw(const screen_def_t *s, u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area);

// Layout of the "list on the left, picture on the right" screens: the elements in one column of rows, in table order, in the list
// column of the style. The picture box is `ui_picture_box(st)`.
void ui_layout_column(u8g2_t *g, const gui_style_t *st, gui_rect_t area, int n, gui_rect_t *rect);
gui_rect_t ui_picture_box(const gui_style_t *st);

// The screen of a menu tab.
const screen_def_t *screen_for_tab(tab_t t);

extern const screen_def_t scr_rack_screen, scr_general_screen, scr_samples_screen, scr_fx_screen, scr_fm_algo_screen, scr_keys_screen,
                          scr_mods_screen, scr_macros_screen, scr_curves_screen, scr_leds_screen;

// The GENERAL tab's setting (cfg_param_id_t) on focus row `row` (ui->row), -1 for none.
int scr_general_setting(int row);

#ifdef __cplusplus
}
#endif
