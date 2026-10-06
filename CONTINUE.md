# Continue development: handoff notes

Read [DEVELOPING.md](DEVELOPING.md) first (how to work in the code, where to change things, tools, testing, **"ESP32 firmware and performance work"**), then
[ENGINE_DESIGN.md](ENGINE_DESIGN.md) for the engine's decisions, measurements and limits (**ADR-035 = what was learned on the real chip**). This file only holds what they do
not: state, the latest measurements and what to do next, the user's working style, known debt and traps.
For any CPU / memory optimization work on the board, use the project skill `.claude/skills/esp32-optimize/SKILL.md` (the loop that worked: measure with
`tools/serial_test.py`, locate, change, host-test, the user flashes, re-measure).

## Session todo (updated 2026-10-06, second session)

- [x] Mixer polish: sources are cables into one plan MIX step (no Mix4 chain), VoiceOut one product when centred; measured on the board (done 2026-10-06, see below)
- [ ] Optional, user decides: voice bus in 32 bits with one saturation at BusIn (VoiceOut ~790 cycles per voice per 64 frames is near the floor of the q15 saturating version; clipping would happen once on the sum instead of per voice add) (from: measurements)
- [x] RGB key LEDs: FastLED driver + play feedback (`core/key_leds.c`), colour order and layout checked by the user on the board (done 2026-10-06, see below)
- [ ] Audio blocks over budget while keys are PLAYED (0.4-0.5 % of blocks, worst ~2.3 ms render; LEDs on 92 / off 63 of 17250): serial notes never do it, so the key path (TCA8418 interrupt / I2C on the audio core?, UI work) - find where (from: measurements)
- [ ] Listen to Strings and the rack oscillator's Q (Blep / Mip / Naive); confirm the `chord 16` "aliasing" is cluster beating; then levels / defaults (from: user)
- [ ] Integration findings for an engine-independent "audio backend" interface (own voice allocator, mono -> stereo, memory policy, measurement hooks) (from: user)
- [ ] Check the F1 power-on keys reset (`[KBD] key r4 c7 held at power-on`) (from: notes)
- [ ] Decide the SD options (a)-(d) and listen to the sampler after the loop fix (from: notes)
- [ ] Internal RAM for the startup patch: what stays internal with a 74 KB fast heap (reverb tank, delay, filters) or free internal RAM (IRAM code 124 KB) (from: measurements)
- [x] Mod Para and Para listened to: no issues; legato and glide OK (user) (done 2026-10-06)
- [x] Build without ESP32Synth flashed and tested; pending work committed (0542d79) (done 2026-10-06)

## Key LEDs (2026-10-06; flashed, checked by the user: every key lights its own LED, colours right)

