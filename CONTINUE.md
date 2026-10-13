# Continue development: handoff notes

Read [DEVELOPING.md](DEVELOPING.md) first (how to work in the code, where to change things, tools, testing, **"ESP32 firmware and performance work"**), then
[ENGINE_DESIGN.md](ENGINE_DESIGN.md) for the engine's decisions, measurements and limits (**ADR-035 = what was learned on the real chip**). This file only holds what they do
not: state, the latest measurements and what to do next, the user's working style, known debt and traps.
For any CPU / memory optimization work on the board, use the project skill `.claude/skills/esp32-optimize/SKILL.md` (the loop that worked: measure with
`apps/ritual_machine/tools/serial_test.py`, locate, change, host-test, the user flashes, re-measure).

## Session todo (updated 2026-10-13)

**Where things stand (2026-10-13): the repo is a library and its apps** (ENGINE_DESIGN.md ADR-043; the layout and the commands:
the top of DEVELOPING.md). The user asked that developers can start a new project easily. **Step 1** (the engine as the library,
the Ritual Machine and a starter template as apps) is committed by the user on `main` (`ffaa378` the renames, `a07f37e` the
contents; the first commit alone does not build: `git commit` without `git add`). **Step 2a** (the HAL, the two boards and the
drawing kit into `framework/`) was built the same day and **committed by the user (`d6f929a`)**; they did not say whether they
tried it, and nothing was flashed by me. **Step 2b** (a control table per board, the Ritual Machine's roles with a map per
board) was built right after, at the user's "let's go for step 2b": **in the working tree, UNCOMMITTED, NOT flashed** (new
files: `git add -A` before the commit). Not Claude's, both in the working tree: `apps/ritual_machine/.gitignore`, untracked
(PlatformIO's, made when the app's folder was opened in VS Code; `apps/template` has one that is versioned), and one line
gone from `apps/template/src/main_esp32.cpp` (the `board_card_reader()` call at start; the file changed at 03:00 on
2026-10-13 while 2b was being built: the user's or another session's, left as found). A bare `samples_src/`, `sdcard/`,
`tools/` or `platformio.ini` in older notes is inside `apps/ritual_machine/`. Two glossaries were made that day as documents for
the user (features and GUI; framework): ask for their links, they are not in the repo.

