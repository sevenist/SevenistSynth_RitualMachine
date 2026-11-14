#pragma once
// Widgets for a pointer (a mouse) and a full keyboard: the drawing kit's controls for a screen that is pointed at, not only stepped
// through with encoders. Immediate mode: every frame the app calls the widgets it shows, in the order the keyboard visits them; each
// one draws itself on a colour canvas (ui/canvas.h) and says what the user did to it. Nothing is kept per widget but its id.
//
//   wg_begin(&ctx, &input);                                 // the frame's input (the desktop: boards/desktop/desk_window.h)
//   if (wg_button(&ctx, ID_PLAY, r, "PLAY", playing)) ...   // clicked, or Enter / Space while it has the focus
//   if (wg_knob(&ctx, ID_CUT, r, &v, &k)) ...               // dragged up / down, wheel, arrows; a double click resets it
//   wg_end(&ctx);
//
// The pointer: a press on a widget makes it ACTIVE (it takes the drags until the release) and gives it the FOCUS. The keyboard: Tab /
// Shift+Tab move the focus through the widgets in the order they were called; the focused one takes the arrows, Page Up / Down, Home /
// End, Delete (its default), Enter / Space. Esc drops the focus. Shift makes every step finer.
// Drag and drop: a widget starts one with wg_drag_source (a payload: a kind and a value); a drop target takes it when it is let go
// over it (wg_drop_target). Ids: any int but 0, unique in a frame (WG_ID builds one from a family and an index).
// Portable C (u8g2 and the canvas only): a board with a touch screen fills the same wg_input_t.
#include <stdbool.h>
#include <stdint.h>
#include "ui/canvas.h"
#include "ui/gui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- input: one frame's worth ---------------- */

enum { WG_BTN_LEFT = 1, WG_BTN_RIGHT = 2, WG_BTN_MIDDLE = 4 };
enum { WG_MOD_SHIFT = 1, WG_MOD_CTRL = 2, WG_MOD_ALT = 4 };

// Keys: below 256 a character key named by its place on a US keyboard ('a'..'z', '0'..'9', ' ', ',', '.', '-', '=', '[' ...), whatever
// the keyboard's own layout prints on it (an AZERTY 'A' is 'q'): the keys as places, for notes and shortcuts. Typed text is in `text`.
enum {
    WG_KEY_TAB = 256, WG_KEY_ENTER, WG_KEY_ESC, WG_KEY_BACKSPACE, WG_KEY_DELETE,
    WG_KEY_LEFT, WG_KEY_RIGHT, WG_KEY_UP, WG_KEY_DOWN, WG_KEY_HOME, WG_KEY_END, WG_KEY_PGUP, WG_KEY_PGDN,
    WG_KEY_F1, WG_KEY_F2, WG_KEY_F3, WG_KEY_F4, WG_KEY_F5, WG_KEY_F6, WG_KEY_F7, WG_KEY_F8,
};

typedef struct {
    uint16_t key;           // WG_KEY_* or a character key
    uint8_t  down;          // 1 = pressed, 0 = released
    uint8_t  repeat;        // a press repeated by the keyboard while held
} wg_key_t;

#define WG_KEYS_MAX 32
typedef struct {
    int     x, y;                   // the pointer, in the canvas's pixels (outside it: negative or past the edge)
    uint8_t buttons;                // WG_BTN_* held now
    uint8_t pressed, released;      // WG_BTN_* that went down / up since the last frame
    uint8_t double_click;           // the left press of this frame is the second of a double click
    int     wheel;                  // wheel detents since the last frame (+ = away from the user)
    uint8_t mods;                   // WG_MOD_* held now
    wg_key_t keys[WG_KEYS_MAX];     // keys pressed / released since the last frame, in order
    int     nkeys;
    char    text[32];               // text typed since the last frame (UTF-8 as the platform gives it)
    bool    quit;                   // the window was closed
    bool    any;                    // something happened (an app may skip the redraw when nothing did)
} wg_input_t;

bool wg_key_pressed(const wg_input_t *in, int key);     // pressed (or repeated) since the last frame

/* ---------------- the theme ---------------- */

typedef struct {
    uint16_t bg, panel, panel2, line, text, dim, accent, warn;
    const uint8_t *font;            // the widgets' text
    const uint8_t *font_small;      // knob labels, values
} wg_theme_t;

extern const wg_theme_t wg_default_theme;

/* ---------------- the context ---------------- */

#define WG_ORDER_MAX 512
enum { WG_DND_NONE = 0 };           // a payload's kind: the app's own numbers from 1

typedef struct {
    canvas_t *cv;
    u8g2_t   *g;
    wg_theme_t theme;
    const wg_input_t *in;
    int hot;                        // the widget under the pointer (this frame)
    int active;                     // the widget that took the press (until the release)
    int focus;                      // the widget the keyboard edits (0: none)
    // what the active widget remembers of its press
    int   press_x, press_y;
    float press_v, press_v2;
    int   press_part;               // which part was pressed (a knob: 0 the knob, 1 its ring)
    // the keyboard's order: the focusable widgets of this frame and of the last one
    int order[WG_ORDER_MAX], norder;
    int prev_order[WG_ORDER_MAX], prev_norder;
    int tab_move;                   // +1 / -1: Tab pressed this frame (applied at wg_end, over this frame's order)
    bool focus_seen;                // the focused widget was called this frame
    // drag and drop
    int dnd_kind, dnd_value, dnd_from;  // the payload (kind WG_DND_NONE: nothing dragged)
    int dnd_x, dnd_y;               // where it started
    bool dnd_dropped;               // let go this frame: the targets called after the release take it
    char dnd_label[24];
    // the hint of the widget under the pointer (an app shows it in a status line)
    char hint[96];
} wg_ctx_t;

