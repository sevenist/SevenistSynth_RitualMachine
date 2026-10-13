# Starter app

The smallest complete project on the SevenSynthCore library: a four-voice synth (saw oscillator, low-pass filter, one envelope)
with a screen and the board's keys, on the desktop and on the first prototype. Copy it to start your own:

```powershell
.\tools\new_project.ps1 my_synth        # from the repo root: makes apps/my_synth
```

## Build and run

From the repo root:

```powershell
.\build.ps1 -App template               # desktop program (needs MSYS2 UCRT64 with gcc, g++ and SDL2, and lib/u8g2: DEVELOPING.md, "Requirements")
.\run.ps1 -App template                 # two windows: the screen, and the panel (its controls work with the mouse)
.\build.ps1 -App template -Board hwv1   # the same program as the first prototype: the panel window draws that board (8 key columns, 7 pots)
.\run.ps1 -App template -Board hwv1     # ... its own program, build/template_hwv1.exe
C:\.platformio\penv\Scripts\pio.exe run -d apps/template               # firmware for the first prototype (HWV1)
C:\.platformio\penv\Scripts\pio.exe run -d apps/template -t upload     # flash it
```

The controls, the same on both:

| Control | On the desktop | What it does |
| --- | --- | --- |
| The note rows | the PC keyboard's rows `1`.., `Q`.., `A`.., `Z`.. (by position; as many keys as the board has columns: 4 on the panel, 8 with `-Board hwv1`) | notes: bottom left = C3, +1 per key to the right, +8 per row up |
| Function keys 1 / 2 | `F1` / `F2` | octave down / up |
| Function keys 3 / 4 | `F3` / `F4` | filter cutoff down / up |
| Anything that turns in steps: the two encoders of the panel, the seven pots of the first prototype | `[` / `]` and `;` / `'` (by position), then one pair per further pot: the panel window writes each part's keys under it | filter cutoff |

Esc or closing a window quits the desktop program.

The firmware builds, but has not run on the board in this form: its display, keys, power and audio output are now the library's
board code (`framework/boards/hwv1`), which runs there under the Ritual Machine. The output level is 25 % (`DEV_OUTPUT_GAIN_PCT`
in `platformio.ini`): the prototype is loud on headphones.

## What is here

| File | What it is |
| --- | --- |
| `src/synth.h`, `src/synth.cpp` | Your synth, portable: which engine modules it uses, how they are cabled, the values a control can move. Start here. |
| `src/app.h`, `src/app.cpp` | What the controls do and what the screen shows, portable too. |
| `src/main_sim.cpp` | The desktop side: the synth's memory and the loop. |
| `src/main_esp32.cpp` | The firmware side: the same, with the board's start-up. |
| `build.ps1` | The desktop build. The compile and link steps are shared: `framework/tools/sim_build.ps1`. |
| `platformio.ini` | The firmware build: board, flags, libraries. |

Nothing in this folder is copied from the library (`framework/`): you include its headers and both builds compile its sources
next to yours.

| From the library | What it gives |
| --- | --- |
| `engine/...` | The DSP engine (`#include "engine/core/engine.h"`). |
| `hal/hal_display.h`, `hal/hal_input.h` | The screen (a u8g2 instance), and the controls: each board lists its own in a table, an event names one of them. |
| `hal/hal_audio_out.h` | The audio output: you hand it `synth_render`, the board calls it for every buffer. |
| `hal/hal_leds.h`, `hal/hal_storage.h`, `hal/hal_power.h` | The key LEDs, small files on the card, the battery. Not used by this app. |
| `ui/gui.h` | The drawing kit: rectangles to lay a screen out, text, fields, sprites. |
| `boards/hwv1`, `boards/desktop` | The two boards behind those headers. |

## Where to change things

- **Another sound**: `build_graph()` in `src/synth.cpp`. A node is one engine module, a cable goes from an output to an input or
  to a parameter, with a depth. The modules and their parameters are listed in the headers of `framework/engine/modules`
  (`synth_modules.h`: oscillator, envelopes, LFO, filter, shaper; `fx_modules.h` and `fx2_modules.h`: the effects;
  `osc_engines.h`, `mi_osc.h`: more oscillators; `sampler_modules.h`: samples).
- **A module family you do not see yet**: register it in `synth_init()` next to `register_synth_modules` (for example
  `register_fx_modules(eng.registry())` with `#include "engine/modules/fx_modules.h"`).
- **A value a control moves**: add a function like `synth_set_cutoff()`. `eng.set_param(node, index, value)` is a live write:
  no reload, no click. Then give it a control in `app_event()` and a row in `app_draw()` (`src/app.cpp`).
- **A change of structure while it runs** (a module added, a cable moved): build a new graph and call `eng.load()` again.
  Modules that keep their node id and type keep their state.
- **More voices**: `kVoices` in `src/synth.cpp`. Memory and CPU grow with it.
- **What a control does**: `app_event()` in `src/app.cpp`. It reads the board's table (`input_controls()`, `hal/hal_input.h`): the
  kind of the control that spoke and, for a key, its place in the matrix, so it runs on any board. To give one particular control
  a job, compare `e.ctl` with its id from the board's header (`boards/hwv1/hwv1_controls.h`, `boards/desktop/panel_controls.h`).
- **Another board**: a folder in `framework/boards` with the HAL behind it and its control table, added to `build_src_filter` in
  `platformio.ini`. The desktop program can be built as that board too (the panel window is drawn from the board's table): a
  branch in `framework/boards/desktop/sim_board.h` and an entry in `Get-SimBoard` (`framework/tools/sim_build.ps1`).

## Three rules the engine relies on

1. `synth_render()` is the only call the audio thread makes. It never allocates, locks or prints.
2. Everything else (`synth_note_on`, the setters, `synth_update`) is called from one other thread. Those calls post commands
   that the audio thread applies at the start of its next block.
3. Parameters are integers in the module's own units: pitch and cutoff in semitones x 256, levels in q15 (32767 = 1.0),
   times in ms. The headers say which.

The words used here (node, cable, plan, block, fast and bulk heap) are defined in the framework glossary; the engine's design
and its measurements on the chip are in `ENGINE_DESIGN.md`, the Ritual Machine's guide in `DEVELOPING.md`.
