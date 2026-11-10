# The Ritual Machine: design document (living)

The app's decisions: the rack and its mapping onto the engine, the Synth and Pads sections, the sequencers, the GUI. ADR numbers
are shared by the whole repo: the index of all of them, and the engine's and the boards' decisions, are in
[framework/docs/DESIGN.md](../../../framework/docs/DESIGN.md).

## Decisions (ADRs)

### ADR-026: Rack -> graph mapper, always-on master effects (Accepted, `platform/engine/rack_graph.*`)
`rack_graph_build()` turns `rack_t` + the amp envelope + general settings into a `GraphDesc` with the previous semantics:
audio modules in slot order, a source (OC) adds to the running signal (levels x 1/sqrt n; since 2026-10-06 every source is a cable into the next module's input and the plan's MIX step sums them in one pass with one saturation, no Mix4 chain; more than `kMaxFanIn` sources fold the first eight into one pass-through Mix4), a
processor (FL, SA) acts on everything to its left, a processor with nothing before it is dropped, an ENV aimed at a cutoff
replaces the filter's own envelope, modulators are cables whose depth is the effect per unit of module depth divided by the
target's full-scale range. Node ids come from the rack module ids, so editing the rack keeps the state of untouched
modules. Not realised (as before): resonance and mix modulation, modulating a modulator (MS: see ADR-029; an OC with a target: ADR-028). The master chain
BusIn -> Chorus -> Delay -> Reverb -> MasterOut is always in the graph and driven by two new menu tabs (FX 1: chorus mode,
delay mix / time / feedback; FX 2: reverb mix / decay / size / damp / pre-delay), so every effect edit is a live parameter
message, never a rebuild. `engine_synth_set_params()` rebuilds the description and sends only the parameter differences
(or reloads when the structure or a cable depth changed).

### ADR-028: An oscillator with a target modulates at its own pitch and stays in the chain (Accepted, `rack_graph.cpp`, `core/rack.c`)
The old "OC with a target" was a separate control-rate oscillator with its own `Rate` in Hz, out of the audio chain. Now an
OC is always a source in the chain; giving it a target additionally routes its own output (wave, Crs / Fine, note pitch, PW)
into that parameter with the cable depth `0.5 x Dpth / range` (full-scale pitch cable = +-12 semitones, so Dpth 2 is the
maximum). The oscillator then runs at full level and the depth of its cable into the chain applies `Lvl` (and the 1/sqrt n source scaling; `qd()`, never exactly unity, so a level edit stays a live `SetDepth`) on its
way into the chain, so the modulation signal does not depend on `Lvl`; the `Out` switch on the OSC MOD page (`On` / `Mute`)
sets that cable's depth to 0 (the MIX step skips zero gains) while the modulation cable stays untouched. Mute only counts while the target exists in the rack.
Consequence: `MP_OC_RATE` is gone (the slot value index became `MP_OC_MUTE`); an OC that used to be silent in the chain
because it had a target is now audible until it is muted.

### ADR-030: Sampler in the application (Accepted, `platform/engine/engine_synth.*`, `platform/sim/sample_sim.cpp`, `core/rack.*`, `core/synth_ui.c`; user choices: samples/ folder in SimStorage, rack module first, one sample per module, selector row + browser tab)
- **SM rack module** (source, like OC): File (catalog index + 1, 0 = none), Lvl, Crs / Fine, Loop (File / Off / Fwd / Ping), Dir, Start (ms),
  Trk (Key = follows the keyboard, Drum = original speed), Slc (0 = whole sample, n = start at slice n), Mode (play on / stop at the next
  slice). Pages `SMP n`, `SMP n LOOP`, `SMP n SLICE`; the graph shows the amplitude overview with slice ticks, loop bracket and start marker.
  An LFO / ENV / OC / MS can target its pitch. An SM without a usable file is not in the chain (no node). A sampler *synth type* (own
  page set, zones) is still to do; `MOD_PARAM_MAX` became 12 for this module.
- **Library** (`core/audio.h`): `audio_sample_count / audio_sample_info / audio_samples_rescan`, entries with name, length, rate, root, slices,
  loop and 64 amplitude peaks. The order is stable and only grows (a rescan appends), because the rack stores the index. Up to 32 files
  (the bank has 32 slots). The rack refers to the file by index, not by name: saving racks (later) must store the name.
  **Since 2026-10-11 the library is read on demand** (folders, the catalog = the 32 files in use, `audio_sample_use`, a browser
  screen; `audio_samples_rescan` and the Scan row are gone): ADR-041, decisions 51-52.
- **Engine side** (`engine_synth`): `engine_synth_attach_storage(dev)` + `engine_synth_catalog_set()` (C++ header `sample_catalog.h`); the
  mapper receives `slot_of_file[]` and the engine loads a file into a bank slot the first time a rack uses it (`resolve_samples`); loaded
  samples stay loaded (unloading while a voice might still read them is not safe), so playing every file of a full library costs about
  32 x (150 ms head) of RAM. `engine_synth_io_pump()` runs the loader with a clock derived from the audio blocks. A mutex serialises the control
  calls with the loader; the audio thread never takes it.
- **Desktop** (`sample_sim.cpp`): the folder `samples/` (cwd, `..` or `../..`) is the card. `*.smp` join the library; `*.wav` (8/16/24/32-bit PCM
  or float, any channel count mixed to mono, `smpl` root and loop respected) and `*.mp3` (minimp3 in `lib/minimp3`, not versioned; the import is
  skipped without it) are decoded and cooked into `<name>.smp` next to the source, once (re-done when the source is newer). Files are held in a
  `SimStorage` with a modest card model (1 ms per request, 3 MB/s, a 12 ms stall every 80 requests) so streaming behaves like a slow TF card.
  A loader thread pumps the bank every millisecond. `framework/tools/make_demo_samples.py` writes five synthetic demo files.
- **Browser** = menu tab SAMPLES (modular synth): File (highlight; the graph shows its overview, length, root, slices), Tgt (`New` or an existing
  SM), Assign (`New` appends an SM at the end of the rack, or sets the File of the chosen SM), Scan (rescans the folder, importing new wav / mp3).
  Assigning is a rack edit, applied when the menu closes. The File row on the SMP page cycles through the same list.
- Risks: whole files are copied into RAM on the desktop; on the ESP32 the TF driver and the I/O task are still to be written
  (`audio_esp32.cpp` has stubs, no library). Imports run on the UI thread and block it for the length of the decode (a few seconds for long mp3s).

### ADR-032: Modulation depth in real units (Accepted, `core/rack.c rack_depth_*`, `rack_graph.cpp depth_target`; user choice)
A modulator's `Dpth` (and an MS lane's) is a signed value in the unit of its target: **st** for pitch (+-96), **oct** for cutoff (+-8) and drive
(+-4), **%** for level / PW / morph (+-100). Changing the target resets Dpth to a default of the new unit. The mapper turns it into the cable depth
`Dpth / range` and sets the engine node's own modulation range to match (`OSC_PITCH_MOD` / `SMPR_TUNE` 96 st, `FLT_CUT_MOD` 8 oct, new
`SHP_DRIVE_MOD` 4 oct, `OSCX_*` like the oscillator), so a +60 st kick sweep is one setting. Old patches' Dpth values change meaning (the demo
rack's LFO now has 1.5 oct). Modulating resonance / mix / another modulator is still not realised.

### ADR-030 addendum: .wav / .mp3 are listed at once and converted when assigned
The library lists `.wav` and `.mp3` next to `.smp` (entry marked with `*`, `pending`, the graph says "converts on assign"). The decode (24-bit / float / any channel
count -> mono, minimp3) runs when a sampler uses the file: pressing Assign in the SAMPLES tab, or any build / parameter update that references it
(`engine_synth_set_importer`). The cooked `<name>.smp` is written next to the source (done once; redone when the source is newer). Risk: the UI thread waits
for the decode (a few seconds for a long mp3).

### ADR-040: Rack lanes, Sum points and FX modules in the rack (**Superseded by ADR-041** on 2026-10-09; stage 1 code of 2026-10-08 to be reworked, not extended)
User decisions (asked with options, all recommendations taken except the scope rule):
1. **FX in the rack, scope by a Sum point** (user's choice over "cheap per voice, heavy at the end"): a **Sum** module in a lane; modules left of it
   run per voice, right of it once on that lane's summed voices (stereo). A lane without a Sum is summed at the output.
2. **Parallel lanes**: every audio module has a lane (**3 lanes**); a lane is today's serial chain (sources add, processors process what is to their left
   in that lane). **One Sum per lane**: each lane is summed over the voices on its own and stays separate after its Sum; lanes meet only at the output,
   with a level and a pan each. Global FX cost once per lane that uses them.
3. **Before a Sum (per voice, mono) only the cheap FX**: Tremolo, EQ, Ring / Shift, Phaser, Flanger, Compressor (plus the existing Saturator and Comb) get
   per-voice versions; Delay, Reverb, Chorus, Spectral, Cab, Ensemble are global-only (the editor allows them only after a Sum).
4. **16 slots in total**, shared by the lanes (was 10).
5. **The FX RACK tab stays** as the master section after the lanes.
Second round (2026-10-07): **one rack module per effect** (user's choice over one FX module with a Type: own icons, ~12 more entries in the add
list); **three rows** in the rack editor (up / down between lanes, a new module goes on the cursor's lane, Shift + up / down moves a module to another
lane); **level and pan on the lane's Sum** (a lane without a Sum shows an implicit one at its end, 100 %, centre, editable); and the Para rule below.
Proposed by me and confirmed by the user:
- A Sum generalises the Mod Para split: modules after a Sum are global like Para's shared half (filter / envelopes driven by `GateIn`, the newest key);
  in Mod Para a lane without a Sum keeps the implicit split at its first filter. Modulators that feed a post-Sum module are global (the ADR-036 rule).
- Engine: 3 voice buses (`VoiceOut` / `BusIn` get a lane parameter; `ProcessCtx` carries 3 pairs of 32-bit buses); per-voice FX as `<Scope>` templates like
  `Filter`; the lane outputs mix (level, pan) into the master FX chain.
