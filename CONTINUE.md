# Continue development: handoff notes

Read [DEVELOPING.md](DEVELOPING.md) first (how to work in the code, where to change things, tools, testing, **"ESP32 firmware and performance work"**), then
[ENGINE_DESIGN.md](ENGINE_DESIGN.md) for the engine's decisions, measurements and limits (**ADR-035 = what was learned on the real chip**). This file only holds what they do
not: state, the latest measurements and what to do next, the user's working style, known debt and traps.
For any CPU / memory optimization work on the board, use the project skill `.claude/skills/esp32-optimize/SKILL.md` (the loop that worked: measure with
`tools/serial_test.py`, locate, change, host-test, the user flashes, re-measure).

## State (end of the ESP32 bring-up and optimization session)

- The application runs on **our own DSP engine** (`src/engine`, C++17, q15) on **both** the desktop simulator and the **first hardware prototype** ("HWV1": ESP32-S3-Pico, SH1107 128 x 128 OLED, TCA8418
  keyboard, quadrature knobs behind an analog mux, joystick, I2S DAC). 132 engine tests are green in all six rate / block configurations (`-Matrix`, run at the end of this session); the simulator and the firmware build.
- Working on the board (checked by the user): display, keyboard (map found from their key log), the 7 knobs (quadrature decode with hysteresis) and the joystick, power hold, I2S audio (the test tone and the engine),
  serial debugging, the boot-time benchmark, the serial command channel. The default patch is now `rack_init_startup`: **four oscillators on four different engines (Karplus, Modal, Supersaw, Additive) into one filter, delay 1000 ms / 40 %,
  reverb 40 %**. `rack_init` (osc, filter, saturator, LFO) is the older demo rack that the tests build on.
- **Not verified on the board**: the speaker amplifier (SPK_SD is left floating on purpose, the headphones/DAC path is what was heard; `HWV1_SPK_SD_MODE=1` drives it high), audio glitches while the screen draws (mitigated by IRAM code, never checked
  by ear), "the MCU stalls when no serial port is connected" (a fix was applied: non-blocking serial and `delay(1)` in the idle loop; maybe power related), the LED driver (stub only), the TF card (none), the new prototype's controls.
- **Not listened to by the user yet** (all of these changed the sound slightly and were only verified by tests): the interpolated filter coefficients, the 5-saw supersaw, modal as phasors, the additive recurrence, the half-rate reverb, mono delay / reverb,
  the vowel / dust filters in float, the dust level compensation.
- The user's earlier plans: save / load of racks and FM patches "after some fixes"; the engine / UI work of the previous sessions (ADR-028..034) was never listened to either (levels untuned; they asked to skip that step).

## TF card and sampler on the board (first tests by the user, then a fix round; NOTHING of the fix round is verified on the board)

The user played the cooked demo `.smp` files from the card and reported: loop points misplaced / shifted, only 2-3 voices, the card read "too much".
- **Loop: found and fixed on the host** (ADR-022 addendum). A fixed `block % 8` ring slot made a loop's end and start blocks collide, so the loop start was never prefetched and every lap dropped out. The new test fails without the fix. Ask the user to listen to `pad_c4` again.
- **Card reads, fixed by reasoning, not measured:** the ring and heads live in PSRAM, which the SD host cannot fill by DMA, so the driver (from my memory of the ESP-IDF source) reads one sector per command; `SdStorage` now reads through an internal DMA bounce buffer and skips redundant seeks.
  Also removed: the 64 reads per file the scan did at boot (the overview is stored in the `.smp` header now; old files still work but re-cook them: `.\build.ps1` after putting the sources in `samples_src/`, or `python tools/make_demo_samples.py` for the demos).
