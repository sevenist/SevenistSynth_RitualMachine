# Developer guide

A synth editor UI (128x64 OLED) that runs on the desktop (SDL window, sound from our **own DSP engine** through
SDL audio) and is structured to run on the ESP32-S3. The same `core/` and `engine/` code drives both. Two synth
types: a **modular** synth built from a rack of modules, and an **FM** synth (DX7-style, 6 operators) with a patch
editor. Both go through the master effects (chorus, delay, reverb).

The engine's design, every decision with its reasons and the measurements behind them are in
[ENGINE_DESIGN.md](ENGINE_DESIGN.md). This guide is about working in the code.

## The repo: a library and its apps (ENGINE_DESIGN.md ADR-043)

```
oled_sim/
├─ framework/            THE LIBRARY, shared by every app
│  ├─ engine/               the DSP engine (C++17; includes nothing from an app). Included as "engine/..."
│  ├─ hal/                  what a board gives an app: display, controls, key LEDs, the card's small files, power, large memory,
│  │                        and the audio output (the app hands over its render function). Headers only. "hal/..."
│  ├─ ui/                   the drawing kit over u8g2: style, layout, text, sprites, animations (gui), popups. "ui/..."
│  ├─ boards/hwv1/          the first prototype: pins, display, key matrix, knobs, joystick, LEDs, card driver and card task,
│  │                        power, I2S output. "boards/hwv1/..."
│  ├─ boards/desktop/       the desktop: the display and panel windows, the PC keyboard and mouse as controls, SDL audio,
│  │                        the card as the folder sdcard/. "boards/desktop/..."
│  ├─ host/                 desktop-only helpers (a slow-card model for tests and simulators, the .wav / .mp3 converter). "host/..."
│  ├─ tests/                the library's own tests: the engine alone
│  └─ tools/                table generators, sample tools, the shared desktop build (sim_build.ps1), build_engine_tests.ps1
├─ apps/
│  ├─ ritual_machine/       THE RITUAL MACHINE: this guide. Its own src/ (core, app, platform), tests/, tools/, assets/,
│  │                        build.ps1, platformio.ini, and the simulator's card (sdcard/, samples/, samples_src/)
│  └─ template/             the starter app: a four-voice synth with a screen and the board's keys, desktop + first prototype (its README.md)
├─ tools/new_project.ps1    .\tools\new_project.ps1 my_synth  ->  apps/my_synth, a copy of the template under its own name
├─ build.ps1, run.ps1       build / run an app's desktop program from here (-App name; default: ritual_machine)
├─ test.ps1                 every host test: the library's, then the Ritual Machine's engine and UI tests
└─ lib/                     third-party code for the desktop builds (u8g2, minimp3, amy; not versioned)
```

**In the rest of this guide a path without a prefix (`src/core/...`, `tools/...`, `tests/...`, `samples_src/`, `sdcard/`,
`platformio.ini`) is inside `apps/ritual_machine/`,** and `engine/...`, `hal/...`, `ui/...`, `boards/...` are `framework/...`. An app's folder is a whole
project: its scripts work from anywhere, and the firmware is built from it (`pio run -d apps/ritual_machine`; there is no
`platformio.ini` at the repo root any more). **In VS Code open `oled_sim.code-workspace`** (File > Open Workspace from File): it
lists the repo and every app as a folder of its own, which is what the PlatformIO extension needs to build and flash an app (pick
the app in its status bar). An app shows twice in the explorer; the repo's search leaves its files to the app's folder.

**A new project**: `.\tools\new_project.ps1 my_synth`, then `.\build.ps1 -App my_synth` and `.\run.ps1 -App my_synth`. What each
file of the new app is and where to change things: its `README.md`. Step 1 of the split (2026-10-13) made the engine the library;
step 2a (the same day) moved the HAL, the two boards and the drawing kit into `framework/`, so a new app gets a screen, the
controls, the card's files and the audio output without copying; step 2b (the same day) gave each board its own control table
(`hal/hal_input.h`: `input_controls()`; the ids: `boards/hwv1/hwv1_controls.h`, `boards/desktop/panel_controls.h`), and the
Ritual Machine its list of roles with a map per board (`core/controls.h`, `core/boards/map_*.h`: "Change what a control does"
below); step 2c (the same day) draws the simulator's panel window from a board's table, so the desktop program can be built as
either board: `.\build.ps1 -Board hwv1` ("The simulated board" below). Still the Ritual Machine's: the declarative screens, the
bindings, key layouts and layers, the sample library.

## Quick start (desktop simulator)

```powershell
cd D:\DEV\SevenSynthCore\ui\oled_sim
.\build.ps1                      # the Ritual Machine's simulator: clean build ~10 s, one changed file ~1.5 s (per-file objects in apps\ritual_machine\build\obj, parallel)
.\run.ps1                        # runs apps\ritual_machine\build\oled_sim.exe in its folder (its card is sdcard\ there; SDL2.dll from C:\msys64\ucrt64\bin)
.\build.ps1 -Board hwv1          # the same program as the first prototype (8 key columns, 7 pots): build\oled_sim_hwv1.exe, objects in build\obj_hwv1
.\run.ps1 -Board hwv1            # ... and runs it ("The simulated board" below)
.\test.ps1 -Matrix               # every host test: 237 engine tests (123 the library's, 114 the app's) in six sample-rate / block-size configurations, then the UI tests
.\apps\ritual_machine\tools\build_ui_tests.ps1     # the UI tests alone (tests/ui): the core driven by control events on the stubs of tools/ui_stubs.c
```

**Samples:** put `.wav` / `.mp3` files in `samples_src/` and run `.\build.ps1`: every new or changed file is converted to `samples/<name>.smp` (the cooked format; stereo is mixed to mono,
the loop and root note come from the WAV `smpl` chunk, or a note at the end of the name: `pad_c4.wav`, `bass_fs2.wav`). They are copied to the simulator's card, the folder `sdcard/`
(`sdcard/system/samples`; the folders of the card layout are made too; **F12** pulls the simulated card out and puts it back). `.\build.ps1 -Sd E:` also sets up the real
TF card (its folders and `E:\system\samples`). **Card layout** (`hal_storage.h`, `STORAGE_FOLDERS`): `system/samples` (the library), `system/config` (keys.cfg, ui.cfg, synth.cfg ...), `import`, `presets` (the presets, `*.preset`, sub-folders allowed). `-NoSamples` skips the step. See `samples_src/README.txt`.

If `oled_sim.exe` is running, `build.ps1` cannot overwrite it and builds `build\oled_sim_new.exe` instead (close the
window to get the normal name back; `run.ps1` starts the newer of the two). `.\build.ps1 -Clean` rebuilds everything.

The simulator opens two windows: the **OLED** and a **Panel** drawing the front panel of the board the program was built for. By
default that is the desktop's own board, the *panel*, a drawing of the future prototype (left strip: master volume knob,
encoders A and B with their switches, play button; centre: 4 x 5 matrix keys (a function row, then 4 note rows) with a knob above each column; right: 3 knobs,
joystick with push, 3 buttons). Every control works with the mouse (click = press; wheel over a knob / pot / encoder = turn; drag a knob or a pot
up / down; drag inside the joystick; right click on the joystick = its push) and, except the knobs, with the keyboard: each part shows its PC keys. The panel's bottom
lines show the last control -> action and the octave / Shift state.

**The simulated board (`-Board`).** The panel window is drawn from a board's control table (`hal/hal_input.h`: each control's kind
and its place `px`, `py`, the centre of its part in half key pitches), and the app sees that board: its table, its name, so the
Ritual Machine picks that board's map. The board is chosen when the program is built, and each board is a program of its own with its own objects:

| Command | Board | Program | What is drawn |
| --- | --- | --- | --- |
| `.\build.ps1`, `.\run.ps1` | `panel` (default) | `build\oled_sim.exe` (`build\obj`) | the future prototype: 4 key columns, 2 encoders, 8 knobs, joystick, Play, B1..B3 |
| `.\build.ps1 -Board hwv1`, `.\run.ps1 -Board hwv1` | `hwv1` | `build\oled_sim_hwv1.exe` (`build\obj_hwv1`) | the first prototype: 4 function keys + 4 x 8 keys, 7 pots without end stops, joystick |