#define WG_ID(family, index) ((family) * 4096 + (index) + 1)

void wg_init(wg_ctx_t *c, canvas_t *cv);
void wg_begin(wg_ctx_t *c, const wg_input_t *in);
void wg_end(wg_ctx_t *c);           // draws what is being dragged, moves the focus on Tab

/* ---------------- drawing helpers (canvas colours) ---------------- */

void wg_fill(wg_ctx_t *c, gui_rect_t r, uint16_t color);
void wg_frame(wg_ctx_t *c, gui_rect_t r, uint16_t color);
void wg_text(wg_ctx_t *c, int x, int baseline, const char *s, uint16_t color);             // the theme's font
void wg_text_small(wg_ctx_t *c, int x, int baseline, const char *s, uint16_t color);
void wg_text_center(wg_ctx_t *c, gui_rect_t r, const char *s, uint16_t color, bool small); // centred in r, both ways
int  wg_text_w(wg_ctx_t *c, const char *s, bool small);
void wg_line(wg_ctx_t *c, int x0, int y0, int x1, int y1, uint16_t color);
// An arc of a circle around (cx, cy), from angle a0 to a1 (radians, 0 = right, counter-clockwise, y up), `thick` pixels inward.
void wg_arc(wg_ctx_t *c, int cx, int cy, int radius, float a0, float a1, int thick, uint16_t color);
void wg_focus_ring(wg_ctx_t *c, gui_rect_t r);  // the keyboard focus around r (dotted, the accent colour)
uint16_t wg_mix(uint16_t a, uint16_t b, int t256);   // a colour between a (t 0) and b (t 256)

/* ---------------- state queries ---------------- */

bool wg_hover(const wg_ctx_t *c, gui_rect_t r);        // the pointer is over r (and no other widget holds the pointer)
// A widget's pointer bookkeeping, for an app's own widget: makes it hot / active / focused as a press over r says. Returns true while
// it is active. `focusable`: Tab visits it.
bool wg_behavior(wg_ctx_t *c, int id, gui_rect_t r, bool focusable);
void wg_focusable(wg_ctx_t *c, int id);                 // Tab visits it (wg_behavior does it)
bool wg_has_focus(const wg_ctx_t *c, int id);
void wg_set_focus(wg_ctx_t *c, int id);
// The keys of this frame meant for the focused widget `id`: its step count (+ up / right, - down / left, scaled by Page Up / Down = 10),
// and whether Home / End / Delete / Enter were pressed. 0 / false when it does not have the focus.
typedef struct { int steps; bool home, end, reset, enter; } wg_keys_t;
wg_keys_t wg_focus_keys(wg_ctx_t *c, int id);
void wg_set_hint(wg_ctx_t *c, const char *fmt, ...);

/* ---------------- widgets ---------------- */

bool wg_button(wg_ctx_t *c, int id, gui_rect_t r, const char *label, bool selected);
bool wg_tab(wg_ctx_t *c, int id, gui_rect_t r, const char *label, bool selected);
// A value -1 / +1 with arrows on both sides (a list choice): returns the step taken (0: none). Clicks on its halves, the wheel, the keys.
int  wg_stepper(wg_ctx_t *c, int id, gui_rect_t r, const char *text);

// A knob over a value 0..1 (the app maps it onto the parameter's range). `steps`: the number of distinct values (2.. for a whole-number
// parameter: the value moves by 1 / (steps - 1)), 0 = continuous. A ring outside the knob shows a range [*ring_a, *ring_b] in
// ring_color (a macro's span over this parameter): dragging on the ring moves ring_b (Shift: ring_a).
typedef struct {
    const char *label;              // under the knob
    const char *value;              // the value as text (under the label)
    float def;                      // the default (double click, Delete)
    int   steps;
    uint16_t color;                 // the value arc (0: the accent)
    bool  ring;                     // draw / edit the ring
    float *ring_a, *ring_b;
    uint16_t ring_color;
    const uint16_t *dots;           // small marks under the knob (the macros that drive it), `ndots` colours
    int   ndots;
} wg_knob_t;
enum { WG_CHANGED = 1, WG_RING_CHANGED = 2 };
int  wg_knob(wg_ctx_t *c, int id, gui_rect_t r, float *v, const wg_knob_t *k);     // WG_CHANGED | WG_RING_CHANGED
// A horizontal slider over 0..1 with its text in it (no label).
bool wg_slider(wg_ctx_t *c, int id, gui_rect_t r, float *v, float def, const char *text, uint16_t color);

// Drag and drop. Call wg_drag_source right after the widget `id` it starts from: once the pointer moved a few pixels with the press, the
// payload travels (drawn at the pointer with `label`); the widget gets no more drags. wg_drop_target: true when a payload of `kind` was
// let go over r this frame (its value in *value); while one is dragged over r it is highlighted.
void wg_drag_source(wg_ctx_t *c, int id, int kind, int value, const char *label);
bool wg_drop_target(wg_ctx_t *c, gui_rect_t r, int kind, int *value);
bool wg_dragging(const wg_ctx_t *c, int kind);          // a payload of `kind` is on its way

#ifdef __cplusplus
}
#endif
