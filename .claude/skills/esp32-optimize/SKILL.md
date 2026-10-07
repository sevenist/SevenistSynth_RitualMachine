---
name: esp32-optimize
description: Interactive CPU/memory optimization of the SynthCore engine on the real ESP32-S3 prototype. Use when the user asks to optimize, profile or reduce the cost of a module, an effect, the voice count, or to find out why the audio overruns on the board. Drives the measure -> locate -> change -> test -> user flashes -> re-measure loop with tools/serial_test.py.
---

# ESP32-S3 interactive optimization

This is the workflow that took the default synth from "1 voice with reverb and delay" to "4+ voices of a 4-engine patch" on the prototype.
It works because every change is **measured on the board**, never guessed. Follow it in order.

## 0. Read first (mandatory, before touching code)

1. `CONTINUE.md` : state, the user's working style, **traps**, and the latest measurements + next steps.
2. `DEVELOPING.md` : section "ESP32 firmware and performance work" (flags, tools, how to read the logs).
3. `ENGINE_DESIGN.md` : ADR-035 (what was measured, what was decided, the cost table of the S3). Do not redo a measured experiment; do not re-litigate a decision the user already took.

If you change anything the docs describe, update them before you finish (see section 8).

## 1. Standing rules of this project

- **Flashing: ask first, then it is yours.** By default the user flashes (you build, they flash, you measure). When the user has said you may flash for the task ("you can upload automatically", as in the sampler session of 2026-10), run the automatic loop of section 2b. The permission is for that task, not for later ones. Never flash for a change you have not host-tested.
- **Quality trades are the user's decision**: give options with pros and cons (what changes in the sound, estimated saving) and wait. Exact optimizations (same sound, fewer cycles) you just do.
  The user has already decided: never go below 44100 Hz sample rate; they accept an FPU exception to "no float" (ADR-035) and prefer measured results over estimates.
- Hardware pin/board edits live inside `#ifdef HWV1`. Dev-only features sit behind flags (`DEV_*`, `HWV1_DEBUG_*`, `ENGINE_PROFILE`) that must stay removable for release.
- **Report what was verified and what was not.** Say plainly "built, host tests pass, not flashed" until a board measurement exists. Give estimates as estimates.
- Answers are concise; numbers go in small tables (before / now).

## 2. Measure (the board must run the build you think it runs)

Preconditions: firmware built with `-DDEV_SERIAL_CMD -DHWV1_DEBUG_AUDIO -DENGINE_PROFILE` (the default `platformio.ini` has them), the user's serial monitor closed (one program per COM port), board on COM8.

```
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py                       # idle, then 1, 3 and 6 held notes: render time, cycles per engine block, per-module table
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py --chords 1,3,6 --hold 5 --log run.txt
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py --engines "0,1,4,6;2,3,5,7" --chords 2     # cost of each oscillator engine (switched live)
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py --raw --seconds 3     # just read the board (also a safe way to see which firmware runs)
```

Budget: 160000 cycles per 32-frame block at 240 MHz / 48 kHz (`[PROF] ... budget`). The render line says `blocks over budget`: any > 0 is an audible dropout.

**Check the build is the one flashed** before believing a comparison: the `[HEAP]` / `[OSC]` line formats change when you add instrumentation, and per-engine numbers that are identical to the previous run mean the old binary is still running.
This was wrong once in this project (the user said "flashed" about a build that was not). Ask, or compare a number that must change.

Delete the `run*.txt` logs when done (they are not versioned); put the numbers in CONTINUE.md instead.

## 2b. The automatic loop (only when the user authorised flashing)

One round = **hypothesis -> change -> host test -> build + flash -> set the test synth -> measure -> compare -> (ears?) -> next**. Keep each round to one change so a number can be blamed on it.

