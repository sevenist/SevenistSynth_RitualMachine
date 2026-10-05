# How the user interface is built

This guide describes the screen UI as it is now, so you know where to cut before changing it. File and function names are real.
The UI is in a transition: every tab of the **menu** is built with the new **declarative** model (a table of elements, see section 8); the **main view**
(the pages of parameters, the sequencer and the motion / envelope pages) still uses the older **page / handler** model (sections 3 and 4). The boundary between
the two is described in section 7, and the last section lists what is still open.

Files, all in `src/core/`:

| File | Role |
| --- | --- |
| `synth_ui.h` | the public interface of the UI: `synth_ui_t` (cursor state), `ui_event_t`, `synth_ui_handle`, `synth_ui_draw`, knob / macro calls |
| `ui_internal.h` | private: what the files below share (`graph_t`, `page_t`, `tab_t`, prototypes, rack strip constants) |
| `ui_pages.c` | the main view (old model): page definition tables, the page list generated from the rack; and the menu tab helpers |
| `ui_input.c` | `synth_ui_handle` (routes events to the menu screen or the main view), the main view's row editing and hand-made `handle_*`, knobs and macros |
| `ui_draw.c` | the main view's screens (old model) and `synth_ui_draw`, the entry point that draws a frame |
| `ui_graphs.c` | the pictures of the graph box: waveform, envelope, filter response, effect sketches, algorithm diagram... |
| `ui_screen.c/.h` | the declarative-screen engine: element tables, focus navigation, latch, drawing of elements, the tab -> screen lookup |
| `scr_rack.c`, `scr_general.c`, `scr_samples.c`, `scr_fx.c`, `scr_fm.c` | the menu tabs, written with that engine: RACK, GENERAL, SAMPLES, FX RACK, and the three FM editor tabs (ALGORITHM, OPERATOR, ENVELOPE) |
| `gui.c/.h` | the drawing toolkit and style (rectangles, text, fields, buttons, sprites, animations) |
| `app.c` | calls the UI, decides when to redraw |
| `bindings.c/.h` | which hardware control triggers which action (not part of the UI) |
| `sprites.h`, `module_sprites.h` | bitmaps (the second one is generated) |

The data the UI edits lives elsewhere: `rack.c/.h`, `seq.c/.h`, `synth_params.c/.h`, `synth_config.c/.h`, `fxrack.c/.h`.

## 1. The big picture

```text
 hardware control ──> bindings.c ──> app_run_action() ──> ui_event_t ──> synth_ui_handle()   (edits the DATA, moves the focus)
                                                                              │
 app_step() decides "redraw?" ────────────────────────────────────────> synth_ui_draw()     (reads DATA + focus, draws a frame)
```

- The UI is **immediate mode**: `synth_ui_draw()` clears the buffer and redraws the whole screen from scratch every time. Nothing is retained between frames
  except the cursor state (`synth_ui_t`) and the data being edited.
- The UI never owns sound data. It edits plain structs (`rack_t`, `synth_params_t`, `seq_t`) and tells the audio side when a value changed (`audio_set_params`) or
  when the structure changed (`audio_build`). See section 9.
- Drawing and handling of one screen must agree on what is on it. On an old-model screen they agree through the page table (`get_page`); on a declarative screen
  they are generated from the same element table.

## 2. The cursor state: `synth_ui_t`

Declared in `synth_ui.h`. The fields that matter:

| Field | Meaning |
| --- | --- |
| `row` | the focus. 0 = the header (page selector, or the tab selector in the menu); 1..n = the rows of a page, or element index + 1 on a declarative screen |
| `latched` | the focused element is latched: the joystick edits its value (declarative screens only) |
| `page` | index into the generated page list (main view) |
| `in_rack` | true while the **menu** is open (the name is historical: it was the rack editor); then `menu_tab` is the tab |
| `menu_tab` | which tab of the menu: RACK, GENERAL, SAMPLES, FX RACK (modular synth), or GENERAL, ALGORITHM, OPERATOR, ENVELOPE, FX RACK (FM synth) |
| `rack_cur`, `rack_scroll`, `rack_type` | RACK tab: selected cell (a slot, or the empty one after the last), first visible cell, module type Insert will add |
| `cursor` | selected step of the step sequencer |
| `fm_op`, `fm_pt`, `ms_lane`, `ms_step`, `eg_pt`, `fx_slot`, `smp_cur`, `smp_tgt` | per-screen sub-selections (operator, envelope point, lane, step, sample...) |
| `pg_slot[]`, `pg_def[]`, `page_count` | the generated page list (section 3) |
| `rack_dirty`, `rebuild` | structural edits pending / the audio graph must be rebuilt |
| `macro[]`, `knob_key[]`, `knob_pos[]` | the right-hand knob assignments and knob bookkeeping |

