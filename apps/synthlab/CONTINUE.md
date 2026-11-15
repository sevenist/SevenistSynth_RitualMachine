# SynthLab: handoff notes

The app's state and todo. Its synths' designs are in [docs/SYNTHS.md](docs/SYNTHS.md), its controls in [README.md](README.md). The
library's notes, the user's working style and the traps of the environment are in the root [CONTINUE.md](../../CONTINUE.md).

## Session todo (updated 2026-11-12)

**2026-11-15 (commits de94e75 .. da059be):** what changed for SynthLab, and what waits on the user.
- **44.1 kHz on HWV2 too** (the `hwv2` environment), one budget with HWV1.
- **Synths in internal RAM** (`SC_HOT` in lab.h, `-mtext-section-literals` in platformio.ini): the Kick and DrumLab take half the CPU.
- **Lighter modular modules** (the user allowed the sound to change): Osc, ADSR, Perc, LFO, `decay_coef`. **Not heard yet by the user:**
  play Basic and the drums on HWV1, and a patch with an LFO or a Perc (they are now computed every 8 samples).
- **A synth's own voice count**, `lab::Options::voices` (last in Options), sent with the voice mode.
- **Engine synths** (`lab::make_graph_synth`, written by SynthLab Studio's Export > Engine synth): picking one loads its graph (up to
  200 ms of silence, the user's OK), and its values go to its nodes.
  - The tests play them through the engine (`test_graph_synth`).
  - The default patch at 3 voices measured 57 598 cycles a block with 3 notes, about 2 300 a voice more than the float export.
  - None is in the list yet: the user exports their own.
- **Benches:**
  - `hwv1_bench`: the modules, their engine types, the synths in the engine.
  - `hwv1_bench_rm`: the Ritual Machine's modules alone (linked with the rest, SynthLab's engine has no internal RAM left to start).
  - Then `tools/module_bench.py --from bench.txt bench_rm.txt --write framework/modular/module_costs.h`.
  - HWV1 uploads on COM9 (its CH343) and prints on COM8 (native USB).
- On the board now: the normal `hwv1` build.

**Where things stand (2026-11-12):** the user asked for an environment to test home-made synths and oscillator algorithms: a collection of
small all-in-one synths for specific sounds, with a minimal screen (the parameters' names and values, encoder A selects, encoder B
edits) and a file to write the synths' designs in. **The app was made on the new branch `synthlab`** (from `main`): the synth API
(`src/lab.h`), the DSP parts (`src/lab_dsp.h`), the engine module that hosts a synth (`src/lab_voice.cpp`), the screen, two synths (Basic,
FM2), the host tests and the notebook (`docs/SYNTHS.md`). Host-tested (106 checks), the simulator started; every source passes the
xtensa compiler's syntax check; **the firmware was NOT built with PlatformIO, NOT flashed. Nothing committed yet.**

The choices I made (to correct if they do not fit):
- **Built on the engine, not beside it:** a synth runs as the voice of one engine module, so it is polyphonic (6 voices), mono when the
  synth asks, and the same code runs on the board. The voice is written in float, at the engine's blocks of 32 frames.
- **The values are floats in the units shown** (Hz, ms, %), carried to the audio thread in one engine command (`set_blob`, `lab::LabValues`).
- **Each synth keeps its own values** while another plays (not saved: they reset when the program starts).
- **F3 / F4 change synth, PLAY holds a note**, encoder B's push resets a value to its default, encoder A's push goes back to the top.
- **Two starter synths** (Basic, FM2) to show the API, not to be the collection.

**Then (2026-11-12, same session): the Kick**, the user's first synth (docs/SYNTHS.md "Kick"): the chain of their brief, macros, and
the list's sections (up to 48 rows; the values now travel as one blob). Host-tested (252 checks), WAV measured (sweep, decay, no click
on a choke; the click was doubled after measuring), syntax-checked with xtensa. Not yet heard by the user.

