# Continue development: handoff notes (the framework and the repo)

Each part of the repo keeps its own notes: this file holds **the library's** (the engine, the boards, the HAL, the drawing kit),
the repo's, **the user's working style and the environment's traps**; each app has its own `CONTINUE.md`:
[the Ritual Machine](apps/ritual_machine/CONTINUE.md), [TechnoGen](apps/technogen/CONTINUE.md), [SynthLab](apps/synthlab/CONTINUE.md).

Read [DEVELOPING.md](DEVELOPING.md) first (the repo and where each part's documents are), then [framework/README.md](framework/README.md),
[framework/docs/BOARDS.md](framework/docs/BOARDS.md) (**"ESP32 firmware and performance work"**) and
[framework/docs/DESIGN.md](framework/docs/DESIGN.md) for the engine's decisions, measurements and limits (**ADR-035 = what was learned on
the real chip**), and the app you work on. These notes only hold what those do not: state, the latest measurements and what to do
next, the user's working style, known debt and traps.
For any CPU / memory optimization work on the board, use the project skill `.claude/skills/esp32-optimize/SKILL.md` (the loop that worked: measure with
`apps/ritual_machine/tools/serial_test.py`, locate, change, host-test, the user flashes, re-measure).

## Session todo (updated 2026-11-10)

