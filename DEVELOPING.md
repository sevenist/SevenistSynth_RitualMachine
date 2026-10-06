# Developer guide

A synth editor UI (128x64 OLED) that runs on the desktop (SDL window, sound from our **own DSP engine** through
SDL audio) and is structured to run on the ESP32-S3. The same `core/` and `engine/` code drives both. Two synth
types: a **modular** synth built from a rack of modules, and an **FM** synth (DX7-style, 6 operators) with a patch
editor. Both go through the master effects (chorus, delay, reverb).

The engine's design, every decision with its reasons and the measurements behind them are in
[ENGINE_DESIGN.md](ENGINE_DESIGN.md). This guide is about working in the code.

## Quick start (desktop simulator)

```powershell
cd D:\DEV\SevenSynthCore\ui\oled_sim
.\build.ps1                      # clean build ~5 s, one changed file ~1.5 s (per-file objects in build\obj, parallel)
.\build\oled_sim.exe             # SDL2.dll must be on PATH (C:\msys64\ucrt64\bin)
.\tools\build_engine_tests.ps1 -Matrix    # 133 engine tests in six sample-rate / block-size configurations
```

**Samples:** put `.wav` / `.mp3` files in `samples_src/` and run `.\build.ps1`: every new or changed file is converted to `samples/<name>.smp` (the cooked format; stereo is mixed to mono,
the loop and root note come from the WAV `smpl` chunk, or a note at the end of the name: `pad_c4.wav`, `bass_fs2.wav`). The simulator plays `samples/`; `.\build.ps1 -Sd E:` also copies the `.smp` files to the
TF card (`E:\samples`). `-NoSamples` skips the step. See `samples_src/README.txt`.

If `oled_sim.exe` is running, `build.ps1` cannot overwrite it and builds `build\oled_sim_new.exe` instead (close the
window to get the normal name back). `.\build.ps1 -Clean` rebuilds everything.

The simulator opens two windows: the **OLED** and a **Panel** drawing the prototype's front panel (left strip: master volume knob,
encoders A and B with their switches, play button; centre: 8 x 5 matrix keys (a function row of 8, then 4 rows of 8) with a knob above each of the first 4 columns; right: 3 knobs,
joystick with push, 3 buttons). Every control works with the mouse (click = press; wheel over a knob / encoder = turn; drag a knob
up / down; drag inside the joystick pad) and, except the knobs, with the keyboard. The panel's bottom line shows the last
control -> action and the octave / Shift state.

| Control | Default role (table in `core/bindings.c`) | Keyboard (physical key, QWERTY names) |
| --- | --- | --- |
| Encoder A turn / push | previous / next element, i.e. rows (with Shift: pages) / jump to the page selector | `[` `]` / `\` |
| Encoder B turn / push | change the focused value, **no latch needed** (with Shift: 4 steps per detent) / activate a button | `;` `'` / `/` |
| Joystick | move the focus (in the menu: in 2D; in the main view: rows and values as before). **Push = latch** (in the menu only; in the main view it activates the row) the focused value: the joystick then changes it (left / right, or up / down), push again to release; on a button, push activates it. The module strip of the RACK tab and the page / tab selector change with left / right without a latch. With Shift: up / down = octave, left / right = page | arrow keys, push = Right Ctrl |
| Button 1 | open / close the menu | Enter |
| Button 2 | delete the selected module (RACK tab) | Backspace / Delete |
| Button 3 (hold) | **Shift** | Left Shift |
| Play | start / stop the sequencer | Space |
| Matrix keys | **the key layout** (`core/keymap.c`, KEYS tab of the menu): function row F1..F4 = Shift, Menu, Back, Play (F5..F8 on the simulator: Octave - / +, Page - / +); note rows by layout: *Keys 8x4* (default: bottom-left = C4 (60) + octave, +1 per key to the right, +8 per row up), *Two 4x4* (left block 0..15 with +4 per row, right block 16..31), *Notes+Nav* (left block 0..15, right block = octave, cursor cross with Latch, Select, Value, Row, Page), *User* | `F1`-`F8` / `1`-`8` / `Q`-`I` / `A`-`K` / `Z`-`,` (top to bottom) |
| Octave | -5..+4: every MIDI note 0..127 is reachable | Shift + joystick up / down |
| Column knobs 1..4 | the value of row 1..4 of the current page (jump mode: the value follows the knob as soon as it moves) | mouse |
| Right knobs R1..R3 | macros: start on the first filter's cutoff / resonance and the first LFO's rate; **Shift + knob** assigns the parameter under the cursor | mouse |
| Master volume knob | master volume (same value as Vol in the GENERAL tab; 0..2 = the gain, ramped over one audio block). On the prototype it is an endless knob: one step of 0.05 per detent from the current value | mouse |
| Esc, closing a window | quit | |

On an AZERTY keyboard the keys keep their physical position: "Q" is the key labelled A, "W" is Z, "Z" is W, "A" is Q, `[` is `^`, `]` is `$`,
`;` is `M`. The matrix block is therefore the 5 x 8 block starting at F1, `&`, A, Q, W on AZERTY (its last column: F8, `!`, I, K, `;`).

