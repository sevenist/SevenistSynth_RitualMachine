# Update log: the Ritual Machine

What changed for the player and, under "For developers", in the app's code. The library's changes: [framework/CHANGELOG.md](../../framework/CHANGELOG.md).

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
- **For developers:** the repo is now a library and the apps built on it. Its guide: [framework/CHANGELOG.md](../../framework/CHANGELOG.md), 2026-10-13.

## 2026-11-08: the pots say what they do (ADR-047; branch `gui-reorg`)

- **Shift and Mod work in Pads too**: holding one, the knobs do what its layer says, and the legend, the pot LEDs and the
  header's "S" / "M" show it.
- **The pot LEDs follow the legend everywhere**: on the SEQUENCER and the CURVES tab they light; in Pads they show the pad's
  value; behind PERFORM or SETUP over the pads, where the knobs do nothing, they go dark.
- **The legend's tick follows the knob at once**, before the knob has caught the value. Until now it moved only when something
  else redrew the screen.
- For developers: one module, `app/app_knobs.c`, says what every pot drives now; the legend, the LEDs and the catch read it.

## 2026-11-08: the second prototype (GRV-mini) runs the Ritual Machine

- **The firmware runs on the second prototype**, with the controls of the simulator's panel: its 128 x 160 colour screen (the UI as on the
  first board, at its height), the 4 x 4 keys and the function row, the 8 pots with their LEDs, the two push encoders, the joystick and the
  four buttons. Flash it with `.\upload.ps1 -Env hwv2`.
- **The power switch turns it off** on the battery, after saving. On USB the synth goes on; once a battery is in, it can become the card reader
  as on the first board.
- **The LEDs go dark when the power goes**, so they start dark too.
- **A VU meter** on its six LEDs follows what the synth plays (a first version: the codec is not fitted yet, so there is no sound).
- The first prototype is still built and supported: both boards now share their ESP32 code.

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
