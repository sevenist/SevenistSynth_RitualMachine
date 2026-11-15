# Update log: SynthLab Studio

The library's changes: [framework/CHANGELOG.md](../../framework/CHANGELOG.md).

## 2026-11-15: the wallet prices an engine synth

- The cycles wallet also shows the patch as an engine synth: "CPU 45 % (engine 63 %)", a dotted tick on the bar and its cycles in
  the hint. These are the modules' measured costs as engine types (module_costs.h `engine`).

## 2026-11-15: Export > Engine synth

- **PATCH > EXPORT > Engine synth**: the patch as a SynthLab synth whose voices are an engine graph (`apps/synthlab/src/synths/
  eng_<name>.cpp`, added to SynthLab's list). It has the same values as Synth's export, its voice count, and registers only its
  modules. Played on HWV1 (the default patch at 3 voices: 57 598 cycles a block with 3 notes held) and through SynthLab's tests.

## 2026-11-15: Export > Engine writes every module

- **Export > Engine:** a module the engine has nothing like (Body, Metal, Perc, Ratchet, Glue, Gate, Math ...) is no longer listed as
  "new modules needed". It goes into the graph as the catalog's own module made an engine type (`FBody`: its float code, its values;
  framework/modular/engine_modules.h). The file's `register_<name>_types(reg)` registers exactly the ones it uses.

## 2026-11-15: the cycles wallet

- **PATCH: the cycles wallet**, in the strip above the canvas: what the synth exported from the patch costs on the board, against
  its budget. The bar shows the engine, a slice per voice and the FX, a tick for the modules' slowest values, and turns amber or red
  as it fills. Its hint gives the numbers and how many voices fit. The costs were measured on HWV1. Checked on the board: the
  default patch at 3 voices was estimated within 1.3 %.
- **The patch's voice count** (the stepper, 1..6, saved in the `.patch`): the patch's designer sets it from the wallet. Export >
  Synth writes it into the synth (it plays at most that many voices), and the Modular synth plays it here too.
- Each module's header shows its cost in thousands of cycles a block ("?": an engine module, not measured).
- Export > Synth: the generated render runs from internal RAM on the board (`SC_HOT`), as the measured costs assume.

## 2026-11-15: Macros in the patch