**Key layouts (KEYS tab).** The matrix keys are not in the binding table: each key gets one function from the active *layout* (`core/keymap.h`).
Three layouts are built in (in flash); the fourth, *User*, is the user's. In the KEYS tab: *Layout* picks one, *Key* picks a key (or press it: on this tab a key
selects itself and only notes, Shift and Menu still act), *Func* gives it a function (the controls, then notes C4..B7; editing a built-in layout copies it into
User), *Reset* goes back to *Keys 8x4*. The right box lists the keys with their functions. The layout and the User keys are saved in `keys.cfg` at the root of the
TF card when the menu closes (a text file: `layout user` then `key <row>.<col> <function>` lines, editable on a PC; the simulator keeps it next to `samples/`). A card that
shows up gives its `keys.cfg`, unless the keys were changed meanwhile (then they are written to it). **Lock-out escape**: hold F1 (the top-left function key)
for 2 s within 4 s of start: the layout goes back to *Keys 8x4* and a "KEYS RESET" screen shows until F1 is released.

Requirements: MSYS2 UCRT64 (`C:\msys64\ucrt64`) with gcc, g++ and SDL2, and Python 3 for the generator tools.
`lib/` is not versioned. `lib/u8g2` (https://github.com/olikraus/u8g2, needs `csrc/` and `sys/sdl/common/`) is required; `lib/minimp3` (`minimp3.h`, `minimp3_ex.h` from
https://github.com/lieff/minimp3, public domain) enables the mp3 import of the simulator's sample folder (optional);
`lib/amy` (`git clone --depth 1 https://github.com/shorepine/amy.git lib/amy`) is only read by `tools/gen_dx7.py` to
regenerate the DX7 tables.

## Folder map

```
oled_sim/
├─ build.ps1             desktop build (per-file objects, dependency tracking). Params: -Platform sim (default), -Clean
├─ ENGINE_DESIGN.md      the engine's design document: ADRs, measurements, budget, known limits
├─ platformio.ini        ESP32 firmware build (Waveshare ESP32-S3-Pico; runs on the first prototype), flags documented inside
├─ samples_src/          YOUR .wav / .mp3 files: build.ps1 converts the new ones to samples/*.smp (README.txt inside)
├─ tests/engine/         133 unit / integration tests (own tiny runner), see "Testing"
├─ tools/                generators, measuring tools and the serial test tool for the board (see "Tools")
├─ .claude/skills/       project skills for Claude Code (esp32-optimize: the measure / change / test / flash / re-measure loop)
├─ lib/u8g2/             graphics lib + its SDL display/key backend (not versioned)
└─ src/
   ├─ core/              PORTABLE application logic, plain C. No Windows/SDL/Arduino/engine includes here.
   │  ├─ app.c/.h           glues input -> bindings -> UI -> data -> audio; owns the redraw policy, the octave / Shift state and the joystick-as-buttons
   │  ├─ bindings.c/.h      THE table that links hardware controls to actions (the one place to change what a button does), except the matrix keys:
   │  ├─ keymap.c/.h        the key layouts (3 built in + User), the functions a key can take, keys.cfg on the card
   │  ├─ synth_ui.h         public interface of the UI (cursor state, ui_event_t, knob / macro calls)
   │  ├─ ui_pages.c         what is on screen: page tables, the page list generated from the rack, menu tabs
   │  ├─ ui_input.c         what events do: row editing, menu tabs, knobs, macros, synth_ui_handle()
   │  ├─ ui_graphs.c        the small pictures of the graph box (waveform, envelope, filter, effects...)
   │  ├─ ui_draw.c          the screens: header, lists, hand-drawn screens, synth_ui_draw()
   │  ├─ ui_internal.h      what those four share (private to the UI)
   │  ├─ ui_screen.c/.h     declarative screens: element tables, focus navigation, latch, drawing (see human_docs/UI_GUIDE.md section 8)
   │  ├─ scr_*.c            the menu tabs as declarative screens: rack, general, samples, fx, fm (ALGORITHM / OPERATOR / ENVELOPE), keys
   │  ├─ rack.c/.h          modular synth model: slots, audio chain rules, module parameters, modulator targets
   │  ├─ synth_config.c/.h  general settings (synth type, voices, volume, the FM patch copy) and the master FX settings
   │  ├─ dx7.c/.h           editable 6-operator FM patch + editing helpers
   │  ├─ dx7_factory.c      GENERATED: the 128 factory DX7 patches as dx7_patch_t
   │  ├─ dx7_algos.c/.h     GENERATED: 32 algorithm diagrams (+ operator box positions)
   │  ├─ seq.c/.h           polyphonic step sequencer (pure; time is passed in)
   │  ├─ synth_params.c/.h  global amp envelope (AMP ENV page) + the generic LIN/LOG/ENUM parameter table
   │  ├─ gui.c/.h           style sheet + layout/text/sprite/animation helpers over u8g2
   │  └─ sprites.h, module_sprites.h (GENERATED)   1-bit sprites
   ├─ hal/               INTERFACES the core uses; one implementation per platform (audio, display, input: the board's physical controls, storage: settings files on the card)
   ├─ engine/            THE DSP ENGINE, portable C++17 (no exceptions, no RTTI, no heap after init; sees no core/ or hal/)
   │  ├─ dsp/               q15/q31 math, tables (GENERATED), phase/pitch, oscillators, SVF, FFT/STFT, delay, smoothing, CORDIC
   │  ├─ core/              module API, graph description, plan compiler, engine (voices, command queue), heap
   │  ├─ modules/           builtin, synth (Osc Env Lfo Filter Vca Mix Mult Shaper Const), fx (Delay Spectral Vocoder Chorus
   │  │                     Reverb), dx7_voice (FM), sampler_modules (Sampler Granular), strings_modules (Strings voice, Ensemble)
   │  └─ sampler/           .smp format, storage interface, sample bank, streaming loader
   └─ platform/
      ├─ engine/            shared by desktop and ESP32: rack -> graph mapper, DX7 patch conversion, the C API
      │                     (engine_synth.h) the HAL calls
      ├─ sim/               desktop: SDL display, SDL audio thread, keyboard, the samples/ folder on a SimStorage (sim_storage.h: a slow-card model, also used by the tests) (#ifdef PLATFORM_SIM)
      └─ esp32/             the firmware for the first prototype (#if defined(ARDUINO_ARCH_ESP32), pins and drivers inside #ifdef HWV1): I2S audio task + profiling, SH1107 display,
                            TCA8418 keyboard, analog mux + quadrature knobs, joystick, LED stub, power, dev serial commands, boot benchmark (see "ESP32 firmware and performance work")
```

### The rules that keep it portable

1. `core/` only includes `hal/*.h`, `u8g2.h` and the C standard library. Anything platform-specific goes behind a HAL function.
2. `engine/` includes nothing from `core/`, `hal/` or the platform. `platform/engine/` is the only bridge between the two.
3. Every file under `platform/<x>/` is wrapped in its guard, because PlatformIO compiles all of `src/`.
4. The display is created by the platform (`display_init()`), then handed to the app: `app_init(&app, display_init())`.
5. HAL headers have `extern "C"`, so C core code links from C++ files. `core/*.c` stays C (it uses C designated initializers).
6. In the engine, nothing on the audio path allocates, locks, logs or uses `float` (see the guidelines in ENGINE_DESIGN.md).

## Architecture in one page

```
UI thread (main loop)                                        audio thread (SDL callback / I2S task)
input_poll() -> app_step() -> bindings[] -> app_run_action() -> synth_ui_handle()   edits the data below
                  |                |
                  |                +--> returns true when a sound value changed -> audio_set_params(rack, params)
                  |                +--> ui.rebuild set (menu closed with structural edits) -> audio_build(rack, params)
                  +--> seq_tick(audio_millis()) -> audio_note_on / audio_note_off
                  +--> synth_ui_draw() (only when something visible changed)

audio_build / audio_set_params  ->  engine_synth_*  ->  rack_graph_build()  ->  Engine::load()  (atomic plan swap)
audio_note_*                    ->  Engine::note_on/off, set_param, set_blob  -> lock-free command ring -> engine_synth_render()
                                                                                  drains it at the start of every block
```

The data the UI edits (all fixed-size, no malloc):

| Data | Where | What it holds |
| --- | --- | --- |
| `rack_t` | `core/rack.h` | the modular synth: up to 10 slots (the rack strip scrolls, 8 cells visible) (module type, id, own parameters `v[]`, modulator target) and `cfg` |
| `synth_config_t` (`rack.cfg`) | `core/synth_config.h` | synth type (Modular/FM), voices, master volume, the FM patch being edited (`dx7_patch_t fm`), the master FX values |
| `synth_params_t` | `core/synth_params.h` | the global amp envelope (end of the modular voice) |
| `seq_t` | `core/seq.h` | pattern notes/lengths + BPM, steps, transpose, swing, held notes |
| `synth_ui_t` | `core/synth_ui.h` | cursor state, the generated page list, menu tab, FM editor selection |

Every parameter set has the same shape: `*_adjust(dir)` (clamps, returns whether it changed), `*_label`, `*_format`.
That is what lets one generic list renderer show any of them.

### Screens

- **Main view**: a list of pages (Left/Right on row 0). Pages are **generated** from the data
  (`synth_ui_rebuild_pages`): in Modular mode every rack module contributes its own pages (OC: `OSC n` + `OSC n TUNE` + `OSC n DEST`; every modulator (OC, LFO, ENV, EG) has a `... DEST` page with Tgt / Prm / Dpth, same as the RACK tab; FL: `FILTER n` + `FILT n ENV`; SA: `SAT n`; LFO: `LFO n`; ENV: `ENV n` + `ENV n CRV` (curves, hold, start); EG: `EG n` (the selected point) + `EG n REL`; RS: `RES n`; FL also has `FILT n CRV`; the global `AMP ENV` is followed by `AMP CURVE`;
  MS: `MOTION n` + `MS n LANE`; SM: `SMP n`, `SMP n LOOP`, `SMP n SLICE`)
  followed by `AMP ENV`, `SEQUENCER`, `SEQ SETUP`. In FM mode the pages are `FM SYNTH` (Patch, Vol), `SEQUENCER`, `SEQ SETUP`.
- **GENERAL tab**: **Type** = Modular, Mod Mono, FM, FM Mono (the Mono types: one real voice, last-note priority; the others: a copy of every voice module per voice), Patch (FM types), Voices (polyphonic types), **Glide** and **Legato** (Mono types), Vol, Out, **Spk** (built-in loudspeaker: Off, 5..100 % in 5 % steps; ESP32 only, `audio_esp32.cpp`: the MAX98357A plays the left DAC channel (`kSpeakerCh`), so the level scales that channel; on the prototype the headphones do not follow it. Off = gain 0 on that channel; it also pulls `PIN_SPK_SD` (GPIO 5) low, but that alone did not silence the speaker (2026-10-05): GPIO 5 is not proven to reach the amplifier's SD_MODE, `[SPK]` on the serial log prints what the pin reads). Only the rows that apply are shown. The default is Mod Mono (ADR-036); the voice count is real: `Engine::load(graph, nvoices)`. Test for a family with `synth_type_is_fm()` / `synth_type_is_mono()`, never `type == SYNTH_FM`.
- **Menu** (Enter): tabs switched with Left/Right on row 0. Modular: `RACK`, `GENERAL`, `SAMPLES` (library browser), `FX RACK` (four master slots).
  FM: `GENERAL`, `ALGORITHM`, `OPERATOR`, `ENVELOPE`, `FX RACK`. Closing the menu with structural edits
  regenerates the pages and rebuilds the graph. Effect edits are live (no rebuild).

## Where to make changes

### Add / change a module parameter (modular synth)

1. `core/rack.h`: add an index to the module's `MP_*` enum (`MOD_PARAM_MAX` = 8 values per module).
2. `core/rack.c`: add a row to the module's descriptor table (label, LIN/LOG/ENUM, range, step, default, unit, decimals).
   Defaults are applied by `rack_insert`.
3. `core/ui_pages.c`: put the index in one of the module's page definitions (`*_pages[]`, max 4 rows).
4. `platform/engine/rack_graph.cpp`: map the value onto the engine module's parameter (units in `modules/synth_modules.h`).

### Add a module type

`module_type_t` + the `info[]` row in `rack.c` (name, code, audio/modulator flag, target parameters, own parameters), a line
in `tools/gen_module_sprites.py` (then run it), the sprite table `module_sprite()` in `ui_draw.c`, page definitions, and the
branch in `rack_graph_build()` that creates the engine nodes and cables for it. A new *engine* module is a class in
`src/engine/modules/` (see below) plus its `ModuleInfo`, registration and a test.

### Add a page or a graph

Module pages: extend the `*_pages[]` tables in `ui_pages.c`. Global pages: `global_pages[]` and the `modular_globals` /
`fm_globals` lists. A new graph type = a `GRAPH_*` value, a `draw_*` function (the graph box is `gui_default_style.graph`)
and a `case` in `synth_ui_draw()`.

### Add a general or master-effect setting

`core/synth_config.[ch]`: general values have a `cfg_param_id_t` and cases in `synth_config_adjust/format/label`; effect
values are a row in `fx_table` (label, min, max, step, default, unit) plus an id in the FX range. Show it in the
GENERAL tab, or list it in the FX RACK tab (`scr_fx.c`, the parameters come from `fxrack.c`), and use it in `rack_graph.cpp`. Return `CFG_LIVE` for
values applied immediately, `CFG_REBUILD` for structural ones.

### Write an engine module

Derive from `sc::Module` (`engine/core/module.h`): `info()` returns a static `ModuleInfo` (scope voice/global, ports,
parameters with defaults); `init(Memory&)` allocates buffers (`mem.fast` for hot state, `mem.bulk` for long lines) and the
destructor frees them; `reset()` clears state; `set_param(idx, value)` takes int32 in the module's own units (pitch values
are MIDI x 256, times in ms or 1/16 ms, levels q15); `process(ctx, ports)` renders `kBlock` frames of q15 from `in[]`
(always valid) and `mod[]` (null when no cable is connected; a bipolar q15 signal otherwise), writing `out[]`. Register it
(`Registry::add(id, type_of<T>())`), then test it against a float reference (`tests/engine/rig.h` has the helpers).

### Sequencer (`core/seq.c`)

Per step: note + length. Global (page `SEQ SETUP`): BPM, Steps (1..16), Trsp (transpose), Swing (% delay of odd steps),
edited through `seq_param_adjust/label/format` (table in `seq.c`). The sequencer is **polyphonic**: a note longer than one
step keeps ringing over the following steps (up to `SEQ_POLY` = 8 held notes; the oldest is stolen when full; the same pitch
is retriggered). `seq_tick(now_ms)` is polled from `app_step()` and returns the notes to release (`off[]`, applied first) and
the note to start (`on`); stopping releases everything. The time base is the audio clock (`audio_millis()`).

### GUI helpers (`core/gui.h`)

`gui_style_t` (font, margin, padding, gap, list column / graph box geometry, piano-roll and rack metrics; default in
`gui_default_style`, `core/gui.c`) feeds the helpers: rect layout (`gui_inset`, `gui_center`, `gui_below/above/right_of/left_of`,
`gui_take_top/left`, `gui_grid_cell`, `gui_center_box`), text (`gui_text_w/h`, `gui_row_h`, `gui_text_center`,
`gui_draw_text_centered/left/right`, `gui_draw_field`) and sprites. A sprite is a `gui_sprite_t {w, h, bytes, frames}`: 8 pixels
per byte, MSB = leftmost, rows padded to whole bytes; add small ones to `core/sprites.h`.

Animations: a sprite can be a sheet (`frames` images stacked vertically, e.g. `spr_eq`).
`gui_anim_add(&sprite, first, last, frame_ms, loop)` (or `gui_anim_add_total(...)`) returns an id; `first > last` plays
backwards. Call `gui_anim_tick(now_ms)` from the main loop (true = a frame changed, redraw), draw with
`gui_anim_draw[_centered|_right]`, and delete with `gui_anim_remove(id)` (also `gui_anim_clear`, `_restart`, `_finished`).
A non-looping animation stays on its last frame until removed. Up to `GUI_ANIM_MAX` at once. `app_step()` is the reference.

### Change what a control does, or add a control

Input has three layers, so each change has one home:

1. **HAL** (`hal/hal_input.h`, one `input_poll()` per platform): reports PHYSICAL controls only, as `{control id, kind, value}`: `CTL_ENC_A`
   `IN_DELTA` (detents), `CTL_KNOB_R1` `IN_VALUE` (0..1023), `CTL_KEY(row, col)` `IN_PRESS` / `IN_RELEASE`... It knows nothing about the application.
2. **Bindings** (`core/bindings.c`): the table `{control, event kind, modifier state, action, argument}`. Rebinding = editing a row, e.g. to make
   encoder A change pages instead of rows: `{CTL_ENC_A, IN_DELTA, MODS_NONE, ACT_PAGE_MOVE, +1}`. A control can have several rows; Shift (hold-action on
   button 3) selects rows by `MODS_NONE` / `MODS_SHIFT` / `MODS_ANY`. The list of actions and their arguments is in `core/bindings.h`.
3. **Actions** (`app_run_action` in `core/app.c`): what an action does, mostly by sending a `ui_event_t` (`UI_UP`, `UI_PAGE_NEXT`, `UI_SELECT`... in
   `core/synth_ui.h`) to `synth_ui_handle`, or by playing a note, changing the octave, or setting a knob value.

Recipes:
- *Move a function to another button*: change `ctl` in its row of `bindings[]`. No other file changes. **Matrix keys** are not in `bindings[]`: their functions come
  from the key layout (`core/keymap.c`: `preset()` builds the three built-in layouts, `fns[]` lists the functions a key can take; the user edits them in the KEYS tab).
  `app.c` (`key_event`) runs a key's function through the same `app_run_action`.
- *New action*: a value in `action_id_t` + its name in `action_names` (`bindings.c`) + a `case` in `app_run_action`; then use it in the table. Actions that need the
  release of their control (notes, Shift) are listed in `action_is_hold`.
- *New physical control*: a value in `control_id_t` (`hal_input.h`, before `CTL_HW_COUNT`), a name in `control_name`, a widget in `build_layout` of
  `platform/sim/panel_sim.c`, a row in `keymap[]` of `input_sim.c` (optional), the driver code on the ESP32 (`input_esp32.cpp`, pins in `board_pins.h`), and a row in `bindings[]`.
- *Keyboard shortcuts of the simulator*: the `keymap[]` table of `platform/sim/input_sim.c` (physical scancodes).
- *The joystick* is reported as two axes; the core turns deflection into the virtual controls `CTL_JOY_LEFT / RIGHT / UP / DOWN` (with key repeat, thresholds in
  `bindings.h`), so bindings treat it as four buttons.
- *Knobs*: a knob reports a position 0..1023 and a binding gives it an action (`ACT_PAGE_KNOB`, `ACT_MACRO`, `ACT_MASTER_VOLUME`). A parameter only knows
  "one step up / down", so `ui_input.c` (`knob_set`) walks the parameter to both ends and back to the step that matches the position: nothing to add when a new
  parameter appears. Rows that select or cycle (targets, sample file, step editors) are not driven by knobs (`row_is_knobbable`).
- *Test without the window*: `tools/ui_dump.c` takes control events (`encA:+1`, `shift`, `knob:27:900`, `key:4.0`...), see the top of the file.

### Add a HAL function / a platform

Declare it in `hal/hal_*.h`, implement it in **every** `platform/*/`. A new platform (Teensy, RP2040...) is a folder
`src/platform/<name>/` with the three HAL implementations wrapped in a guard macro; for the desktop-style build
`.\build.ps1 -Platform <name>` compiles `core/` + `platform/<name>/*.c` with `-DPLATFORM_<NAME>`. The audio part of a platform is
small: allocate two memory blocks, call `engine_synth_init`, and arrange for `engine_synth_render` to be called from an audio
thread / DMA task.

### Change the layout / fonts / palette / screen size

The screen size is one setting: `DISPLAY_WIDTH` / `DISPLAY_HEIGHT` in `hal/hal_display.h` (default 128 x 64; `.\build.ps1 -Clean -Display 128x128` for
the simulator). The application reads the real size from the u8g2 instance and builds its style from it (`gui_style_init(st, w, h)` in `core/gui.c`, called by
`synth_ui_draw`): the list column takes half the width and the graph box fills the rest, so a taller screen gives taller graphs without touching the drawing
code. Check a size without SDL with `UI_DUMP_SIZE=128x128` and `tools/ui_dump.c`. A real 128 x 128 design (more rows per page, other fonts) starts from the
style: edit `gui_default_style` / `gui_style_init`. The pixel offsets that remain in `ui_draw.c` are multiples of the row height (`gui_row_h`), so they follow the font.

The style (`gui_default_style` in `core/gui.c`) holds the font, margin, padding, gap, `list_w` / `list_top`, `graph` (graph box rect), the piano roll and the rack.
Default font is `u8g2_font_5x7_tr`. The FM editor tabs use a compact copy of the style (padding 0, gap 0) so five rows fit. `platform/sim/display_sim.c` is the
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
| control | MotionSeq (4 lanes x 16 steps, own sample-accurate clock locked to the note sequencer's timing; ADR-029) |
| effects | Delay (stereo, Hermite, ping-pong), Chorus (Juno-like modes), Reverb (Dattorro plate), SpectralFx (freeze, gate/filter, robot, whisper, pitch shift), Vocoder, **Phaser, Flanger, Tremolo / Auto-pan, Compressor, EQ3, Ring mod / Shifter, Convolver (cab / body IRs), Comb (tuned resonator, voice scope)** (ADR-034) |
| samples | (rack module SM, ADR-030) Sampler (streaming, pitched, loops, reverse, slices, key/velocity zones), Granular; `.smp` files via `tools/wav2smp.py` |

Rack -> engine mapping and the master chain are described in ENGINE_DESIGN.md (ADR-026, ADR-034); the threading rules in ADR-024; modulation depths in ADR-032.

## Tools (`tools/`)

| Tool | Generates / does | Run |
| --- | --- | --- |
| `gen_module_sprites.py` | `src/core/module_sprites.h` (module sprites with 2-letter codes) | `python tools/gen_module_sprites.py` |
| `gen_dx7.py` | `src/core/dx7_factory.c` and `src/core/dx7_algos.c` (needs `lib/amy`) | `python tools/gen_dx7.py` |
| `gen_engine_tables.py` | `src/engine/dsp/tables.cpp` (sine, exp2, tan, tanh, FFT twiddles, window, CORDIC) | `python tools/gen_engine_tables.py` |
| `gen_wavetables.py` | `src/engine/dsp/wavetables.cpp`: band-limited saw / triangle tables, 10 octave bands, for the Strings oscillators (41 KB, flash) | `python tools/gen_wavetables.py` |
| `make_demo_samples.py` | five synthetic demo samples into `samples/` (the simulator's card folder) | `python tools/make_demo_samples.py` |
| `smp_convert.cpp` | every `.wav` / `.mp3` of a folder -> `.smp` (new or changed files only; shares `platform/sim/sample_convert.h` with the simulator's importer). build.ps1 builds and runs it on `samples_src/` | `build\smp_convert.exe samples_src samples [--force]` |
| `wav2smp.py` | one WAV (8..32 bit, mono/stereo, `smpl` loops) -> cooked `.smp` sample, with `--slices` / `--loop` / `--root` options the converter has no way to give | `python tools/wav2smp.py in.wav out.smp --root 60 --slices 0 12000` |
| `build_engine_tests.ps1` | builds and runs the engine tests; `-Matrix` for all configurations, `-Filter name` for a few | see the file header |
| `ui_dump.c` | renders the screens to ASCII without SDL or audio (layout checks) | command at the top of the file |
| `serial_test.py` | talks to the prototype over its serial port: holds chords of 1 / 3 / 6 notes, switches oscillator engines live, prints render time, cycles per module and the memory use | `C:/.platformio/penv/Scripts/python.exe tools/serial_test.py` (see "ESP32 firmware and performance work") |

The generated files are committed, so the generators are only needed when their inputs change. The FM loudness table
`src/platform/engine/fm_patch_gain.h` is regenerated by the test run with `DX7_GAIN_OUT=src/platform/engine/fm_patch_gain.h`.

## Testing

`tests/engine` has a small test runner (`TEST(name) { CHECK(...); }`, filter by name on the command line), DSP helpers (SNR, DFT)
and `DspRig`, an engine with all modules registered. The suite covers the numeric kernels against float64 references, the plan
compiler and the engine (feedback relocation per voice, rebuilds that keep state, a two-thread stress test), every module
against theory (filter responses against the analytic bilinear Butterworth, FM sidebands against Bessel functions, aliasing
against the naive waveforms, reverb decay rate, chorus delay swing...), the sampler against a simulated slow card (latency,
bandwidth, stalls), and the production path from `rack_t` to audio samples (`test_integration.cpp`). Run `-Matrix` after
touching anything in `engine/`: the six configurations catch hard-coded rates and block sizes.

Screens: `gcc -O1 -w -Isrc -Ilib/u8g2/csrc tools/ui_dump.c src/core/*.c lib/u8g2/csrc/*.c -lm -o build/ui_dump.exe`, then
`build\ui_dump.exe "menu right right"` prints the frame after those events.

## ESP32 firmware and performance work

The firmware runs on the first prototype ("HWV1": Waveshare ESP32-S3-Pico module, 16 MB flash, 2 MB PSRAM) and was brought up and optimized on the real board. The hardware equals the
SynthBox repository (github.com/clement-chupin/SynthBox, `HWConfig.h`): a 4 x 8 + 4 Fn key keyboard on a TCA8418 over I2C, 7 relative (quadrature) knobs behind a 16-channel analog mux, a joystick,
an SH1107 128 x 128 OLED, a PCM5102A DAC + MAX98357A speaker amplifier and WS2812 LEDs. The new prototype will have different controls (the HAL ones the simulator uses); the old board is mapped
onto them by tables, so only `hwv1_layout.h` is board-specific.

```powershell
C:\.platformio\penv\Scripts\pio.exe run                  # build (use this pio: another Python's pio recreates the penv and breaks it)
C:\.platformio\penv\Scripts\pio.exe run -t upload        # flash. THE USER flashes the board, Claude never does
C:\.platformio\penv\Scripts\python.exe tools\serial_test.py   # measure on COM8 (close any serial monitor first)
```

> **Warning: never type a bare `pio`.** `pio` on PATH is `Python313\Scripts\pio.exe` (PlatformIO 6.2.0 installed into the user's Python 3.13). It sees that
> `C:\.platformio\penv` was made with Python 3.11, tries to recreate it with 3.13, fails (`uv installation via pip failed with exit code 106`) and leaves the penv
> without `pio.exe` or `pyvenv.cfg`: no build, no flash. This happened twice, the last time on 2026-10-05. Repair: close VS Code (its PlatformIO extension keeps files in
> the penv open), delete `C:\.platformio\penv`, then `C:\.platformio\python3\python.exe -m venv C:\.platformio\penv` and
> `C:\.platformio\penv\Scripts\python.exe -m pip install platformio` (or reopen VS Code and let the extension reinstall it). The bundled `C:\.platformio\python3`
> (3.11.7) and the toolchains in `C:\.platformio\packages` / `platforms` are not touched by this failure.

### Files (`src/platform/esp32/`)

| File | What |
| --- | --- |
| `main_esp32.cpp` | setup / loop: power, serial (115200, non-blocking TX), optional boot delay, input task, audio, app. The UI loop runs on core 0 |
| `board_pins.h` | the pin map, inside `#ifdef HWV1` (I2S, OLED, mux, joystick, keyboard I2C, LED, power, SD) |
| `hwv1_layout.h` | how the old board's physical keys and knobs map to the HAL controls (`kHwv1KeyMap`: the 4 function keys = HAL row 0, the 4 x 8 note keys = rows 1..4; `HWV1_KEY_PRESENT`; `kHwv1Knobs`, flip flags, counts per revolution) |
| `kbd_tca8418.*`, `mux_esp32.*`, `input_esp32.cpp` | keyboard (Adafruit TCA8418 on Wire1, config watchdog, bus recovery), mux reads, and the scan task on core 0 that turns them into HAL events (quadrature decode with hysteresis, joystick) |
| `display_esp32.cpp` | `U8G2_SH1107_128X128_F_HW_I2C`, 400 kHz |
| `board_esp32.*` | power hold (PWR_ON_EN), amplifier / DAC enable (XSMT; SPK_SD left floating, `HWV1_SPK_SD_MODE=1` drives it), power polling |
| `leds_esp32.cpp` | LED HAL (`hal_leds.h`): frame buffer and key -> LED map; the driver that pushes it is a TODO |
| `audio_esp32.cpp` | I2S (MSB format, no MCLK) and the audio task on core 1; sizes the fast heap (internal RAM) and the bulk heap (PSRAM); applies `DEV_OUTPUT_GAIN_PCT`; prints the `[AUDIO]` / `[PROF]` / `[SEC]` / `[OSC]` / `[HEAP]` lines |
| `bench_esp32.*` | boot micro benchmark (`HWV1_BENCH`): cycles per operation, RAM vs PSRAM |
| `sd_card.*`, `sd_card_spi.cpp`, `storage_sd.*`, `samples_esp32.*` | the TF card: `sd_card.h` is the interface (mount as FAT at `/sdcard`, probe the read time, alive / identity check); `sd_card_spi.cpp` is HWV1's own small SPI-mode driver (reads with the data CRC16 checked and a bad block read again, `[SD] data CRC errors` in the log; single-block writes for the settings files, each checked with CMD13; the data token is polled `HWV1_SD_PROBE` (8) bytes per SPI call; above 25 MHz only after a CMD6 switch to high-speed mode, which the prototype's SL32G card does not have, so it runs at 25 MHz unless `-DHWV1_SD_OVERCLOCK=1`; `-DHWV1_SD_BENCH` prints raw, scattered and file read times at boot; registered with FATFS; ESP-IDF's sdspi host spent 40 ms before every command on this board); a new prototype with an SDMMC slot replaces that one file. The card is polled once a second (no card-detect pin): inserted -> a timed read test -> a card slower than 15 ms per sector is not used and the app shows the "SD CARD TOO SLOW" screen (any button dismisses it; the synth runs as if there were no card); removed or swapped -> the catalog is reset, `SdStorage` is the sampler's `StorageDevice`, `samples_esp32` is the I/O task (core 0, priority 5, see "Tasks and priorities"): mount, list `/sdcard/samples/*.smp` into the catalog (the header of each file carries the amplitude overview, so a scan reads one block per file), run the loader, and the settings-file jobs of `hal_storage.h` (`storage_read` / `storage_write` post a job and wait up to 1.5 s; the task runs it between loader reads; a write goes to `<name>.tmp`, then replaces the file). Cook files on the PC (`samples_src/` + build.ps1); there is no .wav / .mp3 import on the board. `SdStorage` reads through an 8 KB internal DMA-capable bounce buffer (a PSRAM destination would make the SD driver issue one command per 512 bytes) |
| `serial_cmd_esp32.*` | dev commands over the serial port (`DEV_SERIAL_CMD`) |
| `alt_esp32synth.*`, `esp32synth/` | dev (`ALT_ESP32SYNTH`): the third-party ESP32Synth library (MIT, v2.4.7, copied unchanged except the `#ifndef` around its limits in `ESP32Synth_Config.hpp`) as a second engine for comparison: serial `alt on` makes the audio task render it and sends the notes to it, `alt off` frees it and rebuilds ours; `[ALT]` line once a second (cycles per 32 frames, per voice). `esp32synth/` is excluded from the source build (`build_src_filter`) and compiled through `alt_esp32synth.cpp`. Its header defines lower-case note macros (`c0`, `d1`, ...) |

### Build flags (`platformio.ini`)

| Flag | Meaning | Before a release |
| --- | --- | --- |
| `HWV1` | selects the pin block and drivers of the first prototype | keep for this board |
| `DEV_OUTPUT_GAIN_PCT=N` | output level in percent of full scale (the first prototype's output stage is harsh on headphones) | remove or 100 |
| `DEV_SPEAKER_DEFAULT=N` | the built-in speaker's starting level (0 = off, 20 = 100 %; GENERAL "Spk" still changes it) | remove (full) |
| `DEV_BOOT_DELAY_MS=3000` | wait after `Serial.begin` so a monitor catches the boot log | remove |
| `DEV_SERIAL_CMD` | serial commands (`ping`, `on N`, `off N`, `chord K` (up to 48 notes), `eng a b c d`, `release`, `status`, `samples`, `patch startup`, `patch sampler F L`, `mode mono|poly [glide] [legato]`, `voices N`; the patch ones rebuild the synth like leaving the menu) | remove |
| `ALT_ESP32SYNTH` | ESP32Synth as a second engine (`alt on/off`, `alt wave sine/tri/saw/pulse/noise`, `alt env a d s r`, `alt vol N`, `alt poly N`, `alt vib rate depth`, `alt crush bits`, `alt status`; `chord K` takes up to 48 notes); 8 KB of internal RAM always, ~15 KB more while on | remove |
| `HWV1_SD_BENCH` | boot-time card benchmark (`[SD] bench ...` lines: raw reads of 1 / 8 / 16 sectors, file reads of 512 B / 4 KB) | remove |
| `HWV1_SD_FREQ_KHZ=N` | SPI clock of the card once initialised (default 20000) | keep / tune |
| `HWV1_SD_IO_PRIO=N` | priority of the card I/O task (default 5) | keep |
| `HWV1_SD_IDF` | use ESP-IDF's sdspi host instead of the own SPI driver: on HWV1 it costs about 40 ms before EVERY command, kept for comparison only | do not use |
| `HWV1_DEBUG_AUDIO`, `ENGINE_PROFILE` | once a second: `[AUDIO]` render time / blocks over budget / graph builds, `[PROF]` cycles per module, `[SEC]`, `[OSC]`, `[HEAP]` | remove |
| `HWV1_DEBUG_INPUT`, `HWV1_BENCH`, `HWV1_TEST_TONE` | raw key log; boot benchmark; 440 Hz test tone instead of the engine | remove |
| `ENGINE_FX_MONO=1` | delay and reverb compute one channel (user choice) | decision |
| `ENGINE_REVERB_HALF=1` | reverb tank at half rate (user choice) | decision |
| `ENGINE_FILTER_EXACT=1` | (not set) filter coefficients exact per sample even for smooth modulation | |
| `ENGINE_CMD_RING=128` | command ring entries (270 bytes each) | keep |
| `ENGINE_MAX_VOICES` | (default 32) the engine's voice ceiling; the Strings type uses it all, Modular / FM stop at 8. Costs ~13 KB of static RAM at 32 (instance records); plans are sized by their own voice count | keep |
| `ENGINE_WT_RAM=1` | (not set) the Strings mipmap tables in internal RAM (41 KB) instead of flash: faster reads, smaller fast heap | measure, then decide |
| `ENGINE_NO_IRAM` | (not set) keep module code in flash instead of IRAM | |

The framework appends `-Os` after the project's `-O2`, so `build_src_flags` carries `-O2 -fno-stack-protector` for the project sources. Check `compile_commands.json` if the optimization level is ever in doubt.
The host tests take the same engine flags: `.\tools\build_engine_tests.ps1 -Defs "-DENGINE_REVERB_HALF=1"` (the stereo-specific reverb and delay tests fail with `ENGINE_FX_MONO=1` by design).

### Reading the serial log

- `[BOARD]` power / amplifier pins at boot; `[BENCH]` cycles per operation (the table in ENGINE_DESIGN.md ADR-035 came from here); `[KEY]` raw key events.
- `[AUDIO] render avg A us, worst W us, budget B us per 64 frames, N blocks over budget of M, graph builds G (last: reason)`: the audio task's time to render 64 frames against the 1333 us it has. A block over budget is an audible
  dropout. A `worst` of 80-250 ms with `graph builds` rising means a knob changed the graph's shape (rebuild stall; the reason text says which cable or node).
- `[SD] N reads in T ms: X KB/s, avg A us, worst ever W us; opens, seeks, errors | [SMP] stream blocks +B, underruns U (+d)` every 2 s while samples stream (needs `HWV1_DEBUG_AUDIO`): what the card delivered and whether a sampler playhead ran dry
  (`underruns` rising = audible dropouts; avg read time near 1000 us or more = the card is the limit).
- `[PROF] cycles per block (budget 160000), total T: Name=cycles(xN) ...` per module type, summed over voices (xN = instances). `[SEC]` stage timers inside Reverb and Delay, `[OSC]` per oscillator engine, `[HEAP]` fast heap used / spilled bytes.

### The serial test tool

```
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py                                    # idle, then 1, 3 and 6 held notes
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py --chords 1,2,4,8 --hold 8 --log run.txt
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py --engines "0,1,4,6;2,3,5,7" --chords 2   # cost of each oscillator engine, per voice and sample
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py --raw --seconds 3                  # print what the board sends
```

It opens the port with DTR / RTS low (no reset). Its commands need `DEV_SERIAL_CMD` in the flashed build ("no answer to ping" otherwise), and it discards the first 2 s after each change. The notes it plays go straight to the engine and
do not appear on the UI. If two runs give identical per-engine numbers after you changed the code, the old binary is still on the board.

### Tasks and priorities

The firmware already runs on FreeRTOS (the Arduino core is built on ESP-IDF, dual core): nothing else has to be added, the work is deciding who runs where and how urgent each task is. Higher number = more urgent.

| Task | Core | Priority | Does | Why there |
| --- | --- | --- | --- | --- |
| `audio` | 1 | 23 (`configMAX_PRIORITIES - 2`) | engine render + I2S write | a missed block is a click; core 1 is otherwise idle |
| `input` | 0 | 8 | keyboard / mux / joystick scan | short and periodic, must not lose events |
| `sd_io` | 0 | 5 | card mount / scan, the sample loader | has deadlines (a dry stream is audible) but only milliseconds of CPU; sleeps 2 ticks after 8 reads in a row and during a scan (the card driver busy-waits, so without that the core-0 idle task starves and the task watchdog fires) |
| Arduino `loop()` | 0 | 1 | app, sequencer, display | no deadline: a late frame is invisible |

Rules: nothing on core 1 except `audio`; anything that waits on hardware (card, I2C) must block (DMA / semaphore), never spin; the loader holds the engine mutex while it reads, so a UI build can wait a few ms behind a card read (never the audio task).
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
  lane copy / clear / randomise helper, sending the lane position to the screen from the engine instead of `seq.pos`.
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
