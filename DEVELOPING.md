# Developer guide: the repo

This repo is a library, **the framework** (`framework/`), and the apps built on it (`apps/`). Each part has its own documents,
next to its code; this guide is what they share: the layout, the requirements, the builds and the firmware's toolchain.

| Part | Start with | Then |
| --- | --- | --- |
| the framework | [framework/README.md](framework/README.md) | [docs/DESIGN.md](framework/docs/DESIGN.md) (the decisions, the index of every ADR), [docs/BOARDS.md](framework/docs/BOARDS.md) (the boards, the ESP32 firmware), [docs/DRAWING_KIT.md](framework/docs/DRAWING_KIT.md), [CHANGELOG.md](framework/CHANGELOG.md) |
| the Ritual Machine | [apps/ritual_machine/README.md](apps/ritual_machine/README.md) | [docs/DEVELOPING.md](apps/ritual_machine/docs/DEVELOPING.md), [docs/UI.md](apps/ritual_machine/docs/UI.md), [docs/SERIAL_COMMANDS.md](apps/ritual_machine/docs/SERIAL_COMMANDS.md), [docs/UX_REORGANIZATION.md](apps/ritual_machine/docs/UX_REORGANIZATION.md), [docs/DESIGN.md](apps/ritual_machine/docs/DESIGN.md), [CHANGELOG.md](apps/ritual_machine/CHANGELOG.md), [CONTINUE.md](apps/ritual_machine/CONTINUE.md) |
| TechnoGen | [apps/technogen/README.md](apps/technogen/README.md) | [docs/DESIGN.md](apps/technogen/docs/DESIGN.md), [CHANGELOG.md](apps/technogen/CHANGELOG.md), [CONTINUE.md](apps/technogen/CONTINUE.md) |
| SynthLab | [apps/synthlab/README.md](apps/synthlab/README.md) | [docs/SYNTHS.md](apps/synthlab/docs/SYNTHS.md), [CHANGELOG.md](apps/synthlab/CHANGELOG.md), [CONTINUE.md](apps/synthlab/CONTINUE.md) |
| the template | [apps/template/README.md](apps/template/README.md) | |

