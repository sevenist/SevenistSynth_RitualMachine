# Building the apps (`build.ps1`)

A quick guide to building and running the desktop program of any app in `apps/`, for any board, from the repo root.
The details are in [DEVELOPING.md](../../DEVELOPING.md) ("Quick start"); the firmware is at the end of this page.

## Before the first build

- **MSYS2 UCRT64** in `C:\msys64\ucrt64`, with gcc, g++ and SDL2. The scripts put `C:\msys64\ucrt64\bin` on PATH themselves.
- **`lib/u8g2`** (https://github.com/olikraus/u8g2, with `csrc/` and `sys/sdl/common/`). `lib/` is not versioned: copy it in.
- Optional: **`lib/minimp3`**, which the Ritual Machine needs to import `.mp3` samples.

The first build also compiles u8g2 and takes about 10 s. After that, only the files that changed are compiled (about 1.5 s for one file).

## The command

```powershell
.\build.ps1 [-App <app>] [-Board <board>] [-Clean] [-Defs "<flags>"] [app-specific options]
.\run.ps1   [-App <app>] [-Board <board>]
```

- `-App` is a folder of `apps/`: `ritual_machine` (the default), `technogen` or `template`. You can also give it first without the
  name: `.\build.ps1 technogen`.
- The root `build.ps1` only finds `apps/<app>/build.ps1` and passes every other argument on to it. Running the app's own script
  (`.\apps\technogen\build.ps1`) does the same thing.
- `run.ps1` starts the program in the app's folder, because the simulated card is the `sdcard/` folder there. **Give it the same
  `-App` and `-Board` as the build.**

## Boards (`-Board`)

The board is chosen when the program is built. The panel window draws that board, and the app sees its controls. Each board has its own
objects (`build/obj_<board>`) and its own program, so building one board never overwrites another.

| `-Board` | What it is | Screen |
| --- | --- | --- |
| `panel` | the desktop's own board: the drawing of the future prototype | 128 x 128 |
| `hwv1` | the first prototype: 8 key columns, 7 endless pots | 128 x 128 |
| `hwv2` | the second prototype, GRV-mini | 128 x 160 |

Each app has its own default board:

| App | Default board | Programs (in `apps/<app>/build/`) |
| --- | --- | --- |
| `ritual_machine` | `panel` | `oled_sim.exe`, `oled_sim_hwv1.exe`, `oled_sim_hwv2.exe` |
| `technogen` | `hwv2` | `technogen_hwv2.exe`, `technogen.exe` (panel), `technogen_hwv1.exe` |
| `template` | `panel` | `template.exe`, `template_hwv1.exe`, `template_hwv2.exe` |

## Common recipes

```powershell
# The Ritual Machine
.\build.ps1                                   # panel board
.\run.ps1
.\build.ps1 -Board hwv1; .\run.ps1 -Board hwv1

# TechnoGen (built for HWV2 by default)
.\build.ps1 -App technogen
.\run.ps1   -App technogen
.\build.ps1 -App technogen -Board panel; .\run.ps1 -App technogen -Board panel

# The template, or a new app made with tools\new_project.ps1
.\build.ps1 -App template; .\run.ps1 -App template

# Rebuild everything (deletes this board's objects only)
.\build.ps1 -App technogen -Clean

# Engine flags (framework/engine/dsp/config.h)
.\build.ps1 -Defs "-DENGINE_SR=44100"
```

When the flags change, every file is compiled again on its own (the message says "The build flags changed"), so `-Defs` no longer
needs `-Clean`.

## Options for the Ritual Machine only

| Option | What it does |
| --- | --- |
| `-NoSamples` | skips converting `samples_src/*.wav|*.mp3` to `samples/*.smp` and copying them to `sdcard/system/samples` |
| `-Sd E:` | also sets up a real TF card on that drive: its folders, plus the samples in `E:\system\samples` |
| `-Display 128x64` | builds for another screen size (any `<w>x<h>`) |
| `-Platform sim` | which `src/platform/<name>` is compiled (`sim` is the only desktop one) |

## Things to know

- **A running program can't be overwritten.** The build then writes `<name>_new.exe` next to it and says so. `run.ps1` starts the
  newer of the two, and closing the window gets the normal name back.
- **After a change, build the board you run.** `run.ps1` warns you when the program it starts is older than another board's build of
  the same app.
- **"Nothing built for '<app>' on the board '<board>' yet"** means `run.ps1` found no program for that pair. Build it with the same
  `-App` and `-Board`.
- **Tests** aren't built by `build.ps1`. Run `.\test.ps1` (`-Matrix` runs every sample-rate and block-size configuration).

## The firmware (not `build.ps1`)

The ESP32 firmware is built by PlatformIO from the app's `platformio.ini`. Build it from VS Code with the app's folder open, or with
`.\upload.ps1 -App <app>` (`-BuildOnly` flashes nothing, `-Port COM8` picks the port, `-Env <env>` picks another environment).
`upload.ps1` always calls PlatformIO's own `C:\.platformio\penv\Scripts\pio.exe`, never a bare `pio`. See DEVELOPING.md, "ESP32
firmware".