- **2-3 voices: cause NOT established.** Two suspects, in this order: (1) CPU, not the card: the default patch costs about 24k cycles per voice (see the table below), 3 notes = 75 % of the budget, so 3-4 voices of that patch is its limit; a sampler rack with a light patch should do much better.
  (2) the card (reads per second and the PSRAM bounce above). To tell them apart flash with `-DHWV1_DEBUG_AUDIO -DENGINE_PROFILE` (currently commented out in platformio.ini) and read: `[AUDIO] ... blocks over budget` (CPU), `[PROF]` with the Sampler line (cost per voice),
  and the new `[SD]` / `[SMP]` line (card KB/s, average read time, `underruns` = dry streams). If `blocks over budget` is 0 while `underruns` rises, it is the card; if it is the other way round, it is the CPU.
- Possible next steps if the card is still the limit (each is a design choice for the user): voices that play the same sample share the blocks they read (today every voice reads its own copy); fast seek in FATFS (an sdkconfig option the Arduino prebuilt libs do not expose);
  a faster SPI clock (`HWV1_SD_FREQ_KHZ`, 20000 now); the planned SDMMC 4-bit slot of the next prototype.
- Task priorities are written down in DEVELOPING.md ("Tasks and priorities"): FreeRTOS was already in place; `sd_io` now runs at 5 (above the UI loop, below input and audio).
- New workflow: `samples_src/` (+ `build.ps1`, `-Sd E:` to copy to the card). Names to know: files with names longer than 23 characters are cut; only `.smp` is read on the board.

## Latest measurements (prototype, startup patch, mono FX + half-rate reverb, budget 160000 cycles per 32-frame block)

Measured with `tools/serial_test.py` on the last build that was on the board (engines still with the generic per-sample loop; the loop fast path below was built after it):

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

## Next performance steps (in the order I would take them)