For the whole repo: [BUILDING.md](BUILDING.md) (building and running any app's desktop program), [CONTINUE.md](CONTINUE.md) (the
library's state and todo, the working style, the traps), [GLOSSARY.md](GLOSSARY.md) (the words), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## The repo: a library and its apps (framework/docs/DESIGN.md ADR-043)

```
oled_sim/
├─ framework/            THE LIBRARY, shared by every app (README.md: its guide)
│  ├─ engine/               the DSP engine (C++17; includes nothing from an app). Included as "engine/..."
│  ├─ hal/                  what a board gives an app: display, controls, key LEDs, the card's small files, power, large memory,
│  │                        and the audio output (the app hands over its render function). Headers only. "hal/..."
│  ├─ ui/                   the drawing kit over u8g2: style, layout, text, sprites, animations (gui), popups, and the colour canvas
│  │                        (canvas: u8g2 and the kit painting RGB565, display_send_rgb565; apps/technogen/docs/DESIGN.md ADR-048). "ui/..."
│  ├─ boards/esp32/         what the ESP32 boards share: the pin blocks of every revision, the board services' interface, the display
│  │                        task and boot animation, the LED driver, the I2S output, the mux, the input queue, the soft power and the
│  │                        battery, the card's interface, the card task and the sample storage on it. "boards/esp32/..."
│  ├─ boards/hwv1/          the first prototype: its panel, key matrix, knobs, joystick, LED order, card driver, power. "boards/hwv1/..."
│  ├─ boards/hwv2/          the second prototype (GRV-mini): its screen, control table and scan, LED order and VU meter, SDMMC card,
│  │                        how its power switch reads. "boards/hwv2/..."
│  ├─ boards/desktop/       the desktop: the display and panel windows, the PC keyboard and mouse as controls, SDL audio,
│  │                        the card as the folder sdcard/. "boards/desktop/..."
│  ├─ host/                 desktop-only helpers (a slow-card model for tests and simulators, the .wav / .mp3 converter). "host/..."
│  ├─ tests/                the library's own tests: the engine alone
│  ├─ tools/                table generators, sample tools, the shared desktop build (sim_build.ps1), build_engine_tests.ps1
│  ├─ docs/                 its documents: DESIGN.md (the decisions, the index of every ADR), BOARDS.md, DRAWING_KIT.md
│  └─ CHANGELOG.md          what changed in the library and the repo
├─ apps/
│  ├─ ritual_machine/       THE RITUAL MACHINE: its own src/ (core, app, platform), tests/, tools/, assets/, build.ps1,
│  │                        platformio.ini, the simulator's card (sdcard/, samples/, samples_src/), and its documents: README.md,
│  │                        CHANGELOG.md, CONTINUE.md, docs/ (DEVELOPING.md, UI.md, UX_REORGANIZATION.md, SERIAL_COMMANDS.md, DESIGN.md)
│  ├─ template/             the starter app: a four-voice synth with a screen and the board's keys, desktop + first prototype (its README.md)
│  ├─ technogen/            TECHNOGEN: a techno loop generator for the second prototype, seven tracks and a dice, in colour
│  │                        (README.md, CHANGELOG.md, CONTINUE.md, docs/DESIGN.md)
│  └─ synthlab/             SYNTHLAB: a workbench for home-made synths, one file each (src/synths/), for the second prototype
│                           (README.md, CHANGELOG.md, CONTINUE.md, docs/SYNTHS.md: the synths' design notebook)
├─ tools/new_project.ps1    .\tools\new_project.ps1 my_synth  ->  apps/my_synth, a copy of the template under its own name
├─ docs/images/             the pictures of the repo's README.md
├─ build.ps1, run.ps1       build / run an app's desktop program from here (-App name; default: ritual_machine; BUILDING.md)
├─ test.ps1                 every host test: the library's, then the Ritual Machine's engine and UI tests, TechnoGen's, SynthLab's
├─ README.md, DEVELOPING.md, BUILDING.md, CONTINUE.md, GLOSSARY.md, THIRD_PARTY_NOTICES.md   the repo's documents
└─ lib/                     third-party code for the desktop builds (u8g2, minimp3, amy; not versioned)
```

**In the Ritual Machine's guide (`apps/ritual_machine/docs/DEVELOPING.md`) a path without a prefix (`src/core/...`, `tools/...`, `tests/...`, `samples_src/`, `sdcard/`,
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
in its guide); step 2c (the same day) draws the simulator's panel window from a board's table, so the desktop program can be built as
either board: `.\build.ps1 -Board hwv1` (`framework/docs/BOARDS.md`, "The desktop board"). Still the Ritual Machine's: the declarative screens, the
bindings, key layouts and layers, the sample library.

## Requirements

MSYS2 UCRT64 (`C:\msys64\ucrt64`) with gcc, g++ and SDL2, and Python 3 for the generator tools.
`lib/` is not versioned. `lib/u8g2` (https://github.com/olikraus/u8g2, needs `csrc/` and `sys/sdl/common/`) is required; `lib/minimp3` (`minimp3.h`, `minimp3_ex.h` from
https://github.com/lieff/minimp3, public domain) enables the mp3 import of the simulator's sample folder (optional);
`lib/amy` (`git clone --depth 1 https://github.com/shorepine/amy.git lib/amy`) is only read by `apps/ritual_machine/tools/gen_dx7.py` to
regenerate the DX7 tables.

## Tests and firmware

`.\test.ps1` runs every host test (the library's, then each app's; `-Matrix` for the six sample-rate / block-size configurations).
An app's firmware is built by PlatformIO from its own `platformio.ini`, from VS Code or with `.\upload.ps1 -App <app>`, **never with
a bare `pio`**: [framework/docs/BOARDS.md](framework/docs/BOARDS.md), "ESP32 firmware and performance work".