**Then: the Kick optimised** (host 60.7 to 32.8 ns per sample; on the ESP32 no division or libm call left in its sample loop, from 41
`__divsf3`), with a seeded noise layer (+ low pass) and a half-rate FDN reverb as the synth's Fx (the new `lab.h` Fx: one engine module
on the voices' sum). 527 checks pass; new `tools/esp32_report.ps1`.

**Then: the noise became a rumble** (Pre / resonant low pass 20..500 Hz / Post, the user's request), and the denormals the bench
found were fixed (`lab::flush`; the bench now warns). 557 checks pass; Kick 46 ns per sample on the host with the rumble.

**Then: an echo on the rumble** (Delay / Feedbk / Dly Mix, at sr / 8 on a 16-bit line). 587 checks pass. On the ESP32 a Kick voice is
3.9 KB (x 6 voices) and its reverb 12.8 KB: about 36 KB of the 64 KB fast heap (framework/app/main_esp32.cpp, APP_FAST_BYTES; it spills to PSRAM beyond).

**Then: the master shaper** (glue compressor with attack / release, saturation, tilt, output; in the Kick's Fx after the reverb),
measured from the WAV (the glue's gain first came a block late: now every 8 samples after the detector; the saturation's gain fixed
twice). 647 checks pass; up to 80 rows per synth.

**Then: ducking for the rumble** (Duck, Duck Rel; keyed by the amp envelope +12 dB, after the echo). 667 checks pass. The test runner's
`-Values` silently dropped values past 16 (found when Duck seemed to do nothing): now 64, and an error beyond.

**Then: In Mix at the rumble's input** (noise <-> the kick's oscillator before its amp env, equal power, levels matched at the filter);
the section renamed RUMBLE (`-Values "RUMBLE/Level=100"`). 677 checks pass.

**Then: PLAY loops the Kick at 135 BPM** (`Options::play_bpm`; the engine's sequencer: 2 eighth steps, transport at the tempo;
`synth_loop_start / _note / _stop`). The app test measures the onsets from the sound. 685 checks pass.

**Then: the reverb's Route** (Output / Rumble). The reverb became a template (`Room`) with two copies: the output's (Fx, half rate,
12.9 KB) and the rumble's (Voice, 1/8 rate, 3.2 KB). 695 checks pass. **Memory on the board**: a Kick voice is now 7.2 KB, and the
engine makes 6 voices for every synth, even a mono one: 43 KB + the Fx's 12.9 KB = 56 KB of the 64 KB fast heap (framework/app/main_esp32.cpp, APP_FAST_BYTES;
beyond it the heap spills into PSRAM). If that becomes a problem: load the graph with 1 voice for a mono synth (Engine::load's
nvoices, at a change of synth).

**Then: the optimisation the user asked for, "starting with reverb and delay, at half the sample rate"**: they were already at an
eighth (6 kHz) in the rumble, and the output's reverb at half; the whole rumble chain now runs at an eighth instead (`rumble_tick`),
Kick 51.8 -> 40.6 ns per sample on the host. Its sound measured against the full rate (`kDecim = 1`, Hann-windowed). 695 checks pass.
Still to propose: the output's reverb at a quarter rate (it processes the whole kick: it would lose highs in its tail); one voice for
a mono synth (memory: 56 of 64 KB). Measuring traps met: a spectrum without a window (leakage looks like highs); a file copied back
keeps its old date and the build does not recompile it (touch it).

**Then: Kick Lite**, an optimised clone of the Kick under a null test. Removed on 2026-11-15 with its null test (the user: start fresh
from the five synths: Basic, FM2, Kick, Snare, DrumLab); its notes are in the git history (docs/SYNTHS.md before that date).

**Then: the rumble's filter** (the user, before optimising more: a filter envelope for the rumble, and a filter selection with every
filter there is). Filter (LP 6 / 12 / 24, BP, HP 12 / 24, Notch, Peak), Env Amt (+-5 oct), Env Atk, Env Dec: the Kick at 72 rows;
the defaults bit for bit as before (the null test unchanged). `lab_dsp.h`: `Svf::set_g`, `fast_tan`. 851 checks pass. Next: the
user's choice among the optimisations proposed (measure on the board, IRAM, render once).

