# Update log: SynthLab Studio

The library's changes: [framework/CHANGELOG.md](../../framework/CHANGELOG.md).

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
