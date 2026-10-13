# The Ritual Machine's user interface

How the screens of the Ritual Machine are built, so you know where to cut before changing one. File and function names are
real. The drawing helpers it uses (rectangles, text, fields, sprites, popups) are the library's and have their own guide:
[DRAWING_KIT.md](DRAWING_KIT.md). What each control and each screen does for the player is in
[DEVELOPING.md](../../DEVELOPING.md) ("Quick start", "Screens"); this guide is about the code.

Paths without a prefix are inside `apps/ritual_machine/src/` (`core/...`, `app/...`); `ui/...` and `hal/...` are
`framework/ui` and `framework/hal`.

The UI has **two ways of writing a screen**, and knowing which one a screen uses is the first thing to find out:

- **Declarative** (the newer one): the screen is a table of elements, and one generic piece of code draws it, moves the focus
  and edits the values (section 6). Every tab of the menu and the Pads section's editor pages are written this way.
- **Page / handler** (the older one): a table says which parameters a page shows, and screens that are not a plain list have
  a `draw_*` function and a `handle_*` function written by hand (sections 4 and 5). The synth's main view, the Pads grid, the
  STEPS screen and the sample browser are written this way.

## 1. The files

| File | Role |
| --- | --- |
| `core/synth_ui.h` | the UI's public interface: `synth_ui_t` (all the cursor state), `ui_event_t`, the handle / draw calls of each screen family, knobs, macros, joystick XY, jump slots |
| `core/ui_internal.h` | private: what the UI files share (`graph_t`, `page_t`, `tab_t`, the row numbers of the sequencer pages, the picture functions) |
| `core/ui_pages.c` | what is on screen: the page tables, the page list generated from the rack, the list of menu tabs, jump slots |
| `core/ui_input.c` | what events do in the synth section: `synth_ui_handle`, row editing, the hand-written `handle_*`, knobs, macros, joystick XY |
| `core/ui_draw.c` | the synth section's frame: `synth_ui_draw`, the header, the parameter lists, the hand-drawn pages |
| `core/ui_graphs.c` | the pictures: waveform, envelope, filter response, effect sketches, the FM algorithm, the sample overview |
| `core/ui_screen.c`, `ui_screen.h` | the declarative-screen code: element tables, focus navigation, latch, drawing, which screen belongs to which tab |
| `core/scr_rack.c` | the RACK tab; the same screen shows the Pads section's FX chains |
| `core/scr_preset.c`, `scr_general.c`, `scr_samples.c`, `scr_fm.c`, `scr_keys.c`, `scr_leds.c`, `scr_mods.c`, `scr_macros.c`, `scr_joy.c`, `scr_curves.c` | one menu tab each (section 7) |
| `core/scr_pads.c` | the Pads section: the grid, the pad editor's pages, the FX module page, the STEPS screen |
| `core/scr_browse.c` | the sample browser: a folder of the card as a full-screen list, over either section; also browses the presets |
| `core/ui_sprites_gen.c`, `.h` | GENERATED from the PNGs of `assets/UI_Sprites/` (section 10) |
| `core/sprites.h`, `module_sprites.h` | hand-written small sprites; generated fallback module icons |
| `core/ui_settings.c`, `.h` | `ui.cfg`: what the UI saves on the card (knob mode, volume, jump slots, layers, LED colours) |
| `app/app.c` | one frame (`app_draw_frame`) and the step the main loop calls (`app_step`: it owns the redraw policy) |
| `app/app_actions.c` | `app_run_action`: turns an action into a UI event and gives it to the screen on top |
| `core/bindings.c`, `keymap.c`, `modifiers.c`, `controls.c` | which control triggers which action (not part of the UI: section 2) |
| `ui/gui.c`, `ui/popup.c` | the library's drawing kit and popups |

The data the UI edits lives elsewhere, as plain structs: `core/rack.h` (the modules and `cfg`, the general settings),
`core/seq.h`, `core/synth_params.h`, `core/pads.h`, `core/pad_seq.h`.

## 2. The big picture

```text
 board control                                   (hal/hal_input.h: the board's own id)
   └─> controls_event()                          the ROLE it plays for this app          core/controls.c, core/boards/map_<board>.h
         ├─ a matrix key ──> the key layout      its function                            core/keymap.c
         └─ anything else ─> Shift / Mod layers, then the binding table                  app/app_layers.c, core/bindings.c
               └─> app_run_action()              the action                              app/app_actions.c
                     └─> app_ui_event(ui_event_t)
                           ├─ the browser is open ──> browse_ui_event()                  core/scr_browse.c
                           ├─ the Pads section ─────> pads_ui_handle()                   core/scr_pads.c
                           └─ the synth section ────> synth_ui_handle()                  core/ui_input.c

 app_step() decides "redraw?" ──> app_draw_frame() ──> the screen on top, then the popup, then display_send()
```

