# TechnoGen

A techno loop generator groovebox on the SevenSynthCore library, made for the second prototype (HWV2, GRV-mini: 128 x 160 colour
screen, 7 pots and a volume pot, two encoders, 4 function keys over 16 keys, a joystick, three buttons). Seven tracks, each with one
fixed sound engine, sixteen steps, and a dice that rolls a track's sound and steps from a seed:

| Track | Engine (the design) | Lanes |
| --- | --- | --- |
| T1 KICK | body, click, drive, rumble | 1 |
| T2 BASS | acid, reese, FM, fold | 1 |
| T3 DRUM | snare, clap, rim, hat, cymbal, tom | 6 |
| T4 PERC | modal, rust, byte, clank | 4 |
| T5 PAD | paraphonic strings and ensemble | 1 |
| T6 TEXT | dust, velvet, LFSR, tape | 1 |
| T7 SAMP | slices, scan, spectral freeze | 1 |

The design, the decisions and what is still to build: [docs/DESIGN.md](docs/DESIGN.md), ADR-048. What changed:
[CHANGELOG.md](CHANGELOG.md); the state and the todo: [CONTINUE.md](CONTINUE.md).

**Status (2026-11-08): the first version, in the simulator.** The screens and every control of the panel work; **the sounds are
placeholders** (a few lines each, not tuned, not measured on the board). Not built yet: the real engines, the joystick (MORPH, TENSE),
the PERF page's actions, the master chain, saving, the line input, MIDI. The firmware has not been built with PlatformIO nor flashed.

## Build and run

From the repo root:

```powershell
.\build.ps1 -App technogen              # the desktop program, drawn as HWV2 (needs MSYS2 UCRT64 with gcc, g++ and SDL2, and lib/u8g2)
.\run.ps1 -App technogen                # two windows: the screen, and the panel (its controls work with the mouse)
.\apps\technogen\tools\build_tests.ps1  # its host tests (also run by .\test.ps1); the screens are written to build/*_view.png
```

The firmware: open `oled_sim.code-workspace` in VS Code, pick the folder `technogen` in the PlatformIO status bar, build and upload
(environment `hwv2`, the only one). From a shell, only `C:\.platformio\penv\Scripts\pio.exe run -d apps/technogen`, never a bare `pio`.

## The controls

| Control | What it does |
| --- | --- |
| PLAY | play / stop |
| Encoder A | the track; **SHFT +**: its lane. **Push**: the seven tracks <-> the selected track's engine (its sound as a picture, its seven values as bars) |
| Encoder B | the dice's depth: TIGHT (nudges the values, keeps the steps), RAW (the sweet spots), WEIRD (the edges); **SHFT +**: the tempo. **Push**: the next pattern, at the end of the bar |
| Function keys | STEP, TRK, PERF: what the 16 keys are. SHFT: held, or tapped alone to latch it |
| 16 keys, STEP | the selected track's steps on its lane: off -> hit -> accent. **SHFT + a key**: the lane |
| 16 keys, TRK | top row and a half: select tracks 1-7, LOAD (not yet); the rest: mute tracks 1-7, ALL (unmute) |
| 16 keys, PERF | PRINT, TAKE, FILL, EVOLVE, ROLL 1/4..1/32 (not yet); the bottom 8: patterns A1..A8 |
| B1 MOD | held, or tapped alone to latch it. **MOD + a knob** (engine view): locks or unlocks that value against the dice |
| B2 DICE | rolls the selected track at the depth; **SHFT +**: GEN, all seven |
| B3 BACK | undoes a roll (16 are kept) |
| K1..K4, R1..R3 | the main view: the tracks' levels; the engine view: the selected track's seven values. A knob takes over once it reaches the value (meanwhile the footer shows where it is, in brackets) |
| VOLUME | the master volume |
| Joystick | not yet (X MORPH, Y TENSE) |

The LEDs: the keys show the page (the steps in the track's colour, the playhead in white), the knobs their track's colour (dim until
they have caught their value; white when locked).

## What is here

| File | What it is |
| --- | --- |
| `src/tg_model.h`, `.cpp` | The data: the seven tracks (names, colours, values, lanes), the patterns, the dice (seeded rolls, sweet spots, locks, rules), BACK. |
| `src/tg_tracks.h`, `.cpp` | The track module for the engine: one per track, plays its own hits, adds its sound to the chain. Placeholder sounds. |
| `src/synth.h`, `.cpp` | The sound: the graph (seven tracks chained into the master, loaded once), the pattern as the sequencer's hits, the clock. |
| `src/tg_app.h`, `src/app.cpp` | What the controls do, the screen (drawn in colour on `ui/canvas.h`) and the LEDs. |
| `src/main_sim.cpp`, `src/main_esp32.cpp` | The desktop and the firmware: memory, the loop, the board's start-up. |
| `tests/` | The host tests (`tools/build_tests.ps1`): the dice, the synth with the engine, the controls and the screen. |
| `build.ps1`, `platformio.ini` | The desktop build (default board: hwv2) and the firmware build for HWV2. |
