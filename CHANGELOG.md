# Update log

## beta 1.1 (2026-10-13)

The second beta of the Ritual Machine. The flashable image and the simulator are on the release page, with what was tested
on the hardware.

- **The synth's step sequencer** now plays in the engine, on the machine's clock. A step has a note, a length, a velocity,
  a probability, a ratchet and a chord of up to 4 notes. A pattern has up to 64 steps, a rate (1/8 to 1/32, triplets) and
  a direction (forward, reverse, ping-pong, random). Edit tools: copy, paste, clear, shift, reverse, double, random notes,
  humanize, random probability, undo. On the SEQUENCER page the four column knobs set the note, length, velocity and
  probability of the step under the cursor. Shift + Play records: step by step when stopped, live when running.
- **Patterns.** 8 patterns and a chain of 16 for the synth, on the new PATTERNS page. They are saved on the card by
  themselves, not in presets.
- **A Velocity module (VL)** for the rack: the played velocity as a modulation source.
- **A sequencer for the Pads section.** One pattern covers all 64 pads. A hit has a level, a probability, a ratchet and,
  for gate pads, a length. Select + Play (or Shift + Play) opens step mode, where the pad keys are the selected pad's
  steps. Select + Rec records live. 8 patterns and a chain.
- **The simulator draws either board.** The release holds two programs: the first prototype (8 columns of keys, 7 knobs)
  and the next panel (4 columns, encoders).
- **For developers:** the repo is now a library and the apps built on it. The next section is the guide to it.

## 2026-10-13: the guides moved to `docs/guides/`

The folder `human_docs/` is now `docs/guides/`, and its content follows the split into a library and its apps.

| Guide | What it is |
| --- | --- |
| `docs/guides/DRAWING_KIT.md` | New. Putting something on the screen from any app, with the library's `ui/gui.h` and `ui/popup.h` |
| `docs/guides/RITUAL_MACHINE_UI.md` | Replaces `UI_GUIDE.md`. How the Ritual Machine's screens are built: every menu tab, the Pads section, the browser |
| `docs/guides/SERIAL_COMMANDS.md` | Moved and brought up to date: working links, the `mi` command, how to make a measuring build |
| `docs/guides/README.md` | The list of guides |

- `human_docs/ui_positioning_map.html` is gone. The weak point it described (the joystick's navigation and the layout are
  not tied together) is item 2 of "What is still open" in `docs/guides/RITUAL_MACHINE_UI.md`.
- A link or a note that says `human_docs/...` or `UI_GUIDE.md` is stale: the files in this repo were updated.

## 2026-10-13: the repo is a library and its apps

Commits `ffaa378` to `0ab1696` ("app separation", phase 1 to step 2c).

Until now this repo was one program, the Ritual Machine. It is now a library, `framework/`, and the apps built on it,
`apps/`. The Ritual Machine is the first app. A new synth starts from a template with one command, runs on the desktop
and on the first prototype from the same source, and copies nothing from the library.

### Start your own synth

```powershell
.\tools\new_project.ps1 my_synth       # makes apps/my_synth from the template
.\build.ps1 -App my_synth              # its desktop program
.\run.ps1 -App my_synth                # two windows: the screen, and the board's front panel
C:\.platformio\penv\Scripts\pio.exe run -d apps/my_synth -t upload     # firmware for the first prototype
```

The template is a four-voice synth (saw oscillator, low-pass filter, one envelope) with a screen and the board's keys.
What you edit:

| File | What it holds |
| --- | --- |
| `src/synth.cpp` | Your sound: which engine modules, how they are cabled, the values a control can move |
| `src/app.cpp` | What each control does and what the screen shows |
| `src/main_sim.cpp`, `src/main_esp32.cpp` | The two entry points: memory for the engine and the main loop |
| `build.ps1`, `platformio.ini` | The desktop build and the firmware build |

`apps/my_synth/README.md` says where to change things: another sound, more voices, a value on a control, another
board.

### What the library gives an app

| Part | Include as | What you get |
| --- | --- | --- |
| The DSP engine | `engine/...` | The modular graph: oscillators, filters, envelopes, effects, samplers. It includes nothing from an app |
| The board interface | `hal/...` | Display, controls, key LEDs, small files on the card, battery, large memory, audio output |
| The drawing kit | `ui/gui.h`, `ui/popup.h` | Layout rectangles, text, fields, sprites and popups on a u8g2 display |
| The first prototype | `boards/hwv1/` | Its drivers behind the board interface |
| The desktop | `boards/desktop/` | The same interface on a PC: SDL windows, keyboard, mouse, audio, the card as a folder |

Three things work differently from a classic "one firmware" layout:

- **Audio is a callback.** The library owns the output; your app owns its engine and hands over one function,
  `audio_out_start(synth_render, sample_rate)`. The board calls it for every buffer.
