#pragma once
// Small immediate-mode GUI toolkit on top of u8g2: a style sheet, rectangle layout
// helpers, text measuring/centering and 1-bit sprites. Portable (u8g2 only).
//
// Conventions: y grows downwards; a rect is {x, y, w, h} in pixels; text is drawn
// with its BASELINE at the point returned by the *_text_* helpers.
#include <stdbool.h>
#include <stdint.h>
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- basic types ---------------- */

typedef struct { int x, y; } gui_point_t;
typedef struct { int x, y, w, h; } gui_rect_t;

/* ---------------- the 8 x 8 grid ---------------- */
// Every placement sits on an 8 px grid (assets/UI_Sprites/README.md): the 128 x 128 screen is 16 x 16 cells. The top bar is one
// 16 px row (its rule is the bar's last pixel row); the screens draw under it.
#define GUI_GRID   8
#define GUI_BAR_H  16
#define GUI_SNAP(v) (((v) + GUI_GRID - 1) / GUI_GRID * GUI_GRID)   // v rounded up to the grid

/* ---------------- style sheet ---------------- */

typedef struct {
    const uint8_t *font;      // u8g2 font used by every text helper
    int margin;               // space between the screen edge and content
    int padding;              // space between a box edge and its text/sprite
    int gap;                  // space between sibling elements (rows, cells)

    // Parameter pages: list column on the left, graph box on the right.
    int list_w;               // width of the parameter list column
    int list_top;             // y offset of the first row below the header rule
    gui_rect_t graph;         // graph box (frame included)

    // Sequencer piano roll.
    int roll_pitch_px;        // pixels per semitone
    int roll_bar_h;           // height of a note bar
    int roll_pad;             // space between the roll frame and the notes
    int roll_beat;            // dotted beat line every N steps
    int strip_h;              // height of the playhead strip under the roll

    // Oscillator / LFO previews: how many periods of the wave the graph shows (engines too: a decay is drawn over the whole box).
    float wave_cycles;

    // Rack (module builder): horizontal slots.
    int rack_pitch;           // distance between slot origins (sprite width + gap)
    int rack_lane_gap;        // distance between the connection lanes under the slots
} gui_style_t;

// The style used by default; screens may copy and tweak it.
extern const gui_style_t gui_default_style;

// Style for a display of w x h pixels, derived from gui_default_style (the 128 x 64 layout). The application builds its style
// from the display it draws on (see synth_ui_draw), so a new screen size needs no change in the drawing code.
void gui_style_init(gui_style_t *st, int w, int h);

// Selects the style's font on the display. Call before measuring or drawing text.
void gui_begin(u8g2_t *g, const gui_style_t *st);

/* ---------------- geometry ---------------- */


gui_rect_t  gui_rect(int x, int y, int w, int h);
gui_rect_t  gui_screen(u8g2_t *g);                          // whole display
gui_rect_t  gui_inset(gui_rect_t r, int n);                 // shrink by n on every side
gui_point_t gui_center(gui_rect_t r);                       // center point
int         gui_right(gui_rect_t r);                        // x just past the right edge
int         gui_bottom(gui_rect_t r);                       // y just past the bottom edge

// Placement relative to another rect (same x / y / size unless given).
gui_rect_t  gui_below(gui_rect_t r, int gap, int h);
gui_rect_t  gui_above(gui_rect_t r, int gap, int h);
gui_rect_t  gui_right_of(gui_rect_t r, int gap, int w);
gui_rect_t  gui_left_of(gui_rect_t r, int gap, int w);

// Cuts h (or w) pixels off the top (or left) of *r and returns them as a rect.
gui_rect_t  gui_take_top(gui_rect_t *r, int h);
gui_rect_t  gui_take_left(gui_rect_t *r, int w);

// Cell `index` (row-major) of a cols x rows grid inside r, `gap` pixels apart.
gui_rect_t  gui_grid_cell(gui_rect_t r, int cols, int rows, int index, int gap);

// Centers a w x h box inside r.
gui_rect_t  gui_center_box(gui_rect_t r, int w, int h);

/* ---------------- text ---------------- */

int         gui_text_w(u8g2_t *g, const char *s);           // pixel width
int         gui_text_h(u8g2_t *g);                          // ascent + descent
int         gui_row_h(u8g2_t *g, const gui_style_t *st);    // text_h + 2*padding

// Baseline position that centers `s` inside r (use with u8g2_DrawStr).
gui_point_t gui_text_center(u8g2_t *g, gui_rect_t r, const char *s);
// Bounding rect of `s` drawn with its baseline at (x, y).
gui_rect_t  gui_text_rect(u8g2_t *g, const char *s, int x, int y);