- **A Macro module** (PATCH, the list's Macros kind): a named knob, Value 0..100 % out as 0..1. Cable it onto values, or onto
  another Macro (a layer). The inspector renames it (a name another Macro has, or none, is refused).
- The Modular synth lists the Macros first, `MACROS/<name>`, then the modules as before.
- **Export > Synth of a patch with Macros**: the synth's values are its Macros alone, everything else fixed in the generated code (the
  synth's player gets abstract parameters); its sound keeps the Macros' values alone. Twins are refused. Export > Engine writes a Macro
  as the engine's Const.

## 2026-11-15: the library keeps Basic

- The library of SynthLab synths rebuilt from modules (`apps/studio/library/`) keeps Basic only: FM2's and the Kick's rebuilds are
  removed (the PATCH tab's Open lists Basic; the self-test nulls it alone). SynthLab's Kick Lite is gone too.

## 2026-11-15: an exported synth takes the patch's sound

- PATCH > EXPORT > Synth also writes `presets/<name>.sound`, the patch's sound (its values, macros and name) as the exported synth's,
  when that file is not there yet: built, the synth starts as the patch sounded, ready for SOUND > To the card. A sound that is there
  is kept.
- A patch of more than 128 values (a title per module included) is refused by Export > Synth: the board's synths have 128 at most
  (the studio lists 256), and the synth would not build for it.
- The self-test checks that an exported synth names its values as the Modular synth does, for the starting patch and every patch of
  the library (what lets a sound made on the patch load on the exported synth).

## 2026-11-15: named sounds, one file block on every tab

- **Sounds have names, and each synth a library of them.** At the bottom left of SOUND: the sound's name (Rename), New, Save as
  `presets/sounds/<Synth>/<name>.sound`, the synth's saved sounds and Load. The name is in the file (`name "Hollow Kick"`).
- **EXPORT > To the card** writes the sound as the board reads it, `synthlab/sounds/<Synth>.sound`, in SynthLab's card folder
  (`apps/synthlab/sdcard/`): copy that folder to the board's card.
- **The same file block on the three tabs** (`file_block.h`): PATCH (as before), SOUND (above), LAYOUT (one layout per synth: Save and
  Revert of the layout alone; no export yet).
- **Fixed:** in PATCH, a module scrolled up under the VOICE / FX tabs was drawn over them; the tabs are now drawn last.

## 2026-11-15: the sound file is the library's

- **Fixed: a value whose name holds a space** (RUMBLE/Dly Mix, Env Amt, High Hz ...) did not come back when a sound was opened: it
  was written without quotes and read only up to the space. It is now written in quotes, and the sounds saved before read right
  (in a value line the number is the last word, the path everything before it).
- The `.sound` file's reader and writer are the library's (`framework/control/preset.h`), the text a board will read; nothing else
  changes in the file. `params.cpp`'s paths and words are the library's too.

## 2026-11-14: a macro can drive another macro

- SOUND: drop a macro's handle on another macro's knob and the first drives the second (and the second its own values); the
  second's knob shows the range as a ring, dragged like a value's (right click: unlink). A link that would loop back is refused.
  Saved in the `.sound` file as `link N min max curve` lines (older files read as before).
- The macros are the library's (`framework/control/macro.h`, the Ritual Machine's model and code): `Macro` is its `mac_t`, a
  destination its `ctl_map_t` (`dest_param` / `dest_macro` say what it drives); `macro_apply` takes the set and the macro.

## 2026-11-14: the layout's new kinds and texts

- LAYOUT: three new kinds that show a value (Fader, Toggle, Choice) and three decorations (Label, Line, Box: a click on their
  button adds one).
- Any element's name can be replaced by your own text: Enter or a double click on it, or the inspector's Text; Name goes back to
  the value's. The synth's name in the top title too. Saved in the `.layout` file (older files read as before).
- Typing a name no longer lets Enter press the focused Done / Rename button again (it reopened the rename); the SOUND tab no longer
  shows the patch's or a label's text as the macro's name.

## 2026-11-14: math modules, names, selection and the clipboard

- Four modules under Math: Math (A op B), Func (abs, invert, quantize, wrap ...), Range (a range converter) and Slew (a lag).
- The Ritual Machine's choices show their names: an oscillator's engine (Karp, Modal, Fold ...), MiOsc's models, the filter's
  mode, slope and algorithm, the shaper's modes; on / off values read Off / On.
- A selection of modules: drag the empty canvas for a zone, Ctrl / Shift click a header to add one; the selection moves together.
  Ctrl+C / X / V copy, cut, paste it with the cables between its modules; Ctrl+D duplicates, Ctrl+A selects all, Delete removes.
- Looking around is now the middle drag or Alt + drag (the left drag draws the zone), the wheel and the arrows as before.
- Right click a jack: a menu, "Remove cable" / "Remove N cables" (it used to remove them at once).

## 2026-11-14: modules in both areas

- Every module goes in VOICE or FX, except the ends (VoiceOut; Voices, Output): no module is greyed out for its kind any more.
- The FX area follows the last note played (pitch, velocity, the gate while any key is held, a start at each note). An envelope,
  a Body or a Note on the FX plays once, on the sum. An exported synth does the same.
- The Ritual Machine's per-voice modules (RMEg, RMOscEngines ...) go on the FX, its global effects (RMDelay, RMReverb ...) in a voice.
  Export > Engine lists those as modules the engine needs in that scope; their tag is dimmed in the list.
- A Modular voice has 2 MB (was 1 MB); the inspector warns when a patch is too big.

## 2026-11-14: the module list by kind

- The Ritual Machine's modules are filed under their kind (Sources, Envelopes, Filters, Shapers, Utilities, Effects), after the
  catalog's own, and marked RM in yellow. The list takes the panel's whole height (the patch's buttons at its bottom).

## 2026-11-14: the Ritual Machine's modules

- The module list has a new category, **Ritual Machine**: 34 of its modules, played by the engine's own code.
  - **Sources:** its oscillators (Osc, the 9 oscillator engines, the Mutable engines, Strings).
  - **Envelopes and modulators:** its envelopes (Env, Eg) and its LFO.
  - **Filters, shapers, utilities:** its filter, shaper, VCA, mixer, tuned comb.
  - **Effects:** its delay, reverb, chorus, ensemble, spectral FX, vocoder, phaser, flanger, tremolo, compressor, EQ and pitch
    shifter (those with a per-voice version have both).
- A pitch input follows the key and a gate input the voice unless a cable comes in. Values are the engine's, levels as % and
  pitches as semitones.
- Export > Engine writes them as themselves; Export > Synth refuses a patch that has them.
- Left out for now: the sampler, granular and pads (they need the card's samples), the motion sequencer, the paraphonic gates, the
  convolver and the DX7 (they need data only the Ritual Machine sends).
- `--modules`: the catalog, one line each. The self-test: 6557 checks.

## 2026-11-14: cables off the canvas

- Fixed: a cable whose end was scrolled off the top or the left of the PATCH canvas drew long horizontal and vertical strokes across
  the window (found by the user). Lines are now clipped to the frame before u8g2 draws them (`wg_line`).

## 2026-11-14: a resizable window, full screen

- The window fills with no black border at any size: the frame follows the window (at least 800 x 480, scaled by the largest whole
  number that leaves that much), and every tab lays itself out in the room there is. **F11** (or Alt+Enter): full screen.
- `--size WxH` draws `--shot` at another size.

## 2026-11-14: KEYS

- **KEYS**, a button in the top bar (and F4): the computer keyboard plays notes while it is latched. It is latched at start;
  unlatched, the letters play nothing and the notes they held are let go.

## 2026-11-14: SynthLab's synths open as patches

- Basic, FM2 and the Kick are rebuilt from modules (`library/`), each checked against the synth itself with a null test: 74.3, 90.7
  and 71.6 dB (60 dB and more: the same sound). The PATCH tab opens them (under Load) to rewire them.
- New modules: Op (an FM operator), Rumble (the Kick's whole rumble), Comp3 (its 3-band compressor), Limit. New settings: Perc's Law
  (Power) and Steps (Block), Body's Steps, Noise's stream B; the Filter reads its Cutoff input at the block's first sample.
- `--null <synth> [--patch file]`: the null test from the command line (`STUDIO_NULL_TRACE=1`: where the difference lies, 5 ms at a
  time). `--scale N` for `--shot`.
- Fixed: a patch's values were reset to the catalog's defaults the first time the Modular synth was chosen; a seeded Noise played the
  unseeded noise on the very first note.
- The self-test: 6079 checks (the three rebuilds' null depths included).

## 2026-11-14: PATCH, synths built from modules

- **PATCH** (F3), a third tab: a synth built from modules and cables on a canvas, the **Modular** synth. The modules are the
  library's new modular catalog: 23 of them, gathered from SynthLab's synths (the Kick's body, drive, room and glue, the Snare's
  wires and gate, the hat's metal) and the basics. There are two areas (per voice, and once on the sum); cables go to inputs or,
  as modulations, to values.
- The Modular synth's values are the patch's, so the SOUND tab's knobs and macros and the LAYOUT tab work on patches too.
- **Exports:** a SynthLab synth (`gen_<name>.cpp`, built with SynthLab, runs on the board), the Ritual Machine engine's graph
  (with the modules the engine lacks), the patch as text. Also from the command line: `--export synth|engine|patch`.
- Patches saved in `presets/patches/`, the Modular synth's in `presets/Modular.patch`.
- The self-test grew to 5994 checks: the catalog, the runner, the Modular synth through the engine, the exports, and the canvas
  driven by scripted mouse input.

## 2026-11-14: the first version

- A desktop program for SynthLab's synths, worked with the mouse and the keyboard ([README.md](README.md)).
- **SOUND**: every value as a knob, by section; eight macros (the Ritual Machine's model: up to 8 destinations each, with Min, Max,
  Curve), assigned by dragging a macro's handle onto a knob, or with Ctrl+1..8; the macro's range as a ring around the knob, dragged
  to set it; the destinations' list and the response under the macros.
- **LAYOUT**: the board's screen at twice its size, drawn as the board would draw it; rows, knobs, bars, titles and big values
  placed by dragging, moved, resized, snapped, over pages; automatic layouts (rows, knobs); overlaps and values on no page shown;
  TRY works the screen as the encoders would.
- The values, the macros and the layout are saved per synth as text (`presets/<synth>.sound`, `.layout`).
- `--selftest` (5104 checks: the models, the widgets, the window driven by scripted input) and `--shot` (one frame into a PNG).