Everything is a plain int or small array: no pointers, no allocation.

## 3. The main view: pages (old model)

The main view is a list of **pages**, flipped through with the header (`< PAGE NAME >`) or the page-move action.

Pages are **generated from the rack**, not hand-listed (`synth_ui_rebuild_pages`): each module contributes its own pages in rack order, then the global pages follow
(AMP ENV, AMP CURVE, SEQUENCER, SEQ SETUP). In FM mode the list is FM SYNTH, SEQUENCER, SEQ SETUP. The list is two parallel arrays: `pg_slot[i]` (the rack slot that owns
page i, or 255 for a global page) and `pg_def[i]` (which of that module type's page definitions). It is regenerated at start and when the menu closes with structural edits.

A module page definition says what to show:

```c
typedef struct { const char *fmt; graph_t graph; int count; int p[6]; bool needs_mod; } mpage_def_t;
//   fmt        title, "%d" = the module's instance number ("OSC %d" -> "OSC 2")
//   graph      which picture goes in the graph box (GRAPH_WAVE, GRAPH_ENV, GRAPH_FILTER ...), or which special screen to draw
//   count      number of rows
//   p[]        for each row: the index of the parameter in the module's slot.v[]  (or a pseudo index, see below)
//   needs_mod  only generate the page while the module acts as a modulator (no table uses it at the moment)
```

The tables are at the top of `ui_pages.c` (`oc_pages[]`, `fl_pages[]`, `lf_pages[]`, `en_pages[]`, `eg_pages[]`, `sm_pages[]`, `ms_pages[]`, ...), connected to module types by
`mod_pages[]`. Global pages use `gpage_def_t` in `global_pages[]`. `get_page(ui, rack, idx, &page_t)` resolves a page into a `page_t` (title, graph, row count, parameter
indexes, slot); `synth_ui_handle` and `synth_ui_draw` both call it.

Pseudo parameter indexes: a row whose value is not in `slot.v[]` uses a special number: `PRM_TGT` and `PRM_TPRM` are a modulator's target module and parameter (the `... DEST`
pages of OSC, LFO, ENV and EG). `page_row_step` (editing) and `synth_ui_draw` (display) both check for them.

Limits: at most 6 rows per page definition, at most 4 pages per module (`SYNTH_UI_MAX_PAGES = RACK_MAX * 4 + 4`).

## 4. Special screens of the main view (old model)

Some pages are drawn and handled by hand because they are not "rows of parameters". `synth_ui_draw` picks them by `page.graph`:

| Screen | Selected by | Drawn by (`ui_draw.c`) | Edited by (`ui_input.c`) |
| --- | --- | --- | --- |
| Step sequencer (piano roll) | `GRAPH_SEQ` | `draw_seq` | `handle_seq` |
| Sequencer setup | `GRAPH_SEQ_CFG` | `draw_seq_cfg` | `seq_param_adjust` through `page_row_step` |
| Motion sequencer steps / lane | `GRAPH_MS_STEPS`, `GRAPH_MS_LANE` | `draw_ms_steps`, `draw_ms_lane` | `handle_ms` |
| Multi-stage envelope | `GRAPH_EG` | `draw_eg_page` | `handle_eg` |
| FM synth page | `GRAPH_FM` | generic list + `draw_cfg_list`, `draw_synth_info` | `synth_config_adjust` through `page_row_step` |

A generic page is only data in a table; a special page is a `draw_*` function plus a `handle_*` function plus a branch in `synth_ui_draw` and in `synth_ui_handle`.
The rows of these pages are numbered by hand and the numbers in the handler and in the drawing must agree: that is what the declarative model is meant to replace.
The menu tabs used to be built this way too; they are all converted now (section 8).

## 5. How a frame is drawn (`synth_ui_draw`)

1. Build the **style** from the display size: `gui_style_init(&style, width, height)`.
2. Clear the buffer, select the font.
3. Cut the **header** row off the top of the screen rectangle: the page title between `<` `>` (or the tab name in the menu), filled when row 0 has the focus. Draw a rule under it.
4. Branch:
   - menu open: `screen_draw` of the current tab's screen (`screen_for_tab`);
   - special page: its `draw_*`;
   - generic page: cut a **list column** off the left (`style.list_w`) and draw one `gui_draw_field` per row; draw the **graph box** (`style.graph`) on the right with the picture
     chosen by `page.graph` (`draw_wave`, `draw_env`, `draw_filter`, ...).
