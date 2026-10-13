# Starter app

The smallest complete project on the SevenSynthCore engine: a four-voice synth (saw oscillator, low-pass filter, one envelope)
that runs on the desktop and on the first prototype. Copy it to start your own:

```powershell
.\tools\new_project.ps1 my_synth        # from the repo root: makes apps/my_synth
```

## Build and run

From the repo root:

```powershell
.\build.ps1 -App template               # desktop program (needs MSYS2 UCRT64 with gcc, g++ and SDL2: DEVELOPING.md, "Requirements")
.\run.ps1 -App template                 # A W S E D F T G Y H U J K = notes, Z X = octave, arrows = cutoff, Esc = quit
C:\.platformio\penv\Scripts\pio.exe run -d apps/template               # firmware for the first prototype (HWV1)
C:\.platformio\penv\Scripts\pio.exe run -d apps/template -t upload     # flash it
```

The firmware has run on the first prototype (2026-10-13). Its I2S, DAC, power and key code follow the Ritual Machine's
drivers. The output level is set to 25 % in `src/main_esp32.cpp` (`kOutGainPct`): the prototype is loud on
headphones.

## What is here

| File | What it is |
| --- | --- |
| `src/synth.h`, `src/synth.cpp` | Your synth, portable: which engine modules it uses, how they are cabled, the values a control can move. Start here. |
| `src/main_sim.cpp` | The desktop side: an SDL window for the keyboard, SDL audio. |
| `src/main_esp32.cpp` | The firmware side: the audio task on core 1, the key matrix, the power button. |
| `src/board_pins.h` | The pins the firmware uses, per hardware revision. |
| `src/kbd_tca8418.*` | The prototype's key matrix driver (a copy of the Ritual Machine's). |
| `build.ps1` | The desktop build. The compile and link steps are shared: `framework/tools/sim_build.ps1`. |
| `platformio.ini` | The firmware build: board, flags, libraries. |

The engine itself is `framework/engine`, shared by every app. Nothing in this folder is copied from it: you include its headers
(`#include "engine/core/engine.h"`) and both builds compile its sources next to yours.

## Where to change things

- **Another sound**: `build_graph()` in `src/synth.cpp`. A node is one engine module, a cable goes from an output to an input or
  to a parameter, with a depth. The modules and their parameters are listed in the headers of `framework/engine/modules`
  (`synth_modules.h`: oscillator, envelopes, LFO, filter, shaper; `fx_modules.h` and `fx2_modules.h`: the effects;
  `osc_engines.h`, `mi_osc.h`: more oscillators; `sampler_modules.h`: samples).
- **A module family you do not see yet**: register it in `synth_init()` next to `register_synth_modules` (for example
  `register_fx_modules(eng.registry())` with `#include "engine/modules/fx_modules.h"`).
- **A value a control moves**: add a function like `synth_set_cutoff()`. `eng.set_param(node, index, value)` is a live write:
  no reload, no click.
- **A change of structure while it runs** (a module added, a cable moved): build a new graph and call `eng.load()` again.
  Modules that keep their node id and type keep their state.
- **More voices**: `kVoices` in `src/synth.cpp`. Memory and CPU grow with it.
- **Another board**: a new block in `src/board_pins.h`, and what differs in `src/main_esp32.cpp`.

## Three rules the engine relies on

1. `synth_render()` is the only call the audio thread makes. It never allocates, locks or prints.
2. Everything else (`synth_note_on`, the setters, `synth_update`) is called from one other thread. Those calls post commands
   that the audio thread applies at the start of its next block.
3. Parameters are integers in the module's own units: pitch and cutoff in semitones x 256, levels in q15 (32767 = 1.0),
   times in ms. The headers say which.

The words used here (node, cable, plan, block, fast and bulk heap) are defined in the framework glossary; the engine's design
and its measurements on the chip are in `ENGINE_DESIGN.md`, the Ritual Machine's guide in `DEVELOPING.md`.