```
# build + flash (log to a file: NEVER pipe pio into Select-Object -First, it kills the upload and leaves half an image; a busy COM8 is usually transient, retry once)
C:\.platformio\penv\Scripts\pio.exe run -t upload *> $env:TEMP\pio_upload.log ; $LASTEXITCODE
# experiments without editing platformio.ini: flags are appended
$env:PLATFORMIO_BUILD_FLAGS = "-DHWV1_SD_BENCH"   ... run ... ; Remove-Item Env:\PLATFORMIO_BUILD_FLAGS
# set the synth under test over serial, then measure it (the patch lives in RAM until the next reset)
C:\.platformio\penv\Scripts\python.exe tools\serial_test.py --cmd "patch sampler 3 2" --chords 1,3,6,8 --hold 6 --log run.txt
```
- Dev serial commands (`DEV_SERIAL_CMD`): `patch startup`, `patch sampler F L` (catalog index F, loop mode L: 0 file 1 off 2 fwd 3 ping-pong; one sampler into one filter, effects off), `voices N`, `samples` (catalog), plus the older `on/off/chord/eng/release/status`.
  Add a `patch` for any other test synth you need (`rack_init_*` in `core/rack.c`): the loop is only as good as its repeatable patches.
- The board reboots on flash (RTS): to see the **boot log** start `serial_test.py --raw --seconds 25` right after the upload returns. `--raw` prints only lines newer than the `--cmd` pauses, so replies to `--cmd` do not show there: use the normal mode, or read `run.txt`.
- The report prints CPU (`cycles per engine block`, `blocks over budget`) and the card (`reads/s`, KB/s, average read, `sampler underruns`) side by side per phase: **over budget with no underruns = CPU, underruns with no over budget = card.** They cascade: an underrun sends a voice to the slow path, which costs more CPU.
- Task watchdog / crash backtraces: decode with `C:\.platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32s3-elf-addr2line.exe -pfiaC -e .pio\build\waveshare_esp32s3_pico\firmware.elf <addresses>` (the ELF must be the flashed build). Rules the watchdog taught: a task that busy-waits (the SD driver, loops over card reads) must give the CPU away (`vTaskDelay`) at least every few ms, and failing mount attempts included.
- **When to pause for human ears: after any change that can alter the sound** (not after an exact optimization proven by an identical checksum / SNR test), after fixing something that was reported as audible (loops, glitches), and before the loop moves on to a quality trade. Say exactly what to play and listen for (which patch, which notes, how many voices), then ask with `AskUserQuestion` using concrete options ("clean / click at the loop point / crackle with many notes / other") and wait. Do not stack further sound changes on an unlistened one.
- Bit-exactness check for a hot-path rewrite: a host test that prints a checksum of a long render, run in the normal build and with a `-DSC_..._NO_FAST` switch that keeps the old path; the two numbers must match (done for the Sampler fast path).
- Stop the loop and report when two rounds in a row move nothing, when a number gets worse for a reason you cannot explain, or when the next step is a quality trade (options with pros and cons, the user decides).

## 3. Locate: from the total to the instruction

Go from coarse to fine, one level at a time:

| Level | Tool | Gives |
|---|---|---|
| module | `[PROF]` line (cycles per block, summed over voices, `xN` = instances) | which module type dominates; divide by voices for per-voice cost |
| stage inside a module | `SEC_BEGIN()/SEC_MARK(n)` (`fx_modules.h`), printed as `[SEC]`; `g_osc_prof[]` per oscillator engine, printed as `[OSC]` | where inside Reverb / Delay / OscEngines the cycles go. Add marks for what you investigate, remove or keep them under `ENGINE_PROFILE` |
| memory | `[HEAP]` line: fast heap used / spilled bytes | whether module state fell into PSRAM (see lesson 7) |
| primitive | `bench_esp32.cpp` (`HWV1_BENCH`, printed at boot as `[BENCH]`) | cycles of one operation or one candidate formulation, RAM vs PSRAM. Add a line for any new idea BEFORE implementing it |
| instruction | `xtensa-esp32s3-elf-objdump -d -C --start-address ... firmware.elf` (path under `C:/.platformio/packages/toolchain-xtensa-esp-elf/bin/`), `nm -S -C` for sizes | what the compiler produced: calls to libgcc (`__divdi3`, `callx8`), 64-bit shifts, reloads. The core is in-order and single-issue: **cycles ~ instructions + latency stalls** |

Per-sample cost = cycles per block / 32. Per-voice cost = module cost / voices.

## 4. Facts about this chip (measured, cycles; full table in ADR-035)

