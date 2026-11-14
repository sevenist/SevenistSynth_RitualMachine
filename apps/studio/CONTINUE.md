# SynthLab Studio: handoff notes

The app's state and todo. Its controls and files are in [README.md](README.md). The library's notes, the user's working style and
the traps of the environment are in the root [CONTINUE.md](../../CONTINUE.md).

## Session todo (updated 2026-11-14)

**Where things stand (2026-11-14):** the user asked for two tools: a UI editor to try arrangements of the screen for different
engines, and a sound design tool with macros in a Vital-like UI (the Ritual Machine's macro building), with the mouse and the
keyboard ("need to add mouse to the ui engine"). Their answers to my questions: **SynthLab's synths first; a large desktop window;
desktop only for now (the board does not read the files); one app with two tabs.** Built on branch `synthlab`, not committed.

What was built:
- **The library:** `framework/ui/widgets.h` (immediate-mode widgets for a pointer and a keyboard: knob with a macro ring, button, tab,
  stepper, slider, focus order with Tab, drag and drop; portable C, it compiles for the ESP32-S3), `framework/boards/desktop/desk_window.h`
  (a desktop window of any size that shows an RGB565 frame at a whole zoom and reports the mouse and keys as `wg_input_t`), and
  `CANVAS_MAX_W / H` can be raised with `-D` (the studio: 800 x 480).
- **The app** (`apps/studio`): `params` (values <-> knob positions, names, the files' words), `macros` (the Ritual Machine's
  built-in curves and mapping), `layout` (the arrangement model and the board's drawing), `tab_sound`, `tab_layout`, `main`
  (the window, the top bar, notes, files), `selftest`.

Verified: `--selftest` passes (5104 checks, including the window driven by scripted input: a macro dropped on a knob, its ring
dragged, the macro turned); the two tabs looked at in PNGs (`--shot`); the four other apps' desktop programs still build;
`widgets.c` passes the xtensa compiler's syntax check. **Not verified: the window by hand (mouse, keys, sound): the user has not
tried it yet.**

The choices I made (to correct if they do not fit):
- The **macro model is the Ritual Machine's** (a macro sets its values to Min..Max through a curve, absolutely), not Vital's
  (modulation added to the knob's own value). The knob shows Min..Max as its ring; a new destination is centred on the value so
  nothing jumps.
- Min / Max are **positions along the knob's travel** (an Exp value: along its logarithm).
- The Ritual Machine's `curves.c` was not moved into the library: the studio has the 4 built-in curves (same formulas); no user
  curves.
- **One file pair per synth** (`presets/<synth>.sound` with the values and the macros, `.layout`), no preset browser, no undo.
- The window is 800 x 480 pixels in the 6x10 / 5x7 pixel fonts, scaled by a whole number (2 on a 1080p monitor).
- The layout's elements: row, knob, bar, title, big value; TRY's order is top to bottom, then left to right.

**Then (2026-11-14, same session): the modular environment** (the user: "make possible the creating of synth with modules directly
... gathering common modules used in the synthlab and having a real modular environment that i can then export informations to
then implement it in the ritual machine engines and rework the existing ones"). Their answers: **a shared float catalog in the
library** (shaped to the engine's limits), **a free canvas with cables**, exports **patch file + generated SynthLab synth + engine
graph code** (not a Markdown report), **two areas** (VOICE, FX). Decisions: framework/docs/DESIGN.md ADR-049; the guide:
framework/docs/MODULAR.md. Built: `framework/modular/` (SynthLab's `lab_dsp.h` moved there as `mdsp.h`, 23 modules, patches, the
runner), the PATCH tab (`tab_patch.cpp`), the Modular synth (`modular_synth.cpp`), the exports (`export.cpp`), `--export`.

Verified:
- `--selftest`: 5994 checks.
- SynthLab's tests unchanged after the move: 2217 checks, the same null depths.
- A generated synth (a test drum patch using most of the catalog) passed SynthLab's per-synth tests (976 checks, after two
  real faults of my patch that those tests caught) and the ESP32-S3 compiler's syntax check.
- The init patch's engine graph loaded into the real engine (with `register_synth_modules` / `register_fx_modules`) and
  played: a 1 s note peaking at 22690.
- The drum patch's graph loaded with its six missing modules listed.
- The test outputs were removed afterwards: no `gen_*.cpp` is left in SynthLab, and `exports/` is empty.

**Not verified:** the canvas by hand; how the engine's conversions sound against the studio's.

My choices to confirm:
- Modulation of a value is block-rate (the source's mean over 32 samples), not per sample.
- A change of modules or cables rebuilds the voices (a sounding note is cut).
- The Modular synth is polyphonic (6 voices) with no mono / glide setting.
- An exported synth's defaults are the patch's values.
- The engine conversions marked "approximation": Room -> Reverb, Perc's curve sign, Drive -> one Shaper mode.

**Then (same day): the other engines in PATCH.** The user: "the patch section is only for modular, is it possible to extend its use to
edit the other engines?" Their answers: **both SynthLab's synths and the Ritual Machine's racks; SynthLab's synths rebuilt as
patches** (not one module each), checked with a null test. Done (ADR-049's addendum):
- `--null`, `nulltest.cpp` and `library/`: Basic 74.3 dB, FM2 90.7, Kick 71.6.
- New modules Op, Rumble, Comp3, Limit; the Steps / Law / stream settings.
- The PATCH tab's "Open".
- A rebuild exported as a synth plays bit for bit like the patch (177.6 dB); that test export was removed.

Learned: to null, a module must compute as the synth does. The Kick per sample: 33 dB; with its block-rate envelopes: 72 dB.

Next, in that order (the user's plan):
- [ ] Kick Lite (its O1 distortion table, its skipped triangle), the Snare (ring, wires = Ratchet + its noise EQ, its gate and master),
      DrumLab (metal, grooves: PLAY's loop is a synth option, not a module), each to 60 dB or more, the gaps named.
- [ ] The Ritual Machine's racks. The plan: an adapter module hosting any engine module (its ModuleInfo -> a ModuleDef; q15 <-> float
      at the block's edges), so a rack opens as a patch of its own engine modules, exactly, and Export > Engine writes it back as the
      same graph. The rack's rules (rows, Para) do not hold in a free patch: reading a rack is easy, writing one back is not.
      How a rack reaches the studio (the Ritual Machine's patch text, or its graph dumped) to decide with the user.

**Then (same day): every module of the Ritual Machine in the catalog** (the user: "add every module already in the Ritual Machine's
modular synth"). Done:
- The adapter, `framework/modular/engine_adapter.h`, hosting the real engine modules: 34 entries.
- What it leaves out, and why: framework/docs/MODULAR.md, "The Ritual Machine's modules".
- Checked by the self-test, and a patch of them exported, loaded into the real engine and played (peak 8035).
- Not tried by hand: their sound in the window.

Still open around it:
- [ ] Their values show the engine's names and units (`t1`, `cut_mod`, `engine 0..8` without the engines' names). Labels for the
      enumerations (the 9 oscillator engines, the filter modes) would need a table in the adapter: propose it.
- [ ] Dx7 (needs a DX7 patch: the Ritual Machine's factory patches, or an editor), Convolver (its IR), Sampler / Granular / Pads (a
      sample bank in the studio), MotionSeq (the step sequencer).
- [ ] A rack read as a patch (the plan above) is now possible with these modules: ask the user how a rack reaches the studio.

**Then (same day): modules in both areas** (the user: "the majority of modules free to go on both sections, the monophonic state
of the actual exported synth would be added afterwards after some measurements of real performances"). Done, ADR-049 addendum 3:
- Every module but the ends goes in either area.
- The FX area follows the last note (`mod::LastNote`): **my stand-in for the mono state the user will design after measuring.**
- Export > Engine lists a module whose engine type is in the other scope only.

Checked: self-test 6633; a generated synth with FX envelopes through SynthLab's tests and the xtensa syntax check (removed after).
Not heard by hand.
- [ ] The mono state (the user's, after measurements): what the FX's last note should become (priority, legato, retrigger), and a
      mono / legato / glide setting for the Modular synth and the generated synth.

Next (to propose to the user):
- [ ] The user tries the PATCH tab; fix what they find.
- [ ] The Ritual Machine's rework: write the modules the engine lacks (Body, Metal, Ratchet, EQ3, Glue, Gate) as engine modules
      (q15), each checked against its catalog original with a null test as Kick Lite was; then the engine export carries them.
- [ ] Patch options: mono / legato / glide, PLAY's tempo and grooves (lab::Options) for a patch, and in the generated synth.
- [ ] Per-sample modulation for chosen values (an "audio-rate" flag), if block-rate is heard.

Next, from the first version (to propose to the user):
- [ ] The user tries the window; fix what they find.
- [ ] Macros as modulation (Vital's way: added to the knob's value, bipolar) as a choice per destination?
- [ ] The board reading `.layout` / `.sound` (SynthLab's screen drawn by `layout_draw`; the macros on the HWV2 knobs).
- [ ] The Ritual Machine's engines in the layout editor (needs its screens as data: `ui_screen.h`'s layout callbacks).
- [ ] Undo; several sounds per synth; user curves.
