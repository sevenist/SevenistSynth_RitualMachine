# Continue development: handoff notes

Read [DEVELOPING.md](DEVELOPING.md) first (how to work in the code, where to change things, tools, testing, **"ESP32 firmware and performance work"**), then
[ENGINE_DESIGN.md](ENGINE_DESIGN.md) for the engine's decisions, measurements and limits (**ADR-035 = what was learned on the real chip**). This file only holds what they do
not: state, the latest measurements and what to do next, the user's working style, known debt and traps.
For any CPU / memory optimization work on the board, use the project skill `.claude/skills/esp32-optimize/SKILL.md` (the loop that worked: measure with
`tools/serial_test.py`, locate, change, host-test, the user flashes, re-measure).

## Session todo (updated 2026-10-07, fourth session)

This session: commit the sprite pipeline, then the 8 x 8 grid layout. Confirmed by the user on the board (2026-10-07): joystick XY, fine steps, role-based
LED colours (preview by role, GRB order, default colours). Still to tune on the board: the HWV1 knob hysteresis.

- [ ] **8 x 8 grid layout** (user, 2026-10-07): every placement on an 8 px grid, the top bar 16 px high (one row); rack cells 32 px (24 icon + 8),
  four across; then the links drawn to the fixed connectors. Touches every screen (from: user).
  **Step 1 BUILT 2026-10-07** (option A, 56 UI tests green, sim + firmware build, NOT flashed, NOT seen by the user): `GUI_GRID` / `GUI_BAR_H` (gui.h);
  the top bar is 16 px (rule = its row 15; every screen now starts at y 16, `graph.y` 16) with a menu tab's `16/tab_<name>` icon at the left;
  RACK tab: `RACK_PITCH` 32, 4 cells, icons centred, the selection arrow removed (the frame shows it). Connectors (user, 2026-10-07):
  mix in (0, 6), mod in (0, 18), one output (23, 18) = mix out or mod out (user confirmed the shared point); mix links via the gap (neighbours)
  or the audio lane, mod links via the mod lane up into the mod in (row 0 above the icons is now only frame room); description row at y 56,
  fields in 16 px rows from y 64. Placeholders: `tools/make_placeholder_sprites.py` wrote 39 PNGs (9 modules, 2 slots, 15 osc waves / engines `osc_*` (not wired:
  asked where to show them), 13 tabs), indexed 3-colour PNGs (transparent / black / white, user request; `--convert` rewrites any PNG), for the user to paint over. Next: the other screens (list rows of 9 px are not on the grid; the parameter pages lost 5 px to the bar)
