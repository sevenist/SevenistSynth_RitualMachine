# The GUI reorganization: plan and handoff

**Status (2026-10-29, second session): decided** (the user's answers to Q1..Q9 are ENGINE_DESIGN.md **ADR-046**: design for
HWV1, push = Enter, JOY to PERFORM, Out / Spk / Knob to SETUP > MACHINE, Back never deletes, **no Relative mode**, a Pads Rec
list, the extras Bar / Reset / macro Value / **battery in the header**). **Being built on branch `gui-reorg`**: phase 1, then
2, then 4. Where this file and ADR-046 disagree, ADR-046 wins. **Phases 1, 2 and 4 are built** (2026-10-29: the knob legend
and the header's marks; push = Enter with the XY binding on a held push; Back never deletes, the "*", the REC mark), **and phase 3**
(the same day: the views in both sections, the view selector, the map and the page list, SETUP > MACHINE, PERFORM / SETUP over
the pads; flashed and tried by the user 2026-10-30: "working"), **and phase 5** (2026-10-30: the Pads Rec list, the macro Value
row, Reset (cog and action), the fallback table below, tested by `test_fallbacks.c`); **and phase 6** (2026-11-04: macro 1 Blue, encoder A Mint in Pads); **next:** the extra Bar widget. The
original status line follows.

**Status (2026-10-29): a proposal. Nothing is decided by the user and nothing is built.** Written at the user's request ("you
have the role of UX designer, create a plan to reorganize the whole GUI ... without leaving current features hidden or lost
... a current and future proof ruleset for creating a new UX for specific hardware"). The user asked for a plan only, no code.

The plan was first written as a claude.ai doc, which the user may have edited or commented since:
**<https://claude.ai/artifact/1uWHvDoWvGxJRfAgxWrMeG>** ("Ritual Machine - GUI Reorganization & UX Ruleset"). This file is the
repo copy, with what a developer needs on top (code pointers, traps, what was not checked). When the two disagree, the
user's edits in the doc win: read it first (section 1).

Sources the plan was built from: [RITUAL_MACHINE_UI.md](RITUAL_MACHINE_UI.md) (the code of the screens, its section 14 "What
is still open"), [DEVELOPING.md](../../DEVELOPING.md) ("Quick start": the controls table; "Screens"), ENGINE_DESIGN.md
ADR-041 / 042 / 044 / 045, `core/bindings.c`, `core/keymap.c`, `core/boards/map_hwv1.h`.

## 1. What to do next, in order

1. **Read the doc** (Artifact tool `read`, or the docs tools on that link) and its comments: the user may have answered
   questions or changed the plan there. Report what changed before anything else.
2. **A decision round on the open questions** (section 8), in the user's way: options with pros and cons, a recommendation,
   one round at a time (they often pick against the recommendation). Several questions reopen decisions of ADR-044 that are
   recorded as "not to be re-litigated": ask, never assume.
3. **Record the answers as a new ADR** in ENGINE_DESIGN.md (the next number, ADR-046 at the time of writing), in the format of
   ADR-044 / 045: Why, "Decisions taken by the user", the plan one commit per step, "Chosen by me while building", "As built",
   "Not done / not checked".
4. **Build phase by phase** (section 7), one commit per step (the user's wish since ADR-044: "commit per step so commit tree
   shows advancement"). Ask first whether the work goes on `main` or a branch. Every phase ends with a test list for the user
   to try on the board (they flash it themselves), as for ADR-044.
5. **Keep the docs in step**: RITUAL_MACHINE_UI.md (it describes the code; rewrite the parts a phase changes),
   DEVELOPING.md (the controls table of "Quick start"), CHANGELOG.md, this file (mark phases done), CONTINUE.md.

## 2. The problems it solves

| # | Problem | Evidence in the code |
| --- | --- | --- |
| P1 | Two editing models | Main view: the joystick edits values directly, push = "right", no latch (`synth_ui_handle` translates `UI_LATCH` to `UI_SELECT`). Menu tabs: spatial focus. RACK and the pad editor: latch (`use_latch`) |
| P2 | Push has six meanings | Latch, activate, "right", toggle a note (SEQUENCER), open the pad editor (grid), open the cog |
| P3 | Knob targets invisible | RITUAL_MACHINE_UI.md 14.1; only the catch wedge on a row |
| P4 | Four routes, two maps | 9 tabs in one ring (`tab_kind`), the Shift + Menu list groups them MAIN / ADVANCED (`app_lists.c`), the jump list holds up to 69 pages flat |
| P5 | Sound, sequencing and setup mixed | SEQUENCER / SEQ SETUP / PATTERNS are the last global pages of the main view; KEYS, LEDS sit beside RACK |
| P6 | Synth and Pads not parallel | Menu = tabs vs FX chains; no jump list in Pads; Presets / Advanced picked in Pads switch to the synth |
| P7 | Back overloaded, sometimes destructive | Back deletes the selected module on RACK (`scr_rack.c` `back()`); held while recording it erases; Shift + Back releases notes / stops all pads |
| P8 | Hidden state | Rack edits apply when the menu closes (`rebuild`); Rec armed shown only on an info line and the Enc A LED |
| P9 | Hardware gaps solved case by case | HWV1 has no encoder push; the panel simulator has no Select / Mute / Rec / Bank keys |
| P10 | Column knobs idle in the menu; endless pots used as positions | RITUAL_MACHINE_UI.md 14.7; `map_hwv1.h`: knob roles as positions from 512 |

## 3. The ruleset (hardware-agnostic)

The UI speaks in **verbs**, never in controls; each board maps its controls to verbs (the existing chain: board table ->
roles `controls.c` / `map_<board>.h` -> bindings `bindings.c` -> actions `app_actions.c` -> `ui_event_t`). Every verb must work
on the smallest board. A new screen or board that breaks a rule needs a written exception.

**A. Structure**
- **R1 One map**: every screen has an address Section > View > Page > Element, used by the lists, the jump slots, the header and the tests.
- **R2 Parallel sections**: Synth and Pads have the same view slots in the same order; a slot may be empty, never moved.
- **R3 Three ways through one map**: step (rings), list (the map popup), jump (slots), all with the same grouping and names.
- **R4 Three-action reach**: any screen within 3 actions of the section home, on the smallest board.
- **R5 Append-only addresses**: `tab_t`, `GP_*`, `SQP_*`, jump slots, parameter numbers are appended, never renumbered (already a rule: they are saved in `ui.cfg`).

**B. Controls and verbs**
- **R6 Verbs, not controls**: the UI never knows which control was used (already true of the screens; keep it so).
- **R7 Smallest-board rule**: every verb reachable on Tier 0 (joystick + push + 4 function keys); higher tiers add speed, never features.
- **R8 Stable vs contextual**: stable controls (Volume, macros, Play, notes, Shift, Mod, Menu, Back) never change meaning with the page; contextual ones (Enc A, Enc B, joystick, K1..K4) always have their meaning shown on screen.
- **R9 One meaning for push**: push = Enter (open, activate, latch), never a direction.
- **R10 Back goes up, never destroys**: one level up per press; deleting is an explicit item or a hold with on-screen feedback.
- **R11 Shift = same control, other job; Mod = user layer**: holding either previews its layer on the legend and the pot LEDs.
- **R12 Relative controls edit relatively**: endless pots and encoders step from the current value: no catch, no jump. Catch only for absolute knobs.
- **R13 Declared fallbacks**: an absent control's verb falls back to one table of chords / key functions (section 5), not a workaround per feature.

**C. Screen**
- **R14 Fixed zones**: header, content, knob legend, toast.
- **R15 The legend mirrors the hardware**: laid out like the board's knobs, from the board table's `px` / `py` (2 x 2 on HWV1, 1 x 4 on the panel).
- **R16 One visible focus**; focus (frame) and latch (filled, `< >`) always look different.
- **R17 The picture follows the focus** (SOUND already does).
- **R18 One monochrome vocabulary**: frame = focus, invert = latched / playing, dotted = modulation / probability, wedge = catch, "n/a" = disabled, "*" = pending.

**D. Feedback**
- **R19 LEDs speak roles**: one meaning per colour; red only for record and destructive; a hue per section (Enc A).
- **R20 Blind changes get a toast** (`popup_info`), as Shift + knob does today.

**E. Safety and data**
- **R21 Pending is visible**: structural edits show "*" in the header and apply on leaving the view by any route.
- **R22 Ask before destroying**, No focused; one level of undo where the data allows.
- **R23 Card work shows progress**.

**Capability tiers** (a board's tier follows from its control table; the bindings pick the mapping):

| Tier | Adds | Speeds up |
| --- | --- | --- |
| T0 | Joystick + push, 4 function keys, note matrix | everything reachable (R7) |
| T1 | Pots (K1..K4, Volume) | 4 values per page by hand |
| T2 | 2 push encoders | linear focus + value without the joystick, Enter / Top on pushes |
| T3 | Dedicated buttons (Play, Rec, Select, Mute), macro pots, LEDs under controls | performance without modifiers, state at a glance |

## 4. The navigation model

**2 sections x 5 views in the same order, plus SETUP over both.**

| Slot | View | Synth | Pads | Today |
| --- | --- | --- | --- | --- |
| 1 (home) | EDIT | the patch's pages: module pages in rack order, AMP ENV, AMP CURVE (FM: FM SYNTH, OPn, OPn ENV; Strings: its 4; then row M's effects) | the grid (page 0), then the selected pad's PLAY, TONE, CHOP, EDIT | main view; Pads grid + editor |
| 2 | SEQ | SEQUENCER, SEQ SETUP, PATTERNS | STEPS (+ its pattern / chain fields) | last pages of the main view; Select / Shift + Play |
| 3 | RACK | RACK, SOUND | PAD FX (chains), the FX module page | RACK + SOUND tabs; Menu in Pads |
| 4 | PERFORM | MACROS, JOY | the same pages (Q3) | MACROS tab; JOY in ADVANCED |
| 5 | PRESETS | PRESETS | empty (later: pad kits) | PRESETS tab |
| overlay | SETUP | KEYS, LEDS, MODIFIERS, CURVES, MACHINE (Out, Spk, Knob: Q4), STORAGE | the same, opened over Pads without switching | ADVANCED submenu |

**Focus levels** (Back climbs one, Enter descends one): (1) the view selector (header, left segment: left / right = views);
(2) the page selector (`< PAGE >`: left / right = pages of the view; skipped when the view has one page); (3) the elements;
(4) the latch. Up from the first element = page selector, up again = view selector; Enc A push (`ACT_ROW_TOP`) does the same.

**Routes** (gestures kept where possible):

| Route | Gesture | Shows |
| --- | --- | --- |
| Toggle | Menu short (at its release, as now) | home view <-> the last other view (today: main view <-> last tab) |
| Step | header left / right; Page - / +; Shift + Enc A | the view ring or the page ring |
| Map | Shift + Menu | MODES (Synth, Pads), the 5 views and their pages, SETUP > (the ADVANCED submenu renamed) |
| Page list | Menu held `MENU_LONG_MS` | the pages of the **current view**, in both sections (fits ADR-044 decision 8: no synth pages in Pads) |
| Jump | Jump 1..8; Shift + key saves | any address, stored by what it points at |
| Section | key function Sectn; MODES | the other section on its last view |

In Pads, Menu short still lands on PAD FX by default (RACK is the default "last other view").

## 5. Controls per hardware profile

The next board, as the user described it on 2026-10-29: **7 relative (endless) pots and a joystick like HWV1, plus 2 push
encoders**. The encoders take Enc A / Enc B from pots P1 / P2, which become macro knobs R1 / R2.

| Pot | HWV1 (now, `map_hwv1.h`) | Next board | Panel (simulator reference) |
| --- | --- | --- | --- |
| P0 | Volume | Volume | Volume |
| P1 | Enc A (rows) | R1, macro 1 | - |
| P2 | Enc B (value) | R2, macro 2 | - |
| P3..P6 | K1..K4 (around the joystick: P6 top left = K1, P3 top right = K2, P5 bottom left = K3, P4 bottom right = K4) | K1..K4 | K1..K4 (1 x 4) |
| R3 | - | macro 3 on a layer | its own knob |
| Enc A / Enc B | - | encoders with push | encoders with push |

| Verb | HWV1 (T1) | Next (T2) | Panel (T3) | Tier 0 fallback |
| --- | --- | --- | --- | --- |
| MOVE | joystick | joystick | joystick | joystick |
| STEP (linear focus) | P1 | Enc A | Enc A | joystick up / down |
| ADJUST | P2 | Enc B | Enc B | latch, then joystick left / right |
| COARSE / FINE | Shift + P2 / Shift + joystick left / right | Shift + Enc B / same | same | Shift + joystick left / right |
| ENTER | joystick push | joystick push, Enc B push | same | joystick push |
| TOP | up from row 1 | Enc A push | Enc A push | up from row 1 |
| BACK | function key 3 | function key 3 | B2 | function key 3 |
| TOGGLE / PAGE LIST / MAP | Menu / Menu held / Shift + Menu | same; Enc A held = page list (proposed) | same (B1) | same |
| PAGE - / + | Shift + P1 | Shift + Enc A | Shift + Enc A | header left / right |
| OCTAVE | Shift + joystick up / down | same | same | same, or Oct keys |
| PLAY / REC | Play key / Shift + Play | same | same | same |
| XY mode | Shift + joystick push | same | same | same |
| CONTEXT KNOBS | P3..P6 | P3..P6 | K1..K4 | focus + ADJUST |
| MACROS | Shift + K3 / K4 = macro 1 / 2; any macro: MACROS > Value | R1, R2; macro 3 on a layer | R1..R3 | MACROS > Value (built) |
| LEARN | Shift + Mod + turn; Learn button | same; Shift + R1 / R2 | Shift + R1..R3 | Learn button |
| RESET TO DEFAULT | Shift + push on a latched value; the action Reset on any key / layer entry; a module: its cog's Reset | (Enc B held: not built) | same as HWV1 | the same (built) |
| CLICK / TAP / BPM | the Pads right block's keys; Click in both Rec lists; any layer entry (Tap, BPM +-1 / +-5; Tempo on a knob) | same | Click in the Rec lists; layer entries | same |
| PADS: BANK | P2, Bank keys | Enc B | Enc B, Page | Shift + joystick left / right on the grid |
| PADS: CAPTURE, LENGTH, PATTERN REC, STEPS | Rec, Shift + Rec, Select + Rec / Play; the Pads Rec list | same | the Pads Rec list (Shift + Play) | the Pads Rec list |
| PADS: SELECT, MUTE, STOP ALL | Select / Mute keys; Shift + pad; Shift + Back | same | Shift + pad; the editor's Mute; Shift + Back | the grid's push (editor), Mute row, Shift + Back |

**As built (phase 5, 2026-10-30)**: every verb above has its HWV1 gesture and its Tier 0 one; `tests/ui/test_fallbacks.c` walks
them all with only the joystick, its push and the 4 function keys (plus two note keys for a mapped Tap / BPM). The next board's
column is the plan's, not built (decision 1).

**Relative knob mode (R12, Q8)**: a third value of `cfg.knob_mode` (today 0 = Catch, 1 = Direct; append 2 = Relative), the
default on endless pots: each detent steps the parameter from its current value with acceleration, no catch, no wedge; the
pot LED is always "caught" and shows the value. Catch / Direct stay for absolute knobs.

**Rules for any new board**: declare each control's kind and place in its table (tier and legend geometry follow); fill the
fixed roles first (Volume, Enc A / B, K1..K4, R1..R3; leftover pots become macro knobs); check every verb above has a
gesture or its fallback; a control with an LED takes its role colour (section 6).

## 6. Screen grammar, LEDs, interaction patterns

**Zones at 128 x 128**: header y 0..15 (view icon, `< PAGE >`, n/N, marks: play, REC, XY, "*", S / M while a layer is held);
content y 16..103 (11 rows of 8 px); knob legend y 104..127 (2 x 2 cells of 64 x 12 on HWV1, 1 x 4 of 32 x 12 on the panel;
each cell: number, short label, wedge if absolute; the layer's targets while Shift / Mod is held); toast centred over the
content. **At 128 x 64**: header 8 px, no legend zone; a badge 1..4 at the right edge of the driven rows instead.

**Content templates**: T-List (rows left, picture right: SOUND, the SETUP screens, MACROS, JOY, SEQ SETUP, PATTERNS, FX
module), T-Module (today's module page), T-Grid (pads grid, RACK, PAD FX), T-Timeline (SEQUENCER, STEPS, MOTION / MS LANE,
EG points), T-Full list (browser, map, page list).

**Widgets**: Field, Selector (`< value >`), Button, Cell, Cog as today; **Bar** (proposed: a thin value bar under a
continuous value; answers RITUAL_MACHINE_UI.md 14.4).

**LED language** (defaults from ADR-045, two changes, Q6, built in phase 6): Amber dim / bright = context knob away / caught; White /
Green = Volume; one colour per macro; Sky = Synth on Enc A; **Mint = Pads on Enc A** (Amber is the Knob role's); red
blinking 150 ms = armed, red steady = recording / destructive; saturation = value; dark = drives nothing. **Macro 1 is Blue**
(red reserved).

**Interaction patterns**
1. Focus, then edit, on every screen including the main view: ADJUST edits the focus with no latch; the joystick moves the
   focus and adjusts where there is no neighbour; ENTER latches. Named exceptions: SEQUENCER and STEPS (cursor + toggle) and
   the Pads grid (ENTER opens the pad).
2. Context knobs drive the page's first 4 knobbable rows or its declared set (SEQUENCER: Note Len Vel Prob; Pads: Start End
   Tune Cut), and in SETUP / menu screens the first 4 numeric fields.
3. Catch only on absolute knobs.
4. Modifier preview: while Shift / Mod is held, legend + LEDs show the layer, "S" / "M" in the header.
5. Structural edit: "*" in the header; leaving RACK by any route applies and rebuilds; toast "Applied".
6. Confirm / undo: Init, Save over, Chop to pads, Storage, module Delete ask with No focused.
7. Recording states in the header: REC blinking (armed), REC steady + position (recording), ERASE (Back held), REC + percent
   (capture finishing); Enc A and Rec / Back LEDs as ADR-045.
8. Browse and audition as today (ENTER keeps, BACK restores).

## 7. Phases, with where the code is

Each phase is usable on HWV1 alone. Gates are what to show before the next phase.

| Phase | Closes | What | Where (start reading here) | Gate |
| --- | --- | --- | --- | --- |
| 1 Legend | P3, P10 | the knob legend zone, header marks, K1..K4 in menu screens | `synth_ui_draw` top bar and rows (`ui_draw.c`), `pads_ui_draw` (`scr_pads.c`), `screen_draw` (`ui_screen.c`), `gui_style_init` (`framework/ui/gui.c`: the content area), `synth_ui_knob_row` / `row_is_knobbable` (`ui_input.c`), knob places in `input_controls()` (`boards/hwv1/hwv1_controls.c`: **the places are a guess**, `panel_controls.c`) | every page names its 4 knob targets |
| 2 Main view declarative | P1, P2 | the synth pages as element tables; push = Enter / latch | `ui_pages.c` page tables -> `screen_def_t`; `synth_ui_handle`'s event translation (`ui_input.c`); the hand-written pages keep their handlers as named exceptions | UI tests pass with one gesture set |
| 3 Views and rings | P4, P5, P6 | tabs grouped into views + SETUP; SEQ pages out of the main view; page list per view; map renamed | `tab_t`, `tab_kind`, `tab_name`, `tab_index_of`, `modular_globals` / `fm_globals`, `synth_ui_go` / `synth_ui_where` (`ui_pages.c`); `app_lists.c`; `app_menu_poll` / `app_menu_up`; jump slots (`jump_slot_t`, `synth_ui_jump_resolve`) | every feature of section 9 within 3 actions on HWV1 |
| 4 Safety | P7, P8 | Back stops deleting on RACK; "*" pending mark; recording states in the header | `back()` in `scr_rack.c`; `rack_dirty` / `rebuild`; `seq_rec`, `pseq_rec`, `cap_state` (written by the app) | no plain Back press changes data |
| 5 Relative pots, next board | P9, P10 | `knob_mode` Relative; the next board's map; the Tier 0 fallback table | `cfg.knob_mode` / `CFGP_KNOB_MODE` (`synth_config.*`), `knob_set`, `knob_check_catch` (`ui_input.c`), `map_<board>.h`, `maps[]` (`controls.c`), `bindings.c`, a new board folder in `framework/boards` | the verb table complete for HWV1, next, panel |
| 6 LEDs | - | the LED language changes | `led_roles.c` defaults, `synth_ui_macro_default_color`, `key_leds.c` (`pot_leds`, `enc_led`) | no colour with two meanings |

## 8. Open questions (ask before building)

- **Q1** The next board: which physical pots become R1 / R2, where the encoders sit, its display size, its buttons (Play, Rec,
  Select, Mute?). Note: ADR-045 assumed **8** pots on the next prototype (the panel: Volume, K1..K4, R1..R3); the user said
  on 2026-10-29 **7 relative pots + 2 push encoders**. Settle which is right; it changes the LED count too.
- **Q2** Back on RACK stops deleting the selected module (Delete stays in the cell menu)?
- **Q3** JOY moves from ADVANCED to PERFORM next to MACROS (**reopens ADR-044 decision 1**). And PERFORM in Pads: the synth's
  macros, or empty until pads have macros?
- **Q4** Out, Spk, Knob move from SOUND to SETUP > MACHINE, Vol stays (**reopens ADR-044 decision 7**).
- **Q5** Pads capture without a Rec key (the panel's 4 columns have none): a Pads Rec list on Shift + Play next to step mode?
- **Q6** Colours: macro 1 off Red; a Pads hue for Enc A other than Amber. **Answered 2026-11-04: Blue, Mint** (ADR-046 "Phase 6").
- **Q7** Push = latch on the synth's pages replaces push = "right" (a habit change on the user's board).
- **Q8** Relative as the default knob mode on endless pots; the acceleration.
- **Q9** The extras: the Bar widget, Enc B held = reset to default, Enc A held = page list, a macro Value field (Tier 0), a
  latched-notes count in the header.

## 9. Nothing lost: where every feature goes

The checklist for the tests of phase 3 (one test per row walking its route on each board's map). "Moved" marks the five
changes of place.

| Feature | Today | New home |
| --- | --- | --- |
| Module pages (OSC, TUNE, DEST, FILTER, ENV, CRV, LFO, EG, EG REL, RES, SAT, MOTION, MS LANE, SMP, LOOP, SLICE, VEL, effects) | main view | Synth > EDIT |
| AMP ENV, AMP CURVE | main view | Synth > EDIT, after the modules |
| FM SYNTH, OP1..6, OP1..6 ENV | main view (FM) | Synth > EDIT |
| STRINGS, STR TONE, VOICE LP, STR FILTER | main view (Strings) | Synth > EDIT |
| Cog hidden settings | first module page | same |
| Modulator DEST (Tgt, Prm, Dpth) | DEST pages + RACK menu | same |
| Rack: branches 1, 2, row M, MIX levels / pans; Insert, Type, Row, Para, PEnv, Delete, Edit | RACK tab | Synth > RACK > RACK |
| Delete the selected module with Back | RACK, Back | **Moved**: the menu's Delete only; Back closes (Q2) |
| Type, Patch, Voices, Glide, Legato; File + browser, Tgt, Assign; Algo, Fb, Op | SOUND tab | Synth > RACK > SOUND |
| Vol | SOUND, Volume pot | same |
| Out, Spk, Knob mode | SOUND tab | **Moved**: SETUP > MACHINE (Q4) |
| Piano roll, tools, follow / scroll, step knobs | SEQUENCER page | **Moved**: Synth > SEQ > SEQUENCER |
| BPM, Steps, Trsp, Swing, Rate, Dir | SEQ SETUP | Synth > SEQ > SEQ SETUP |
| Patterns 1..8, chain, queue | PATTERNS | Synth > SEQ > PATTERNS |
| Rec list (Notes; the pattern's sound as a sample), step / live entry, erase (Back held) + Undo | Shift + Play, Rec | same gestures; header states |
| Macros 1..8, destinations, Min / Max / Curve, Learn, Remove, Color | MACROS tab | Both > PERFORM > MACROS |
| Macro knobs | R1..R3 / Shift + K3 / K4 | section 5 |
| Joystick XY mode | Shift + joystick push | same |
| XY bindings (Axis, Tgt, Min, Max, Curve, Clear) | JOY tab (ADVANCED) | **Moved**: Both > PERFORM > JOY (Q3) |
| Knob Catch / Direct, wedge | SOUND Knob, rows | SETUP > MACHINE (+ Relative); wedge in the legend |
| Latched notes, release all, octave, fine steps | keys, Shift chords | same |
| Presets: Load (auditioned), Save, Save as, Init | PRESETS tab | Synth > PRESETS |
| Pads grid, banks, Select, Mute, Stop all, Shift + pad | Pads screen, keys | Pads > EDIT > grid; same keys |
| Pad PLAY / TONE / CHOP / EDIT (ops, Apply, Ver, Undo, Redo, progress), pad knobs, pad browser | pad editor | Pads > EDIT > the 4 pages |
| STEPS, pad patterns, chain, live record | Select / Shift + Play | **Moved**: Pads > SEQ > STEPS (gestures kept) |
| FX chains FXA..C, routing, load, module values | Menu in Pads | Pads > RACK > PAD FX |
| Capture / resampling, length in bars | Rec key, Shift + Rec | same keys; also a Pads Rec list (Q5) |
| Clock: tempo, Tap, Click, BPM keys, Grid | keys, PLAY Grid | same; tempo also in SEQ SETUP |
| Shift + Menu list | list popup | the map |
| Long Menu jump list | synth pages | the current view's pages, both sections |
| Jump slots 1..8 | key functions | same |
| Section switch | Sectn, MODES | same |
| Key layouts, Key, Func, Reset; lock-out escape (F1 2 s at start) | KEYS tab; boot | SETUP > KEYS; same |
| Key / pot / Enc A LED roles, Value switch | LEDS tab | SETUP > LEDS |
| Shift / Mod layers, mappings, Learn, Reset | MODIFIERS tab | SETUP > MODIFIERS |
| User curves U1..U8 | CURVES tab | SETUP > CURVES |
| Storage (USB card reader) | ADVANCED > Storage | SETUP > STORAGE |
| Popups, boot animation, autosave, card hot-plug | system | same |

## 10. Traps and what was not checked

- **Not checked while planning**: how K1..K4 behave today on HWV1's endless pots with `knob_mode` Catch. `map_hwv1.h` says a
  knob role is a position starting at 512 that "jumps to the pot's counter on the first move (no pick-up)", while the UI's
  catch is per column knob (`knob_catch_dir[]`). Read `knob_set` / `knob_check_catch` and try it in `-Board hwv1` before
  designing Relative mode. The DEVELOPING.md controls table also says Shift + joystick left / right = page in one place and
  fine steps in another (`modifiers_init` gives fine steps): check before relying on either.
- **Saved ids**: `tab_t`, `GP_*`, `SQP_*` are in `ui.cfg` (jump slots, layers): append, never reorder; old tab values map
  through `tab_index_of`. Moving pages between views must keep a saved slot landing on the same screen.
- **UI tests address rows by number** (`tests/ui/test_rows.c`: the RACK menu; `test_pads.c`: the EDIT page): moving or
  adding a row shifts them.
- **Menu close = rebuild** today (`rebuild` when the menu closes with structural edits; ADR-044: the jump list applies a rack
  edit first). Phase 3 changes "the menu closes" into "leaving RACK by any route": every route must apply it.
- **Focus lending**: the Pads screens lend their own focus to the declarative code per call (RITUAL_MACHINE_UI.md section 8);
  a new declarative screen outside the menu must do the same.
- **`in_rack`** means "the menu is open", not the rack (historical name).
- **Menu short acts at its release** (ADR-044) so a hold can be the jump list; keep that when Menu becomes the view toggle.
- **The 128 x 64 drawing** of the list and of SOUND was never looked at (ADR-044); the plan's 64 px fallback is untested.
- **The simulator's 4-column panel** lacks Select, Mute, Rec, Bank: whatever those keys do must stay reachable another way (R7).
- `ui_dump.c` draws at 128 x 128 whatever `UI_DUMP_SIZE` says (CONTINUE.md known debt): check the 64 px fallback another way.
