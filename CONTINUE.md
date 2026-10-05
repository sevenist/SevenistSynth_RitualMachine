# Continue development: handoff notes

Read [DEVELOPING.md](DEVELOPING.md) first (how to work in the code, where to change things, tools, testing), then
[ENGINE_DESIGN.md](ENGINE_DESIGN.md) for the engine's decisions, measurements and limits. This file only holds what they do
not: state, the user's working style, known debt and traps.

## State (end of last session)

- The application runs on **our own DSP engine** (`src/engine`, C++17, q15). The simulator builds in about 5 s; 98 engine tests are green in
  six rate / block configurations. **The user has still not listened to the balance** (levels untuned; they asked to skip that step).
- Done in the last two sessions: OSC-as-modulator fix (ADR-028), motion sequencer (ADR-029), sampler in the app with the SAMPLES browser (ADR-030, wav / mp3
  are converted when assigned), then the sound-design round: **envelope curves + hold + start + the 4-point EG** (ADR-031), **modulation depths in real units** (ADR-032),
  **8 oscillator engines** (ADR-033: Karplus-Strong, modal, FM2, folder, supersaw, vowel, additive, dust), **10 saturation modes, the flexible FX rack (4 slots) and the new effects:
  phaser, flanger, trem / pan, compressor, EQ, ring / shifter, convolver with 8 built-in IRs, tuned resonator (RS module)** (ADR-034). 126 tests in six configurations.
- Done since: modulator **DEST pages** (Tgt / Prm / Dpth on every OSC / LFO / ENV / EG), a **10-slot rack that scrolls**, and the **hardware-aware input**: the HAL reports the
  prototype's physical controls (2 encoders + switches, play, master volume, 4 x 5 matrix keys with 4 column knobs, 3 knobs, joystick + push, 3 buttons), `core/bindings.c`
  is the one table that maps them to actions (Shift = button 3, octave = Shift + joystick up / down, knobs = column rows / macros / volume), the simulator has a second
  **Panel window** (mouse + keyboard), and the screen size is one setting (`DISPLAY_WIDTH/HEIGHT`, style derived from it; tested at 128 x 128 through `ui_dump` and the sim).
  Not tried by the user yet; the ESP32 `input_esp32.cpp` is a skeleton. A developer guide for the binding table is still to write (DEVELOPING.md has the recipes).
- Plan from the user: 3) ESP32 prototype board next week (they bring it up; the I2S audio file, TF driver and display / input are still skeletons,
  `audio_esp32.cpp` has stubs for the sample library), 5) save / load of rack + FM patch later "after some fixes".
- Not tried by a human yet: all the screens added since the OSC fix (MS, SM, SAMPLES, EG, CRV pages, FX RACK, engine previews) and every new sound (only checked with `tools/ui_dump.c` ASCII frames
  and tests). Expect small layout tweaks.

## Decisions that went against my recommendation (recorded with their risks in ENGINE_DESIGN.md)

The user decides and I record the reasoning and move on, without re-litigating:
- **All signals are q15 audio-rate blocks** (ADR-010): simpler, costs CPU for slow modulators. Measure on the S3.
- **Naive saturation** (ADR-013): aliasing on bright, hard-driven signals. The shaper core is a pure function, so ADAA can be
  added later without touching graphs.
- **q15 block-floating-point FFT, N = 512** (ADR-016): measured 67 dB SNR, fine; needs N = 1024 above 48 kHz.
- **Dattorro plate reverb** and **fixed Juno-like chorus modes** (ADR-019 / 020).
- **AMY replaced outright** (ADR-027), which forced the engine's own DX7 voice (ADR-025) and loses the AMY-vs-engine A/B.

## Known debt / small issues

- Sample library: whole files are copied to RAM in the simulator; loaded samples are never unloaded; imports block the UI thread; a rack stores the file *index*
  (names needed when saving). Zones are engine-only (no UI). MS targets are limited to what the mapper realises (OC Pit / Lvl / PW, FL Cut, SA Drv, SM Pit).
- Sound-design round (ADR-031..034), not listened to: the convolver costs 2 x taps MACs per sample (measure on the S3), the compressor / shifter / engines are tested against theory only,
  IRs from the card are possible through the chunk API (`ConvBlob`) but not wired to the library, the engine previews in the OSC graph box are sketches.
- `samples/` holds the generated demo files (`tools/make_demo_samples.py`); `lib/minimp3` is not versioned like the other libs.

- `synth_params_t` and `param_id_t` still contain the old oscillator / filter fields (`P_WAVE`..`P_FENV_R`, `volume`); only the amp
  envelope is used. The graph helpers `draw_env`, `filter_gain` in `ui_graphs.c` are still used by module pages.
- `OSC n` page shows `Lvl`, which is meaningless when the OSC acts as a modulator; modulator
  depths are in the target's unit since ADR-032.
- Rebuilding on menu close no longer cuts held notes (state is kept by node id), but removing the module a note uses silences it.
- Mapper limits (same as before): no modulation of resonance, mix or other modulators; MS has no sound.
- the old `synth_ui.c` is now split (ui_pages / ui_input / ui_graphs / ui_draw, plus ui_screen and scr_*.c: every menu tab is a declarative screen, the main view pages are not yet); split the
  rack / FM / FX drawing into their own files if it grows.
- `tests/engine/test_sampler.cpp` has a `run_trace` helper that prints underruns: handy when a streaming test fails.
- Engine items listed under "Known limits" in ENGINE_DESIGN.md (reverb q15 floor, block-rate envelopes, mono samples, ...).

## Working style and environment traps

- The user iterates with short requests, tests in the SDL window, and wants concise answers that say what was verified vs not.
  Windows 11, PowerShell, MSYS2 UCRT64; AZERTY keyboard; writes in English with some French words. For design work they asked
  to be given options with pros and cons at each stage (AskUserQuestion), then to see it executed and tested.
- The user often keeps the simulator open: `build.ps1` then builds `oled_sim_new.exe`. A stale exe has caused a false "bug" before.
- Bash tool: **heredocs containing apostrophes or quotes break**; `cat > file` with no input hangs. Write multi-line scripts and sources
  with the Write tool, then run them. Inside Python source that you write with a heredoc or a triple-quoted string, `\n` inside a C
  string literal becomes a real newline: edit files with the Edit tool or a script file instead.
- Tool calls that run `g++` through PowerShell hide compiler errors (`NativeCommandError`): compile from bash to see them.
- A test executable needs `C:\msys64\ucrt64\bin` on PATH (exit code 127 / silent otherwise).
- Python on Windows: always `open(..., encoding="utf-8")`.
- The test runner takes a name filter (`engine_tests.exe reverb`); `-Matrix` takes a few minutes, a single configuration about 1 min.

## Starting the next session (compact prompt)

> Read `CONTINUE.md`, `DEVELOPING.md` and `ENGINE_DESIGN.md`. I tried the simulator: <notes on levels / bugs / the MS, SM and SAMPLES screens>.
> Then <fix those>, and next <ESP32 bring-up help / zones in the sampler UI / save and load>.

Swap the last sentence for whichever item you want next.