5. `u8g2_SendBuffer()` flushes the frame.

Layout is done with rectangles, not coordinates: `gui_rect_t {x, y, w, h}` is cut up with `gui_take_top/left`, placed with `gui_below/right_of`, split with `gui_grid_cell`, centred
with `gui_center_box`. That is why the same code works at 128x64 and 128x128. The `draw_*` pictures receive the box rectangle and scale to it.

## 6. The toolkit and the style (`gui.h`, `gui.c`)

- **Style sheet** `gui_style_t`: font, margin, padding, gap, list column width and top, graph box rectangle, piano-roll and rack metrics. `gui_default_style` is the 128x64
  reference; `gui_style_init(st, w, h)` derives the style for any screen (list column = half the width, graph box fills the rest). A few screens copy the style and set
  `padding = 0`, `gap = 0` to fit more rows (FM tabs, FX tab, motion lane).
- **Helpers**: rectangles (`gui_rect`, `gui_inset`, ...), text (`gui_text_w/h`, `gui_row_h`, `gui_draw_text_centered/left/right`), 1-bit sprites (`gui_draw_sprite*`), frame animations
  (`gui_anim_*`, used only by the "running" indicator), and the elements:
  - `gui_draw_field` (label and value, filled when selected): the old look;
  - `gui_draw_field_state` and `gui_draw_button` with `gui_state_t` (`GUI_PLAIN`, `GUI_FOCUSED` outline, `GUI_LATCHED` filled with `<value>`): the declarative look;
  - `gui_draw_arrow`: a small triangle in any of four directions.
- **Drawing primitives** for the pictures are plain u8g2 calls (`u8g2_DrawLine`, `DrawFrame`, `DrawBox`...).
- **Sprites** (`module_sprites.h`, generated by `tools/gen_module_sprites.py`) are the 25 x 25 module icons of the RACK strip plus the empty-slot and OUT symbols; `sprites.h` has
  the others (the equalizer animation). **Font**: one font, `u8g2_font_5x7_tr`.

## 7. Input: events and focus

`synth_ui_handle(ui, params, seq, rack, ui_event_t)` is the only entry point. The events:

| Event | Sent by | Meaning |
| --- | --- | --- |
| `UI_NAV_UP / DOWN / LEFT / RIGHT` | joystick | move the focus; on a latched element, change its value |
| `UI_VALUE_INC / DEC` | encoder B | change the focused value, no latch needed |
| `UI_LATCH` | joystick push | latch / release the focused value; on a button, activate it |
| `UI_UP / DOWN` | encoder A | previous / next row or element |
| `UI_SELECT` | encoder B push | activate a button |
| `UI_PAGE_PREV / NEXT`, `UI_ROW_TOP`, `UI_MENU`, `UI_BACK`, `UI_PLAY` | page-move, encoder A push, button 1, button 2, play | |
| `UI_LEFT / RIGHT` | (internal) | the old events: change the value of the focused row; on row 0 change page or tab |

Which control sends which event is decided in `bindings.c`, not in the UI.

`synth_ui_handle` first handles the events that do not depend on the screen (play, page move, menu toggle), then routes by where the focus is:

- **Menu open** (`ui->in_rack`): the event goes unchanged to `screen_event` of the current tab's screen (`screen_for_tab(tab_kind(...))`). All the tabs behave the same way: section 8.
- **Main view**: the new events are first **translated to the old ones** (`UI_NAV_*` -> `UI_UP/DOWN/LEFT/RIGHT`, `UI_VALUE_INC/DEC` -> `UI_RIGHT/LEFT`, `UI_LATCH` -> `UI_SELECT`),
  so the pages work as before: the joystick moves between rows and changes the value directly, encoder B changes the value, a push of the joystick activates the row.
  There is no latch in the main view.

Knobs do not go through events. They call `synth_ui_knob_row` (column knobs: rows 1..4 of the current main-view page), `synth_ui_macro` / `synth_ui_macro_learn` (right-hand knobs) and
`synth_ui_set_volume`. A parameter only knows "one step up / down", so `knob_set` in `ui_input.c` walks it to both ends and back to the step that matches the knob position.
Rows that select or cycle (targets, sample file, step editors) are skipped (`row_is_knobbable`). The knobs act on the main view only, not inside the menu.

