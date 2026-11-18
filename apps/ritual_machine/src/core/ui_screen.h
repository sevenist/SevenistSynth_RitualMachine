#pragma once
// Declarative screens: a screen is a TABLE of elements. The same table drives the drawing, the focus navigation and the
// editing, so a screen can no longer disagree with itself about what "row 3" means.
//
// An element has a label, a kind, a position in a small grid (used for the joystick's spatial navigation) and callbacks:
//   value()     the text shown for it
//   adjust()    change it by one step (dir = +1 / -1)
//   activate()  do it (buttons)
//   draw()      only for elements that are not a simple field (the module strip of the rack screen)
// Where an element sits is either data, its `box` (widget geometry: absolute or relative position, size in pixels, percent, cells or text
// rows; below), or code: the screen's layout() callback gives every element a rectangle (below the strip, in a column on the left, in a
// grid inside the remaining area, ...). ui_layout_column() is the layout of the "list on the left, picture on the right" screens.
// The two mix: boxes are resolved after layout() and win for the elements that have one.
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
// params / seq: the synth's values, for a screen that drives them (the MACROS tab's Value); NULL on the Pads FX pages.
typedef struct { synth_ui_t *ui; rack_t *rack; int arg; synth_params_t *params; seq_t *seq; } ui_ctx_t;

typedef enum {
    EL_VALUE,       // a value: joystick moves the focus, push latches, then the joystick (or encoder B at any time) changes it
    EL_DIRECT,      // a value the joystick changes directly with left / right (no latch): a selector such as the slot strip; push runs activate() if set
    EL_BUTTON,      // an action: push activates it
} el_kind_t;

enum {
    EF_HIDE_WHEN_DISABLED = 1,           // do not draw the element at all while enabled() is false (default: drawn as "n/a")
    EF_STATIC = 2,                       // a widget that only shows (a label, a picture): the focus and the navigation skip it
    EF_SCROLL = 4,                       // one of the rows of a list: those move up together so the focused one is in view (no scroll state:
                                         // it is worked out from the focus), and a row moved above where the list starts is not drawn
};

// Widget geometry: where an element sits and how big it is, as plain data. All zero = not placed by data: the screen's layout() gives it
// its rectangle, as before. An element with a `place` is positioned by ui_layout_boxes(), after layout() when there is one (so a screen can
// place some elements by data and the others by code), and may only refer to elements before it in the table.
//   PL_IN         absolute: the top-left corner is x, y from the corner of the parent (the screen's area, or the element `ref`)
//   PL_BELOW etc  relative: next to the element `ref` (0 = the one before it in the table); x, y move it from there (the gap)
// Percentages are of the parent (PL_IN: the ref element, or the area) or, for the relative places, of the area.
typedef enum { UN_PX, UN_PCT, UN_CELL, UN_FILL, UN_ROW, UN_GAP, UN_SAME } ui_unit_t;
//   UN_PX   pixels               UN_PCT   v % of the parent       UN_CELL  v cells of the 8 px grid
//   UN_FILL to the parent's far edge, less v px                    UN_ROW   v text rows (the style's row height)
//   UN_GAP  v style gaps         UN_SAME  as the ref element (a size)
typedef struct { int16_t v; uint8_t unit; int8_t adj; } ui_len_t;      // the length is v in `unit`, plus adj pixels
typedef enum { PL_NONE, PL_IN, PL_BELOW, PL_ABOVE, PL_RIGHT_OF, PL_LEFT_OF } ui_place_t;
#define UI_REF(i) ((uint8_t)((i) + 1))   // `ref` of a box: the element i of the table (0 = the default: the area for PL_IN, the previous element otherwise)
typedef struct { uint8_t place, ref; ui_len_t x, y, w, h; } ui_box_t;

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
    ui_box_t    box;                                              // where it sits (all zero: the screen's layout() places it)
} el_def_t;