- [ ] **Module first page + cog** (user, 2026-10-07; asked with options: rows left / icon right, infos name + tuning + quality, the cog opens
  hidden settings, every module's first page): BUILT (59 UI tests green incl. `tests/ui/test_cog.c`, sim + firmware build, NOT flashed, NOT seen
  by the user). A module's first page: 8 px rows at x 0..63 / y 16..63, the right half: icon (`ui_module_sprite`: OSC = `24/osc_<wave>`) at
  (72, 16), 3 info lines at y 40 / 48 / 56 (OSC: wave, "+Nst +Nct", "Q <q>"; others: the module's name only), the cog `8/ui_cog` at (120, 16);
  the preview (graph) at x 0..127 / y 64..127. The cog is the row after the last one (encoder A / joystick to it); a push (or left / right)
  swaps the rows for the hidden settings (title "<page> SET"), a second push or Back closes; leaving the page closes it (`ui->cog_page`).
  Hidden settings (`mod_hidden`, ui_pages.c): OSC = Q (left OSC TUNE) + Mute (left OSC DEST); other modules: none yet, so no cog (to ask).
  EG and MOTION first pages are drawn by hand: old layout. User 2026-10-07: the same layout on every module page (TUNE, DEST, ENV ...: the
  cog only on the first page) and one cycle in the wave / engine previews (`draw_wave`, `draw_engine_preview`; LFO and Strings use draw_wave too).
  Hidden settings of the other modules: the user decides later (from: user)
- [ ] **UI sprites build**: pipeline BUILT 2026-10-07 (56 UI tests green incl. `tests/ui/test_sprites.c`, sim + firmware build). Format decided with the
  user, written in `assets/UI_Sprites/README.md`: folders by size (8 / 16 / 24 / 64), PNG white = lit, black = off (drawn), alpha < 50 % = transparent
  (a mask plane, `gui_sprite_t.mask`), frames `_00 _01 ...`, module connectors by a fixed convention (changed 2026-10-07: mix in (0, 6), mod in (0, 18), output (23, 18);
  see the grid item), modules 24x24 (the generated fallback too; RACK_PITCH 29 -> 28). Converter `tools/gen_ui_sprites.py` (plain
  Python + zlib, no Pillow), run by build.ps1 and `extra_scripts = pre:tools/pio_gen_sprites.py`; lookup `ui_sprite("24/mod_osc")`, NULL = fallback.
  Wired so far: modules + slot cells (scr_rack.c), the XY mark (`8/ui_xy`, a placeholder I drew: redraw it). The user draws the art. NOT flashed (from: user)
- [ ] **Knob hysteresis on the board**: continuous knobs are in (fine steps confirmed); tune `HWV1_ABS_START` / `_STEP` / `_IDLE_MS` (hwv1_layout.h):
  raise START if a knob at rest jitters, lower STEP if fine moves feel steppy (from: notes)
- [ ] **Shift + note = latch**, Shift + Back = release all: built and in b913afa, not confirmed by the user yet (from: notes).
  Latch details (user, 2026-10-07; toggle per note, not a sustain pedal; Shift only, Mod + note plays normally): app.c `ACT_NOTE`, `input_state_t.latched[]`;
  test `modifiers_shift_note_latches_until_pressed_again`. The next press of the key (with or without Shift) releases it; only when the key's Shift entry is
  Default. Shift + Back: popup "LATCH / Released N". Limits: a latched key whose function is changed in KEYS keeps its note until a panic / restart
- [ ] **All-pass filter type "AP"** (user, 2026-10-07): BUILT (161 engine tests green incl. `filter_allpass_is_flat`: worst 0.03 dB, 59 UI
  tests, sim + firmware build, NOT flashed, NOT listened to). `FILT_AP` appended (saved values keep their meaning), engine `FLTM_AP` = the SVF's
  x - 2k bp (one multiply-add over an LP section), 1 section, Res = Q (how fast the phase turns); also in STR FILTER; serial `flt 9`. The preview
  shows the phase (0 top, -180 at the cutoff, -360 bottom). Alone it only shifts phase (inaudible on a static sound): a phaser needs it mixed
  with the dry signal (no mix on the filter: to ask the user) (from: user)
- [ ] Then "D" = engine / board items: decide the SD options (a)-(d); internal RAM for the startup patch (what stays internal with a 74 KB fast heap: reverb tank, delay, filters; or free internal RAM, IRAM code 124 KB) (from: user + measurements)
- [ ] Quick macro UI: a button next to a mappable element, so a macro is assigned without Shift + knob (from: user)
- [ ] Board test later (user): ui.cfg (Knob Direct + two jump slots survive a reboot) and jump slots by identity on the board; catch refresh after manual changes (from: user)
- [ ] Convert the main view (the pages) to a declarative screen: it still translates the new events to the old ones and has no latch (UI_GUIDE.md section 8) (from: debt)
- [ ] Integration findings for an engine-independent "audio backend" interface (own voice allocator, mono -> stereo, memory policy, measurement hooks) (from: user)
- [x] Joystick XY controller: tried by the user on the board, works (done 2026-10-07)
- [x] Fine steps: tried by the user on the board, work (done 2026-10-07)
- [x] Role-based key LED colours: LEDS tab, preview by role, GRB order, default colours: checked by the user on the board (done 2026-10-07)
- Set aside (user, 2026-10-07): unformatted card "format?" ASK (a mount without a file system is still "no card"; needs FATFS mkfs)

## Fine steps, continuous knobs (2026-10-07; fine steps tried by the user: work; knob hysteresis not tuned yet)

- **Fine steps**: `ACT_VALUE_FINE` = the value in steps of 1/5 (`core/fine_step.h`, a flag the app sets around the action: `param_adjust`,
  `rack_mparam_adjust`, the depth, `fxr_adjust` read it): 0.05 -> 0.01, 5 % -> 1 %, a log factor x1.2 -> its 5th root; never finer than the decimals shown
  (semitones, cents, ms stay whole). Enumerations, FM values, sequencer: unchanged. **Shift + joystick left / right** = Fine -/+ (Shift starting entries;
  they were Pages: row 0 + left / right still changes the page); also key functions "Fine -" / "Fine +" and the knob / encoder function "Val fine".
- **Continuous knobs**: a linear / logarithmic module or global parameter is driven exactly (`rack_mparam_norm / _set_norm`, `param_norm / _set_norm`,
  `cont_drv` in ui_input.c) over the knob's whole resolution, rounded to the decimals it shows; log parameters over their log range. Page rows, Shift / Mod
  targets, macros and the joystick XY all use it. Still walked: enumerations, the modulation depth (target units), the sampler slice (clamped to the
  file), GENERAL settings, sequencer. `knob_new_position` keeps the full 0..1023 (was >> 3). Values are no longer on the 0.05 grid after a knob.
- **HAL hysteresis** (input_esp32.cpp `scan_knobs`, HWV1 only): from rest or to reverse a knob must turn `HWV1_ABS_START` 8 units (the old step, which hid
  the noise); while it keeps turning one way (a report < `HWV1_ABS_IDLE_MS` 150 ms ago) it reports every `HWV1_ABS_STEP` 2 units (512 per turn). Untested:
  if a knob at rest still jitters raise START, if fine moves feel steppy lower STEP.

## Joystick XY (2026-10-07; tried by the user on the board: works)

User design (the click rules were confirmed on a worked example; options asked for the response and what to keep):
- **Bind**: on a page, a joystick push on a parameter row (`synth_ui_target_at_cursor`: the rows a knob may drive) binds it. A new parameter replaces the
  older binding (X, Y, X ...); the same one pushed again at once moves to the other axis and the one it replaced comes back (again: back); a bound parameter
  that is not the one just bound gets its axis inverted (Min <-> Max). A push on any other row (row 0, Run, step toggles) keeps its old "act like Right".
  Popup "JOY: X <name>" / "JOY INVERT".
- **XY mode**: Shift + push (Shift layer starting entry `CTL_JOY_SW` = `ACT_JOY_MODE`, editable; also a key function "Joy XY", token `joyxy`) toggles it;
  "XY" at the right of the header. Decided **around the value**: full throw reaches the mapping's ends, letting go (dead zone `JOY_XY_DEADZONE` 48)
  gives exactly the value back; leaving the mode off-centre also restores it. **The rest value follows the parameter** (user, after the first build: a knob
  / navigation change was undone): it is read each time the stick leaves the centre; a change made elsewhere during a push is kept on release.
- **Range + curve per axis**: the JOY menu tab (after MACROS): Axis, Tgt, Min, Max, Curve, Clear, the response picture. **Saved in ui.cfg**: `joy` then
  `joy x|y <mapping>`. Code: `ui_input.c` (`synth_ui_joy_click / _mode / _axis`), `app.c` (`joy_bind`, `joy_mode`, `joy_update`), `scr_joy.c`.
- Chosen by me (say if one is wrong): the toggle is **Shift only** like the latch (Mod + push = Default, a normal push); "just bound" ends when XY mode is
  toggled (after playing, a push on it inverts); in XY mode the stick never navigates, so Shift + joystick (octave / pages) does nothing there; a push in XY
  mode still binds (the cursor moves with the encoders or after leaving the mode); the axis Y is up = positive.
- Limits: a walked parameter (most module params) is measured on every stick move like a knob (cost not measured on the board); no LED for XY mode.

## FM editor, mappings, macros, curves (2026-10-07; tried by the user: works; decisions asked with options)

- **FM like the rack**: BUILT 2026-10-07 (UI tests green, sim + firmware build). Main-view pages FM SYNTH (Patch Algo
  Fb Vol, the tree as picture), OPn (Lvl Crs Fine Fix, the tree with OPn marked), OPn ENV (Pt Lvl Time, the envelope); the ALGORITHM tab keeps Algo / Fb and an
  Op selector over the tree (left / right), a push opens OPn (`synth_ui_open_page`). OPERATOR / ENVELOPE tabs removed (their tab_t values stay: saved jumps).
  The tree is the existing generated sprite (now centred, transparent), not a new drawing: say if a bigger / different tree is wanted
- **FM parameters mappable**: BUILT with the above: `MACRO_FM` targets (op + `dx7_value_t`, ui.cfg `param fm <op> <v>`), learn / Shift / Mod / macros on
  every FM row but Patch and Pt. FM values are driven exactly (`dx7_value_get / set` + `knob_drv_t` in ui_input.c): the walking measure would have changed
  off-grid fine values and envelope times just by showing a page. Envelope time scale: at least 1 ms per knob position (found by the new test)
- **UI tests in the repo** (2026-10-07): `tests/ui/*.c` + `tools/build_ui_tests.ps1` (45 tests: modifiers, FM, mappings / macros, curves, LEDs; they found a parse bug: a mapping line read the range / curve of the NEXT line of ui.cfg), stubs `tools/ui_stubs.c` (RAM card, prototype
  controls). `STORAGE_FILE_MAX` 2 -> 4 KB (ui.cfg with many layer entries could be cut; +6 KB RAM on the board, 55.5 %)
- **Range per mapping**: BUILT 2026-10-07 (with the two items below; UI tests green). Every mapping (a Shift / Mod knob target, each macro destination) has Min and Max as a % of the parameter's range; Min > Max inverts.
  The knob's full turn sweeps Min..Max through the mapping's curve. Decided: % (not the parameter's unit)
- **Response curves**: BUILT (built-ins Lin Exp Log S; user curves = the editor item). A curve per mapping = a LUT read with linear interpolation between entries (cheap). Built-in curves (at least linear, exp, log, S; to list)
  and user curves. Catch with any curve: decided **in knob position** (the knob is caught when it crosses the position where the curve last left the value; a value
  changed elsewhere is caught at the first knob position that maps to it)
- **Multi-destination macros**: BUILT (MACROS tab, ui.cfg `macro K ...` lines; Shift + R knob now ADDS a destination instead of replacing). Decided **macros 1..8, up to 8 destinations each**, each destination with its own range and curve (learn adds a destination;
  today's single-target macros = one destination). Needs a macro editor (list of destinations, remove, range, curve) and saving in ui.cfg
- **Custom curves editor**: BUILT 2026-10-07 (UI tests green). User decisions: 16 points max, a CURVES tab for now (may move), X / Y by two knobs set in code (`CURVES_KNOB_X / _Y`, bindings.h; default K3 / K4 so the board has them, R1 / R2 the other choice). One file `system/config/curves.cfg` instead of a folder (a new folder would make every card ask "Missing: ... Create?"); 8 curves U1..U8; table 129 entries. Firmware RAM 58.9 % (+9 KB). Was: a curve = a list of points (x, y); for **each point** the segment after it is either linear (interpolated to the next point) or
  stepped (held until the next point). Stored on the card (`system/curves/<name>.crv`, text), turned into the LUT when loaded. To decide when started: max points,
  LUT size, where the editor lives (a CURVES menu tab?), how a point is moved with 4 knobs on a 128 x 128 screen

## Shift / Mod layers, the MODIFIERS tab (2026-10-07; built, tried by the user: works)

User decisions (asked with options): **every control, keys included**, can be set per layer (I recommended knobs + encoders + joystick only: against it, see the
decisions list); knob targets = **any parameter by learn** (module params by module id, global, sequencer, live GENERAL settings); **Mod = a new key function, on no
built-in layout** (the user assigns it in the KEYS tab); learn by **both** the chord Shift + Mod + turn a knob and a Learn button in the tab.
Details chosen by me (say if one is wrong): **Shift and Mod behave the same** (user, after the first try: Mod lacked Shift's popup): Default = as without
modifier, except a column knob ("No target" popup), a jump key (saves) and, **Shift only**, a note key (latch) and Back (release all latched notes) (user 2026-10-07: see the todo); Shift's old fixed behaviours (EncA pages, EncB x4, joystick octave / pages, R learn,
K1 Spk, K2 Vol) are the Shift layer's starting entries, editable; ui.cfg stores only the differences from them (`act default` for a cleared one); Shift + Mod held = the Mod layer; the layers are global (not per key layout) and live in ui.cfg; a modifier key is never taken by a layer, and Shift / Mod
cannot be a layer entry; the chord learns into the layer the tab shows (default Shift); with the Learn button the next Latch / Select push assigns (Back cancels);
an absolute knob with a step action (Rows, Pages, Value, Value x4, Octave, Volume) steps once per 1/32 of its travel; a learned module param goes back to Default
when its module is deleted (`modifiers_prune` at every page rebuild).
- Code: `core/modifiers.c/.h` (entries, function lists, ui.cfg lines), `scr_mods.c` (tab: Layer, Ctrl, Func, Learn, Reset + the list), `app.c` (`layer_event`,
  `control_event`, `learn`, `learn_push`, the catch targets per layer: `MACRO_PAGE_ROW`, `ui->knob_away`), `ui_input.c` (generic targets: `synth_ui_knob_row_target`,
  `synth_ui_knob_target`, `synth_ui_target_step`, `synth_ui_target_describe`, `synth_ui_target_at_cursor`; `MACRO_CFG`), `ACT_MOD` (key function
  "Mod"; a "Save 1..8" function was added then removed at the user's request: saving a jump = Shift / Mod + the jump key only), HAL `input_control_present()` (the board: the 7 knobs + joystick; the sim: everything). `ui->knob_shift[]` and `ACT_PAGE_KNOB_SHIFT` are gone.
  The macro learn (Shift + R knob) now also takes GENERAL settings.
- Verified: sim + firmware build (RAM 53.4 %); a scratch test through `app_step` (defaults, Mod + EncA = Pages, the chord, the Learn button + push, Shift + jump key
  still saves, a key's Shift entry overrides it, ui.cfg round trip, the old `shift N cfg` format, prune); the tab drawn at 128 x 128 with `ui_dump`'s stubs.
- Macros on any hardware (user: "parametrable so any hardware layout can accommodate the UI"): knob functions "Macro 1..3" (plays `ui->macro[k]`; on a col knob with
  the catch) and "Learn M1..3"; the starting entries depend on the board (`input_control_present(CTL_KNOB_R1)`): without R knobs Shift + K3 / K4 = Macro 1 / 2.
  `ui_dump` stub: `UI_DUMP_BOARD=hwv1` = the prototype's controls.
- Func list of a knob (fixed after the user found Spk missing once unselected): Default, None, the live GENERAL settings (Vol Spk Out Glide Legato Patch Knob), the steps; other parameters only by learn.
- Not done / limits: no LED for a key's layer function; keys with a Shift / Mod entry do not show it on their LED; the tab cannot set a knob param without learn.

## UI/UX round (2026-10-06; built, mostly confirmed by the user on the board; the user flashes)

The user wanted the confusion about the joystick gone and a safer knob. Order taken: navigation, Shift knob + catch, knob mode setting, rack links, jump keys. What exists:
- **Navigation** (`ui_screen.c`, `screen_def_t.use_latch`): left / right move to the spatial neighbour; with none on that side (and on `EL_DIRECT`) they edit. Push latches only where `use_latch` is
  set (the RACK tab; a first version removed latch everywhere and the user found the rack unusable without it). Encoder behaviour was left as it was.
- **Column knobs** (`ui_input.c`): catch by default. The first version only released on an exact step hit, which a normal sweep skips: now it releases on crossing the value. The first arrow was
  inverted (knob above the value means turn left). A 4 px wedge at the right end of the row (`ui_draw.c`) shows the way. `cfg.knob_mode` (GENERAL "Knob") switches to Direct.
- **Re-evaluation after manual changes** (`synth_ui_catch_refresh`, called by `app_step` when `catch_stale`): the catch state used to change only when a knob moved, so a page change or an encoder edit
  left a knob "caught". The first refresh ran after knob moves too and the knob stopped latching (the step re-measured by walking differs by one from the step the knob just set); the user had it backed
  out. It is back with two rules: it never runs after a knob move, and `knob_cur == -2` keeps a knob that just set its value caught. **Not tested by the user yet.**
  Bug found by the user (a jump key "reset every parameter of the page to 0"): the measuring helper (`knob_param_pos`, now `knob_measure`) left the parameter at its minimum and overcounted the range by the
  start position, so every catch check and every refresh parked the page's parameters at 0 and skewed the catch maths (the "finicky" catch). Now it restores the value and returns the true range.
- **Shift + column knob** (`ACT_PAGE_KNOB_SHIFT`, `ui->knob_shift[]`): knob 1 = speaker level (off -> 5 % .. 100 %), knob 2 = master volume. Bindings: the plain column-knob rows are `MODS_NONE`.
- **GENERAL tab**: seven rows on a 64 px screen did not fit (the user could not see "Knob"). The list scrolls to keep the focus visible and `screen_draw` clips to the area under the header
  (clipping applies to every declarative screen: if a tab ever draws outside its area, look there).
- **RACK tab links** (`scr_rack.c`): every link is drawn; a module scrolled out of the window stands at the window border on its side (stub + lane), so links never vanish while scrolling.
- **Jump keys** (`ACT_JUMP`, `UI_JUMP_1..8` that the user added to `ui_event_t`; `jump[]` in `synth_ui_t`; `fns[]` in `keymap.c`): Shift + key saves (`in_rack`, tab, page, row), key goes there.
  A first version had two actions and no keymap entry (unreachable): found by a review the user asked for after a model change; fixed. Leaving the rack editor by a jump now rebuilds the synth.
- Risks / limits: (the jump slots and the knob mode are saved since 2026-10-07, ui.cfg; slots point at module id + page since 2026-10-07, so rack edits no longer move them); `catch_stale` is a file-level static in `app.c`;
  refresh walks up to four parameters per manual change (cheap, not measured on the board).

## Filters, UI latency, voice bus (2026-10-06; flashed and measured, NOT listened to; decisions in ENGINE_DESIGN.md ADR-038)

- UI: `display_send()` replaces `u8g2_SendBuffer` (HAL); on the board a `display` task (core 0, prio 1) sends only changed tiles; `app_step` puts the redraw off while input is
  queued. Measured with `HWV1_DEBUG_UI` while playing: slowest UI step ~120 ms -> 4-16 ms, up to 92 events and 69 synth updates per second (was ~8). Screen checked by the user
  after the column-wrap fix (the panel's column offset 96 must wrap: a run at tile x is sent with the offset moved).
- Note-on overruns: `OscEngines::reset()` cleared the 4 KB Karplus buffer in PSRAM for every oscillator; rapid notes now 0 over budget (startup worst 2.3 -> 0.92 ms).
- Cutoff: ~20 ms glide at any block size, coefficients interpolated across the block. New FL types LP6 / Ladr / ChLP (`algo` of the Filter module, live switch), measured
  per 4 voices: LP 18.0k, LP24 24.7k, LP6 7.8k, ChLP 6.4k, Ladr 15.9k. Serial `flt T`. Host test `light_filters_lp6_ladder_and_chamberlin`.
- Not done: listening (all of it); the Ladr tuning at high cutoffs is approximate (not zero-delay); the light types have no exact per-sample path for audio-rate cutoff FM.

## Key LEDs (2026-10-06; flashed, checked by the user: every key lights its own LED, colours right; red and green were in fact swapped, colour order RGB -> GRB 2026-10-07)

- Driver `leds_esp32.cpp`: FastLED 3.10.6 pinned (it gives the first RMT channel DMA on the S3 by itself; SynthBox's `patch_fastled_dma.py` targets 3.10.3's file layout and is not
  needed), `-DFASTLED_RMT_MAX_CHANNELS=1`, SK6812 order **RGB** (SynthBox's BGR swapped red and blue here), chain map in physical columns (the HWV1_FLIP_COLS flip mirrored every row),
  **brightness capped by `HW_LED_MAX_BRIGHTNESS` (0..255, set in platformio.ini; was 51 = 20 %, the user raised it to 80 on 2026-10-06): full white on 36 LEDs ~2 A browns out the board / USB**. A transfer only when the frame changed.
- What they show (user choice "play feedback"): `core/key_leds.c` from the key layout: notes in piano colours (C teal, naturals dim white, sharps dim blue), held = orange;
  Shift yellow (bright while on), Menu blue (bright while open), Back red, Play green (bright while the sequencer runs), Octave violet (bright when shifted that way), navigation dim white.
  Jump keys (`Jump 1..8`): dim teal while the slot is empty, bright teal once saved.
  Repainted at most every 20 ms, at once on input. The simulator's LED HAL is a no-op (the panel window does not draw them yet).
- Serial (dev): `leds N`, `leds all`, `leds off` / `leds on`. Audio while playing keys: see the todo (the overruns happen with the LEDs off too).

## Mixer polish (2026-10-06; host-tested in the 6 configurations, flashed and measured, NOT listened to)

- Mapper (`rack_graph.cpp`): the running signal is a list of sources (`RunSrc`); the next module gets one cable per source and the plan's MIX step sums them
  (32-bit, one saturation). A modulating oscillator joins through a cable whose depth is `Lvl` x 1/sqrt n (`qd()`, never unity: live `SetDepth`), Mute = depth 0
  (skipped by the MIX step). More than 8 sources: the first eight fold into one pass-through Mix4. `Engine::set_edge_depth` now checks the pending plan first
  (a depth edit right after a load no longer forces a rebuild). VoiceOut: one product for both buses when centred (exact; saved only ~75 cycles per voice).
- Tests `test_rack_mix.cpp`: no Mix4 nodes for 8 sources, the fold at 9, the mix equals the sum of the sources alone (<= 8 LSB), level / mute edits stay live.
  The sound changes only by rounding (the old Mix4 multiplied by 32767/32768 per stage and saturated per stage): no listening round needed unless a mix clipped before.
- Board (44.1 kHz, 64 frames, budget 174149): Para 8 osc x 4 voices, 4 keys 82.6k -> 66.1k cycles (Mix4 was 16.7k); Para 4 osc x 8 voices, 8 keys 100.5k -> 71.5k;
  startup patch (Mono) 1 key 63.5k -> 61.5k. Biggest remaining voice cost: the oscillators (Strng ~1.5k per oscillator per voice), then VoiceOut ~790, NoteIn ~310 per voice.

## Mod Para, the rack oscillator's quality and the Strng engine (2026-10-06; flashed and measured; Mod Para listened to by the user: OK, legato and glide OK)

Decisions in ENGINE_DESIGN.md (ADR-036 stage 2 "as built", ADR-037 decision 5). The user flashed-and-test permission covers this task (COM8).
- Mod Para: GENERAL Type "Mod Para", Voices 1..8, PEnv Legato / Retrig / Voice. The rack splits at its first filter; `GateIn`, `ParaGate`, `EnvG` (T_ENV_G) are the new engine
  parts, `ProcessCtx` carries the voices and the held-key count. Host tests (`test_para.cpp`): both keys sound, a released key stops while another is held (power 1e15 -> 1e7),
  the last chord rings through the shared release, Retrig raises the level x 2.8 on a new key, Legato x 0.98, Voice releases a key alone.
- Board (`patch para N V`, Strng saw Mip, FX dry): 8 osc x 4 voices: 1 key 28.8k, 2 keys 46.8k, 4 keys 82.7k cycles (47 %); 4 osc x 8 voices: 1 key 20.3k, 4 keys 51.7k, 8 keys 100.5k (58 %);
  0 blocks over budget. One Strng oscillator (a pair) about 1.5-1.9k cycles per block per voice; Mix4 2k per voice at 8 oscillators.
- Rack OSC: Wav "Strng" (engine 8: Timbre = detune up to 50 c, Morph = saw / pulse / tri), Q row on OSC TUNE (Blep / Mip / Naive). Host C7 saw: Blep -28.2, Mip -40.6, Naive -12.5 dB.
- Aliasing check on the board (`dump` + an offline DFT, C5 single note, dry): saw Mip -40.6 / Naive -18.4 dB, pulse -40.2 / -20.1, triangle -39.7 / -39.7 = the host's numbers.
  What the user heard on `chord 16` (8 notes + a chromatic cluster C#5..G#5, 2 detuned saws per voice) is most likely beating of the cluster, not aliasing; to confirm by ear.
- Engine memory: the instance records and plans are sized by the real voice count (no static kMaxVoices tables): firmware RAM 50.7-51 % (was 55.6 % with the first Strings build).

## Strings, the third synth type (ADR-037; 2026-10-06; flashed and measured, NOT listened to by the user)

User decisions in ENGINE_DESIGN.md ADR-037 (do not re-litigate). What exists:
- `engine/modules/strings_modules.*`: `Strings` (voice scope: two detuned oscillators saw / pulse / tri, naive or mipmap, linear ADSR per block, one-pole LP on / off with
  key and envelope tracking, velocity, level; one render loop per wave x osc kind x LP, chosen per block) and `Ensemble` (global: three taps on one 11 ms line, slow + fast LFO at 0 / 120 / 240 degrees).
- `engine/dsp/wavetables.*` (generated, `tools/gen_wavetables.py`): saw and triangle, 10 octave bands, 1024 samples + guard, 41 KB in flash (`ENGINE_WT_RAM=1` for RAM).
- Type `SYNTH_STRINGS` (GENERAL Type "Strings", `str_voices` 1..32 kept apart from the 8 of Modular / FM); pages STRINGS (Wav Osc Det Mix), STR TONE (PW Lvl), VOICE LP (LP Cut Env Key),
  STR FILTER (Typ Cut Res; Off = no node), AMP ENV (A D S R; the curves are ignored), SEQ, SEQ SETUP; menu tabs GENERAL, FX RACK, KEYS. Params in `synth_params_t.str` (P_STR_*).
  FX rack type `FX_ENSEMBLE` ("EN": Rate Dpth Shim Mix) for every synth type. Serial `patch strings`.
- Engine: `ENGINE_MAX_VOICES` 8 -> 32; the plan's instance table is now sized by the plan's voice count (`Plan::inst(node, voice)`), so plans did not grow (the test graph's module memory went
  16.6 / 8.8 KB -> 13.0 / 4.8 KB for 8 / 1 voices); the instance records still grow ~13 KB static (firmware RAM 51.5 -> 55.6 %).
- Host: 151 tests, all six matrix configurations green. C7 saw inharmonic power: naive -12.5 dB, mipmap -40.6 dB; 32 voices are real (rms 8 notes 4211, 32 notes 7208, 32 notes on 8 voices 3301);
  linear attack / release at half way 0.51 / 0.48. Screens checked with `ui_dump` at 128 x 128.
- Board: see the todo (16 voices 87k with the startup FX). Not done: listening, level balance (Lvl 0.5 x VoiceOut 0.5 per voice: 32 voices may clip, Mix 1.0 default), default pad envelope (AMP ENV is shared with Modular).

## ESP32Synth (removed 2026-10-06)

The ESP32Synth library (github.com/danilogcrf2-oss/ESP32Synth, MIT) was tried as a second engine on the board (`alt on`, a lazy-allocated object rendered by our audio
task) and removed at the user's request once the Strings type existed (git history: commit f714a84 has it). What it taught: thin voices (one oscillator x a linear
per-block envelope) make many voices cheap (506 cycles per voice and 32 frames, saw, measured); its S3 "SIMD" vector types compile to scalar code (0 `ee.*` instructions);
an outside engine needs its own voice allocator, mono -> stereo and its own measurement hooks: `engine_synth.h` is not an engine-neutral backend interface.

## State (end of the ESP32 bring-up and optimization session)

- The application runs on **our own DSP engine** (`src/engine`, C++17, q15) on **both** the desktop simulator and the **first hardware prototype** ("HWV1": ESP32-S3-Pico, SH1107 128 x 128 OLED, TCA8418
  keyboard, quadrature knobs behind an analog mux, joystick, I2S DAC). 132 engine tests are green in all six rate / block configurations (`-Matrix`, run at the end of this session); the simulator and the firmware build.
- Working on the board (checked by the user): display, keyboard (map found from their key log), the 7 knobs (quadrature decode with hysteresis) and the joystick, power hold, I2S audio (the test tone and the engine),
  serial debugging, the boot-time benchmark, the serial command channel. The default patch is now `rack_init_startup`: **four oscillators on four different engines (Karplus, Modal, Supersaw, Additive) into one filter, delay 1000 ms / 40 %,
  reverb 40 %**. `rack_init` (osc, filter, saturator, LFO) is the older demo rack that the tests build on.
- **Not verified on the board**: the speaker amplifier (SPK_SD is left floating on purpose, the headphones/DAC path is what was heard; `HWV1_SPK_SD_MODE=1` drives it high), audio glitches while the screen draws (mitigated by IRAM code, never checked
  by ear), "the MCU stalls when no serial port is connected" (a fix was applied: non-blocking serial and `delay(1)` in the idle loop; maybe power related), the TF card (none), the new prototype's controls.
- **Not listened to by the user yet** (all of these changed the sound slightly and were only verified by tests): the interpolated filter coefficients, the 5-saw supersaw, modal as phasors, the additive recurrence, the half-rate reverb, mono delay / reverb,
  the vowel / dust filters in float, the dust level compensation.
- The user's earlier plans: save / load of racks and FM patches "after some fixes"; the engine / UI work of the previous sessions (ADR-028..034) was never listened to either (levels untuned; they asked to skip that step).

## Modular engine redesign (ADR-036: Mono / Paraphonic / Poly, capture, cost metadata) - stage 1 done on the host

The sampler is "fine for now" (user); the next topic is the modular engine. Decisions are in ENGINE_DESIGN.md ADR-036 (do not re-litigate): three modes (Mono default, Paraphonic, Poly kept as the legacy mode); "internal resampling" = capture a patch into a sample for the Sampler (not real-time polyphony); paraphonic splits the rack at its first filter with selectable envelope behaviour (first-key / every-key retrigger / per-voice amp env); budget = cost metadata only for now.
- **Stage 1 (done, host-tested, NOT flashed / measured / heard):** `Engine::load(graph, nvoices)` (the `Voices` setting was cosmetic before: the engine always built 8 copies); a node whose voice count changed is created anew and the old instances are freed by `gc()`; `Engine::set_voice_mode(Mono|Poly, legato, glide_ms)` with a last-note-priority key stack (16 keys), legato (pitch changes, envelopes keep running) or retrigger, exponential glide at block rate; `cfg.mode / glide / legato` in the GENERAL tab (default **Mono**); mode / voice-count changes rebuild, glide / legato are live; serial `mode mono|poly [glide] [legato]`.
  Host numbers: 1 voice instead of 8 saves about half the module memory in the small test graph (16.6 KB -> 8.8 KB; real patches with Karplus / delay buffers should save much more: measure `[HEAP]` on the board); glide of 100 ms reaches 63 % in 105 ms.
- **Next on the board (needs a flash):** with the startup patch in Mono, the CPU and `[HEAP] fast heap used` should drop to about a voice's worth; check by ear that Mono feels right (last-note priority, legato on / off, glide) before stage 2.
- **Stage 2 (paraphonic)**, in the order I would build it: `ProcessCtx` gets the voice array; a global `GateIn` module (any-key gate, a per-key trigger, lowest / last / highest pitch, velocity); `Env` as `Env<Scope>` like the other `_G` variants (it already takes its gate from a cable); the mapper builds per-voice modules up to the first filter and global ones from there; the three envelope policies; paraphonic becomes another synth type (`SYNTH_MOD_PARA`, ADR-036 decision 5: the mode is part of the type, there is no Mode row).
- Stage 3 (capture): `Capture` module + a RAM-backed `StorageDevice` behind a mux with the card; saving to the card needs write support in `sd_card_spi.cpp` (CMD24 / CMD25 + FATFS write). Stage 4: per module cost model from `[PROF]`, a test against the host profiler.

## TF card and sampler on the board (measured on the board with the automatic loop of the skill; the loop fix and the sound are NOT yet listened to)

The user reported glitchy sampler playback, loops misplaced / shifted, 2-3 voices, the card read "too much". What the measurements found, in order:
- **Root cause of the glitching: ESP-IDF's sdspi host spent about 40 ms before EVERY SD command on this board** (a plain CMD13 took 40 ms; with a pin trace, CS / MISO stayed quiet for 40.5 ms and the real transfer took 55 us at the end; two different cards (an old 1 GB and a 32 GB), SPI2 and SPI3, 10 and 20 MHz, MISO pull-up: all the same; a bare 10-byte SPI transfer on the same bus was normal).
  A 4 KB read therefore cost 80 ms = 50 KB/s, and one sampler voice needs about 96 KB/s. Fix: an own small SPI-mode driver (`sd_card_spi.cpp`, read only, registered with FATFS): 1 sector 82 ms -> 1.4 ms, 8 sectors 5 ms, a scan of the 5 demo files 6 s each -> 0.36 s. The IDF host stays behind `-DHWV1_SD_IDF` for comparison.
- **Loop points** (ADR-022 addendum): a fixed `block % 8` ring slot made a loop's end and start blocks collide, so every lap dropped out; fixed on the host with a test (it fails without the fix). Not yet heard on the board.
- **Sampler CPU** (measured, 44.1 kHz, `patch sampler 3 2` = pad_c4, forward loop, one filter, no effects): the Sampler cost 15.7k cycles per voice and block (490 per sample). A per-block fast path (steady rate, no wrap, one contiguous run of data; bit-identical output, checksum test `sampler_fast_path_is_identical_to_the_general_loop`) cut it to 5.4k.

| notes held | cycles per block (budget 174149) before -> now | blocks over budget before -> now | sampler underruns before -> now |
|---|---|---|---|
| 1 | 27.6k -> 17.1k | 0 -> 0 | 0 -> 0 |
| 3 | 72k -> 41.5k | 0 -> 0 | 0 -> 0 |
| 4 | 103k -> 54.7k | 143 -> 0 | 0 -> 0 |
| 6 | 147k -> 81k (47 %) | 1119 -> 0 | 20 -> 0 |
| 8 (two octaves) | not measured -> 312k (179 %) | -> 1266 of 2127 | -> 420 |

- **Now the limit is the card at 8 voices**: about 490 KB/s delivered at 7 ms per 4 KB read (0.6 ms per sector: the token poll is one SPI call per byte and every sector is a separate 514-byte transfer plus a copy; the wire time would be 0.2 ms). An underrun sends a voice to the slow general loop (490 cycles per sample), which is why 8 notes also blow the CPU budget.
  Next steps (in order): (1) read a block's token and data in one SPI transfer for every sector after the first of a command (expected 0.6 -> about 0.35 ms per sector, exact); (2) larger / fewer commands (CMD18 for 16 sectors); (3) design choices for the user: voices playing the same sample share the blocks they read, a faster clock (`HWV1_SD_FREQ_KHZ`, 20000; 26.7 MHz is outside the SD default-speed spec), prefetch deeper.
- **Card handling** (new, tested with the old card on the board, and the screen in `ui_dump`): the card is polled once a second (no card-detect pin). A card whose sector read takes more than 15 ms is not used and the app shows "SD CARD TOO SLOW"; removal or a swap resets the catalog (loaded sample slots stay in memory until reboot: known debt) and the app rebuilds the synth (a 170 ms audio stall, once per card event).
  A failing mount attempt must never busy-wait (it starved the idle task before): the own driver returns quickly when no card answers.
- Task priorities are in DEVELOPING.md ("Tasks and priorities"). `samples_src/` + `build.ps1` (`-Sd E:` copies to the card) is the sample workflow; names are cut to 23 characters; only `.smp` is read on the board.
- `platformio.ini` (2026-10-07, set by the user): `-DENGINE_SR=44100`, `DEV_OUTPUT_GAIN_PCT=25`, `DEV_BOOT_DELAY_MS=500`, `DEV_SERIAL_CMD` on; **`HWV1_BENCH`, `ENGINE_PROFILE`,
  `HWV1_DEBUG_AUDIO`, `HWV1_DEBUG_UI` are commented out: turn them back on before any measurement (`serial_test.py` needs `ENGINE_PROFILE` + `HWV1_DEBUG_AUDIO`)**.

## Latest measurements (prototype, startup patch, mono FX + half-rate reverb, budget 160000 cycles per 32-frame block)

Measured with `tools/serial_test.py` on the last build that was on the board (engines still with the generic per-sample loop; the loop fast path below was built after it):

| notes held | cycles per block | % of budget | worst render (limit 1333 us) | blocks over budget |
|---|---|---|---|---|
| 0 | 30.1k | 19 % | 323 us | 0 / 3004 |
| 1 | 60.3k | 38 % | 669 us | 0 / 3755 |
| 3 | 119.4k | 75 % | 1186 us | 0 / 3755 |
| 6 | 208.5k | 130 % | 1955 us | all |

Per oscillator and voice (cycles per sample): add 263, modal 229, ssaw 237, vowel 217, dust 141, fm2 134, karp 117, fold 117. Fixed costs: reverb 20.5k, delay 8.4k (mono), chorus 0.4k. Per voice of the 4-engine patch about 24k
(engines 15k, filter 4.4k, Env 2.2k, Lfo / Shaper / Vca / VoiceOut / NoteIn the rest). Fast heap: 83.5 KB of 106 KB used, **0 bytes spilled**.

**Interpretation.** 4 voices of this patch fit with margin (about 130k at 4), 5 do not, 6 are at 130 %. The cost is now almost all in the oscillator engines (about 60 % of a voice); each engine sits at roughly 30 cycles per basic step, which is what
a table read plus a few multiplies costs on this in-order core, so further gains need fewer steps, not tuning. A profile inside the oscillator showed about 50-58 cycles per sample of loop overhead (gate test, trigger check, engine dispatch, level) around every
engine: the build made after these numbers adds a **fast path** (steady pitch and gate: one tight loop per engine). **It had not been measured on the board when this was written**: after flashing, rerun
`serial_test.py --engines "0,1,4,6;2,3,5,7" --chords 2` and `serial_test.py --chords 1,3,6`; expect each engine about 40-50 cycles per sample lower (about 4-5k per voice). If the per-engine numbers equal the table above, the old binary is still on the board.

## Full keyboard and key layouts (2026-10-05; built and host-tested, NOT flashed)

- All 36 keys of the prototype are mapped: the HAL matrix is now 8 x 5 (`KEY_COLS 8`); HWV1's 4 function keys are HAL row 0 (they were BTN 3 / BTN 1 / BTN 2 / PLAY), its
  4 x 8 note keys rows 1..4. `input_key_present()` (new in `hal_input.h`) says which keys a board has.
- Key functions come from a **layout** (`core/keymap.c`): 3 built in (*Keys 8x4* default, *Two 4x4*, *Notes+Nav*) + *User*, edited in the new **KEYS** menu tab
  (last tab in both modular and FM). User decisions: the three layouts as presets, user keys on the TF card (`keys.cfg`, text), only keyboard keys in the page,
  lock-out escape = Reset row + F1 held 2 s within 4 s of boot. Details in DEVELOPING.md ("Key layouts").
- Card writes: `sd_card_spi.cpp` got CMD24 single-block writes (+ CMD13 check); file jobs run on the `sd_io` task (`samples_esp32.cpp`, `hal/hal_storage.h`). **Risk the
  user accepted: a write bug can damage the card's file system; back the card up before the first test.**
- Verified: simulator + firmware build; a scratch test (all layouts, User copy, text round trip, save on menu close, card in / out, KEYS-tab key selection, boot reset;
  it found and fixed a wrap-around that fired the reset at once). Not verified: anything on the board (key map orientation of the right half, card writes, keys.cfg on a real card).
- Board test (user, 2026-10-05): all keys OK, F1..F4 OK, `[SD] wrote /sdcard/keys.cfg (689 bytes)`, the layout survives a reboot, samples stream after a write. The F1 power-on
  reset did NOT work: the TCA8418 reports changes only and `kbd_init` drains its FIFO, so a key held from power-on makes no event. Fixed (built, NOT flashed): `kbd_init` reads
  F1 directly before the key scan starts (column driven low as a GPIO, row read; up to 2 s while held), `input_boot_reset()` in the HAL, the app resets at init;
  the "KEYS RESET" screen goes at the next key or after 3 s. Boot log: `[KBD] key r4 c7 held at power-on`.

## Sampler at high pitch (measured 2026-10-05, pad_c4 forward loop, `patch sampler 3 2`, which is Mono: one voice)

| note | stream speed | card | Sampler cycles per block | blocks over budget |
| --- | --- | --- | --- | --- |
| C4 (root) | 1x | 92 KB/s | 6.0k | 0 |
| C5 | 2x | 183 KB/s | 7.1k | 0 |
| C6 | 4x | 369 KB/s | 8.7k | 0 |
| G6 | ~6x | 546 KB/s, underruns | 10-19k | 21 |
| C7 | 8x | 640 KB/s (needs ~770), 160 underruns/s | **182k** (budget 174k) | 342 of 581 |

- Two problems: the card bandwidth (~640 KB/s at most) is reached by ONE voice at ~6-8x its root; and a starved voice cost ~20x CPU, because every sample ran 4 `fetch()`
  misses, each searching the ring by 64-bit atomic tags (a locked libatomic call on the S3). Fix (exact, built, 144 tests x 6 configurations green, NOT flashed / measured):
  `miss_blk_` in the Sampler, a missing block is searched once per process() call.
- **Optimization loop of 2026-10-06 (user allowed flashing for this task; all exact, measured on the board, details in ENGINE_DESIGN.md ADR-022 addendum 2):**

| case (25 MHz unless noted) | start of the session | now |
| --- | --- | --- |
| 1 voice C7 (8x): Sampler cycles / blocks over budget | 182k / 342 of 581 | ~28k (starved) or ~9k / 0 |
| 1 voice C7: underruns per 2 s | 320 | ~1 (25 MHz) / 0 (40 MHz) |
| 3 voices C5 E5 G5 (sum ~7.5x) | ~150 underruns per 2 s | 0-100 (card at its limit) |
| 6 voices C4..G5 (sum ~10x, needs ~880 KB/s) | render 1444 us, 325 blocks over budget | render 966 us, 2-7 over (card limited: ~500 KB/s) |

  Changes: Sampler `miss_blk_` and `starved_block()`, loader tag snapshot, `ENGINE_RING_BLOCKS` 12, driver token poll 8 bytes per call, data CRC check + retry, CMD6 attempt,
  `HWV1_SD_OVERCLOCK`, `seek avg` in the `[SD]` line (seeks cost ~100 us: not the problem), scattered-read bench. Test `a_stalling_card...` now stalls 1 s (longer than the ring).
  Speaker: `DEV_SPEAKER_DEFAULT=0` (user asked for the speaker off while testing).
- **Open, user decides:** (a) 40 MHz overclock: out of this card's spec, 0 CRC errors in 100 s of streaming, 4 KB file read 5.0 -> 3.7 ms; (b) 8 KB reads per command
  (2 ring blocks): ~15 % more bandwidth, loader + storage change; (c) pre-decimated copies of samples for high notes (converter; bandwidth stops growing with pitch,
  less aliasing, ~2x card space); (d) a faster card / the SDMMC 4-bit slot of the next prototype. **Not yet listened to** after these changes.

## Next performance steps (in the order I would take them)

1. Flash the fast-path build, measure, record the numbers here (and in ADR-035's table). Ask the user to **listen** to the list of unlistened changes above and to say which sound changes they dislike; each has a flag or a one-line revert (see ADR-035).
2. Remaining per-voice costs: Env 2.2k (two per voice: amp env + the filter's own, 47 cycles per sample, still 64-bit-free but not specialised), Lfo 1.8k, Shaper 1.8k, VoiceOut 0.9k. A fast path for "Env idle / sustain" and for constant LFO shapes is likely.
3. Engines: additive 263 (12 weighted adds + 11 recurrence steps; fewer harmonics at high pitch would help but changes the sound), modal 229 (8 modes x ~20), ssaw 237 (5 saws, the rest is mixing), vowel 217 (3 float filters).
   Ask the user before any change that alters sound (they decide quality trades; offer options with pros and cons).
4. Fixed cost: reverb 20.5k (the tank halves ~5k each at half rate; trimming one of the four input allpasses or the second decay diffuser saves 2-3k and changes the character; a cheaper diffusion structure was proposed, not tried),
   delay 8.4k (PSRAM Hermite read + write per sample, 5.2k).
5. Before a release remove the dev flags (list in DEVELOPING.md) and decide `DEV_OUTPUT_GAIN_PCT` (currently 1 %, set by the user while testing at night), `ENGINE_FX_MONO`, `ENGINE_REVERB_HALF`.
6. Open hardware items: speaker amplifier check, display / audio separation check by ear, the TF card.

## Decisions that went against my recommendation (recorded with their risks in ENGINE_DESIGN.md)

The user decides and I record the reasoning and move on, without re-litigating:
- **All signals are q15 audio-rate blocks** (ADR-010): simpler, costs CPU for slow modulators.
- **Naive saturation** (ADR-013), **q15 block-floating-point FFT, N = 512** (ADR-016), **Dattorro plate reverb** and **fixed Juno-like chorus modes** (ADR-019 / 020), **AMY replaced outright** (ADR-027).
- **MODIFIERS tab covers the keys too** (2026-10-07; I recommended knobs + encoders + joystick): risk = a larger tab list (~60 controls on the board) and file;
  mitigated: Default entries keep the old behaviour, modifier keys and Menu-less lock-outs cannot happen through a layer (Shift / Mod keys are never remapped by it).
- On the hardware (ADR-035): a **float exception** for the filters; **mono delay / reverb** and the **half-rate reverb tank** as build flags; the **5-saw supersaw**; the sample rate is **never below 44100 Hz**; modal as phasors and the additive recurrence were done before asking
  because their tests show the same sound within a stated tolerance.

## Known debt / small issues

- ui.cfg is written into one `STORAGE_FILE_MAX` (4 KB) buffer: 64 macro destinations (~2.2 KB) plus many Shift / Mod key entries can pass it, and the end is
  cut silently (the cut lines are lost at the next load). Fix if it bites: split macros into their own file, or write in chunks. Min / Max step by 5 %.

- UI: the main view (pages) is not a declarative screen yet (no latch, old events); the Mono / Para GENERAL tab has 7 rows and only scrolls on a short screen; the Knob mode, jump slots and Shift-knob targets are saved in ui.cfg (2026-10-07);
  the sim's panel does not draw the key LEDs; this session's UI changes were verified on the board by the user, not by host tests (the 160 engine tests do not cover `src/core/ui_*.c`; `tools/ui_dump.c` can).
- Sample library: whole files are copied to RAM in the simulator; loaded samples are never unloaded; imports block the UI thread; a rack stores the file *index*
  (names needed when saving). Zones are engine-only (no UI). MS targets are limited to what the mapper realises (OC Pit / Lvl / PW, FL Cut, SA Drv, SM Pit).
- Sound-design round (ADR-031..034), not listened to: the convolver costs 2 x taps MACs per sample (not measured on the S3), the compressor / shifter / engines are tested against theory only,
  the engine previews in the OSC graph box are sketches.
- `samples/` holds the generated demo files (`tools/make_demo_samples.py`); `lib/minimp3` is not versioned like the other libs.
- `synth_params_t` and `param_id_t` still contain the old oscillator / filter fields; only the amp envelope is used.
- Rebuilding on menu close keeps held notes (state by node id), but removing the module a note uses silences it. Mapper limits: no modulation of resonance, mix or other modulators; MS has no sound.
- `svf_tick` (integer, 202 cycles) and `svf_coef_f` remain in `dsp/svf.h` only as the references the boot benchmark compares against; the engine uses `svf_tick_f`.
- The reverb / delay stage timers (`SEC_BEGIN` / `SEC_MARK`) and the oscillator counters stay in the code under `ENGINE_PROFILE` (zero cost without it).
- `tests/engine/test_sampler.cpp` has a `run_trace` helper that prints underruns. Engine items under "Known limits" in ENGINE_DESIGN.md (reverb q15 floor, block-rate envelopes, mono samples, ...).

## Working style and environment traps

- The user iterates with short requests, tests on the board or in the SDL window, and wants concise answers that say what was verified vs not. Windows 11, PowerShell, MSYS2 UCRT64; AZERTY keyboard; English with some French.
  For design work they want options with pros and cons at each stage; they often pick against my recommendation. **They flash the board themselves** (COM8); never flash. They sometimes say "flashed" about an older build: check (see the skill).
- The user often keeps the simulator open: `build.ps1` then builds `oled_sim_new.exe`. A stale exe has caused a false "bug" before.
- PlatformIO: use `C:\.platformio\penv\Scripts\pio.exe`, **never a bare `pio`** (PATH points at Python 3.13's pio, which recreates and destroys the penv). It happened twice; the second time (2026-10-05) Claude did it, and the user had to restore the penv by hand. Repair steps: DEVELOPING.md, "ESP32 firmware" warning. One link failure with no error text happened twice; the immediate rerun succeeded.
- One program per serial port: close the serial monitor before `serial_test.py`.
- Bash tool: **heredocs containing apostrophes or quotes break; `\n` inside a C string written from a Python literal becomes a real newline** (it hit `printf` lines many times: grep for a string that wrapped, or fix with a regex on `%u[\r\n]+"`).
  Write multi-line scripts with the Write tool and run them; edit with the Edit tool. Many sources are CRLF: scripted edits must keep each file's line endings.
- `src/platform/engine/engine_synth.h` is included from C: no default arguments, no C++ in it.
- Tool calls that run `g++` through PowerShell hide compiler errors (`NativeCommandError`): compile from bash to see them. A test executable needs `C:\msys64\ucrt64\bin` on PATH.
- Python on Windows: always `open(..., encoding="utf-8")`. The test runner takes a name filter (`engine_tests.exe reverb`) and, via the script, engine flags: `build_engine_tests.ps1 -Defs "-DENGINE_REVERB_HALF=1"`; `-Matrix` takes a few minutes, one configuration about 1 min.
- Do not commit unless asked. Delete `run*.txt` serial captures when done (ignored by git now); keep the numbers in this file.

## Starting the next session (compact prompt)

Run `/session-start` (skill `.claude/skills/session-start/SKILL.md`): it reads these notes, talks through the open items, writes the "Session todo" section here and proposes where to start.
For board performance work directly:

> Read `CONTINUE.md`, `DEVELOPING.md` and `ENGINE_DESIGN.md` (ADR-035), and the skill `esp32-optimize`. The board is connected on COM8 with the last build flashed. <what I heard / what to change>.
> Then re-measure with `tools/serial_test.py` and continue with "Next performance steps".
