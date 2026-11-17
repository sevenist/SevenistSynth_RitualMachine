# GUI rework for a bigger node system: a proposal (branch `gui-nodes`)

**Status (2026-11-16): a proposal, nothing built, nothing decided.** Written at the user's request ("propose a gui rework to navigate a more
complex node system ... make it colored, use the new screen of 128*160"). Colour mock-ups of the three levels: [images/gui_nodes_mockup.svg](images/gui_nodes_mockup.svg)
(each frame is 128 x 160, drawn at 3x). Decisions the user must take are in section 9.
The same three frames run on the board in [apps/gui_mockup](../../gui_mockup/platformio.ini): a firmware of one file (`src/main.cpp`) with only the panel driver, the colour canvas and u8g2.

## 1. The problem

The rack is a strip: 16 slots on three rows, four cells visible at a time (`RACK_PITCH` 32 px, `RACK_VIS` 4), cables implied by the order. That
works for a chain. A node system has **branches, shared modulators, several cables per input and a patch of 30+ modules** (the Studio's PATCH
tab already builds those). On a 128 x 160 screen driven by two encoders, a joystick and four buttons, three things break:

1. **Where am I?** A strip of four cells shows no neighbours, no cables, no sense of the whole.
2. **What is connected to what?** A cable is not drawn; you read it by opening the module.
3. **Making a cable** has no gesture. A mouse drags; we have no pointer.

The screen is also 1-bit in the app (u8g2 white on black), so a family of modules, a signal type or a "this value is modulated" cannot be told
apart except by shape. The board's panel is already colour (RGB565, `display_send_rgb565`), and `framework/ui/canvas.h` lets the same u8g2 calls paint
colours. This rework uses that.

## 2. The idea in one picture: three zoom levels

You never pan a free canvas. You **zoom between three levels**, and at each level the joystick moves a cursor between *things*, not pixels.

| Level | Shows | Cursor moves between | Enc A push | Back |
| --- | --- | --- | --- | --- |
| **L0 MAP** | the whole patch, auto laid out, cables drawn and coloured | nodes (follows cables) | dive into the node | (top: nothing / menu) |
| **L1 NODE** | one node: its picture, its values, its ports with where each goes | rows (values, ports) | edit / follow the cable | back to the map, cursor kept |
| **L2 PICK** | a list: the sources for an input, or the modules to add | list items (grouped, coloured) | confirm | cancel |

A fourth, optional **L-1 OVERVIEW** (tiny 8 x 6 px tiles, all 16 slots at once) is the same map drawn smaller when the patch has more nodes than the map shows;
Enc A turning counter-clockwise from L0 reaches it. It is read-only navigation: the cursor jumps, then you zoom back in.

## 3. L0 MAP: the whole patch

- **Layout is automatic**, never hand-placed: signal flows **top to bottom** (portrait suits it). A node's row is its depth from the note input; parallel
  branches are columns (rack rows 0 and 1 are already two branches; the modulators (row M) sit in a side column). No layout data is saved in a preset.
- **A tile** is 28 x 14 px: a 2 px coloured left band (family), a 3-letter label (`OSC`, `FLT`, `LFO`), a one-pixel-wide bar under it for the module's
  *main value* (cutoff, mix, level) so the map doubles as a quick view of the sound. 4 columns x 6 rows = 24 tiles at 128 x 100 px.
- **Cables** are orthogonal lines, 1 px, in the **signal colour** (section 5). A modulation cable is dashed and ends in a dot on the target tile's side, so
  it is never confused with the audio path.
- **The cursor** is a 2 px white frame. Joystick: **down / up follow the signal** (to the next / previous node on the audio path), **left / right
  switch branch** (the nearest tile on that side). Held Shift: joystick moves to the next node *of the same colour* (all the LFOs, all the filters).
- **The info strip** (28 px under the map): the selected node's full name and its colour dot, two key values, and its **ports as chips**
  (`in <- OSC`, `out -> FLT, CHR`) so a cable is read without opening anything. It is the same strip at every level (the "legend zone" already in `ui_legend.c`).
- A node that is **muted or bypassed** is drawn with its fill dimmed (not removed: the cable keeps going). A node over budget (the cycles wallet) gets
  a red corner pixel.

## 4. L1 NODE: one module, readable

Header: the family colour as a 12 px band with the module's name and its position (`FLT  3/11`). Then:

- the module's **picture** (the waveform, the filter response, the envelope: the ones in `ui_graphs.c`, now drawn in the family colour), 128 x 44;
- **value rows**, 12 px each, with a coloured bar behind the value. A value that is **modulated** shows a small amber arrow and its live position as a second, thinner bar, so you see the LFO move the cutoff;
- a **ports section**: one row per input and output (`in  pitch  <- NoteIn`, `in  cutoff <- LFO 2  +40%`, `out  -> VCA`). Focus on a port row and Enc A push = jump to the node on the other end (that is how you follow a cable through the patch, and Back returns).

Enc A edits the focused value (as now); Enc B turns the page through the node's pages (values / ports / macros) when there is more than one.

## 5. Colour: what it means, and a rule that never relies on colour alone