- Driver `leds_esp32.cpp`: FastLED 3.10.6 pinned (it gives the first RMT channel DMA on the S3 by itself; SynthBox's `patch_fastled_dma.py` targets 3.10.3's file layout and is not
  needed), `-DFASTLED_RMT_MAX_CHANNELS=1`, SK6812 order **RGB** (SynthBox's BGR swapped red and blue here), chain map in physical columns (the HWV1_FLIP_COLS flip mirrored every row),
  **brightness capped at 20 % (51/255, `HW_LED_MAX_BRIGHTNESS`): the user's limit, full white on 36 LEDs ~2 A browns out the board / USB**. A transfer only when the frame changed.
- What they show (user choice "play feedback"): `core/key_leds.c` from the key layout: notes in piano colours (C teal, naturals dim white, sharps dim blue), held = orange;
  Shift yellow (bright while on), Menu blue (bright while open), Back red, Play green (bright while the sequencer runs), Octave violet (bright when shifted that way), navigation dim white.
  Repainted at most every 20 ms, at once on input. The simulator's LED HAL is a no-op (the panel window does not draw them yet).
- Serial (dev): `leds N`, `leds all`, `leds off` / `leds on`. Audio while playing keys: see the todo (the overruns happen with the LEDs off too).

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

Decisions in ENGINE_DESIGN.md (ADR-036 stage 2 "as built", ADR-037 decision 5). The user flashed-and-test permission covers this task (COM8).
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

User decisions in ENGINE_DESIGN.md ADR-037 (do not re-litigate). What exists:
- `engine/modules/strings_modules.*`: `Strings` (voice scope: two detuned oscillators saw / pulse / tri, naive or mipmap, linear ADSR per block, one-pole LP on / off with
  key and envelope tracking, velocity, level; one render loop per wave x osc kind x LP, chosen per block) and `Ensemble` (global: three taps on one 11 ms line, slow + fast LFO at 0 / 120 / 240 degrees).
- `engine/dsp/wavetables.*` (generated, `tools/gen_wavetables.py`): saw and triangle, 10 octave bands, 1024 samples + guard, 41 KB in flash (`ENGINE_WT_RAM=1` for RAM).
- Type `SYNTH_STRINGS` (GENERAL Type "Strings", `str_voices` 1..32 kept apart from the 8 of Modular / FM); pages STRINGS (Wav Osc Det Mix), STR TONE (PW Lvl), VOICE LP (LP Cut Env Key),
  STR FILTER (Typ Cut Res; Off = no node), AMP ENV (A D S R; the curves are ignored), SEQ, SEQ SETUP; menu tabs GENERAL, FX RACK, KEYS. Params in `synth_params_t.str` (P_STR_*).
  FX rack type `FX_ENSEMBLE` ("EN": Rate Dpth Shim Mix) for every synth type. Serial `patch strings`.
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

- The application runs on **our own DSP engine** (`src/engine`, C++17, q15) on **both** the desktop simulator and the **first hardware prototype** ("HWV1": ESP32-S3-Pico, SH1107 128 x 128 OLED, TCA8418
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

The sampler is "fine for now" (user); the next topic is the modular engine. Decisions are in ENGINE_DESIGN.md ADR-036 (do not re-litigate): three modes (Mono default, Paraphonic, Poly kept as the legacy mode); "internal resampling" = capture a patch into a sample for the Sampler (not real-time polyphony); paraphonic splits the rack at its first filter with selectable envelope behaviour (first-key / every-key retrigger / per-voice amp env); budget = cost metadata only for now.
- **Stage 1 (done, host-tested, NOT flashed / measured / heard):** `Engine::load(graph, nvoices)` (the `Voices` setting was cosmetic before: the engine always built 8 copies); a node whose voice count changed is created anew and the old instances are freed by `gc()`; `Engine::set_voice_mode(Mono|Poly, legato, glide_ms)` with a last-note-priority key stack (16 keys), legato (pitch changes, envelopes keep running) or retrigger, exponential glide at block rate; `cfg.mode / glide / legato` in the GENERAL tab (default **Mono**); mode / voice-count changes rebuild, glide / legato are live; serial `mode mono|poly [glide] [legato]`.
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
  Next steps (in order): (1) read a block's token and data in one SPI transfer for every sector after the first of a command (expected 0.6 -> about 0.35 ms per sector, exact); (2) larger / fewer commands (CMD18 for 16 sectors); (3) design choices for the user: voices playing the same sample share the blocks they read, a faster clock (`HWV1_SD_FREQ_KHZ`, 20000; 26.7 MHz is outside the SD default-speed spec), prefetch deeper.
- **Card handling** (new, tested with the old card on the board, and the screen in `ui_dump`): the card is polled once a second (no card-detect pin). A card whose sector read takes more than 15 ms is not used and the app shows "SD CARD TOO SLOW"; removal or a swap resets the catalog (loaded sample slots stay in memory until reboot: known debt) and the app rebuilds the synth (a 170 ms audio stall, once per card event).
  A failing mount attempt must never busy-wait (it starved the idle task before): the own driver returns quickly when no card answers.
- Task priorities are in DEVELOPING.md ("Tasks and priorities"). `samples_src/` + `build.ps1` (`-Sd E:` copies to the card) is the sample workflow; names are cut to 23 characters; only `.smp` is read on the board.
- `platformio.ini` currently has `-DENGINE_SR=44100` (the user's test), `DEV_OUTPUT_GAIN_PCT=12`, and the measurement flags (`DEV_SERIAL_CMD`, `ENGINE_PROFILE`, `HWV1_DEBUG_AUDIO`) on.

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

## Full keyboard and key layouts (2026-10-05; built and host-tested, NOT flashed)

- All 36 keys of the prototype are mapped: the HAL matrix is now 8 x 5 (`KEY_COLS 8`); HWV1's 4 function keys are HAL row 0 (they were BTN 3 / BTN 1 / BTN 2 / PLAY), its
  4 x 8 note keys rows 1..4. `input_key_present()` (new in `hal_input.h`) says which keys a board has.
- Key functions come from a **layout** (`core/keymap.c`): 3 built in (*Keys 8x4* default, *Two 4x4*, *Notes+Nav*) + *User*, edited in the new **KEYS** menu tab
  (last tab in both modular and FM). User decisions: the three layouts as presets, user keys on the TF card (`keys.cfg`, text), only keyboard keys in the page,
  lock-out escape = Reset row + F1 held 2 s within 4 s of boot. Details in DEVELOPING.md ("Key layouts").
- Card writes: `sd_card_spi.cpp` got CMD24 single-block writes (+ CMD13 check); file jobs run on the `sd_io` task (`samples_esp32.cpp`, `hal/hal_storage.h`). **Risk the
  user accepted: a write bug can damage the card's file system; back the card up before the first test.**
- Verified: simulator + firmware build; a scratch test (all layouts, User copy, text round trip, save on menu close, card in / out, KEYS-tab key selection, boot reset;
  it found and fixed a wrap-around that fired the reset at once). Not verified: anything on the board (key map orientation of the right half, card writes, keys.cfg on a real card).
- Board test (user, 2026-10-05): all keys OK, F1..F4 OK, `[SD] wrote /sdcard/keys.cfg (689 bytes)`, the layout survives a reboot, samples stream after a write. The F1 power-on
  reset did NOT work: the TCA8418 reports changes only and `kbd_init` drains its FIFO, so a key held from power-on makes no event. Fixed (built, NOT flashed): `kbd_init` reads
  F1 directly before the key scan starts (column driven low as a GPIO, row read; up to 2 s while held), `input_boot_reset()` in the HAL, the app resets at init;
  the "KEYS RESET" screen goes at the next key or after 3 s. Boot log: `[KBD] key r4 c7 held at power-on`.

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
- **Optimization loop of 2026-10-06 (user allowed flashing for this task; all exact, measured on the board, details in ENGINE_DESIGN.md ADR-022 addendum 2):**

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

1. Flash the fast-path build, measure, record the numbers here (and in ADR-035's table). Ask the user to **listen** to the list of unlistened changes above and to say which sound changes they dislike; each has a flag or a one-line revert (see ADR-035).
2. Remaining per-voice costs: Env 2.2k (two per voice: amp env + the filter's own, 47 cycles per sample, still 64-bit-free but not specialised), Lfo 1.8k, Shaper 1.8k, VoiceOut 0.9k. A fast path for "Env idle / sustain" and for constant LFO shapes is likely.
3. Engines: additive 263 (12 weighted adds + 11 recurrence steps; fewer harmonics at high pitch would help but changes the sound), modal 229 (8 modes x ~20), ssaw 237 (5 saws, the rest is mixing), vowel 217 (3 float filters).
   Ask the user before any change that alters sound (they decide quality trades; offer options with pros and cons).
4. Fixed cost: reverb 20.5k (the tank halves ~5k each at half rate; trimming one of the four input allpasses or the second decay diffuser saves 2-3k and changes the character; a cheaper diffusion structure was proposed, not tried),
   delay 8.4k (PSRAM Hermite read + write per sample, 5.2k).
5. Before a release remove the dev flags (list in DEVELOPING.md) and decide `DEV_OUTPUT_GAIN_PCT` (currently 1 %, set by the user while testing at night), `ENGINE_FX_MONO`, `ENGINE_REVERB_HALF`.
6. Open hardware items: speaker amplifier check, display / audio separation check by ear, the TF card.

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
- PlatformIO: use `C:\.platformio\penv\Scripts\pio.exe`, **never a bare `pio`** (PATH points at Python 3.13's pio, which recreates and destroys the penv). It happened twice; the second time (2026-10-05) Claude did it, and the user had to restore the penv by hand. Repair steps: DEVELOPING.md, "ESP32 firmware" warning. One link failure with no error text happened twice; the immediate rerun succeeded.
- One program per serial port: close the serial monitor before `serial_test.py`.
- Bash tool: **heredocs containing apostrophes or quotes break; `\n` inside a C string written from a Python literal becomes a real newline** (it hit `printf` lines many times: grep for a string that wrapped, or fix with a regex on `%u[\r\n]+"`).
  Write multi-line scripts with the Write tool and run them; edit with the Edit tool. Many sources are CRLF: scripted edits must keep each file's line endings.
- `src/platform/engine/engine_synth.h` is included from C: no default arguments, no C++ in it.
- Tool calls that run `g++` through PowerShell hide compiler errors (`NativeCommandError`): compile from bash to see them. A test executable needs `C:\msys64\ucrt64\bin` on PATH.
- Python on Windows: always `open(..., encoding="utf-8")`. The test runner takes a name filter (`engine_tests.exe reverb`) and, via the script, engine flags: `build_engine_tests.ps1 -Defs "-DENGINE_REVERB_HALF=1"`; `-Matrix` takes a few minutes, one configuration about 1 min.
- Do not commit unless asked. Delete `run*.txt` serial captures when done (ignored by git now); keep the numbers in this file.

## Starting the next session (compact prompt)

Run `/session-start` (skill `.claude/skills/session-start/SKILL.md`): it reads these notes, talks through the open items, writes the "Session todo" section here and proposes where to start.
For board performance work directly:

> Read `CONTINUE.md`, `DEVELOPING.md` and `ENGINE_DESIGN.md` (ADR-035), and the skill `esp32-optimize`. The board is connected on COM8 with the last build flashed. <what I heard / what to change>.
> Then re-measure with `tools/serial_test.py` and continue with "Next performance steps".