int32 mul + shift 3 | 32x32->64 widening >>16: 13 | 64x64: 24 | high-word multiply (`mulh`): 5 | int->float 4, float add 4, float multiply-add 6, float->int 8 |
**float divide 68, bit cast float<->int via memcpy 62, 64-bit divide = library call (100+)** | table read from RAM 5, from flash on a cache miss ~120 |
Hermite read 64 (RAM) / 70 (PSRAM), linear read 18 | PSRAM sequential access is only ~2x RAM; random access in a big table is not.

Consequences that held up:
- The FPU wins on a **chain of multiply-adds on state** (the filter tick: 202 -> 25 cycles) and loses on conversions, divides, bit tricks and indexed memory work. Measure before moving something to float.
- Dependency **latency** dominates: a long chain of FPU ops costs 4-6 cycles each even when few instructions. Batch independent work, divide once for several sections.
- Kill 64-bit math in the audio path: use int32 with `mulh()`/`shl_sat()` (`dsp/q.h`) where the value range allows.
- Anything per-sample that is constant per block belongs in a **per-block setup, cached on its inputs** (`OscEngines::prepare` key cache).

## 5. Change: smallest exact optimization first

Order of preference: (a) same output, fewer instructions; (b) same sound within a measured tolerance (state the tolerance); (c) a quality trade behind a build flag, **only after the user chose it**.
Keep the old behaviour reachable by a flag when a trade is made (`ENGINE_FILTER_EXACT`, `ENGINE_REVERB_HALF`, `ENGINE_FX_MONO`) so the user can A/B by ear and by cycles.

Never change the **shape of the graph** from a knob (adding/removing a node or cable, or making a cable exactly unity): that is a full rebuild = a 100-170 ms audio stall. Use live commands (`SetDepth`) and always-present nodes (lesson 8).

## 6. Test on the host before the board

```
powershell -NoProfile -File tools/build_engine_tests.ps1                      # default configuration, ~1 min
powershell -NoProfile -File tools/build_engine_tests.ps1 -Defs "-DENGINE_REVERB_HALF=1"   # a flag variant
powershell -NoProfile -File tools/build_engine_tests.ps1 -Matrix               # six rate/block configurations, before a milestone
```

- Every optimization of numeric code gets a test that compares it to an independent reference (examples: `additive_recurrence_stays_on_its_harmonics` < -60 dB residual, `modal_fundamental_decays_with_its_designed_time_constant`, `svf_batch_float_coefficients_match_the_integer_ones`, `changing_a_modulation_depth_does_not_rebuild_the_graph`). A passing old test is not proof of accuracy.
- If a test fails after an optimization, find out whether the test or the change is wrong (the analysis window of a test once leaked energy and looked like an engine bug).
- Build the firmware (`/c/.platformio/penv/Scripts/pio.exe run`) and the simulator (`powershell -File build.ps1`; it writes `oled_sim_new.exe` if the sim is running). A single link failure with no error text once happened; run it again before investigating.

## 7. Hand over to the user

Say in this order: what was found (measured numbers), what changed (and what it costs in sound, if anything), what was verified (host tests, builds) and what was not (board), then the one thing you need: "flash and tell me".
After they say flashed: rerun section 2, compare with a before/now table, say if the estimate was right.

## 8. Before you finish a session

- Update `CONTINUE.md` (state, the newest measurements, next steps, new traps) and the parts of `DEVELOPING.md` / `ENGINE_DESIGN.md` (ADR-035, budget table) that you changed. Remove stale statements ("nothing built on hardware" was left wrong for a long time).
- Clean up scratch files (`run*.txt`), keep dev flags documented in `platformio.ini`, and note which flags must go before a release.
- Do not commit unless asked.

## Lessons from the first optimization campaign (examples to imitate)

