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
- [ ] FM2 and the Kick again (their rebuilds were removed on 2026-11-15), the Snare (ring, wires = Ratchet + its noise EQ, its gate and master),
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
- [x] Their values show the engine's names and units (`t1`, `cut_mod`). The choices now show their names (2026-11-14, the
      adapter's table: ADR-049 addendum 4).
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

**Then (same day): math, names, editing** (the user: math modules and a range converter, the engine's names instead of numbers,
copy / paste and zone selection, a menu on a jack's right click). Done, ADR-049 addendum 4: Math / Func / Range / Slew; the
engine's choices by name; a selection (zone, Ctrl / Shift click), Ctrl+C / X / V / D / A, Delete; panning on the middle button or
Alt + drag; the jack's menu ("Remove cable(s)"). Checked: self-test 6715, a screenshot. Not tried by hand.
- [ ] More items for the jack's menu when the user asks (the user: "only option for now").
- [ ] The clipboard as patch text (between studios, into a file): not done, it lives while the studio runs.

**Then (same day): the layout's kinds and texts** (the user: "need some more widgets to place in the layout editor, also a way to
edit text label (titles etc)"). My choice of widgets (to correct): Fader, Toggle, Choice (they show a value), Label, Line, Box
(decorations). Any element's text replaces its name (Enter / a double click / the inspector). The keys go to the text alone while
it is typed (a fix: Enter used to press the focused Rename again). Checked: self-test 6810 (each kind drawn inside its rectangle,
following its value; the file with every kind and texts; the tab by scripted clicks and typing); a demo page looked at. Not tried
by hand. Not proposed yet: an envelope graph (4 values in one element), a meter, an icon.

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

**2026-11-15: Macros as modules** (the user: "create macros as modules, just to have parameter layers inside the engine (the goal
is to create abstracted parameters for the end user of generated synth)"). Done, ADR-049 addendum 5: the Macro module, its name, the
MACROS section first in the list, Export > Synth of a patch with Macros keeps them alone. Self-test 6741; a generated Macro synth
compiled. Not tried by hand. My choices to confirm:
- A Macro is 0..100 % and acts through modulation cables (added to the target's own value, linear along its range): a curve or a
  range of its own needs Func / Range in between.
- With Macros, an exported synth has **only** them: a raw value the player should keep needs a Macro of its own (target at its
  min, depth 1: the whole range).
- One Macro is one area: a Macro of the VOICE area cannot reach the FX (cables stay in their area).

**2026-11-15: the user's note, the voice count and a cycles wallet** (for later, or this session): "the voice count will be locked by
the patch designer according to estimation of budget ... make measurements of cycles per block to estimate a cost per module, and
add to the ui a "cycles wallet" to help the designer to budget for voice count". Not started. The plan:
- [x] **Done 2026-11-15** (HWV1; the table: `framework/modular/module_costs.h`, `mod::module_cost(name)`, `mod::kBoardCost`; how:
      apps/synthlab README "The modules' bench"). The modules' code is in internal RAM on the board (SC_HOT), so a voice pays `cycles`.
      Engine 7 691 a block + 3 310 a voice; a generated voice's own work 915; a modulation cable 270 (Lin) / 1 534 (Exp).
      The plan as written: **Measure on the board:** a dev build of SynthLab (a build flag, as the Ritual Machine's HWV1_BENCH) that runs each catalog
      module's process on a 32-frame block, with its default values, from the memory the engine would give it, and prints cycles per
      block per module over serial (and the engine's own fixed cost: NoteIn, the voice mix, MasterOut, the codec task). The user
      flashes it in VS Code; the table goes into the repo (framework/modular, a cost per ModuleDef, with the chip and the date). A cost
      that depends on a value (a Delay's length in PSRAM, a filter's mode, Unison's count) is measured at its worst.
- [x] **The wallet in the PATCH tab** (2026-11-15): the strip above the canvas (`draw_wallet`, `modular/cost.h`): the bar (engine,
      a slice per voice, FX; amber > 75 %, red > 90 %; a tick at the slowest values), each module's cost in its header. Screenshots
      looked right at 800 x 480 and 1200 x 700. **Not tried by hand.**
- [x] **The voice count locked by the designer**: `Patch::voices` (the stepper, a `voices` line in the `.patch`), Export > Synth ->
      `lab::Options::voices` -> the engine's allocator cap (`set_voice_mode(..., max_voices)`); the Modular synth plays it too.
- [x] **The estimate checked on the board** (HWV1): the default patch exported at 3 voices, put in the bench build for the measure
      (then taken out): 68 512 estimated, 69 408 measured (1.3 %); 6 notes held played 3 voices.
- [ ] The fast heap the same way (a memory wallet): LabVoice gives each voice the largest Voice of all the synths, so the memory is
      the firmware's, not the patch's alone. To design with the user.
- [ ] HWV2's budget (48 kHz: 160 000): the table is HWV1's (44.1 kHz). Cycles per block barely depend on the rate; the wallet uses
      the measured board's budget. A board choice in the wallet, or a bench run on HWV2, when the user asks.

**2026-11-15: the user's next batch** ("both prototypes at 44.1 kHz; optimize the envelopes and the main osc; remove redundant modules,
keep the optimized or lightweight ones; implement the last remaining modules for the esp32 (hwv1 for now); unify all modules; add a
switch group module"). The user's choices: unify = merge the float and the engine (q15) catalogs, each function one entry with its
implementation as an option, the cheaper one by default (the user will pick per function later); ESP32 = the RM modules usable in an
exported synth on the board; sound changes of the optimizations: free; switch group: both a bypass container and a selector.
- [x] 44.1 kHz on HWV2 (SynthLab, TechnoGen; the Ritual Machine already was): one budget for both prototypes.
- [x] Osc 8 160 -> 2 047, ADSR 2 365 -> 1 573, Perc 7 742 -> 2 022, LFO 3 151 -> 1 529 (HWV1, cycles a block); decay_coef without expf.
      Not heard yet by the user.
- [x] The 33 engine modules measured through the adapter (hwv1_bench_rm), in module_costs.h: hosted, each costs more than its float
      counterpart (the q15 <-> float conversion, about 1 300 a module): RMOsc 4 836 / Osc 2 047, RMEnv 4 216 / ADSR 1 573, RMFilter
      7 736 / Filter 1 711, RMVca 3 040 / VCA 446, RMDelay 18 050 / Delay 2 212; RMReverb 63 407.
- [x] **Direction chosen (2026-11-15): the engine is the one module system** (ADR-049 addendum 7). The user: "rewrite the
      functionally new modules to match those of the Ritual Machine engine and keep the new functionalities"; "wrapper first".
      Done: modular/engine_modules.h, every catalog module an engine type "F<name>", null-tested (-66..-99 dB).
- [x] Export > Engine writes a float module as its F type where the engine has none (no more "the engine lacks"), with
      register_<id>_types(reg) for exactly those (a generated graph compiled with -Wall -Wextra; self-test 6756).
- [x] SynthLab plays an exported engine graph (Export > Engine synth, lab::make_graph_synth; the user OK'd up to 200 ms of
      silence at the switch): tested through the engine, played on HWV1 (default patch, 3 voices: 57 598 cycles a block with 3
      notes, about 2 300 a voice more than the float export: the wrapper's conversions). The wallet counts the float export's.
- [ ] Next on that road: registering only its types (register_modular_type); measure the F types natively on
      the board (ENGINE_PROFILE in a graph); then the merged catalog (a choice per function, the cheaper by default).
- [ ] (Superseded by the above) RM modules in an exported synth on the board. Blocker found: the engine's families linked into SynthLab put all their code in
      internal RAM (SC_HOT) and SynthLab's own 64 KB fast heap no longer fits (the bench saw 83 KB free). Options to put to the user:
      register only the types a synth uses (a registry per exported synth; families split), or those modules' code from flash
      (ENGINE_NO_IRAM for them: slower, cold misses), or a smaller fast heap.
- [ ] The merged catalog: one palette entry per function (pairs by ModuleDef::engine), an implementation choice in the inspector
      (values mapped by name), the cheaper by the table as default.
- [ ] The switch group: a bypass container (In -> Out, its modules skipped) and a selector (one of N groups plays); the switch a
      synth parameter like a Macro (the layout shows it as a switch, a list or a knob).