- **Each board lists its own controls.** `input_controls()` returns a table: for each control its name, its kind (key,
  button, encoder, pot without end stops, knob, joystick axis), its place in the key matrix, its place on the panel,
  and whether it has an LED. An event says "control number N was pressed, turned or moved". An app that reads only
  the kind and the place, as the template does, runs on any board with no change.
- **An app that wants named controls keeps its own list and a map per board.** The Ritual Machine does this: its
  roles (Enc A, column knob 1, key row.column ...) are in `core/controls.h`, and one small file per board says which
  board control plays which role (`core/boards/map_hwv1.h`, `map_panel.h`). Swapping two pots is one row of the map.

### Try an app as either board on the desktop

The simulator's panel window is now drawn from a board's control table, so the desktop program can be built as any
board the library knows:

```powershell
.\build.ps1                  # the panel: the future prototype (4 key columns, 2 encoders, 8 knobs, joystick)
.\run.ps1
.\build.ps1 -Board hwv1      # the first prototype (4 function keys, 4 x 8 keys, 7 pots without end stops, joystick)
.\run.ps1 -Board hwv1
```

- Each board is its own program (`oled_sim.exe`, `oled_sim_hwv1.exe`) with its own objects. After a change, build the
  board you run; `run.ps1` prints a note when the program it starts is the older of the two.
- `-App` and `-Board` combine: `.\build.ps1 -App my_synth -Board hwv1`.
- The app sees the simulated board exactly as it sees the real one: the same table, the same board name.
- Every part works with the mouse (click, wheel, drag; right click on the joystick is its push). Each part shows the PC
  keys that work it. Keys are given by a part's kind and place: matrix keys take the block `F1`.. / `1`.. / `Q`.. /
  `A`.. / `Z`.., what turns takes a pair each starting with `[ ]` and `; '`, the joystick takes the arrows.

### Add a board

1. A folder in `framework/boards/` with the board interface behind it, its control table, and its name
   (`input_board_name()`).
2. In the app's `platformio.ini`, the folder in `build_src_filter`.
3. To draw it on the desktop: a branch in `framework/boards/desktop/sim_board.h` and an entry in `Get-SimBoard`
   (`framework/tools/sim_build.ps1`). The panel window needs nothing else: it draws each line of the table at its place.
4. For an app with its own roles: a map file for the board (the Ritual Machine: `core/boards/map_<board>.h` and a line
   in `core/controls.c`). Without a map, the board's matrix keys still play.

### What changed for existing work

- **Paths.** The Ritual Machine's `src/`, `tests/`, `tools/`, `assets/`, `sdcard/`, `samples_src/` and `platformio.ini`
  are now in `apps/ritual_machine/`. There is no `platformio.ini` at the repo root: build the firmware with
  `C:\.platformio\penv\Scripts\pio.exe run -d apps/ritual_machine`. In VS Code, open `oled_sim.code-workspace` so the
  PlatformIO extension finds each app.
- **Commands.** `.\build.ps1`, `.\run.ps1` and `.\test.ps1` stay at the root and default to the Ritual Machine.
  `.\test.ps1` runs the library's tests, then the app's engine and UI tests.
- **The board interface.** `hal_input.h` has no shared control list any more: events and LEDs carry the board's own
  control ids. `hal_audio.h` is gone from the library: the output is `hal_audio_out.h`, and the Ritual Machine's engine
  calls are its own `core/audio.h`.
- **The card.** Each app declares its own folders; the library creates what is declared and knows no layout.
- **The simulator's panel.** It has 4 key columns, like the board it draws. The PC keys `F5`-`F8`, `5`-`8`, `T`-`I`,
  `G`-`K` and `B`-`,` do nothing there any more: use `-Board hwv1` for 8 columns. The octave is Shift + joystick
  up / down. The joystick's push is a right click on the stick, or Right Ctrl.
- **Saved settings are untouched.** The Ritual Machine's roles kept their values, so `ui.cfg`, `keys.cfg`, bindings,
  key layouts and presets read as before.

### Limits today

- The panel places of the first prototype's pots and joystick are estimated, not measured. The drawing with
  `-Board hwv1` shows the right parts, not yet their exact positions (`framework/boards/hwv1/hwv1_controls.c`).
- Still the Ritual Machine's, not the library's: its declarative screens, bindings, key layouts and layers, and its
  sample library. Another app that wants samples writes its own "find the file, hand it to the sampler".
- Apps do not share a sample folder on the card. Two firmwares used with one card can write the same file names.
- The simulated pots without end stops report clean steps or positions; the real board's noise thresholds are not
  simulated.

More: `DEVELOPING.md` (how to work in the code), `apps/template/README.md` (the starter app), `docs/guides/` (the drawing
kit, the Ritual Machine's screens, the serial commands), `ENGINE_DESIGN.md` ADR-043 (the decisions behind the split).
