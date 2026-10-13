# The drawing kit (`framework/ui`)

How to put something on the screen from any app built on the library: the style sheet, the rectangle helpers, text, fields and
buttons, sprites, animations and popups. The kit is plain C over [u8g2](https://github.com/olikraus/u8g2) and knows nothing
about a synth: the starter app (`apps/template`) uses it for three rows of text, the Ritual Machine for all of its screens.

Paths below are from the repo root. In code the library is included without its folder: `#include "ui/gui.h"`,
`#include "hal/hal_display.h"`.

| File | What it gives |
| --- | --- |
| `framework/ui/gui.h`, `gui.c` | the style sheet, rectangles, text, fields and buttons, sprites, sprite animations |
| `framework/ui/popup.h`, `popup.c` | popup messages over any screen: a note that goes by itself, an error with OK, a Yes / No question |
| `framework/hal/hal_display.h` | the screen itself: `display_init()` returns a ready `u8g2_t *`, `display_send()` shows the frame |

**What the kit is not.** It draws; it does not remember. There is no focus, no list of widgets, no event handling: your app
keeps its own cursor and decides what a control does. The Ritual Machine has a layer of its own on top (screens written as
tables of elements, with focus navigation: `apps/ritual_machine/src/core/ui_screen.h`, described in
[RITUAL_MACHINE_UI.md](RITUAL_MACHINE_UI.md)); that layer is tied to its data and is not part of the library.

## 1. Getting it into a build

A project made with `.\tools\new_project.ps1 my_synth` already has it:

- the desktop build (`apps/<name>/build.ps1`) adds the kit's sources with `Get-UiSources` (`framework/tools/sim_build.ps1`)
  and the include path `-I../../framework`;
- the firmware build (`apps/<name>/platformio.ini`) has `+<framework/ui/>` in `build_src_filter` and `-I ../../framework`
  in `build_flags`.

u8g2 is third-party code: `lib/u8g2` for the desktop (not versioned, see DEVELOPING.md "Requirements"), a PlatformIO
library for the firmware.

## 2. One frame, from start to end

The whole screen is redrawn into u8g2's buffer every time (immediate mode): nothing is kept between frames except your own
state. This is the starter app's frame (`apps/template/src/app.cpp`), with what each line is for:

```cpp
gui_style_t style;                                           // once, at start:
gui_style_init(&style, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));

void app_draw() {
    if (!dirty || !g) return;                                // draw only when something visible changed
    dirty = false;
    u8g2_ClearBuffer(g);                                     // start from an empty frame
    gui_begin(g, &style);                                    // selects the style's font: before any text is measured or drawn
    gui_rect_t area = gui_screen(g);                         // the whole display as a rectangle
    const gui_rect_t bar = gui_take_top(&area, GUI_BAR_H);   // cut the 16 px top bar off it
    gui_draw_text_centered(g, bar, APP_NAME);
    u8g2_DrawHLine(g, 0, GUI_BAR_H - 1, bar.w);              // the rule is the bar's last pixel row
    area = gui_inset(area, style.margin);
    const int row_h = gui_row_h(g, &style) + style.gap;
    gui_draw_field(g, &style, gui_take_top(&area, row_h), "Octave", "+1", false);
    gui_draw_field(g, &style, gui_take_top(&area, row_h), "Cutoff", "64", false);
    display_send(g);                                         // show it (never u8g2_SendBuffer)
}
```

Three rules come with it:

1. **`display_send(g)`, not `u8g2_SendBuffer(g)`.** The board decides how the frame reaches the panel. On the first prototype
   the panel is on I2C and a full frame takes about 120 ms, so `display_send` copies the frame and a task of its own sends
   only the 8 x 8 tiles that changed; your loop goes on at once.
2. **Redraw only when something visible changed.** Keep a `dirty` flag, as above. A frame drawn for nothing costs time the
   notes and knobs need.
3. **The display is the board's.** `display_init()` creates it (bus, pins, controller, or an SDL window on the desktop); the
   app only ever receives the `u8g2_t *`.

Anything the kit has no helper for is a plain u8g2 call on the same buffer (`u8g2_DrawLine`, `u8g2_DrawFrame`, `u8g2_DrawBox`,
`u8g2_DrawPixel`, `u8g2_DrawStr`): the kit and u8g2 mix freely.

## 3. Conventions

- **Coordinates**: x grows to the right, y grows downwards, in pixels. A rectangle is `gui_rect_t {x, y, w, h}`.
- **Text** is drawn with its **baseline** at the given point (u8g2's rule). The text helpers work that out for you.
- **Draw colour** is u8g2's: 1 = lit (the default), 0 = off, 2 = invert what is there. A helper that changes it sets it back
  to 1 before it returns; do the same in your own drawing code.
- **The 8 px grid**: placements sit on multiples of 8 (`GUI_GRID`), so a 128 x 128 screen is 16 x 16 cells. The top bar is one
  16 px row (`GUI_BAR_H`). `GUI_SNAP(v)` rounds a length up to the grid. The grid is a convention for a tidy screen, not
  something the helpers enforce.
- **Sizes come from the display**, not from constants: read `u8g2_GetDisplayWidth / Height` (or start from `gui_screen(g)`)
  so the same code runs on another panel.

## 4. Layout with rectangles

Screens are laid out by cutting and placing rectangles, not by writing coordinates. That is what lets one drawing function
run at 128 x 64 and at 128 x 128.

| Helper | Gives |
| --- | --- |
| `gui_screen(g)` | the whole display |
| `gui_rect(x, y, w, h)` | a rectangle from four numbers |
| `gui_inset(r, n)` | `r` shrunk by `n` on every side |
| `gui_take_top(&r, h)`, `gui_take_left(&r, w)` | cuts `h` (or `w`) pixels off `*r` and returns them; `*r` keeps the rest |
| `gui_below(r, gap, h)`, `gui_above(r, gap, h)` | a rectangle under (over) `r`, same x and width |
| `gui_right_of(r, gap, w)`, `gui_left_of(r, gap, w)` | a rectangle beside `r`, same y and height |
| `gui_grid_cell(r, cols, rows, index, gap)` | cell `index` (row by row) of a grid inside `r` |
| `gui_center_box(r, w, h)` | a `w` x `h` box centred in `r` |
| `gui_center(r)`, `gui_right(r)`, `gui_bottom(r)` | the centre point; the x just past the right edge; the y just past the bottom |

A column of rows, and a 4 x 4 grid of cells:

```c
gui_rect_t row = gui_rect(area.x, area.y, area.w, gui_row_h(g, st));
for (int i = 0; i < n; i++) { draw_row(g, st, row, i); row = gui_below(row, st->gap, row.h); }

for (int i = 0; i < 16; i++) draw_cell(g, gui_grid_cell(area, 4, 4, i, 0), i);
```

## 5. Text

Call `gui_begin(g, st)` first: it selects the style's font, and every measure below depends on the font.

| Helper | Gives |
| --- | --- |
| `gui_text_w(g, s)` | the width of `s` in pixels |
| `gui_text_h(g)` | the font's height (ascent + descent) |
| `gui_row_h(g, st)` | the height of a row of text: `gui_text_h` + 2 x `st->padding` |
| `gui_draw_text_centered(g, r, s)` | `s` centred in `r` |
| `gui_draw_text_left(g, st, r, s)`, `gui_draw_text_right(g, st, r, s)` | `s` at the left (right) edge of `r`, `st->padding` + 1 pixels in, centred vertically |
| `gui_text_center(g, r, s)` | the baseline point that centres `s` in `r`, for your own `u8g2_DrawStr` |
| `gui_text_rect(g, s, x, y)` | the box `s` covers when drawn with its baseline at (x, y) |

The kit uses one font, the style's (`u8g2_font_5x7_tr`: 5 x 7 pixels, ASCII only). Nothing clips or wraps a text that is too
long for its rectangle: shorten it yourself (`"%.9s"`), or set a clip window with `u8g2_SetClipWindow` and clear it with
`u8g2_SetMaxClipWindow`.

## 6. Fields, buttons, arrows

| Helper | Draws |
| --- | --- |
| `gui_draw_field(g, st, r, label, value, selected)` | the label at the left, the value at the right; `selected` = the row filled, text dark. `value` may be `NULL` (draw your own, a sprite for example) |
| `gui_draw_field_state(g, st, r, label, value, state)` | the same with three looks (below) |
| `gui_draw_button(g, st, r, label, state)` | the label centred in a frame; filled when not plain |
| `gui_draw_arrow(g, x, y, dir, size)` | a small triangle whose tip is at (x, y); `dir` = `'l'`, `'r'`, `'u'`, `'d'`; `size` = its length in pixels |

| `gui_state_t` | Field | Button |
| --- | --- | --- |
| `GUI_PLAIN` | text only | a frame |
| `GUI_FOCUSED` | a frame around the row | filled |
| `GUI_LATCHED` | filled, the value between `<` `>` | filled |

The states only say how to draw. Which element is focused, and what "latched" means for your controls, is your app's to
decide and to remember.

## 7. The style sheet

`gui_style_t` holds the font and the spacings every helper reads. `gui_style_init(&st, w, h)` fills one for a display of
`w` x `h` pixels, starting from `gui_default_style`. A screen that needs tighter rows copies the style and changes the copy:

```c
gui_style_t tight = *st;
tight.padding = 0; tight.gap = 0;
```

| Field | Default | Meaning |
| --- | --- | --- |
| `font` | `u8g2_font_5x7_tr` | the font of every text helper |
| `margin` | 0 | space between the screen edge and the content |
| `padding` | 1 | space between a box edge and its text or sprite |
| `gap` | 1 | space between neighbouring rows or cells |
| `list_w`, `list_top`, `graph` | half the width; 2; the rest of the screen under the bar | a list column on the left and a picture box on the right |
| `roll_*`, `strip_h`, `wave_cycles`, `rack_pitch`, `rack_lane_gap` | | metrics of the Ritual Machine's piano roll, wave previews and rack |

The first four are all a new app needs. `list_w` and `graph` are useful when you want the "list on the left, picture on the
right" screen. The last row is the Ritual Machine's and was left in the library's struct when the kit moved there; ignore it.

**Another screen size**: `DISPLAY_WIDTH` and `DISPLAY_HEIGHT` in `framework/hal/hal_display.h` (or `-DDISPLAY_WIDTH=...
-DDISPLAY_HEIGHT=...` in the build), plus the board's display driver. Code that takes its sizes from the display needs no change.

## 8. Sprites

A sprite is a 1-bit image:

```c
typedef struct {
    uint8_t w, h;
    const uint8_t *data;      // rows of ceil(w / 8) bytes, the leftmost pixel in the top bit, 1 = lit
    uint8_t frames;           // 0 or 1 = one image; n = n images of w x h stacked top to bottom (a sheet)
    const uint8_t *mask;      // optional, same layout: 1 = this pixel is drawn, 0 = transparent
} gui_sprite_t;
```

Without a mask the sprite is solid: its 0 pixels are drawn off and erase what is behind. With one, only the pixels whose mask
bit is 1 are drawn (lit or off, as `data` says).

By hand, a sprite is a byte array and a struct in a header:

```c
static const uint8_t spr_play_data[8] = {
    0x40,   // .X......
    0x60,   // .XX.....
    0x70,   // .XXX....
    0x78,   // .XXXX...
    0x78,   // .XXXX...
    0x70,   // .XXX....
    0x60,   // .XX.....
    0x40,   // .X......
};
static const gui_sprite_t spr_play = {8, 8, spr_play_data, 1, NULL};
```

| Helper | Draws |
| --- | --- |
| `gui_draw_sprite(g, &sp, x, y)` | frame 0 with its top left corner at (x, y) |
| `gui_draw_sprite_frame(g, &sp, frame, x, y)` | one frame of a sheet |
| `gui_draw_sprite_selected(g, &sp, x, y)` | the sprite dark on a filled box (the selected look) |
| `gui_draw_sprite_centered(g, &sp, r)` | centred in `r` |
| `gui_draw_sprite_right(g, st, &sp, r)` | at the right edge of `r` (the style's padding in), centred vertically |

**From PNG files.** The library has no image converter. The Ritual Machine has one that an app can copy:
`apps/ritual_machine/tools/gen_ui_sprites.py` turns the PNGs of `assets/UI_Sprites/<size>/` into a `.c` / `.h` pair of
`gui_sprite_t` (with a mask when the PNG has transparency, with frames for `name_00.png`, `name_01.png` ...), and a lookup by
name. It is plain Python with no package to install. Its paths and its list of sizes (8, 16, 24, 64) are at the top of the
file; `build.ps1` and the PlatformIO pre-build script `tools/pio_gen_sprites.py` run it before each build. The image rules are
in `apps/ritual_machine/assets/UI_Sprites/README.md`.

## 9. Animations

An animation plays a range of frames of a sprite sheet on a timer. The kit keeps up to 8 at once (`GUI_ANIM_MAX`); your app
owns the id.

```c
int a = gui_anim_add(&spr_eq, 0, 3, 120, true);       // frames 0..3, 120 ms each, looping
...
if (gui_anim_tick(now_ms)) dirty = true;              // every loop: true when a frame changed
...
gui_anim_draw(g, a, x, y);                            // in the frame
...
gui_anim_remove(a);                                   // when it is no longer shown
```

| Call | Does |
| --- | --- |
| `gui_anim_add(sp, first, last, frame_ms, loop)` | registers one; returns its id, or `GUI_ANIM_INVALID` when all 8 are in use or a frame is outside the sheet. `first` > `last` plays backwards |
| `gui_anim_add_total(sp, first, last, total_ms, loop)` | the same, the whole range taking `total_ms` |
| `gui_anim_tick(now_ms)` | advances all of them (`now_ms` must only grow); true when any frame changed |
| `gui_anim_draw`, `gui_anim_draw_centered`, `gui_anim_draw_right` | draws the current frame |
| `gui_anim_finished(id)` | true once a non-looping animation reached its last frame (it stays there until removed) |
| `gui_anim_restart(id)`, `gui_anim_frame(id)` | back to the first frame; the current frame's index (-1 for a bad id) |
| `gui_anim_remove(id)`, `gui_anim_clear()` | frees one (safe with `GUI_ANIM_INVALID`); frees all |

Store `GUI_ANIM_INVALID` in the id while nothing plays, so drawing and removing stay safe.

## 10. Popups

A popup is a small box in the middle of the display, over whatever screen is up.

| Kind | Call | Behaviour |
| --- | --- | --- |
| INFO | `popup_info(&p, title, text, arrow, now_ms, ms)` | goes by itself after `ms` (`POPUP_INFO_MS` = 1200 is the usual value); not modal: input still goes to the screen behind; a new one replaces the one on screen. `arrow` -1 / +1 adds an arrow left / right after the text, 0 none |
| ERROR | `popup_error(&p, title, text, answer, ctx)` | an OK button; modal. `title` `NULL` = "ERROR" |
| ASK | `popup_ask(&p, title, text, default_yes, answer, ctx)` | Yes / No buttons; modal |

Modal popups wait in a queue of 4 (`POPUP_QUEUE`) and show one at a time, oldest first; `popup_error` and `popup_ask` return
false when the queue is full. The answer comes through a callback, `void answer(void *ctx, bool yes)`, called once after the
popup closed (an ERROR always answers yes); it may raise another popup. A title holds 19 characters, a text 71; the text is
wrapped to the box and `\n` starts a new line.

The kit draws the popup and keeps its queue. Your app does four things:

```c
popup_t popup;
popup_init(&popup);                                        // once

if (popup_tick(&popup, now_ms)) dirty = true;              // every loop: true when an INFO just went

if (popup_modal(&popup)) {                                 // input: while a modal popup is up it takes the controls
    if (left)  popup_move(&popup, -1);                     //   towards Yes
    if (right) popup_move(&popup, +1);                     //   towards No
    if (push)  popup_confirm(&popup);                      //   press the focused button
    if (back)  popup_cancel(&popup);                       //   No, or close an error
} else { /* the screen's own handling */ }

draw_my_screen(g);                                         // in the frame: the screen first,
popup_draw(&popup, g, &style);                             // the popup on top,
display_send(g);                                           // then show
```

Which controls mean left, right, push and back is your choice. The Ritual Machine hands a modal popup the actions of its
binding table and key layout instead of raw controls, so the user's own key layout works in a popup too
(`apps/ritual_machine/src/app/app_actions.c`, `popup_action`).

## 11. The boot animation hook

A board may play an animation by itself while the machine starts (the first prototype does, from a task of its own; the
desktop does nothing). An app that wants one gives the board its frames before `display_init()`:

| Call (`hal/hal_display.h`) | When |
| --- | --- |
| `display_boot_config(&boot)` | before `display_init()`: the frames (a sprite sheet's layout), their size and count, the time per frame, and an optional function that draws a text kept over every frame |
| `display_boot_progress(percent)` | while the app starts: 0..100 for the loading bar under the animation; it never has to go back |
| `display_boot_end()` | once, when the app has finished starting: the animation ends and the newest frame sent is shown |

Without `display_boot_config` there is no animation and the two other calls do nothing.

## 12. Looking at a screen

- **The desktop program**: `.\build.ps1 -App my_synth`, `.\run.ps1 -App my_synth` opens the display window and a panel
  window whose controls work with the mouse. `-Board hwv1` builds the same app as the first prototype.
- **On the board**: `C:\.platformio\penv\Scripts\pio.exe run -d apps/my_synth -t upload` (never a bare `pio`: DEVELOPING.md,
  "ESP32 firmware").
- **As text, without a window**: the Ritual Machine has a small program that prints its screens as ASCII after a script of
  control events (`apps/ritual_machine/tools/ui_dump.c`). It is built on that app's sources, so it is an example to copy,
  not a tool of the library.

## 13. Limits

- One font, ASCII only. No text wrapping or clipping outside the popups.
- The whole frame is redrawn on every change; there is no partial redraw in the kit (the first prototype's display task sends
  only the changed tiles, which hides most of the cost).
- No focus, no scrolling list, no value bar or slider: a value is a label and a text. Build what you need from rectangles
  and u8g2 calls.
- 8 animations at once, 4 modal popups waiting.
- `gui_style_t` still carries the Ritual Machine's page metrics (section 7).
