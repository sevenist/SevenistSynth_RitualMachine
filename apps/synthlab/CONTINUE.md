# SynthLab: handoff notes

The app's state and todo. Its synths' designs are in [docs/SYNTHS.md](docs/SYNTHS.md), its controls in [README.md](README.md). The
library's notes, the user's working style and the traps of the environment are in the root [CONTINUE.md](../../CONTINUE.md).

## Session todo (updated 2026-11-12)

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
3.9 KB (x 6 voices) and its reverb 12.8 KB: about 36 KB of the 64 KB fast heap (main_esp32.cpp; it spills to PSRAM beyond).

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
engine makes 6 voices for every synth, even a mono one: 43 KB + the Fx's 12.9 KB = 56 KB of the 64 KB fast heap (main_esp32.cpp;
beyond it the heap spills into PSRAM). If that becomes a problem: load the graph with 1 voice for a mono synth (Engine::load's
nvoices, at a change of synth).

**Then: the optimisation the user asked for, "starting with reverb and delay, at half the sample rate"**: they were already at an
eighth (6 kHz) in the rumble, and the output's reverb at half; the whole rumble chain now runs at an eighth instead (`rumble_tick`),
Kick 51.8 -> 40.6 ns per sample on the host. Its sound measured against the full rate (`kDecim = 1`, Hann-windowed). 695 checks pass.
Still to propose: the output's reverb at a quarter rate (it processes the whole kick: it would lose highs in its tail); one voice for
a mono synth (memory: 56 of 64 KB). Measuring traps met: a spectrum without a window (leakage looks like highs); a file copied back
keeps its old date and the build does not recompile it (touch it).

**Then: Kick Lite** (the user: clone the kick, the fewest values, optimise as far as it cannot be heard, presets compared under a null
test). 8 values, 6 presets (`kick_lite.h`), `tests/test_null.cpp` (60 dB required). Stage 0 bit-exact, stage 1 (the fixed values'
dead code) bit-exact, stage 2 kept O1 (the distortions as a table, 2048 steps over +-2) and O4 (the dead triangle skipped): voice
38.6 -> 28.9 ns, nulls 75 to 97 dB; O2, O3, O5, O6, O7 measured and rejected (docs/SYNTHS.md, "Kick Lite"). 811 checks pass.

**Then: the rumble's filter** (the user, before optimising more: a filter envelope for the rumble, and a filter selection with every
filter there is). Filter (LP 6 / 12 / 24, BP, HP 12 / 24, Notch, Peak), Env Amt (+-5 oct), Env Atk, Env Dec: the Kick at 72 rows;
the defaults bit for bit as before (the null test unchanged). `lab_dsp.h`: `Svf::set_g`, `fast_tan`. 851 checks pass. Next: the
user's choice among the optimisations proposed (measure on the board, IRAM, render once).

**Open:**
- [ ] **The user tries SynthLab and the Kick** (`.\build.ps1 -App synthlab`, `.\run.ps1 -App synthlab`; encoder B on the first row to
  "Kick", then PLAY or the keys) and says what to change, in the sound and in the screen. One reading to confirm: their "user
  parametrable env" became the amplitude envelope.
- [x] **The first real synth:** the Kick (2026-11-12).
- [ ] **Flash it on HWV2** (VS Code, folder `synthlab`, env `hwv2`), then measure the cycles per voice of each synth on the board (a
  cycle counter around LabVoice / LabFx's process, printed once a second, would give the real numbers: not built yet).
- [ ] FM2 still calls `expf` 4 times a block (its envelopes' `set`): fine, but the Kick's `update()` pattern would remove them.
- [ ] Possibly later: save the values to the card; a scope or spectrum view; the seven knobs on the first seven values.