- The UI is **immediate mode**: a frame is cleared and drawn whole from the data and the cursor state. Nothing else is kept
  between frames.
- The UI **never owns sound data**. It edits the plain structs above and reports back: a handle call returns `true` when a
  sound value changed, and sets a flag when something larger is needed (section 11).
- The UI **never knows which control was used**. It receives `ui_event_t` values; which control sends which is decided in
  the key layout, the layers and the binding table. To change what a control does, do not touch the UI.
- **Popups** (`ui/popup.h`) are drawn over any screen by `app_draw_frame`. While a modal one is up, `app_run_action` gives
  it the actions and the screen behind gets nothing.

### Who draws the frame

`app_draw_frame` (`app/app.c`) picks one, in this order:

| When | Drawn by |
| --- | --- |
| the keys-reset notice is up | `draw_keys_notice` (full screen; popups wait) |
| the boot animation plays (desktop only: on the board the board plays it) | `gui_anim_draw_centered` + `app_boot_draw_text` |
| the browser is open (`ui.br_open`) | `browse_ui_draw` |
| the Pads section | `pads_ui_draw` |
| otherwise | `synth_ui_draw` |

Then `popup_draw`, then `display_send`. Each of the three screen functions clears the buffer and builds its own style
(`gui_style_init` from the display's size).

## 3. The cursor state: `synth_ui_t`

One struct (`core/synth_ui.h`) holds every cursor, selection and request of the three screen families. All plain integers
and small arrays, plus a few pointers to data the app owns. The fields by group:

| Group | Fields | Meaning |
| --- | --- | --- |
| Where we are | `page`, `row`, `latched`, `in_rack`, `menu_tab`, `cog_page` | the main-view page; the focus (0 = the header, 1..n = rows or elements); a value is latched; **the menu is open** (`in_rack`: the name is historical); its tab; the page whose hidden settings are open |
| The page list | `pg_slot[]`, `pg_def[]`, `page_count` | generated from the rack (section 4) |
| RACK tab | `rack_row`, `rack_col[]`, `rack_cur`, `rack_scroll`, `rack_type`, `rack_menu`, `rack_dirty`, `rebuild` | the selected row and each row's cell; the selected module's slot; the scroll; the type Insert adds; the push menu is open; edits waiting / the engine must be rebuilt |
| Sequencer pages | `cursor`, `seq_voice`, `seq_tool`, `seq_scope`, `seq_msg`, `seq_knob_*`, `bank`, `seq_chain_cur`, `seq_rec` | the step and the note of its chord; the edit tool and what it just did; the step knobs' bookkeeping; the patterns (the app's); Rec armed |
| Other pages | `fm_op`, `fm_pt`, `ms_lane`, `ms_step`, `eg_pt` | the sub-selection of the FM, motion and envelope pages |
| Menu tabs | `smp_cur`, `smp_tgt`, `key_cur`, `mods_layer`, `mods_ctl`, `macro_cur`, `macro_dest`, `curve_cur`, `curve_pt`, `led_role`, `joy_tab`, `preset_name`, `preset_edited`, `preset_req` | each tab's own selection; the PRESETS tab's request to the app |
| Learn | `learn_req`, `learn_wait`, `learn_layer`, `learn_ctl`, `learn_macro` | a Learn button was pushed; the next push on a page row assigns it |
| Knobs and macros | `macro[]`, `knob_key[]`, `knob_pos[]`, `knob_catch_dir[]`, `knob_cur[]`, `knob_val[]`, `knob_away` | the 8 macros; what each knob last drove; the catch state of the four column knobs |
| Joystick XY | `joy[]`, `joy_prev[]`, `joy_xy`, `joy_fresh`, `joy_next`, `joy_rest[]`, `joy_centre[]`, `joy_wrote[]`, `joy_last[]` | what X and Y drive, XY mode on, and the bookkeeping of a push |
| Jump slots | `jump[]` | 8 saved locations, stored by what they point at |
| Pads section | `pads`, `pad_bank`, `pad_cur`, `pad_edit`, `pad_page`, `pad_row`, `pad_latched` | the pads (the app's); the bank and pad selected; grid or editor; the editor's page and its own focus |
| Pads: chop, edit | `chop`, `chop_pad`, `pad_chop_req`, `edit_op`, `edit_tonal`, `edit_amt[]`, `pad_render_req`, `pad_ver_changed`, `render_pct`, `history` | the CHOP page's cuts; the EDIT page's operation and amounts; requests to the app; the job's progress |
| Pads: FX, steps, capture | `pad_fx`, `fx_edit_id`, `fx_row`, `fx_latched`, `pbank`, `pad_steps`, `pseq_*`, `cap_state`, `cap_secs`, `clock_bpm` | the FX pages and their focus; step mode and its cursor; what the app reports (it runs, the step that plays, recording) |
| Browser | `br_open`, `br_req`, `br_done`, `br_state`, `br_dir`, `br_total`, `br_cur`, `br_first`, `br_n`, `br_page[]`, `br_info`, `br_pad`, `br_target`, `br_ms` | what it is open for, the folder, the highlight, the rows it holds, the highlighted file's overview |

`synth_ui_init` sets the starting state; `synth_ui_sound_loaded` / `_new` put the cursors back on something that exists
after a preset replaced the sound.

## 4. The synth's main view: pages

The main view is a list of pages. The header shows `< PAGE NAME >`; left / right on the header (or Page - / + from any row)
change the page.

**The list is generated from the rack** (`synth_ui_rebuild_pages`, `ui_pages.c`), at start and when the menu closes with
structural edits:

| Synth type | Pages, in order |
| --- | --- |
| Modular | every module's pages in rack order, then AMP ENV, AMP CURVE, SEQUENCER, SEQ SETUP, PATTERNS |
| FM | FM SYNTH, then OP1, OP1 ENV .. OP6, OP6 ENV, then SEQUENCER, SEQ SETUP, PATTERNS, then the pages of row M's effects |
| Strings | STRINGS, STR TONE, VOICE LP, STR FILTER, AMP ENV, SEQUENCER, SEQ SETUP, PATTERNS, then the pages of row M's effects |

The list is two parallel arrays: `pg_slot[i]` (the rack slot that owns page i, or `GLOBAL_PAGE`) and `pg_def[i]` (which of
that module type's page definitions, or which global page). At most `SYNTH_UI_MAX_PAGES` = `RACK_MAX` x 4 + 5.

A **module page definition** says what to show:

```c
typedef struct { const char *fmt; graph_t graph; int count; int p[6]; bool needs_mod; } mpage_def_t;
//   fmt        the title; "%d" = the module's instance number ("OSC %d" -> "OSC 2")
//   graph      which picture goes in the box (GRAPH_WAVE, GRAPH_ENV, GRAPH_FILTER, GRAPH_FX ...), or which hand-written page it is
//   count      the number of rows (6 at most)
//   p[]        for each row, the index of the parameter in the module's slot.v[] (or a pseudo index, below)
//   needs_mod  only while the module acts as a modulator (no table uses it today)
```

The tables are at the top of `ui_pages.c` (`oc_pages[]`, `fl_pages[]`, `lf_pages[]`, `en_pages[]`, `eg_pages[]`,
`sm_pages[]`, one per effect ...), tied to module types by `mod_pages[]`. Global pages use `gpage_def_t` in `global_pages[]`
(`GP_AMP_ENV`, `GP_SEQ` ...). **`GP_*` and `tab_t` values are saved in `ui.cfg` by the jump slots: append, never reorder.**

`get_page(ui, rack, idx, &page_t)` turns an entry of the list into a `page_t` (title, graph, row count, parameter indexes,
slot). Both `synth_ui_handle` and `synth_ui_draw` call it, which is how the two agree on what a page holds.

- **Pseudo indexes**: `PRM_TGT` and `PRM_TPRM` are a modulator's target module and parameter (the `... DEST` pages): they
  live in the slot, not in `slot.v[]`. `page_row_step` (editing) and `synth_ui_draw` (display) both test for them.
- **The cog**: a module with rarely used settings (`mod_hidden[]`: the oscillator's Qual / Mute / Harm, Spectral's
  Hold / Lo / Hi) has a cog on its first page. It is the row after the last one; a push, left or right on it opens the hidden
  settings in place of the page's rows (`ui->cog_page`, the title gets " SET"), Back closes them.

### How a main-view page is drawn (`synth_ui_draw`, `ui_draw.c`)

1. The style from the display's size, the buffer cleared, the font selected.
2. **The top bar** (16 px, its last pixel row is the rule): `< TITLE >`, filled when the header has the focus. In the menu the
   tab's icon (`16/tab_<name>`) sits at its left. With joystick XY mode on, the mark `8/ui_xy` sits at its right.
3. Then one of:
   - the menu is open: `screen_draw` of the tab's screen (section 6);
   - a hand-written page (section 5): its `draw_*`;
   - **a module's page** (on the 8 px grid): the rows in the left half, 8 px each, over the first six grid rows; the right half
     is `draw_module_head` (the module's 24 px icon, three info lines, the cog); the picture takes the full width below;
   - **a global page**: a list column on the left (`style.list_w`), the picture box on the right (`style.graph`).
4. Each row is one `gui_draw_field` (label, value, filled when focused). A column knob that has not caught its value yet
   shows a small chevron at the row's right edge, pointing the way to turn.
5. The picture, chosen by `page.graph`: `draw_wave`, `draw_env`, `draw_filter`, `draw_fx_module` ... (`ui_graphs.c`). Each
   receives the box rectangle and scales to it.

The frame is not sent here: `app_draw_frame` adds the popup, then sends.

### Events in the main view (`synth_ui_handle`, `ui_input.c`)

The main view understands only the older events, so the newer ones are **translated first**: `UI_NAV_*` become
`UI_UP / DOWN / LEFT / RIGHT`, `UI_VALUE_INC / DEC` become `UI_RIGHT / LEFT`, `UI_LATCH` becomes `UI_SELECT`. The result:
the joystick moves between rows and changes the value directly, encoder B changes the value, and a push acts like "right" on
the row (it runs a sequencer tool on the Tool row, opens the cog on the cog). **There is no latch in the main view.**

Row editing goes through `page_row_step`, which finds what the row is (a module parameter, a pseudo index, an FM value
through `fm_page_row`, a sequencer or global parameter) and steps it by one.

## 5. Hand-written pages of the main view

Some pages are not "rows of parameters". `synth_ui_draw` (drawing) and `page_row_step` (editing) pick them by `page.graph`:

| Page | Selected by | Drawn by (`ui_draw.c`) | Edited by (`ui_input.c`) |
| --- | --- | --- | --- |
| SEQUENCER (piano roll, the step's values, the tools) | `GRAPH_SEQ` | `draw_seq` | `handle_seq`; the step knobs: `seq_knob` |
| SEQ SETUP | `GRAPH_SEQ_CFG` | generic rows + `draw_seq_cfg` | `seq_param_adjust` through `page_row_step` |
| PATTERNS (patterns and chain) | `GRAPH_SEQ_PAT` | generic rows (`seq_pat_row_text`) + `draw_seq_pat` | `handle_seq_pat` |
| MOTION n (steps of a lane) | `GRAPH_MS_STEPS` | `draw_ms_steps` | `handle_ms` |
| MS n LANE | `GRAPH_MS_LANE` | `draw_ms_lane` | `handle_ms` |
| EG n (the multi-stage envelope's points) | `GRAPH_EG` | `draw_eg_page` | `handle_eg` |
| FM SYNTH, OPn, OPn ENV | `GRAPH_FM`, `GRAPH_FM_OP`, `GRAPH_FM_ENV` | generic rows (`fm_row_text`) + `draw_algo` / `draw_eg_editor` | `fm_page_row` says what each row edits |

The rows of the sequencer pages are numbered by hand (`SEQ_ROW_*`, `PAT_ROW_*` in `ui_internal.h`), and the numbers in the
handler and in the drawing must agree. That is what the declarative model replaces.

## 6. Declarative screens (`core/ui_screen.h`)

A declarative screen is a **table of elements**. The same table drives the drawing, the focus and the editing, so a screen
cannot disagree with itself about what "row 3" is.

An element (`el_def_t`):

| Field | Meaning |
| --- | --- |
| `label`, `label_dyn()` | the fixed label, or one computed from the state (an effect's parameter names) |
| `kind` | `EL_VALUE`, `EL_DIRECT` or `EL_BUTTON` (below) |
| `row`, `col`, `span` | its place in a small grid, used by the joystick's navigation |
| `value(ctx, out, n)` | the text shown as its value; a button with a value shows it as its caption |
| `adjust(ctx, dir)` | one step up or down; returns true when a sound value changed |
| `activate(ctx)` | do it (buttons; a selector that opens what it selects) |
| `draw(...)` | only for elements that are not a plain field or button (the rack's rows) |
| `enabled(ctx)` | `NULL` = always; a disabled element is skipped by the focus and drawn as `n/a`, or not at all with `EF_HIDE_WHEN_DISABLED` |
| `arg` | handed to the callbacks as `ctx->arg`, so one function serves a family of rows |

Every callback receives a `ui_ctx_t {ui, rack, arg}`.

A screen (`screen_def_t`) is the table plus:

| Field | Meaning |
| --- | --- |
| `layout(g, st, ctx, area, rect[])` | gives every element its rectangle inside the area under the header. `ui_layout_column()` is the ready-made "one column of rows on the left"; `ui_picture_box(st)` is the box on the right |
| `draw_extra(...)` | drawn before the elements: the picture, the links between the rack's modules |
| `compact` | on a screen under 100 px high, draw with padding 0 and gap 0 so more rows fit |
| `use_latch` | a push latches an `EL_VALUE` so the joystick edits it (dense multi-column screens) |
| `back(ctx)` | what Back does |
| `after_edit(ctx)` | called after anything changed (keep a selection valid, mark the rack dirty) |

The focus is `ui->row`: 0 = the header, element i has it when `ui->row == i + 1`. A screen holds at most 24 elements
(`MAX_ELEMENTS` in `ui_screen.c`; the pad editor's PLAY page has 17).

The three kinds, and what the controls do on them (`screen_event`):

| Kind | Joystick | Joystick push | Encoder B | Examples |
| --- | --- | --- | --- | --- |
| `EL_VALUE` | moves the focus to the neighbour; with no neighbour on that side it changes the value. On a `use_latch` screen, once latched, left / right change the value | latch / release (`use_latch` screens only) | changes the value | Type, Tgt, Voices, Algo |
| `EL_DIRECT` | left / right change it, up / down move the focus | runs `activate()` if set | changes it | the RACK tab's three rows; Op of the ALGORITHM tab |
| `EL_BUTTON` | moves the focus | activates | its push activates | Insert, Delete, Assign, Load |

- **The joystick moves spatially** (`nav`): left / right to the nearest enabled element of the same grid row, up / down to
  the nearest row that has one, then the element closest in column. Up from the first row reaches the header.
- **Encoder A moves linearly** (`linear`): through the header and the enabled elements, in table order.
- On the header, left / right change the tab. When an edit disables the focused element, the focus falls back to the one
  before it (`fix_focus`).
- Drawing is clipped to the area under the header, so a list longer than the screen can scroll rows out of it.
- Looks: focused = a frame around the field, latched = filled with the value between `<` `>`; a button is a frame, filled
  when focused (`gui_draw_field_state`, `gui_draw_button`).

### Adding a screen

Write its element table, its `layout()` and, if it has a picture, a `draw_extra()` in a `scr_*.c`; export a `screen_def_t`
and declare it in `ui_screen.h`. For a new menu tab: a value at the end of `tab_t`, its name and icon in `tab_name` /
`tab_icon`, its place in `tab_kind` / `tab_count` (`ui_pages.c`) and a case in `screen_for_tab`. For a screen outside the
menu, call `screen_event` and `screen_draw` yourself, as `scr_pads.c` does for the pad editor.

## 7. The menu and its tabs

Menu (Button 1, or the key function Menu) opens and closes it. Closing it with structural edits regenerates the pages and
asks the app to rebuild the engine. The tabs depend on the synth type (`tab_kind`, `ui_pages.c`):

| Synth type | Tabs, in order |
| --- | --- |
| Modular | RACK, PRESETS, GENERAL, SAMPLES, KEYS, LEDS, MODIFIERS, MACROS, JOY, CURVES |
| FM | RACK, PRESETS, GENERAL, ALGORITHM, KEYS, LEDS, MODIFIERS, MACROS, JOY, CURVES |
| Strings | RACK, PRESETS, GENERAL, KEYS, LEDS, MODIFIERS, MACROS, JOY, CURVES |

| Tab | File | Elements | Picture / extra |
| --- | --- | --- | --- |
| RACK | `scr_rack.c` | the three rows (FM / Strings: row M only), and the push menu (below) | the rows, their links, a line describing the selection |
| PRESETS | `scr_preset.c` | Load, Save, Save as, Init: each only raises `ui->preset_req`, the app does the work | the preset's name, `edited` or `saved` |
| GENERAL | `scr_general.c` | Type, Patch (FM), Voices, Glide and Legato (Voices 1), Vol, Out, Spk, Knob | the info box (`draw_synth_info`) |
| SAMPLES | `scr_samples.c` | File (a push opens the browser), Tgt, Assign | the file's overview, length and root note |
| ALGORITHM | `scr_fm.c` | Algo, Fb, Op (left / right pick an operator, a push opens its OPn page) | the operator tree, the selected operator inverted |
| KEYS | `scr_keys.c` | Layout, Key, Func, Reset | the keys with their functions |
| LEDS | `scr_leds.c` | Role, Idle, Idle %, Active, Act %, Reset | the keys of that role filled |
| MODIFIERS | `scr_mods.c` | Layer, Ctrl, Func, Min, Max, Curve, Learn, Reset | the controls with their function in the layer |
| MACROS | `scr_macros.c` | Macro, Dest, Tgt, Min, Max, Curve, Learn, Remove | the selected destination's response |
| JOY | `scr_joy.c` | Axis, Tgt, Min, Max, Curve, Clear | the axis' response, XY mode on or off |
| CURVES | `scr_curves.c` | Curve, New, Point, X, Y, Seg, Add, Del pt, Delete | the curve and its points |

All but RACK use `ui_layout_column` (a list on the left, the picture on the right) and no latch. Edits that change the
structure (a module inserted or deleted, a target, the type, the voices, the FM patch) set `rack_dirty` or `rebuild`; edits
that are live (volume, an effect's value, FM operator values) return `true` so the app pushes them to the engine at once.

### The RACK tab (`scr_rack.c`)

```text
[ < RACK > ]                                    the header: up from row 1
[ 1: its modules, +, OUT (to the MIX) ]         three EL_DIRECT rows: left / right pick the cell, up / down the row; the selected
[ 2: the same ]                                 cell has a frame (doubled while its row or its menu has the focus); a push opens
[ M: MIX, its modules, +, OUT ]                 the menu; Back deletes the selected module
  one line describing the selection ("1 FL1 Filter PARA")
the menu (a popup over the rows, only the rows that apply): Edit, Type, Insert, Tgt, Prm, Dpth, Row, Para, PEnv,
  Lvl1, Pan1, Lvl2, Pan2, Delete; Back closes it
```

- Rows 1 and 2 are the two branches (same notes and voices, summed into row M's MIX); row M runs once on that sum. FM and
  Strings show row M only (their voices feed it).
- A row's cells: (row M of a rack synth: the MIX), its modules in slot order, a `+` cell (while the rack has fewer than
  `RACK_MAX` = 16 modules) and its OUT.
- **Insert** puts the Type before the selected module, or at the row's end on the `+` cell, and is hidden where the rules
  forbid it (`rack_can_insert`). **Row** moves the module to another row that accepts it. **Para** switches the branch to
  shared from that module on; **PEnv** shows on the branch's shared start. The MIX cell's menu holds each branch's level and
  pan.
- Drawing: with more than one voice a per-voice module has a card behind it (a stack), a shared one is flat; the shared
  start carries the Para badge (`8/ui_para`). Audio links run inside a row; modulation links (dotted) may cross rows.
- The menu rows are ordinary elements, enabled only while `ui->rack_menu` is set: `layout()` stacks the enabled ones into
  the popup.
- The rows share one horizontal scroll (`rack_scroll`) so columns line up. A cell is `RACK_PITCH` = 32 px wide (the 24 px
  icon and one grid cell), four across.
- **The same screen shows the Pads section's FX chains**: there the `rack_t` is `pads->fx` (its `fx` flag set), the rows are
  FXA / FXB / FXC, there is no MIX, no Para and no modulators, and a module's menu starts with **Edit** (its values).

## 8. The Pads section (`core/scr_pads.c`)

Shift + Menu switches Synth <-> Pads. In Pads, `pads_ui_handle` and `pads_ui_draw` take over; they pick a screen from the
state:

| State | Screen | Written as |
| --- | --- | --- |
| `pad_fx == 1` | the FX chains | `scr_rack_screen` on `pads->fx` (declarative) |
| `pad_fx == 2` | one FX module's values (`fxmod_screen`) | declarative, `ui_layout_column` |
| `pad_steps` and not `pad_edit` | STEPS: the selected pad's steps (step mode) | by hand: `draw_steps`, `steps_event` |
| `pad_edit` | the pad editor, page `pad_page`: 0 PLAY, 1 TONE, 2 CHOP, 3 EDIT | four declarative screens, all with `use_latch` |
| otherwise | the grid: 4 x 4 cells of the bank shown | by hand: `draw_cell`, `grid_event` |

- **The header** is drawn by `pads_ui_draw` for all of them: the tempo at the left, then the bank and bar.beat (grid),
  `< A1 name >` or `< A1 TONE >` (editor), `A1 STEPS 2/4` or `REC` (step mode), the routing and the estimated load (FX).
- **The grid**: the joystick moves the selection, encoder A steps through the pads, encoder B or Page changes the bank, a
  push opens the editor. A pad that sounds is drawn inverted.
- **The editor**: left / right on the header (or Page - / + from any row) change the page; Back goes to PLAY, then to the
  grid. PLAY's layout is the sample overview across the top, the sample row, then two columns of 8 px rows. A push on the
  sample row asks for the browser (`br_req`).
- **STEPS**: the joystick moves the cursor over the steps and its push switches the hit; encoder A picks the field
  (`PSF_*`), encoder B changes it.
- **A focus of its own**: the declarative code reads `ui->row` and `ui->latched`. The Pads screens keep their own
  (`pad_row` / `pad_latched`, `fx_row` / `fx_latched`) and **lend** them for the length of a call: `pads_ui_handle` and
  `pads_ui_draw` copy them into `ui->row` / `ui->latched`, call `screen_event` / `screen_draw`, and put the synth's back.
  A new declarative screen outside the menu has to do the same.
- The pictures (`draw_extra`, `tone_extra`, `chop_extra`, `edit_extra`) draw the sample overview with the trim, the loop,
  the cuts, or the filter's response.

## 9. The sample browser (`core/scr_browse.c`)

One folder of the card as a full-screen list, with the overview of the highlighted sample on top. It opens over either
section and has its own event and draw functions (`browse_ui_event`, `browse_ui_draw`): it is not a table screen.

| Opened for (`br_open`) | By | The choice goes to |
| --- | --- | --- |
| `BROWSE_PAD` | a push on the pad editor's sample row | the pad, at once: its key plays the highlighted file; Back puts the pad back as it was |
| `BROWSE_RACK` | the SAMPLES tab's File row | that tab's File |
| `BROWSE_PRESET` | the PRESETS tab's Load | the sound: the highlighted preset plays under the keys; Back brings the sound back |

A screen only sets `ui->br_req`; the app opens the browser (`app/app_library.c`, `app_browse_open`), because reading a
folder is card work. Up / down move the highlight, Page - / + by a screen; right or a push opens a folder, left or the `..`
row goes to its parent; a push on a file chooses it (`br_done = 1`), Back cancels (`br_done = 2`). The screen holds only the
rows it shows (`br_page[]`); the app reads the highlighted file once the highlight has rested for `BROWSE_REST_MS`.

## 10. Sprites and icons

- **From PNG files**: `assets/UI_Sprites/<size>/<name>.png` (sizes 8, 16, 24, 64) become `core/ui_sprites_gen.c` / `.h` by
  `tools/gen_ui_sprites.py`, which `build.ps1` and the firmware build run before every build. In code, a sprite is looked up
  by name: `ui_sprite("16/tab_rack")` returns it, or `NULL` when there is no such image yet. The image rules, the names the
  UI looks for and the placeholders are in `assets/UI_Sprites/README.md`.
- **Every use has a fallback**, so art can arrive one file at a time: a module without an image takes the generated icon of
  `core/module_sprites.h` (`tools/gen_module_sprites.py`: a rounded square with a two-letter code), a tab without an icon
  shows its name alone, the cog and the XY mark fall back to a few drawn pixels.
- **Hand-written**: `core/sprites.h` (play, stop, the four-frame equalizer shown while the sequencer runs).
- **One font**: `u8g2_font_5x7_tr`, the drawing kit's.

## 11. How the UI talks to the rest

- **Data in**: the draw functions read `rack_t`, `synth_params_t`, `seq_t`, the pads, as `const`. Drawing never writes
  (the casts in `synth_ui_draw` and `pads_ui_draw` only build the context the screens share with the event code).
- **A sound value changed**: the handle call returns `true`; `app_ui_event` then calls `audio_set_params(rack, params)`
  (the Pads section: `app_pad_changed`).
- **Something larger is needed**: the UI sets a flag and the app acts on it, because the work needs the engine or the card:

| Flag (in `synth_ui_t`) | Set when | The app |
| --- | --- | --- |
| `rebuild` | the menu closed with structural edits | `audio_build` (`app_step`) |
| `rack_dirty` (Pads FX pages) | the chains were edited | sends and saves them (`fx_changed`) |
| `br_req`, `br_done` | a screen asks for the browser; the browser was answered | opens / closes it, applies the choice (`app_library.c`) |
| `preset_req` | a button of the PRESETS tab | loads, saves, asks first (`app_preset.c`) |
| `learn_req` | a Learn button | closes the menu and waits for a push on a row (`app_layers.c`) |
| `pad_chop_req` | Chop to pads | asks, then writes the slices (`app_pads.c`) |
| `pad_render_req`, `pad_ver_changed` | Apply on the EDIT page; Ver / Undo / Redo | runs the job, sends the pad (`app_render.c`) |
| `pseq_rec_req` | the STEPS screen's Rec field | arms / disarms the live record (`app_pseq.c`) |
| `seq_msg` | a sequencer tool ran | shows it as a popup |

- **State the app writes for the screens**: `seq_rec`, `pseq_run`, `pseq_pos`, `pseq_rec`, `cap_state`, `cap_secs`,
  `clock_bpm`, `render_pct`, `preset_name`, `preset_edited`, `knob_away`, and the pointers `pads`, `bank`, `pbank`, `history`.
- **Redraw policy** (`app_step`, `app/app.c`): the app sets `dirty` when an action touched the UI, and also redraws when the
  sequencer stepped or an animation ticked *and* the page shows a playhead (`synth_ui_shows_playhead`). Notes and the Shift
  modifier do not redraw. While more input is queued (`input_pending()`) the redraw is owed, not done: every pending note
  and knob turn is handled first.
- **Knobs do not go through events.** The app calls `synth_ui_knob_row` (the column knobs: rows 1..4 of the current page),
  `synth_ui_knob_row_target` / `synth_ui_knob_target` (a Shift / Mod target through its mapping), `synth_ui_macro*` (macros),
  `synth_ui_joy_axis` (joystick XY) and `synth_ui_set_volume`. A parameter only knows "one step up / down", so `knob_set`
  walks it to the step that matches the knob's position; continuous parameters are set directly. Rows that select or cycle
  are skipped (`row_is_knobbable`). The catch (a knob is ignored until it crosses the current value) is kept per column knob
  in `knob_catch_dir[]`; what the player sees is in DEVELOPING.md, the controls table of "Quick start" (row "Column knobs 1..4").
- **Parameters are generic**: every parameter set has `*_adjust(dir)`, `*_label`, `*_format`. That is what lets one list
  renderer show any of them and lets the knobs drive any row.
- **What is saved**: the UI's own settings in `system/config/ui.cfg` (`core/ui_settings.c`, written 2 s after a change); the
  macros and the joystick's bindings belong to the sound (`core/patch.h`: `synth.cfg` and the presets).

## 12. Typical changes and where to make them

| I want to... | Change |
| --- | --- |
| add a row to a main-view page | the page's `p[]` and `count` in its `*_pages[]` table in `ui_pages.c` (6 rows at most) |
| add a page to a module | a new entry in that module's `*_pages[]` and its count in `mod_pages[]` |
| hide a rarely used setting behind the cog | `mod_hidden[]` in `ui_pages.c` |
| change a label or a value's format | the module descriptor table in `core/rack.c` (`label`, `unit`, `decimals`) |
| change what the picture box shows | the picture function for that page's `graph` in `ui_graphs.c` |
| change a menu tab: add an element, move one, change its text | the element table in that tab's `scr_*.c` (section 6) |
| add a menu tab | section 6, "Adding a screen" |
| change a Pads editor page | its element table and `*_layout` in `scr_pads.c` |
| change the look of a focused / latched element | `gui_draw_field_state`, `gui_draw_button` in `framework/ui/gui.c` (it changes every app) |
| change fonts, spacing, the list column's width | `gui_default_style` / `gui_style_init` in `framework/ui/gui.c` |
| change the top bar | the first lines of `synth_ui_draw` (`ui_draw.c`) and of `pads_ui_draw` (`scr_pads.c`) |
| add a screen that is not a list | a declarative screen (section 6); or the old way: a `graph_t` value, a `draw_*`, a `handle_*` and a branch in `synth_ui_draw` and in `page_row_step` |
| change an icon | paint its PNG in `assets/UI_Sprites/`, then build |
| change the screen size | `DISPLAY_WIDTH` / `DISPLAY_HEIGHT` in `framework/hal/hal_display.h` |
| change what a button or a knob does | `core/bindings.c`; a matrix key: the key layout (`core/keymap.c`, the KEYS tab); with Shift / Mod: the MODIFIERS tab (`core/modifiers.c`, defaults in `modifiers_init`) |
| give a new board's control a role | `core/boards/map_<board>.h` |
| raise a message | `popup_info` / `popup_error` / `popup_ask` on `app->popup` ([DRAWING_KIT.md](DRAWING_KIT.md), "Popups") |

## 13. Checking a change

From the repo root unless said otherwise.

- **The simulator**: `.\build.ps1`, `.\run.ps1` (the panel board, 4 key columns); `.\build.ps1 -Board hwv1`,
  `.\run.ps1 -Board hwv1` (the first prototype, 8 key columns). Each board is its own program: build the one you run.
- **The UI tests**: `.\apps\ritual_machine\tools\build_ui_tests.ps1` (`-Filter fm` for the tests whose name contains the
  text). They drive the application with control events on stand-ins for the platform (`tools/ui_stubs.c`): no window, no
  sound. The files are `apps/ritual_machine/tests/ui/test_*.c`. **Some address rows by number** (the RACK menu's rows in
  `test_rows.c`, the EDIT page's in `test_pads.c`): inserting a row in those screens shifts them.
- **A screen as text**: `apps/ritual_machine/tools/ui_dump.c` prints a frame as ASCII after a script of control events
  (the build line is at the top of the file; `UI_DUMP_SIZE=128x128` for the taller screen). With no argument it shows the
  main view, then every menu tab of the modular and of the FM synth.

## 14. What is still open

Observations about the current structure, not decisions.

1. **The main view is not declarative.** Its pages still have hand-numbered rows, a `handle_*` far from its `draw_*`, and no
   latch: the joystick edits values directly there and moves the focus in the menu. Nothing on the screen says which row a
   column knob drives.
2. **Navigation and layout are not tied together.** On a declarative screen the joystick goes by the `row` / `col` typed in
   the element table (`nav`), while the pixels come from `layout()`. The RACK tab's layout reads the typed numbers back to
   build its grid, so the two agree; the list tabs stack the elements in table order and ignore the numbers, so they agree
   only because every element has `col 0` and a row equal to its index. Move an element in a `layout()` without changing its
   `row` / `col` and the joystick no longer follows what the eye sees. A way out that was proposed and not built: no position
   in the table at all, and `nav` picks the nearest element from the rectangles `layout()` produced.
3. **Three screens keep their own focus** (the synth's `row` / `latched`, the pad editor's, the FX pages') and lend it to the
   shared code for each call (section 8). It works, and it is easy to get wrong in a new screen.
4. **Rows are a label and a text.** No bar or other control shows a value; a main-view page holds 6 rows at most.
5. **Everything is redrawn on every change.** The first prototype's display task sends only the changed tiles, which hides
   most of the cost; the drawing itself is still done whole.
6. **The pictures are sketches** drawn with u8g2 calls; they scale because they take the box rectangle. There is no shared
   helper for axes, grids or labels.
7. **Small leftovers**: `in_rack` is the flag of the whole menu, not of the rack; `needs_mod` in `mpage_def_t` is used by no
   table; `TAB_FM_OP`, `TAB_FM_ENV` and `TAB_FX` stay in `tab_t` because saved jump slots hold the values; the column knobs
   do not act inside the menu; `gui_style_t` (the library's) still carries this app's page metrics.