## 8. Declarative screens (`ui_screen.h`: every tab of the menu)

A declarative screen is a **table of elements**. The same table drives the drawing, the focus navigation and the editing, so the screen cannot disagree with itself.

An element (`el_def_t`) has a label (fixed, or computed by `label_dyn`), a kind, a position in a small grid (row, column, span), an `arg` handed to its callbacks, and the callbacks:
`value()` (the text), `adjust(dir)`, `activate()`, `draw()` for elements that are not a plain field, `enabled()`. A flag, `EF_HIDE_WHEN_DISABLED`, makes a disabled element disappear
instead of showing `n/a`. A screen (`screen_def_t`) is the element table plus:

- `layout()`: gives every element its rectangle inside the area left under the header: this is where the anchors live. `ui_layout_column()` is the layout of the "list on the left, picture
  on the right" tabs; the RACK tab has its own (strip on top, a grid of fields below).
- `draw_extra()`: what is drawn behind the elements (the picture in the graph box, the connection lanes).
- `compact`: on a short screen (under 100 px high) draw with padding 0 and gap 0 so more rows fit.
- `back()`: what the Back button does. `after_edit()`: called after anything changed (keep a selection valid).

The three kinds and how the controls act on them (`screen_event`):

| Kind | Joystick | Joystick push | Encoder B | Example |
| --- | --- | --- | --- | --- |
| `EL_VALUE` | moves the focus; once latched, changes the value | latch / release | changes the value | Type, Tgt, Voices, Algo |
| `EL_DIRECT` | left / right change it, up / down move the focus | nothing | changes it | the module strip of the RACK tab |
| `EL_BUTTON` | moves the focus | activates | its push (`UI_SELECT`) activates | Insert, Delete, Assign, Scan |

Focus moves spatially with the joystick (`nav`: the nearest element in that direction, by grid position) and linearly with encoder A (`linear`, header included). A disabled element is skipped by
the focus, and when an edit makes the focused element unavailable (an effect with fewer parameters) the focus falls back to the one before it. On the header, left / right change the tab.
Looks (`gui.c`): `GUI_FOCUSED` = outline, `GUI_LATCHED` = filled with the value between `<` `>`; a button is a frame, filled when focused.

The tabs and their files:

| Tab | File | Elements | Picture / extra |
| --- | --- | --- | --- |
| RACK | `scr_rack.c` | module strip, Type, Insert, Tgt, Prm, Dpth, Delete | the strip itself, connection lanes, a description line |
| GENERAL | `scr_general.c` | Type, Patch, Voices, Vol | info box (`draw_synth_info`) |
| SAMPLES | `scr_samples.c` | File, Tgt, Assign, Scan | overview of the highlighted file, its length and root note |
| FX RACK | `scr_fx.c` | Slot, Type, the effect's parameters (names from `fxr_label`, unused ones hidden) | sketch of the effect (`draw_fx_picture`) |
| ALGORITHM, OPERATOR, ENVELOPE (FM) | `scr_fm.c` | Algo / Fb / Op, Op + the operator's parameters, Op / Pt / Lvl / Time | algorithm diagram or operator envelope, the patch name under the list |

Edits that change the structure (module insert / delete, target, type, voices, the FM patch) set `rack_dirty` or `rebuild`; the audio graph is rebuilt when the menu closes. Edits that are live
(volume, effect parameters, FM operator values) return `true` so the app pushes them to the audio side at once.

### The RACK tab (`scr_rack.c`)

```text
[ < RACK > ]                                    header: up from the strip
[ strip: modules as 25 x 25 sprites ]           EL_DIRECT: joystick left / right select the slot and scroll; the selected slot has a frame
( connection lanes: audio chain, modulator links )   (doubled while the strip has the focus) and an arrow above it
  description of the selected module            only when it fits the screen height
[Type]  [Insert]                                Type: what Insert adds
[Tgt]   [Prm]                                   a modulator's target module and parameter
[Dpth                ]                          modulation depth in the target's unit
[Delete              ]                          buttons Insert / Delete; Back (button 2) also deletes
```

The strip shows `RACK_VIS` cells (derived from `DISPLAY_WIDTH`) and scrolls over the 10 slots plus OUT; arrows at its ends show that more cells are hidden. `RACK_PITCH` in
`ui_internal.h` is the sprite width + 4 and a `_Static_assert` in `scr_rack.c` checks it against `MODULE_SPRITE_W`; the sprite size itself is set in `tools/gen_module_sprites.py`.