- [ ] **The split, step 2b: try it, then commit** (from: user, "let's go for step 2b"; ADR-043 "Step 2b as built", with
  the eight choices made while building: say if one is wrong). What is there: `hal/hal_input.h` has no control list any more,
  a board gives a table (`input_controls()`: name, kind, matrix place, panel place, LED; a control's id is its index) and
  events and LEDs carry that id; `boards/hwv1/hwv1_controls.*` (36 keys, 7 endless pots, the joystick) and
  `boards/desktop/panel_controls.*`; the Ritual Machine's roles are `core/controls.h` (`control_id_t`, the same values: no
  saved file changes), bound to a board by `core/controls.c` with `core/boards/map_hwv1.h` / `map_panel.h` (the pots' rows are
  what `hwv1_layout.h` had); `platform/controls_board.c` talks to the HAL; the template reads the table. Checked by me: 123 +
  114 engine tests and 147 UI tests pass (6 new: `tests/ui/test_controls.c`), both simulators build and ran 5 s without a
  window, a scratch program pushed every PC key and the panel's mouse parts through the real desktop code and each arrived as
  its old role, the firmwares build (the Ritual Machine: static RAM 222260 bytes, 67.8 %, +176; every engine object the same
  size as at `d6f929a`). **To try, simulator** (`.\build.ps1`, `.\run.ps1`): everything as before: the keys (F5..F8 too), both
  encoders, the knobs with the mouse, the joystick, the key LEDs, the KEYS and MODIFIERS tabs listing the same controls;
  `.\run.ps1 -App template` after `.\build.ps1 -App template`: notes on the rows 1.. / Q.. / A.. / Z.., F1 / F2 octave, F3 / F4
  cutoff, and now BOTH encoders (`[` `]` and `;` `'`) move the cutoff. **To try, board (ask before flashing; 2a has not run
  there either, as far as I know, so a fault can be either step's)**: 2a's list first (sound and its level, the boot
  animation, a streamed pad and the browser, a setting saved and back, the card pulled and put back, the speaker), then 2b's:
  every key and its LED, the three top pots in steps (volume, rows, value), the four around the joystick as positions (from
  the middle, no jitter at rest), the joystick and its push; the template: every pot moves the cutoff now.
- [ ] **The split, step 2c** (from: user; ADR-043 decision 14 and "Step 2 as planned"). NOT started: after the user's try
  and commit of 2b. The simulator's panel drawn from a board's table (`-Board hwv1`: the prototype's 8 key columns on the
  desktop). What 2b left ready: the tables carry a panel place per control (half key pitches; **HWV1's places are a guess,
  to correct against the real board when it is first drawn**), the app picks its map by `input_board_name()`, so the desktop
  only has to report another board; `hwv1_controls.c` compiles on the desktop already. Known work: `panel_sim.c` (356
  lines) is rewritten; `input_sim.c`'s PC keys come from the control's kind and matrix place; the desktop's key columns
  5..8 (`INPUT_OFF_PANEL` today) go when the panel board is drawn with its 4 columns.
- [x] **The split, step 2a** (committed by the user: `d6f929a`; ADR-043 "Step 2a as built". Its board try list is in the 2b
  item above; not known to have been flashed. Left for later, on purpose: `gui_style_t` still has the Ritual Machine's page
  fields; with `HWV1_TEST_TONE` the dev `dump` captures nothing) (done 2026-10-13)
- [x] **The split, step 1** (committed by the user: `ffaa378` + `a07f37e`; the template flashed and played by them before 2a:
  "it works nicely"; the root scripts ran in my baseline: tests, both simulators, both firmwares). `oled_sim.code-workspace`
  exists (the user's choice): to try once, open it in VS Code, the PlatformIO status bar must offer both apps. `new_project.ps1`
  adds a new app to it (checked once with a scratch app); it no longer copies the template's `.vscode` (that line NOT run).
  The old root `.pio/build` and `build/` are stale and can be deleted (`.pio/libdeps` at the root is still used); `stash@{0}`
  is still there (the user can `git stash drop` it); Aseprite: the sprites are in `apps/ritual_machine/assets/UI_Sprites` (a
  save to the old path recreates `assets/` at the root) (done 2026-10-13)

**Where things stood before it (end of 2026-10-12):** the session of 2026-10-11 / 12 built **the sequencers** (ENGINE_DESIGN.md ADR-042, three
stages, three rounds of options with the user): the synth's step sequencer now plays in the engine on the machine clock, with
chords, velocity, probability, ratchet, 64 steps, rate, direction, quick edit tools, step knobs with a movement threshold, recording
from the keys, 8 patterns + a chain, and a Velocity rack module (VL); the Pads section has its own sequencer (a list of bit-field
hits over all 64 pads, step mode, live record, 8 patterns + a chain). **All of it was committed by the user on 2026-10-13
(`72b2334`)**, host-tested (237 engine tests x 6 configurations, 141 UI tests), the simulator and the firmware build
(static RAM 67.7 %); **nothing was flashed, nothing was listened to, the user has not tried any of it yet** (they asked two
questions at the end: where the Select key is, and where to record in the synth: the answers are in the two "Sequencer" entries
below). Left in the repository by mistake: **a stash, `stash@{0}`**, an early copy of stage 1 (a `git stash` + `git stash apply`
typed by Claude; dropping it was refused by the permission system): the user can `git stash drop` it. Not Claude's: six sprite
PNGs of `assets/UI_Sprites/24` changed during the session (the user painting) and `apps/ritual_machine/src/core/ui_sprites_gen.c` regenerated from
them by `build.ps1`. Firmware builds of this session went to a scratch build directory (`PLATFORMIO_BUILD_DIR`): `.pio/build`
still holds the build of before. Next: the user tries the sequencers (simulator first, then a flash: ask before flashing), then
what they report; an on-screen Rec control for the synth's SEQUENCER page was offered and not answered; the pads sequencer has no
edit tools yet.

**Where things stood before it (end of 2026-10-11):** everything is on **`main`**, committed and pushed, and published as the pre-release
**`beta-1.0`** on GitHub (one flashable image for the first prototype + the Windows simulator; `apps/ritual_machine/tools/make_release.ps1`). **The
release image itself was NOT run on the board** (the user's choice: publish as untested): it is `[env:release]` (no serial
commands, output 18 %), and it holds the persistent sound, the presets and the simple storage screen, none of which ran on
hardware. What did run on the board that day: the card reader (the drive mounted and read at 543 KB/s) and the build with the
muted loading screen (flashed by Claude with the user's go-ahead for that one flash; the user has not reported on it). Ask
before flashing. Earlier that day: the browser no longer holds the UI (tried by the user: "fluid now"); the boot animation
(the user's `loading7.h`) plays from a task of its own on the board, back and forth with a loading bar until the start is over
(the looping version and the bar NOT seen by the user yet); the joystick switch's level is a HAL setting (the user checks the push
themselves). NOT started, from this session's choice (user): **the two optimizations (internal RAM, then the Pads fixed cost)**:
they are the next work. The samples folder: the user sorts the card's 1800 files into sub-folders themselves (nothing to build).
The test round stays open: everything in "Built, not tried by the user yet" below. Next features, each with a design round: live
synth (engine not chosen: "decide later"), song mode + backtrack, stereo samples (a format change).

Not Claude's, left as they are: `assets/UI_Sprites/24/mod_cab.png`, `mod_chorus.png`, `mod_comb.png`, `mod_osc.png` changed during
the session (the user painting); `BOOT_FRAME_MS` 33 -> 50 is the user's edit. `build.ps1`: the sample copy uses literal paths now (a
sample name with `[ ]` stopped the build).

Order: what is built and waits for the user's try, this session's choices, then the Pads section (user, 2026-10-09), the rest as chosen by Claude earlier (user: "I let you choose the order"). The user has made no sound design yet: **no saved patch or data to keep, old
patches can be deleted, no migration code** (user, 2026-10-09). The rest of the workflow plan: "Next: the production workflow" below.

- [ ] **Sequencer stages 1 + 2 (ADR-042): try it, by ear** (user, 2026-10-11: "add more features to the step sequencer and let's have
  a sequencer for the sample pad section"; two rounds of options, their choices in ADR-042). BUILT 2026-10-11 / 12, host-tested
  (with stage 3: 237 engine tests x 6, 141 UI tests), simulator + firmware built (static RAM 67.7 %, 221996 bytes; 66.2 % before
  this work, 68.4 % before the stored patterns moved to PSRAM), **NOT flashed, NOT listened to**
  (committed in `72b2334`). What is there:
  the sequencer plays in the audio thread on the machine clock (`engine/core/sequencer.*`; the UI loop times nothing any more);
  per step Note, Len, Vel, Prob, Rtch and a chord of 4 (the Voice row picks the note the Note row edits); SEQ SETUP has Rate (1/8 ..
  1/32, triplets) and Dir (Fwd / Rev / Ping / Rand), Steps goes to 64 (the roll shows the 16 around the cursor), notes C0 .. C8; the
  motion lanes follow the sequencer's position; **tools** (Tool + On rows, a push runs: Copy, Paste, Clear, Shift, Reverse, Double,
  RndNote, Human, RndProb, Undo); **the four column knobs = Note, Len, Vel, Prob of the cursor's step**, each only after it moved
  (their rule; `SEQ_KNOB_HYST` 24 of 1023, ui_internal.h: raise it if a resting knob still writes, lower it if it feels dead);
  **Rec** = Shift + Play (or the key function Rec; the user asked "where to record in synth mode?" on 2026-10-12: there is no
  on-screen control, the SEQUENCER page's Run field reads Rec while armed; an on-screen Rec row was offered, not answered):
  stopped = step entry with chords, running = live to the nearest step;
  **8 patterns + a chain of 16** on the new PATTERNS page, the change at the pattern's end, done by the audio thread; saved by
  themselves (`system/config/seq.cfg`, `pattern1.cfg` ..), not in the sound's state, not in presets; **the VL module** (Velocity as
  a source in the rack: Tgt / Prm / Dpth).
  To try: Play on the SEQUENCER page; a ratchet, Prob 50 %, a chord; Rate 1/8T, Dir Ping, Steps 32 and the cursor past step 16; the
  knobs on a step, then the cursor to another step with a knob left where it is (it must not write); Tool Double then Undo; Shift +
  Play, play a chord (stopped), then Play and play a line (running); PATTERNS: Pat while it runs ("1 >2", changes at the end), a chain
  1 1 2; power off and on (the patterns are back); insert VL, aim it at an oscillator's Lvl, give steps different Vel; on the board,
  scroll the sample browser while it plays (the timing must not move).
  Chosen by me (say if one is wrong): the row order; a ratchet's last hit keeps the step's length; a rest raised takes the last note
  entered; Vel and Prob move in fives; Prob 0 = never; the roll's window follows the page's lowest note; a pattern longer than 16
  plays the motion lanes again every 16 steps; the Note knob covers C0 .. C8 (coarse: 10 positions a semitone) and Len 1..16; the
  knobs write directly once moved (no catch arrows on that page); step entry replaces the step's notes and keeps its Len / Vel;
  live notes add to the chord (a full chord loses its first note) and take the step's Len; the cursor does not follow the playhead;
  with the chain on, Play starts at its first entry and the Pat row is ignored while it runs; a new chain entry = the pattern shown;
  Steps / Trsp / Swing / Rate / Dir belong to a pattern, the tempo to the machine; the velocity source is 0 .. 1 (keys = 100 / 127).
  **Known gaps**: Vel does nothing in a Modular patch without a VL module (their choice); **about 5 KB more static internal RAM** for the three stages
  (ADR-042 "Cost"; the stored patterns are in PSRAM), not measured on the board; the start now reads up to
  9 more small files from the card; no LED shows Rec; the Pads layout is untouched. A stale `synth.cfg` with `seq` / `notes` lines
  is read without them (from: user)
- [ ] **Sequencer stage 3, the Pads section's sequencer (ADR-042 decisions 16-20): try it, by ear** (from: user). Their answers of
  2026-10-12 (options offered): a step grid with live record; **one pattern covers all 64 pads**; a step carries **level,
  probability, ratchet and a length for gate pads**, and "you can compact ram/rom usage with bit-field struct"; step mode by
  **Select + Play** (against my recommendation, a Seq key on the free row); **patterns as on the synth**. BUILT 2026-10-12,
  host-tested (237 engine tests x 6, 141 UI tests; simulator + firmware built), **NOT flashed, NOT listened to** (committed in `72b2334`). What is there: a pattern = a list of hits (a 32-bit bit-field each, 160 at most, 648 bytes); it plays in the
  engine (a second sequencer; a hit starts its pad whatever the pad's own Grid says, at the hit's level; a Gate / Toggle pad is let
  go after the hit's Len); 8 patterns + chain, their own files (`padseq.cfg`, `padpat1.cfg` ..); **Select + Play, or Shift + Play** (the user asked where Select is: it is the right block's
  second row, first key, on the prototype; the simulator shows only the 4 left columns, so there it is Shift + Play) = step mode
  (the pad keys are the selected pad's steps, lit; Select or Shift + pad picks the track; Back leaves), the STEPS screen (joystick
  = cursor, its push = hit on / off, encoder A = field, encoder B = value: Lvl, Prob, Rtch, Len, Pat, Steps, Rate, Swing, Dir,
  Seq, Chain, CLen, Slot, Is, Rec); **Select + Rec** or the Rec field = live record while it runs. The stored patterns (pads and synth) are in PSRAM.
  To try: Pads section, a sample on A1 and A2; Select + Play; push pad keys 1, 5, 9, 13; Play; Select + A2, more steps; a step's
  Lvl and Rtch with the encoders; Steps 32 and Page +; Back, then Select + Rec and play pads in time; a Gate pad with a long Len;
  Pat while it runs; power off and on.
  Chosen by me (say if one is wrong): 160 hits a pattern; level in 16 steps, chance in tens of %; a hit's Len only matters for Gate
  and Toggle pads; a Toggle pad is started by a hit and stopped after its Len (never flipped); a pad's Next is not started by the
  sequencer; in step mode Mute + pad still mutes; the cursor does not follow the playhead; the sequencer runs with Play (the Seq
  field switches it off); the fields' order; live record keeps a hit's level / chance / ratchet when the step already has one.
  **Known gaps**: no edit tools for a pads pattern (copy, clear a track, shift), no LED for the armed record or for step mode, the
  Pads key layout's free bottom row is still free.
- [ ] **Flash the working tree, then look at the start** (from: notes). After the upload: the boot animation loops with its bar
  until the UI shows whole; a joystick push acts when pushed. To measure a start: build with `PLATFORMIO_BUILD_FLAGS=-DBOOT_TRACE`
  and read the `[BOOT]` lines (`serial_test.py --raw --seconds 10 --log run.txt` right after the upload); flash the normal build
  again afterwards.
- [ ] **Third session of 2026-10-11, built and host-tested, NOT flashed: check on the board** (from: user). (1) `apps/ritual_machine/src/core/app.c` is
  now the folder `apps/ritual_machine/src/app/` (nine files, `app_internal.h` lists them; a move, 110 UI tests). (2) The boot screen says RITUAL
  MACHINE above the animation and beta 1.0 under the bar (`BOOT_TITLE` / `BOOT_VERSION`, app.h): look at the placing. (3) Battery:
  serial `power` prints the raw count, the pin's millivolts and the result; compare with a meter and correct
  `HW_BATT_FULL_SCALE_MV` (board_pins.h; the scale is the first firmware's 10.4 V, never measured here). It shows in the GENERAL
  tab's box; warnings at 15 % and 5 %. (4) Mux channel 0 is read as a button (`power` prints it): push whatever is there and see
  whether the count moves; its pushed level (`HWV1_AUX_PUSHED`) is a guess. (5) **The power button, now read as a latching switch
  (down = on, PWR_SENSE high), and the USB card reader**, a second firmware: flash it once with the button down,
  `pio run -d apps/ritual_machine -e card_reader -t upload` (it goes to the second slot; `pio run -d apps/ritual_machine -t upload` is still the synth alone). Then: cable in,
  button up = the screen USB STORAGE and the card as a drive on the computer; button down = the synth. **If USB STORAGE shows
  with the button down, the level is the other way**: hold the joystick 2 s and keep it held until the boot screen, then set
  `HW_PWR_SENSE_ON 0` (board_pins.h). In the card reader there is no serial port: to flash, switch the button on first (or the
  joystick as above; the BOOT button always works). Never run, so unknown: the switch between the two firmwares
  (`esp_ota_set_boot_partition`), the USB port's hand-over to mass storage and its way back to the serial console, the custom
  upload command, the drive's speed, a card pulled out while the computer has it. On the battery: button up must switch the
  board off (it saves the settings first). Why two firmwares: TinyUSB costs 23 KB of static internal RAM, the synth had 24 KB.
  **Flashed 2026-10-11 by Claude (both firmwares; the user's go-ahead was for that one flash)**: the board came up as the card
  reader, the computer mounted the card (F:, 32 GB) and read 25 MB at 543 KB/s. So the switch to the second firmware, TinyUSB's
  take-over of the port and the custom upload command work. Still to try by hand: was the button really up then (else the level
  is inverted), button down = back to the synth with its serial port, writing a file, eject, the joystick way out.
  (6) **No sound and no key LEDs until the loading screen has ended** (user, after a first try: the keys lit and the board made
  noise during the loading): the DAC stays muted and the audio task writes zeros until the boot task is done
  (`loading_over` in display_esp32.cpp -> `board_audio_open`), `leds_show` sends nothing meanwhile. Listen and look at a start.
- [ ] **Fourth round of 2026-10-11, built and host-tested (116 UI tests), NOT flashed: the persistent sound and the presets** (from:
  user; decisions: a preset = the sound + macros + joystick, named files browsed like samples, an edit buffer with explicit
  Save). DEVELOPING.md has the description ("The sound is persistent"). To try on the board, after flashing both firmwares: change
  the sound, wait 3 s, power off and on: it is back, with the sequencer and the volume. Menu > PRESETS (second tab): Save as, edit,
  Save as, Load: the list, the highlighted preset plays under the keys after a moment, a push keeps it, Back brings the edited
  sound back. Unknown until tried: how long a preset takes to load on the board (a card read, then a rebuild of the engine: the
  audio stops for that moment), and the pause of the autosave's card write while playing (2 s after the last change). The card
  reader's screen is the simple one now (name and version, the placeholder picture `64/ui_sdcard.png` to paint over, "Storage
  mode / Eject before disconnecting", the battery's percentage); the PRESETS tab's icon `16/tab_presets.png` is a placeholder too.
- [ ] **Boot animation with a loading bar: look at it** (user, 2026-10-11, in four steps: the file `loading7.h` of the Sevenist
  Controller firmware, 24 frames of 37 x 55, the "7"; "the animation should start with a high priority task, and the task is deleted
  at the end"; "the screen shows partially on startup, we can finish loading in the background while the animation loops back and
  forth", with 20 frames a second, `BOOT_FRAME_MS` 50; "add a loading bar at the bottom and keep the long loading").
  Its frames are `assets/UI_Sprites/64/boot_00.png` .. `_23.png` (centred in 64 x 64; `apps/ritual_machine/tools/import_boot_anim.py`; editable in
  Aseprite like the other sprites; the first and the last must stay empty: the animation stops there). **On the board**
  `display_init` is the first thing `setup()` does and starts the task `boot` (priority 10, core 0, display_esp32.cpp): a frame every
  50 ms, first .. last .. first .., while everything else starts under it. The application reports its start once (`start_poll` in
  app/app.c -> `display_boot_end`): the card read and the pads' files in the catalog (**the user's decision: keep this long loading**),
  or no card within 1.5 s of `app_init` (`START_CARD_WAIT_MS`), 8 s at most (`START_MAX_MS`); the task then heads for the nearer end
  of the animation (half a pass at most), hands the panel to the display task and deletes itself, and the UI shows whole. While it
  plays, the card's work runs at full priority. **The bar** (96 x 7 pixels, 20 pixels above the bottom): `setup()` fills its first
  30 % by its steps (`display_esp32_boot_stage`), the application the rest (`display_boot_progress`: 30 % once the settings are read,
  then by the pads' files that are in); it glides, a few pixels a frame, and never goes back. Measured on the user's card before the
  bar (`BOOT_TRACE`, a reset): display 0.47 s, loop 1.47 s, **the UI at 5.1 s**: the first step holds 0.6 .. 1.06 s (the settings
  read again from the card + the rebuild; why so long is NOT looked at), then 2.3 s for three pads' files (`pad_c4` and `kick` are
  old demo files: 65 reads, 0.39 s each; plus the walk through the 1800-file folder for each), then 0.24 s to the animation's end.
  With the demo files cooked again and the samples in sub-folders it should be near 3 s (an estimate). The simulator plays the
  frames once in the application (`app_boot_animation`; no bar; a key, a push or an encoder step ends it there). 107 UI tests
  (`start_is_reported_once_when_the_pads_files_are_in`), simulator + firmware built. **The looping version was flashed and
  measured, not seen by the user; the bar is NOT flashed** (never drawn on a panel: its place and size are untested). Chosen by me
  (say if one is wrong): animation size x1 (small on 128 x 128), no text, not skippable on the board, the turn to the nearer end
  when loading is done, the bar's look and its 30 / 70 split (from: user)
- [x] **Sample browser no longer holds the UI** (user, 2026-10-11: "the browser is getting stuck while scrolling down"; then, tried
  on the board: "browser is fluid now"). Every row the highlight rested on held the UI for 0.15 s at the top of the 1800-file
  folder, 0.8 s in the middle, 1.1 s near the end; now 9 .. 37 ms (ENGINE_DESIGN.md ADR-041, "The browser held the UI"; serial
  `browse N REST FIRST`). Left: near the end of the 1800-file folder an audition makes a STREAMED pad drop out (7 .. 8 underruns in
  6 auditions, the same before the change; 0 near the top: sub-folders cure it); a whole pad sample is still one read of up to 0.4 s
  (done 2026-10-11)
- [ ] **The joystick switch's level: the user checks the push** (found 2026-10-11 with `BOOT_TRACE`): GPIO 40 (`PIN_JOY_SW`,
  internal pull-up) read 0 at every sample for 5 s after start with the stick untouched, and one `CTL_JOY_SW` press reached the UI
  in its first step, with no release: read as active low, the rest was a held push and each real push acted when it was let go.
  The user: "maybe it is pull up on the board, you can modify the HAL", then "I will check the joystick behaviour myself". Changed:
  the pushed level is a setting, `HWV1_JOY_SW_PUSHED` (hwv1_layout.h), now 1 = HIGH; the pin mode is unchanged. **Built, NOT
  flashed.** That HIGH means pushed is inferred (only the rest level was measured): if a push never acts after the flash, set it
  back to 0 and look at the wiring (from: measurements + user)
- [ ] **Pads fixed cost: ~9k cycles a block for the 64-pad bookkeeping** ("Pads on the board": 13.1k with one pad, ~3.6k per voice).
  An exact change. Done = before / after with `apps/ritual_machine/tools/pads_test.py` on the board, the output bit-identical, host tests green in the 6
  configurations (from: user)
- [ ] **Internal RAM: the fast heap is in PSRAM on the board** (measured 2026-10-09, `[RAM]` boot lines): largest internal block 59 KB at boot,
  51 KB before `audio_init`, so the fast heap (needs 64 KB + 16 KB margin) falls back whole to PSRAM: every module's state is in PSRAM. On
  2026-10-06 the largest block was 90 KB: ~27 KB of static RAM appeared since. Biggest user: `s_synth` 78.6 KB (the Engine, two RackGraph copies and
  the sample catalog; the RackGraphs and the catalog are control-only). Boot log of this session (normal build): internal free 83 KB,
  largest block 41 KB when the audio starts, fast heap 409600 bytes in PSRAM; since the display starts first (the boot task) it is
  71 KB free, largest 32 KB at that point (the display's task and bus are allocated before the audio now: count them in). Done = the `[RAM]` lines show the fast heap in internal
  RAM and the startup patch + the Pads cases are re-measured; options to the user where a trade exists (from: user + measurements)
- [ ] **Sample browser, the rest: try it** (built in 00235fb + 49dfdce; ADR-041 decisions 51-52 + "As built" + "On the board",
  `apps/ritual_machine/tools/library_test.py`): folders, the waveform, the audition from any pad key, push = take it, Back = the pad as it was. The
  board tests left pad A1 as it was and `1 - BASS C` looping on A2 (from: user)
- [ ] **Test round on the board** (the user's choice of 2026-10-11: pause features, test): everything in "Built, not tried by the
  user yet" below. The 38-item test list of the earlier session was written in the chat only: rebuild it from that section when
  asked (from: user)
- [ ] **Waiting on the user's answers**: the rest of test item 16 (the Play key on F4, "which is working but ..."); does a gate
  release trigger Next; is it right that the full synth stops in the Pads section (my reading of ADR-041 decision 22); "quantized
  capabilities" for the knobs and "the clock should start at the sample loop point for looped samples" (Snap and Sync were my
  reading of them); should the Grid chop unit and Fit beats follow Tune like Snap does (from: user)
- [x] **Snap 1/16 = a 16th note at the tempo** (61170b7, `pad_snap_grid`: Beat and 1/16 count the pad's Tune / Fine): tried by the
  user on the board: fine (done 2026-10-11)
- [ ] **Pads steps 4-5**: live synth (design round; its entry into the FX chains), song mode + backtrack (design round; builds on
  the capture); "Next: the production workflow" (from: user)
- [ ] **Workflow plan phase 2: measure the cost on the board**: step 1 (both branches, Para per branch, MIX) and step 2 (effects inside a
  branch, modulators reaching row M, row M's filter envelope; ADR-041 "Phase 2, step 2") flashed and tried by the user on the prototype
  2026-10-09: Osc -> Phaser -> Filter works, modulators work. Not measured: the cost of per-voice effects and of a stereo shared part (from: notes)
- [ ] **New placeholders to paint**: `24/slot_mix`, `8/ui_para` (`24/mod_sum` deleted). Defaults of SA / CB in row M reach full scale on the
  loud demo patch (measured, 32767 peak): the user's call (from: notes)
- [ ] **Board checks not confirmed yet**: Shift + note latch, knob hysteresis (`HWV1_ABS_START` / `_STEP` / `_IDLE_MS`), Spectral Gate / Robot /
  Whisper, ui.cfg + jump slots after a reboot; **listen to the Cab (Convolver)**: flashed and measured 2026-10-09 (PIE kernel, see
  "Convolver on the board"), bit-identical by design, its loudness (the IR is energy-normalised, level x1: a Cab in row M peaked at full
  scale on the demo patch) is the user's call (from: notes + user)
- [ ] Engine / board "D" items: SD options (a)-(d) (8 streamed pads reach the card's ~500 KB/s) (from: user + measurements)
- Set aside (user, 2026-10-07): unformatted card "format?" ASK (a mount without a file system is still "no card"; needs FATFS mkfs)

## Built, not tried by the user yet (the test round; moved from the todo 2026-10-11)

On the board since the flash of 5162be7 unless an entry says otherwise. "try it" = by hand; "by ear" = listen.

- **Sync (loop start on the grid), Snap (Beat / 1/16 / Attack / Zero), grid units 1 beat .. 8 bars, editor pages PLAY / TONE / CHOP
  from the header, no tempo popup in Pads: try it** (ADR-041 decisions 40-42; built 2026-10-10, 193 engine tests x 6, 87 UI tests) (from: user)
- **Board test of 2026-10-10 (user, test list items 1-17)**: OK: section switch, grid, editor, layout, modes, mixing, knobs, loops
  (Off / Trim / File, crossfade), pads.cfg after reboot, Next. Fixed after it: BPM at the top left of the Pads header, the Stop key's popup,
  a muted pad that sounds pulses red <-> its colour, a "Chop >" button (Page - / + was not found). Open: voice steal and streaming not
  tested; Play LED (F4) flash on the beat to recheck with the clock running; "quantized capabilities" for the knobs and "the clock should
  start at the sample loop point for looped samples" to clarify; items 18-38 of the list not reported yet (from: user)
- **Pads step 2 (clock, start on grid, tempo, metronome): try it**: BUILT 2026-10-10 (ADR-041 decisions 27-29; 11 pad engine tests,
  81 UI tests), flashed; how much of it the board test of 2026-10-10 covered is not recorded. Play in Pads runs the clock; Grid row of
  a pad; right block row 3 = Tap, Click, BPM -1, BPM +1 (from: user)
- **Pads loop crossfade, loop start, launch from, tail on the overview: try it**: BUILT 2026-10-10 (ADR-041 decisions 30-32; 12 pad
  engine tests in the 6 configurations, 82 UI tests), flashed; loops and crossfade OK in the board test of 2026-10-10, the rest not
  reported (from: user)
- **Chop (sample tools 1) + Next: try it**: BUILT 2026-10-10 (ADR-041 decision 39 + "Sample tools step 1"), flashed; Next OK in the
  board test of 2026-10-10, chop not reported (the CHOP page was not found then: the "Chop >" button was added since; Page - / + =
  Shift + encoder A also reaches it) (from: user)
- **EDIT page, version history, Grid chop padding: try it** (sample tools 2 + 3; ADR-041 "Sample tools steps 2 + 3 as built"):
  BUILT 2026-10-10, host-tested (199 engine tests x 6, 91 UI tests), flashed and measured (ADR-041 "The sample tools on the board"), NOT
  tried by the user. It writes sample files; the user (2026-10-10): the card holds nothing important and may be erased, no backup
  needed. In the simulator the card is the folder `sdcard/` (from: user)
- **Time & pitch: try it, by ear** (sample tools 4; ADR-041 "Sample tools step 4"): EDIT ops Stretch, Pitch, Speed, Fit beats,
  Fit speed. BUILT 2026-10-10, host-tested (204 engine tests x 6, 93 UI tests), flashed and timed (ADR-041 "Speed"), NOT tried by the
  user, NOT listened to. Listen for: a drum loop stretched +-10..30 % (hits doubled or smeared?), a sustained chord (flutter?), Fit
  beats then Loop + Sync against the metronome (from: user)
- **Crop, Undo with its trim points, Color / lo-fi: try it** (ADR-041 "Sample tools: Crop, the trim points per version, Color"):
  EDIT ops Crop, Lo-fi rate, Bits, Drive, Low-pass, High-pass; Undo / Redo / Ver put Start / End / LStr back. BUILT 2026-10-10,
  host-tested (207 engine tests x 6, 95 UI tests), flashed, NOT tried by the user, NOT listened to. Crop is my addition (from: notes)
- **Texture: try it, by ear, and check it on the board** (sample tools 5; ADR-041 decisions 44-46 + "Sample tools step 5"):
  EDIT op Paulstr. (x1.5..x64, a 4096-point window) and the Algo row Beat / Tonal for Stretch, Pitch, Fit beats. BUILT 2026-10-11,
  host-tested (211 engine tests x 6, 96 UI tests), FLASHED 2026-10-11 and measured (ADR-041 "The sample tools on the board" and
  "Speed": every operation fits; a 1.5 s sample takes 1.4 .. 6 s; streamed pads keep playing), NOT tried by the user, NOT listened to.
  Listen for: Paulstretch x8 on a chord, a voice, a drum hit; Tonal against Beat on the same sample. Reverse reverb and freeze / smear: from the FX chains + resampling (decision 46) (from: user)
- **FX chains: try it, by ear** (Pads step 3; ADR-041 decisions 47-49 + "Pads step 3, the FX chains, as built"): Menu in Pads =
  the FX page (rows A / B / C, the header = the routing and the load), Edit in a module's menu = its values (the four knobs drive
  them), a pad's Bus row on PLAY. BUILT 2026-10-11, host-tested (214 engine tests x 6, 98 UI tests), FLASHED and measured (8 pads +
  EQ, compressor, delay, reverb = 53 %; twelve light effects = 81 %; nine or twelve heavy ones = 111 .. 124 %, dropouts), NOT tried by
  the user, NOT listened to. Listen for: the reload when the routing or a module changes while a pad plays (from: user)
- **Capture (Rec key): try it, by ear** (ADR-041 decision 50 + "Capture as built"): Rec in the Pads layout (right block, row 2,
  between Mute and Stop) arms a recording of everything the Pads section plays; it starts at the first sound, Rec again stops it, the
  result lands on the selected pad if empty (else the next empty one). Shift + Rec = a length in bars: then it starts on the bar (clock
  running) and becomes a looping pad. BUILT 2026-10-11, host-tested (216 engine tests x 6, 100 UI tests), FLASHED and measured (no
  frame lost even with 4 streamed pads), NOT tried by the user, NOT listened to. Mono (the sample format). Reverse reverb = Reverse
  (EDIT), play through a reverb chain, Rec, Reverse the result (from: user)
- **Joystick swap: try it**: a push on an already bound parameter swaps X and Y, each keeps its range / curve (built 2026-10-09, 71 UI
  tests; on the board since the flash of 5162be7, NOT tried by the user) (from: user)

## Pads on the board (2026-10-10; flashed and measured by Claude with the user's permission, `apps/ritual_machine/tools/pads_test.py`, 44.1 kHz)

Cycles per engine block (budget 174149), the Pads section alone (Pads + MasterOut), pads looping (Trim):

| case | cycles | % | DMA underruns | card underruns |
|---|---|---|---|---|
| kick (42 KB, whole in memory) x 1 / 4 / 8 | 13.1k / 23.9k / 38.4k | 8 / 14 / 22 % | 0 | 0 |
| x 8 with a 200 ms crossfade | 38.4k | 22 % | 0 | 0 |
| pad_c4 (225 KB, streamed) x 1 / 4 / 8 | 13.3k / 28.6k / 57.4k | 8 / 16 / 33 % | 0 | 0 / 0 / 117-224 |

- About 3.6k cycles per voice (whole sample) and ~9k fixed (the 64-pad bookkeeping per block: a candidate for an exact optimization).
  8 streamed pads hit the card (534 KB/s: the known ~500 KB/s limit, "SD options" in the todo): fades, no DMA dropout.
- Memory: synth 48.6 KB fast / 187 KB bulk; Pads 10 KB / 467 KB (8 rings x 48 KB + samples). **Fixed**: the old graph stayed allocated
  after a switch until the next rebuild (both graphs in PSRAM: 773 KB bulk back in the synth); `engine_synth_housekeeping()` (called by
  `audio_update`) frees it once the audio task runs the new graph: back to exactly 48.6 KB / 187 KB. PSRAM outside the engine: 59 KB free.
- Measured with the measuring build (HWV1_DEBUG_AUDIO + ENGINE_PROFILE passed by PLATFORMIO_BUILD_FLAGS; platformio.ini untouched);
  the board runs the normal build again (5162be7, flashed 2026-10-11).

## Pads section, step 1 (2026-10-10; built, host-tested, flashed; tried by the user in the simulator and on the board)

- Try it in the simulator: **Shift + Menu** (BTN 3 + BTN 1) = Pads; joystick / encoder A select a pad, encoder B = bank, push = its editor:
  the Smp row (encoder B) picks a file of the library, then Mode, Strt, End, Loop, Tail, Tune, Fine | Lvl, Pan, Cut, Res, Chok, Mute;
  Back = the grid. The left 4 x 4 keys play pads 1..16 (the simulator shows only those 4 columns: Shift + pad selects, Shift + Back = Stop
  all; Bank / Select / Mute keys are the right block on the prototype). Column knobs = Start, End, Tune, Cut. Shift + Menu again = the synth.
- User answers (2026-10-10): pads start empty (the sound designer fills them, no auto-fill); **Loop = Off / Trim / File**: a file with its
  own loop points loads them when assigned (File: exact frames, ping-pong kept), Trim loops start..end (the case of a sample recorded from
  the synth: it has no loop points), File is not offered for a file without loop points.
- Chosen by me (say if one is wrong): a trimmed start fades in over 1 ms; a stolen voice stops at once (a click is possible with 9+ pads at once); the pan is a
  balance (centre = full level on both sides); Cut = 20 Hz .. 20 kHz log, 100 = off; tail default 20 ms, level 80 %; the knob set above.
- Limits / debt: tune and trim changes apply at the next push; root note and zone stretch, reverse, the Pads layout in KEYS, the clock /
  grid start, FX entries are not there yet; `pads.cfg` must fit `STORAGE_FILE_MAX` (4 KB; about 64 pads x 60 characters); bank slots are
  shared with the rack's SM (32 loaded samples at most; unloaded when their catalog entry leaves, since 2026-10-11); firmware static RAM +~6.7 KB (64 pads in `app_t` + a 4 KB file
  buffer; RAM 66.2 % in the build of 2026-10-10, the internal RAM is already short: see the todo).

## Next: the production workflow (ADR-041, phases 3 to 6; phases 0 to 2 done 2026-10-09)

The decisions are in ENGINE_DESIGN.md ADR-041; the user's design brief with screen sketches: https://claude.ai/artifact/HiSWqALbydqwbo8ibHp8nA.
Take the phases in order; each ends with host tests green, docs updated and the user trying the result. Ask the "ask first" points with
options (pros / cons) before building.

- **Phase 3, step sequencer for the synth.** **Started 2026-10-11: ENGINE_DESIGN.md ADR-042, stage 1 built** (in the engine on the
  machine clock; chords, velocity, probability, ratchet, 64 steps, rate, direction), **stage 2 built** (edit tools, step knobs,
  recording from the keys, 8 patterns + chain, the VL module); stage 3 is in the todo above (the Pads step grid). Still open from the plan: MIDI clock in / out behind a HAL (prototype 1 has no
  MIDI), MIDI input recorded into the sequencer (prototype 2). A captured sequenced pattern gives an on-grid sample.
- **Phase 4, capture.** A global shortcut from any page (ask which combo; check `bindings.c`, `modifiers.c` for free ones). Record the
  engine output into PSRAM, write a WAV to the card, add it to the sample catalog. Modes: manual, threshold (start on level, trim the
  trailing silence), N bars (starts on the next bar, uses the clock); the auto-sampler later. Prototype 1 has no input: capture = resampling.
- **Phase 5, Pads section** (SP-404 style, a separate workflow from the synth). **Started first (user, 2026-10-09); design rounds 1-3 done,
  ADR-041 decisions 14-26.** Steps, in order:
  1. **BUILT 2026-10-10** (see "Pads section, step 1"). Pad model (16 pads x 4 banks: sample, start / end, loop, tail, grid off / beat / bar, mode one-shot / gate / toggle, root + tune + zone
     stretch, choke group, level / pan / filter, FX entry Dry / A / B / C, mute), `pads.cfg` on the card; the section switch (Shift + Menu,
     key function "Section"); the built-in "Pads" key layout (left 4 x 4 pads; right: Bank A-D, Stop all, Select, Mute); the full-screen
     grid, a push opens the pad's pages. Engine: a pad player of 8 mono voices (Sampler streaming, choke, modes, tail), dry to the output.
  2. **BUILT 2026-10-10**: the machine clock (ADR-041 decisions 27-29) + "start on grid".
  3. FXA / FXB / FXC: a short module chain each, the four routings, entry per source. BUILT 2026-10-11 (the live synth's entry: step 4).
  4. Live synth: design round first (voices, engine, how it is played with 16 pads + Shift, its FX entry).
  5. Song mode + backtrack: design round first (what is recorded: events or audio; start / end edit; the backtrack in the Synth section).
  Then measure on the board (8 pad voices + FX chains + live synth; card streams vs PSRAM heads).
- **Phase 6, one settings page** reachable from both sections (LEDs, KEYS layouts, clock, MIDI): move the existing tabs there.
- Open questions: voice budget synth vs pads (measure on the board; phases 2, 5); hearing the other section's song layer while working
  (5 or later); keyboard Switch / Split / Layer, still needed with separate sections? (5); auto-sampler range, step, note length, velocity
  layers (later).

## Convolver on the board (2026-10-09; flashed and measured by Claude, NOT listened to)

Cycles per 32-frame engine block (budget 174k at 44.1 kHz), the Convolver alone, startup patch with only a Cab in row M:

| Len | before | after (PIE + aligned taps + advancing history) |
|---|---|---|
| 64 | 52.1k | 7.9k |
| 128 | 99.7k (57 %) | 10.0k |
| 256 | ~195k (112 %, 400-570 DMA underruns / s) | 14.2k |
| 512 | (not run) | 22.3k |

- What it was: 25 cycles per tap per frame (wrap mask + 64-bit sums); the IR regenerated in double on the audio thread at every IR / Length step.
- What changed (exact, details ADR-034 addendum): `pie_conv2_s16` (fft_s3.S), 8 shifted reversed tap copies (aligned loads), history written at an
  advancing position (a move per block cost ~30k cycles: the buffers are in PSRAM), IR computed in engine_synth (`conv_builtin_ir`) and sent as
  `ConvBlob` chunks; serial `pieconv` checks PIE against C (0 of 768 sums differ). ~14 KB per instance (was 5 KB).
- Not measured: the switch cost when IR / Length change (~10k cycles once, estimated).
- `[RAM]` lines (main_esp32.cpp, `HWV1_DEBUG_AUDIO`): free internal RAM and its largest block before each init step.

## Rack lanes, Sum points, FX modules (ADR-040; stage 1 built 2026-10-08; superseded, reworked by ADR-041 phase 1 on 2026-10-09)

**Superseded 2026-10-09**: the user dropped lanes and the Sum module. Follow ADR-041 and "Next: the production workflow" (branches + row M, Para switch, Pads
section, capture, sequencer; phase 0 done 2026-10-09); the stage-1 code below gets reworked in its phase 1.

- User decisions (four rounds of options, recorded in ENGINE_DESIGN.md ADR-040): Sum point per lane, 3 parallel lanes, cheap FX per voice /
  heavy only after a Sum, 16 slots, master FX tab kept, one module per effect, level / pan on the lane's Sum, Sum = the Para split generalised,
  RACK tab = **lanes + push menu**.
- Stage 1 done: model + rules + pages + the RACK tab (3 lanes, popup menu) + placeholder icons; tests `apps/ritual_machine/tests/ui/test_lanes.c`. Lanes B / C and
  the Sum / FX modules are **silent until stage 2** (the mapper builds lane A only).
- Next: the user tries the RACK tab in the simulator (navigation, the menu, links across lanes); then stage 2 (engine: 3 voice buses,
  per-voice FX `<Scope>` templates, the mapper per lane, lane mix into the master FX), then stage 3 (board, listening).

## FFT, STFT and the Spectral FX on the ESP32-S3 (2026-10-07; board session: flashed and measured by Claude; listened to by the user: Thru clean, Pitch / Freeze OK; Gate / Robot / Whisper not reported)

Since ADR-041 (2026-10-09) the FX rack, its tab and `fx_cog` are gone: Spectral is the rack module `MOD_SPECTRAL` ("SP") in row M or a branch.

- The C FFT measured 173-205k cycles per 512 transform on the board: an STFT hop needed more than a core, so SpectralFx / Vocoder could never have run there.
  User choice (asked with options; I recommended ESP-DSP float + a real-FFT trick): **own PIE block-float kernel**. Details and all measured candidates: ENGINE_DESIGN.md ADR-016 addendum.
- PIE kernels in `dsp/fft_s3.S` (S3 only, `SC_FFT_PIE`; every other target keeps the C code, which the host tests run): FFT stages + pack + min / max, the STFT window
  (`pie_vmul_s16`), the overlap-add (`pie_ola_shl / _shr`, bit-identical to C, checked by the boot bench), the CORDIC of the phase vocoder (`pie_cordic`, bit-identical).
  `ee.vmul.s16` truncates and wraps (measured). Exact C speed-ups too: FFT scan folded (test `fft_is_bit_identical_to_the_reference`), Stft overlap-add in 32 bits.
  A fully unrolled scalar CORDIC measured SLOWER (19.6k -> 26k per block: code from flash); reverted.
- Board: FFT 16.0k cycles (was 173k), SNR 63 / 64 / 63 dB loud / quiet / very quiet (C: 67 / 60 / 37); Stft frame 63.6k -> 40.4k; Stft mono 333 cycles per sample.
- **FX rack type Spectral** (`FX_SPECTRAL`, "SP", appended): rows Mode / Shft / Amt / Mix, behind a **cog** (new on the FX RACK tab, `ui->fx_cog`): Hold / Lo / Hi
  (user: all 7 parameters, the rest behind the cog; `FXR_PARAMS` 4 -> 8, `FXR_ROWS` 4; the rack is not saved, no file format changed). Engine: SpectralFx `stereo` = 1:
  STFT on (L + R) / 2, each channel keeps its own delayed dry (user choice "mono sum, dry stereo"); test `spectral_stereo_keeps_each_dry_channel_and_shares_the_wet`.
  Serial: `fx K T [values]` (SERIAL_COMMANDS.md).
- Board, startup patch + Spectral in slot 4, 3 notes (budget 174k per 64 frames): empty 65k; Thru 88k; Freeze (Hold) 104k; **Pitch +7 114k (66 %), 0 DMA underruns**
  (SpectralFx 43k per block: STFT ~18k, analysis 8.5k (CORDIC was 19.6k), resynthesis 10.9k). Pitch shows ~190 renders over budget but no real dropout: the frame
  lands on every other 64-frame write and the 6 x 64 DMA ring absorbs it. New `[AUDIO] DMA underruns` (HWV1_DEBUG_AUDIO) + column in `serial_test.py`: validated
  (0 with one shifter, 260-340 per second with four, ~10 per graph rebuild). User had also chosen fewer CORDIC iterations, block 128 and a frame split: not done,
  no longer needed (the CORDIC is 8.5k now; block 128 / split only add latency). Say so if they still want them.
- Next: Gate / Robot / Whisper not reported by the user yet; then maybe the Vocoder (needs a modulator source) and more spectral effects.
  Remaining STFT cost: the FFT itself (2 x 16k per frame), resynthesis 21k per frame (sine lookups), bit-reversal unpack 4.7k per transform.
- Traps: a flag passed by `PLATFORMIO_BUILD_FLAGS` is lost when the user uploads (put it in `platformio.ini` for that flash); the boot bench must time the fastest
  run (preemption outliers of 1M cycles); scripted edits lose one backslash level (`\n` in printf, again twice).

## Fine steps, continuous knobs (2026-10-07; fine steps tried by the user: work; knob hysteresis not tuned yet)

- **Fine steps**: `ACT_VALUE_FINE` = the value in steps of 1/5 (`core/fine_step.h`, a flag the app sets around the action: `param_adjust`,
  `rack_mparam_adjust`, the depth, `fxr_adjust` read it): 0.05 -> 0.01, 5 % -> 1 %, a log factor x1.2 -> its 5th root; never finer than the decimals shown
  (semitones, cents, ms stay whole). Enumerations, FM values, sequencer: unchanged. **Shift + joystick left / right** = Fine -/+ (Shift starting entries;
  they were Pages: row 0 + left / right still changes the page); also key functions "Fine -" / "Fine +" and the knob / encoder function "Val fine".
- **Continuous knobs**: a linear / logarithmic module or global parameter is driven exactly (`rack_mparam_norm / _set_norm`, `param_norm / _set_norm`,
  `cont_drv` in ui_input.c) over the knob's whole resolution, rounded to the decimals it shows; log parameters over their log range. Page rows, Shift / Mod
  targets, macros and the joystick XY all use it. Still walked: enumerations, the modulation depth (target units), the sampler slice (clamped to the
  file), GENERAL settings, sequencer. `knob_new_position` keeps the full 0..1023 (was >> 3). Values are no longer on the 0.05 grid after a knob.
- **HAL hysteresis** (input_esp32.cpp `scan_knobs`, HWV1 only): from rest or to reverse a knob must turn `HWV1_ABS_START` 8 units (the old step, which hid
  the noise); while it keeps turning one way (a report < `HWV1_ABS_IDLE_MS` 150 ms ago) it reports every `HWV1_ABS_STEP` 2 units (512 per turn). Untested:
  if a knob at rest still jitters raise START, if fine moves feel steppy lower STEP.

## Joystick XY (2026-10-07; tried by the user on the board: works)

User design (the click rules were confirmed on a worked example; options asked for the response and what to keep):
- **Bind**: on a page, a joystick push on a parameter row (`synth_ui_target_at_cursor`: the rows a knob may drive) binds it. A new parameter replaces the
  older binding (X, Y, X ...); the same one pushed again at once moves to the other axis and the one it replaced comes back (again: back); a bound parameter
  that is not the one just bound swaps X and Y (user, 2026-10-09; it was a Min <-> Max invert, not in the design): **each parameter keeps its range and
  curve** (a pitch set to exactly one octave stays one octave on the other axis); the next new parameter still replaces the older binding. A push on
  any other row (row 0, Run, step toggles) keeps its old "act like Right". Popup "JOY: X <name>" / "JOY SWAP". An inverted direction = Min > Max in the JOY tab.
- **XY mode**: Shift + push (Shift layer starting entry `CTL_JOY_SW` = `ACT_JOY_MODE`, editable; also a key function "Joy XY", token `joyxy`) toggles it;
  "XY" at the right of the header. Decided **around the value**: full throw reaches the mapping's ends, letting go (dead zone `JOY_XY_DEADZONE` 48)
  gives exactly the value back; leaving the mode off-centre also restores it. **The rest value follows the parameter** (user, after the first build: a knob
  / navigation change was undone): it is read each time the stick leaves the centre; a change made elsewhere during a push is kept on release.
- **Range + curve per binding** (they travel with the parameter on a swap): the JOY menu tab (after MACROS): Axis, Tgt, Min, Max, Curve, Clear, the response picture. **Saved in ui.cfg**: `joy` then
  `joy x|y <mapping>`. Code: `ui_input.c` (`synth_ui_joy_click / _mode / _axis`), `app/app_input.c` (`app_joy_bind`, `app_joy_mode`, `joy_update`), `scr_joy.c`.
- Chosen by me (say if one is wrong): the toggle is **Shift only** like the latch (Mod + push = Default, a normal push); "just bound" ends when XY mode is
  toggled (after playing, a push on it swaps X and Y); in XY mode the stick never navigates, so Shift + joystick (octave / pages) does nothing there; a push in XY
  mode still binds (the cursor moves with the encoders or after leaving the mode); the axis Y is up = positive.
- Limits: a walked parameter (most module params) is measured on every stick move like a knob (cost not measured on the board); no LED for XY mode.

## FM editor, mappings, macros, curves (2026-10-07; tried by the user: works; decisions asked with options)

- **FM like the rack**: BUILT 2026-10-07 (UI tests green, sim + firmware build). Main-view pages FM SYNTH (Patch Algo
  Fb Vol, the tree as picture), OPn (Lvl Crs Fine Fix, the tree with OPn marked), OPn ENV (Pt Lvl Time, the envelope); the ALGORITHM tab keeps Algo / Fb and an
  Op selector over the tree (left / right), a push opens OPn (`synth_ui_open_page`). OPERATOR / ENVELOPE tabs removed (their tab_t values stay: saved jumps).
  The tree is the existing generated sprite (now centred, transparent), not a new drawing: say if a bigger / different tree is wanted
- **FM parameters mappable**: BUILT with the above: `MACRO_FM` targets (op + `dx7_value_t`, ui.cfg `param fm <op> <v>`), learn / Shift / Mod / macros on
  every FM row but Patch and Pt. FM values are driven exactly (`dx7_value_get / set` + `knob_drv_t` in ui_input.c): the walking measure would have changed
  off-grid fine values and envelope times just by showing a page. Envelope time scale: at least 1 ms per knob position (found by the new test)
- **UI tests in the repo** (2026-10-07): `apps/ritual_machine/tests/ui/*.c` + `apps/ritual_machine/tools/build_ui_tests.ps1` (45 tests: modifiers, FM, mappings / macros, curves, LEDs; they found a parse bug: a mapping line read the range / curve of the NEXT line of ui.cfg), stubs `apps/ritual_machine/tools/ui_stubs.c` (RAM card, prototype
  controls). `STORAGE_FILE_MAX` 2 -> 4 KB (ui.cfg with many layer entries could be cut; +6 KB RAM on the board, 55.5 %)
- **Range per mapping**: BUILT 2026-10-07 (with the two items below; UI tests green). Every mapping (a Shift / Mod knob target, each macro destination) has Min and Max as a % of the parameter's range; Min > Max inverts.
  The knob's full turn sweeps Min..Max through the mapping's curve. Decided: % (not the parameter's unit)
- **Response curves**: BUILT (built-ins Lin Exp Log S; user curves = the editor item). A curve per mapping = a LUT read with linear interpolation between entries (cheap). Built-in curves (at least linear, exp, log, S; to list)
  and user curves. Catch with any curve: decided **in knob position** (the knob is caught when it crosses the position where the curve last left the value; a value
  changed elsewhere is caught at the first knob position that maps to it)
- **Multi-destination macros**: BUILT (MACROS tab, ui.cfg `macro K ...` lines; Shift + R knob now ADDS a destination instead of replacing). Decided **macros 1..8, up to 8 destinations each**, each destination with its own range and curve (learn adds a destination;
  today's single-target macros = one destination). Needs a macro editor (list of destinations, remove, range, curve) and saving in ui.cfg
- **Custom curves editor**: BUILT 2026-10-07 (UI tests green). User decisions: 16 points max, a CURVES tab for now (may move), X / Y by two knobs set in code (`CURVES_KNOB_X / _Y`, bindings.h; default K3 / K4 so the board has them, R1 / R2 the other choice). One file `system/config/curves.cfg` instead of a folder (a new folder would make every card ask "Missing: ... Create?"); 8 curves U1..U8; table 129 entries. Firmware RAM 58.9 % (+9 KB). Was: a curve = a list of points (x, y); for **each point** the segment after it is either linear (interpolated to the next point) or
  stepped (held until the next point). Stored on the card (`system/curves/<name>.crv`, text), turned into the LUT when loaded. To decide when started: max points,
  LUT size, where the editor lives (a CURVES menu tab?), how a point is moved with 4 knobs on a 128 x 128 screen

## Shift / Mod layers, the MODIFIERS tab (2026-10-07; built, tried by the user: works)

User decisions (asked with options): **every control, keys included**, can be set per layer (I recommended knobs + encoders + joystick only: against it, see the
decisions list); knob targets = **any parameter by learn** (module params by module id, global, sequencer, live GENERAL settings); **Mod = a new key function, on no
built-in layout** (the user assigns it in the KEYS tab); learn by **both** the chord Shift + Mod + turn a knob and a Learn button in the tab.
Details chosen by me (say if one is wrong): **Shift and Mod behave the same** (user, after the first try: Mod lacked Shift's popup): Default = as without
modifier, except a column knob ("No target" popup), a jump key (saves) and, **Shift only**, a note key (latch) and Back (release all latched notes) (user 2026-10-07: see the todo); Shift's old fixed behaviours (EncA pages, EncB x4, joystick octave / pages, R learn,
K1 Spk, K2 Vol) are the Shift layer's starting entries, editable; ui.cfg stores only the differences from them (`act default` for a cleared one); Shift + Mod held = the Mod layer; the layers are global (not per key layout) and live in ui.cfg; a modifier key is never taken by a layer, and Shift / Mod
cannot be a layer entry; the chord learns into the layer the tab shows (default Shift); with the Learn button the next Latch / Select push assigns (Back cancels);
an absolute knob with a step action (Rows, Pages, Value, Value x4, Octave, Volume) steps once per 1/32 of its travel; a learned module param goes back to Default
when its module is deleted (`modifiers_prune` at every page rebuild).
- Code: `core/modifiers.c/.h` (entries, function lists, ui.cfg lines), `scr_mods.c` (tab: Layer, Ctrl, Func, Learn, Reset + the list), `app/app_layers.c` (`app_layer_event`,
  `learn`, `app_learn_push`, the catch targets per layer: `MACRO_PAGE_ROW`, `ui->knob_away`), `ui_input.c` (generic targets: `synth_ui_knob_row_target`,
  `synth_ui_knob_target`, `synth_ui_target_step`, `synth_ui_target_describe`, `synth_ui_target_at_cursor`; `MACRO_CFG`), `ACT_MOD` (key function
  "Mod"; a "Save 1..8" function was added then removed at the user's request: saving a jump = Shift / Mod + the jump key only), HAL `input_control_present()` (the board: the 7 knobs + joystick; the sim: everything). `ui->knob_shift[]` and `ACT_PAGE_KNOB_SHIFT` are gone.
  The macro learn (Shift + R knob) now also takes GENERAL settings.
- Verified: sim + firmware build (RAM 53.4 %); a scratch test through `app_step` (defaults, Mod + EncA = Pages, the chord, the Learn button + push, Shift + jump key
  still saves, a key's Shift entry overrides it, ui.cfg round trip, the old `shift N cfg` format, prune); the tab drawn at 128 x 128 with `ui_dump`'s stubs.
- Macros on any hardware (user: "parametrable so any hardware layout can accommodate the UI"): knob functions "Macro 1..3" (plays `ui->macro[k]`; on a col knob with
  the catch) and "Learn M1..3"; the starting entries depend on the board (`input_control_present(CTL_KNOB_R1)`): without R knobs Shift + K3 / K4 = Macro 1 / 2.
  `ui_dump` stub: `UI_DUMP_BOARD=hwv1` = the prototype's controls.
- Func list of a knob (fixed after the user found Spk missing once unselected): Default, None, the live GENERAL settings (Vol Spk Out Glide Legato Patch Knob), the steps; other parameters only by learn.
- Not done / limits: no LED for a key's layer function; keys with a Shift / Mod entry do not show it on their LED; the tab cannot set a knob param without learn.

## UI/UX round (2026-10-06; built, mostly confirmed by the user on the board; the user flashes)

The user wanted the confusion about the joystick gone and a safer knob. Order taken: navigation, Shift knob + catch, knob mode setting, rack links, jump keys. What exists:
- **Navigation** (`ui_screen.c`, `screen_def_t.use_latch`): left / right move to the spatial neighbour; with none on that side (and on `EL_DIRECT`) they edit. Push latches only where `use_latch` is
  set (the RACK tab; a first version removed latch everywhere and the user found the rack unusable without it). Encoder behaviour was left as it was.
- **Column knobs** (`ui_input.c`): catch by default. The first version only released on an exact step hit, which a normal sweep skips: now it releases on crossing the value. The first arrow was
  inverted (knob above the value means turn left). A 4 px wedge at the right end of the row (`ui_draw.c`) shows the way. `cfg.knob_mode` (GENERAL "Knob") switches to Direct.
- **Re-evaluation after manual changes** (`synth_ui_catch_refresh`, called by `app_step` through `app_catch_poll` when `app_catch_stale`): the catch state used to change only when a knob moved, so a page change or an encoder edit
  left a knob "caught". The first refresh ran after knob moves too and the knob stopped latching (the step re-measured by walking differs by one from the step the knob just set); the user had it backed
  out. It is back with two rules: it never runs after a knob move, and `knob_cur == -2` keeps a knob that just set its value caught. **Not tested by the user yet.**
  Bug found by the user (a jump key "reset every parameter of the page to 0"): the measuring helper (`knob_param_pos`, now `knob_measure`) left the parameter at its minimum and overcounted the range by the
  start position, so every catch check and every refresh parked the page's parameters at 0 and skewed the catch maths (the "finicky" catch). Now it restores the value and returns the true range.
- **Shift + column knob** (replaced on 2026-10-07 by the Shift layer's starting entries K1 Spk / K2 Vol; `ACT_PAGE_KNOB_SHIFT` and `ui->knob_shift[]` are gone): knob 1 = speaker level (off -> 5 % .. 100 %), knob 2 = master volume. Bindings: the plain column-knob rows are `MODS_NONE`.
- **GENERAL tab**: seven rows on a 64 px screen did not fit (the user could not see "Knob"). The list scrolls to keep the focus visible and `screen_draw` clips to the area under the header
  (clipping applies to every declarative screen: if a tab ever draws outside its area, look there).
- **RACK tab links** (`scr_rack.c`): every link is drawn; a module scrolled out of the window stands at the window border on its side (stub + lane), so links never vanish while scrolling.
- **Jump keys** (`ACT_JUMP`, `UI_JUMP_1..8` that the user added to `ui_event_t`; `jump[]` in `synth_ui_t`; `fns[]` in `keymap.c`): Shift + key saves (`in_rack`, tab, page, row), key goes there.
  A first version had two actions and no keymap entry (unreachable): found by a review the user asked for after a model change; fixed. Leaving the rack editor by a jump now rebuilds the synth.
- Risks / limits: (the jump slots and the knob mode are saved since 2026-10-07, ui.cfg; slots point at module id + page since 2026-10-07, so rack edits no longer move them); `app_catch_stale` is a global of `app/app_layers.c`;
  refresh walks up to four parameters per manual change (cheap, not measured on the board).

## Filters, UI latency, voice bus (2026-10-06; flashed and measured, NOT listened to; decisions in ENGINE_DESIGN.md ADR-038)

- UI: `display_send()` replaces `u8g2_SendBuffer` (HAL); on the board a `display` task (core 0, prio 1) sends only changed tiles; `app_step` puts the redraw off while input is
  queued. Measured with `HWV1_DEBUG_UI` while playing: slowest UI step ~120 ms -> 4-16 ms, up to 92 events and 69 synth updates per second (was ~8). Screen checked by the user
  after the column-wrap fix (the panel's column offset 96 must wrap: a run at tile x is sent with the offset moved).
- Note-on overruns: `OscEngines::reset()` cleared the 4 KB Karplus buffer in PSRAM for every oscillator; rapid notes now 0 over budget (startup worst 2.3 -> 0.92 ms).
- Cutoff: ~20 ms glide at any block size, coefficients interpolated across the block. New FL types LP6 / Ladr / ChLP (`algo` of the Filter module, live switch), measured
  per 4 voices: LP 18.0k, LP24 24.7k, LP6 7.8k, ChLP 6.4k, Ladr 15.9k. Serial `flt T`. Host test `light_filters_lp6_ladder_and_chamberlin`.
- Not done: listening (all of it); the Ladr tuning at high cutoffs is approximate (not zero-delay); the light types have no exact per-sample path for audio-rate cutoff FM.

## Key LEDs (2026-10-06; flashed, checked by the user: every key lights its own LED, colours right; red and green were in fact swapped, colour order RGB -> GRB 2026-10-07)

- Driver `leds_esp32.cpp`: FastLED 3.10.6 pinned (it gives the first RMT channel DMA on the S3 by itself; SynthBox's `patch_fastled_dma.py` targets 3.10.3's file layout and is not
  needed), `-DFASTLED_RMT_MAX_CHANNELS=1`, SK6812 order **GRB** (`HW_LED_ORDER`, board_pins.h; SynthBox's BGR swapped red and blue, RGB then swapped red and green), chain map in physical columns (the HWV1_FLIP_COLS flip mirrored every row),
  **brightness capped by `HW_LED_MAX_BRIGHTNESS` (0..255, set in platformio.ini; was 51 = 20 %, the user raised it to 80 on 2026-10-06): full white on 36 LEDs ~2 A browns out the board / USB**. A transfer only when the frame changed.
- What they show (user choice "play feedback"): `app/key_leds.c` from the key layout: notes in piano colours (C teal, naturals dim white, sharps dim blue), held = orange;
  Shift yellow (bright while on), Menu blue (bright while open), Back red, Play green (bright while the sequencer runs), Octave violet (bright when shifted that way), navigation dim white.
  Jump keys (`Jump 1..8`): dim teal while the slot is empty, bright teal once saved.
  Repainted at most every 20 ms, at once on input. The simulator's LED HAL is a no-op (the panel window does not draw them yet).
- Serial (dev): `leds N`, `leds all`, `leds off` / `leds on`. Audio while playing keys: see the todo (the overruns happen with the LEDs off too).

## Mixer polish (2026-10-06; host-tested in the 6 configurations, flashed and measured, NOT listened to)

- Mapper (`rack_graph.cpp`): the running signal is a list of sources (`RunSrc`); the next module gets one cable per source and the plan's MIX step sums them
  (32-bit, one saturation). A modulating oscillator joins through a cable whose depth is `Lvl` x 1/sqrt n (`qd()`, never unity: live `SetDepth`), Mute = depth 0
  (skipped by the MIX step). More than 8 sources: the first eight fold into one pass-through Mix4. `Engine::set_edge_depth` now checks the pending plan first
  (a depth edit right after a load no longer forces a rebuild). VoiceOut: one product for both buses when centred (exact; saved only ~75 cycles per voice).
- Tests `test_rack_mix.cpp`: no Mix4 nodes for 8 sources, the fold at 9, the mix equals the sum of the sources alone (<= 8 LSB), level / mute edits stay live.
  The sound changes only by rounding (the old Mix4 multiplied by 32767/32768 per stage and saturated per stage): no listening round needed unless a mix clipped before.
- Board (44.1 kHz, 64 frames, budget 174149): Para 8 osc x 4 voices, 4 keys 82.6k -> 66.1k cycles (Mix4 was 16.7k); Para 4 osc x 8 voices, 8 keys 100.5k -> 71.5k;
  startup patch (Mono) 1 key 63.5k -> 61.5k. Biggest remaining voice cost: the oscillators (Strng ~1.5k per oscillator per voice), then VoiceOut ~790, NoteIn ~310 per voice.

## Mod Para, the rack oscillator's quality and the Strng engine (2026-10-06; flashed and measured; Mod Para listened to by the user: OK, legato and glide OK)

Since ADR-041 (2026-10-09) the type "Mod Para" is gone: Para is a switch on a rack module, PEnv a row of that module; the engine parts below remain.

Decisions in ENGINE_DESIGN.md (ADR-036 stage 2 "as built", ADR-037 decision 5). The user flashed-and-test permission covers this task (COM8).
- Mod Para: GENERAL Type "Mod Para", Voices 1..8, PEnv Legato / Retrig / Voice. The rack splits at its first filter; `GateIn`, `ParaGate`, `EnvG` (T_ENV_G) are the new engine
  parts, `ProcessCtx` carries the voices and the held-key count. Host tests (`test_para.cpp`): both keys sound, a released key stops while another is held (power 1e15 -> 1e7),
  the last chord rings through the shared release, Retrig raises the level x 2.8 on a new key, Legato x 0.98, Voice releases a key alone.
- Board (`patch para N V`, Strng saw Mip, FX dry): 8 osc x 4 voices: 1 key 28.8k, 2 keys 46.8k, 4 keys 82.7k cycles (47 %); 4 osc x 8 voices: 1 key 20.3k, 4 keys 51.7k, 8 keys 100.5k (58 %);
  0 blocks over budget. One Strng oscillator (a pair) about 1.5-1.9k cycles per block per voice; Mix4 2k per voice at 8 oscillators.
- Rack OSC: Wav "Strng" (engine 8: Timbre = detune up to 50 c, Morph = saw / pulse / tri), Q row on OSC TUNE (Blep / Mip / Naive). Host C7 saw: Blep -28.2, Mip -40.6, Naive -12.5 dB.
- Aliasing check on the board (`dump` + an offline DFT, C5 single note, dry): saw Mip -40.6 / Naive -18.4 dB, pulse -40.2 / -20.1, triangle -39.7 / -39.7 = the host's numbers.
  What the user heard on `chord 16` (8 notes + a chromatic cluster C#5..G#5, 2 detuned saws per voice) is most likely beating of the cluster, not aliasing; to confirm by ear.
- Engine memory: the instance records and plans are sized by the real voice count (no static kMaxVoices tables): firmware RAM 50.7-51 % (was 55.6 % with the first Strings build).

## Strings, the third synth type (ADR-037; 2026-10-06; flashed and measured, NOT listened to by the user)

User decisions in ENGINE_DESIGN.md ADR-037 (do not re-litigate). What exists:
- `engine/modules/strings_modules.*`: `Strings` (voice scope: two detuned oscillators saw / pulse / tri, naive or mipmap, linear ADSR per block, one-pole LP on / off with
  key and envelope tracking, velocity, level; one render loop per wave x osc kind x LP, chosen per block) and `Ensemble` (global: three taps on one 11 ms line, slow + fast LFO at 0 / 120 / 240 degrees).
- `engine/dsp/wavetables.*` (generated, `framework/tools/gen_wavetables.py`): saw and triangle, 10 octave bands, 1024 samples + guard, 41 KB in flash (`ENGINE_WT_RAM=1` for RAM).
- Type `SYNTH_STRINGS` (GENERAL Type "Strings", `str_voices` 1..32 kept apart from the 8 of Modular / FM); pages STRINGS (Wav Osc Det Mix), STR TONE (PW Lvl), VOICE LP (LP Cut Env Key),
  STR FILTER (Typ Cut Res; Off = no node), AMP ENV (A D S R; the curves are ignored), SEQ, SEQ SETUP; menu tabs GENERAL, FX RACK, KEYS (since ADR-041: RACK with row M first). Params in `synth_params_t.str` (P_STR_*).
  FX rack type `FX_ENSEMBLE` ("EN": Rate Dpth Shim Mix) for every synth type (since ADR-041: the rack module `MOD_ENSEMBLE`, "ES"). Serial `patch strings`.
- Engine: `ENGINE_MAX_VOICES` 8 -> 32; the plan's instance table is now sized by the plan's voice count (`Plan::inst(node, voice)`), so plans did not grow (the test graph's module memory went
  16.6 / 8.8 KB -> 13.0 / 4.8 KB for 8 / 1 voices); the instance records still grow ~13 KB static (firmware RAM 51.5 -> 55.6 %).
- Host: 151 tests, all six matrix configurations green. C7 saw inharmonic power: naive -12.5 dB, mipmap -40.6 dB; 32 voices are real (rms 8 notes 4211, 32 notes 7208, 32 notes on 8 voices 3301);
  linear attack / release at half way 0.51 / 0.48. Screens checked with `ui_dump` at 128 x 128.
- Board: see the todo (16 voices 87k with the startup FX). Not done: listening, level balance (Lvl 0.5 x VoiceOut 0.5 per voice: 32 voices may clip, Mix 1.0 default), default pad envelope (AMP ENV is shared with Modular).

## ESP32Synth (removed 2026-10-06)

The ESP32Synth library (github.com/danilogcrf2-oss/ESP32Synth, MIT) was tried as a second engine on the board (`alt on`, a lazy-allocated object rendered by our audio
task) and removed at the user's request once the Strings type existed (git history: commit f714a84 has it). What it taught: thin voices (one oscillator x a linear
per-block envelope) make many voices cheap (506 cycles per voice and 32 frames, saw, measured); its S3 "SIMD" vector types compile to scalar code (0 `ee.*` instructions);
an outside engine needs its own voice allocator, mono -> stereo and its own measurement hooks: `engine_synth.h` is not an engine-neutral backend interface.

## State (end of the ESP32 bring-up and optimization session)

- The application runs on **our own DSP engine** (`framework/engine`, C++17, q15) on **both** the desktop simulator and the **first hardware prototype** ("HWV1": ESP32-S3-Pico, SH1107 128 x 128 OLED, TCA8418
  keyboard, quadrature knobs behind an analog mux, joystick, I2S DAC). 132 engine tests are green in all six rate / block configurations (`-Matrix`, run at the end of this session); the simulator and the firmware build.
- Working on the board (checked by the user): display, keyboard (map found from their key log), the 7 knobs (quadrature decode with hysteresis) and the joystick, power hold, I2S audio (the test tone and the engine),
  serial debugging, the boot-time benchmark, the serial command channel. The default patch is now `rack_init_startup`: **four oscillators on four different engines (Karplus, Modal, Supersaw, Additive) into one filter, delay 1000 ms / 40 %,
  reverb 40 %**. `rack_init` (osc, filter, saturator, LFO) is the older demo rack that the tests build on.
- **Not verified on the board**: the speaker amplifier (SPK_SD is left floating on purpose, the headphones/DAC path is what was heard; `HWV1_SPK_SD_MODE=1` drives it high), audio glitches while the screen draws (mitigated by IRAM code, never checked
  by ear), "the MCU stalls when no serial port is connected" (a fix was applied: non-blocking serial and `delay(1)` in the idle loop; maybe power related), the TF card (none), the new prototype's controls.
- **Not listened to by the user yet** (all of these changed the sound slightly and were only verified by tests): the interpolated filter coefficients, the 5-saw supersaw, modal as phasors, the additive recurrence, the half-rate reverb, mono delay / reverb,
  the vowel / dust filters in float, the dust level compensation.
- The user's earlier plans: save / load of racks and FM patches "after some fixes"; the engine / UI work of the previous sessions (ADR-028..034) was never listened to either (levels untuned; they asked to skip that step).

## Modular engine redesign (ADR-036: Mono / Paraphonic / Poly, capture, cost metadata) - stage 1 done on the host

The sampler is "fine for now" (user); the next topic is the modular engine. Decisions are in ENGINE_DESIGN.md ADR-036 (do not re-litigate): three modes (Mono default, Paraphonic, Poly kept as the legacy mode); "internal resampling" = capture a patch into a sample for the Sampler (not real-time polyphony); paraphonic splits the rack at its first filter with selectable envelope behaviour (first-key / every-key retrigger / per-voice amp env); budget = cost metadata only for now.
- **Stage 1 (done, host-tested, NOT flashed / measured / heard):** `Engine::load(graph, nvoices)` (the `Voices` setting was cosmetic before: the engine always built 8 copies); a node whose voice count changed is created anew and the old instances are freed by `gc()`; `Engine::set_voice_mode(Mono|Poly, legato, glide_ms)` with a last-note-priority key stack (16 keys), legato (pitch changes, envelopes keep running) or retrigger, exponential glide at block rate; `cfg.mode / glide / legato` in the GENERAL tab (default **Mono**; since ADR-041 there is no mode field: Voices 1 = mono, Glide / Legato shown only then); mode / voice-count changes rebuild, glide / legato are live; serial `mode mono|poly [glide] [legato]` (now also `para [env]`).
  Host numbers: 1 voice instead of 8 saves about half the module memory in the small test graph (16.6 KB -> 8.8 KB; real patches with Karplus / delay buffers should save much more: measure `[HEAP]` on the board); glide of 100 ms reaches 63 % in 105 ms.
- **Next on the board (needs a flash):** with the startup patch in Mono, the CPU and `[HEAP] fast heap used` should drop to about a voice's worth; check by ear that Mono feels right (last-note priority, legato on / off, glide) before stage 2.
- **Stage 2 (paraphonic)**, in the order I would build it: `ProcessCtx` gets the voice array; a global `GateIn` module (any-key gate, a per-key trigger, lowest / last / highest pitch, velocity); `Env` as `Env<Scope>` like the other `_G` variants (it already takes its gate from a cable); the mapper builds per-voice modules up to the first filter and global ones from there; the three envelope policies; paraphonic becomes another synth type (`SYNTH_MOD_PARA`, ADR-036 decision 5: the mode is part of the type, there is no Mode row).
- Stage 3 (capture): `Capture` module + a RAM-backed `StorageDevice` behind a mux with the card; saving to the card needs write support in `sd_card_spi.cpp` (CMD24 / CMD25 + FATFS write). Stage 4: per module cost model from `[PROF]`, a test against the host profiler.

## TF card and sampler on the board (measured on the board with the automatic loop of the skill; the loop fix and the sound are NOT yet listened to)

The user reported glitchy sampler playback, loops misplaced / shifted, 2-3 voices, the card read "too much". What the measurements found, in order:
- **Root cause of the glitching: ESP-IDF's sdspi host spent about 40 ms before EVERY SD command on this board** (a plain CMD13 took 40 ms; with a pin trace, CS / MISO stayed quiet for 40.5 ms and the real transfer took 55 us at the end; two different cards (an old 1 GB and a 32 GB), SPI2 and SPI3, 10 and 20 MHz, MISO pull-up: all the same; a bare 10-byte SPI transfer on the same bus was normal).
  A 4 KB read therefore cost 80 ms = 50 KB/s, and one sampler voice needs about 96 KB/s. Fix: an own small SPI-mode driver (`sd_card_spi.cpp`, read only, registered with FATFS): 1 sector 82 ms -> 1.4 ms, 8 sectors 5 ms, a scan of the 5 demo files 6 s each -> 0.36 s. The IDF host stays behind `-DHWV1_SD_IDF` for comparison.
- **Loop points** (ADR-022 addendum): a fixed `block % 8` ring slot made a loop's end and start blocks collide, so every lap dropped out; fixed on the host with a test (it fails without the fix). Not yet heard on the board.
- **Sampler CPU** (measured, 44.1 kHz, `patch sampler 3 2` = pad_c4, forward loop, one filter, no effects): the Sampler cost 15.7k cycles per voice and block (490 per sample). A per-block fast path (steady rate, no wrap, one contiguous run of data; bit-identical output, checksum test `sampler_fast_path_is_identical_to_the_general_loop`) cut it to 5.4k.

| notes held | cycles per block (budget 174149) before -> now | blocks over budget before -> now | sampler underruns before -> now |
|---|---|---|---|
| 1 | 27.6k -> 17.1k | 0 -> 0 | 0 -> 0 |
| 3 | 72k -> 41.5k | 0 -> 0 | 0 -> 0 |
| 4 | 103k -> 54.7k | 143 -> 0 | 0 -> 0 |
| 6 | 147k -> 81k (47 %) | 1119 -> 0 | 20 -> 0 |
| 8 (two octaves) | not measured -> 312k (179 %) | -> 1266 of 2127 | -> 420 |

- **Now the limit is the card at 8 voices**: about 490 KB/s delivered at 7 ms per 4 KB read (0.6 ms per sector: the token poll is one SPI call per byte and every sector is a separate 514-byte transfer plus a copy; the wire time would be 0.2 ms). An underrun sends a voice to the slow general loop (490 cycles per sample), which is why 8 notes also blow the CPU budget.
  Next steps (in order): (1) read a block's token and data in one SPI transfer for every sector after the first of a command (expected 0.6 -> about 0.35 ms per sector, exact); (2) larger / fewer commands (CMD18 for 16 sectors); (3) design choices for the user: voices playing the same sample share the blocks they read, a faster clock (since then `HWV1_SD_FREQ_KHZ` is 40000 in `sd_card_spi.cpp`, used only when the card accepts CMD6 high-speed, else 25 MHz;
  `HWV1_SD_OVERCLOCK=1` forces it), prefetch deeper.
- **Card handling** (new, tested with the old card on the board, and the screen in `ui_dump`): the card is polled once a second (no card-detect pin). A card whose sector read takes more than 15 ms is not used and the app shows "SD CARD TOO SLOW"; removal or a swap resets the catalog (loaded sample slots stay in memory until reboot: known debt) and the app rebuilds the synth (a 170 ms audio stall, once per card event).
  A failing mount attempt must never busy-wait (it starved the idle task before): the own driver returns quickly when no card answers.
- Task priorities are in DEVELOPING.md ("Tasks and priorities"). `samples_src/` + `build.ps1` (`-Sd E:` copies to the card) is the sample workflow; a name is the file's path below `system/samples`, 47 characters at most (longer: listed, not usable); only `.smp` is read on the board.
- `platformio.ini` (2026-10-07, set by the user): `-DENGINE_SR=44100`, `DEV_OUTPUT_GAIN_PCT=25`, `DEV_BOOT_DELAY_MS=500`, `DEV_SERIAL_CMD` on; **`HWV1_BENCH`, `ENGINE_PROFILE`,
  `HWV1_DEBUG_AUDIO`, `HWV1_DEBUG_UI` are commented out: turn them back on before any measurement (`serial_test.py` needs `ENGINE_PROFILE` + `HWV1_DEBUG_AUDIO`)**.

## Latest measurements (prototype, startup patch, mono FX + half-rate reverb, budget 160000 cycles per 32-frame block)

Measured with `apps/ritual_machine/tools/serial_test.py` on the last build that was on the board (engines still with the generic per-sample loop; the loop fast path below was built after it):

| notes held | cycles per block | % of budget | worst render (limit 1333 us) | blocks over budget |
|---|---|---|---|---|
| 0 | 30.1k | 19 % | 323 us | 0 / 3004 |
| 1 | 60.3k | 38 % | 669 us | 0 / 3755 |
| 3 | 119.4k | 75 % | 1186 us | 0 / 3755 |
| 6 | 208.5k | 130 % | 1955 us | all |

Per oscillator and voice (cycles per sample): add 263, modal 229, ssaw 237, vowel 217, dust 141, fm2 134, karp 117, fold 117. Fixed costs: reverb 20.5k, delay 8.4k (mono), chorus 0.4k. Per voice of the 4-engine patch about 24k
(engines 15k, filter 4.4k, Env 2.2k, Lfo / Shaper / Vca / VoiceOut / NoteIn the rest). Fast heap: 83.5 KB of 106 KB used, **0 bytes spilled**.

**Interpretation.** 4 voices of this patch fit with margin (about 130k at 4), 5 do not, 6 are at 130 %. The cost is now almost all in the oscillator engines (about 60 % of a voice); each engine sits at roughly 30 cycles per basic step, which is what
a table read plus a few multiplies costs on this in-order core, so further gains need fewer steps, not tuning. A profile inside the oscillator showed about 50-58 cycles per sample of loop overhead (gate test, trigger check, engine dispatch, level) around every
engine: the build made after these numbers adds a **fast path** (steady pitch and gate: one tight loop per engine). **It had not been measured on the board when this was written**: after flashing, rerun
`serial_test.py --engines "0,1,4,6;2,3,5,7" --chords 2` and `serial_test.py --chords 1,3,6`; expect each engine about 40-50 cycles per sample lower (about 4-5k per voice). If the per-engine numbers equal the table above, the old binary is still on the board.

## Full keyboard and key layouts (2026-10-05; built and host-tested, NOT flashed)

- All 36 keys of the prototype are mapped: the HAL matrix is now 8 x 5 (`KEY_COLS 8`); HWV1's 4 function keys are HAL row 0 (they were BTN 3 / BTN 1 / BTN 2 / PLAY), its
  4 x 8 note keys rows 1..4. `input_key_present()` (new in `hal_input.h`) says which keys a board has.
- Key functions come from a **layout** (`core/keymap.c`): 3 built in (*Keys 8x4* default, *Two 4x4*, *Notes+Nav*) + *User*, edited in the new **KEYS** menu tab
  (last tab in both modular and FM). User decisions: the three layouts as presets, user keys on the TF card (`keys.cfg`, text), only keyboard keys in the page,
  lock-out escape = Reset row + F1 held 2 s within 4 s of boot. Details in DEVELOPING.md ("Key layouts").
- Card writes: `sd_card_spi.cpp` got CMD24 single-block writes (+ CMD13 check); file jobs run on the `sd_io` task (`samples_esp32.cpp`, `hal/hal_storage.h`). **Risk the
  user accepted: a write bug can damage the card's file system.** (User, 2026-10-10: the card holds nothing important and may be erased; no backup needed.)
- Verified: simulator + firmware build; a scratch test (all layouts, User copy, text round trip, save on menu close, card in / out, KEYS-tab key selection, boot reset;
  it found and fixed a wrap-around that fired the reset at once). Not verified: anything on the board (key map orientation of the right half, card writes, keys.cfg on a real card).
- Board test (user, 2026-10-05): all keys OK, F1..F4 OK, `[SD] wrote /sdcard/keys.cfg (689 bytes)`, the layout survives a reboot, samples stream after a write. The F1 power-on
  reset did NOT work: the TCA8418 reports changes only and `kbd_init` drains its FIFO, so a key held from power-on makes no event. Fixed (built, NOT flashed): `kbd_init` reads
  F1 directly before the key scan starts (column driven low as a GPIO, row read; up to 2 s while held), `input_boot_reset()` in the HAL, the app resets at init;
  the "KEYS RESET" screen goes at the next key or after 3 s. Boot log: `[KBD] key r4 c7 held at power-on`.

## Sampler at high pitch (measured 2026-10-05, pad_c4 forward loop, `patch sampler 3 2`, which is Mono: one voice)

| note | stream speed | card | Sampler cycles per block | blocks over budget |
| --- | --- | --- | --- | --- |
| C4 (root) | 1x | 92 KB/s | 6.0k | 0 |
| C5 | 2x | 183 KB/s | 7.1k | 0 |
| C6 | 4x | 369 KB/s | 8.7k | 0 |
| G6 | ~6x | 546 KB/s, underruns | 10-19k | 21 |
| C7 | 8x | 640 KB/s (needs ~770), 160 underruns/s | **182k** (budget 174k) | 342 of 581 |

- Two problems: the card bandwidth (~640 KB/s at most) is reached by ONE voice at ~6-8x its root; and a starved voice cost ~20x CPU, because every sample ran 4 `fetch()`
  misses, each searching the ring by 64-bit atomic tags (a locked libatomic call on the S3). Fix (exact, built, 144 tests x 6 configurations green, NOT flashed / measured):
  `miss_blk_` in the Sampler, a missing block is searched once per process() call.
- **Optimization loop of 2026-10-06 (user allowed flashing for this task; all exact, measured on the board, details in ENGINE_DESIGN.md ADR-022 addendum 2):**

| case (25 MHz unless noted) | start of the session | now |
| --- | --- | --- |
| 1 voice C7 (8x): Sampler cycles / blocks over budget | 182k / 342 of 581 | ~28k (starved) or ~9k / 0 |
| 1 voice C7: underruns per 2 s | 320 | ~1 (25 MHz) / 0 (40 MHz) |
| 3 voices C5 E5 G5 (sum ~7.5x) | ~150 underruns per 2 s | 0-100 (card at its limit) |
| 6 voices C4..G5 (sum ~10x, needs ~880 KB/s) | render 1444 us, 325 blocks over budget | render 966 us, 2-7 over (card limited: ~500 KB/s) |

  Changes: Sampler `miss_blk_` and `starved_block()`, loader tag snapshot, `ENGINE_RING_BLOCKS` 12, driver token poll 8 bytes per call, data CRC check + retry, CMD6 attempt,
  `HWV1_SD_OVERCLOCK`, `seek avg` in the `[SD]` line (seeks cost ~100 us: not the problem), scattered-read bench. Test `a_stalling_card...` now stalls 1 s (longer than the ring).
  Speaker: `DEV_SPEAKER_DEFAULT=0` (user asked for the speaker off while testing).
- **Open, user decides:** (a) 40 MHz overclock: out of this card's spec, 0 CRC errors in 100 s of streaming, 4 KB file read 5.0 -> 3.7 ms; (b) 8 KB reads per command
  (2 ring blocks): ~15 % more bandwidth, loader + storage change; (c) pre-decimated copies of samples for high notes (converter; bandwidth stops growing with pitch,
  less aliasing, ~2x card space); (d) a faster card / the SDMMC 4-bit slot of the next prototype. **Not yet listened to** after these changes.

## Next performance steps (in the order I would take them)

1. Flash the fast-path build, measure, record the numbers here (and in ADR-035's table). Ask the user to **listen** to the list of unlistened changes above and to say which sound changes they dislike; each has a flag or a one-line revert (see ADR-035).
2. Remaining per-voice costs: Env 2.2k (two per voice: amp env + the filter's own, 47 cycles per sample, still 64-bit-free but not specialised), Lfo 1.8k, Shaper 1.8k, VoiceOut 0.9k. A fast path for "Env idle / sustain" and for constant LFO shapes is likely.
3. Engines: additive 263 (12 weighted adds + 11 recurrence steps; fewer harmonics at high pitch would help but changes the sound), modal 229 (8 modes x ~20), ssaw 237 (5 saws, the rest is mixing), vowel 217 (3 float filters).
   Ask the user before any change that alters sound (they decide quality trades; offer options with pros and cons).
4. Fixed cost: reverb 20.5k (the tank halves ~5k each at half rate; trimming one of the four input allpasses or the second decay diffuser saves 2-3k and changes the character; a cheaper diffusion structure was proposed, not tried),
   delay 8.4k (PSRAM Hermite read + write per sample, 5.2k).
5. Before a release remove the dev flags (list in DEVELOPING.md) and decide `DEV_OUTPUT_GAIN_PCT` (currently 25 %, platformio.ini), `ENGINE_FX_MONO`, `ENGINE_REVERB_HALF`.
6. Open hardware items: speaker amplifier check, display / audio separation check by ear, the TF card.

## Decisions that went against my recommendation (recorded with their risks in ENGINE_DESIGN.md)

The user decides and I record the reasoning and move on, without re-litigating:
- **All signals are q15 audio-rate blocks** (ADR-010): simpler, costs CPU for slow modulators.
- **Naive saturation** (ADR-013), **q15 block-floating-point FFT, N = 512** (ADR-016), **Dattorro plate reverb** and **fixed Juno-like chorus modes** (ADR-019 / 020), **AMY replaced outright** (ADR-027).
- **Own PIE block-float FFT kernel** (2026-10-07; I recommended ESP-DSP float + a real-FFT trick): risk = S3-only assembly checked by the boot bench, not by the
  host tests (the host runs the C path); built and measured: 16k cycles, 63-64 dB.
- **MODIFIERS tab covers the keys too** (2026-10-07; I recommended knobs + encoders + joystick): risk = a larger tab list (~60 controls on the board) and file;
  mitigated: Default entries keep the old behaviour, modifier keys and Menu-less lock-outs cannot happen through a layer (Shift / Mod keys are never remapped by it).
- **Synth types Modular / FM / Strings only, mono = Voices 1** (2026-10-09, ADR-041; I recommended dropping only Mod Para): risk = going from 1 to 2 voices
  silently loses legato / glide / the note stack; the Para switch does nothing with 1 voice.
- **Branch level / pan in one MIX cell at the start of row M** (2026-10-09, ADR-041; I recommended an OUT cell per row): risk = a special cell in row M, less
  obvious which row its values belong to.
- **Pads FX chains = a short chain of modules each** (2026-10-10, ADR-041 decision 23; I recommended one effect per chain): risk = CPU of up to
  ~12 effect modules next to 8 pad voices and the live synth.
- **Pads screen = grid full screen, a push opens the pad** (2026-10-10, ADR-041 decision 25; I recommended grid on top + pad below): risk =
  editing a pad hides the other pads.
- **A control table per board** (2026-10-13, ADR-043 decision 10; I recommended one shared control list): risk = the Ritual
  Machine's bindings, key layouts, layer entries and the control names saved in `ui.cfg` / `keys.cfg` are written against the
  shared list; the larger step, and the one that can break saved settings. Mostly taken away by decision 11 (the app keeps its
  role list and maps each board onto it).
- **Each app lists its own card folders** (2026-10-13, ADR-043 decision 13; I recommended shared samples and `apps/<name>/` for
  any other app): risk = no shared sample folder unless apps agree, two firmwares on one card can overwrite each other's files.
- **The simulator's panel drawn from a board's table** (2026-10-13, ADR-043 decision 14; I recommended moving today's two windows
  as they are): risk = `panel_sim.c` rewritten, about half a session more, a generic drawing in place of today's panel.
- **4 effect slots per FX chain** (2026-10-11, ADR-041 decision 47; I recommended 2): risk = twelve effects never fit (measured: four to
  six do next to 8 pads); nothing is refused, the FX page shows the estimated load.
- **Paulstretch with a 4096-point window** (2026-10-11, decision 44; I recommended 2048, which always fits): it fits (37 KB of PSRAM
  and 25 KB of internal RAM while the job runs), but leaves little room for anything else during a spectral job.
- On the hardware (ADR-035): a **float exception** for the filters; **mono delay / reverb** and the **half-rate reverb tank** as build flags; the **5-saw supersaw**; the sample rate is **never below 44100 Hz**; modal as phasors and the additive recurrence were done before asking
  because their tests show the same sound within a stated tolerance.

## Known debt / small issues

- **The start** (2026-10-11): 5.1 s to the UI on the user's card. Not looked at: why the first step (the settings read from the
  card again + the synth's rebuild) holds 0.6 .. 1.06 s; `app_init` reads the same files a moment before when the card is already
  mounted. The old demo files (`pad_c4`, `kick`: no overview in the header, 65 reads) cost 0.39 s each at every start: cook them
  again. The simulator's boot animation has no loading bar and does not loop (it loads at once).

- **The sample library** (2026-10-11): 32 different samples in use at once (64 pads cannot all hold a different file); pads.cfg
  must fit 4 KB and now stores paths (64 pads with long paths can pass it: the end is cut silently; a file per bank would fix it);
  a folder shows 2048 names / 30 KB of names at most (the card's 1800 use 28 KB); every open of a file searches its folder from the
  start (0.4 s for the last of 1800: ADR-041 "On the board"); a long name is cut at the row's edge; no audition from the SAMPLES tab;
  `apps/ritual_machine/tools/sample_sim_check.cpp` still expects the old scan at start (not updated, not built by any script).

From the session todo (long-term; moved here 2026-10-11):

- **8 x 8 grid layout, the other screens**: step 1 built (top bar 16 px, RACK cells 32 px); list rows (9 px) and parameter pages still off
  the grid. Do it together with the phase-1 RACK tab where they overlap (from: user)
- **Module first page + cog: detailing**: works, the user will detail it later (EG / MOTION old layout, hidden settings of the other
  modules, one cycle in the previews) (from: user)
- **UI sprites**: pipeline built; the user paints the placeholder PNGs (from: user)
- **MI follow-ups**: loudness per model, denormals (Plaits drums), IRAM, Harm placement, tier 3; cycles per model (`[MI]`) not recorded
  here yet (from: notes)
- Quick macro UI button; declarative main view (UI_GUIDE.md section 8); "audio backend" interface findings (from: user + debt)

- ui.cfg is written into one `STORAGE_FILE_MAX` (4 KB) buffer: 64 macro destinations (~2.2 KB) plus many Shift / Mod key entries can pass it, and the end is
  cut silently (the cut lines are lost at the next load). Fix if it bites: split macros into their own file, or write in chunks. Min / Max step by 5 %.

- UI: the main view (pages) is not a declarative screen yet (no latch, old events); the GENERAL tab has up to 9 rows (Glide / Legato only with Voices 1) and scrolls; the Knob mode, jump slots and Shift-knob targets are saved in ui.cfg (2026-10-07);
  the sim's panel does not draw the key LEDs; this session's UI changes were verified on the board by the user, not by host tests (the 160 engine tests do not cover `apps/ritual_machine/src/core/ui_*.c`; `apps/ritual_machine/tools/ui_dump.c` can).
- Sample library: whole files are copied to RAM in the simulator; loaded samples are never unloaded; imports block the UI thread; a rack stores the file *index*
  (names needed when saving). Zones are engine-only (no UI). MS targets are limited to what the mapper realises (OC Pit / Lvl / PW, FL Cut, SA Drv, SM Pit).
- Sound-design round (ADR-031..034), not listened to: the convolver (PIE since 2026-10-09: 10k cycles per block at 128 taps, ADR-034), the compressor / shifter / engines are tested against theory only,
  the engine previews in the OSC graph box are sketches.
- `samples/` holds the generated demo files (`framework/tools/make_demo_samples.py`); `lib/minimp3` is not versioned like the other libs.
- `synth_params_t` and `param_id_t` still contain the old oscillator / filter fields; only the amp envelope is used.
- Rebuilding on menu close keeps held notes (state by node id), but removing the module a note uses silences it. Mapper limits: no modulation of resonance, mix or other modulators; MS has no sound.
- `svf_tick` (integer, 202 cycles) and `svf_coef_f` remain in `dsp/svf.h` only as the references the boot benchmark compares against; the engine uses `svf_tick_f`.
- The reverb / delay stage timers (`SEC_BEGIN` / `SEC_MARK`) and the oscillator counters stay in the code under `ENGINE_PROFILE` (zero cost without it).
- `framework/tests/test_sampler.cpp` has a `run_trace` helper that prints underruns. Engine items under "Known limits" in ENGINE_DESIGN.md (reverb q15 floor, block-rate envelopes, mono samples, ...).

## Working style and environment traps

- The user iterates with short requests, tests on the board or in the SDL window, and wants concise answers that say what was verified vs not. Windows 11, PowerShell, MSYS2 UCRT64; AZERTY keyboard; English with some French.
  For design work they want options with pros and cons at each stage; they often pick against my recommendation. **By default they flash the board themselves** (COM8). On 2026-10-10 / 11 they allowed Claude to flash and measure (asked each time: "flash and measure?"): ask again in a new session before the first flash. They sometimes say "flashed" about an older build: check (see the skill).
- A profiling build goes through the environment, not platformio.ini: `$env:PLATFORMIO_BUILD_FLAGS = "-DHWV1_DEBUG_AUDIO -DENGINE_PROFILE"` then `pio.exe run -d apps/ritual_machine -t upload`; **flash the normal build again afterwards** (the profiling one prints every second and costs RAM).
- Board tools for the Pads work (all over serial, `DEV_SERIAL_CMD`; see human_docs/SERIAL_COMMANDS.md, `pad ...`, `dir`, `use`): `apps/ritual_machine/tools/library_test.py` (the folder list, header reads by place in the folder, samples cycled on a pad, a stream while a folder is read), the serial command `browse N REST FIRST` (the browser driven as by hand: how long each UI step took, stream underruns; `serial_test.py --cmd "pad on" --cmd "browse 6 1500 1750" --raw --seconds 12 --log run.txt`, then read the log: `--raw` does not print the replies), `apps/ritual_machine/tools/pads_test.py` (pad voices), `padsfx_test.py` (FX chains, per-module costs), `edit_test.py` (sample jobs: time, memory), `capture_test.py` (recording).
- UI tests address some rows by number (the RACK page's menu rows in `apps/ritual_machine/tests/ui/test_rows.c`, the EDIT page's rows in `apps/ritual_machine/tests/ui/test_pads.c`): inserting a row in those screens shifts them.
- The user often keeps the simulator open: `build.ps1` then builds `oled_sim_new.exe`. A stale exe has caused a false "bug" before.
- PlatformIO: use `C:\.platformio\penv\Scripts\pio.exe`, **never a bare `pio`** (PATH points at Python 3.13's pio, which recreates and destroys the penv). It happened twice; the second time (2026-10-05) Claude did it, and the user had to restore the penv by hand. Repair steps: DEVELOPING.md, "ESP32 firmware" warning. One link failure with no error text happened twice; the immediate rerun succeeded.
- One program per serial port: close the serial monitor before `serial_test.py`. Before an upload, check that the board is there
  (`[System.IO.Ports.SerialPort]::GetPortNames()`): switched off or unplugged it is on no port, and `pio run -d apps/ritual_machine -t upload` fails after
  the whole build ("Please specify `upload_port`"). A build to check only: `PLATFORMIO_BUILD_DIR` in the scratchpad.
- Start-up traces: `-DBOOT_TRACE` (DEVELOPING.md, build flags) prints `[BOOT]` lines; found with it on 2026-10-11: an input event
  that waits in the queue at start acts on the UI's first step (the joystick switch read as pushed).
- Bash tool: **heredocs containing apostrophes or quotes break; `\n` inside a C string written from a Python literal becomes a real newline** (it hit `printf` lines many times: grep for a string that wrapped, or fix with a regex on `%u[\r\n]+"`).
  Write multi-line scripts with the Write tool and run them; edit with the Edit tool. Many sources are CRLF: scripted edits must keep each file's line endings.
  **`sed -i` in this Git Bash rewrites a CRLF file with LF endings** (hit twice on 2026-10-09; git hides it in the diff): use a Python script that reads
  with `newline=''` and writes the endings back, and check with `file <path>` after any scripted edit.
- `apps/ritual_machine/src/platform/engine/engine_synth.h` is included from C: no default arguments, no C++ in it.
- Tool calls that run `g++` through PowerShell hide compiler errors (`NativeCommandError`): compile from bash to see them. A test executable needs `C:\msys64\ucrt64\bin` on PATH.
- Python on Windows: always `open(..., encoding="utf-8")`. The test runner takes a name filter (`engine_tests.exe reverb`) and, via the script, engine flags: `test.ps1 -Defs "-DENGINE_REVERB_HALF=1"`; `-Matrix` takes a few minutes, one configuration about 1 min.
- Do not commit unless asked. (The Pads work: the user asked for a commit at every step; it was on the branch `pads-step1`, merged into `main` and removed on 2026-10-11. Ask where the next work goes: `main` or a new branch.) Delete `run*.txt` serial captures when done (ignored by git now); keep the numbers in this file.

## Starting the next session (compact prompt)

Run `/session-start` (skill `.claude/skills/session-start/SKILL.md`): it reads these notes, talks through the open items, writes the "Session todo" section here and proposes where to start.
For board performance work directly:

> Read `CONTINUE.md`, `DEVELOPING.md` and `ENGINE_DESIGN.md` (ADR-035, and ADR-041 for the Pads section), and the skill `esp32-optimize`. The work is on `main`. The board is connected on COM8 with the last build flashed. <what I heard / what to change>.
> Then re-measure with `apps/ritual_machine/tools/serial_test.py` and continue with "Next performance steps".