void gui_draw_text_centered(u8g2_t *g, gui_rect_t r, const char *s);
void gui_draw_text_left(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *s);
void gui_draw_text_right(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *s);

// Label on the left, value on the right; inverted (filled box) when selected.
// `value` may be NULL (then draw the value yourself, e.g. a sprite).
void gui_draw_field(u8g2_t *g, const gui_style_t *st, gui_rect_t r,
                    const char *label, const char *value, bool selected);

// Look of an interactive element: plain, focused (outlined), latched (filled; the joystick edits its value).
typedef enum { GUI_PLAIN, GUI_FOCUSED, GUI_LATCHED } gui_state_t;

// Label on the left, value on the right, drawn according to the state: focused = frame around, latched = filled with the value between < >.
void gui_draw_field_state(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *label, const char *value, gui_state_t state);
// A push button: centred label inside a frame; focused = filled.
void gui_draw_button(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *label, gui_state_t state);
// A small arrow (triangle) whose tip points in a direction ('l', 'r', 'u', 'd') at (x, y); size = length in pixels.
void gui_draw_arrow(u8g2_t *g, int x, int y, char dir, int size);

/* ---------------- sprites ---------------- */
// 1-bit sprite: rows of ceil(w/8) bytes, 8 pixels per byte, MSB = leftmost pixel,
// 1 = pixel on. Define them in a header as `static const uint8_t data[]` (see sprites.h).
// A sprite may be a sheet: `frames` images of w x h stacked vertically in `data`
// (frames 0 or 1 = a single image). Used by the animation helpers below.
// mask (optional, same layout as data): 1 = the pixel is drawn (lit where data is 1, off where it is 0), 0 = transparent (left as it is).
// Without a mask the sprite's 0 pixels are drawn off (solid). Images from assets/UI_Sprites/ get a mask when the PNG has transparency.
typedef struct {
    uint8_t w, h;
    const uint8_t *data;
    uint8_t frames;
    const uint8_t *mask;
} gui_sprite_t;

void gui_draw_sprite(u8g2_t *g, const gui_sprite_t *sp, int x, int y);            // frame 0
// Sprite drawn dark on a filled box (selected look).
void gui_draw_sprite_selected(u8g2_t *g, const gui_sprite_t *sp, int x, int y);
void gui_draw_sprite_frame(u8g2_t *g, const gui_sprite_t *sp, int frame, int x, int y);
void gui_draw_sprite_centered(u8g2_t *g, const gui_sprite_t *sp, gui_rect_t r);
// Right-aligned inside r (respecting the style padding), vertically centered.
void gui_draw_sprite_right(u8g2_t *g, const gui_style_t *st, const gui_sprite_t *sp, gui_rect_t r);

/* ---------------- sprite animations ---------------- */
// Register an animation over a frame range of a sprite sheet, tick it from the main
// loop, draw its current frame. The app layer owns the id and removes it when done.
//
//   int a = gui_anim_add(&spr_eq, 0, 3, 120, true);   // frames 0..3, 120 ms each, loop
//   ...each loop:  if (gui_anim_tick(now_ms)) redraw();
//   ...drawing:    gui_anim_draw(g, a, x, y);
//   ...later:      gui_anim_remove(a);
//
// `first` > `last` plays backwards. A non-looping animation stops on its last frame
// (gui_anim_finished) and stays registered until removed.

#define GUI_ANIM_MAX     8          // simultaneous animations
#define GUI_ANIM_INVALID (-1)

int  gui_anim_add(const gui_sprite_t *sp, int first, int last, uint32_t frame_ms, bool loop);
// Same, but the whole first..last range takes total_ms.
int  gui_anim_add_total(const gui_sprite_t *sp, int first, int last, uint32_t total_ms, bool loop);

void gui_anim_remove(int id);       // safe with GUI_ANIM_INVALID / stale ids
void gui_anim_clear(void);          // remove all
void gui_anim_restart(int id);      // back to `first` (also re-arms a finished one)
bool gui_anim_finished(int id);     // true after a non-looping animation reached its end
int  gui_anim_frame(int id);        // current frame index in the sheet, -1 if invalid

// Advances every animation to now_ms (monotonic). True if any frame changed => redraw.
bool gui_anim_tick(uint32_t now_ms);

void gui_anim_draw(u8g2_t *g, int id, int x, int y);
void gui_anim_draw_centered(u8g2_t *g, int id, gui_rect_t r);
void gui_anim_draw_right(u8g2_t *g, const gui_style_t *st, int id, gui_rect_t r);

#ifdef __cplusplus
}
#endif