Background is pure black (`#000000`: the near black `#0B0E14` showed as a lifted grey on the panel, next to the boot animation's black), text off-white (`#E8ECF2`). The palette is built for the ST7735S: saturated, not subtle (its viewing angle is poor, its grey ramp muddy).

| Family (tile band) | Colour | Which modules |
| --- | --- | --- |
| Source | green `#3DDC84` | Osc, Sampler, Strings, Pads |
| Shaper | violet `#A87BFF` | Filter, Saturation, Resonator, EQ |
| Modulator | amber `#FFB020` | LFO, Envelope, Multi Env, Motion Seq, Velocity, Macro |
| Effect | cyan `#35C2E8` | Delay, Reverb, Chorus, Phaser, Flanger, Spectral, Cab |
| Utility | grey `#8A93A3` | Mixer, VCA, Math, Func, Range, Slew |
| Out | red `#FF5A5F` | VoiceOut, MasterOut |

| Cable | Colour and style |
| --- | --- |
| Audio | off-white, solid |
| Control (continuous modulation) | amber, dashed |
| Gate / trigger | magenta `#FF4FD8`, dotted |

**Rule:** colour is a second channel, never the only one. Every family also has its 3-letter label, every cable its dash pattern, every state (muted, over budget, selected)
a shape or position as well. The 1-bit boards (HWV1, the simulator in 1-bit mode) draw the same screens with the shapes only; `canvas.h` already maps a colour
frame to lit / unlit pixels for them (section 7).

Colours are **a theme** (one table, `ui_theme.[ch]`): the family colour, the cable colour, background, text, cursor. A user theme (a few colours in `ui.cfg`) comes for free later.

## 6. Making and breaking cables (the missing gesture)

No pointer, so **grab and drop with the cursor**:

1. On L0 or L1, put the cursor on an **output** (L0: a node; L1: an output row) and **hold Button 1 (Patch)**. The cursor becomes a flying cable end, drawn in the signal colour.
2. Move: the joystick now jumps between **compatible inputs only** (an audio output offers audio inputs; a modulation output offers the modulatable values). Incompatible tiles dim.
3. **Release**: connected. If the target value already has a source, an L2 list asks *Replace / Add (sum) / Cancel*.
4. **Unpatch**: on a port row (L1), Button 1 + Back removes that cable; on L0 the cable under the cursor's link chip.

Insert a module on a cable: on L0, the cursor on a cable (Shift + joystick down/up selects links) and Enc A push opens the L2 module picker; the new node is spliced in.
Add / delete a module keeps the current rules (Back never deletes, ADR-046): the delete is the picker's own item, with a confirmation.

## 7. How it is built (small steps, nothing thrown away)

- **A colour canvas for the app.** The app draws into a `canvas_t` (`framework/ui/canvas.h`) of 128 x 160 (40 KB of RGB565 in PSRAM; the display task already keeps three 41 KB frames, so
  this replaces the 1-bit-to-colour conversion rather than adding to it). Every existing screen keeps working with a **default theme colour** (white on dark, one accent per section): no screen is rewritten to get colour.
- **A node view model, not a new data model.** `core/nav_graph.[ch]` builds, from `rack_t` (and later from a `Patch`), a small struct: nodes (type, family, grid cell, main value),
  edges (from, to, signal type, depth), ports. It has no state of its own; it is rebuilt when the rack changes (cheap: 16 slots). The auto layout and the cursor moves
  are pure functions on it, so they are **tested on the desktop** like the rest of `src/core` (`tests/ui`).
- **Three new screens**, written in the declarative style of `ui_screen.c` where possible: `scr_map.c` (L0, hand-drawn), L1 reuses `scr_rack.c`'s page code with the ports section added, L2 is the existing picker list.
- **The RACK tab becomes the MAP tab**; the old strip stays as the fallback for 128 x 64 and for HWV1 until the map is proven. Pads' FX chains use the same map with a single column.
- **Controls stay verbs** (Move, Enter, Back, Adjust, Patch, Zoom): which physical control sends which is the board's table (`map_hwv2.h`, `bindings.c`), so nothing here touches the key layout.
- **Frame budget:** a map redraw is rectangles and short lines on a 5 K pixel area; with changed-tile sending the panel's 15 MHz SPI (about 22 ms for a full frame) is not a problem; no tile redraw while the cursor is still.

## 8. Plan, one commit per step (the user's habit since ADR-044)

1. **The theme and the canvas in the app**: every screen in default colours; header and legend in family colour. (Look only; a visible change you can flash.)
2. **`nav_graph` + tests**: layout, cursor moves, ports. No screen yet.
3. **L0 MAP** read-only: draw, move the cursor, the info strip; Enc A push enters the old module page.
4. **L1 NODE**: coloured picture and rows, the ports section, follow-a-cable, Back returns.
5. **Cables**: grab / drop, replace / add, unpatch, insert on a cable; the compatibility rules from the mapper's limits.
6. **L-1 overview** and Shift-joystick "same family" jumps.
7. **Docs and handoff**: UI.md, DEVELOPING.md controls table, a new ADR, CHANGELOG, CONTINUE.

## 9. Questions for you

1. **Flow direction**: top to bottom (proposed; portrait) or left to right with horizontal scrolling like the Studio's PATCH tab?
2. **The patch's size**: is the board's target about 16 nodes (the rack today) or the Studio's 30+? It decides whether L-1 is needed at once.
3. **Cables**: do you want **real multi-source inputs** (summing several LFOs on one cutoff) or one source per input as the rack has now? The Patch gesture changes with it.
4. **Palette**: the proposed families and colours (section 5), or your own? (Mock-up uses the proposal.)
5. **Button for Patch**: which of the four buttons (Button 1 in the proposal; ADR-046's roles for Back, Menu and Shift stay).
6. **HWV1** (128 x 128, 1-bit controls): keep the old strip there, or give it the map without colour?

## 10. Not checked

Nothing is built or flashed. The tile sizes and the colours are paper values: the real ST7735S panel will change which of them read well (try on the board first with step 1).
The auto layout of a deep patch (more than 6 levels, 3 or more branches) was only sketched; the L-1 overview is the fallback. The cycles-wallet "red corner" assumes the
rack can ask the wallet per module (`Patch::voices` does; the rack does not yet).
