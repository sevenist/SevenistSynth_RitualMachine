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

## 2026-11-04: the GUI reorganization, part 4: LED colours (ADR-046 phase 6; branch `gui-reorg`)

For boards with LEDs under the pots and encoder A:

- **Macro 1 is Blue by default**, because red means recording. A colour you chose in the MACROS tab stays.
- **Encoder A is Mint in Pads**, because Amber is the column knobs' colour. It can be changed in the LEDS tab.

## 2026-10-30: the GUI reorganization, part 3: nothing out of reach (ADR-046 phase 5; branch `gui-reorg`)

- **Shift + Play in Pads opens a Rec list**: Capture (until you stop it), Capture 1, 2, 4 or 8 bars, Pattern rec, Steps on / off
  and Click. A board without the Rec, Select or Click keys can now do all of it. While something records, Shift + Play stops it.
- **Click (the metronome)** is also the last entry of the synth's Rec list.
- **MACROS has a Value row**: play any macro from the screen, including the ones no knob plays (macros 3 to 8 on the first
  prototype). 5 % a step, Shift for 1 %.
- **Reset to default**: with a value latched, **Shift + push** puts it back to its default. "Reset" is also an action you can put
  on any key or Shift / Mod entry. **Every module now has a cog**; its last row, Reset, puts the whole module back to its
  defaults after asking.
- In Pads, **Shift + joystick left / right** on the grid changes the bank. Tap and BPM can go on any Shift / Mod entry.

## 2026-10-29: the GUI reorganization, part 2: views (ADR-046 phase 3; branch `gui-reorg`)

- **Views**: the screens are grouped by what they are for, in the same order in Synth and Pads: EDIT (the sound's pages; Pads:
  the grid), SEQ (the sequencer's three pages; Pads: the steps), RACK (rack and sound; Pads: the FX chains), PERFORM (macros and
  the joystick), PRESETS, SETUP (keys, LEDs, modifiers, curves, and the new MACHINE page). The badge at the left of the header
  ("ED", "SQ" ...) is the view: push the joystick up from the page name to reach it, then left / right to change view.
- **Left / right on the page name** now stays in the view: the EDIT pages no longer run into the sequencer's.
- **Menu** goes between EDIT and the last other view (at first RACK; in Pads the FX chains, as before). **Back** climbs: from a
  row to the header, from the header home.
- **MACHINE** (in SETUP): Out, Spk and Knob moved here from SOUND, with Storage and the battery.
- **In Pads, PERFORM and SETUP open over the pads**: the pads keep playing while you edit a macro or a setting.
- **Shift + Menu** is now a map with one heading per view; **Menu held** lists the pages of the view on show.

## 2026-10-29: the GUI reorganization, part 1 (ADR-046; branch `gui-reorg`)

- **A knob legend** at the bottom of the screen: what each column knob drives, laid out like the knobs (2 x 2 on the first
  prototype), with a bar of the value and, until the knob has caught it, a tick where the knob stands. Hold Shift or Mod and it
  shows that layer's targets. In the Pads section: Start, End, Tune, Cut.
- **The header** shows the battery, the page's place ("3/22"), "S" / "M" while a modifier is held, and REC (framed: armed,
  filled: recording).
- **Push = Enter** on the synth's pages: a push latches the value (up / down then change it by 4 steps), flips an on / off
  row, opens the cog. It no longer steps the value up. **Hold the push half a second** to bind the row to the joystick's XY.
- **Back never deletes**: on RACK it closes the cell's menu (Delete is there), then goes up to the header, then leaves the
  menu. A "*" in the menu's title marks rack edits waiting; "Applied" says when they are.

## 2026-10-29: LEDs under the pots and encoder A, for the next prototype (ADR-045)

- **The next prototype's pots and encoder A light up** (the simulator draws them as rings). A column knob is dim until it
  has caught the value it drives, bright after, and its colour gets paler as the value goes down. R1..R3 show their macro's
  colour. The volume knob shows the level. Encoder A shows the section, and blinks red while a recording is armed (lit while
  it records).
- **Hold Shift or Mod** and each pot shows what it does in that layer (a macro in its colour, nothing: dark).
- **A colour per macro**: the MACROS tab's new Color row; it is saved with the sound.
- **The LEDS tab** has three new roles, Knob, Volume and Enc A, and a Value switch for the pots' saturation.
- The first prototype has no LED under its pots: nothing changes on it.

## 2026-10-28: a page list, a jump list, the SOUND tab (ADR-044)

- **Shift + Menu opens a list of the machine's screens**: MODES (Synth, Pads), MAIN (Rack, or Pad FX in Pads; Presets;
  Sound; Sequencer, or Steps in Pads; Macros) and ADVANCED > (Keys, LEDs, Modifiers, Joy, Curves, Storage). It opens on
  the screen you are on. Switching Synth <-> Pads is now done there (or with the key function Section).
- **Hold Menu half a second** in the synth for a list of the main view's pages, and jump to one (in Pads, Menu still opens
  the FX chains, however long it is held). A short press still opens the menu,
  now when you let go of the key.