typedef struct {
    const el_def_t *el;
    int             n;
    bool            compact;                                      // on a short screen (under 100 px) draw with padding 0 and gap 0 so more rows fit
    bool            use_latch;                                    // joystick push latches EL_VALUE elements so L/R edits them (for dense multi-column screens like the rack)
    // gives every element its rectangle, inside `area` (what is left under the header). NULL: every element has a `box`; with both, the
    // boxes are resolved after it and win for the elements that have one
    void (*layout)(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect);
    bool (*back)(const ui_ctx_t *c);                              // the Back button (NULL: nothing)
    void (*after_edit)(const ui_ctx_t *c);                        // called after anything changed (keep the scroll in view, mark the rack dirty...)
    void (*draw_extra)(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect);   // drawn before the elements (pictures, connections)
    // The joystick's move from element `cur` (-1: the header) towards 'l', 'r', 'u' or 'd': the element it lands on, -1 for the header, `cur` when
    // there is nowhere to go. NULL: the grid of the elements' row / col fields (screens whose elements are not at fixed cells, such as the MAP)
    int (*move)(const ui_ctx_t *c, int cur, char dir);
    void (*header_push)(const ui_ctx_t *c);                       // a push while the focus is on the header (NULL: nothing; the map: its Add menu)
} screen_def_t;

// Element 0..n-1 is focused when ui->row == index + 1; ui->row == 0 is the header (page / tab selector).
// Handles one UI event on the screen. Returns true when a sound value changed. Left / right on the header change the menu tab.
bool screen_event(const screen_def_t *s, const ui_ctx_t *c, ui_event_t e);

void screen_draw(const screen_def_t *s, u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area);

// Resolves the `box` of every element of `el` (n of them) into rect[i], inside `area`; the elements without a box keep their rect.
void ui_layout_boxes(u8g2_t *g, const gui_style_t *st, gui_rect_t area, const el_def_t *el, int n, gui_rect_t *rect);

// Layout of the "list on the left, picture on the right" screens: the elements in one column of rows, in table order, in the list
// column of the style. The picture box is `ui_picture_box(st)`.
void ui_layout_column(u8g2_t *g, const gui_style_t *st, gui_rect_t area, int n, gui_rect_t *rect);
gui_rect_t ui_picture_box(const gui_style_t *st);

// The rack's cell menu (scr_rack.c), opened on the map's nodes too (scr_map.c): its items (RM_*: their element numbers in scr_rack.c), the cell
// it acts on (scr_rack_select: a module's slot, RACK_SEL_PLUS for the + at the end of a row, RACK_SEL_MIX), and each item by number (c->arg).
enum { RM_EDIT = 3, RM_TYPE, RM_INSERT, RM_TGT, RM_PRM, RM_DPTH, RM_ROW, RM_PARA, RM_PENV, RM_LVL1, RM_PAN1, RM_LVL2, RM_PAN2, RM_DELETE, RM_END };
#define RACK_SEL_PLUS (-1)
#define RACK_SEL_MIX  (-2)
void        scr_rack_select(synth_ui_t *ui, const rack_t *r, int row, int slot);
int         scr_rack_selected_slot(const ui_ctx_t *c);         // the module the menu acts on, -1: none (the +, the MIX)
int         scr_rack_insert_pos(const ui_ctx_t *c);            // where Insert puts a module
bool        scr_rack_item_enabled(const ui_ctx_t *c);
void        scr_rack_item_value(const ui_ctx_t *c, char *out, int n);
bool        scr_rack_item_adjust(const ui_ctx_t *c, int dir);
void        scr_rack_item_activate(const ui_ctx_t *c);
const char *scr_rack_item_label(int item);
el_kind_t   scr_rack_item_kind(int item);
void        scr_rack_menu_close(const ui_ctx_t *c);

// The screen of a menu tab.
const screen_def_t *screen_for_tab(tab_t t);
// The screen on show for a tab: the same, except where a tab has more than one (the MAP tab: the map, a node's page, the PICK list).
const screen_def_t *screen_shown(const synth_ui_t *ui, const rack_t *rack, tab_t t);

extern const screen_def_t scr_rack_screen, scr_sound_screen, scr_keys_screen,
                          scr_mods_screen, scr_macros_screen, scr_curves_screen, scr_leds_screen, scr_joy_screen, scr_preset_screen,
                          scr_machine_screen, scr_map_screen, scr_node_screen, scr_pick_screen;

// The SOUND tab's synth setting (cfg_param_id_t, the rows that were GENERAL) on focus row `row` (ui->row), -1 for none.
int scr_general_setting(int row);
int scr_machine_setting(int row);        // the same for the MACHINE tab (Out, Spk, Knob)

#ifdef __cplusplus
}
#endif