1. Flash the fast-path build, measure, record the numbers here (and in ADR-035's table). Ask the user to **listen** to the list of unlistened changes above and to say which sound changes they dislike; each has a flag or a one-line revert (see ADR-035).
2. Remaining per-voice costs: Env 2.2k (two per voice: amp env + the filter's own, 47 cycles per sample, still 64-bit-free but not specialised), Lfo 1.8k, Shaper 1.8k, VoiceOut 0.9k. A fast path for "Env idle / sustain" and for constant LFO shapes is likely.
3. Engines: additive 263 (12 weighted adds + 11 recurrence steps; fewer harmonics at high pitch would help but changes the sound), modal 229 (8 modes x ~20), ssaw 237 (5 saws, the rest is mixing), vowel 217 (3 float filters).
   Ask the user before any change that alters sound (they decide quality trades; offer options with pros and cons).
4. Fixed cost: reverb 20.5k (the tank halves ~5k each at half rate; trimming one of the four input allpasses or the second decay diffuser saves 2-3k and changes the character; a cheaper diffusion structure was proposed, not tried),
   delay 8.4k (PSRAM Hermite read + write per sample, 5.2k).
5. Before a release remove the dev flags (list in DEVELOPING.md) and decide `DEV_OUTPUT_GAIN_PCT` (currently 1 %, set by the user while testing at night), `ENGINE_FX_MONO`, `ENGINE_REVERB_HALF`.
6. Open hardware items: LED driver (FastLED push in `leds_esp32.cpp`), speaker amplifier check, display / audio separation check by ear, map the right half of the keyboard and the extra row if wanted, the TF card.

## Decisions that went against my recommendation (recorded with their risks in ENGINE_DESIGN.md)

The user decides and I record the reasoning and move on, without re-litigating:
- **All signals are q15 audio-rate blocks** (ADR-010): simpler, costs CPU for slow modulators.
- **Naive saturation** (ADR-013), **q15 block-floating-point FFT, N = 512** (ADR-016), **Dattorro plate reverb** and **fixed Juno-like chorus modes** (ADR-019 / 020), **AMY replaced outright** (ADR-027).
- On the hardware (ADR-035): a **float exception** for the filters; **mono delay / reverb** and the **half-rate reverb tank** as build flags; the **5-saw supersaw**; the sample rate is **never below 44100 Hz**; modal as phasors and the additive recurrence were done before asking
  because their tests show the same sound within a stated tolerance.

## Known debt / small issues

- Sample library: whole files are copied to RAM in the simulator; loaded samples are never unloaded; imports block the UI thread; a rack stores the file *index*
  (names needed when saving). Zones are engine-only (no UI). MS targets are limited to what the mapper realises (OC Pit / Lvl / PW, FL Cut, SA Drv, SM Pit).
- Sound-design round (ADR-031..034), not listened to: the convolver costs 2 x taps MACs per sample (not measured on the S3), the compressor / shifter / engines are tested against theory only,
  the engine previews in the OSC graph box are sketches.
- `samples/` holds the generated demo files (`tools/make_demo_samples.py`); `lib/minimp3` is not versioned like the other libs.
- `synth_params_t` and `param_id_t` still contain the old oscillator / filter fields; only the amp envelope is used.
- Rebuilding on menu close keeps held notes (state by node id), but removing the module a note uses silences it. Mapper limits: no modulation of resonance, mix or other modulators; MS has no sound.
- `svf_tick` (integer, 202 cycles) and `svf_coef_f` remain in `dsp/svf.h` only as the references the boot benchmark compares against; the engine uses `svf_tick_f`.
- The reverb / delay stage timers (`SEC_BEGIN` / `SEC_MARK`) and the oscillator counters stay in the code under `ENGINE_PROFILE` (zero cost without it).
- `tests/engine/test_sampler.cpp` has a `run_trace` helper that prints underruns. Engine items under "Known limits" in ENGINE_DESIGN.md (reverb q15 floor, block-rate envelopes, mono samples, ...).

## Working style and environment traps

- The user iterates with short requests, tests on the board or in the SDL window, and wants concise answers that say what was verified vs not. Windows 11, PowerShell, MSYS2 UCRT64; AZERTY keyboard; English with some French.
  For design work they want options with pros and cons at each stage; they often pick against my recommendation. **They flash the board themselves** (COM8); never flash. They sometimes say "flashed" about an older build: check (see the skill).
- The user often keeps the simulator open: `build.ps1` then builds `oled_sim_new.exe`. A stale exe has caused a false "bug" before.
- PlatformIO: use `C:\.platformio\penv\Scripts\pio.exe`. Running another Python's `pio` once recreated and destroyed the penv. One link failure with no error text happened twice; the immediate rerun succeeded.
- One program per serial port: close the serial monitor before `serial_test.py`.
- Bash tool: **heredocs containing apostrophes or quotes break; `\n` inside a C string written from a Python literal becomes a real newline** (it hit `printf` lines many times: grep for a string that wrapped, or fix with a regex on `%u[\r\n]+"`).
  Write multi-line scripts with the Write tool and run them; edit with the Edit tool. Many sources are CRLF: scripted edits must keep each file's line endings.
- `src/platform/engine/engine_synth.h` is included from C: no default arguments, no C++ in it.
- Tool calls that run `g++` through PowerShell hide compiler errors (`NativeCommandError`): compile from bash to see them. A test executable needs `C:\msys64\ucrt64\bin` on PATH.
- Python on Windows: always `open(..., encoding="utf-8")`. The test runner takes a name filter (`engine_tests.exe reverb`) and, via the script, engine flags: `build_engine_tests.ps1 -Defs "-DENGINE_REVERB_HALF=1"`; `-Matrix` takes a few minutes, one configuration about 1 min.
- Do not commit unless asked. Delete `run*.txt` serial captures when done (ignored by git now); keep the numbers in this file.

## Starting the next session (compact prompt)

> Read `CONTINUE.md`, `DEVELOPING.md` and `ENGINE_DESIGN.md` (ADR-035), and the skill `esp32-optimize`. The board is connected on COM8 with the last build flashed. <what I heard / what to change>.
> Then re-measure with `tools/serial_test.py` and continue with "Next performance steps".