- **The SOUND tab** holds what GENERAL, SAMPLES and ALGORITHM held: one screen, the menu has nine tabs for every synth type.
- **Storage** (in ADVANCED) restarts the machine as the USB card reader, after asking.
- **For developers:** the drawing kit's list popup scrolls past six options, has headings and asks its labels from the
  caller (`popup_choose_labels`); the storage HAL has `storage_card_reader`.

## 2026-10-28: faster Pads, the engine's fast memory back in internal RAM (`perf-pads`, merged)

- **The Pads section costs less CPU.** With one pad playing the Pads player takes a fifth of what it did, and 8 streamed
  pads leave about 20 % more of the budget free. Nothing should sound different (every change is exact).
- **For developers:** the engine's fast heap fits in the first prototype's internal RAM again (it had fallen whole into
  PSRAM): no static instance per module type, the command ring and the rack graphs in the bulk heap, and a section switch
  frees the old graph before building the new one. New dev serial command `nosave` (nothing written to the card, for
  tests); `pads_test.py` uses it. Numbers: ENGINE_DESIGN.md ADR-035 item 10.

## 2026-10-28: the repo can be moved

- **For developers:** the desktop build and the three test scripts hand the compilers paths relative to the app's folder,
  never absolute ones. The dependency files and the link line hold no folder name, so the repo can be moved or copied without
  stale objects, and the link line is about 11 000 characters instead of 28 000 (Windows' limit is 32 767). An object whose
  dependency file names a file that no longer exists is rebuilt (after a move: once, everything). The toolchain paths
  (`C:\msys64`, `C:\.platformio`) are still fixed.

## 2026-10-22: the synth sequencer's piano roll, edited with the joystick

- **The joystick picks a note in the roll.** Left / right moves to the previous / next step, up / down over the notes of
  that step's chord, and to an empty slot where a new note would go when the chord has room. Up past the highest note
  reaches the page header. A push removes the selected note, or puts one in the empty slot (the last note entered).
- **The values are on info lines under the roll**, no longer in rows: the selected note, Len, Vel, Prob, Rtch, then Tool and
  On, then the step and the run state. Encoder A picks the value, encoder B changes it; a push on Tool or On runs the tool.
  The Step, Voice and Run rows are gone (the joystick and Play do what they did). The column knobs are unchanged.
- **A taller roll**: 3 pixels a semitone over two octaves. A note is an unfilled box whose inside line is filled to its
  velocity; the selected note is drawn larger. To be tried: the sizes are the user's own proposal, "to put to the test".
- **The speaker amplifier is an LM4871**, not a MAX98357A as the code's comments said. Its shutdown pin (GPIO 5) is active
  high, so Spk Off pulled it the wrong way: the speaker was silent only through a gain of 0. Spk Off now really shuts the
  amplifier down (no idle current, no hiss), and so does a power-off. The build flag `HWV1_SPK_SD_MODE` is gone (set to 1
  it would have shut the speaker down). Not tried on the board yet.

## 2026-10-14: simulator fixes, the sequencer's roll and erase, the repo in a folder with a space

- **The default key layout skips no note on the panel.** *Keys 8x4* counted 8 notes a row on every board, so the panel's
  4 columns played 0..3, 8..11, 16..19, 24..27. A row now goes on where the row under it ends: 16 notes in a row on the
  panel, the same 32 as before on the first prototype. A saved `keys.cfg` with `layout keys8x4` follows by itself.
- **The simulator's panel shows an LED change at once.** The panel window was only drawn again when a control moved, so a
  playhead or a blinking LED stood still until the next key press. As on a board, what `leds_set` sets now shows at the
  next `leds_show()`.
- **The synth sequencer's roll follows the playhead.** While it runs, the SEQUENCER page shows the 16 steps the playhead is
  on and the octaves of what plays there. Moving the cursor or editing its step brings the roll back to the cursor for
  4 seconds. When the playhead is on another page, a mark at that end of its strip says on which side.
- **Record the synth's pattern as a sample.** Shift + Play in the synth section now opens a list: Notes (the note recording
  as before), or the pattern's sound from its first step to its last: once (then the sequencer stops or plays on), once with
  its tail (the recording goes on until the sound has died away), or as a loop (the pattern plays twice and the second pass
  is kept, so the first pass's reverb tail is under its start). Afterwards a second list asks where it goes: the selected pad,
  the next empty pad or the library only. For developers: the drawing kit has a list popup, `popup_choose`; the engine's
  sequencer can mark a pass to record and stop at its end (`record_pass`), and the recording tap can start on it
  (`CaptureTap::arm_seq`).
- **Back held while recording erases.** With Rec armed and the sequencer running, every step the playhead passes while Back
  is held becomes a rest. The Undo tool brings back everything one hold erased.
- **The build and test scripts work in a folder whose name has a space** (`framework/tools/sim_build.ps1`, the three
  test scripts): the paths given to the compiler are quoted, and a header's change is still seen there.

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