**Then (2026-11-13): the Snare** (the user: a noise burst with a ratchet-like envelope, a steady sine and a pitch-swept sine for the
resonance and the skin, the Kick's flavour of reverb, sine shaping and saturation). `src/synths/snare.cpp`, docs/SYNTHS.md "Snare": skin,
ring, wires (up to 8 bursts, Spacing / Accel / Burst / Decay), Shape / Fold / Sat, the Kick's Room as its Fx; mono, PLAY at 135 BPM.
Host-tested (277 checks with -Filter Snare), the bursts and the sweep measured from the WAV; host 23.7 ns per sample, ESP32 render 669
instructions with no call in its loop. My choices to confirm: mono (a choke, as a drum machine), Seed 0 = free noise, no macros or
master shaper yet. Not heard by the user yet.
The user then confirmed mono and Seed 0 (macros later) and asked for a 3-band EQ on the noise instead of its filters, a 3-band EQ in
the shaping chain and a gate after the reverb: done (`lab::Eq3`, new in `lab_dsp.h`; the gate in the Fx, keyed Dry or Out), measured
from the WAV, 1286 checks pass. Then: the Kick's master chain after the gate, and the noise EQ's shelves made a high pass and a
low pass (I read "the low and high band to a low pass and high pass" as low band -> high pass, high band -> low pass: to confirm).
1326 checks pass. Not committed.

**Then (2026-11-13): DrumLab** (the user: "a drumlab page with kick and snare parts mixed together", then, asked: the modules
merged into one long chain for every percussion, all four sources with a new metal (Squares and FM), the rumble as a module, the keys as
notes, preset grooves on PLAY, its own values, the row limit raised to 128). `src/synths/drumlab.cpp`, docs/SYNTHS.md "DrumLab";
`lab::Options::grooves` (lab.h) played by synth.cpp through the engine's sequencer (no framework change). Its defaults measured
equal to the Kick, the Snare's values on its rows within 2 dB of the Snare; the grooves tested through the engine. 2217 checks pass.
Not heard by the user; not committed. **Memory to watch:** its voice is 7.7 KB, 6 of them + its Fx about 59 KB of the 64 KB fast
heap (it spills into PSRAM beyond): one voice for a mono synth would fix it. Proposed next: sound presets on a row, macros.

**Open:**
- [ ] **The user tries SynthLab and the Kick** (`.\build.ps1 -App synthlab`, `.\run.ps1 -App synthlab`; encoder B on the first row to
  "Kick", then PLAY or the keys) and says what to change, in the sound and in the screen. One reading to confirm: their "user
  parametrable env" became the amplitude envelope.
- [x] **The first real synth:** the Kick (2026-11-12).
- [x] **Measured on the board** (2026-11-15, HWV1, the bench `hwv1_bench`: README "The modules' bench"): every synth in the engine
  (docs/SYNTHS.md "Measured on the board") and every modular module (`framework/modular/module_costs.h`). Found: a synth's code from
  flash cost up to 2.5 times its cycles (the 16 KB cache the screen's code shares); now `SC_HOT` (lab.h, modules.h) with
  `-mtext-section-literals` (platformio.ini): the Kick 86 500 -> 44 100 a block with one note. Regular hwv1 and hwv2 builds pass; the
  regular hwv1 build is on the board (not heard since). HWV2 not measured (no codec yet; the bench does not need one).
- [ ] What still runs from flash each block: the C library's expf / powf / tanf (ADSR, Comp3, Glue, Drive, Slew, Svf::set ...): cold,
  1 000 to 50 000 cycles a block (module_costs.h `cold`). Faster forms (fast_exp2, tables) would change the sound slightly: the user's call.
- [ ] The engine at 1 note costs more than at the next notes (Basic: the first voice +20 000, the others +10 500): the first voice of a
  block finds the code cold. Not looked into further.
- [ ] FM2 still calls `expf` 4 times a block (its envelopes' `set`): fine, but the Kick's `update()` pattern would remove them.
- [ ] Possibly later: save the values to the card; a scope or spectrum view; the seven knobs on the first seven values.