After a change, build the board you run: `run.ps1` says so when the program it starts is older than another board's. `-App` and
`-Board` combine (`.\build.ps1 -App template -Board hwv1`). The files: `boards/desktop/sim_board.h` (the board behind `SIM_BOARD_<NAME>`;
`Get-SimBoard` in `framework/tools/sim_build.ps1` sets the define and adds the board's table to the sources), `sim_board.c` (the table as
parts and their places: controls at the same place are one part, an encoder with its push, a joystick's axes with its push; no SDL,
`tests/ui/test_controls.c` lays out both boards and checks that no part sits on another), `panel_sim.c` (draws the parts, the mouse),
`input_sim.c` (the PC keys, the pots' two readings). **PC keys come from a part's kind and place, not from the board**: a matrix key takes
the key of its row and column in the block F1.. / 1.. / Q.. / A.. / Z.. (8 wide); what turns (encoders, pots without end stops) takes a
pair each, `[ ]`, `; '`, `- =`, `9 0`, `O P`, `L .`, keypad `- +`, the two parts a board names first (`SIM_TURN_KEYS_FIRST`: the panel's
encoders, HWV1's pots 1 and 2, the Ritual Machine's rows and value) then the table's order, and `\` `/` push the first two; buttons take
Space, Enter, Backspace, Left Shift in the table's order; the joystick the arrows and Right Ctrl. On `hwv1`: `[ ]` = pot 1, `; '` = pot 2,
`- =` = pot 0 (volume), `9 0` / `O P` / `L .` / keypad `- +` = pots 3 / 4 / 5 / 6. A pot without end stops reports steps, or a position when the
app's map asks for one (a step moves it by 24 of 1023; the real board's noise thresholds are not simulated). **HWV1's panel places are a
guess** (`boards/hwv1/hwv1_controls.c`): correct `px`, `py` there against the board. The table below is the `panel` board.

| Control | Default role (table in `core/bindings.c`) | Keyboard (physical key, QWERTY names) |
| --- | --- | --- |
| Encoder A turn / push | previous / next element, i.e. rows (Shift layer default: pages) / jump to the page selector | `[` `]` / `\` |
| Encoder B turn / push | change the focused value, **no latch needed** (with Shift: 4 steps per detent) / activate a button | `;` `'` / `/` |
| Joystick | **one navigation model on every screen** (`ui_screen.c`): up / down / left / right move the focus to the neighbouring element; an element with no neighbour on that side (and every `EL_DIRECT` element: the module strip, the page / tab selector) is edited by left / right instead. **Push = latch, only on screens with `use_latch` (today: the RACK tab, whose dense multi-column grid needs it)**: the joystick then edits the latched value, push again to release; on a button, push activates it. In the main view (pages, not converted yet): rows and values, push activates the row. With Shift: up / down = octave, left / right = page | arrow keys, push = Right Ctrl |
| Button 1 | open / close the menu | Enter |
| Button 2 | delete the selected module (RACK tab) | Backspace / Delete |
| Button 3 (hold) | **Shift** | Left Shift |
| Play | start / stop the sequencer. **Shift + Play = Rec**: notes played go into the pattern (stopped: step by step; running: live). In the Pads section: Play = the clock and its sequencer, **Select + Play or Shift + Play = step mode** (the pad keys are steps) | Space |
| Matrix keys | **the key layout** (`core/keymap.c`, KEYS tab of the menu): function row F1..F4 = Shift, Menu, Back, Play (the layouts also give the function row's columns 5..8 Octave - / +, Page - / +: no board has those four keys, the simulator's PC-only columns 5..8 went on 2026-10-13); note rows by layout: *Keys 8x4* (default: bottom-left = C4 (60) + octave, +1 per key to the right, +8 per row up), *Two 4x4* (left block 0..15 with +4 per row, right block 16..31), *Notes+Nav* (left block 0..15, right block = octave, cursor cross with Latch, Select, Value, Row, Page), *User* | the panel's 4 columns: `F1`-`F4` / `1`-`4` / `Q`-`R` / `A`-`F` / `Z`-`V` (top to bottom); `-Board hwv1`: `F1`-`F4`, then 8 columns, `1`-`8` / `Q`-`I` / `A`-`K` / `Z`-`,` |
| Octave | -5..+4: every MIDI note 0..127 is reachable | Shift + joystick up / down |
| Column knobs 1..4 | the value of row 1..4 of the current page. **Catch mode (default, GENERAL tab "Knob")**: a knob is ignored until it crosses the current value (or lands on it), so no jump; a small arrow at the right edge of the row says which way to turn (right = turn right). It is re-evaluated after every manual change (page, row, encoder / joystick edit, macro, Shift) but never after a knob move. *Direct* mode: the value follows the knob at once, no arrows. **With Shift / Mod**: what the MODIFIERS tab gives it (default Shift + knob 1 = speaker level, Shift + knob 2 = master volume): a parameter, with the same catch, or a step action | mouse |
| Right knobs R1..R3 | macros 1..3 of 8 (MACROS tab): a macro drives up to 8 parameters, each through its own Min / Max / curve; they start on the first filter's cutoff / resonance and the first LFO's rate; **Shift + knob** adds the parameter under the cursor as a destination (Shift layer entries "Learn M1..3"). Not on the prototype: there any knob can play a macro through a Shift / Mod entry "Macro 1..8" (default on a board without R knobs: Shift + K3 / K4 = Macro 1 / 2, with the catch) | mouse |
| Master volume knob | master volume (same value as Vol in the GENERAL tab; 0..2 = the gain, ramped over one audio block). On the prototype it is an endless knob: one step of 0.05 per detent from the current value | mouse |
| Esc, closing a window | quit | |

On an AZERTY keyboard the keys keep their physical position: "Q" is the key labelled A, "W" is Z, "Z" is W, "A" is Q, `[` is `^`, `]` is `$`,
`;` is `M`. The matrix block is therefore the block starting at F1, `&`, A, Q, W on AZERTY, 4 keys wide on the panel and 8 with `-Board hwv1`
(its last column there: `!`, I, K, `;`).

**Key layouts (KEYS tab).** The matrix keys are not in the binding table: each key gets one function from the active *layout* (`core/keymap.h`).
Three layouts are built in (in flash); the fourth, *User*, is the user's. In the KEYS tab: *Layout* picks one, *Key* picks a key (or press it: on this tab a key
selects itself and only notes, Shift and Menu still act), *Func* gives it a function (the controls, then notes C4..B7; editing a built-in layout copies it into
User), *Reset* goes back to *Keys 8x4*. The right box lists the keys with their functions. The layout and the User keys are saved in `system/config/keys.cfg` on the
TF card when the menu closes (the other UI settings, Knob mode, jump slots and the Shift / Mod layers, are in `system/config/ui.cfg`: `core/ui_settings.c`, written 2 s after a change) (a text file: `layout user` then `key <row>.<col> <function>` lines, editable on a PC; the simulator's card is `sdcard/`). A card that
shows up gives its `keys.cfg`, unless the keys were changed meanwhile (then they are written to it). **Lock-out escape**: hold F1 (the top-left function key)
for 2 s within 4 s of start: the layout goes back to *Keys 8x4* and a "KEYS RESET" screen shows until F1 is released.

**Jump keys.** *Func* also offers `Jump 1..8` (`ACT_JUMP`, arg = slot). Pressing a jump key goes to the location saved in its slot (menu or main view, tab, page, row; `synth_ui_t.jump[]`,
`ui_input.c`); **Shift + the key saves the current location** in the slot. Leaving the rack editor by a jump rebuilds the synth like closing it with Menu; a saved row / page is clamped to
what exists now (saved page numbers go stale when the rack is edited). The slots are saved in `system/config/ui.cfg` (`core/ui_settings.c`). A slot stores WHAT it points at (`jump_slot_t`: module id + type + which of its pages, a global page, or a menu tab's kind), resolved to a page index once per page rebuild (`synth_ui_jump_resolve`): inserting modules does not move it; deleting its module clears it (LED dim); a page the synth type does not show (FM) makes it wait (popup "Not in this synth"). The key's LED is dim teal when the slot is empty, bright when saved.
On the KEYS tab a jump key only selects itself. Saving a location is only Shift (or Mod) + the jump key (user, 2026-10-07: keep it simple; the "Save 1..8" key function was removed).

**Shift / Mod layers (MODIFIERS tab, `core/modifiers.h`, `scr_mods.c`).** Every control, keys included, has an entry per layer (Shift, Mod): *Default*, an action, or
(knobs and encoders) a parameter. **Shift and Mod work the same**: Default = the control as without a modifier, except a column knob (drives nothing, popup
"No target") and a jump key (saves its slot); Shift only: a note key latches its note, Back releases every latched note. Shift's starting entries: EncA pages, EncB x4,
joystick up / down octave, joystick left / right **fine steps** (`ACT_VALUE_FINE`, `core/fine_step.h`), joystick push XY mode (`ACT_JOY_MODE`), R knobs learn, K1 speaker,
K2 volume: the Shift layer's starting entries (`modifiers_init`), visible and editable; the Mod layer starts empty. Shift and Mod both held: the Mod layer wins. Mod is a key function (`ACT_MOD`,
"Mod" in the KEYS tab), on no built-in layout. Learn: **Shift + Mod + turn a knob** gives it the parameter under the cursor (a page row a knob may drive, or a
live GENERAL setting) for the layer the tab shows; or the tab's *Learn* button: the menu closes and the next push (Latch / Select) on a row assigns it, Back cancels.
On the tab a key press or a turn of a column / right knob selects that control (with Shift or Mod held: that layer too). A learned module parameter goes back to
Default when its module is deleted. Saved in `ui.cfg`, only what differs from the starting entries (`modifiers` then lines like `shift k3 param mod 1 0`,
`mod enca act pages`, `shift joyu act default`); the
first format's `shift N cfg <setting>` lines are still read.

**Mappings and macros (`core/curves.h`, `scr_macros.c`).** A Param entry and every macro destination is a `mapping_t`: target + Min / Max (% of the target's range, Min > Max inverts) + a curve (Lin, Exp, Log, S: 33-point tables read with linear interpolation). Edited in the MODIFIERS tab (Min, Max, Curve rows) and the MACROS tab (Macro, Dest, Tgt, Min, Max, Curve, Learn, Remove; the picture is the response). The catch of a mapped col knob works in knob position: caught when the knob's output lands on the current step, or once it crosses the first position that maps to the current value; a macro's catch follows its first destination. An encoder steps the parameter itself (the mapping is not used). ui.cfg: `macros` then `macro K <target> [range MIN MAX] [curve NAME]` per destination; a file without the `macros` line keeps the default macros.

**User curves (CURVES tab, `scr_curves.c`).** U1..U8, each 2..16 points (X / Y 0..100); every point says whether the segment after it is linear or stepped (held until the next point). Turned into a 129-entry table on every edit (a step is one table step wide). Rows: Curve, New, Point, X, Y, Seg, Add, Del pt, Delete; the knobs `CURVES_KNOB_X / _Y` (bindings.h, default column knobs 3 / 4; R1 / R2 on the simulator are the other choice) move the selected point absolutely. Saved in `system/config/curves.cfg` (`curve K` then `point X Y lin|step`), 2 s after an edit like ui.cfg, loaded before ui.cfg (its mappings name the curves). A mapping whose curve was deleted reads it as linear.

**Key LED colours (LEDS tab, `core/led_roles.h`, `scr_leds.c`, `key_leds.c`).** Every key has a role from its function: Note, Sharp, Root (C), Shift, Mod, Menu, Back, Play, Octave, Jump, Nav, Pad, Bank, None. A role has an Idle and an Active colour, each one of 16 named colours at 0..100 %; Active = a held note (the note roles), Shift / Mod held, the menu open, the sequencer running, the octave shifted that way, a jump slot saved (Back, Nav, None have none). The defaults are the first version's colours. Rows: Role, Idle, Idle %, Active, Act %, Reset; the picture fills the keys of that role; while the tab is open those keys light in the Active colour. Saved in ui.cfg (`leds` then `led <role> <colour> <pct> <colour> <pct>` for the roles that differ). The simulator's panel does not draw the LEDs yet: judge on the board. `controls_present()` (`core/controls.h`) tells which non-key controls the board has.

**The Pads section (`core/pads.h`, `scr_pads.c`, `app/app_pads.c`; ADR-041 decisions 14-26).** **Shift + Menu** switches Synth <-> Pads (also the key function "Sectn"). In Pads the synth is unloaded (its notes and sequencer stop) and the engine runs the `Pads` module (8 voices, `sampler_modules.cpp`); the keys take the built-in Pads layout: the function row as always, the left 4 x 4 block = pads 1..16 of the bank shown (top left = 1), the right block = Bank A-D, then Select, Mute, -, Stop all (roles Pad / Bank for the LEDs: an empty pad is dark, a muted one red). Shift + pad = select without sound, Shift + Back = Stop all (for the 4-column board). The section has its own sequencer ("The Pads section's sequencer" below): Select + Play or Shift + Play = step mode, Select + Rec (or the STEPS screen's Rec field) = its live record. **The simulator shows only the 4 left key columns: no Select, Mute, Rec or Bank keys there, so whatever those keys do must also be reachable with Shift or from the screen.** Screen: the grid (joystick / encoder A = selection, encoder B or Page = bank, push = the pad's editor); the editor, pages PLAY / TONE / CHOP from its header (left / right on the header; the edited pad = the pad key pushed). PLAY (overview on top: the trim, the tail as a fade after End, the loop bracket with its crossfade; Smp, then Mode Strt End Loop (Off / Trim / File: a file with loop points sets File when assigned) LStr XFad XPos Tail | Grid Sync From Snap Next Chok Mute, the loop rows only while the pad loops); TONE: Lvl Pan Tune Fine | Cut Res and the filter's response; EDIT: Op (Normalize, Gain, Fade in / out, Trim silence, Crop = the pad's Start..End as a file, Reverse, Remove DC; Stretch, Pitch, Speed, Fit beats, Fit speed: the pad's loop or Start..End to N beats at the clock's tempo, the trim points scaled with the result; an Algo row Beat / Tonal for Stretch, Pitch, Fit beats; Paulstr.; Lo-fi rate, Bits, Drive, Low-pass, High-pass), its amount, Apply (a new version file `name~n` written by the card task), Ver / Undo / Redo over the version history (`history.cfg`; the last 8 versions and what the pads use are kept); Page - / + switches to the CHOP page: Mode Equal / Trans / Grid / Manual, N / Sens / Div / Cut, Pos, To, Add, Del, Chop to pads (the transient scan is the sample bank's `scan`); trims in frames, 1/200 of the file per step; the header steps the pad; Back = the grid). Column knobs = Start, End, Tune, Cut of the selected pad. Saved in `system/config/pads.cfg` 2 s after a change (one line per pad that differs from the defaults, the file name last). Menu alone does nothing in Pads (the shared settings page is phase 6). **The clock** (ADR-041 decisions 27-29): one tempo, the sequencer's BPM; Play in Pads starts / stops it (bar 1 at a start), in the synth Play is the sequencer and the clock runs with it; the grid header shows the tempo and bar.beat; pad row Grid = Off / Beat / Bar (block-accurate start); key functions Tap, Click (metronome), BPM -1 / +1 / -5 / +5 (the Pads layout's right block, row 3: Tap Click BPM-1 BPM+1), knob / encoder function Tempo. The engine side is `Transport` (`engine/core/module.h`, `Engine::set_transport`). **FX chains** (decisions 23, 47-49): Menu in the Pads section opens the FX page, the RACK page (`scr_rack.c`) on `pads_t.fx`, a `rack_t` with `fx` set whose rows are the chains FXA / FXB / FXC (4 effect modules each, `rack_can_insert`); the header is the routing (`fx_route`) and the estimated load (`rack_fx_load_pct`); Edit in a module's menu shows its values. A pad enters at its Bus (PLAY page). The engine side is `pads_graph_build` (rack_graph.cpp) and `engine_synth_pads_fx`; the chains are saved in `padsfx.cfg`. **Capture** (decision 50): the Rec key (`ACT_PAD_REC`, `app/app_capture.c`: `app_capture_key` / `app_capture_poll`) records what the master output receives, before the volume, mono, straight onto the card (`engine/sampler/capture.*` = the tap the audio thread feeds, `platform/engine/capture_job.cpp` = the card job) and puts the new sample on an empty pad; Shift + Rec sets a length in bars.

Requirements: MSYS2 UCRT64 (`C:\msys64\ucrt64`) with gcc, g++ and SDL2, and Python 3 for the generator tools.
`lib/` is not versioned. `lib/u8g2` (https://github.com/olikraus/u8g2, needs `csrc/` and `sys/sdl/common/`) is required; `lib/minimp3` (`minimp3.h`, `minimp3_ex.h` from
https://github.com/lieff/minimp3, public domain) enables the mp3 import of the simulator's sample folder (optional);
`lib/amy` (`git clone --depth 1 https://github.com/shorepine/amy.git lib/amy`) is only read by `apps/ritual_machine/tools/gen_dx7.py` to
regenerate the DX7 tables.

**The sound is persistent, and there are presets (`core/patch.h`, `app/app_preset.c`, `scr_preset.c`; user decisions 2026-10-11).** What plays is a
working copy: 2 s after its last change it is written to `system/config/synth.cfg` (with the name of the preset it came from) and it
is read at power-on, so the machine comes back as it was left. A preset is a text file `presets/<name>.preset` (sub-folders allowed): the rack, the
voice settings, the amp envelope, the Strings and FM settings, the motion sequences, a sampler's file by name, **the macros and the joystick's XY
bindings** (they point at that rack's modules; they left ui.cfg). Not in it: the machine's settings (volume, output, speaker, knob mode: ui.cfg, where
the three first are saved now; key layout; LED colours; the Shift / Mod layers, whose Param entries are pruned when their module is gone) and the
sequencers (their patterns have their own files: `seq.cfg`, `pattern1..8.cfg`, `padseq.cfg`, `padpat1..8.cfg`; ADR-042). **Menu > PRESETS** (second tab): Load opens the browser on the presets folder (the sample browser, `BROWSE_PRESET`; the listing is
`audio_dir_open(":<folder>")`): the highlighted preset is put in place once the highlight rests, so the keys play it; a push keeps it, Back reads the
working state again. Save writes over the preset the sound came from (asks first), Save as makes `Preset N` next to it (no naming on the machine:
rename and sort the files on a computer, through the card reader), Init starts a new sound (asks first). The tab's box names the preset and says
`edited` or `saved`. Nothing but Save / Save as ever writes a preset. A text that does not fit the 4 KB settings buffer is refused with a message
(the start sound is about 1 KB; 16 modules with 64 macro destinations would not fit). Dev: a serial `patch ...` command changes the working copy
like any edit, so it is saved and comes back at the next start (`patch startup` puts the start sound back).

## Folder map

The Ritual Machine's folder (the whole repo, and the engine's place in it: the section at the top). The `engine/` entry below is
`framework/engine`, the library; everything else is under `apps/ritual_machine/`:

```
apps/ritual_machine/
├─ build.ps1             desktop build (per-file objects, dependency tracking). Params: -Platform sim (default), -Clean
├─ platformio.ini        ESP32 firmware build (Waveshare ESP32-S3-Pico; runs on the first prototype), flags documented inside
├─ samples_src/          YOUR .wav / .mp3 files: build.ps1 converts the new ones to samples/*.smp (README.txt inside)
├─ tests/engine/, ui/    the app's tests: the engine driven through the rack, the pads and the sequencers; the UI (see "Testing")
├─ tools/                generators, measuring tools and the serial test tool for the board (see "Tools")
└─ src/                  (+ the library's engine, drawn here where the code includes it from)
   ├─ core/              PORTABLE logic the application is built from (the UI, the key layouts, the data models), plain C. No Windows/SDL/Arduino/engine includes here, and nothing from app/.
   │  ├─ bindings.c/.h      THE table that links hardware controls to actions (the one place to change what a button does), except the matrix keys:
   │  ├─ keymap.c/.h        the key layouts (3 built in + User, and the Pads section's), the functions a key can take, keys.cfg on the card
   │  ├─ patch.c/.h         a sound as text (the rack, the voice settings, envelopes, FM / Strings, the macros and the joystick's bindings): the working state synth.cfg and the presets; its line format is in patch.h
   │  ├─ pads.c/.h          the Pads section's model (ADR-041): 4 banks x 16 pads, their settings, pads.cfg on the card; scr_pads.c = its grid and pad editor
   │  ├─ pad_seq.c/.h       the Pads section's sequencer model (ADR-042): a pattern as a list of bit-field hits, the bank, their text
   │  ├─ history.c/.h       the version history of the library's samples (the pads' EDIT page): names, parents, the pad's trim points per version, trimming; history.cfg
   │  ├─ card_layout.h/.c, storage_buf.c   the card's folders (handed to the storage HAL at start) and the one text buffer every settings file goes through (`g_storage_buf`)
   │  ├─ audio.h            the application's calls into its engine: build, notes, sequencers, pads, the sample library, the card's state (implemented per platform in platform/*/audio_*.c*, forwarded to platform/engine)
   │  ├─ synth_ui.h         public interface of the UI (cursor state, ui_event_t, knob / macro calls)
   │  ├─ ui_pages.c         what is on screen: page tables, the page list generated from the rack, menu tabs
   │  ├─ ui_input.c         what events do: row editing, menu tabs, knobs, macros, synth_ui_handle()
   │  ├─ ui_graphs.c        the small pictures of the graph box (waveform, envelope, filter, effects...)
   │  ├─ ui_draw.c          the screens: header, lists, hand-drawn screens, synth_ui_draw()
   │  ├─ ui_internal.h      what those four share (private to the UI)
   │  ├─ ui_screen.c/.h     declarative screens: element tables, focus navigation, latch, drawing (see human_docs/UI_GUIDE.md section 8)
   │  ├─ scr_*.c            the menu tabs as declarative screens: rack, preset (PRESETS: Load / Save / Save as / Init), general, samples, fx, fm (ALGORITHM: the operator tree), keys, mods (MODIFIERS)
   │  ├─ rack.c/.h          modular synth model: slots, audio chain rules, module parameters, modulator targets
   │  ├─ synth_config.c/.h  general settings (synth type, voices, volume, the FM patch copy); the effects are row M of the rack (ADR-041)
   │  ├─ dx7.c/.h           editable 6-operator FM patch + editing helpers
   │  ├─ dx7_factory.c      GENERATED: the 128 factory DX7 patches as dx7_patch_t
   │  ├─ dx7_algos.c/.h     GENERATED: 32 algorithm diagrams (+ operator box positions)
   │  ├─ seq.c/.h           the step sequencer's pattern and settings (pure data; it plays in the engine: engine/core/sequencer.*)
   │  ├─ synth_params.c/.h  global amp envelope (AMP ENV page) + the generic LIN/LOG/ENUM parameter table
   │  └─ sprites.h, module_sprites.h (GENERATED)   1-bit sprites
   ├─ app/               THE APPLICATION, portable plain C like core/: glues input -> bindings -> UI -> data -> audio. Only the platform's main loop and the dev tools include it.
   │  ├─ app.c/.h           app_t, the start, one frame, and the step the main loop calls (it owns the redraw policy)
   │  ├─ app_internal.h     PRIVATE: what the files of this folder share, and the list of them
   │  ├─ app_input.c        where an input event goes: the matrix keys (key layout), the other controls (binding table), the joystick as buttons or XY
   │  ├─ app_layers.c       the Shift / Mod layers, Learn, the catch of the column knobs
   │  ├─ app_actions.c      `app_run_action`: what an action does; UI events, the keyboard's notes (octave, latch), the tempo
   │  ├─ app_pads.c         the Pads section: the switch Synth <-> Pads, the pad keys, its knobs, the chop to pads
   │  ├─ app_library.c      the samples in use (the catalog) and the sample browser
   │  ├─ app_render.c       a pad's sample processed into a new version (the EDIT page)
   │  ├─ app_capture.c      the Rec key: what plays, recorded onto a pad
   │  ├─ app_seq.c          the synth's sequencer: its patterns and chain handed to the engine, their files, recording from the keys
   │  ├─ app_pseq.c         the Pads section's sequencer: the same, plus step mode and its live record
   │  ├─ app_card.c         the card: the settings files read and saved by themselves, the card's events as popups
   │  ├─ app_power.c        the battery (`hal_power.h`): a note when it gets low, a message to confirm when it is almost empty
   │  ├─ app_preset.c       the sound's working state (synth.cfg: written 2 s after the last change, read at power-on) and the presets (the PRESETS tab's requests, the browser that tries a preset before it is kept)
   │  └─ key_leds.c/.h      the LEDs under the keys, from the key layout and the application's state
   ├─ hal/, ui/          (= framework/hal, framework/ui, the library) the INTERFACES the core uses, one implementation per board (display, input: the board's physical controls, LEDs, storage: small files on the card, power: the battery's level, mem: large application data, audio_out: the output that calls the app for sound), and the drawing kit (gui.c/.h: style sheet + layout / text / sprite / animation helpers over u8g2; popup)
   ├─ engine/            (= framework/engine, the library) THE DSP ENGINE, portable C++17 (no exceptions, no RTTI, no heap after init; sees no core/ or hal/)
   │  ├─ dsp/               q15/q31 math, tables (GENERATED), phase/pitch, oscillators, SVF, FFT/STFT, delay, smoothing, CORDIC
   │  ├─ core/              module API, graph description, plan compiler, engine (voices, command queue), heap, sequencer (the two step sequencers: notes, pads)
   │  ├─ modules/           builtin, synth (Osc Env Lfo Filter Vca Mix Mult Shaper Const), fx (Delay Spectral Vocoder Chorus
   │  │                     Reverb), dx7_voice (FM), sampler_modules (Sampler Granular Pads), strings_modules (Strings voice, Ensemble), para_modules (GateIn, ParaGate: the Para switch)
   │  ├─ offline/           sample processing off the audio thread, block by block (process.*: normalize, gain, fades, trim, crop, reverse, DC, pad; stretch (WSOLA or phase vocoder), resample, pitch, paulstretch; lo-fi rate, bits, drive, filters; rfft.*: the 4096-point float FFT of the spectral ones)
   │  └─ sampler/           .smp format, storage interface, sample bank, streaming loader
   └─ platform/          THE APP'S OWN SIDE of each platform (the boards themselves are the library's: framework/boards)
      ├─ engine/            shared by desktop and ESP32: rack -> graph mapper, DX7 patch conversion, the C API
      │                     (engine_synth.h) core/audio.h is forwarded to; render_job.cpp = a sample processed into a new file on the card
      ├─ sim/               desktop: main, the engine's memory and render function, the sample library on a SimStorage (framework/host/sim_storage.h: a slow-card model, also used by the tests) (#ifdef PLATFORM_SIM)
      └─ esp32/             the first prototype: setup / loop, the engine's memory, render function and profiling lines, the sample library on the card task's hooks, dev serial commands,
                            boot benchmark, the card reader's firmware (#if defined(ARDUINO_ARCH_ESP32); see "ESP32 firmware and performance work")
```

### The rules that keep it portable

1. `core/` only includes `hal/*.h`, `ui/*.h`, `u8g2.h` and the C standard library; `app/` the same plus `core/*.h`. `core/` includes nothing from `app/`. Anything platform-specific goes behind a HAL function. The library's `hal/`, `ui/` and `boards/` include nothing from an app: what a board needs from one is handed over (the render function, the card's folder list, the boot animation's frames, the card task's hooks).
2. `engine/` (the library, `framework/engine`) includes nothing from `core/`, `hal/` or the platform, nor from any app. `platform/engine/` is the only bridge between the two.
3. Every file under `platform/<x>/` and `framework/boards/<x>/` is wrapped in its guard, because PlatformIO compiles all of the app's `src/` and of the board's folder.
4. The display is created by the board (`display_init()`), then handed to the app: `app_init(&app, display_init())`.
5. HAL headers have `extern "C"`, so C core code links from C++ files. `core/*.c` and `app/*.c` stay C (they use C designated initializers).
6. In the engine, nothing on the audio path allocates, locks, logs or uses `float` (see the guidelines in ENGINE_DESIGN.md).

## Architecture in one page

```
UI thread (main loop)                                        audio thread (SDL callback / I2S task)
input_poll() -> app_step() -> bindings[] -> app_run_action() -> synth_ui_handle()   edits the data below
                  |                |
                  |                +--> returns true when a sound value changed -> audio_set_params(rack, params)
                  |                +--> ui.rebuild set (menu closed with structural edits) -> audio_build(rack, params)
                  +--> audio_seq_set(seq), audio_set_clock(bpm, running)   the sequencer plays there ---> Engine's Sequencer: its notes, on the clock
                  +--> audio_seq_pos() -> seq.pos (the playhead on the screen)
                  +--> synth_ui_draw() (only when something visible changed)

audio_build / audio_set_params  ->  engine_synth_*  ->  rack_graph_build()  ->  Engine::load()  (atomic plan swap)
audio_note_*                    ->  Engine::note_on/off, set_param, set_blob  -> lock-free command ring -> engine_synth_render()
                                                                                  drains it at the start of every block
```

The data the UI edits (all fixed-size, no malloc):

| Data | Where | What it holds |
| --- | --- | --- |
| `rack_t` | `core/rack.h` | the modular synth: up to 10 slots (the rack strip scrolls, 8 cells visible) (module type, id, own parameters `v[]`, modulator target) and `cfg` |
| `synth_config_t` (`rack.cfg`) | `core/synth_config.h` | synth type (Modular / FM / Strings), voices (1 = mono), master volume, the FM patch being edited (`dx7_patch_t fm`) |
| `synth_params_t` | `core/synth_params.h` | the global amp envelope (end of the modular voice) |
| `seq_t` | `core/seq.h` | pattern notes/lengths + BPM, steps, transpose, swing, held notes |
| `synth_ui_t` | `core/synth_ui.h` | cursor state, the generated page list, menu tab, FM editor selection |

Every parameter set has the same shape: `*_adjust(dir)` (clamps, returns whether it changed), `*_label`, `*_format`.
That is what lets one generic list renderer show any of them.

### Screens

- **Main view**: a list of pages (Left/Right on row 0). Pages are **generated** from the data
  (`synth_ui_rebuild_pages`): in Modular mode every rack module contributes its own pages (OC: `OSC n` + `OSC n TUNE` + `OSC n DEST`; every modulator (OC, LFO, ENV, EG) has a `... DEST` page with Tgt / Prm / Dpth, same as the RACK tab; FL: `FILTER n` + `FILT n ENV`; SA: `SAT n`; LFO: `LFO n`; ENV: `ENV n` + `ENV n CRV` (curves, hold, start); EG: `EG n` (the selected point) + `EG n REL`; RS: `RES n`; FL also has `FILT n CRV`; the global `AMP ENV` is followed by `AMP CURVE`;
  MS: `MOTION n` + `MS n LANE`; SM: `SMP n`, `SMP n LOOP`, `SMP n SLICE`; VL (the note's velocity as a source): `VEL n`)
  followed by `AMP ENV`, `SEQUENCER`, `SEQ SETUP`, `PATTERNS`. In FM mode the pages are `FM SYNTH` (Patch, Vol), `SEQUENCER`, `SEQ SETUP`, `PATTERNS`.
- **GENERAL tab**: **Type** = Modular, FM, Strings (ADR-041; a copy of every voice module per voice), Patch (FM), Voices (**1 = mono**: one real voice, last-note priority; Modular / FM), **Glide** and **Legato** (shown with Voices 1), Vol, Out, **Spk** (built-in loudspeaker: Off, 5..100 % in 5 % steps; ESP32 only, `audio_esp32.cpp`: the MAX98357A plays the left DAC channel (`kSpeakerCh`), so the level scales that channel; on the prototype the headphones do not follow it. Off = gain 0 on that channel; it also pulls `PIN_SPK_SD` (GPIO 5) low, but that alone did not silence the speaker (2026-10-05): GPIO 5 is not proven to reach the amplifier's SD_MODE, `[SPK]` on the serial log prints what the pin reads). Only the rows that apply are shown. The default is Modular with 1 voice (mono, ADR-036); the voice count is real: `Engine::load(graph, nvoices)`. Test with `synth_type_is_fm()` / `synth_type_is_rack()` / `synth_config_is_mono()`. Paraphonic is the Para switch on a rack module (RACK tab).
- **Menu** (Enter): tabs switched with Left/Right on row 0. Modular: `RACK` (branches 1 and 2, row M = the effects on their sum), `GENERAL`, `SAMPLES` (a file for a rack sampler; its File row opens the sample browser, `scr_browse.c`), `KEYS` ...
  FM: `RACK` (row M only), `GENERAL`, `ALGORITHM` (Algo, Fb, Op: the operator tree; a push on Op opens that operator's page), `KEYS`, `MODIFIERS` ... Row M's effects have their pages after the FM / Strings pages. The FM patch is edited on main-view pages like a module's: FM SYNTH (Patch, Algo, Fb, Vol), then per operator OPn (Lvl, Crs, Fine, Fix) and OPn ENV (Pt, Lvl, Time); every row but Patch / Pt is a knob row and can be learned (`MACRO_FM` targets). The FM values are driven by the knobs exactly (`dx7_value_get / set`, 0..1): reading one for the catch never changes it. Closing the menu with structural edits
  regenerates the pages and rebuilds the graph. Effect edits are live (no rebuild).
- **Popups** (`framework/ui/popup.h`, `app->popup`): a box over any screen. `popup_info` (goes after a time, input passes through: the Shift-knob value),
  `popup_error` (OK) and `popup_ask` (Yes / No, answer by callback) are modal and queued (4); while one is up `app_run_action` hands it the actions
  (Nav left / right, Value, Row: the button; Latch / Select: press; Back: No), notes / Shift / volume still act, the rest is ignored.
  `synth_ui_draw` no longer sends the buffer: `app_draw_frame` (`app/app.c`) draws the screen, the popup on top, then `display_send`. Dev tests: serial `popup info|error|ask`, `ui_dump popup:ask`.

## Where to make changes

### Add / change a module parameter (modular synth)

1. `core/rack.h`: add an index to the module's `MP_*` enum (`MOD_PARAM_MAX` = 8 values per module).
2. `core/rack.c`: add a row to the module's descriptor table (label, LIN/LOG/ENUM, range, step, default, unit, decimals).
   Defaults are applied by `rack_insert`.
3. `core/ui_pages.c`: put the index in one of the module's page definitions (`*_pages[]`, max 4 rows).
4. `platform/engine/rack_graph.cpp`: map the value onto the engine module's parameter (units in `modules/synth_modules.h`).

### Add a module type

`module_type_t` + the `info[]` row in `rack.c` (name, code, audio/modulator flag, target parameters, own parameters), a line
in `apps/ritual_machine/tools/gen_module_sprites.py` (then run it), the sprite table `module_sprite()` in `ui_draw.c`, page definitions, and the
branch in `rack_graph_build()` that creates the engine nodes and cables for it. A new *engine* module is a class in
`framework/engine/modules/` (see below) plus its `ModuleInfo`, registration and a test.

### Add a page or a graph

Module pages: extend the `*_pages[]` tables in `ui_pages.c`. Global pages: `global_pages[]` and the `modular_globals` /
`fm_globals` lists. A new graph type = a `GRAPH_*` value, a `draw_*` function (the graph box is `gui_default_style.graph`)
and a `case` in `synth_ui_draw()`.

### Add a general setting or an effect

`core/synth_config.[ch]`: general values have a `cfg_param_id_t` and cases in `synth_config_adjust/format/label`; show it in the GENERAL tab
and use it in `rack_graph.cpp`. Return `CFG_LIVE` for values applied immediately, `CFG_REBUILD` for structural ones. An effect is a rack module
(ADR-041): a type in `rack.h` (appended; `apps/ritual_machine/tools/gen_module_sprites.py` and `make_placeholder_sprites.py` follow the order), its parameter table
and `info[]` row in `rack.c`, a page in `ui_pages.c`, and its engine node in `fx_type` / `fx_params` of `rack_graph.cpp` (a cheap effect also needs a per-voice mono
version: the `template <int CH>` pattern of `fx2_modules.cpp`).

### Write an engine module

Derive from `sc::Module` (`engine/core/module.h`): `info()` returns a static `ModuleInfo` (scope voice/global, ports,
parameters with defaults); `init(Memory&)` allocates buffers (`mem.fast` for hot state, `mem.bulk` for long lines) and the
destructor frees them; `reset()` clears state; `set_param(idx, value)` takes int32 in the module's own units (pitch values
are MIDI x 256, times in ms or 1/16 ms, levels q15); `process(ctx, ports)` renders `kBlock` frames of q15 from `in[]`
(always valid) and `mod[]` (null when no cable is connected; a bipolar q15 signal otherwise), writing `out[]`. Register it
(`Registry::add(id, type_of<T>())`), then test it against a float reference (`framework/tests/rig.h` has the helpers).

### Sequencer (`core/seq.c` the model, `engine/core/sequencer.cpp` the player, `app/app_seq.c`; ENGINE_DESIGN.md ADR-042)

`seq_t` is the pattern that plays and is edited: 64 steps (`seq_step_t`: up to 4 notes = a chord, Len in steps, Vel, Prob, Rtch = hits
inside the step) and the settings of page `SEQ SETUP`: BPM, Steps (1..64), Trsp (transpose), Swing (% delay of every second step),
Rate (a step = 1/8, 1/8T, 1/16, 1/16T, 1/32), Dir (Fwd / Rev / Ping / Rand), edited through `seq_param_adjust/label/format` (table in
`seq.c`; the `SQP_*` numbers are saved in ui.cfg: append only). **It holds no timing.** The engine's `Sequencer` plays it on the
machine clock in the audio thread: every step `app_seq_step()` calls `audio_seq_set(&seq)` (only what changed is sent) and THEN
`audio_set_clock(bpm, running)`; `app_step()` reads `audio_seq_pos()` (the step that plays, -1 = none) into `seq.pos` for the screens.
A note longer than a step rings over the following ones (16 held at most; the same pitch is retriggered); stopping releases everything.

- **Page `SEQUENCER`** (`draw_seq` / `handle_seq`, rows `SEQ_ROW_*`): Step, Note, Len, Vel, Prob, Rtch, Voice (which note of the chord
  the Note row edits), Run, Tool, On. The roll shows the 16 steps around the cursor and two octaves (it follows the page's lowest note
  and the edited one); a step with Prob < 100 is dotted, a ratchet cuts its first column. The motion pages keep 16 steps
  (`seq_motion_steps`).
- **Tools** (`seq_tool_apply`, core/seq.c): the Tool row picks one, On its scope (Step / Page / All), a push runs it; the UI leaves
  what it did in `ui->seq_msg` and the app shows it as a popup. One clipboard, one level of undo.
- **Knobs**: on that page the column knobs set Note, Len, Vel, Prob of the cursor's step (`seq_knob`, `seq_set_norm`), each only
  after it moved `SEQ_KNOB_HYST` from where it stood when the cursor came to the step.
- **Recording** (`app_seq.c`): Shift + Play or the key function Rec arms it; stopped = step entry at the cursor, running = live to
  the nearest step (`audio_seq_where`: step, fraction, count), written once the playhead is there.
- **Patterns and chain**: `seq_bank_t` (8 `seq_pattern_t`, the chain) is the app's; `seq_bank_select` stores `seq_t` and takes
  another. Page `PATTERNS` (`handle_seq_pat`, rows `PAT_ROW_*`). While it runs the next pattern is handed to the engine ahead
  (`audio_seq_queue`), which goes on to it at the pattern's end and says so (`audio_seq_taken`, called before `audio_seq_set`).
  Files: `system/config/seq.cfg` + `pattern1.cfg` .. `pattern8.cfg` (`seq_pattern_to_text`), autosaved by `app_seq_autosave`,
  read by `app_seq_load` at start and when the card shows up. The sound's `synth.cfg` and the presets hold no sequencer.
- **Velocity** reaches a Modular sound through the rack module VL (`MOD_VEL`: Tgt / Prm / Dpth; `rack_graph.cpp`: a cable from the
  note's velocity, no node). FM and Strings use velocity by themselves.

**The Pads section's sequencer** (`core/pad_seq.h`, `app/app_pseq.c`, the STEPS screen of `scr_pads.c`; ADR-042 decisions 16-20): a
pattern is a list of hits (`pseq_hit_t`, a 32-bit bit-field: pad, step, level, chance, ratchet, length; `PSEQ_HITS` 160), edited in
place in `app->pbank.pat[cur]` (bulk memory, `hal/hal_mem.h`). It plays in the engine's `PadSequencer` while the Pads clock runs and
the Seq field is On: `app_pseq_step()` (called BEFORE `app_seq_step()`, which sets the clock) hands it over with `audio_pseq_set`;
patterns, chain and the queued pattern work as the synth's. **Select + Play** (or Shift + Play: no Select key in the simulator) = step mode (`ui->pad_steps`: the pad keys are the
selected pad's steps, `app_pseq_step_key`; `key_leds.c` lights them), **Select + Rec** (or the STEPS screen's Rec field) = live record. Files: `padseq.cfg`,
`padpat1.cfg` .. Tests: `apps/ritual_machine/tests/ui/test_pseq.c`, `apps/ritual_machine/tests/engine/test_pads.cpp` (the last two).

To add a per-step value: `seq_step_t` + `SeqStep` (engine) + the conversion `seq_steps_of` in `engine_synth.cpp` + the `step` line of
`seq_pattern_to_text / _from_text` + a row. Tests: `apps/ritual_machine/tests/engine/test_seq.cpp`, `apps/ritual_machine/tests/ui/test_seq*.c`, `apps/ritual_machine/tests/ui/test_vel.c`.

### GUI helpers (`framework/ui/gui.h`)

`gui_style_t` (font, margin, padding, gap, list column / graph box geometry, piano-roll and rack metrics; default in
`gui_default_style`, `framework/ui/gui.c`) feeds the helpers: rect layout (`gui_inset`, `gui_center`, `gui_below/above/right_of/left_of`,
`gui_take_top/left`, `gui_grid_cell`, `gui_center_box`), text (`gui_text_w/h`, `gui_row_h`, `gui_text_center`,
`gui_draw_text_centered/left/right`, `gui_draw_field`) and sprites. A sprite is a `gui_sprite_t {w, h, bytes, frames}`: 8 pixels
per byte, MSB = leftmost, rows padded to whole bytes; add small ones to `core/sprites.h`.

Animations: a sprite can be a sheet (`frames` images stacked vertically, e.g. `spr_eq`).
`gui_anim_add(&sprite, first, last, frame_ms, loop)` (or `gui_anim_add_total(...)`) returns an id; `first > last` plays
backwards. Call `gui_anim_tick(now_ms)` from the main loop (true = a frame changed, redraw), draw with
`gui_anim_draw[_centered|_right]`, and delete with `gui_anim_remove(id)` (also `gui_anim_clear`, `_restart`, `_finished`).
A non-looping animation stays on its last frame until removed. Up to `GUI_ANIM_MAX` at once. `app_step()` is the reference.

### Change what a control does, or add a control

Input has four layers, so each change has one home:

1. **The board** (`hal/hal_input.h`, one `input_poll()` per board): reports ITS OWN controls only, as `{control id, kind, value}`. Each board lists its
   controls in a table (`input_controls()`: name, kind = key / button / encoder / endless pot / knob / axis, place in the key matrix, place on the panel
   (the simulator draws the board from it), LED); a control's id is its index there and has a name in the board's header (`boards/hwv1/hwv1_controls.h`: `HWV1_KEY(row, col)`, `HWV1_POT_0..6`,
   `HWV1_JOY_X`...; `boards/desktop/panel_controls.h`: `PANEL_ENC_A`, `PANEL_KEY(row, col)`...). It knows nothing about the application.
2. **Roles and the board's map** (`core/controls.h`, `core/controls.c`, `core/boards/map_<board>.h`): the Ritual Machine's own control list, `control_id_t`
   (`CTL_ENC_A` `IN_DELTA` (detents), `CTL_KNOB_R1` `IN_VALUE` (0..1023), `CTL_KEY(row, col)` `IN_PRESS` / `IN_RELEASE`...), which everything below and
   the saved files (`ui.cfg`, `keys.cfg`) are written against. A map says which board control plays which role, one row each: `{HWV1_POT_3,
   CTL_COL_KNOB_1, IN_VALUE, 512}` (an endless pot plays a knob role as a position that starts at 512; `IN_DELTA` = in steps, for an encoder role). Matrix
   keys need no row: a key takes the key role of its place. `controls_init()` binds the board the program runs on (the map is picked by
   `input_board_name()`), the entry points pass every event through `controls_event()` (board id -> role), `controls_led()` lights a role's LED,
   `controls_key_present()` / `controls_present()` say which roles the board plays (the KEYS and MODIFIERS tabs list only those). A board control without a
   role does nothing. The UI tests send role events straight to the app (`tools/ui_stubs.c` answers for the board); `tests/ui/test_controls.c` checks the two maps.
3. **Bindings** (`core/bindings.c`): the table `{control, event kind, modifier state, action, argument}`. Rebinding = editing a row, e.g. to make
   encoder A change pages instead of rows: `{CTL_ENC_A, IN_DELTA, MODS_NONE, ACT_PAGE_MOVE, +1}`. A control can have several rows; Shift (hold-action on
   button 3) selects rows by `MODS_NONE` / `MODS_SHIFT` / `MODS_ANY`. The list of actions and their arguments is in `core/bindings.h`.
4. **Actions** (`app_run_action` in `app/app_actions.c`): what an action does, mostly by sending a `ui_event_t` (`UI_UP`, `UI_PAGE_NEXT`, `UI_SELECT`... in
   `core/synth_ui.h`) to `synth_ui_handle`, or by playing a note, changing the octave, or setting a knob value.

Recipes:
- *Move a function to another button*: change `ctl` in its row of `bindings[]`. No other file changes. **Matrix keys** are not in `bindings[]`: their functions come
  from the key layout (`core/keymap.c`: `preset()` builds the three built-in layouts, `fns[]` lists the functions a key can take; the user edits them in the KEYS tab).
  `app/app_input.c` (`key_event`) runs a key's function through the same `app_run_action`.
- *New action*: a value in `action_id_t` + its name in `action_names` (`bindings.c`) + a `case` in `app_run_action`; then use it in the table. Actions that need the
  release of their control (notes, Shift) are listed in `action_is_hold`.
- *Another board control plays a role* (the prototype's pots swapped, a pot read in steps instead of a position): the row in the board's map,
  `core/boards/map_hwv1.h`. No other file changes.
- *New physical control on a board*: an id in the board's header and a line in its table at the same place (`hwv1_controls.h` + `hwv1_controls.c`, or
  `panel_controls.h` + `panel_controls.c`), the driver code that reports it (`input_esp32.cpp`, pins in `board_pins.h`; on the desktop nothing more: the panel window
  draws the table's line at its `px`, `py` and `input_sim.c` gives it a PC key by its kind), then a row in the app's map of that board.
- *New role*: a value in `control_id_t` (`core/controls.h`, before `CTL_HW_COUNT`), a name in `control_name` (and in `others[]` of `modifiers.c` if the
  layers may use it), a row in `bindings[]`, and a row in each board's map that has a control for it.
- *A new board*: its folder in `framework/boards` with its table and `input_board_name()`, then `core/boards/map_<board>.h` and a line in `maps[]` of
  `core/controls.c`. Without a map the board's matrix keys still play their key roles; nothing else does. To see it on the desktop: its
  branch in `boards/desktop/sim_board.h` and its entry in `Get-SimBoard` (`framework/tools/sim_build.ps1`), then `.\build.ps1 -Board <name>`.
- *Keyboard shortcuts of the simulator*: `build_keys()` of `framework/boards/desktop/input_sim.c` (physical scancodes, given by a part's kind and place).
- *The joystick* is reported as two axes; the core turns deflection into the virtual controls `CTL_JOY_LEFT / RIGHT / UP / DOWN` (with key repeat, thresholds in
  `bindings.h`), so bindings treat it as four buttons.
- *Knobs*: a knob reports a position 0..1023 and a binding gives it an action (`ACT_PAGE_KNOB`, `ACT_MACRO`, `ACT_MASTER_VOLUME`). A parameter only knows
  "one step up / down", so `ui_input.c` (`knob_set`) walks the parameter to both ends and back to the step that matches the position: nothing to add when a new
  parameter appears. Rows that select or cycle (targets, sample file, step editors) are not driven by knobs (`row_is_knobbable`).
  *Catch* (`knob_check_catch`, `synth_ui_catch_refresh` in `ui_input.c`): `knob_catch_dir[k]` is 0 = caught, -1 / +1 = the way to turn (drawn as a 4 px wedge by `ui_draw.c`), 127 = not
  measured yet. A blocked knob is released when it crosses the value (a fast turn skips the exact step). The app sets `app_catch_stale` after manual changes and Shift, and calls
  `synth_ui_catch_refresh` before drawing (`app_catch_poll`, `app/app_layers.c`); `knob_cur[k] == -2` marks "the knob set this value itself" (walking can measure the step one off). The Catch / Direct choice is `cfg.knob_mode`.
  The Shift targets are `ui->knob_shift[]` (`macro_t`, today only `MACRO_GLOBAL` + `cfg_param_id_t`).
- *Test without the window*: `apps/ritual_machine/tools/ui_dump.c` takes control events (`encA:+1`, `shift`, `knob:27:900`, `key:4.0`...), see the top of the file.

### Add a HAL function / a platform

Declare it in `framework/hal/hal_*.h`, implement it in **every** `framework/boards/*/` (and in the test stubs,
`tools/ui_stubs.c`). A new board (Teensy, RP2040...) is a folder `framework/boards/<name>/` with the HAL implementations wrapped in
a guard macro, added to the app's `build_src_filter` (`platformio.ini`), plus the app's own side in
`apps/ritual_machine/src/platform/<name>/`: its `main`, and `core/audio.h` (allocate two memory blocks, call `engine_synth_init`,
hand `engine_synth_render` to `audio_out_start`). The board's audio part is small too: call the render function from an audio
thread / DMA task (`hal/hal_audio_out.h`).

### Change the layout / fonts / palette / screen size

The screen size is one setting: `DISPLAY_WIDTH` / `DISPLAY_HEIGHT` in `hal/hal_display.h` (default 128 x 64; `.\build.ps1 -Clean -Display 128x128` for
the simulator). The application reads the real size from the u8g2 instance and builds its style from it (`gui_style_init(st, w, h)` in `framework/ui/gui.c`, called by
`synth_ui_draw`): the list column takes half the width and the graph box fills the rest, so a taller screen gives taller graphs without touching the drawing
code. Check a size without SDL with `UI_DUMP_SIZE=128x128` and `apps/ritual_machine/tools/ui_dump.c`. A real 128 x 128 design (more rows per page, other fonts) starts from the
style: edit `gui_default_style` / `gui_style_init`. The pixel offsets that remain in `ui_draw.c` are multiples of the row height (`gui_row_h`), so they follow the font.

The style (`gui_default_style` in `framework/ui/gui.c`) holds the font, margin, padding, gap, `list_w` / `list_top`, `graph` (graph box rect), the piano roll and the rack.
Default font is `u8g2_font_5x7_tr`. The FM editor tabs use a compact copy of the style (padding 0, gap 0) so five rows fit. `framework/boards/desktop/display_sim.c` is the
simulator's own display driver (any size, plain monochrome: white on black by default, or black on white with `OLED_SIM_PALETTE=light`).
`OLED_SIM_PANEL_SHOT=path.bmp` saves both windows (the panel to `path.bmp`, the OLED to `path.bmp.oled.bmp`) after a moment: handy to look at the result without a screen.

## The engine in short

Everything is a graph of modules compiled into a flat plan: `NoteIn` per voice, any number of modules joined by **cables with a
signed depth** (any output can drive any input or any parameter; there is no distinction between audio and control, so audio-rate
FM / AM / filter FM are just cables), `VoiceOut` summing the voices, then global modules (effects). All signals are q15 blocks of
`ENGINE_BLOCK` (32) frames at `ENGINE_SR` (48 kHz); state is q31 / Q28 where it matters. Modules in the box:

| Group | Modules |
| --- | --- |
| structure | NoteIn (pitch CV, gate, velocity), VoiceOut (sum, pan, frees silent voices), BusIn, MasterOut |
| synthesis | Osc (sine, PolyBLEP saw / pulse, PolyBLAMP triangle, noise), **OscEngines** (Karplus-Strong, modal, FM2, folder, supersaw, vowel, additive, dust; ADR-033), Env (ADSR + hold / start / curves), **Eg** (4-point envelope), Lfo, Filter (TPT SVF, 12..48 dB/oct, LP/BP/HP/notch), Vca, Mult (ring / AM), Mix4, Shaper (10 modes), Const |
| FM | Dx7 (6 operators, 32 algorithms, feedback, 4-stage envelopes) |
| control | MotionSeq (4 lanes x 16 steps; it follows the engine's step sequencer, `ProcessCtx::seq`; ADR-029, ADR-042); the two step sequencers (notes, pads) are not modules: `engine/core/sequencer.*`, owned by the `Engine` |
| effects | Delay (stereo, Hermite, ping-pong), Chorus (Juno-like modes), Reverb (Dattorro plate), SpectralFx (freeze, gate/filter, robot, whisper, pitch shift), Vocoder, **Phaser, Flanger, Tremolo / Auto-pan, Compressor, EQ3, Ring mod / Shifter, Convolver (cab / body IRs), Comb (tuned resonator, voice scope)** (ADR-034) |
| samples | (rack module SM, ADR-030) Sampler (streaming, pitched, loops, reverse, slices, key/velocity zones), Granular; `.smp` files via `framework/tools/wav2smp.py` |

Rack -> engine mapping and the master chain are described in ENGINE_DESIGN.md (ADR-026, ADR-034); the threading rules in ADR-024; modulation depths in ADR-032.

## Tools (`tools/`)

| Tool | Generates / does | Run |
| --- | --- | --- |
| `gen_module_sprites.py` | `apps/ritual_machine/src/core/module_sprites.h` (module sprites with 2-letter codes) | `python apps/ritual_machine/tools/gen_module_sprites.py` |
| `gen_dx7.py` | `apps/ritual_machine/src/core/dx7_factory.c` and `apps/ritual_machine/src/core/dx7_algos.c` (needs `lib/amy`) | `python apps/ritual_machine/tools/gen_dx7.py` |
| `gen_engine_tables.py` | `framework/engine/dsp/tables.cpp` (sine, exp2, tan, tanh, FFT twiddles, window, CORDIC) | `python framework/tools/gen_engine_tables.py` |
| `gen_wavetables.py` | `framework/engine/dsp/wavetables.cpp`: band-limited saw / triangle tables, 10 octave bands, for the Strings oscillators (41 KB, flash) | `python framework/tools/gen_wavetables.py` |
| `make_demo_samples.py` | five synthetic demo samples into `samples/` (the simulator's card folder) | `python framework/tools/make_demo_samples.py` |
| `smp_convert.cpp` | every `.wav` / `.mp3` of a folder -> `.smp` (new or changed files only; shares `framework/host/sample_convert.h` with the simulator's importer). build.ps1 builds and runs it on `samples_src/` | `build\smp_convert.exe samples_src samples [--force]` |
| `wav2smp.py` | one WAV (8..32 bit, mono/stereo, `smpl` loops) -> cooked `.smp` sample, with `--slices` / `--loop` / `--root` options the converter has no way to give | `python framework/tools/wav2smp.py in.wav out.smp --root 60 --slices 0 12000` |
| `import_boot_anim.py` | a monochrome frame array in a C header (rows of bytes, LSB = left pixel) -> the boot animation's images `assets/UI_Sprites/64/boot_NN.png` (replaces the ones there; `--sheet out.png` = every frame side by side) | `python apps/ritual_machine/tools/import_boot_anim.py path/to/loading7.h` |
| `build_engine_tests.ps1` | builds and runs the app's engine tests (`tests/engine`); `-Matrix` for all configurations, `-Filter name` for a few. The library's own are `framework/tools/build_engine_tests.ps1`; `test.ps1` at the repo root runs both and the UI tests | see the file header |
| `build_ui_tests.ps1` | builds and runs the UI tests (`apps/ritual_machine/tests/ui/*.c`, C: `TEST(name)` in `ui_test.h`; each test starts a fresh app with `ui_fresh()` and sends control events; `ui_stub_ramcard(true)` gives a card in RAM, `ui_board(true)` the prototype's controls); `-Filter name` for a few | see the file header |
| `ui_dump.c` | renders the screens to ASCII without SDL or audio (layout checks); its platform stand-ins are `ui_stubs.c`, shared with the UI tests | command at the top of the file |
| `capture_test.py` | the capture on the prototype (serial `pad rec`): a few seconds recorded while pads in memory, effects and streamed pads play; the frames the card lost, the streams' underruns |
| `padsfx_test.py` | the Pads section's FX chains on the prototype (serial `pad fx`): 8 pads into chains from empty to full, the cycles per block, the per-module costs, the dropouts |
| `edit_test.py` | the sample processing jobs on the prototype (serial `pad edit`): the time and the PSRAM of each kind of operation, and a job while streamed pads play |
| `library_test.py` | the sample library on the prototype (serial `dir`, `use`, `pad`): the time to list the root folder, a header read by the file's place in the folder, different samples cycled on one pad (the catalog and the memory must stay bounded), a streamed pad while a folder is read | `C:/.platformio/penv/Scripts/python.exe apps/ritual_machine/tools/library_test.py --cycle 40 --stream "<a long sample>"` |
| `pads_test.py` | the Pads section on the prototype (the `pad` serial command): 1 / 4 / 8 looping pads, short and streamed, a crossfade, the heaps | `C:/.platformio/penv/Scripts/python.exe apps/ritual_machine/tools/pads_test.py` |
| `serial_test.py` | talks to the prototype over its serial port: holds chords of 1 / 3 / 6 notes, switches oscillator engines live, prints render time, cycles per module and the memory use | `C:/.platformio/penv/Scripts/python.exe apps/ritual_machine/tools/serial_test.py` (see "ESP32 firmware and performance work") |

The generated files are committed, so the generators are only needed when their inputs change. The FM loudness table
`apps/ritual_machine/src/platform/engine/fm_patch_gain.h` is regenerated by the test run with `DX7_GAIN_OUT=apps/ritual_machine/src/platform/engine/fm_patch_gain.h`.

## Testing

`framework/tests` has a small test runner (`TEST(name) { CHECK(...); }`, filter by name on the command line), DSP helpers (SNR, DFT)
and `DspRig`, an engine with all modules registered; the app's `tests/engine` use the same runner and rig. A test that needs no app
code (no `core/`, no `platform/`) belongs to the library, the others to the app; `.\test.ps1` at the repo root runs both. The suite covers the numeric kernels against float64 references, the plan
compiler and the engine (feedback relocation per voice, rebuilds that keep state, a two-thread stress test), every module
against theory (filter responses against the analytic bilinear Butterworth, FM sidebands against Bessel functions, aliasing
against the naive waveforms, reverb decay rate, chorus delay swing...), the sampler against a simulated slow card (latency,
bandwidth, stalls), and the production path from `rack_t` to audio samples (`test_integration.cpp`). Run `-Matrix` after
touching anything in `engine/`: the six configurations catch hard-coded rates and block sizes.

Screens, in `apps/ritual_machine`: `gcc -O1 -w -Isrc -I../../lib/u8g2/csrc tools/ui_dump.c src/core/*.c src/app/*.c ../../lib/u8g2/csrc/*.c -lm -o build/ui_dump.exe`, then
`build\ui_dump.exe "menu right right"` prints the frame after those events.

## ESP32 firmware and performance work

The firmware runs on the first prototype ("HWV1": Waveshare ESP32-S3-Pico module, 16 MB flash, 2 MB PSRAM) and was brought up and optimized on the real board. The hardware equals the
SynthBox repository (github.com/clement-chupin/SynthBox, `HWConfig.h`): a 4 x 8 + 4 Fn key keyboard on a TCA8418 over I2C, 7 relative (quadrature) knobs behind a 16-channel analog mux, a joystick,
an SH1107 128 x 128 OLED, a PCM5102A DAC + MAX98357A speaker amplifier and WS2812 LEDs. The new prototype will have different controls (the ones the simulator's panel has). Each board
lists its own (`hwv1_controls.h`; the wiring behind them: `hwv1_layout.h`), and the Ritual Machine maps them onto its roles in `core/boards/map_hwv1.h`.

```powershell
C:\.platformio\penv\Scripts\pio.exe run -d apps/ritual_machine                  # build (use this pio: another Python's pio recreates the penv and breaks it)
C:\.platformio\penv\Scripts\pio.exe run -d apps/ritual_machine -t upload        # flash. THE USER flashes the board, Claude never does
C:\.platformio\penv\Scripts\python.exe apps\ritual_machine\tools\serial_test.py   # measure on COM8 (close any serial monitor first)
```

> **Warning: never type a bare `pio`.** `pio` on PATH is `Python313\Scripts\pio.exe` (PlatformIO 6.2.0 installed into the user's Python 3.13). It sees that
> `C:\.platformio\penv` was made with Python 3.11, tries to recreate it with 3.13, fails (`uv installation via pip failed with exit code 106`) and leaves the penv
> without `pio.exe` or `pyvenv.cfg`: no build, no flash. This happened twice, the last time on 2026-10-05. Repair: close VS Code (its PlatformIO extension keeps files in
> the penv open), delete `C:\.platformio\penv`, then `C:\.platformio\python3\python.exe -m venv C:\.platformio\penv` and
> `C:\.platformio\penv\Scripts\python.exe -m pip install platformio` (or reopen VS Code and let the extension reinstall it). The bundled `C:\.platformio\python3`
> (3.11.7) and the toolchains in `C:\.platformio\packages` / `platforms` are not touched by this failure.

### Files (the board: `framework/boards/hwv1/`; the app's side: `apps/ritual_machine/src/platform/esp32/`)

The app's own: `main_esp32.cpp`, `audio_esp32.cpp`, `samples_esp32.*`, `serial_cmd_esp32.*`, `bench_esp32.*`, `usb_storage_esp32.cpp`. The rest is the board's.

| File | What |
| --- | --- |
| `main_esp32.cpp` | setup / loop: power, serial (115200, non-blocking TX), the hand-over to the card reader's firmware when the power button is up (`board_card_reader`), optional boot delay, input task, audio, app. The UI loop runs on core 0; when the power button goes up it saves (`app_save_now`) and lets the supply go |
| `board_pins.h` | the pin map, inside `#ifdef HWV1` (I2S, OLED, mux, joystick, keyboard I2C, LED, power, SD), the power button's level and the battery's scale |
| `hwv1_controls.h`, `hwv1_controls.c` | the board's control table (`hal_input.h`): the ids (`HWV1_KEY(row, col)`: the 4 function keys = row 0, the 4 x 8 note keys = rows 1..4; `HWV1_POT_0..6`; `HWV1_JOY_X / _Y / _SW`) and, per control, its name, kind, matrix place, panel place (not measured: a guess to correct against the board; the simulator built with `-Board hwv1` draws the board from it) and LED. Plain data: it also compiles on the desktop (the host tests read it) |
| `hwv1_layout.h` | the wiring behind the table: the key controller's orientation (flip flags), each pot's direction (`kHwv1KnobSign`), counts per revolution and the noise thresholds, the joystick's levels, the mux's two other channels. Which role a control plays is the app's (`core/boards/map_hwv1.h`) |
| `kbd_tca8418.*`, `mux_esp32.*`, `input_esp32.cpp` | keyboard (Adafruit TCA8418 on Wire1, config watchdog, bus recovery), mux reads, and the scan task on core 0 that turns them into events with the board's ids (quadrature decode with hysteresis, joystick). A pot reports steps, or a position once the app asked for it (`input_control_mode`, before `input_esp32_init`) |
| `display_esp32.cpp` | `U8G2_SH1107_128X128_F_HW_I2C`, 400 kHz (a full frame takes ~120 ms). `display_send()` (HAL, in place of `u8g2_SendBuffer`) copies the frame and wakes the `display` task, which sends only the changed 8 x 8 tiles; this panel's column offset (96) wraps, so a run starting at tile x is sent with the offset moved to (96 + 8x) mod 128. `app_step` also puts a redraw off while input is queued (`input_pending()`) |
| `board_esp32.*` | power hold (PWR_ON_EN), amplifier / DAC enable (XSMT; SPK_SD left floating, `HWV1_SPK_SD_MODE=1` drives it). **The power button** is a latching switch, down = on, read on PWR_SENSE (`HW_PWR_SENSE_ON`, `board_power_button_on`); the USB cable feeds the board whatever it says, so a board that still runs after the latch was let go with the button up is on the cable (`board_power_started_off` at the start, `board_power_poll` / `board_power_off` later): on the battery the board stops, on the cable it restarts into the card reader (`board_card_reader`: `esp_ota_set_boot_partition` on the second slot, then a restart; with no firmware there the synth goes on). The joystick held at such a start keeps the synth (the button is ignored until it is next used). `board_usb_host`: a computer on the cable (the USB-Serial/JTAG port sees its frames). **The battery** (`hal_power.h`): mux channel 1 through a divider, millivolts on the pin x `HW_BATT_FULL_SCALE_MV` / 3300, smoothed, 6.0 .. 8.4 V = 0 .. 100 %, under 4 V = no battery; read by the input task every 100 ms with the mux's spare channel 0 (`HWV1_MUX_AUX`, kept as a button state, not a control yet). Serial (dev): `power`, `storage` |
| `usb_storage_esp32.cpp` | **the card reader, a second firmware** (`[env:card_reader]`, built with `HWV1_CARD_READER` from this file, `board_esp32`, `display_esp32`, `mux_esp32` and the card driver; 540 KB, in the second application slot at 0x650000; `pio run -d apps/ritual_machine -e card_reader -t upload`, once). With the power button up on the cable the TF card is a USB drive on the computer: TinyUSB mass storage over the card driver's raw sector access (`sd_card_raw_*`), its own screen, the USB transceiver taken from the serial console and given back before it restarts into the synth (the power button down, or the joystick held 2 s and kept held). Its first act is to point the bootloader back at the synth. Why not a mode of the synth: TinyUSB takes 23 KB of static internal RAM whether it runs or not (measured: the synth's link failed by 10 KB). `HWV1_USB_STORAGE=0` (synth) never starts it. On the board (2026-10-11): the drive mounts and reads at 543 KB/s (full-speed USB); writing and the way back to the synth not tried yet |
| `leds_esp32.cpp` | LED HAL (`hal_leds.h`): FastLED 3.10.6 (pinned; its first RMT channel uses DMA on the S3), SK6812 colour order RGB, key -> chain map (physical columns, checked on the board), brightness capped at `HW_LED_MAX_BRIGHTNESS` = 20 % (power: the user's limit), sends only when the frame changed. Serial (dev): `leds N` (chain LED N red for 5 s), `leds all`, `leds off` / `leds on` (stop / resume every transfer). What the keys show: `app/key_leds.c` (play feedback, called at the end of `app_step`) |
| `audio_out_esp32.cpp` | the audio output (`hal_audio_out.h`): I2S (MSB format, no MCLK) and the audio task on core 1, which calls the app's render function for every 64 frames; applies `DEV_OUTPUT_GAIN_PCT`, the speaker channel's level (`board_speaker_level`), the gate that stays closed until the loading screen has ended; with `HWV1_DEBUG_AUDIO` it measures and calls the app's report once a second |
| `audio_esp32.cpp` | the app's side: sizes the fast heap (internal RAM) and the bulk heap (PSRAM), starts the engine and hands `engine_synth_render` to the output; prints the `[AUDIO]` / `[PROF]` / `[SEC]` / `[OSC]` / `[HEAP]` lines from the output's report; `core/audio.h` forwarded to the bridge |
| `card_task.*` | **the card task** (core 0): mounts and watches the card, the small files of `hal_storage.h`, the folders the app listed, the card events; an app adds its own card work through `card_hooks_t` (in the loop's order: calm work, always, idle, pump). `samples_esp32.cpp` is the Ritual Machine's: the sample library on those hooks |
| `bench_esp32.*` | boot micro benchmark (`HWV1_BENCH`): cycles per operation, RAM vs PSRAM |
| `sd_card.*`, `sd_card_spi.cpp`, `storage_sd.*`, `samples_esp32.*` | the TF card: `sd_card.h` is the interface (mount as FAT at `/sdcard`, probe the read time, alive / identity check); `sd_card_spi.cpp` is HWV1's own small SPI-mode driver (reads with the data CRC16 checked and a bad block read again, `[SD] data CRC errors` in the log; single-block writes for the settings files, each checked with CMD13; the data token is polled `HWV1_SD_PROBE` (8) bytes per SPI call; above 25 MHz only after a CMD6 switch to high-speed mode, which the prototype's SL32G card does not have, so it runs at 25 MHz unless `-DHWV1_SD_OVERCLOCK=1`; `-DHWV1_SD_BENCH` prints raw, scattered and file read times at boot; registered with FATFS; ESP-IDF's sdspi host spent 40 ms before every command on this board); a new prototype with an SDMMC slot replaces that one file. The card is polled once a second (no card-detect pin): inserted -> a timed read test and a check of the folders of `STORAGE_FOLDERS` -> a card event (`storage_poll_event`, hal_storage.h) that the app shows as popups: "Card inserted", "SD CARD TOO SLOW" (slower than 15 ms per sector: not used for samples; the synth runs as if there were no card), and an ASK when folders are missing: Yes runs `storage_make_folders` on the card task (the old layout's `/samples` and `/keys.cfg` move into `/system`, then the missing folders are made, then a rescan); removed -> "Card removed"; removed or swapped -> the catalog is reset, `SdStorage` is the sampler's `StorageDevice`, `samples_esp32` is the I/O task (core 0, priority 5, see "Tasks and priorities"): mount, list `/sdcard/system/samples/*.smp` into the catalog (the header of each file carries the amplitude overview, so a scan reads one block per file), run the loader, and the settings-file jobs of `hal_storage.h` (`storage_read` / `storage_write` post a job and wait up to 1.5 s; the task runs it between loader reads; a write goes to `<name>.tmp`, then replaces the file). Cook files on the PC (`samples_src/` + build.ps1); there is no .wav / .mp3 import on the board. `SdStorage` reads through an 8 KB internal DMA-capable bounce buffer (a PSRAM destination would make the SD driver issue one command per 512 bytes) |
| `serial_cmd_esp32.*` | dev commands over the serial port (`DEV_SERIAL_CMD`) |

### Build flags (`platformio.ini`)

| Flag | Meaning | Before a release |
| --- | --- | --- |
| `HWV1` | selects the pin block and drivers of the first prototype | keep for this board |
| `HWV1_USB_STORAGE=0` | (not set: 1) the synth never starts the card reader's firmware: with the power button up on the cable the synth runs, as before 2026-10-11 | leave unset |
| `HWV1_CARD_READER` | set by `[env:card_reader]` only: this build is the card reader's firmware (`usb_storage_esp32.cpp`) | keep there |

**A release** (`apps/ritual_machine/tools/make_release.ps1 -Version beta-1.1`, into `build/release`; the version shown at start is `BOOT_VERSION` in `app/app.h`): `[env:release]` is the synth without `DEV_SERIAL_CMD` and with the output at
18 % (the user's settings for the first prototype, 2026-10-11); the script merges it with the bootloader, the partition table and the card reader into one image
written at address 0 (`esptool --chip esp32s3 write-flash 0x0 ritual-machine-<version>-hwv1.bin`), and packs the simulator (two programs, one per board: `ritual-machine-sim-hwv1.exe` and `ritual-machine-sim-panel.exe`) with its DLLs, the five synthetic
demo samples (never the user's `samples/`), a readme (`tools/release/README-simulator.txt`) and the licence notices into a zip. Published with
`gh release create <tag> --prerelease` and the two files.
| `DEV_OUTPUT_GAIN_PCT=N` | output level in percent of full scale (the first prototype's output stage is harsh on headphones) | remove or 100 |
| `DEV_SPEAKER_DEFAULT=N` | the built-in speaker's starting level (0 = off, 20 = 100 %; GENERAL "Spk" still changes it) | remove (full) |
| `DEV_BOOT_DELAY_MS=3000` | wait after `Serial.begin` so a monitor catches the boot log | remove |
| `DEV_SERIAL_CMD` | serial commands, full guide with examples in [human_docs/SERIAL_COMMANDS.md](human_docs/SERIAL_COMMANDS.md) (`ping`, `on N`, `off N`, `chord K` (up to 48 notes), `eng a b c d`, `release`, `status`, `samples`, `dir [PATH]`, `use NAME`, `patch startup`, `patch sampler F L`, `patch strings`, `patch para N V` (N Strng oscillators into one filter, Mod Para, V voices), `mode mono|poly [glide] [legato]`, `mode para [env 0-2]`, `voices N`, `str wave|osc|det|mix|lvl|lp|ftype|fx V` (Strings settings; `str fx 0` = every FX slot None), `flt T` (type of every FL module: 0 Off 1 LP 2 BP 3 HP 4 LP24 5 Notch 6 LP6 7 Ladr 8 ChLP), `leds N|all|on|off`, `dump N` (the next N engine output samples as `[DUMP] @index v ...` lines, before the dev gain: offline spectrum checks); the patch ones rebuild the synth like leaving the menu) | remove |
| `HWV1_DEBUG_UI` | once a second while the controls are used: input events, the slowest app step, the slowest `audio_set_params` (`[UI]` lines) | remove |
| `HWV1_SD_BENCH` | boot-time card benchmark (`[SD] bench ...` lines: raw reads of 1 / 8 / 16 sectors, file reads of 512 B / 4 KB) | remove |
| `HWV1_SD_FREQ_KHZ=N` | SPI clock of the card once initialised (default 20000) | keep / tune |
| `BOOT_TRACE` | `[BOOT]` lines: when the display, the audio, the application and the loop are ready (ms since reset), the boot animation's frames, its slowest frame and when its end was asked, what the start waits for (card state, the pads' files, steps over 100 ms), and every control event of the loop's first 5 s (kind, control id, value) | remove |
| `HWV1_SD_IO_PRIO=N` | priority of the card I/O task (default 5) | keep |
| `HWV1_SD_CALM_PRIO=N` | its priority for card work without a deadline: a header, a folder's list, a sample's load (default 1 = the UI's) | keep |
| `HWV1_SD_IDF` | use ESP-IDF's sdspi host instead of the own SPI driver: on HWV1 it costs about 40 ms before EVERY command, kept for comparison only | do not use |
| `HWV1_DEBUG_AUDIO`, `ENGINE_PROFILE` | once a second: `[AUDIO]` render time / blocks over budget / graph builds / DMA underruns (the real dropouts), `[PROF]` cycles per module, `[SEC]`, `[OSC]`, `[HEAP]` | remove |
| `HWV1_DEBUG_INPUT`, `HWV1_BENCH`, `HWV1_TEST_TONE` | raw key log; boot benchmark; 440 Hz test tone instead of the engine | remove |
| `ENGINE_FX_MONO=1` | delay and reverb compute one channel (user choice) | decision |
| `ENGINE_REVERB_HALF=1` | reverb tank at half rate (user choice) | decision |
| `ENGINE_FILTER_EXACT=1` | (not set) filter coefficients exact per sample even for smooth modulation | |
| `ENGINE_CMD_RING=128` | command ring entries (270 bytes each) | keep |
| `ENGINE_MAX_VOICES` | (default 32) the engine's voice ceiling; the Strings type uses it all, Modular / FM stop at 8. Costs ~13 KB of static RAM at 32 (instance records); plans are sized by their own voice count | keep |
| `ENGINE_WT_RAM=1` | (not set) the Strings mipmap tables in internal RAM (41 KB) instead of flash: faster reads, smaller fast heap | measure, then decide |
| `ENGINE_NO_IRAM` | (not set) keep module code in flash instead of IRAM | |

The framework appends `-Os` after the project's `-O2`, so `build_src_flags` carries `-O2 -fno-stack-protector` for the project sources. Check `compile_commands.json` if the optimization level is ever in doubt.
The host tests take the same engine flags: `.\test.ps1 -Defs "-DENGINE_REVERB_HALF=1"` (the stereo-specific reverb and delay tests fail with `ENGINE_FX_MONO=1` by design).

### Reading the serial log

- `[BOARD]` power / amplifier pins at boot; `[BENCH]` cycles per operation (the table in ENGINE_DESIGN.md ADR-035 came from here); `[KEY]` raw key events.
- `[AUDIO] render avg A us, worst W us, budget B us per 64 frames, N blocks over budget of M, graph builds G (last: reason)`: the audio task's time to render 64 frames against the 1333 us it has. A block over budget is an audible
  dropout. A `worst` of 80-250 ms with `graph builds` rising means a knob changed the graph's shape (rebuild stall; the reason text says which cable or node).
- `[SD] N reads in T ms: X KB/s, avg A us, worst ever W us; opens, seeks, errors | [SMP] stream blocks +B, underruns U (+d)` every 2 s while samples stream (needs `HWV1_DEBUG_AUDIO`): what the card delivered and whether a sampler playhead ran dry
  (`underruns` rising = audible dropouts; avg read time near 1000 us or more = the card is the limit).
- `[PROF] cycles per block (budget 160000), total T: Name=cycles(xN) ...` per module type, summed over voices (xN = instances). `[SEC]` stage timers inside Reverb and Delay, `[OSC]` per oscillator engine, `[HEAP]` fast heap used / spilled bytes.

### The serial test tool

```
C:/.platformio/penv/Scripts/python.exe apps/ritual_machine/tools/serial_test.py                                    # idle, then 1, 3 and 6 held notes
C:/.platformio/penv/Scripts/python.exe apps/ritual_machine/tools/serial_test.py --chords 1,2,4,8 --hold 8 --log run.txt
C:/.platformio/penv/Scripts/python.exe apps/ritual_machine/tools/serial_test.py --engines "0,1,4,6;2,3,5,7" --chords 2   # cost of each oscillator engine, per voice and sample
C:/.platformio/penv/Scripts/python.exe apps/ritual_machine/tools/serial_test.py --raw --seconds 3                  # print what the board sends
```

It opens the port with DTR / RTS low (no reset). Its commands need `DEV_SERIAL_CMD` in the flashed build ("no answer to ping" otherwise), and it discards the first 2 s after each change. The notes it plays go straight to the engine and
do not appear on the UI. If two runs give identical per-engine numbers after you changed the code, the old binary is still on the board.

### Tasks and priorities

The firmware already runs on FreeRTOS (the Arduino core is built on ESP-IDF, dual core): nothing else has to be added, the work is deciding who runs where and how urgent each task is. Higher number = more urgent.

| Task | Core | Priority | Does | Why there |
| --- | --- | --- | --- | --- |
| `audio` | 1 | 23 (`configMAX_PRIORITIES - 2`) | engine render + I2S write | a missed block is a click; core 1 is otherwise idle |
| `input` | 0 | 8 | keyboard / mux / joystick scan | short and periodic, must not lose events |
| `sd_io` | 0 | 5; 1 for work without a deadline | card mount / scan, the sample loader | has deadlines (a dry stream is audible) but only milliseconds of CPU; sleeps 2 ticks after 8 reads in a row and during a scan (the card driver busy-waits, so without that the core-0 idle task starves and the task watchdog fires). A sample's header, a folder's list and the load of a sample (hundreds of ms each) run at the UI's priority (`Calm`, `HWV1_SD_CALM_PRIO`): at 5 they held the UI for that long, at 0 they starved |
| Arduino `loop()` | 0 | 1 | app, LEDs; draws frames into RAM (the sequencer's timing is the audio task's: ADR-042) | notes and knobs must not wait for the display bus |
| `display` | 0 | 1 | sends the changed tiles of the newest frame over I2C | the bus waits block, so `loop()` runs meanwhile |
| `boot` | 0 | 10 | the boot animation (`64/boot`): a frame every 50 ms (`BOOT_FRAME_MS`) from `display_init` on (the first thing `setup()` does), back and forth until the application reports its start (`display_boot_end`: the card read, the pads' files in), then on to the nearer end of the animation; under it a loading bar (`display_esp32_boot_stage` from `setup()` for its first 30 %, `display_boot_progress` from the application for the rest); it hands the panel to `display` and deletes itself | above everything else on core 0, so the frames keep their rhythm while the audio, the card and the application start (measured: a frame sent in 12 ms at most; the UI at 5.1 s on a card with 1800 samples in one folder, of which 2.3 s for three pads' files). While it plays, the card's work without a deadline keeps priority 5 |

Rules: nothing on core 1 except `audio`; anything that waits on hardware (card, I2C) must block (DMA / semaphore), never spin; the loader holds the engine mutex while it reads a ring block, so a UI build can wait a few ms behind a card read (never the audio task); a sample's header and heads are read outside it (`SdStorage::service`, `IoRead::pinned`), and the UI never waits for a card answer it can take later (`audio_sample_request`).
If something new needs the CPU on core 0, give it a priority by its deadline, not by its importance.

### Performance rules (details and numbers in ENGINE_DESIGN.md ADR-035)

- Budget 160000 cycles per 32-frame block. Check `blocks over budget` with 1, 3 and 6 held notes of the real patch (`rack_init_startup` = 4 oscillator engines into a filter, delay and reverb on).
- Module `process()` methods are `SC_HOT` (IRAM); lookup tables are `SC_TABLE` (internal RAM); module state and the plan buffers come from the fast heap (about 106 KB of internal RAM), long delay lines and large buffers from the
  bulk heap (PSRAM). When the fast heap is full, allocations spill to PSRAM and the affected modules get 2-4x slower: watch `[HEAP] ... spilled`.
- Do not change the graph's shape from a parameter (nodes, cables, a cable at exactly unity): it rebuilds the plan and stalls the audio.
- Prefer int32 + `mulh` over 64-bit math; use float32 only where the benchmark shows a chain of multiply-adds on state (the filters); no float divide or bit tricks in per-sample code.
- The recipe for a new optimization round is the project skill `.claude/skills/esp32-optimize/SKILL.md`.

## Not implemented yet (next steps)

- **Sampler in the application: done for one sample per module** (ADR-030). Left: zones (multisample keyboards, velocity layers) in the UI,
  the granular module in the rack, a sampler synth type, saving racks with file *names*. The TF card driver and I/O task exist (untuned: see CONTINUE.md for what to measure).
- **Motion sequencer: done** (ADR-029). Left for later: more targets (needs mapper support: resonance, mix, modulator rates), a
  lane copy / clear / randomise helper. (`seq.pos` is the engine's report since ADR-042.)
- Modulation of resonance and of other modulators in the mapper (the engine can do both: they are cables).
- LFO sync to the sequencer tempo, key tracking, per-voice phase retrigger options.
- Saving / loading racks and FM patches (flash / NVS) and a patch list.
- "Learn" a parameter by moving a control; the TF card driver and the LED driver on the ESP32.
- CPU headroom for the voice counts the UI allows: measured for the startup patch (CONTINUE.md has the numbers and the next steps).

## Gotchas

- Run the exe from a folder where `SDL2.dll` can be found (PATH or next to the exe).
- Sound too quiet/loud: master **Vol** on the menu's GENERAL tab (and, for FM, the per-patch trim table).
- Rack changes only take effect when the menu is closed; effect edits are immediate.
- Notes posted and released within one audio block never open the gate (commands are applied at the next block).
- PowerShell does not run `sdl2-config` or expand `*.c` for gcc; `build.ps1` handles both.
- Files written from Python on Windows must be opened with `encoding="utf-8"`.
- When editing files with scripts: do not put `\n` inside C string literals in a Python triple-quoted string (it becomes a real
  newline); write the script with the editor tool and run it from a file.