Stages: (1) model + rules + editor (lane per slot, Sum, FX modules, placement rules, 16 slots, the 3-row rack strip), host UI tests; (2) engine (buses,
per-voice FX, mapper per lane), host tests in the six configurations; (3) board: cost per lane / per voice FX, listening.
**Stage 1 as built (2026-10-08; 65 UI tests incl. `apps/ritual_machine/tests/ui/test_lanes.c`, 169 engine tests, the simulator builds; NOT seen by the user):**
`rack_slot_t.lane`, `RACK_MAX` 16, `rack_t.lane_lvl / lane_pan` (the implicit Sums), types MOD_SUM .. MOD_ENSEMBLE appended (codes SU TR EQ RG PH FG CP
DL RV CH SP CB ES), their parameter tables (FX rack ranges in rack units), pages (`GRAPH_FX`: the FX tab's sketch or the name) and Spectral's cog
(Hold / Lo / Hi); rules `rack_can_insert / rack_insert_lane / rack_set_lane / rack_delete`; lane-aware `rack_audio_prev / next`, `rack_has_signal`.
RACK tab (user choice, third round: **lanes + push menu** over "selected lane big" / "lanes view + fields view" / "16 px icons"): 3 lane rows, a
description line, a popup menu Type / Insert / Tgt / Prm / Dpth / Lane / Lvl / Pan / Delete (only the rows that apply). Moving a module to another
lane is the menu's Lane row (my choice: Shift + up / down is taken by the Shift layer). Placeholder icons `24/mod_<name>` for the user to paint.
**Until stage 2 the mapper builds lane A only** and skips Sum / FX modules (lanes B / C are editable but silent).
Risks: per-voice FX cost x voices (a 4-voice Phaser = 4 Phasers); 3 lanes x Sum x global FX can exceed the budget (the cost display must show it);
the rack strip gets 3 rows on 128 x 128 (with the 8 x 8 grid: 16 px bar + 3 x 32 px rows = 112 px).

### ADR-041: Two branches + row M, a Para switch, Synth and Pads sections (Accepted; user choices of 2026-10-09; phases 1 and 2 built 2026-10-09)
**Why.** The user's real need behind ADR-040 was (osc 1 -> filter 1) + (osc 2 -> filter 2), each filter per voice or paraphonic, plus a sampling and
production loop (synth -> capture -> pads). Three lanes with Sum modules were more than that and made the scope of every module hard to read.
The remaining phases and open questions: apps/ritual_machine/CONTINUE.md "Next: the production workflow"; the user's brief with screen sketches: https://claude.ai/artifact/HiSWqALbydqwbo8ibHp8nA.

**Decisions taken by the user** (recorded, not to be re-litigated):
1. **No lanes in the engine, no Sum module.** Rows exist only on the RACK page; there are no separate note streams.
2. **Rows 1 and 2 are two parallel branches**: same notes, same voices, summed at the output. **Row M** is a chain on the sum of both branches,
   always shared (once, not per voice). Oscillators side by side in a branch are summed, as today.
3. **Para is a switch on any processor**: from that module on, the branch runs once on the sum of its voices. Heavy FX (Delay, Reverb, Chorus,
   Spectral, Cab, Ensemble) always run shared, so a branch becomes shared at the first of them. One way only: a shared module never feeds a
   per-voice one. Per-voice modules are drawn as a small stack, shared ones flat.
4. **Row M replaces the master FX tab.**
5. **One synth plus a separate Pads section** (SP-404 style); two workflows, the user works in one or the other. A pad = a sample per key with
   start / end, loop, tail, start on grid (off / beat / bar), play mode (one-shot / gate / toggle), root + tune + zone stretch, choke group,
   level / pan / filter. Short samples in PSRAM, long ones streamed from the card.
6. **Step sequencer for the synth** (notes on the grid, MIDI input recorded into it); **capture** by a global shortcut from any page, modes manual /
   threshold / N bars on the grid / auto-sampler; capturing a sequenced pattern gives an on-grid sample, a live capture is not quantized.
7. **Clock**: internal, MIDI clock in and out. **One settings page** (LEDs, keys, clock, MIDI) reachable from both sections.
8. **No saved data to keep**: no sound design exists yet; old patches are deleted and file formats change freely, no migration code.
Hardware: prototype 1 (PCM5102A) is output only, no MIDI, so capture = resampling the device's own output; prototype 2 (ES8388) adds line / mic in and MIDI.

**Engine sketch** (phase 2): the voice bus carries both branches; each branch's Para point (explicit, or the first heavy FX) sums its voices into a
shared chain; the two branch outputs mix, then row M, then the output. The ADR-040 per-voice FX (`<Scope>` templates of Trem, EQ, Ring / Shift,
Phaser, Flanger, Comp) and the FX module types / pages of stage 1 are kept. Modulators that feed a shared module are global (ADR-036 rule).

**Risks / known:** per-voice FX cost x voices; two Para branches = two shared chains (cost once each); the RACK tab must fit 3 rows on 128 x 128
(16 px bar + 3 x 32 px = 112 px); the sprite count depends on the module list (the user draws them, so the grid / sprite work waits for phase 1).

**Phase 0 answers (user, 2026-10-09; options with pros / cons were offered):**
9. **Row M for every synth type**: FM and Strings get a RACK tab with row M only (no branch rows). The old FX tab, `fxrack` and its engine path
   are deleted; the FX parameter tables move to the row-M modules.
10. **Synth types = Modular, FM, Strings** (against my recommendation "drop Mod Para only"): Mod Para, Mod Mono and FM Mono go. **Voices = 1 is
   mono**: last-note priority, Glide and Legato appear only then (ADR-036 decision 5 replaced). Para is the switch on a processor.
   Risk: going from 1 to 2 voices silently loses the mono behaviour (legato, glide, note stack); with 1 voice the Para switch does nothing audible.
11. **Branch level / pan in one MIX cell at the start of row M** (against my recommendation "on each row's OUT cell"): a fixed cell, not
   deletable, with Lvl / Pan of branch 1 and branch 2. Risk: a special cell in row M, and it is less obvious which row its values belong to;
   for FM / Strings (one source) it is hidden or holds a single level.
12. **The Para envelope (Legato / Retrig / Voice) is set per Para point** (a row of the module where Para is on; each branch its own). Leaves GENERAL.
13. **16 slots shared by the three rows** (as built for lanes); the MIX cell does not take a slot.

**Phase 1 as built (2026-10-09; 71 UI tests incl. `apps/ritual_machine/tests/ui/test_rows.c`, 172 engine tests, simulator builds; GUI checked and synth heard by the user 2026-10-09):**
- Model (`rack.h/.c`): `slot.row` (0, 1 = branches, `ROW_M`), `slot.para`, `slot.penv` (para_env_t), `rack_t.br_lvl / br_pan` (the MIX); `MOD_SUM`
  removed (the type list shifted: no saved data). Rules: `rack_can_insert`, `rack_set_row`, `rack_set_para` (checked in place, undone when
  refused); `rack_shared_start` (Para on, or the first heavy FX), `rack_slot_is_shared` (a modulator: its target's), `rack_type_voice_only`
  (sources + resonator), `rack_type_shared_only`, `rack_add_m`. FM / Strings take processors in row M only.
- Types (`synth_config`): Modular / FM / Strings; `synth_config_is_mono` (Voices 1, not Strings); default Modular with 1 voice (as the old
  default Mod Mono). `cfg.para_env`, `cfg.fxr`, `fxrack.c/.h`, `scr_fx.c`, the FX tab (`TAB_FX` value kept: jump slots) are gone.
- RACK tab (`scr_rack.c`): rows 1 / 2 / M (FM / Strings: M only, and RACK is their first tab), MIX cell first in row M (generated sprite +
  `24/slot_mix` placeholder), OUT at the end of every row, menu rows Row / Para / PEnv / Lvl1 / Pan1 / Lvl2 / Pan2; per-voice modules
  stacked when Voices > 1, the Para badge (`8/ui_para` placeholder) on a branch's shared start. FM / Strings: row M's effect pages come after
  their own pages.
- Engine bridge (`rack_graph.cpp`): branch 1 as before, its Para split at `rack_shared_start(0)` with that slot's `penv` (was: the first filter
  of Mod Para); branch 1's level / pan from the MIX on `VoiceOut` (pan only without Para: the shared bus reads its left side); row M built
  after BusIn for every type (FX nodes from the rack's units, SA / FL as a pair of global nodes per channel, the filter without its envelope).
  **Not yet (phase 2)**: branch 2, FX modules inside a branch (they still start its shared part), a Para point in branch 2, modulators aimed
  at row M, row M's filter envelope, branch 2's MIX values.
- Serial (`serial_cmd_esp32.cpp`): `mode mono|poly|para`, `patch para`, `fx <code> [values]` / `fx clear` reworked (SERIAL_COMMANDS.md).

**Phase 2, step 1 as built (2026-10-09, user report "the second lane has no sound"; host-tested in the 6 configurations, simulator +
firmware built; flashed and heard by the user 2026-10-09):** both branches sound.
- Engine: a **second voice bus** (`ProcessCtx::bus2_l / bus2_r`, `Engine::bus2_*`, cleared per block); `VoiceOut` has a second input
  (`VO_IN2` = 1 when it is used) that accumulates into it, and keeps the voice alive while either input sounds; `BusIn` reads bus 0 or 1
  (`BUS_SEL`).
- Mapper: one pass per branch (a branch without a source is not built), each with its Para split (`rack_shared_start(br)`, PEnv of that
  slot), its amp VCA / ParaGate / shared amp (`RN_*2` node ids for branch 2), into VoiceOut input `br` and its own BusIn. The per-voice amp
  envelope (AMP ENV page) and GateIn are shared by the branches. A global envelope aimed at a shared module follows its branch's PEnv.
- MIX: each branch's output (Para: mono, else its bus in stereo) goes to row M (or MasterOut) through cables whose gains are the MIX level
  x linear pan (`qd`: never exactly unity, so a MIX change is a live gain write, not a rebuild). The running signal of row M is a list of
  cables per channel (no mixer node).
- Tests `apps/ritual_machine/tests/engine/test_branches.cpp`: both branches audible and independent, branch 2 alone plays and frees its voice, MIX level / pan
  live (no rebuild), Para none / 1 / 2 / both (shared filters are global nodes, both keys on both branches, everything stops after the release).
- Step 1 tried by the user on the prototype: works ("very cool", 2026-10-09).

**Phase 2, step 2 as built (2026-10-09; host-tested in the 6 configurations, simulator + firmware built; flashed and heard by the user 2026-10-09, cost not measured):**
- Per-voice effects: Phaser, Flanger, Tremolo, Compressor, Eq3, Shifter are templates on their channel count (`fx2_modules.cpp`):
  CH 2 = the global stereo module (unchanged code path), CH 1 = a mono voice-scope version (`T_PHASER_V` .. `T_SHIFTER_V`, ids 74-79).
  Per voice, Trem's Pan mode is a plain tremolo. A cheap effect before a branch's Para point uses it.
- One builder (`shared` in `rack_graph.cpp`) for a branch's shared part (after its Para point / first heavy effect) and for row M: the
  running signal is a list of cables per channel, **mono until a stereo effect**, then a filter / saturator becomes a pair (a node per
  channel; a modulator cables into both, `Target::node2`), and so does the branch's shared amp (`RN_PARA_AMP_VCA_R`). Existing patches keep
  their mono shared chain and its cost. A filter in a shared part or in row M has its envelope (`T_ENV_G`, gated by GateIn: the branch's
  PEnv, legato in row M; a pair's envelope is node `RN_EXTRA + slot`).
- Modulators are built after row M, so an LFO / ENV aimed at a row-M (or shared) module becomes global and reaches it; GateIn is created
  whenever something needs it. Row M's saturator now has its drive modulation range (`SHP_DRIVE_MOD`).
- Tests (`test_branches.cpp`): the phaser before the Para point runs per voice; a reverb in a branch starts its shared part, the filter
  after it is a pair (stereo out); a tremolo after a Para point runs once; an LFO aimed at row M's filter is global and cabled into both
  channels; row M's filter envelope opens and closes the filter.
- Costs: per-voice effects multiply by the voice count (a 4-voice phaser = 4 phasers; a per-voice flanger takes a 2 KB line per voice);
  after a stereo effect the branch's shared filter costs twice. +2 KB static RAM (the probes of the six new module types). Not measured
  on the board. Flashed and tried by the user 2026-10-09: Osc -> Phaser -> Filter (per-voice phaser) and the modulators work.

**Phase 5 (Pads), round 1 answers (user, 2026-10-09; options with pros / cons offered, all four my recommendation):** Pads started before
phases 3 / 4 (the clock and capture come later: "start on grid" needs the clock, pads take card files until capture exists).
14. **16 pads per bank (4 x 4)**: half of prototype 1's 32 note keys, the other 16 free for pad-section functions; fits the 4-column board.
15. **4 banks, 64 pads, all ready to play**: every pad's head stays in PSRAM (2 MB on the board, shared with the synth), so any pad of any
   bank starts at once. More banks later = heads loaded per bank (a card read on a bank switch).
16. **Synth / Pads switch = Shift + Menu**, a Shift-layer entry (editable in MODIFIERS) plus a key function "Section" for the KEYS layouts.
17. **SM stays in the rack; one shared sample library**: the SAMPLES browser serves both the rack's SM and the pads (the auto-sampler's
   multisamples will play through SM). Risk: two ways to play a sample.

**Phase 5, round 2 answers (user, 2026-10-09; all my recommendation):**
18. **Built-in "Pads" key layout** (editable in KEYS): on prototype 1 the left 4 x 4 are the pads, the right 4 x 4 the functions; on the
   4-column board all 16 keys are pads and the functions are Shift + pad.
19. **Function keys**: Bank A-D (4 keys, lit in the bank colour), Stop all, Select (hold + pad: show its settings, no sound), Mute (hold +
   pad: mute / unmute).
20. **The synth keeps sounding in the Pads section** (latched notes, later its sequencer); only the keys go to the pads. Risk: the synth's
   CPU stays in use, fewer pad voices fit.
21. **Each pad mono (a push restarts it), 8 pad voices**, the oldest stolen. Risk: a fast roll cuts the previous hit's tail.

**Phase 5, round 3 answers (user, 2026-10-10):**
22. **A "live synth" in the Pads section**: a simpler, optimized synth for live play; the full synth and its row M are not loaded there
   (replaces decision 20; my reading: the full synth stops while in Pads, to confirm). Its design (voices, engine, how it is played) is open.
23. **Three FX chains FXA / FXB / FXC** in the Pads section, routings A/B/C, A+B/C, A+B+C, A+C/A+B ('+' = series, '/' = parallel;
   A+C/A+B = A, then split into C and B in parallel), like the SP-404 / Elektron buses. **Each source (a pad, the live synth) picks its
   entry Dry / A / B / C** (in A+B+C a pad on B goes through B and C). **Each chain is a short chain of modules** (against my
   recommendation "one effect per chain"): risk = up to ~12 effect modules next to 8 pad voices and the live synth; measure on the board.
24. **A minimal clock now**: internal BPM and a 4/4 bar / beat position, shown in the Pads header, used by "start on grid"; the sequencer,
   capture and MIDI clock build on it later.
25. **Pads screen = the grid full screen**; a push (or Select + pad) opens the pad's settings pages (against my recommendation "grid on
   top, pad below"): risk = editing a pad hides the others.
26. **Song mode + backtrack**: in the Pads section the user sets start / end edit points in a song mode; the performance is recorded as a
   sample and offered as a **backtrack** in the Synth section (the answer to "hearing the other section while working"). Design open.

**Phase 5, step 1 as built (2026-10-10; 7 engine tests `test_pads.cpp` in the 6 configurations, 6 UI tests `apps/ritual_machine/tests/ui/test_pads.c`,
simulator + firmware built; NOT tried by the user, NOT flashed):**
- Model `core/pads.c/.h`: 64 `pad_t` (file name, start / end in 1/1000, mode, loop, tail, tune, fine, level, pan, cut, res, choke 1..4,
  mute), `pads.cfg` (one line per changed pad, the file name last), saved 2 s after a change.
- Engine: module **Pads** (`T_PADS` 80, global, `sampler_modules.cpp`): 8 voices, each an internal `Sampler` at a fixed pitch (new Sampler
  parameters `fstart` / `fend` / `region`: a trim in frames and a loop over it), then a 12 dB SVF low-pass (float, the ADR-035 filter
  exception), a fade envelope, level / pan / mute ramped per block, into L / R. Pad settings as `PadBlob` (set_blob), pushes / releases /
  Stop as parameters `(n << 8) | pad`. Mono restart and choke fade the old sound in 5 ms; a gate release / toggle off / Stop all fade in
  the pad's tail; the oldest voice (the quietest fading one first) is stolen at once. `pads_playing()` publishes the sounding pads.
- `engine_synth_pads_build` loads Pads -> MasterOut (the rack's volume / Out) in place of the synth (decision 22: the full synth stops);
  `engine_synth_build` brings the synth back; in between `set_params` only follows the master volume. Every pad's sample is loaded at
  build (decision 15): whole in memory up to 192 KB each and 768 KB in total, else a 150 ms head and streaming.
- Decision 16 as built: Shift + Menu is the **Default** of the Menu key's Shift entry (like Shift + Back), not a separate layer entry; a
  Shift entry set on that key in MODIFIERS takes over. Key function "Sectn". Decision 18: the Pads layout is built in and **not editable
  yet**. Decision 19 + 4-column boards: Shift + pad = select, Shift + Back = Stop all. Decision 25: push or Select + pad opens the editor.
- **Loop = Off / Trim / File** (user, 2026-10-10): assigning a file with loop points sets File (the Sampler plays from the trim start and
  loops at the file's own points, exact; `PadBlob.loop` 2 = `SMPR_LOOP` -1); Trim loops start..end (a sample recorded from the synth has
  no loop points); File is not offered for a file without them. Pads start empty (the user: the sound designer fills them).
- Not yet: the clock / start on grid (step 2), FX entry (step 3), root note / zone stretch, reverse, live tune changes of a sounding pad
  (tune / trim apply at the next push), the Pads layout in the KEYS tab, an LED for Select / Mute held.

**Phase 5, step 2: the clock (user answers 2026-10-10; built 2026-10-10, host-tested, NOT tried by the user, NOT flashed):**
27. **One clock for the machine**: the sequencer's BPM (40..240). **Play** starts / stops it (Pads: the clock; the synth: the sequencer
   and the clock with it); a start is bar 1 beat 1; leaving / entering the Pads section stops it.
28. **Tempo**: Tap (key function), BPM -1 / +1 / -5 / +5 key functions (bindable as Shift / Mod + any key, MODIFIERS), a "Tempo" step
   function for encoders / knobs, the SEQ SETUP page's BPM row is learnable as before; a Clock row in the settings page comes in phase 6.
29. **Beat display**: Pads header "BANK A ... 120 3.2" (bar.beat inverted for the first quarter of the beat, "--" when stopped); the Play
   LED flashes on every beat (longer on beat 1, both sections); a metronome click (key function Click, off by default).
- As built: `Transport` in the engine (`module.h`, `Engine::set_transport`, `Cmd::Transport`): the position in beats (Q32) advanced per
  block, read by modules through `ProcessCtx::transport`, published for the UI (`engine_synth_transport`, HAL `audio_transport`). The click
  (1.5 kHz, 3 kHz on beat 1, about -12 dBFS) is added after the master output. Start on grid (pad setting Grid Off / Beat / Bar):
  **block-accurate** (the pad starts in the block nearest to its beat: within half a block, 0.36 ms at 44.1 kHz / 32), a stopped clock
  starts it at once (also a pad still waiting when the clock stops), a gate released or a toggle pushed again before its beat cancels it.
  The sequencer still counts its steps in milliseconds of the audio clock beside it (phase 3 moves it onto the transport).
- Fixed on the way (both only bite when the engine is started again, which the tests do): `engine_synth_init` did not reset the stored
  clock values (a second engine ignored the first `set_clock`) nor the pads' resident-sample budget; `SampleBank::shutdown` kept the
  loader's jobs, in-flight count and stream list, so a read in flight at shutdown left the next bank's queue "full" (no more card reads).

**Pads: tried by the user in the simulator (2026-10-10): "all fine, I love the options".** Their next requests (2026-10-10, options
offered):
30. **The tail shows on the overview** (a fade after End, as long as Tail).
31. **Loop crossfade**: XFad in ms (capped at half the loop), equal-power, **Pre** (the fade-in material ends at the loop start: taken
   from before it) or **Post** (it starts at the loop start: after the wrap the play goes on from loop start + XFad). Trim and File loops.
32. **Loop start apart from the sample start** (LStr, the Trim loop's), and a grid launch can start at the sample start or the loop
   start (From).
33. **Sample tools next, before FX** (design round first): chopping, stretching, tempo matching (fit to N beats: **offline only**, a
   render to a new file; not live) and offline processing (normalize, stretch, paulstretch, more).
- As built (host-tested, NOT tried by the user): the trims are **frames** now (1/1000 of the file was 10 ms on a 10 s file), stepped by
  1/200 of the file (fine 1/1000); `pads.cfg` start / end / lstart in frames, xfade / xpos / from. The Sampler mixes the fade-in in its
  general loop (two interpolated reads, sine-table gains; its fast paths stop where the fade begins); measured largest step at the wrap of a
  broken 500 Hz loop: 9266 plain -> 986 Pre / 857 Post (48 kHz). Limit: a streamed sample's fade-in material may not be loaded in time
  (the loader follows the playback path): at 96 kHz, where the 2 s test file streams, Post left a 1944 step (still far below 12107). The
  editor has 19 rows (8 px, the loop rows only while it loops); `ui_screen.c` MAX_ELEMENTS 16 -> 24.

**Sample tools, design round (user, 2026-10-10; options offered):**
34. **Chop modes: all four**: equal slices (N), on transients (onset detection, a sensitivity), on the beat grid (N beats / steps at a
   BPM), manual markers (drop cuts while it plays, move them; also edits what the automatic modes found).
35. **Slices go onto consecutive pads**: slice k = the pad after the selected one + k, start / end in the SAME file (no card write).
36. **Processing: all four groups**: Level & cleanup (normalize, gain, fades, trim silence, DC, reverse, mono), Time & pitch (stretch
   WSOLA / phase vocoder, pitch shift, fit to N beats: offline, decision 33), Texture (paulstretch, granular freeze / smear, reverse
   reverb), Color / lo-fi (resample / decimate, bit crush, tape / saturation, filter, print a pad's settings).
37. **A version history per sample on the card**: every processing writes a child version (the original is the root; undo = the parent;
   processing an older version = a branch). **Trimming, automatic**: kept = the original, every version a pad uses, and the last K (8)
   versions per sample; the others are deleted when a new one is written; a version whose parent goes re-links to its nearest kept
   ancestor.
38. **Order**: chop (no card writes) -> render-to-card + the history tree (shared with capture, phase 4) -> Level & cleanup -> Time &
   pitch -> Texture -> Color.
39. **Next** (user, 2026-10-10): a pad setting Off / Up / Down / Left / Right = the neighbour on the bank's 4 x 4 grid that starts when
   the pad ends by itself or is stopped by hand (a toggle pushed again; my reading: a gate released too), not on Stop all or Play / Stop
   (none past the grid's edge; a grid pad waits for its beat as when pushed).

**After the first board test (user, 2026-10-10):**
40. **Snap** for Start / End / LStr, configurable per pad: Off, Beat (at the clock's tempo), 1/16 of the sample, Attack (the onset scan),
   Zero crossing. **Changed 2026-10-11 (user: "the snap function isn't sync with set tempo"): 1/16 = a 16th note at the clock's tempo**,
   not 1/16 of the file. Beat and 1/16 are the notes as the pad plays them: its Tune / Fine count (an octave down = half the frames),
   and a point is the whole count times the exact beat, rounded once (the beat was cut to whole frames first: up to a frame per beat).
41. **Sync = Loop: the loop start is the pad's anchor**: with the clock running the pad's start is delayed so that its loop start lands
   on the grid (a reverse-reverb pre-roll leads into the downbeat); with the clock stopped it starts at once like any pad. (First asked:
   the playhead reaching the loop start also starts a stopped clock; dropped by the user the same day as too complicated.) **Grid
   units**: 1 / 2 beats, 1 .. 8 bars (odd counts too).
43. **Grid chop fills whole grid units** (user, 2026-10-10): when the slices of a Grid chop do not fill whole units (a sample shorter than
   the grid unit, or a last slice cut short), Chop to pads extends them to the next grid line: with the file's own audio when it is there,
   else with a padded version of the sample (silence appended, a new version in the history) so the pads loop on the grid at the current
   tempo (a later BPM change does not follow: chop again).
42. The tempo popup goes (the header shows the BPM). The editor gets pages PLAY / TONE / CHOP switched from its header (the board has no
   Page control); the edited pad = the pad key pushed (or Select + pad).

- Decisions 40-42 as built (2026-10-10, host-tested, NOT tried on the board): pad rows Sync (Off / Loop: shown while the pad loops) and
  Snap; Grid has 11 units (`pad_grid_beats`); the Pads module waits for the grid line the loop start can reach (`PadBlob.preroll` = the
  output samples from the start to the loop start at the pad's tune; 0 with From = Loop) and starts that much before it (measured: the
  pre-roll starts within 1 sample of the expected time, the loop on the bar). Snap: `pads_ui_trim` (scr_pads.c) for the editor and the
  knobs; Zero uses `SampleBank::zero_near`, only where the sample is in memory (a resident pad sample, a streamed one's first 150 ms):
  elsewhere the plain step. Editor pages PLAY / TONE (with the filter's response) / CHOP from the header; Back: a page -> PLAY -> the grid.
  Fixed on the way: `fix_focus` (ui_screen.c) read past the element table when the focus was beyond a shorter screen's last row.

**Sample tools step 1 (chop) + Next, as built (2026-10-10; 14 pad engine tests in the 6 configurations, 84 UI tests, sim + firmware
built; NOT tried by the user, NOT flashed):**
- **Onset scan** in the SampleBank's loader (`SampleBank::scan / onsets`, `engine_synth_sample_onsets`, HAL `audio_sample_onsets`): one
  block per pump at the lowest priority (after headers, heads and the streams; resident blocks from memory), the rise of the log energy
  over 256-frame windows, candidates over 3 dB, the strongest within 40 ms, the 64 strongest kept per file (bulk heap). Measured on 4 noise
  hits at 0.1 / 0.35 / 0.6 / 0.85 s (one at -24 dB): all 4 found, about 4 ms early (the window start: the cut keeps the attack).
- **CHOP page** of the pad editor (Page - / + = the pad's settings <-> CHOP): Mode Equal (N 2..16) / Trans (Sens: 30 dB .. 3 dB rises)
  / Grid (1/16 .. a bar at the clock's tempo) / Manual (the selected cut), Pos (moves the selected cut, 1/400 of the file), To (the
  first target pad), Add (at the playhead while the pad sounds, `audio_pad_position`; else in the middle of the selected slice), Del,
  Chop to pads (asks; the slices replace the pads To.. : copies of the source pad, the same file, their start / end, no loop, no Next).
  Pos / Add / Del on an automatic chop turn it into Manual with its cuts. Cuts at least 64 frames apart; 16 slices at most.
- **Next**: `PadBlob.next` (engine_synth finds the neighbour), started after a natural end, a toggle off or a gate release; the Pads
  module publishes each pad's position (`pads_position`). The pad page has 20 rows now (overview 16 px).

**Sample tools steps 2 + 3 as built (2026-10-10; 199 engine tests in the 6 configurations, 91 UI tests, simulator + firmware built;
NOT tried by the user, NOT flashed: the first run on the board writes sample files to the card; the user: the card may be erased):**
- **Offline processing** `engine/offline/process.*`: `Processor` runs block by block over a reader / writer (nothing the size of the
  sample in memory: the board has ~60 KB of PSRAM to spare). Level & cleanup: Normalize (two passes), Gain (saturating), Fade in / out
  (linear), Trim silence (threshold; 5 ms kept each side; the loop and the slices move with it), Reverse (the loop mirrored, slices
  dropped), Remove DC (10 Hz one-pole high-pass), Pad to (silence appended: decision 43). Tests `test_offline.cpp`.
- **Card job** `platform/engine/render_job.cpp` (`engine_synth_render_start / _step / _poll / _done`, HAL `audio_render_*`): the UI posts
  one job; the thread that owns the card runs it 2-4 chunks at a time between the loader's reads (the board's card task, the desktop's
  loader thread), with C stdio on `<card root>/system/samples/<name>.smp`: the header is written last, the file is renamed into place
  from `.tmp`. The job's memory (8 KB then; ~5 KB plus the operation's own since step 5) comes from PSRAM on the board and is freed when the result is taken.
- **Version history** `core/history.*`, `system/samples/history.cfg` (decision 37): versions `<root>~<n>`, parent links, Undo / Redo /
  branches; trimming after every new version (the root, what a pad uses, the last 8), the trimmed files deleted (`storage_remove`) and
  their library entries hidden (`audio_sample_hide`: the name goes, the index stays).
- **EDIT page** (the editor's 4th page): Op, its amount, Apply ("Working n %"), Ver (steps through the versions), Undo, Redo, and where
  the version comes from. The Smp row no longer offers versions or hidden entries. After a Trim / Reverse the pad's trim points reset.
- **Grid chop fills whole grid units** (decision 43): End moves to the next grid line when the file has the audio, else a padded
  version is rendered first and the chop runs on it.
- Internal RAM: the five settings modules shared nothing (5 x 4 KB text buffers); one `g_storage_buf` now (hal_storage.h): firmware
  static RAM 70.8 % -> 65.8 %.
- Limits: 32 library entries in all (versions included; a hidden entry's slot is not reused until a restart); no cancel of a running
  job; the processing reads the file's whole length, not the pad's trim (Crop, added later, makes the trim a file); mono 16-bit only.

**Sample tools step 4, Time & pitch, as built (2026-10-10; 204 engine tests in the 6 configurations, 93 UI tests, simulator + firmware
built, firmware static RAM unchanged at 65.8 %; NOT tried by the user, NOT flashed, NOT listened to, NOT timed on the board):**
- Five EDIT operations: **Stretch** (25..400 % of the length, the pitch kept), **Pitch** (+-24 semitones, a semitone per step, a cent
  with fine; the length kept), **Speed** (25..400 %: the length and the pitch change together, like a tape), **Fit beats** (the pitch
  kept) and **Fit speed** (by the speed): the pad's loop, or Start..End when it does not loop, is made to last N beats (1..128) at the
  clock's tempo; the amount row shows the length factor ("8b x1.07"); beyond x0.25..x4 it is refused. A neutral amount (100 %, 0 cents,
  a fit that already fits) writes nothing ("Nothing to change").
- **The whole file is processed with one factor**, and the pad's Start / End / LStr are scaled with it (`render_adopt`), so the
  lead-in before a loop stretches along and Sync still puts the loop start on the grid. Loop points and slices in the file's header
  scale too.
- Engine (`engine/offline/process.*`, ProcOp `Stretch`, `Resample`, `Pitch`; amounts: the new length in ppm, cents):
  - Stretch = **WSOLA** in the time domain: Hann windows of ~43 ms (hop 1024 at 48 kHz, scaled with the file's rate) overlap-added at
    50 %; each window is taken within +-10.7 ms of its ideal place, where it continues the previous one best (normalised
    correlation over the overlap, on every 4th frame and every 4th offset, then refined to the frame; when going straight on is
    within reach it is taken: x1 gives the file back, +-1 LSB). The first window does not fade in: the attack is the file's own.
  - Resample = a 32-tap Hann-windowed sinc, 32 phases interpolated, the cutoff lowered to the new Nyquist when the result is shorter.
  - Pitch = Stretch by the pitch ratio feeding the resampler, in one pass.
  - Measured (tests, a pure tone at -6 dB): the level stays within 0.01 dB; a stretch's seams are 34..47 dB under the tone
    (whole-frame alignment), a pitch shift's 36..52 dB; resampling leaves 63 dB or better, 15 kHz slowed to 10 kHz loses 0.00 dB,
    and a tone that would fold over on a x2 speed-up is at -44 dB.
- **My choice, open to the user (decision 36 names "WSOLA / phase vocoder"): only WSOLA is built.** It is the right tool for drums and
  loops (transients stay sharp up to about +-30 %; further out a hit can double or smear) and it is weaker on sustained chords and
  pads (a faint flutter at the window rate). A phase vocoder is the opposite (smooth on sustained sounds, soft on drums); it needs a
  float FFT of 2048+ points, ~30 KB of working memory out of the ~59 KB of PSRAM the board has to spare. Paulstretch (the Texture
  group) needs the same FFT, so a "Tonal" stretch mode is cheap to add with that step if the user wants it.
- Memory: per operation since step 5 (below): Stretch 10.8 KB, Pitch 17 KB, Speed 6.3 KB, from PSRAM, only while a job runs. Card:
  a stretch reads about 3 times the result's size (every hop reads its search range again); not timed on the board yet.

**Sample tools: Crop, the trim points per version, Color / lo-fi, as built (2026-10-10; 207 engine tests in the 6 configurations,
95 UI tests, simulator + firmware built, firmware static RAM 66.0 %; NOT tried by the user, NOT flashed, NOT listened to):**
- **Crop** (my addition, not asked for; it closes the limit "the processing reads the whole file, not the pad's trim"): a new version
  that is the pad's Start..End only (ProcOp `Crop`, frames amount..amount2; loop and slices move with it). The pad then plays the whole
  new file, its loop start moved along. For a slice of a chopped file: Crop first, then Reverse / Normalize / Stretch work on the slice
  alone. The render call has a second amount now (`audio_render_start(src, dst, op, amount, amount2)`).
- **A version remembers the pad's trim points** (`hist_ver_t.ptrim / ctrim`, `history_set_trim`, `history_trim_for`): Start, End and
  loop start as they were on the parent when the version was made, and as they became on it. Undo / Redo / Ver put them back
  (before: Undo after a Stretch, Crop, Trim or Reverse left points that belonged to another file). Going to an ancestor restores what
  it had when the step away was made; anywhere else, what that version started with; a trimmed-away version hands its parent's points
  to its children. `history.cfg` lines carry the 6 numbers (an older file is not read: the history starts empty). Limit: points moved
  by hand on a version are not remembered when the pad leaves it.
- **Color / lo-fi** (decision 36's fourth group), the length kept: **Lo-fi rate** (1..32 kHz: the sample held at that rate, no filter
  before it, so the top folds over), **Bits** (2..15: rounded to that word length), **Drive** (0..+36 dB into a soft clipper, a
  rational tanh; full scale stays full scale), **Low-pass / High-pass** (20 Hz..20 kHz, a step about a semitone, fine 1 %; 12 dB per
  octave Butterworth as a state-variable filter: measured -3.01 dB at the cutoff, -12.4 / -24.5 dB one / two octaves past it).
  **Not built: "print a pad's settings"** (the pad's tune, filter and level rendered into the file): it needs the pad's whole blob
  passed to the job, to do with the capture path (phase 4), which renders the same thing live.
- The EDIT page lists 18 operations in this order: Normalize, Gain, Fade in, Fade out, Trim sil., Crop, Reverse, Remove DC, Stretch,
  Pitch, Speed, Fit beats, Fit speed, Lo-fi rate, Bits, Drive, Low-pass, High-pass (`pads.h` EDIT_*; `app/app_render.c` maps each to a ProcOp).

**Texture, design round (user, 2026-10-11; options offered):**
44. **Paulstretch with a 4096-point window** (the user's own answer; offered: 2048 always fitting / up to 8192 with memory borrowed
   from the pads' samples / not on the board). 85 ms at 48 kHz: smoother than 2048, still shorter than the classic's 0.25 s and more.
   Risk: the memory is tight on the board (below).
45. **A Tonal stretch (phase vocoder)** next to the WSOLA one, as a Beat / Tonal choice for Stretch, Pitch and Fit beats (my
   recommendation, taken).
46. **Reverse reverb and freeze / smear come from the FX chains + resampling** (my recommendation, taken): reverse, play through an
   FX chain, capture, reverse again. No offline operation for them; they arrive with Pads step 3 (FXA-C) and the capture path.

**Sample tools step 5, Texture, as built (2026-10-11; 211 engine tests in the 6 configurations, 96 UI tests, simulator + firmware
built, firmware static RAM 66.0 %; NOT tried by the user, NOT listened to; timed and memory-checked on the board the same day: the
table further down):**
- **`engine/offline/rfft.*`**: a float FFT of a real signal, 4096 points, in place (one complex FFT of 2048 points and the even / odd
  split), its quarter-wave sine table in the caller's memory. Checked against a direct transform (5e-5 on bins of ~1800) and its
  inverse (6e-7).
- **Paulstretch** (EDIT op "Paulstr.", x1.5..x64, x8 by default; ProcOp `Paul`): Hann-windowed frames of 4096, magnitudes kept,
  phases random, overlapped by half under a sine window (the frames are unrelated: their powers add to 1). The result is 6 dB under
  the source (the random phases turn a tone into a noise band with higher peaks); Normalize after. The result is capped at 8 million
  frames (2 min 46 at 48 kHz, 16 MB). Measured: a 440 Hz tone x4 keeps 100 % of its energy within 60 Hz, level -6.2 dB.
- **Tonal** (the EDIT page's Algo row, shown for Stretch, Pitch, Fit beats; ProcOps `StretchTonal`, `PitchTonal`): a phase vocoder,
  frames of 4096 at 75 % overlap; each spectral peak goes on at the frequency its phase advance shows and its neighbours are turned
  with it (identity phase locking), phases kept as 16-bit angles, atan by a polynomial (the FPU has none). x1 gives the sample back
  (+-1 LSB). Measured on tones: level within 0.01 dB, everything else 45..85 dB under (WSOLA: 34..47); a three-note chord at x1.5
  keeps every note within 0.04 dB. Expect soft attacks (a window is 85 ms) and, below about x0.5, a rougher result.
- **The working memory is per operation now** (`Processor::work_bytes`, lent by the card job from PSRAM for the job's time; the
  processor itself is 4.9 KB and its block doubles as the job's header block): none for the level / color operations, Resample
  6.3 KB, Stretch 10.8 KB, Pitch 17 KB, Paulstretch 28.7 KB, Tonal stretch 36.9 KB, Tonal pitch 43.2 KB. **With the job's ~5 KB the
  worst case is ~48 KB of the ~59 KB the board has to spare (checked on the board: it fits, 48.5 of 59.3 KB).** If the memory is
  not there the job is refused ("Busy, no card or no memory"), nothing else breaks.
- Card: a spectral frame reads its 4096 frames again every hop (8 KB read per 2 KB written for Tonal, per 4 KB for Paulstretch).
  Timed on the board (the table further down), and the card task now runs a job's chunks between the streams' reads.

**The sample tools on the board (2026-10-11, flashed by me with the user's go-ahead; `apps/ritual_machine/tools/edit_test.py`, serial `pad edit`; the
sample: pluck_c4, 1.5 s, 140 KB; NOT listened to):**

| operation | result | time | memory borrowed |
|---|---|---|---|
| Normalize | 1.50 s | 2.3 s | 5.5 KB |
| Reverse / Low-pass | 1.50 s | 1.8 s | 5.5 KB |
| Speed 200 % | 0.75 s | 1.5 s | 11.8 KB |
| Stretch 150 % (Beat) | 2.25 s | 4.6 s | 16.3 KB |
| Pitch +7 (Beat) | 1.50 s | 4.8 s | 22.5 KB |
| Stretch 150 % (Tonal) | 2.25 s | 8.0 s | 42.5 KB |
| Pitch +7 (Tonal) | 1.50 s | 8.1 s | 48.5 KB |
| Paulstretch x4 | 6.00 s | 12.1 s | 34.0 KB |

- **Every operation fits**: PSRAM free 59.3 KB (largest block 58.4 KB) before a job, the worst case takes 48.5 KB.
- **Every job is slower than real time** (0.2 to 0.8 of it). Where the time goes, from the numbers: the card writes at about
  100 KB/s (single-block writes, CMD24) against ~500 KB/s read, and a spectral frame costs about 30 ms of CPU (estimated 6 to 8:
  not profiled). Candidates, both exact: multi-block writes (CMD25) in `sd_card_spi.cpp`, and a profile of the FFT (its memory is
  in PSRAM and the loop order strides across it).
- **Streamed pads and a job**: with two chunks of the job before every pump of the loader, 4 streamed pads lost 13 blocks during
  a Normalize and 48 during a Tonal stretch. Now the card task gives the job one chunk at a time and only while the loader has no
  read waiting (one chunk anyway after 8 reads in a row, so a job always ends): **0 and 1 lost blocks**; the job then takes about
  twice as long (4.5 s, 16.2 s). Pads held in memory are not concerned.

**Pads step 3, FX chains, design round (user, 2026-10-11; options offered):**
47. **4 slots per chain** (against my recommendation "2"): up to 12 effect modules in the three chains. Risk: the board will not
   run 12 heavy effects next to 8 pad voices and the live synth; the limit becomes what is loaded (measure, show the load).
48. **A slot takes any of the rack's effect modules** (row M's: my recommendation, taken): same sound and parameters as in the synth.
49. **A rack-style FX page** in the Pads section (my recommendation, taken): three rows FXA / FXB / FXC, slots as cells, the routing
   on top.

**Pads step 3, the FX chains, as built (2026-10-11; 214 engine tests in the 6 configurations, 98 UI tests, simulator + firmware
built, firmware static RAM 65.5 %; flashed and measured by me; NOT tried by the user, NOT listened to):**
- **Model**: the chains are a `rack_t` with `fx` set (`pads_t.fx`, `core/rack.h`): its three rows are FXA / FXB / FXC, each up to
  `RACK_FX_SLOTS` (4) modules; the rules let in what row M takes (the effects, the filter, the saturator), no source, no modulator,
  no Para; `fx_route` = A/B/C, A+B/C, A+B+C, A+C/A+B. A filter in a chain has no envelope (Type, Cut, Res only: there are no keys).
  A pad's **Bus** (PLAY page: Dry, FXA, FXB, FXC; `pad_t.bus`, `bus` in pads.cfg) says where it enters. Saved in `padsfx.cfg`
  ("route N", then "fx <A|B|C> <code> <values>" per module), by itself a moment after a change.
- **Engine**: the Pads player has four stereo buses (8 outputs; `kMaxOut` 4 -> 8); `pads_graph_build` (rack_graph.cpp) wires
  Pads -> the chains in the routing's order -> MasterOut, with the rack's own module mapping (`fx_params`, a filter / saturator as a
  node per channel). An empty chain passes its input on. `engine_synth_pads_fx`: the same shape = only the values that moved are sent
  (a knob never reloads the graph); another shape (a module, the routing) = a reload, through which the pads keep playing and the
  untouched effects keep their tails (instances are kept by id and type; the chains' nodes are 300 + 2 x the module's id).
- **UI**: **Menu in the Pads section opens the FX page** = the RACK page (scr_rack.c) on that rack: rows A / B / C, a push on a cell
  opens its menu (Edit, Type, Insert, Row, Delete; Type only offers what a chain takes); **the header is the routing** (left / right
  on it) with the load next to it. **Edit** opens the module's values (a list and the effect's sketch); there the four column knobs
  drive its first four values. Back: values -> chains -> pads.
- **On the board** (`apps/ritual_machine/tools/padsfx_test.py`, serial `pad fx`; 8 looping pads into chain A, 48 kHz, cycles per 32-frame block of
  174 000):

  | chains | cycles | load | dropouts |
  |---|---|---|---|
  | none | 41 200 | 24 % | 0 |
  | filter + saturator | 53 000 | 30 % | 0 |
  | delay + reverb | 75 300 | 43 % | 0 |
  | EQ, compressor, delay, reverb | 114 200 | 66 % | 0 |
  | 12 light ones (4 a chain, A+B+C) | 208 800 | 120 % | 520..700 per run |
  | delay, reverb, chorus in each chain (9) | 192 700 | 111 % | 360..470 |
  | 12 heavy ones | 215 500 | 124 % | 540..550 |

  Per module (stereo): saturator 3.7 k, tremolo 4.5 k, ring / shift 5.0 k, filter 8.3 k, compressor 8.7 k, delay 8.8 k, chorus
  12.9 k, cab 15.6 k, flanger 15.9 k, ensemble 17.8 k, spectral 19.3 k, phaser 19.5 k, reverb 26 k, **EQ 26.5 k** (as much as the
  reverb for three bands; fixed the same day: 5.9 k, see "Speed" below); the 8 pad voices 40..48 k.
  **So decision 47's risk is real: four to six effects fit next to 8 pads, twelve never do.** Nothing is refused; the FX page's
  header shows the load estimated from these costs with all 8 pads sounding ("FX A+B/C 66%", a "!" from 90 %).
- Static RAM: the chains cost 2 KB (the rack in `pads_t`); loading pads.cfg and the serial `pad def` no longer keep a spare copy of
  the 64 pads (two passes over the text, `pad_parse`), so the firmware went from 66.0 % to 65.5 %.
- Not built yet: the live synth's entry into the chains (step 4), modulators on an effect's values, a per-chain level or on / off,
  a tempo-synced delay time.

**Capture / resampling, design (user, 2026-10-11; options offered):**
50. **Capture next** (before the live synth and song mode; my recommendation, taken), and **it records the whole Pads output** (all
   pads, all chains; "from a Record key until Stop or for a set number of bars on the clock").
- My choices inside that, open to the user: a **Rec** key on the Pads layout's free key (right block, row 2, between Mute and Stop;
  a key function, so it can move); the recording goes to **the selected pad when it is empty, else to the next empty one**; it starts
  at the **first sound** (about -50 dB), or with a length on the **next bar line** of the running clock; **Shift + Rec** sets the
  length (until Rec again / 1 / 2 / 4 / 8 bars at the tempo); a recording with a length becomes a pad that **loops on the bar**;
  the pad plays it at **full level** (a copy of a copy must not fade); files are named rec1, rec2 ...
- **Mono**: the library's samples are mono, so the recording is (L + R) / 2: a ping-pong delay or a wide reverb folds to the middle.
  Stereo samples would be a format change through the whole sampler (to decide separately).

**Capture as built (2026-10-11; 216 engine tests in the 6 configurations, 100 UI tests, simulator + firmware built, RAM 65.7 %;
flashed and measured by me; NOT tried by the user, NOT listened to):**
- **The tap** `engine/sampler/capture.*`: MasterOut hands it every block it receives, before the volume (the volume knob and the
  metronome do not reach the recording); it mixes to mono into a ring (16384 frames, 0.37 s) that the card thread empties. Armed, it
  waits for the bar line (to the sample) or for a block over the threshold; with a limit it stops after exactly that many frames.
  What does not fit in the ring is counted as lost.
- **The card job** `platform/engine/capture_job.cpp` (`engine_synth_capture_start / stop / poll / done / step`, HAL
  `audio_capture_*`): 8 KB at a time into `<name>.smp.tmp`, the overview kept while the length is unknown (64 windows that merge in
  pairs when they are full), the header last, then the rename. Its memory (41 KB of PSRAM) only while it runs; not together with a
  processing job. The card task writes a recording's chunk first on every round.
- **The app** (`app_capture_key`, `app_capture_poll`): arms, stops, finds the file in the library, puts it on its pad and selects it. The
  grid's header shows "REC ready" / "REC 0:07"; the Rec key is dim, blinks while armed, is lit while it records. A recording ends
  when the section is left; one without a length stops by itself after 5 minutes.
- **SD driver: multi-block writes (CMD25 with ACMD23)** for runs of sectors, single blocks as before: it was the precondition (a
  recording needs 88 KB/s sustained) and it speeds the sample jobs up by 15..30 % (Reverse of 1.5 s: 1.8 -> 1.3 s; Normalize 2.3 ->
  1.8 s; Paulstretch x4 12.1 -> 10.3 s). Checked with a read-back of every write (serial `pad verify 1`): 1922 sectors, none differed.
- **On the board** (`apps/ritual_machine/tools/capture_test.py`, serial `pad rec`; 6 s each, 264 600 frames at 44.1 kHz): silence, 8 pads in memory,
  the same through delay + reverb, 1 / 2 / 4 streamed pads: **0 frames lost, 0 stream underruns in every case**.
- Not built: capture in the Synth section (the tap is there: MasterOut feeds it in both), threshold / pre-roll settings, a count-in,
  choosing the destination pad by hand, overdub.

**Speed: the EQ and the sample jobs (2026-10-11, while the user tests; 218 engine tests in the 6 configurations, 100 UI tests,
simulator + firmware built, RAM 65.8 %; flashed and measured by me; the sound is meant to be unchanged: see each check):**
- **EQ: 26.5 k -> 5.9 k cycles a block** (stereo). Its three sections were int64 biquads with Q32 coefficients (five 64-bit
  multiplies a tick, ~140 cycles on the S3); they run in float32 now, a channel from one conversion to the next. The high shelf
  (6 kHz) is the same biquad in float. The low shelf (150 Hz) and the peak (down to 65 Hz) are the same transfer functions as a
  trapezoidal state-variable filter (the cookbook prototypes as 1 + m1 BP + m2 LP): a float32 direct form loses its accuracy near
  z = 1 (measured: 6.7 LSB rms on a 30 Hz tone through the shelf, against 0.8 for this form). Checked against the same filters in
  double precision (`eq3_matches_its_cookbook_sections_in_double_precision`): **0.3 .. 0.8 LSB rms, where the int64 loop was off by
  1.0 .. 2.6** (it rounded to 16 bits after every section). `ENGINE_EQ_EXACT=1` keeps the old loop.
  On the board: EQ + compressor + delay + reverb 66 % -> 53 %; twelve light effects 120 % with dropouts -> 81 % without.
- **Sample jobs**, a profile first (`ENGINE_PROFILE`: the stages of a spectral frame, printed by the serial `pad edit`): of a frame's
  58 ms, 35 went to **reading its whole 4096-frame window from the card again**, 4..5 to each FFT. Four exact changes, each checked
  by a fingerprint of the output (`offline_time_ops_fingerprint_and_source_reads`: the same 8 numbers before and after):
  1. the window is **kept between frames** and only its new end is read (`Processor::slide`; WSOLA's search range too): the source
     is read once instead of 4 to 8 times;
  2. the job's file reader keeps **one block** and asks the card for whole aligned blocks (a small unaligned request cost three
     card commands, ~12 ms);
  3. a spectral operation's **frame and sine table lie in internal RAM** while the job runs (20.5 KB, plus the reader's 4 KB), when
     there is room: a pass over a block in PSRAM costs milliseconds. Without it the job runs as before, in PSRAM (the same result).
     It also frees PSRAM: Tonal pitch got the kept window too (it had no room for it), and the largest job borrows 37 KB of PSRAM
     instead of 48.5;
  4. no `lround` in the per-sample loops (a library call on the S3), one cos / sin per spectral region instead of per bin, one
     power per bin in the peak search instead of five.

  | operation (a 1.5 s sample) | 2026-10-11 first | after CMD25 | now |
  |---|---|---|---|
  | Normalize | 2.3 s | 1.8 s | 1.9 s |
  | Reverse | 1.8 s | 1.3 s | 1.35 s |
  | Speed 200 % | 1.5 s | 1.3 s | 1.1 s |
  | Stretch 150 % (Beat) | 4.6 s | 4.4 s | 2.8 s |
  | Pitch +7 (Beat) | 4.8 s | 4.6 s | 2.8 s |
  | Stretch 150 % (Tonal) | 8.0 s | 8.1 s | 4.4 s |
  | Pitch +7 (Tonal) | 8.1 s | 8.1 s | 4.4 s |
  | Paulstretch x4 (6 s) | 12.1 s | 10.3 s | 6.1 s |

  A Tonal stretch while 4 pads stream: 16.2 s -> 9.6 s, still no stream underrun. A spectral frame is 23 ms now (load 8, FFT 4,
  spectrum 5, inverse FFT 4, overlap-add 2); what is left is mostly the card's write rate (~150 KB/s).

**The sample library read on demand, and a browser (user, 2026-10-11: "there is only 32 samples available, it needs a real browser";
both choices were my recommendation):**
51. **Folders, read on demand.** The library is everything below `system/samples`, in folders of any depth. Nothing is scanned at start:
   a folder is read when the browser opens it, a file's header when the file is asked for by name. Against "a flat list of 256 files"
   (still a limit, a long list, an 18 s scan at start) and "raise 32 to 64 now" (no browser).
52. **Browser = a full-screen list with the overview and audition.** The folder's sub-folders, then its samples, sorted by name; the
   highlighted sample's waveform and length on top; the highlighted sample plays from the pad keys. Against a list without audition and
   against keeping the one-file-per-step Smp row.

**As built (2026-10-11; 219 engine tests in the 6 configurations (the whole matrix once, then the pads tests again after a fix of the
new test's thresholds), 102 UI tests, simulator + firmware built, RAM 66.0 %; `apps/ritual_machine/tools/sample_sim_check.cpp` run on the simulator's card
of 155 files; flashed and measured the same day, see "On the board" below; NOT tried by the user):**
- **A sample's name is its path below the samples folder, without ".smp"** ("Drums/Kicks/808 long"), at most 47 characters
  (`AUDIO_NAME_MAX` = `PAD_NAME_MAX` 48; it was 24). pads.cfg, the history and the jobs keep working by name: a version (`name~n`), a
  crop or a stretch lands in its source's folder; a recording (`recN`) in the root. A path longer than 47 characters is listed by the
  browser but cannot be taken ("cannot be read").
- **The catalog holds the files in use** (`AUDIO_SAMPLES_MAX` 32, unchanged: it is also the number of loaded samples). `core/audio.h`:
  `audio_sample_find(name)`, `audio_sample_use(name)` (reads the header from the card when the file is not in the catalog: -1 no such
  sample, -2 the catalog is full), `audio_dir_open(dir)` / `audio_dir_read(first, out, max)` (one folder, sorted, folders first,
  versions left out; the platform keeps the list until another folder or `audio_dir_close`: on the board 34 KB of PSRAM, the names
  packed, 2048 names or 30 KB of them at most; a longer folder shows what fits and the log says how many were left out).
  `audio_samples_rescan` is gone. The engine side (`sample_catalog.h`): `engine_synth_catalog_find / _add / _remove`.
- **The samples nothing uses do not pile up** (`app/app_library.c` `app_sample_use`: no pad, no rack sampler, no job refers to them): the 3 asked for
  last stay loaded (`LIB_SPARE`: going back to one in the browser costs no card read), older ones leave, oldest first (`lib_stamp`);
  when the catalog is full the oldest unused entry leaves. **Their sample is unloaded** (new: samples were never unloaded). Safe unload: `engine_synth_catalog_remove` only clears the name;
  `engine_synth_housekeeping` unloads once 4 engine blocks have passed (every command sent before the removal has reached the audio
  thread) and `SampleBank::can_unload` says nothing reads it (loaded, no head read in flight, not being scanned, no stream on it: a
  voice that still plays it keeps it). Then the next file takes the entry's index. `StorageDevice::forget` gives the file handle back
  (the board's table of 48 paths would fill; handles are counted, a file can be loaded again while its old copy waits to be unloaded).
  Test `pads_catalog_entry_leaves_and_its_sample_is_unloaded_once_nothing_plays_it`: the voice is not cut, 49 KB come back, the index
  is reused.
- **The pads' files enter the catalog one per app step** (`app_pads_resolve`: after a start, a card change, pads.cfg, a version picked);
  a pad whose file is not on the card is not asked for again until it or the card changes. The first switch to the Pads section after
  a start can find pads silent for a moment (16 files: about 0.1 .. 0.2 s on the board, estimated).
- **The browser** (`scr_browse.c`, over either section): opened by the pad editor's Smp row (a turn of encoder B or a push; the row no
  longer steps through files) and by the SAMPLES tab's File row (the Scan row is gone). Up / down by the joystick or either encoder,
  Page - / + by a screen; a push or right opens a folder, left or the ".." row goes to its parent; a push on a sample chooses it,
  Back cancels. It opens on the folder of the current file with that file highlighted. Header: the folder, and "row / rows".
- **Audition (Pads)**: 150 ms after the highlight rests on a sample, its header is read (the overview appears) and **the pad takes
  it at once**, so every pad key plays it on that pad, with the pad's level, tune, filter and FX bus. Back puts the pad back exactly
  (its old file and trim). No extra voice and no new engine path: it is an ordinary pad change.
- Chosen by me (say if one is wrong): a file taken through the browser plays whole (Start, End and the loop start go back to the
  file's ends; mode, level, tune, filter, bus and the rest stay), where the old Smp stepping kept the trim frames of the previous
  file; the pad keys all play the browsed pad while the browser is open (no selection change); the SAMPLES tab's browser has no
  audition (the synth plays the rack, not a file); versions are not listed (the EDIT page reaches them); `.wav` / `.mp3` are listed
  in the simulator only (converted when taken); the serial `samples` command first takes the root folder's files into the catalog
  (the board test tools pick from it).
- Changed formats (no saved content to keep: user, 2026-10-09): history.cfg delimits its names (`v <seq> |name|parent| ...`: a name
  with a space cut the line before); pads.cfg `file` holds the path.
- Risks / limits: **32 different samples in use at once** is still the limit (64 pads cannot all hold a different file; the popup
  "LIBRARY Full: 32 samples in use"); raising it costs internal RAM (about 200 bytes per entry plus the bank's slot). pads.cfg must
  fit 4 KB and a path is longer than a name: 64 pads with long paths can pass it (the end is cut silently: known debt, a file per bank
  would fix it). A long name is cut at the row's edge (the highlighted row shows its end). Firmware RAM: the names grew (+1.5 KB pads, +1.5 KB history,
  +0.8 KB catalog, +0.8 KB card paths, +1 KB browser) and the board's second catalog copy went (-5.6 KB).

**On the board (2026-10-11; flashed and measured by Claude in a board session, `apps/ritual_machine/tools/library_test.py`; the card: SL32G at 25 MHz,
its `system/samples` holds 1800 samples in ONE folder; NOT tried by hand, NOT listened to):**

| what | time |
|---|---|
| the folder's list, read from the card (1800 names; a "reading the card..." screen first) | 0.75 s (1.0 s while a pad streams) |
| the same folder opened again (the list is kept) | 35 us |
| a sample's header (`audio_sample_use`), by its place in the folder: first files .. last files | 24 ms .. 430 ms |
| a name that is not on the card (a pad whose file is gone; once per start) | 430 ms |
| a header of the old demo files (cooked before the overview was stored in the header: 65 reads) | 330 ms |

- **FAT searches a folder from its first entry**: opening the 1764th file costs 430 ms, the 36th 24 ms, and the card answers nothing
  else meanwhile (no loader pump inside an open). In a folder of 100 files everything is under 30 ms. So 1800 files in one folder
  make the audition slow towards the end of the list and can make a streamed pad drop out while another file is opened; the folders
  this work adds are the cure (the user's call: how to sort the card).
- 40 different samples put on one pad in turn and played: 39 sounded (one was asked before it had loaded), the catalog stayed at 18
  entries, the engine's bulk heap between 1.02 and 1.24 MB (it filled at 1.52 MB and the pads went silent before the fix below).
  A streamed pad while the 1800-entry folder is read four times: 0 stream underruns, 0 blocks over budget.
- Found on the board and fixed: (1) **the eviction took the sample just left, never the old ones**, and only when the catalog was
  full, so the memory filled after about 25 auditions: now oldest first, and early (above). (2) **The card was used from two threads**:
  `SdStorage::open` did a `stat` on the UI thread (in `SampleBank::load`, under the engine's lock) while the I/O task polled the card
  outside that lock; in this folder the `stat` lasts up to 0.4 s, the two met ("spi_master: Cannot send polling transaction ...") and
  the card was taken for removed. A race older than this work, rare with a short folder. Now `open` and `forget` touch nothing: the
  I/O task opens the file and reads its size at the first read, and closes forgotten files. (3) The folder list held 320 entries: now
  the names are packed (1800 names = 28 KB) and the list is given back before a sample job (`audio_dir_close`: the PSRAM outside the
  engine is 59 KB, 23 KB with the list held, and a spectral job wants 37 KB). (4) The Rec key listed the root folder at every arming
  to find a free `recN` (0.75 s here): now once, then counted (`app->rec_next`).
- Not done: the version-name check before an EDIT job and a pad whose file is missing still cost one folder search each (0.4 s in
  this folder); the old demo files want cooking again (`build.ps1 -Sd`) to lose their 330 ms; RAM of the normal build 66.0 %.

**The browser held the UI (2026-10-11, second session; the user: "the browser is getting stuck while scrolling down"; measured with
the new serial command `browse N REST FIRST`, which opens the browser on the root folder, rests on N rows in turn and times every
UI step; same card, 1800 files in one folder):**

| a row the highlight rests on | UI held, before | UI's slowest step, now | sample shown after (now) |
|---|---|---|---|
| near the top (row 21) | 0.10 .. 0.19 s | 19 .. 37 ms | 0.2 .. 0.3 s |
| the middle (row 900) | 0.8 s | (as below) | (as below) |
| near the end (row 1750) | 1.0 .. 1.1 s | 9 .. 30 ms | 0.9 s |
| scrolling on every 200 ms (row 900 on) | 0.8 s a row | 5 .. 7 ms, nothing is read | (the highlight never rests) |

- Three causes, in the order they were found. (1) **The UI waited for the header** (`audio_sample_use`: 24 .. 430 ms by the file's
  place). Now the browser's highlight and the pads' files at start use `audio_sample_request`: the job is posted, the answer taken
  at a later step, an answer for a row the highlight left is dropped (it never enters the catalog). A choice or a pad key before the
  answer still waits (`app_browse_load(app, true)`). (2) **The UI waited on the engine's lock while the card task opened and loaded the
  sample**: `engine_synth_io_pump` holds the lock, `SdStorage::poll` read inside it, and a whole pad sample is ONE read (up to 192 KB,
  0.4 s) after a second walk through the folder for the open. Now a header or head read is marked `IoRead::pinned` (its destination
  cannot be freed while it is read: `SampleBank::can_unload`; test `header_and_head_reads_are_pinned_and_ring_blocks_are_not`) and
  `SdStorage::service` reads it outside the lock, before each pump; ring blocks (4 KB, a few ms) stay inside. The file is opened once:
  the header job leaves its descriptor to the loader (`keep_spare`). (3) **The card driver busy-waits, and the card task (priority 5)
  shares core 0 with the UI (priority 1)**: with (1) and (2) alone the UI was still held 0.15 .. 0.5 s a row. The work that has no
  deadline (the header job, a folder's list, pinned reads) now runs at priority 1 (`HWV1_SD_CALM_PRIO`), tick by tick with the UI.
  At priority 0 it starved: the folder's list took 2.9 s and a header did not arrive in 1.5 s.
- Also: `pads.cfg` is not written while the browser tries samples on a pad (it was, every two seconds, with a file that was not the
  pad's yet).
- **A streamed pad while samples are auditioned**: near the top of the folder 0 stream underruns in 8 auditions; near the end 7 .. 8
  in 6 auditions, **the same with the old priority (7 and 7)**: the open walks the folder for 0.4 s and no ring block is read
  meanwhile (the limit of the first bullet of this section, not of this change). Sub-folders are the cure; the user sorts the card.
  Not done: a whole pad sample is still one read of up to 0.4 s during which the streams wait (it would have to be split in the bank).
- 220 engine tests in the 6 configurations, 106 UI tests (`browser_list_moves_on_while_the_card_reads_a_sample`), simulator and
  firmware built (RAM 66.1 %), flashed and measured; NOT tried by hand.

### ADR-042: The step sequencer plays in the engine, on the machine clock (Accepted; user choices of 2026-10-11 / 12; stages 1 to 3 built and host-tested, NOT flashed, NOT listened to)

**User choices (2026-10-11, options with pros / cons offered):** for the synth's sequencer: velocity, probability, ratchet, chords
(up to 4 notes), "quick edition tools" (their own words, not specified yet); record from the keys, up to 64 steps, several patterns +
chain, direction + step rate. For the Pads section: **a step grid with live record** (one track per pad, the pad keys as steps, Rec +
Play writes the pads hit to the nearest step). Timing: **in the engine** (all three my recommendation). Built in stages; stage 1 is
this record.

1. **A `Sequencer` owned by the `Engine`** (`engine/core/sequencer.h`), run by the audio thread at the start of every block, after the
   commands: its events become `do_note_on` / `do_note_off`, exactly like played notes (same voices, same Mono / Poly rules). The
   control thread posts the pattern (`Engine::seq_steps`, 16 steps per command) and its settings (`Engine::seq_setup`); the step that
   plays is published (`Engine::seq_step`). The UI loop no longer times anything: a card read that holds it does not move a note.
2. **Step times come from the step count, not from an accumulation**: slot k of a run is at `base + k / steps_per_beat` beats (Q32),
   the odd slots late by `swing` % of a step (`seq_slot_time`). Rates: a step = 1/8, 1/8T, 1/16, 1/16T, 1/32 (2, 3, 4, 6, 8 per beat).
   An event sounds in the block nearest to its time (within half a block: 0.36 ms at 44.1 kHz / 32), the pads' start-on-grid rule.
   Test: ten minutes of triplets at 133 BPM end on the computed step.
3. **A step** (`SeqStep`, 8 bytes): 4 notes (0 = none), gate length in steps, velocity, probability, ratchet 1..4. A ratchet's hits
   share the step equally; each lasts until the next, the last one until the gate's end. A pitch that still sounds is released before
   it starts again (notes are keyed by number); 16 notes held at most, the one that ends first gives way. Transpose is applied at
   playback, clamped to 1..127.
4. **Directions**: forward, reverse, ping-pong (the ends are not played twice), random. The swing belongs to the time slot, not to
   the step's number, so a reversed pattern swings the same way.
5. **Start and stop**: it plays while the clock runs and `run` is set. Armed before the clock starts, its first step is on beat 0:
   the application posts the sequencer BEFORE the clock at every step (`audio_seq_set`, then `audio_set_clock`; the commands are
   applied in order). Switched on under a running clock it waits for the next bar line (what the Pads section will use). A clock that
   is started again restarts the pattern; a stop releases every held note. Another rate while it plays: the next step keeps its time.
6. **The motion sequencers follow its position** (`ProcessCtx::seq`: the step and the fraction of it at the block's end), replacing
   their own millisecond clock of ADR-029 (which would have drifted against the transport: its step was `15000 / bpm` in whole ms).
   `MotionSeq` has no parameters any more; a pattern longer than the lanes' 16 steps plays them again every 16; going backwards the
   fraction runs down, so a LINEAR lane stays continuous. `engine_synth_set_clock` is `(bpm, running)`; `audio_motion_restart` is gone.
7. **The application's model** is `core/seq.h` (`seq_t`: 64 `seq_step_t`, BPM, Steps, Trsp, Swing, Rate, Dir, the step that plays as
   the engine reports it). No timing in it: `seq_tick` is gone. `engine_synth_seq_set` compares with what the engine has and sends
   the chunks that differ.
8. **Saved with the machine, in its own files** (stage 2, decision 12): not in `synth.cfg` any more, never in presets.

**Stage 2 (user answers of 2026-10-11, second round: every edit tool offered, plus their own words "have some hysteresis to the step
edit so a knob noise won't change the step as soon as step to edit is changed"; recording = step entry + live; patterns = with the
machine; velocity = a source in the rack, against my recommendation "amp level"):**

9. **Quick edit tools** on the SEQUENCER page: a Tool row (Copy, Paste, Clear, Shift < / >, Reverse, Double, RndNote, Human, RndProb,
   Undo) and its scope On (Step / Page / All); a push on the Tool row runs it and a popup says what it did. One clipboard (a step, a
   page of 16 or a pattern: Paste follows what was copied, so a pattern copies onto another pattern) and one level of undo (again =
   redo), both in `core/seq.c`. Rests stay rests under the random tools; RndNote stays between the pattern's lowest and highest note.
10. **The four column knobs set Note, Len, Vel, Prob of the cursor's step**, over their travel (Note: the bottom = a rest; Len 1..16).
   **A knob writes only once it has moved 24 of 1023 (`SEQ_KNOB_HYST`) from where it stood when the cursor came to the step** (or the
   chord's voice changed, or the page was entered): the user's rule against knob noise. No catch: after that it writes directly.
11. **Recording from the keys** (`app/app_seq.c`): Rec is armed by Shift + Play, or the key function Rec (the Pads capture key, which
   in the synth section arms this). Stopped = step entry: a note goes to the cursor's step, notes pressed while others are held into
   the same step (4 at most), the cursor moves on when every key is up; the step's other values stay. Running = live: a note goes to
   the step nearest to the press (the next step only when the pattern runs forward), with the length it was held for, added to the
   step's chord. It is written once the playhead is on that step, so the sequencer never plays it a second time under the finger.
   The engine publishes the step, how far into it and a step count for this (`Engine::seq_where`). Notes are stored without the
   pattern's transpose. The Run field of the page reads Rec while armed.
12. **8 patterns and a chain of up to 16 entries, kept with the machine**: `seq_t` stays the pattern that plays and is edited,
   `seq_bank_t` (core/seq.h) holds all of them; Steps, Trsp, Swing, Rate, Dir belong to a pattern, the tempo to the machine. Files:
   `system/config/seq.cfg` (tempo, current pattern, chain) and `pattern1.cfg` .. `pattern8.cfg`, each written 2 s after the last
   change, one file per pass; a pattern never touched has no file. Loading a preset leaves them alone.
13. **The change of pattern is the audio thread's** (the user: at the pattern's end): the control thread hands the next pattern over
   ahead (`Engine::seq_steps(.., queue)` + `seq_setup` with `SEQ_QUEUE` and an id); the `Sequencer` goes on to it when the playing one
   has played as many steps as it has (also in ping-pong and random), from its first step, its time counted from there. It says so
   (`seq_taken`); the application then makes that pattern `seq_t` and `engine_synth_seq_set` sends it whole again, so the engine ends
   up with exactly what the screen shows whatever was edited meanwhile. Stopped, a pattern is taken at once. The chain's next entry is
   queued as soon as an entry starts; an entry that repeats its pattern is queued as it is then (edits made after that reach the
   repeat through the resend). If the queued pattern does not arrive in time the playing one loops once more.
14. **PATTERNS page** (after SEQ SETUP): Pat, Chain On / Off, Len, Slot, Is, and the chain drawn (the entry that plays inverted). While
   it runs without the chain, Pat picks what follows ("1 >3"); with the chain on the chain decides.
15. **Velocity is a rack module, `MOD_VEL` ("VL")**: a modulator with Tgt / Prm / Dpth like an LFO, no engine node: a cable from the
   note's velocity (NoteIn out 2), or from the newest key's (GateIn "vel") when the target is after a Para point or in row M. The
   source is 0 .. 1 (velocity / 127: the keys play 100, the sequencer its steps' Vel); nothing is heard until it is aimed. Risk
   (their choice): Vel does nothing in a Modular patch without the module; FM and Strings keep their built-in velocity.

**Stage 3, the Pads section's sequencer (user answers of 2026-10-12: one pattern covers all 64 pads; a step carries level,
probability, ratchet and a length for gate pads, and "you can compact ram/rom usage with bit-field struct"; step mode by Select + Play,
against my recommendation of a Seq key; patterns as on the synth):**

16. **A pads pattern is a list of hits, not a grid** (`core/pad_seq.h`): a hit = pad, step, level (16 steps of the pad's own level),
   chance (tens of %), ratchet, length: **one 32-bit bit-field** (their request). 160 hits a pattern at most: 648 bytes, where a
   64 x 64 grid of the same values would be 8 KB. The 8 patterns (5.2 KB) and the synth sequencer's 8 stored patterns (4.2 KB) are
   in **bulk memory** (`hal/hal_mem.h` `mem_bulk_alloc`: PSRAM on the board, its internal heap when PSRAM has no room left); the
   pads pattern that plays is edited in place. Risk: a pattern is full at 160 hits (a popup says so).
17. **A second sequencer in the engine** (`PadSequencer`), on the same clock: `StepSeq` now holds what the two share (step times,
   direction, the queued pattern, the position) and `Sequencer` / `PadSequencer` the patterns and how they play. A hit goes to the
   Pads module as a `PadSeqEvent` by `set_blob`: the pad starts at once **whatever its own grid setting says**, at the hit's level
   (the voice's gains times level / 16; a key's push is exactly the pad's level as before); a Toggle pad is started, not flipped;
   after the hit's length the pad is let go: a Gate or Toggle pad stops with its tail, a one-shot plays on. A ratchet pushes again
   inside the step. 16 pads wait to be let go at once, 8 ratchet at once.
18. **It plays while the Pads clock runs and it is on** (the Seq field; Play starts it on bar 1), with 8 patterns + a chain of 16, the
   change at the pattern's end done by the audio thread, as decisions 12 and 13. Files: `system/config/padseq.cfg`, `padpat1.cfg` ..
   `padpat8.cfg` (`padpattern <steps> <swing> <rate> <dir>`, `hit <pad> <step> <level> <chance> <ratchet> <length>`).
19. **Step mode = Select + Play** (their choice; **Shift + Play does the same**, since a board without the right block and the
   simulator have no Select key: Shift stands in for it, as Shift + pad does): the 16 pad keys are the selected pad's steps of the page shown, lit when there is
   a hit, the playhead in Play's colour; Select or Shift + pad picks the track (a Bank key first for another bank); Back leaves it.
   The STEPS screen: the 16 steps, then the fields: the hit under the cursor (Lvl, Prob, Rtch, Len), Pat, Steps, Rate, Swing, Dir,
   Seq On / Off, Chain, CLen, Slot, Is, Rec (the live record, for a board without a Rec key). Joystick = the cursor (its push: the hit on / off), encoder A = the field, encoder B = its
   value, Page - / + = 16 steps.
20. **Live record = Select + Rec** while it runs: a pad pushed goes to the nearest step, a Gate / Toggle pad with the length it was
   held for, written once the playhead is on that step (as decision 11). Rec alone is still the audio capture.

**2026-10-14, after the user's try on the board (their words: "pressing back/delete key while recording, erase the sequencer under
the playhead, the undo should revert the deletions, also the sequencer doesn't scroll"; asked: Back erases "while it's held"; the
roll follows the playhead, goes sideways past step 16 and up and down by pitch: "all of them"). Built and host-tested, not tried
by the user:**

21. **Back held while a live recording runs erases under the playhead** (`app/app_seq.c`, `app_seq_erase_down / _up`): the step that
   plays when Back goes down, then every step the playhead reaches while it is held, becomes a rest with the default values (my
   choice: Len, Vel, Prob and Rtch go with the notes, so a note recorded there afterwards does not inherit them). Only with Rec
   armed and the sequencer running, on the pages (in the menu and the browser Back keeps its work; with Shift it still releases
   the latched notes). Either Back works: the button, or a key the layout gives Back; the hold ends with the control that began it.
   A note played into an erased step and not written yet is dropped. The page's Run field reads Erase meanwhile. Stopped (step
   entry), Back is Back: not asked.
22. **One Undo brings back all that a hold erased** (`seq_undo_keep`, the tools' one level of undo): the pattern is kept at the
   first step a hold really changes, so a hold over rests leaves the Undo as it was. The undo is still one level: an erase
   replaces a tool's undo. Notes recorded live are not undone by it (not asked).
23. **The roll follows the playhead** (`synth_ui_seq_view`, core/ui_draw.c; `app_seq_view`): while it runs the roll shows the page
   of 16 steps the playhead is on and the octaves of that page, moved up to the playing step's highest note when it is above the
   window. A move of the cursor or an edit of its step by hand (the Step .. Voice rows, a column knob that writes) holds the roll
   on the cursor for 4 s (`SEQ_FOLLOW_HOLD_MS`), so a step of another page can still be edited while it plays; a tool, the Run
   row and a live recording do not hold it. The playhead on another page than the one shown: a mark at that end of its strip.
   The cursor is drawn only on its own page; the fields under the roll are always the cursor's step. Stopped: the cursor's page,
   as before. Chosen by me (say if one is wrong): pages, not a roll that slides step by step (a stable picture, and the tools'
   Page scope is the same 16 steps); octaves, not semitones; the 4 s; the cursor does not follow the playhead (decision of stage 2,
   kept: the knobs and the rows write to the cursor's step). The UI core has no clock: it counts the edits by hand
   (`ui->seq_touch`), the app times them and sets `ui->seq_follow`.

24. **The synth's pattern recorded as a sample, "from start to finish"** (user, 2026-10-14: "add option to record from start to
   finish"; "user selectable, also make the option to record in loops (keep a tail, or record 2 time the sequence (so the reverb
   tail of the 1st phrase is in the second too"; then, asked with options: it records **audio into a sample**, **a popup when
   arming**, and where the result goes is **asked each time**). Built and host-tested, not tried by the user:
   - **Shift + Play (or the key function Rec) in the synth section opens a list** (the drawing kit's new `popup_choose`): Notes
     (the note recording of decision 11), Audio, stop / Audio, play on (one pass; then the sequencer stops, or plays on), Audio +
     tail (one pass, the sequencer stops, the recording goes on until the sound has died away), Loop, stop / Loop, play on (the
     pattern plays twice, the second pass is kept: the first one's tail is under its start, so it loops without a cut). The list
     opens on the option chosen last. With Rec armed, or a recording running, Shift + Play ends it at once (no list).
   - **The engine marks the pass** (`StepSeq::record_pass(skip, stop)`, command `SeqRecord`): a pass = the pattern's steps once,
     from the first step played in it. After `skip` passes (Loop: 1) the next one is marked in `SeqPos` in the block it starts
     (`rec_start`, `rec_frames` = its steps at the tempo, from the slot times, swing included), and the recording tap
     (`CaptureTap::arm_seq`) begins at that block's first sample, where the first step sounds (events are block-accurate: the
     step sounds in that block), for exactly those frames. With `stop` the run ends where the pass ends, before a queued pattern
     would be taken: its next step does not play, the notes held over the end are let go there, `Engine::seq_halts()` counts it
     and the run does not start again until a setup with run = 0 (the app sets `seq.running` off when it sees the count move).
   - **The tail**: after the pass the tap goes on until the sound has stayed under -60 dB (q15 33) for 0.25 s, 30 s after the pass
     at most (`capture_job.cpp` kTail*). Tail always stops the sequencer (my choice: the next pass would play into the tail).
   - **The order** (app_capture.c): the capture opens its file (the card thread), and only once it listens
     (`engine_synth_capture_listening`) is the pass asked for and the sequencer started if it was stopped; asked earlier, a pass
     that starts at once could go unrecorded. Running already: from its next pass start. Stopped by hand (or the section left)
     while it records: the recording ends there and is kept, the pass asked for is forgotten. An empty pattern is refused.
   - **Where it goes**: once the file is in the library a second list, titled with its name and length: "Pad A1" (the selected
     pad; "(replace)" when it holds a sample), "Next empty: A5" (when the selected one is taken), "Library only" (also what Back
     gives). A Loop recording becomes a looping Toggle pad (Trim loop) on the bar when the pass is whole bars, on the beat when
     whole beats, else free; the others a one-shot at full level. Names: `rec<n>`, as the Pads section's recordings.
   - Measured on the host (`test_seq.cpp`): one pass at 120 BPM = 96001 frames for a bar of 96000, sound from the first sample;
     with a 0.3 s release the tail ended 0.53 s after the pass, its last 50 ms silent. Not run on the board (the card's speed
     against a long tail; the cost is nothing per block when not recording).
   - Chosen by me (say if one is wrong): the six labels and their order; Tail always stops; the tail's -60 dB / 0.25 s / 30 s;
     the destination list's options; Back on it = the library only; Shift + Play during a recording stops it and keeps it;
     the Run field reads Audio while it records.

**2026-10-22, design round "ui ux and editing tools", scope the synth's piano roll only (the user's brief of 2026-10-14: "make
the notes selectable with joystick, to do this, put the per step parameter inside a menu, and make the piano roll taller + make
notes 3px high so the middle part can show it is selected (unselected = 3px by (step_length)px unfilled box plus pixels to
represent velocity, selected = 5px by (steplength+2)px, to put to the test)"; two rounds of options). Built the same day,
host-tested, tried by the user the same day ("perfect all working fine"); the points where the build differs from the first
wording are marked "as built":**

25. **The joystick picks a note in the roll** (their choice, my recommendation): left / right = the previous / next step, rests
   included (key repeat as the joystick's virtual keys); up / down = the notes of that step's chord, ordered by pitch, then, when
   the chord has room (fewer than 4 notes), **an empty slot drawn above its top note**; up past that goes to the page header, down
   from the header comes back (as on every page). A rest shows only the empty slot. The selection is today's cursor + chord voice
   (`ui->cursor`, `ui->seq_voice`; `seq_chord_slots` orders them by pitch), so the column knobs (Note, Len, Vel, Prob, decision
   10, the same threshold), the tools, step entry and live record are unchanged. As built: the empty slot sits at the pitch a
   note added there takes (`seq_free_pitch`), ordered with the notes by that pitch, so it is not always above them; left / right
   lands on the note of the new step nearest in pitch to the one selected; encoder B on the slot starts at that pitch too. With joystick XY mode on, the joystick drives its
   parameters as before, not the roll.
26. **No menu: an info line under the roll** (their choice; **against my recommendation**, a popup list opened by the joystick's
   push: risk = one value at a time is marked for encoder B, and the 25 characters of a line are full). Line 1: the step and the
   selected note's values (`05/32 C#4 L2 V100 P75 x2`), line 2: Tool and On, then the Run / Rec / Erase / Audio word with the
   play / stop picture. As built: three lines, the room was there (`C#4  L2  V100  75%  x2` in five cells; `Tool Copy` and
   `On Page`; the step `05/32` with Rec / Erase / Audio and the play / stop picture on the right). **Encoder A picks what encoder B changes**: the header, Note, Len, Vel, Prob, Rtch, Tool, On (the
   linear order of today's rows; the Step, Voice and Run rows go: the joystick does the first two, Play the third). The picked
   value is drawn filled, as a row today.
27. **The joystick's push adds or removes the selected note** (their choice, my recommendation): on a note = it becomes a rest
   (that chord voice emptied; the step keeps Len / Vel / Prob / Rtch while a note is left in it, a step left without notes gets
   the default values, as the erase of decision 21); on the empty slot or a rest = the last note entered goes there (one
   semitone above the top note when that pitch is in the step already). With encoder A on
   Tool or On, the push runs the tool (as the Tool row today). A push that removes is kept by the one level of Undo (as the erase
   of decision 22; it replaces a tool's undo). Risk (stated to them): a stray push deletes a note.
28. **The roll: 3 px a semitone, 2 octaves** (their choice, my recommendation): 24 semitones x 3 px + frame and padding = 76 px
   under the 16 px header, the playhead strip, the two info lines. It still scrolls by pages of 16 steps and by octaves
   (decision 23). **A note = an unfilled box 3 px high and its length long; the selected one 5 px high and 2 px longer** (1 px
   over each side and over its neighbours' rows, the pixels under it cleared first). **Velocity fills the box's inside line from
   the left, vel / 127 of its length** (their choice, my recommendation; the selected note: its three inside lines); a note with
   Vel above 0 shows at least one pixel. A rest's empty slot: an unfilled 5 px frame, one step long.
   Chosen by me (say if one is wrong): Prob below 100 % = the box's top and bottom lines dotted; a ratchet = a 1 px tick inside
   the box at each hit's start (as built: a pixel of its inside inverted, so it shows on the filled part and the empty part);
   the cursor's column frame and its dotted line go (the selected note marks the cursor); a screen lower than the tall layout
   (the 128 x 64 one) keeps the thin filled bars, the selected one framed, and one info line (the one encoder A is on); the
   empty slot of a rest sits at the last note entered. Where two notes are a semitone apart the selected one's 5 px cover 1 px
   of its neighbour (3 px a semitone): expected from the sizes, to look at when tried.
   Tests: `seq_chord_notes_are_picked_with_the_joystick` (test_seq.c), the others moved from the removed rows to the joystick.

- Cost (static internal RAM, where the board is short; CONTINUE.md "Internal RAM"): the `Engine`'s two sequencers 3.4 KB (the notes'
  pattern and the queued one 1.0 KB, the pads' two hit lists 1.3 KB, held notes, pads and ratchets 1.0 KB), `s_synth` 0.5 KB, the
  application's pattern that plays 0.55 KB, the tools' clipboard and undo 1.0 KB, state 0.4 KB: **about 6 KB**; the stored patterns
  (9.4 KB) are in bulk memory. The firmware's static RAM went from 66.2 % to 67.7 % (221996 bytes: +5.1 KB). Not measured on the board.
  CPU: per block a pass over 16 + 16 + 8 pending slots; per step a pass over the pattern's hits (160 at most); a change of pattern
  copies 512 or 640 bytes in the audio thread. Not measured on the board.
- Limits: events are block-accurate, not sample-accurate. The sequencer and a key playing the same pitch share that note: the
  sequencer's note-off releases it; a pad played by hand while its hits play is cut and restarted like any pad pushed twice. Live
  recording has the control loop's latency (a note is placed when its key event is handled). The step count used for recorded
  lengths starts again when the rate changes or a pattern is taken: a note held across that gets a wrong length. The longest synth
  pattern (64 steps of 4-note chords) is 2.35 KB of text, a full pads pattern 3.15 KB, inside the 4 KB file buffer. The simulator
  without an audio device does not sequence. The start reads up to 18 small files from the card (not measured on the board). The
  pads' sequencer has no edit tools (copy, clear a track ...) and no step entry. The capture's "N bars" and a pads pattern both start on the bar: a captured pattern is on the grid.
- Tests: `apps/ritual_machine/tests/engine/test_seq.cpp` (16: times at every rate / swing / tempo, no drift, gates, chords, velocity, transpose,
  probability, ratchets, directions, start / stop / restart / next bar, live edits, the position, the queued pattern, the engine and
  `engine_synth` paths, the Velocity module), `test_pads.cpp` (2 more: hits on the clock at their level, ratchets, a gate pad held
  for its length, a toggle pad, the queued pattern, a stop), `test_motion.cpp` reworked (the lanes against the sequencer's step
  times, 72 combinations); `apps/ritual_machine/tests/ui/test_seq*.c`, `test_vel.c`, `test_pseq.c` (25: the pages, the tools, the knobs' threshold, step
  entry and live record, patterns, chain, the files, the hand-over to the engine, step mode and its LEDs).

### ADR-044: A page list on Shift + Menu, a page jump list on a long Menu press (Accepted; user choices of 2026-10-28 / 29; built on `main`, `e45de47` .. `d30f0b2`; flashed and tried by the user on the prototype 2026-10-29: "all good", every point of the test list; the jump list in Pads changed at their word, decision 8)
**Why.** The user, preparing the controls of the next prototype: "the shift menu should now show a list of pages available
including main view (with all pages unchanged)", "a long menu press should show a list of dynamic pages in the main view",
and "group stuff together: the advanced options in a separate submenu, the main ones together, and modes". Until now a screen
is reached by stepping through the menu's tabs (up to 10, left / right) or the main view's pages, and Shift + Menu switches
Synth <-> Pads.

**Decisions taken by the user** (one round of options with pros and cons; recorded, not to be re-litigated):
1. **The list** (Shift + Menu), grouped under headings:
   `MODES`: Synth (the main view, its pages unchanged), Pads (the 404-style section: grid, editor, steps);
   `MAIN`: Rack (Synth) / Pad FX (Pads), Presets, Sound, Sequencer (the SEQUENCER page / the Pads STEPS), **Macros** (the
   user: "a main part of recording movement and texture change made by the user");
   `ADVANCED >`, a submenu: Keys, LEDs, Modifiers, Joy, Curves, Storage.
2. **A short Menu press stays as today** (the tabs, on the last one; my recommendation): the list is a way in, nothing to
   relearn, the saved settings untouched.
3. **The entries follow the mode** (my recommendation): Rack <-> Pad FX, Sequencer <-> Steps, Sound only in Synth.
4. **Sound = GENERAL + SAMPLES (Modular) or ALGORITHM (FM) merged into one screen** (I offered: one screen, or separate
   entries).
5. **A long Menu press** lists the main view's pages (the ones built from the patch, then the global ones); the one picked
   is shown.
6. **SOUND replaces GENERAL + SAMPLES / ALGORITHM in the menu's tabs** (asked while building, my recommendation; the other
   option: Sound only in the list, the three tabs kept): 9 tabs for every synth type.
7. **Vol, Out, Spk and Knob stay in SOUND** (asked while building; the other option: Out / Spk / Knob to a Settings entry of
   ADVANCED).
8. **No jump list in Pads** (2026-10-29, after trying it; their words: "the long press on menu shouldn't show synth parts in pad
   mode"; offered: the Pads screens, nothing, the 64 pads): a long Menu press there is a short one (the FX chains, at the
   release). It replaces the second half of my point 2 below.

**The plan, one commit per step.** (1) The list popup takes more options than fit (it scrolls), headings that the focus skips,
and labels asked from the caller (a list of 69 pages needs no copy of their names). (2) The list on Shift + Menu. (3) The long
press. (4) Storage: the USB card reader, through the storage HAL. (5) The Sound screen. (6) Docs.

Chosen by me while building (say if one is wrong):
1. **Shift + Menu opens the list at once** (on the press); **a plain Menu press acts on its release** when it was short, so a
   hold can become the jump list (`MENU_LONG_MS` 500): the menu opens a moment later than before, at the release.
2. **In Pads, the synth's entries take you to the synth** (Presets, Macros, the ADVANCED tabs: they are the synth's menu
   tabs); Sound is not listed there. (The long press in Pads listed the synth's pages too: changed by decision 8.)
3. **The Synth <-> Pads switch moves into MODES**; the key function Section (`ACT_SECTION`) stays as a direct switch.
4. **The list opens on the entry of the screen on show** (Rack when the RACK tab is open, Synth on a main view page ...), the
   jump list on the page on show.
5. **Pads, picked in the list, is the grid**: the FX chains and step mode close (the pad editor stays as it was).
6. **The jump list opened from the menu with a rack edit applies it first** (the menu closes, the synth is rebuilt), so the
   list holds the pages that edit makes; Back then leaves you on the main view. Without an edit the menu stays open behind it.
7. **Storage asks first** (Yes / No, No focused), then saves everything (`app_save_now`) and calls the new storage HAL
   function `storage_card_reader` (HWV1: `board_card_reader`; the simulator has none and says so).
8. **The SOUND tab's picture follows the part the focus is in**; SAMPLES' length / root lines under the list and ALGORITHM's
   patch name are gone (no room under a list that scrolls; the patch name is the Patch row). Its icon is GENERAL's until
   `16/tab_sound` is painted.
9. **The tab value `TAB_SOUND` is appended**, the old three kept and mapped onto SOUND by `tab_index_of`, so a jump slot saved
   on any of them lands there.

**As built (2026-10-28), one commit per step.**
1. `framework/ui/popup.*`: `POPUP_CHOICES` 6 -> 128, `popup_choose_labels` (a label callback, `true` = a heading), the focus
   skips headings, headings drawn centred between two lines (no memory per option: the jump list of up to 69 pages needs none).
2. `app/app_lists.c` (new): the list and its submenu (`list_ent`, 16 bytes in `app_t`); `synth_ui_go` / `synth_ui_where`
   (`synth_go_t`) in `ui_pages.c`. ACT_MENU with Shift opens it. The UI tests' `shift_menu()` picks the other mode in it
   (`ui_list_pick`, `test_main.c`).
3. The long press: ACT_MENU records the control and the time (`menu_ctl`, `menu_ms`), `app_menu_poll` (from
   `app_input_poll`) opens the list after `MENU_LONG_MS`, `app_menu_up` (both release paths) runs the short press;
   `synth_ui_page_title` / `synth_ui_go_page`. A trap met: the step's `now` can be older than the press time (the tests'
   clock moves at every call), hence a signed difference.
4. Storage: `storage_card_reader` in `hal_storage.h`, HWV1 (`card_task.cpp`), the simulator (`storage_sim.c`), the stubs.
5. `scr_sound.c` (was `scr_general.c`; `scr_fm.c` folded in and removed, `scr_samples.c` keeps the samplers' helpers):
   15 elements, the ones that do not apply hidden (`EF_HIDE_WHEN_DISABLED`), the list scrolls. `tab_kind`: one table of 9.
   `tools/ui_dump.c`: "menu" is a press and a release.
- Checked: 162 UI tests (new: `test_popup.c`, `test_lists.c`: the list in both modes, the submenu and its Back, the rack
  edit left by the list, Storage, the jump list from the pages, the menu and Pads, the SOUND tab's rows per synth type); the
  Ritual Machine's simulator and firmware build after every step (static RAM 154132 -> 154148 bytes), the template's
  simulator and firmware after step 4; the SOUND tab drawn by `ui_dump` at 128 x 128 (the settings with the info box, then
  scrolled to File with the overview) and looked at.
- **Not done / not checked**: nothing flashed, nobody used it by hand; the 128 x 64 drawing of the list and of SOUND was not
  looked at; the HWV1 simulator was not rebuilt; Storage on the board (the restart into the card reader) not tried from the
  list; the engine tests were not run again (no engine file changed).

### ADR-045: LEDs under the pots and encoder A, for the next prototype (Accepted; user choices of 2026-10-28 / 29; built on `main` 2026-10-29, `5ed38ed` .. the docs commit; host-tested and seen in the simulator, NOT on hardware: no board has these LEDs yet)
**Why.** The user: "the leds are mapped through keys coordinates, but the next prototype will have leds under potentiometers and
one of the encoders"; "1 under each pots, and one additional one under encoder A". **What was there:** the LED HAL already
addresses an LED by the board control it sits under (`hal_leds.h`, `input_control_t.led`), and `controls_led` maps a role onto
it; but no pot or encoder of any board table has `led` set, the simulator draws an LED on keys only, and the app
(`key_leds.c`) paints the key matrix only (that loop, over key rows and columns, is the "key coordinates").

**Decisions taken by the user** (two rounds of options; recorded, not to be re-litigated):
1. **All 8 pots of the next prototype** (the simulator's panel: Volume, K1..K4, R1..R3) **and encoder A** have an LED (my
   recommendation; the other option: not Volume).
2. **What a pot's LED shows, configurable** (their words: "catch state using brightness, value with color saturation, and role
   colour override, macros color matching on macro mode, show the updated color on shift or mod press (if a pots switches to a
   macro on shift/mod press)").
3. **A colour per macro**, a Color row in the MACROS tab (my recommendation; the others: fixed by number, one Macro role).
4. **Encoder A: section + Rec** (my recommendation): its own role, one shade in Synth and another in Pads; blinking while a
   recording is armed, lit while it records.

**The plan, one commit per step.** (1) The panel's table gives the 8 pots and encoder A an LED; the simulator draws a ring around
a knob or an encoder that has one. (2) New LED roles Knob, Volume and Enc A (LEDS tab, ui.cfg): a column knob shows the Knob
role, its Idle shade until it has caught the value, its Active shade once caught (catch as brightness), the value as
saturation (pale at 0, the full colour at the top; a Value row switches it off per role); Volume the same, always caught. (3)
The macros' colours (MACROS tab Color row, saved with the sound): R1..R3 and any knob that plays a macro show its colour. (4)
Shift / Mod held: every pot shows what it does in that layer. (5) Encoder A. (6) Docs.

Chosen by me while building (say if one is wrong):
1. **Default colours**: Knob Amber (Away 15 %, Caught 100 %), Volume White 40 % / Green 100 %, Enc A Sky 60 % (Synth) / Amber
   60 % (Pads); the macros Red, Teal, Violet, Yellow, Pink, Lime, Sky, Orange (all different). All editable. (ADR-046 phase 6 made
   macro 1 Blue and encoder A Mint in Pads.)
2. **Saturation**: the pale end is white at the colour's brightness with a quarter of the colour in it (never plain white).
3. **A col knob in the menu is dark** (it drives nothing there); **in Pads it shows Caught without a value** (the pad editor's
   values are not measured); **a knob not moved since the start is Away** (its place is not known).
4. **A walked value is never measured for an LED**: walking moves the value (found by a test: the sequencer's cursor moved),
   and the LEDs repaint every 20 ms. The LED reads the knob's place once it has caught, else the last catch refresh
   (`knob_norm`, 16 bytes); exact drivers are read (`synth_ui_target_peek`). The volume is read from `cfg.volume`.
5. **R1..R3 have no catch** (a macro knob sets at once): the Caught brightness; Away while the macro has no destination.
6. **The layer view**: a col knob's Default entry with a modifier = dark (it drives nothing); a Default on R1..R3 or Volume =
   their plain view; None = dark; a macro, or Learn into one (Shift + R1..R3 by default) = the macro's colour; the volume (as an
   action or a parameter) = the Volume role; another action = the Knob colour; a parameter = Knob with its catch and value.
7. **What counts as a recording for encoder A**: the synth's notes (armed = Rec on with the sequencer stopped, recording =
   running), the pattern's sound, the Pads' capture (armed / recording / finishing), the Pads sequencer's live record (armed =
   the clock stopped). Blink 150 ms, in Back's active red, as the Rec key.
8. **The LEDS tab**: Value shows n/a for the other roles; Knob's shades are labelled Away / Caught, Enc A's Synth / Pads; the
   picture names the pots. **The MACROS tab's Color row is the second row** (the rows below moved by one; never Off).
9. **The simulator's LED is a ring** 3 px wide just outside the part (a thin grey ring when dark).

**As built (2026-10-29), one commit per step.** (1) `panel_controls.c`: `LIT(...)` for the 8 pots and Enc A; `panel_sim.c`:
`led_ring`. (2) `led_roles`: LR_KNOB, LR_VOLUME (+ `value`, saved as `novalue` when off); `scr_leds.c` Value row;
`synth_ui_col_knob_state`, `synth_ui_target_peek`; `key_leds.c`: `pot_leds`, `pot_preview`. (3) `macro_def_t.color`,
`synth_ui_macro_default_color`, `mcolor` lines in the sound (`patch.c`; the sound's hash counts it), MACROS Color row. (4)
`layer_pot` (`app_active_layer` from `app_layers.c`). (5) LR_ENC, `enc_led`.
- Checked: 168 UI tests (new: the panel's LED flags; the catch shade, the saturation, the Value switch saved and read, the
  volume, the menu, the LEDS preview; measuring never moves a walked value; the macro colours, their row, saved with the sound
  and in its hash; the layer view; encoder A in both sections and while recording); the simulator built and its panel
  screenshot looked at (rings in the right colours); the firmware builds after every step (static RAM 154196 bytes: +48).
- **Not done / not checked**: no hardware has these LEDs (the next prototype's LED chain order, brightness and colour
  rendering are unknown); the user has not seen it; the ESP32's cost of painting the pots every 20 ms was not measured (it
  runs on HWV1 too, where the LEDs do not exist: a few reads per frame); the HWV1 simulator shows no pot LEDs (its board has
  none).

### ADR-046: The GUI reorganization, one UX ruleset, built for HWV1 (Accepted; user choices of 2026-10-29; phases 1, 2, 4 built on branch `gui-reorg` 2026-10-29, `bf4151f` .. the docs commit; host-tested; flashed and tried by the user the same day: "working properly"; phase 3 built the same day, `02c7a94` .. the docs commit, host-tested; flashed and tried by the user 2026-10-30: "tested, working"; phase 5 built 2026-10-30, `59f5635` .. the docs commit, host-tested, NOT tried by the user; phase 6 built 2026-11-04, host-tested, NOT seen on an LED)
**Why.** The user, 2026-10-29: "you have the role of UX designer, create a plan to reorganize the whole GUI ... without leaving
current features hidden or lost ... a current and future proof ruleset for creating a new UX for specific hardware". The plan
(rules R1..R23, the views, the controls per board, where every feature goes, the phases) is
[UX_REORGANIZATION.md](UX_REORGANIZATION.md) and the claude.ai doc it links. The problems it solves
(P1..P10): two editing models, push with six meanings, invisible knob targets, four routes through two maps, sound / sequencing
/ setup mixed, Synth and Pads not parallel, a destructive Back, hidden pending state, hardware gaps solved case by case, idle
column knobs in the menu.

**Decisions taken by the user** (two rounds of options, 2026-10-29; recorded, not to be re-litigated):
1. **Design for HWV1, the board in hand** ("we will make modifications for the next board on another app version"). The next
   board (7 relative pots, a joystick, 2 push encoders) is out of this work; the simulator's panel stays a reference that must
   keep working. (Q1)
2. **Push = Enter everywhere**: the joystick push latches a value, opens or activates; never "right". (Q7, my recommendation)
3. **JOY moves to a PERFORM view next to MACROS** (reopens ADR-044 decision 1, on the user's answer). (Q3, my recommendation)
4. **Out, Spk, Knob move from SOUND to SETUP > MACHINE**; Vol stays in SOUND (reopens ADR-044 decision 7, on the user's
   answer). (Q4, my recommendation)
5. **Back never deletes**: on RACK, Back goes up; Delete stays in the cell menu. (Q2, my recommendation)
6. **No Relative knob mode for now**: Catch / Direct stay as they are. (Q8; against my recommendation, Relative as the default
   on endless pots. Risk: HWV1's endless pots keep the catch, a knob can still have to travel before it acts.)
7. **Pads capture also in a Pads Rec list on Shift + Play** (next to step mode); the Rec key keeps working. (Q5, my
   recommendation)
8. **The extras in**: the Bar widget, Reset to default (a cog item), a macro Value field (set a macro from the MACROS page),
   and **a battery indicator in the header** (theirs). **Out**: the latched-notes count; Enc A / B held gestures (HWV1 has no
   encoders). (Q9)
9. Asked on 2026-11-04 ("Phase 6" below): **Q6** (colours: macro 1 off Red, a Pads hue for Enc A): ADR-045's pot LEDs are not on HWV1; to ask before the
   LED phase.
10. **The order of the work** (2026-10-29): phase 1 (the knob legend), then phase 2 (one editing model), then phase 4 (safety);
   the user away for some hours: "make decisions on your own, I trust you! Make your best and be creative". One commit per
   step on `gui-reorg`.

**The plan, one commit per step** (guide section 7): (1) the knob legend zone and the header marks, the battery; (2) the main
view's editing model: push = Enter / latch; (4) safety: Back never deletes, the pending mark, the recording states in the
header. Phases 3 (views and rings), 5 (the Tier 0 fallback table, the Pads Rec list) and 6 (LEDs) after.

Chosen by me while building (the user away; say if one is wrong):
1. **The legend zone** (`core/ui_legend.c`): laid out from the knobs' places in the board table (`controls_place`, new): 2 x 2
   cells of 64 x 9 on HWV1 (21 px with the rule), 1 x 4 cells of 32 x 12 on the panel (14 px); none under 100 px of screen. A
   cell: the knob's number (filled: it drives something; hollow and "-": nothing), what it drives, **a bar of the value** (the
   Bar widget of Q9, used here first), and while the knob has not caught the value **a tick where the knob stands** (XOR across
   the bar): turn from the tick to the end of the fill. The row's catch chevron stays.
2. **Only on the screens where the knobs drive something**: the synth's pages, the CURVES tab (Pt X / Pt Y), the Pads screens
   but the FX chains. The menu's other tabs and the FX chains keep the full height (RACK's three rows of 32 px do not fit over a
   legend: it would show one row only). This bends rule R14 (fixed zones): the zone is fixed per screen, not on every screen.
3. **The layer preview** (rule R11): while Shift / Mod is held the cells show the layer's targets (a parameter's short name, "M1",
   an action's name, "-" for Default); a parameter's bar and catch as for a page row; the volume and the speaker read from the
   setting.
4. **Pads**: Start, End, Tune, Cut (what the knobs drive there, no catch); a module's values page: its first four values.
5. **The header**: the page's place "3/22" at the left of the synth's pages; at the right **the battery** (a 12 x 7 icon, filled
   as charged, a stroke across when empty; drawn only when a battery is measured; redrawn when its value changes, polled every
   5 s) and **an "S" / "M" while a modifier is held** (both sections' headers, XOR so they read on a focused header).
6. **The pad editor's overview shrinks** over the legend (PLAY / EDIT 24 -> 17 px, CHOP 40 -> 39 px; EDIT's rows 11 -> 10 px) so
   every row stays on screen.
7. `ui_dump` binds the board's table (panel, or `UI_DUMP_BOARD=hwv1`) and takes the events `pads`, `synth`, `batt:<n>`, `push`.
8. **Phase 2, push = Enter on the synth's pages** (`page_enter`, `ui_input.c`): a value row **latches** (filled `<value>`, as on
   the latch screens); **latched, up / down step it by 4** (left / right by 1, as unlatched), so the joystick alone gives fine
   and coarse; a push or Back releases (Back does nothing else then), encoder A's row move and a page change release too. A
   two-state row **flips** on a push (PATTERNS Chain, MOTION On, LANE Mode / Pol); the cog opens / closes. **The focus on the
   pages is now a frame** (it was filled), the menu's look: filled = latched only (rule R16).
9. **The XY binding moves to a held push** (0.5 s, `app_push_down` / `_poll` / `_up`, `app_input.c`). **This changes a design
   of the user's** (2026-10-07: a plain push on a page row bound it to the joystick), which my Q7 did not mention: push = Enter
   (their answer) and the binding both stay, the binding behind a hold. On a parameter row the short push therefore acts at its
   release (as Menu's). In XY mode a plain push still binds at once (the stick plays there; nothing to latch). Risk: the user's
   habit; the other way (bind only in XY mode) is a small change if they prefer it.
10. Not done in phase 2: the pages are not turned into element tables (the plan's "main view declarative"): the gestures are
   one set, the code keeps its page tables and hand-written handlers. The SEQUENCER page keeps its push (a note in / out, a
   tool run): a named exception.
11. **Phase 4, Back in the menu goes up one level** (`handle_event`, `ui_input.c`): a latch, then the screen's own (RACK: its
   cell menu), then the focus to the header, then **out of the menu** (its edits applied, as Menu does). Before, Back did
   nothing on the list tabs and deleted the selected module on RACK; **Delete is now only the cell menu's item** (decision 5).
   The pages' Back is unchanged but for the latch (it still closes the cog; held while recording it erases).
12. **The pending mark**: the menu's header reads `< RACK* >` (any tab) while structural edits wait; leaving the menu applies
   them and **a toast says "RACK / Applied"**.
13. **The recording mark** in both headers (`rec_mark`, `app.c`; drawn by `ui_header_marks`): `REC` framed = armed (the
   synth's Rec on and stopped, the Pads capture waiting, the Pads live record with the clock stopped), `REC` inverted =
   recording (running, the pattern's sound being recorded, the capture), `ERS` inverted = Back held erasing. No blinking (it
   would need timed redraws: the frame / fill difference says it).

**As built (2026-10-29, branch `gui-reorg`), one commit per step.** `bf4151f` this ADR; `193229b` phase 1 (`core/ui_legend.c`,
`controls_place`, `synth_ui_legend` / `row_text`, `app_layer_legend`, `app_pads_legend`, `pads_ui_fx_legend`, the header's
marks, the battery from `app_power_poll`, the pad editor's overview heights); `1f17964` phase 2 (`page_enter`, `row_flips`,
`draw_row`, `app_push_down` / `_poll` / `_up`); `83d6762` phase 4 (Back in the menu, `rec_mark`, the "*", the Applied toast).
- Checked: 184 UI tests (16 new: `test_legend.c`, `test_enter.c`, `test_safety.c`; `test_joy.c`'s binding now holds the push);
  both simulators built; the screens looked at as images (`ui_dump`, HWV1 and the panel: the legend, the Shift preview, the
  Pads editor's four pages, the latch, the header).
- **On the board** (2026-10-29): the user flashed `gui-reorg` and tried the new UX: "so far it's working properly". No details
  reported yet on the HWV1 knob places or the held push.
- **Not done / not checked**: the 128 x 64 screen has no legend and was not looked at; the Bar widget is not built
  (phase 5, Reset to default and the macro Value: "Phase 5" below; Q6: "Phase 6" below); the menu's other tabs keep idle knobs (no "first four numeric fields" yet).

**Phase 3, the views (2026-10-29, the user: "let's start"; built on `gui-reorg`, one commit per step).** Chosen by me (the plan,
guide sections 4 and 9, gave the views, the focus levels and the routes; these are the details; say if one is wrong):
1. **The views**: EDIT (the patch's pages: modules, AMP, FM / Strings, row M's effects), SEQ (SEQUENCER, SEQ SETUP, PATTERNS:
   moved after every type's own pages, so EDIT's ring never reaches them), RACK (RACK, SOUND), PERFORM (MACROS, JOY: decision 3),
   PRESETS, SETUP (KEYS, LEDS, MODIFIERS, CURVES, MACHINE). The tab order follows the views; `tab_t` only appended (`TAB_MACHINE`).
2. **The header**: the view's **badge** at the left, two letters in a frame (ED, SQ, RK, PF, PR, ST), the look of the tab icons it
   replaces (a painted `16/view_<name>` sprite takes its place when there is one), then "n/N" in the view, then the title, tighter
   ("<TITLE>", then without arrows) when it does not fit; no arrows on a one-page view (PRESETS).
3. **The view selector** is a focus of its own (`view_sel`): **up from the page selector** (encoder A's row moves skip it; the
   key function Top goes page selector, then view selector). The badge fills; left / right / Page - / + step the views, each shown
   where it was left (`view_at`), the focus staying on the badge; down or Enter go back in (to the first row of a one-page view);
   Back goes home. The ring holds SETUP too (after PRESETS), so stepping reaches everything.
4. **Up from a page's header no longer wraps to its last row** (it goes to the badge); encoder A still wraps.
5. **Menu** = EDIT <-> the last other view (RACK at first, as the menu was). Before, Menu opened the menu from any page, the
   SEQ pages included; now from SEQ it goes home and back.
6. **Back on the pages climbs** (it did nothing there): a row to the header; on the SEQ view's header, home. In the menu as
   phase 4 made it, "out of the menu" now meaning home (EDIT).
7. **MACHINE** (SETUP): Out, Spk, Knob (decision 4) and **Storage** as a button (asks first, as the map's entry), the battery in
   its picture (moved from SOUND's). SOUND keeps Type .. Vol and its sample / algorithm parts.
8. **The map** (Shift + Menu, title MAP): a flat list, one heading per view (MODES, EDIT, SEQ, RACK, PERFORM, PRESETS, SETUP),
   so every screen but the settings is 2 actions away (the settings 3: Setup > is a submenu). Not a submenu per view: Sound would
   have needed 3 actions where it needed 2. MODES Synth in the synth moves nothing; Edit = the EDIT view as left.
9. **The page list** (Menu held): the pages of the view on show (EDIT's, SEQ's three, a view's tabs), titled with the view.
   Still not in Pads (ADR-044 decision 8, the user's).
10. **Pads**: the same views, **PERFORM and SETUP as the synth's tabs shown over the pads** (the plan's "opened over Pads without
   switching"): the section, the keys and the music stay; Back from their header goes to the Pads' EDIT; the jump slots and the
   column knobs do nothing there; Learn (MODIFIERS, MACROS) goes to the synth section first (it takes a page row). PRESETS is
   empty in Pads: skipped. **Up from the top** of a Pads screen (the grid's first row, STEPS' first row of steps, the editor's and
   the FX pages' header) reaches the badge. Menu = EDIT <-> the last other view, PAD FX at first (Menu's old job in Pads). EDIT
   comes back with the pad's editor open if it was.
11. **Each section keeps its own tab** (`sec_tab`): the synth on SOUND, Pads on MACHINE: switching section shows each one's.
   Leaving the synth with rack edits now regenerates its pages at once (they were left stale until the next rebuild).
12. Not done in phase 3: Pads views are not jump slots; the "3 actions" gate was checked by the tests' routes (`test_views.c`,
   `test_lists.c`), not by a table walk of section 9 per board; the 128 x 64 header was not looked at.

**Phase 3 as built**: `02c7a94` the synth's views (`view_t`, `synth_ui_view*`, `ui_view_note`, `ui_leave_menu`, `view_sel_event`,
`ui_view_badge`, `ui_header_title`, `scr_machine.c`); `7c1ce95` the map and the page list (`app_lists.c`); `ba57997` the Pads'
views (`pads_ui_view`, `at_top`, `app_pads_view*`, `pads_tab_event`, `sec_tab`). Checked: 197 UI tests (13 new in `test_views.c`,
`test_lists.c` rewritten for the map); both simulators built; the headers, MACHINE, the map and the Pads views looked at as images
(HWV1). **Flashed and tried by the user 2026-10-30: "tested, working"**; they try it in use and report issues.

**Phase 5, the fallbacks (2026-10-30, the user: "you can start now phase 5").** Their answers (a round of options; recorded):
1. **Click, Tap, BPM** without the right block: **Click in the Rec lists** (both sections), and the actions **mappable on the
   Shift / Mod layers** (the user: "put it in the rec list and add the action to mappable modifier keys"). The layers already took
   every key function (Click, Tap, BPM +-1 / +-5, Rec) and Tempo on a knob: checked by a test, nothing to add.
2. **The pad bank without encoder B: Shift + joystick left / right** on the grid (my recommendation). It already worked: fine
   steps send Value +- and the grid takes them as the bank; now a test and the table say so.
3. **The extras the table needs are built in phase 5** (my recommendation): the macro Value row and Reset.
4. **Reset: both** (the cog held a Reset only on the two modules with a cog): **a cog on every module** (but the motion sequencer
   and the EG, whose first page is drawn by hand) whose last row Reset puts every value of the module back to its default,
   asked first (No focused; the sampler's File and the motion sequencer's pool kept), **and one value**: Shift + push while a
   value is latched. The user then asked whether the reset used the modifier system or was hard-coded: it was a branch in
   the Joy XY action; it is now **an action of its own, Reset** (`ACT_VALUE_RESET`, the key function word `reset`), mappable on
   any key or layer entry (the focused value, latched or not); Shift + push's default calls it while a value is latched.

Chosen by me (say if one is wrong):
1. **The Pads Rec list** (Shift + Play in Pads; decision 7): Capture (until Shift + Play or Rec again), Capture 1 / 2 / 4 / 8
   bars (sets Shift + Rec's length), Pattern rec (Select + Rec), Steps on / off (Select + Play), Click: Off / On. It opens on the
   option chosen last (Click excepted). **Shift + Play no longer toggles step mode at once in Pads** (one press more; Select +
   Play and the SEQ view still do). While a capture or the pattern's record runs, Shift + Play stops it, no list.
2. **The synth's Rec list** gets Click: Off / On as its last option (not remembered).
3. **The macro Value row** (MACROS, after Macro): where the macro is, 0..100 %, 5 % a step, Shift 1 %; set like a knob at that
   position (every destination through its mapping). The position is where it was last set (a knob, the row); until then it
   is measured from the first destination (the nearest position on its mapping), so the first step does not jump. An encoder
   that steps a macro forgets the position (measured again). Hidden while the macro has no destination. The MACROS rows after
   Macro moved down by one.
4. **The cog's Reset row** reads "Reset All"; a push asks "Every value of FL1 back to its default?". The single-value reset
   shows "Default: <value>" titled with the parameter, or "No default here" (the sequencer's, SETUP's and FM's values have no
   default table: not covered).
5. The screens' context (`ui_ctx_t`) carries the synth's values (`params`, `seq`) so a menu screen can drive them (Value).
6. Not done: Enc B held (no encoder on HWV1); Reset of the sequencer's, the settings' and FM's values; pad parameters' Reset;
   the Bar widget (an extra, still to do).

**Phase 5 as built**: `59f5635` the Pads Rec list (`app_capture_pads_list`, `capture_arm`, `app_click_toggle`); `dc7f25e` the
macro Value (`scr_macros.c`, `synth_ui_macro_pos`, `macro_pos[]`); `4eed3ce` defaults (`rack_mparam_reset`, `rack_slot_reset`,
`param_reset`, `synth_ui_target_reset`, `PRM_RESET`, `type_has_cog`, `ACT_VALUE_RESET`, `app_module_reset_ask`); then the fallback
test (`test_fallbacks.c`) and the docs. Checked: 204 UI tests; both simulators built; the Rec lists, MACROS and a cog's Reset
looked at as images (HWV1). **NOT tried by the user, the firmware not built by me.**

**Phase 6, the LED language (2026-11-04, the user: "we can start the phase 6", while they prepare the second prototype).** Their
answers to Q6 (my recommendations): **macro 1 is Blue by default** (Red means recording / destructive), **encoder A is Mint in
Pads** (Amber is the column knobs'). Only defaults: a colour chosen in the MACROS or LEDS tab stays, and neither is written to a
file while it is the default, so a preset that never chose macro 1's colour shows Blue. A macro 1 set to Red by hand before this
was not written either, so it turns Blue too.
Chosen by me (say if one is wrong):
1. **The gate "no colour with two meanings" is read over the pots and encoder A only.** The keys already reuse colours across
   roles (Blue: Sharp and Menu; Mint: Mod; Amber: a pushed pad): they are another part of the panel and were not changed.
2. **One exception left**: encoder A's Synth Sky is also macro 7's, which shows only on a knob mapped to it. Told to the user.
3. Near neighbours stay (Yellow, Orange and Amber; Mint and Teal): different colours by the palette, not checked on real LEDs.

**Phase 6 as built**: the first value of `synth_ui_macro_default_color`, `LR_ENC`'s active shade in `led_roles.c`,
`leds_a_pots_default_colour_has_one_meaning` in `test_leds.c`. Checked: 205 UI tests; both simulators built. **NOT seen on an LED:
HWV1 has none under its pots or encoder A, and the simulator's panel rings were not looked at by me. GRV-mini (2026-11-08) has an LED
under each of its 8 pots, and one near encoder A that stays off for now (the user's choice): macro 1's Blue can be seen there, Mint not yet.**

### ADR-047: The knob resolver, one answer to "what does this pot drive now" (Accepted; user choices of 2026-11-08; built on `gui-reorg`, host-tested, NOT flashed)
**Why.** An architecture review (2026-11-08, the user: "let start refactoring then!") found the question answered in 10+ places:
the legend (`synth_ui_legend`, `app_layer_legend`, `app_pads_legend`), the pots' LEDs (`synth_ui_col_knob_state`, `layer_pot`, the
Pads branch of `pot_leds`), the catch (`app_catch_poll`), each redoing the case analysis (3 contexts x 4 readers; the "caught" rule,
the Pads knob table and the sample-length lookup copied), and nothing checking they agreed. A probe showed they did not: in Pads
Shift + K2 set the master volume while the legend and the LEDs showed the pad's End; the SEQUENCER's and CURVES' pots were dark while
the legend named them; Pads pots had no value; PERFORM / SETUP over the pads left the pots lit while the knobs did nothing.

**The module.** `app/app_knobs.c`: `app_knobs_resolve(app, pot[APP_POTS])` describes the 8 pots (K1..K4, R1..R3, the volume knob):
the legend's cell (label, value, knob place, state), a macro whose colour the LED takes, or the LED's role. The legend (`app_draw_frame`),
the LEDs (`pot_leds`: dark / role shade / macro colour from the description) and the catch (`app_catch_poll`, moved there) read it.
`app_layer_legend`, `app_pads_legend`, `layer_pot` and `macro_color` are gone; the Pads cell (`app_pads_knob_cell`) sits next to the
Pads knobs' action and shares its table; `pad_param_norm` (core/pads.c) is `pad_param_set_norm`'s inverse.

**Decisions taken by the user** (a round of options, 2026-11-08; recorded, not to be re-litigated):
1. **The Shift / Mod layers act in Pads too** (against my recommendation: the knobs keep the pad there), and the legend, the LEDs and
   the header's "S" / "M" show the layer held in Pads as in the synth.
2. **The SEQUENCER's and the CURVES tab's pots light** as the legend names them (Knob role; the SEQUENCER's Away until the knob moved
   for that step). (my recommendation)
3. **Pads pots show the pad's value as saturation** (the Knob role's Value switch), as on the synth's pages. (my recommendation)
4. **PERFORM / SETUP over the pads: the column knobs' pots are dark** (the knobs drive nothing there); CURVES over the pads lights its
   two. (my recommendation)

Chosen by me (say if one is wrong):
1. **Learn into a macro has no catch** on the LED either (the legend's answer); its value is the macro's, on the legend too (a bar under
   "Lrn M1").
2. **A column knob playing a macro through a layer** shows the macro's first destination (what its catch is measured against), not the
   macro's last knob position; R1..R3 keep the position.
3. **The speaker as a layer parameter** reads the setting on the LED too (it read the walked value, -1 most of the time).
4. **The knob's action is not routed through the resolver**: it already followed these rules (`app_layer_event`, then the binding
   table); `test_knobs.c` turns pots in each context and checks the resolver's answer followed (a second step if the paths drift).

**As built**: `app/app_knobs.c` (new), `app_layers.c` (the legend and the catch out), `app_pads.c` (`pad_frames`, `app_pads_knob_cell`),
`key_leds.c` (`pot_led`), `app.c` (the legend, `layer_held` in both sections), `synth_ui_col_knob_state` moved to `ui_internal.h`,
`pad_param_norm`. Checked: 215 UI tests (6 new, `test_knobs.c`: every screen and layer, LED dark exactly when nothing is driven; the
four decisions; a turned row reads back), the app's 118 engine tests, the simulator built. **NOT flashed** (the firmware is built
from VS Code); GRV-mini is the first board where the pot LEDs can be seen.

**Fixed after (2026-11-08, the user: "the knob position indicator on knob legends updates very slowly")**: a column knob that has not
caught its row changes no value, so it never asked for a redraw, and the legend's tick moved only when something else redrew (a bug
since ADR-046 phase 1, not from the resolver). `ACT_PAGE_KNOB` (`app_actions.c`) now redraws on each uncaught move while the legend is
shown on a screen with a legend zone, the same rate as a caught knob. A layer's knob already redrew, and Pads has no tick. Checked: 216 UI
tests at 128 and 160 px (`legend_tick_follows_a_knob_that_has_not_caught` counts the frames sent, `ui_stub_frames`); NOT flashed.