### Adding or converting a screen

Write its element table, its `layout()` and (if it has a picture) a `draw_extra()` in a `scr_*.c`, export a `screen_def_t`, declare it in `ui_screen.h` and return it from `screen_for_tab` (menu tab) -
the generic code then draws it and handles its events. For a screen outside the menu (a page of the main view) the routing in `synth_ui_handle` and `synth_ui_draw` has to be extended the same way;
the main view's pages are the remaining screens to convert. Callbacks that serve several rows use `ctx->arg`: see `scr_fx.c` (one function for the four effect parameters).

## 9. How the UI talks to the rest

- **Data in**: `synth_ui_draw` reads `rack_t`, `synth_params_t`, `seq_t` (const). Drawing never writes. (`synth_ui_draw` casts the const away only to build the context that the screens share with the event code.)
- **Changes out**: `synth_ui_handle` returns `true` when a *sound value* changed; `app_run_action` then calls `audio_set_params(rack, params)`. Structural changes set `ui->rebuild`;
  `app_step` then calls `audio_build`. Moving the focus or switching pages changes nothing in the data and only triggers a redraw.
- **Redraw policy** (`app_step` in `app.c`): the app sets `dirty` when an action touched the UI, and also redraws when the sequencer steps or an animation ticks *and* the current page
  shows a playhead (`synth_ui_shows_playhead`). Notes and the Shift modifier do not redraw. The reason is the real display (I2C): a full-frame flush is slow and must not starve the audio loop.
- **Parameters are generic**: every parameter set has `*_adjust(dir)`, `*_label`, `*_format`. That is what lets one list renderer show any of them and lets the knobs drive any row.

## 10. Typical changes and where to make them

| I want to... | Change |
| --- | --- |
| add a row to an existing main-view page | the page's `p[]` and `count` in its `*_pages[]` table in `ui_pages.c` (max 6 rows) |
| add a page to a module | a new entry in that module's `*_pages[]` and its count in `mod_pages[]` |
| change a label or a value format | the module descriptor table in `rack.c` (`label`, `unit`, `decimals`) |
| change what the graph box shows | the picture function for that page's `graph` in `ui_graphs.c` (it receives the box rectangle) |
| change fonts, spacing, column widths | `gui_default_style` / `gui_style_init` in `gui.c` |
| change the header | the first lines of `synth_ui_draw` (`ui_draw.c`) |
| change a menu tab: add an element, move one, change its text | the element table in that tab's `scr_*.c` (section 8) |
| change the look of a focused / latched element | `gui_draw_field_state`, `gui_draw_button` in `gui.c` |
| add a screen that is not a list | a declarative screen (section 8), or the old way: a `graph_t` value, a `draw_*`, a `handle_*` and a branch in `synth_ui_draw` and `synth_ui_handle` |
| change the screen size | `DISPLAY_WIDTH` / `DISPLAY_HEIGHT` in `hal/hal_display.h` |
| change what a button or knob does | `core/bindings.c` (not the UI) |
| change the module icons | `tools/gen_module_sprites.py`, then run it |

To look at a screen without the SDL window: `tools/ui_dump.c` prints it as ASCII after a script of control events (`UI_DUMP_SIZE=128x128` for a taller screen; see DEVELOPING.md).

## 11. What is still open

Observations about the current structure, not decisions:

1. **The main view is not declarative yet.** The pages of parameters, the sequencer, the motion sequencer, the multi-stage envelope and the FM page still have hand-numbered rows, a `handle_*` far
   from its `draw_*`, and no latch: there the joystick edits values directly and its push only activates. Until they are converted the joystick behaves differently in the menu and in the main view,
   and the column knobs (which act on rows 1..4 of a main-view page) have no sign on the screen of which row they drive.
2. **The old screens share two global behaviours**: one style with a single list-plus-graph layout, and rows that are label + value text only (no bars or other controls for a value). Page rows are
   limited to 6 per definition.
3. **Everything is redrawn on every change.** Fine on the desktop, the main cost on the real I2C display; there is no dirty-rectangle or partial update.
4. **No on-screen feedback for the new controls**: the octave, the Shift state, macro assignments and the last action are only visible in the simulator's Panel window.
5. **The graph pictures are sketches** drawn directly with u8g2 calls; they scale only because they take the box rectangle. There is no shared helper for axes, grids or labels.
6. **Small leftovers**: `in_rack` is the flag of the whole menu, not of the rack; `needs_mod` in `mpage_def_t` is not used by any table; the knobs do not act inside the menu.
