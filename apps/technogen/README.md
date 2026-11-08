# TechnoGen

A techno loop generator groovebox on the SevenSynthCore library, made for the second prototype (HWV2, GRV-mini: 128 x 160 colour
screen, 7 pots and a volume pot, two encoders, 4 function keys over 16 keys, a joystick, three buttons). Seven tracks, each with one
fixed sound engine, and a dice that rolls a track's sound and pattern from a seed:

| Track | Engine |
| --- | --- |
| T1 KICK | body, click, drive, rumble |
| T2 BASS | acid, reese, FM, fold |
| T3 DRUM | 6 lanes: snare, clap, rim, hats, cymbal, tom |
| T4 PERC | modal, rust, byte, clank |
| T5 PAD | paraphonic strings and ensemble |
| T6 TEXT | dust, velvet, LFSR, tape |
| T7 SAMP | slices, scan, spectral freeze |

The design (the main view, the signal and core map, the ideas) and the decisions are in `ENGINE_DESIGN.md` ADR-048 at the repo root.

**Status:** being built. Step 0: this folder, made from the template, building for HWV2 and for the simulator drawn as HWV2.

## Build and run

From the repo root:

```powershell
.\build.ps1 -App technogen              # the desktop program, drawn as HWV2 (needs MSYS2 UCRT64 with gcc, g++ and SDL2, and lib/u8g2)
.\run.ps1 -App technogen                # two windows: the screen, and the panel (its controls work with the mouse)
```

The firmware: open `oled_sim.code-workspace` in VS Code, pick the folder `technogen` in the PlatformIO status bar, build and upload
(environment `hwv2`, the only one). From a shell, only `C:\.platformio\penv\Scripts\pio.exe run -d apps/technogen`, never a bare `pio`.

## What is here

| File | What it is |
| --- | --- |
| `src/synth.h`, `src/synth.cpp` | The sound: which engine modules it uses and how they are cabled. |
| `src/app.h`, `src/app.cpp` | What the controls do and what the screen shows. |
| `src/main_sim.cpp` | The desktop side: the synth's memory and the loop. |
| `src/main_esp32.cpp` | The firmware side: the same, with the board's start-up. |
| `build.ps1` | The desktop build (default board: hwv2). The compile and link steps are shared: `framework/tools/sim_build.ps1`. |
| `platformio.ini` | The firmware build for HWV2: board, flags, libraries. |