**Since (2026-11-09 / 10):** the user committed the HWV2 card reader in the `hwv2` build (`63e034d`), removed `apps/HWV2_scanner` (the
CH32V006's firmware is no longer in this repo; the frame it sends is described in `framework/boards/hwv2/hwv2_layout.h`) and added the
guide `BUILDING.md` (building any app's desktop program) and the project skill `improve-codebase-architecture`. **On 2026-11-10 the
docs were split by part** (the user's choices: each part's documents in its own folder, the ADRs by owner, an update log and a todo
per app): `framework/README.md` and `framework/docs/` (DESIGN.md with the index of every ADR, BOARDS.md, DRAWING_KIT.md),
`framework/CHANGELOG.md`; each app's `README.md`, `CHANGELOG.md`, `CONTINUE.md` and `docs/`; at the root only what the repo shares
(README, DEVELOPING, BUILDING, this file, GLOSSARY, THIRD_PARTY_NOTICES). `ENGINE_DESIGN.md`, `CHANGELOG.md` and `docs/guides/` are gone.

**Where things stand (2026-11-08):** the user brought up the second prototype (GRV-mini, "HWV2") and, at their request, **the firmware was
ported to it in the board layer only** (`framework/boards/esp32` for what the boards share, `framework/boards/hwv2`; outside it: `map_hwv2.h`, a line in
`core/controls.c`, the include paths of the app's ESP32 files, the card reader and serial commands made board-agnostic, the UI tests at 160 px). Flashed and tried by the user, one phase at a time: the screen and LEDs, the controls ("the
firmware works"; both joystick axes inverted since), the power switch and battery ("tested and working"), then the shared soft power and the
LEDs dark at power-off ("tested and working"). The bring-up app `apps/HWV2_tests` was deleted at their word; `apps/HWV2_scanner` (the
CH32V006's firmware) stayed, until the user removed it on 2026-11-10. Committed on `gui-reorg` (the code, then the docs). **Open:**
- [ ] **The SD card and the card reader on HWV2** (`[env:hwv2_card_reader]`, SDMMC 4-bit): written, not reported on by the user.
- [ ] **The VU meter** (LEDs 5..10, the engine's peak): written as a placeholder, not reported on; tune its levels (`hwv2_layout.h`) once there is sound.
- [ ] **The codec** (ES8388) when the parts arrive: its I2C setup in the board layer; then the CPU numbers on this board.
- [ ] **The joystick push also acts as Shift** on the user's board: a fault between U2's pins 4 and 5 (B3 / JOYS). They repair the board or wait for rev B.
- [ ] **HWV1 not tried since its input and power code moved to the shared files** (the user: "untested on hwv1"). Flash the default environment and try the keys, pots, switch-off.
- [ ] **Once a battery stays in HWV2:** the card reader is on in the `hwv2` build since `63e034d` (`HWV2_USB_STORAGE=1`); check `HW_BATT_FULL_SCALE_MV` (computed, 11065) with `power` against a meter.

**Where things stood before it (2026-10-13): the repo is a library and its apps** (framework/docs/DESIGN.md ADR-043; the layout and the commands:
the top of DEVELOPING.md). The user asked that developers can start a new project easily. **Step 1** (the engine as the library,
the Ritual Machine and a starter template as apps) is committed by the user on `main` (`ffaa378` the renames, `a07f37e` the
contents; the first commit alone does not build: `git commit` without `git add`). **Step 2a** (the HAL, the two boards and the
drawing kit into `framework/`) was built the same day and **committed by the user (`d6f929a`)**. **Step 2b** (a control table
per board, the Ritual Machine's roles with a map per board) was built right after and **committed by the user (`4234482`,
working tree clean)**; their words at the next session start: "committed and builds fine on prototype board, app tested
too" (no fault reported; which points of the two try lists they went through was not said; nothing was flashed by me).
The commit also holds the two things that were not Claude's: `apps/ritual_machine/.gitignore` (PlatformIO's) and the line
gone from `apps/template/src/main_esp32.cpp` (the `board_card_reader()` call at start). **Step 2c** (the simulator's panel
drawn from a board's table) was built on 2026-10-13 at the user's "go for step 2c" and **committed by the user (`0ab1696`,
the two new files included)**; they did not say whether they tried it. With it step 2 of the split is complete. At their
request an update log for developers was written after the commit: `framework/CHANGELOG.md` at the repo root (committed since: `85f3ae4`). A bare `samples_src/`, `sdcard/`,
`tools/` or `platformio.ini` in older notes is inside `apps/ritual_machine/`. Two glossaries were made that day as documents for
the user (features and GUI; framework): ask for their links, they are not in the repo.

## Convolver on the board (2026-10-09; flashed and measured by Claude, NOT listened to)

Cycles per 32-frame engine block (budget 174k at 44.1 kHz), the Convolver alone, startup patch with only a Cab in row M:

| Len | before | after (PIE + aligned taps + advancing history) |
|---|---|---|
| 64 | 52.1k | 7.9k |
| 128 | 99.7k (57 %) | 10.0k |
| 256 | ~195k (112 %, 400-570 DMA underruns / s) | 14.2k |
| 512 | (not run) | 22.3k |

- What it was: 25 cycles per tap per frame (wrap mask + 64-bit sums); the IR regenerated in double on the audio thread at every IR / Length step.
- What changed (exact, details ADR-034 addendum): `pie_conv2_s16` (fft_s3.S), 8 shifted reversed tap copies (aligned loads), history written at an
  advancing position (a move per block cost ~30k cycles: the buffers are in PSRAM), IR computed in engine_synth (`conv_builtin_ir`) and sent as
  `ConvBlob` chunks; serial `pieconv` checks PIE against C (0 of 768 sums differ). ~14 KB per instance (was 5 KB).
- Not measured: the switch cost when IR / Length change (~10k cycles once, estimated).
- `[RAM]` lines (main_esp32.cpp, `HWV1_DEBUG_AUDIO`): free internal RAM and its largest block before each init step.

## FFT, STFT and the Spectral FX on the ESP32-S3 (2026-10-07; board session: flashed and measured by Claude; listened to by the user: Thru clean, Pitch / Freeze OK; Gate / Robot / Whisper not reported)

Since ADR-041 (2026-10-09) the FX rack, its tab and `fx_cog` are gone: Spectral is the rack module `MOD_SPECTRAL` ("SP") in row M or a branch.

- The C FFT measured 173-205k cycles per 512 transform on the board: an STFT hop needed more than a core, so SpectralFx / Vocoder could never have run there.
  User choice (asked with options; I recommended ESP-DSP float + a real-FFT trick): **own PIE block-float kernel**. Details and all measured candidates: framework/docs/DESIGN.md ADR-016 addendum.
- PIE kernels in `dsp/fft_s3.S` (S3 only, `SC_FFT_PIE`; every other target keeps the C code, which the host tests run): FFT stages + pack + min / max, the STFT window
  (`pie_vmul_s16`), the overlap-add (`pie_ola_shl / _shr`, bit-identical to C, checked by the boot bench), the CORDIC of the phase vocoder (`pie_cordic`, bit-identical).
  `ee.vmul.s16` truncates and wraps (measured). Exact C speed-ups too: FFT scan folded (test `fft_is_bit_identical_to_the_reference`), Stft overlap-add in 32 bits.
  A fully unrolled scalar CORDIC measured SLOWER (19.6k -> 26k per block: code from flash); reverted.
- Board: FFT 16.0k cycles (was 173k), SNR 63 / 64 / 63 dB loud / quiet / very quiet (C: 67 / 60 / 37); Stft frame 63.6k -> 40.4k; Stft mono 333 cycles per sample.
- **FX rack type Spectral** (`FX_SPECTRAL`, "SP", appended): rows Mode / Shft / Amt / Mix, behind a **cog** (new on the FX RACK tab, `ui->fx_cog`): Hold / Lo / Hi
  (user: all 7 parameters, the rest behind the cog; `FXR_PARAMS` 4 -> 8, `FXR_ROWS` 4; the rack is not saved, no file format changed). Engine: SpectralFx `stereo` = 1:
  STFT on (L + R) / 2, each channel keeps its own delayed dry (user choice "mono sum, dry stereo"); test `spectral_stereo_keeps_each_dry_channel_and_shares_the_wet`.
  Serial: `fx K T [values]` (apps/ritual_machine/docs/SERIAL_COMMANDS.md).
- Board, startup patch + Spectral in slot 4, 3 notes (budget 174k per 64 frames): empty 65k; Thru 88k; Freeze (Hold) 104k; **Pitch +7 114k (66 %), 0 DMA underruns**
  (SpectralFx 43k per block: STFT ~18k, analysis 8.5k (CORDIC was 19.6k), resynthesis 10.9k). Pitch shows ~190 renders over budget but no real dropout: the frame
  lands on every other 64-frame write and the 6 x 64 DMA ring absorbs it. New `[AUDIO] DMA underruns` (HWV1_DEBUG_AUDIO) + column in `serial_test.py`: validated
  (0 with one shifter, 260-340 per second with four, ~10 per graph rebuild). User had also chosen fewer CORDIC iterations, block 128 and a frame split: not done,
  no longer needed (the CORDIC is 8.5k now; block 128 / split only add latency). Say so if they still want them.
- Next: Gate / Robot / Whisper not reported by the user yet; then maybe the Vocoder (needs a modulator source) and more spectral effects.
  Remaining STFT cost: the FFT itself (2 x 16k per frame), resynthesis 21k per frame (sine lookups), bit-reversal unpack 4.7k per transform.
- Traps: a flag passed by `PLATFORMIO_BUILD_FLAGS` is lost when the user uploads (put it in `platformio.ini` for that flash); the boot bench must time the fastest
  run (preemption outliers of 1M cycles); scripted edits lose one backslash level (`\n` in printf, again twice).

## Filters, UI latency, voice bus (2026-10-06; flashed and measured, NOT listened to; decisions in framework/docs/DESIGN.md ADR-038)

- UI: `display_send()` replaces `u8g2_SendBuffer` (HAL); on the board a `display` task (core 0, prio 1) sends only changed tiles; `app_step` puts the redraw off while input is
  queued. Measured with `HWV1_DEBUG_UI` while playing: slowest UI step ~120 ms -> 4-16 ms, up to 92 events and 69 synth updates per second (was ~8). Screen checked by the user
  after the column-wrap fix (the panel's column offset 96 must wrap: a run at tile x is sent with the offset moved).
- Note-on overruns: `OscEngines::reset()` cleared the 4 KB Karplus buffer in PSRAM for every oscillator; rapid notes now 0 over budget (startup worst 2.3 -> 0.92 ms).
- Cutoff: ~20 ms glide at any block size, coefficients interpolated across the block. New FL types LP6 / Ladr / ChLP (`algo` of the Filter module, live switch), measured
  per 4 voices: LP 18.0k, LP24 24.7k, LP6 7.8k, ChLP 6.4k, Ladr 15.9k. Serial `flt T`. Host test `light_filters_lp6_ladder_and_chamberlin`.
- Not done: listening (all of it); the Ladr tuning at high cutoffs is approximate (not zero-delay); the light types have no exact per-sample path for audio-rate cutoff FM.

## Mixer polish (2026-10-06; host-tested in the 6 configurations, flashed and measured, NOT listened to)

- Mapper (`rack_graph.cpp`): the running signal is a list of sources (`RunSrc`); the next module gets one cable per source and the plan's MIX step sums them
  (32-bit, one saturation). A modulating oscillator joins through a cable whose depth is `Lvl` x 1/sqrt n (`qd()`, never unity: live `SetDepth`), Mute = depth 0
  (skipped by the MIX step). More than 8 sources: the first eight fold into one pass-through Mix4. `Engine::set_edge_depth` now checks the pending plan first
  (a depth edit right after a load no longer forces a rebuild). VoiceOut: one product for both buses when centred (exact; saved only ~75 cycles per voice).
- Tests `test_rack_mix.cpp`: no Mix4 nodes for 8 sources, the fold at 9, the mix equals the sum of the sources alone (<= 8 LSB), level / mute edits stay live.
  The sound changes only by rounding (the old Mix4 multiplied by 32767/32768 per stage and saturated per stage): no listening round needed unless a mix clipped before.
- Board (44.1 kHz, 64 frames, budget 174149): Para 8 osc x 4 voices, 4 keys 82.6k -> 66.1k cycles (Mix4 was 16.7k); Para 4 osc x 8 voices, 8 keys 100.5k -> 71.5k;
  startup patch (Mono) 1 key 63.5k -> 61.5k. Biggest remaining voice cost: the oscillators (Strng ~1.5k per oscillator per voice), then VoiceOut ~790, NoteIn ~310 per voice.

## Mod Para, the rack oscillator's quality and the Strng engine (2026-10-06; flashed and measured; Mod Para listened to by the user: OK, legato and glide OK)

Since ADR-041 (2026-10-09) the type "Mod Para" is gone: Para is a switch on a rack module, PEnv a row of that module; the engine parts below remain.

Decisions in framework/docs/DESIGN.md (ADR-036 stage 2 "as built", ADR-037 decision 5). The user flashed-and-test permission covers this task (COM8).
- Mod Para: GENERAL Type "Mod Para", Voices 1..8, PEnv Legato / Retrig / Voice. The rack splits at its first filter; `GateIn`, `ParaGate`, `EnvG` (T_ENV_G) are the new engine
  parts, `ProcessCtx` carries the voices and the held-key count. Host tests (`test_para.cpp`): both keys sound, a released key stops while another is held (power 1e15 -> 1e7),
  the last chord rings through the shared release, Retrig raises the level x 2.8 on a new key, Legato x 0.98, Voice releases a key alone.
- Board (`patch para N V`, Strng saw Mip, FX dry): 8 osc x 4 voices: 1 key 28.8k, 2 keys 46.8k, 4 keys 82.7k cycles (47 %); 4 osc x 8 voices: 1 key 20.3k, 4 keys 51.7k, 8 keys 100.5k (58 %);
  0 blocks over budget. One Strng oscillator (a pair) about 1.5-1.9k cycles per block per voice; Mix4 2k per voice at 8 oscillators.
- Rack OSC: Wav "Strng" (engine 8: Timbre = detune up to 50 c, Morph = saw / pulse / tri), Q row on OSC TUNE (Blep / Mip / Naive). Host C7 saw: Blep -28.2, Mip -40.6, Naive -12.5 dB.
- Aliasing check on the board (`dump` + an offline DFT, C5 single note, dry): saw Mip -40.6 / Naive -18.4 dB, pulse -40.2 / -20.1, triangle -39.7 / -39.7 = the host's numbers.
  What the user heard on `chord 16` (8 notes + a chromatic cluster C#5..G#5, 2 detuned saws per voice) is most likely beating of the cluster, not aliasing; to confirm by ear.
- Engine memory: the instance records and plans are sized by the real voice count (no static kMaxVoices tables): firmware RAM 50.7-51 % (was 55.6 % with the first Strings build).

## Strings, the third synth type (ADR-037; 2026-10-06; flashed and measured, NOT listened to by the user)

User decisions in framework/docs/DESIGN.md ADR-037 (do not re-litigate). What exists:
- `engine/modules/strings_modules.*`: `Strings` (voice scope: two detuned oscillators saw / pulse / tri, naive or mipmap, linear ADSR per block, one-pole LP on / off with
  key and envelope tracking, velocity, level; one render loop per wave x osc kind x LP, chosen per block) and `Ensemble` (global: three taps on one 11 ms line, slow + fast LFO at 0 / 120 / 240 degrees).
- `engine/dsp/wavetables.*` (generated, `framework/tools/gen_wavetables.py`): saw and triangle, 10 octave bands, 1024 samples + guard, 41 KB in flash (`ENGINE_WT_RAM=1` for RAM).
- Type `SYNTH_STRINGS` (GENERAL Type "Strings", `str_voices` 1..32 kept apart from the 8 of Modular / FM); pages STRINGS (Wav Osc Det Mix), STR TONE (PW Lvl), VOICE LP (LP Cut Env Key),
  STR FILTER (Typ Cut Res; Off = no node), AMP ENV (A D S R; the curves are ignored), SEQ, SEQ SETUP; menu tabs GENERAL, FX RACK, KEYS (since ADR-041: RACK with row M first). Params in `synth_params_t.str` (P_STR_*).
  FX rack type `FX_ENSEMBLE` ("EN": Rate Dpth Shim Mix) for every synth type (since ADR-041: the rack module `MOD_ENSEMBLE`, "ES"). Serial `patch strings`.
- Engine: `ENGINE_MAX_VOICES` 8 -> 32; the plan's instance table is now sized by the plan's voice count (`Plan::inst(node, voice)`), so plans did not grow (the test graph's module memory went
  16.6 / 8.8 KB -> 13.0 / 4.8 KB for 8 / 1 voices); the instance records still grow ~13 KB static (firmware RAM 51.5 -> 55.6 %).
- Host: 151 tests, all six matrix configurations green. C7 saw inharmonic power: naive -12.5 dB, mipmap -40.6 dB; 32 voices are real (rms 8 notes 4211, 32 notes 7208, 32 notes on 8 voices 3301);
  linear attack / release at half way 0.51 / 0.48. Screens checked with `ui_dump` at 128 x 128.
- Board: see the todo (16 voices 87k with the startup FX). Not done: listening, level balance (Lvl 0.5 x VoiceOut 0.5 per voice: 32 voices may clip, Mix 1.0 default), default pad envelope (AMP ENV is shared with Modular).

## ESP32Synth (removed 2026-10-06)

The ESP32Synth library (github.com/danilogcrf2-oss/ESP32Synth, MIT) was tried as a second engine on the board (`alt on`, a lazy-allocated object rendered by our audio
task) and removed at the user's request once the Strings type existed (git history: commit f714a84 has it). What it taught: thin voices (one oscillator x a linear
per-block envelope) make many voices cheap (506 cycles per voice and 32 frames, saw, measured); its S3 "SIMD" vector types compile to scalar code (0 `ee.*` instructions);
an outside engine needs its own voice allocator, mono -> stereo and its own measurement hooks: `engine_synth.h` is not an engine-neutral backend interface.

## State (end of the ESP32 bring-up and optimization session)

- The application runs on **our own DSP engine** (`framework/engine`, C++17, q15) on **both** the desktop simulator and the **first hardware prototype** ("HWV1": ESP32-S3-Pico, SH1107 128 x 128 OLED, TCA8418
  keyboard, quadrature knobs behind an analog mux, joystick, I2S DAC). 132 engine tests are green in all six rate / block configurations (`-Matrix`, run at the end of this session); the simulator and the firmware build.
- Working on the board (checked by the user): display, keyboard (map found from their key log), the 7 knobs (quadrature decode with hysteresis) and the joystick, power hold, I2S audio (the test tone and the engine),
  serial debugging, the boot-time benchmark, the serial command channel. The default patch is now `rack_init_startup`: **four oscillators on four different engines (Karplus, Modal, Supersaw, Additive) into one filter, delay 1000 ms / 40 %,
  reverb 40 %**. `rack_init` (osc, filter, saturator, LFO) is the older demo rack that the tests build on.
- **Not verified on the board**: the speaker amplifier (SPK_SD is left floating on purpose, the headphones/DAC path is what was heard; `HWV1_SPK_SD_MODE=1` drives it high), audio glitches while the screen draws (mitigated by IRAM code, never checked
  by ear), "the MCU stalls when no serial port is connected" (a fix was applied: non-blocking serial and `delay(1)` in the idle loop; maybe power related), the TF card (none), the new prototype's controls.
- **Not listened to by the user yet** (all of these changed the sound slightly and were only verified by tests): the interpolated filter coefficients, the 5-saw supersaw, modal as phasors, the additive recurrence, the half-rate reverb, mono delay / reverb,
  the vowel / dust filters in float, the dust level compensation.
- The user's earlier plans: save / load of racks and FM patches "after some fixes"; the engine / UI work of the previous sessions (ADR-028..034) was never listened to either (levels untuned; they asked to skip that step).

## Modular engine redesign (ADR-036: Mono / Paraphonic / Poly, capture, cost metadata) - stage 1 done on the host

The sampler is "fine for now" (user); the next topic is the modular engine. Decisions are in framework/docs/DESIGN.md ADR-036 (do not re-litigate): three modes (Mono default, Paraphonic, Poly kept as the legacy mode); "internal resampling" = capture a patch into a sample for the Sampler (not real-time polyphony); paraphonic splits the rack at its first filter with selectable envelope behaviour (first-key / every-key retrigger / per-voice amp env); budget = cost metadata only for now.
- **Stage 1 (done, host-tested, NOT flashed / measured / heard):** `Engine::load(graph, nvoices)` (the `Voices` setting was cosmetic before: the engine always built 8 copies); a node whose voice count changed is created anew and the old instances are freed by `gc()`; `Engine::set_voice_mode(Mono|Poly, legato, glide_ms)` with a last-note-priority key stack (16 keys), legato (pitch changes, envelopes keep running) or retrigger, exponential glide at block rate; `cfg.mode / glide / legato` in the GENERAL tab (default **Mono**; since ADR-041 there is no mode field: Voices 1 = mono, Glide / Legato shown only then); mode / voice-count changes rebuild, glide / legato are live; serial `mode mono|poly [glide] [legato]` (now also `para [env]`).
  Host numbers: 1 voice instead of 8 saves about half the module memory in the small test graph (16.6 KB -> 8.8 KB; real patches with Karplus / delay buffers should save much more: measure `[HEAP]` on the board); glide of 100 ms reaches 63 % in 105 ms.
- **Next on the board (needs a flash):** with the startup patch in Mono, the CPU and `[HEAP] fast heap used` should drop to about a voice's worth; check by ear that Mono feels right (last-note priority, legato on / off, glide) before stage 2.
- **Stage 2 (paraphonic)**, in the order I would build it: `ProcessCtx` gets the voice array; a global `GateIn` module (any-key gate, a per-key trigger, lowest / last / highest pitch, velocity); `Env` as `Env<Scope>` like the other `_G` variants (it already takes its gate from a cable); the mapper builds per-voice modules up to the first filter and global ones from there; the three envelope policies; paraphonic becomes another synth type (`SYNTH_MOD_PARA`, ADR-036 decision 5: the mode is part of the type, there is no Mode row).
- Stage 3 (capture): `Capture` module + a RAM-backed `StorageDevice` behind a mux with the card; saving to the card needs write support in `sd_card_spi.cpp` (CMD24 / CMD25 + FATFS write). Stage 4: per module cost model from `[PROF]`, a test against the host profiler.

## TF card and sampler on the board (measured on the board with the automatic loop of the skill; the loop fix and the sound are NOT yet listened to)

The user reported glitchy sampler playback, loops misplaced / shifted, 2-3 voices, the card read "too much". What the measurements found, in order:
- **Root cause of the glitching: ESP-IDF's sdspi host spent about 40 ms before EVERY SD command on this board** (a plain CMD13 took 40 ms; with a pin trace, CS / MISO stayed quiet for 40.5 ms and the real transfer took 55 us at the end; two different cards (an old 1 GB and a 32 GB), SPI2 and SPI3, 10 and 20 MHz, MISO pull-up: all the same; a bare 10-byte SPI transfer on the same bus was normal).
  A 4 KB read therefore cost 80 ms = 50 KB/s, and one sampler voice needs about 96 KB/s. Fix: an own small SPI-mode driver (`sd_card_spi.cpp`, read only, registered with FATFS): 1 sector 82 ms -> 1.4 ms, 8 sectors 5 ms, a scan of the 5 demo files 6 s each -> 0.36 s. The IDF host stays behind `-DHWV1_SD_IDF` for comparison.
- **Loop points** (ADR-022 addendum): a fixed `block % 8` ring slot made a loop's end and start blocks collide, so every lap dropped out; fixed on the host with a test (it fails without the fix). Not yet heard on the board.
- **Sampler CPU** (measured, 44.1 kHz, `patch sampler 3 2` = pad_c4, forward loop, one filter, no effects): the Sampler cost 15.7k cycles per voice and block (490 per sample). A per-block fast path (steady rate, no wrap, one contiguous run of data; bit-identical output, checksum test `sampler_fast_path_is_identical_to_the_general_loop`) cut it to 5.4k.

| notes held | cycles per block (budget 174149) before -> now | blocks over budget before -> now | sampler underruns before -> now |
|---|---|---|---|
| 1 | 27.6k -> 17.1k | 0 -> 0 | 0 -> 0 |
| 3 | 72k -> 41.5k | 0 -> 0 | 0 -> 0 |
| 4 | 103k -> 54.7k | 143 -> 0 | 0 -> 0 |
| 6 | 147k -> 81k (47 %) | 1119 -> 0 | 20 -> 0 |
| 8 (two octaves) | not measured -> 312k (179 %) | -> 1266 of 2127 | -> 420 |

- **Now the limit is the card at 8 voices**: about 490 KB/s delivered at 7 ms per 4 KB read (0.6 ms per sector: the token poll is one SPI call per byte and every sector is a separate 514-byte transfer plus a copy; the wire time would be 0.2 ms). An underrun sends a voice to the slow general loop (490 cycles per sample), which is why 8 notes also blow the CPU budget.
  Next steps (in order): (1) read a block's token and data in one SPI transfer for every sector after the first of a command (expected 0.6 -> about 0.35 ms per sector, exact); (2) larger / fewer commands (CMD18 for 16 sectors); (3) design choices for the user: voices playing the same sample share the blocks they read, a faster clock (since then `HWV1_SD_FREQ_KHZ` is 40000 in `sd_card_spi.cpp`, used only when the card accepts CMD6 high-speed, else 25 MHz;
  `HWV1_SD_OVERCLOCK=1` forces it), prefetch deeper.
- **Card handling** (new, tested with the old card on the board, and the screen in `ui_dump`): the card is polled once a second (no card-detect pin). A card whose sector read takes more than 15 ms is not used and the app shows "SD CARD TOO SLOW"; removal or a swap resets the catalog (loaded sample slots stay in memory until reboot: known debt) and the app rebuilds the synth (a 170 ms audio stall, once per card event).
  A failing mount attempt must never busy-wait (it starved the idle task before): the own driver returns quickly when no card answers.
- Task priorities are in framework/docs/BOARDS.md ("Tasks and priorities"). `samples_src/` + `build.ps1` (`-Sd E:` copies to the card) is the sample workflow; a name is the file's path below `system/samples`, 47 characters at most (longer: listed, not usable); only `.smp` is read on the board.
- `platformio.ini` (2026-10-07, set by the user): `-DENGINE_SR=44100`, `DEV_OUTPUT_GAIN_PCT=25`, `DEV_BOOT_DELAY_MS=500`, `DEV_SERIAL_CMD` on; **`HWV1_BENCH`, `ENGINE_PROFILE`,
  `HWV1_DEBUG_AUDIO`, `HWV1_DEBUG_UI` are commented out: turn them back on before any measurement (`serial_test.py` needs `ENGINE_PROFILE` + `HWV1_DEBUG_AUDIO`)**.

## Latest measurements (prototype, startup patch, mono FX + half-rate reverb, budget 160000 cycles per 32-frame block)

Measured with `apps/ritual_machine/tools/serial_test.py` on the last build that was on the board (engines still with the generic per-sample loop; the loop fast path below was built after it):

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

## Sampler at high pitch (measured 2026-10-05, pad_c4 forward loop, `patch sampler 3 2`, which is Mono: one voice)

| note | stream speed | card | Sampler cycles per block | blocks over budget |
| --- | --- | --- | --- | --- |
| C4 (root) | 1x | 92 KB/s | 6.0k | 0 |
| C5 | 2x | 183 KB/s | 7.1k | 0 |
| C6 | 4x | 369 KB/s | 8.7k | 0 |
| G6 | ~6x | 546 KB/s, underruns | 10-19k | 21 |
| C7 | 8x | 640 KB/s (needs ~770), 160 underruns/s | **182k** (budget 174k) | 342 of 581 |

- Two problems: the card bandwidth (~640 KB/s at most) is reached by ONE voice at ~6-8x its root; and a starved voice cost ~20x CPU, because every sample ran 4 `fetch()`
  misses, each searching the ring by 64-bit atomic tags (a locked libatomic call on the S3). Fix (exact, built, 144 tests x 6 configurations green, NOT flashed / measured):
  `miss_blk_` in the Sampler, a missing block is searched once per process() call.
- **Optimization loop of 2026-10-06 (user allowed flashing for this task; all exact, measured on the board, details in framework/docs/DESIGN.md ADR-022 addendum 2):**

| case (25 MHz unless noted) | start of the session | now |
| --- | --- | --- |
| 1 voice C7 (8x): Sampler cycles / blocks over budget | 182k / 342 of 581 | ~28k (starved) or ~9k / 0 |
| 1 voice C7: underruns per 2 s | 320 | ~1 (25 MHz) / 0 (40 MHz) |
| 3 voices C5 E5 G5 (sum ~7.5x) | ~150 underruns per 2 s | 0-100 (card at its limit) |
| 6 voices C4..G5 (sum ~10x, needs ~880 KB/s) | render 1444 us, 325 blocks over budget | render 966 us, 2-7 over (card limited: ~500 KB/s) |

  Changes: Sampler `miss_blk_` and `starved_block()`, loader tag snapshot, `ENGINE_RING_BLOCKS` 12, driver token poll 8 bytes per call, data CRC check + retry, CMD6 attempt,
  `HWV1_SD_OVERCLOCK`, `seek avg` in the `[SD]` line (seeks cost ~100 us: not the problem), scattered-read bench. Test `a_stalling_card...` now stalls 1 s (longer than the ring).
  Speaker: `DEV_SPEAKER_DEFAULT=0` (user asked for the speaker off while testing).
- **Open, user decides:** (a) 40 MHz overclock: out of this card's spec, 0 CRC errors in 100 s of streaming, 4 KB file read 5.0 -> 3.7 ms; (b) 8 KB reads per command
  (2 ring blocks): ~15 % more bandwidth, loader + storage change; (c) pre-decimated copies of samples for high notes (converter; bandwidth stops growing with pitch,
  less aliasing, ~2x card space); (d) a faster card / the SDMMC 4-bit slot of the next prototype. **Not yet listened to** after these changes.

## Next performance steps (in the order I would take them)

Before the list, moved from the todo on 2026-10-14: the first two were chosen by the user on 2026-10-11 as the next
performance work and not picked at the session start of 2026-10-14.

- **Done 2026-10-28 (branch `perf-pads`, ADR-035 item 10): the Pads fixed cost and the fast heap back in internal RAM.** What is
  left from that work, in the order I would take it:
  - **`Sampler::process` (10 KB) in IRAM**: `Pads::process` in IRAM cut the Pads player's cost by 4k a block at no cost to the
    synth; the samplers are now the Pads section's biggest cost (~3k per short pad, ~5.6k per streamed one). In IRAM they would
    take the fast heap under its 64 KB minimum (largest block 90.1 KB at the audio start, the fast heap = block - 16 KB). Room
    must be found first: the next big static user is the UI's `app_t app` (18.5 KB, `main_esp32.cpp`), which could live in
    PSRAM (a trade: the UI's state behind the cache; measure the UI step times with `HWV1_DEBUG_UI`). Ask the user
  - **The synth's graph after a stay in the Pads section holds 48 KB of the fast heap, 76 KB at boot** (same before the RAM
    work, seen in the `[HEAP]` lines): the cycles were the same both times, so not chased; likely the order in which the
    reverb's big buffers find room
  - The card limit of 8 streamed pads (~300 card underruns in 6 s, unchanged): "SD options" below
- **Workflow plan phase 2: measure the cost on the board**: step 1 (both branches, Para per branch, MIX) and step 2 (effects inside a
  branch, modulators reaching row M, row M's filter envelope; ADR-041 "Phase 2, step 2") flashed and tried by the user on the prototype
  2026-10-09: Osc -> Phaser -> Filter works, modulators work. Not measured: the cost of per-voice effects and of a stereo shared part (from: notes)
- Engine / board "D" items: SD options (a)-(d) (8 streamed pads reach the card's ~500 KB/s) (from: user + measurements)

1. Flash the fast-path build, measure, record the numbers here (and in ADR-035's table). Ask the user to **listen** to the list of unlistened changes above and to say which sound changes they dislike; each has a flag or a one-line revert (see ADR-035).
2. Remaining per-voice costs: Env 2.2k (two per voice: amp env + the filter's own, 47 cycles per sample, still 64-bit-free but not specialised), Lfo 1.8k, Shaper 1.8k, VoiceOut 0.9k. A fast path for "Env idle / sustain" and for constant LFO shapes is likely.
3. Engines: additive 263 (12 weighted adds + 11 recurrence steps; fewer harmonics at high pitch would help but changes the sound), modal 229 (8 modes x ~20), ssaw 237 (5 saws, the rest is mixing), vowel 217 (3 float filters).
   Ask the user before any change that alters sound (they decide quality trades; offer options with pros and cons).
4. Fixed cost: reverb 20.5k (the tank halves ~5k each at half rate; trimming one of the four input allpasses or the second decay diffuser saves 2-3k and changes the character; a cheaper diffusion structure was proposed, not tried),
   delay 8.4k (PSRAM Hermite read + write per sample, 5.2k).
5. Before a release remove the dev flags (list in DEVELOPING.md) and decide `DEV_OUTPUT_GAIN_PCT` (currently 25 %, platformio.ini), `ENGINE_FX_MONO`, `ENGINE_REVERB_HALF`.
6. Open hardware items: speaker amplifier check (Spk Off checked by the user 2026-10-28; the pop at start-up is the
   prototype's hardware, fixed in the next one), display / audio separation check by ear, the TF card.

## Decisions that went against my recommendation (recorded with their risks in framework/docs/DESIGN.md)

The user decides and I record the reasoning and move on, without re-litigating:
- **All signals are q15 audio-rate blocks** (ADR-010): simpler, costs CPU for slow modulators.
- **Naive saturation** (ADR-013), **q15 block-floating-point FFT, N = 512** (ADR-016), **Dattorro plate reverb** and **fixed Juno-like chorus modes** (ADR-019 / 020), **AMY replaced outright** (ADR-027).
- **Own PIE block-float FFT kernel** (2026-10-07; I recommended ESP-DSP float + a real-FFT trick): risk = S3-only assembly checked by the boot bench, not by the
  host tests (the host runs the C path); built and measured: 16k cycles, 63-64 dB.
- **A control table per board** (2026-10-13, ADR-043 decision 10; I recommended one shared control list): risk = the Ritual
  Machine's bindings, key layouts, layer entries and the control names saved in `ui.cfg` / `keys.cfg` are written against the
  shared list; the larger step, and the one that can break saved settings. Mostly taken away by decision 11 (the app keeps its
  role list and maps each board onto it).
- **Each app lists its own card folders** (2026-10-13, ADR-043 decision 13; I recommended shared samples and `apps/<name>/` for
  any other app): risk = no shared sample folder unless apps agree, two firmwares on one card can overwrite each other's files.
- **The simulator's panel drawn from a board's table** (2026-10-13, ADR-043 decision 14; I recommended moving today's two windows
  as they are): risk = `panel_sim.c` rewritten, about half a session more, a generic drawing in place of today's panel.
- **The simulated board picked at build time** (2026-10-13, ADR-043 decision 15; I recommended at run time, one program with
  every table): risk = a rebuild to switch and a board that is not built can stop compiling unnoticed. Softened: each board has
  its own program and objects, `run.ps1` says when the one it starts is older than another board's, the host tests read both
  tables.
- On the hardware (ADR-035): a **float exception** for the filters; **mono delay / reverb** and the **half-rate reverb tank** as build flags; the **5-saw supersaw**; the sample rate is **never below 44100 Hz**; modal as phasors and the additive recurrence were done before asking
  because their tests show the same sound within a stated tolerance.


## Known debt / small issues

- **MI follow-ups**: loudness per model, denormals (Plaits drums), IRAM, Harm placement, tier 3; cycles per model (`[MI]`) not recorded
  here yet (from: notes)
- Sound-design round (ADR-031..034), not listened to: the convolver (PIE since 2026-10-09: 10k cycles per block at 128 taps, ADR-034), the compressor / shifter / engines are tested against theory only,
  the engine previews in the OSC graph box are sketches.
- `svf_tick` (integer, 202 cycles) and `svf_coef_f` remain in `dsp/svf.h` only as the references the boot benchmark compares against; the engine uses `svf_tick_f`.
- The reverb / delay stage timers (`SEC_BEGIN` / `SEC_MARK`) and the oscillator counters stay in the code under `ENGINE_PROFILE` (zero cost without it).
- `framework/tests/test_sampler.cpp` has a `run_trace` helper that prints underruns. Engine items under "Known limits" in framework/docs/DESIGN.md (reverb q15 floor, block-rate envelopes, mono samples, ...).


## Working style and environment traps

- **Never run `pio` from a shell** (2026-10-29): a `pio run` from Bash / PowerShell (PlatformIO Core 6.2.0, from Python313 or
  `%USERPROFILE%\.platformio\penv`) failed on "uv installation via pip failed" and emptied `C:\.platformio\penv`, the
  environment of the VS Code extension that `build.ps1` (sprites) and the firmware use. `pyvenv.cfg` was recreated
  (`C:\.platformio\python3\python.exe -m venv C:\.platformio\penv`), then pip, then the Core. **PyPI's `platformio==6.2.0`
  reports itself as `6.2.0.post1`, which the pinned platform 55.03.312 ("Core >=6.2.0") counts as older (IncompatiblePlatform)**:
  the Core that works is GitHub master (`pip install --no-deps --force-reinstall
  https://github.com/platformio/platformio-core/archive/master.zip` with that penv's python; the user installed it). The user
  builds and flashes the firmware from VS Code; say "firmware not built" instead.

- The user iterates with short requests, tests on the board or in the SDL window, and wants concise answers that say what was verified vs not. Windows 11, PowerShell, MSYS2 UCRT64; AZERTY keyboard; English with some French.
  For design work they want options with pros and cons at each stage; they often pick against my recommendation. **By default they flash the board themselves** (COM8). On 2026-10-10 / 11 they allowed Claude to flash and measure (asked each time: "flash and measure?"): ask again in a new session before the first flash. They sometimes say "flashed" about an older build: check (see the skill).
- A profiling build goes through the environment, not platformio.ini: `$env:PLATFORMIO_BUILD_FLAGS = "-DHWV1_DEBUG_AUDIO -DENGINE_PROFILE"` then `pio.exe run -d apps/ritual_machine -t upload`; **flash the normal build again afterwards** (the profiling one prints every second and costs RAM).
- Board tools for the Pads work (all over serial, `DEV_SERIAL_CMD`; see apps/ritual_machine/docs/SERIAL_COMMANDS.md, `pad ...`, `dir`, `use`): `apps/ritual_machine/tools/library_test.py` (the folder list, header reads by place in the folder, samples cycled on a pad, a stream while a folder is read), the serial command `browse N REST FIRST` (the browser driven as by hand: how long each UI step took, stream underruns; `serial_test.py --cmd "pad on" --cmd "browse 6 1500 1750" --raw --seconds 12 --log run.txt`, then read the log: `--raw` does not print the replies), `apps/ritual_machine/tools/pads_test.py` (pad voices), `padsfx_test.py` (FX chains, per-module costs), `edit_test.py` (sample jobs: time, memory), `capture_test.py` (recording).
- UI tests address some rows by number (the RACK page's menu rows in `apps/ritual_machine/tests/ui/test_rows.c`, the EDIT page's rows in `apps/ritual_machine/tests/ui/test_pads.c`): inserting a row in those screens shifts them.
- The user often keeps the simulator open: `build.ps1` then builds `oled_sim_new.exe`. A stale exe has caused a false "bug" before. Since 2026-10-13 each simulated board is its own exe (`oled_sim.exe`, `oled_sim_hwv1.exe`): a change built for one board is not in the other (`run.ps1` prints a note when the one it starts is the older).
- PlatformIO: use `C:\.platformio\penv\Scripts\pio.exe`, **never a bare `pio`** (PATH points at Python 3.13's pio, which recreates and destroys the penv). It happened twice; the second time (2026-10-05) Claude did it, and the user had to restore the penv by hand. Repair steps: framework/docs/BOARDS.md, "ESP32 firmware" warning. One link failure with no error text happened twice; the immediate rerun succeeded.
- **Never send the serial command `storage` in an unattended session**: it restarts the board into the USB card reader, the
  serial port disappears and only a hand brings it back (the joystick held 2 s, or the power button off and on). It happened
  on 2026-10-28 (Claude used it to look at the card): the board work stopped until the user came by. Read what a serial command
  does (`serial_cmd_esp32.cpp`) before sending it. **Tests that redefine pads or sounds save them to the card** (the app saves
  a moment later): `nosave 1` first (`pads_test.py` does it and refuses a firmware without it), and a restart afterwards
  brings the card's settings back.
- One program per serial port: close the serial monitor before `serial_test.py`. Before an upload, check that the board is there
  (`[System.IO.Ports.SerialPort]::GetPortNames()`): switched off or unplugged it is on no port, and `pio run -d apps/ritual_machine -t upload` fails after
  the whole build ("Please specify `upload_port`"). A build to check only: `PLATFORMIO_BUILD_DIR` in the scratchpad.
- Start-up traces: `-DBOOT_TRACE` (apps/ritual_machine/docs/DEVELOPING.md, build flags) prints `[BOOT]` lines; found with it on 2026-10-11: an input event
  that waits in the queue at start acts on the UI's first step (the joystick switch read as pushed).
- Bash tool: **heredocs containing apostrophes or quotes break; `\n` inside a C string written from a Python literal becomes a real newline** (it hit `printf` lines many times: grep for a string that wrapped, or fix with a regex on `%u[\r\n]+"`).
  Write multi-line scripts with the Write tool and run them; edit with the Edit tool. Many sources are CRLF: scripted edits must keep each file's line endings.
  **`sed -i` in this Git Bash rewrites a CRLF file with LF endings** (hit twice on 2026-10-09; git hides it in the diff): use a Python script that reads
  with `newline=''` and writes the endings back, and check with `file <path>` after any scripted edit.
- **The repo can be moved** (2026-10-28; the user: "my SSD is probably dying", they want to move things around). It moved
  again that day to `D:\PRODUCT DESIGN\SevenSynthCore\Sevenist Core`, and the build was found linking stale objects (the
  `.d` files named the old folder: every header looked unchanged) with a link line of 28 200 characters. Fixed: the
  desktop build (`framework/tools/sim_build.ps1`, `Get-RelPath`) and the three test scripts pass paths relative to the app's
  folder; a `.d` file naming a file that is gone rebuilds its object. Link line now ~10 900 characters. Still absolute:
  the toolchains (`C:\msys64\ucrt64`, `C:\.platformio`: if C: is the dying disk, they have to be reinstalled or made a
  setting) and the PlatformIO-generated `.vscode` / `compile_commands.json` (ignored by git, rewritten by PlatformIO; the
  root `.vscode` still names the very old `D:\DEV\SevenSynthCore\ui\oled_sim`, unused). The paragraph below about the link
  line's length is history now.
- **The repo's folder has a space in its name** (`Sevenist Core`, since 2026-10-14). `Start-Process -ArgumentList` quotes nothing, so
  the scripts handed the compiler half a path ("Core\apps\...: No such file"): `framework/tools/sim_build.ps1` (`Format-ToolArgs`,
  and the `.d` files' `\ ` in `Test-NeedsBuild`) and the three test scripts now quote. The objects built in the old folder name it in
  their `.d` files: a header changed since is not seen for them until they are built again (delete `build/obj*` once for a clean
  start). The link line holds every object's full path, about 25 000 characters of Windows' 32 767: a longer folder name can pass it.
  A script not run since the move (`make_release.ps1`, `new_project.ps1`, the Python board tools) may have the same fault.
- `apps/ritual_machine/src/platform/engine/engine_synth.h` is included from C: no default arguments, no C++ in it.
- Tool calls that run `g++` through PowerShell hide compiler errors (`NativeCommandError`): compile from bash to see them. A test executable needs `C:\msys64\ucrt64\bin` on PATH.
- Python on Windows: always `open(..., encoding="utf-8")`. The test runner takes a name filter (`engine_tests.exe reverb`) and, via the script, engine flags: `test.ps1 -Defs "-DENGINE_REVERB_HALF=1"`; `-Matrix` takes a few minutes, one configuration about 1 min.
- Do not commit unless asked. (The Pads work: the user asked for a commit at every step; it was on the branch `pads-step1`, merged into `main` and removed on 2026-10-11. Ask where the next work goes: `main` or a new branch.) Delete `run*.txt` serial captures when done (ignored by git now); keep the numbers in this file.

## Starting the next session (compact prompt)

Run `/session-start` (skill `.claude/skills/session-start/SKILL.md`): it reads these notes and the app's, talks through the open items, writes the "Session todo" of the part concerned (here for the
library, in `apps/<app>/CONTINUE.md` for an app) and proposes where to start.
For board performance work directly:

> Read `CONTINUE.md`, `apps/ritual_machine/CONTINUE.md`, `framework/docs/BOARDS.md`, `framework/docs/DESIGN.md` (ADR-035) and
> `apps/ritual_machine/docs/DESIGN.md` (ADR-041, the Pads section), and the skill `esp32-optimize`. The work is on `main`. The board is connected on COM8 with the last build flashed. <what I heard / what to change>.
> Then re-measure with `apps/ritual_machine/tools/serial_test.py` and continue with "Next performance steps".