1. **Check the compiler flags first.** The Arduino framework appends `-Os` after the project's `-O2`; everything was compiled for size. `build_src_flags = -O2` fixed inlining. Found by reading `compile_commands.json`, not by guessing.
2. **Bypass what is off.** Chorus mode off, Delay mix 0, Reverb mix 0 still ran their loops: idle cost fell by 40k cycles with a copy path (state cleared once).
3. **Measure the primitive, then choose the formulation.** The float reciprocal with a table and bit tricks looked smart and cost 232 cycles; the benchmark line showed bit casts cost 62. The winning filter change was not computing coefficients per sample at all when the modulation is smooth (interpolated across the block, with an exact fallback), 11k -> 4k per voice.
4. **A cost that is the same for every variant is outside the variant.** Removing 2 of 7 saws saved 8%, not 28%: a profile of "inside the switch vs the whole function" showed ~58 cycles/sample of loop overhead around every engine; a fast path for a steady gate/pitch removed it.
5. **Exact algebra beats approximation when available**: additive with the Chebyshev recurrence (two table reads instead of twelve), proven by a residual test; modal as normalised decaying phasors, proven by a decay-time test. Normalize anything recursive that has a feedback gain close to 1.
6. **Count cycles in instructions** with `objdump` when numbers do not add up (a 1100-instruction reverb sample explained the cost).
7. **Memory placement is performance.** The fast heap (internal RAM, ~106 KB) was full and later allocations silently spilled to PSRAM: a 2-input mixer cost 88 cycles/sample instead of ~20, the reverb ran 12% slower. 32 oscillator instances each owned 4 KB of string buffer and 1.3 KB of identical tables. Fix: share constant tables, put sequential buffers in the bulk heap, and print `spilled` bytes.
8. **Parameter changes must not rebuild the graph.** A knob that crossed 0.00 or the maximum added/removed a node or an aliased cable and stalled the audio 170 ms. A `[AUDIO] ... graph builds N (last: reason)` counter found it in one run; a host test (`engine_synth_build_count()`) keeps it fixed.
9. **Halving the work structurally beats polishing**: the reverb tank at half rate (ENGINE_REVERB_HALF) halved its cost and memory; mono FX halved the delay. These change the sound, so they were offered as options and decided by the user.

10. **Instrument the layer below before blaming the card.** Glitchy sampler -> the first suspects (loop logic, card quality, priorities) were all wrong. Timing a raw sector read (below FATFS), then a bare CMD13, then watching CS / MISO from the other core
    showed 40 ms of silence BEFORE every command and a 55 us transfer at the end: ESP-IDF's sdspi host on this board. A bare SPI transfer on the same bus was fast, so a small own SPI-mode driver (`sd_card_spi.cpp`) fixed it: 82 ms -> 1.4 ms per sector. Order of tools: per-operation timing, then an A/B of the two driver layers, then pin-level tracing.
11. **The hot loop of a streaming module was the general one.** `Sampler` cost 490 cycles per sample because every sample ran bounds, cache and loop-wrap checks; a per-block fast path (steady rate, no wrap, one contiguous run of data) cut it to 5.4k per voice-block, 3x, with a bit-identical render (checksum test). Look for "the common case of a block" before polishing arithmetic.
12. **Two failure modes cascade.** A saturated card -> underruns -> voices fall back to the slow path -> CPU spikes -> more late blocks. Fix the first stage, and read both counters before concluding.

## Environment traps (also in CONTINUE.md)

- **Never bare `python` (or `py`)**: on this machine it is MSYS2 3.12.9 without pyserial (`py` is 3.13). Always `C:\.platformio\penv\Scripts\python.exe` (3.11.7, pyserial), and never bare `pio` either (see the skill `board-session`, section 2).

- **Scripted edits through this harness lose one level of backslashes**: `\\n` written in a Python heredoc reaches the C file as a real newline (the printf then does not compile). Use the Edit tool for any line with `\n`, or build the character with `chr(92)` outside the quoted block. After a scripted printf edit, grep that the format string is one line.
- PowerShell does not expand `*.c` for gcc (compile ui_dump from bash); `Select-Object -First N` on a pipeline closes it and kills the upstream process (an upload!).

- Bash heredocs and Python literals: `\n` inside a C string in a script becomes a real newline, and heredocs with quotes break. Edit with the Edit tool or write a script file with the Write tool, then run it. After any scripted edit of a `printf`, grep that the string is still on one line.
- Many source files are CRLF; scripted edits must preserve the file's line endings (read with `newline=''`, normalise, write back).
- `src/platform/engine/engine_synth.h` is included from C: no default arguments.
- One program per serial port: the user's monitor, the PlatformIO monitor and `serial_test.py` exclude each other. The tool opens with DTR/RTS low so it does not reset the board.
