# Serial commands (prototype, dev builds)

Text commands over the board's USB serial port to play notes, load test patches, change settings and measure the engine, without touching the keys.
Code: [src/platform/esp32/serial_cmd_esp32.cpp](../src/platform/esp32/serial_cmd_esp32.cpp). Measurement tool: [tools/serial_test.py](../tools/serial_test.py).
Paths and commands below are from the `oled_sim` folder (the project root), not from `human_docs/`.

## Before you start

- Firmware built with `-DDEV_SERIAL_CMD` (commands). For the measurement lines also `-DHWV1_DEBUG_AUDIO -DENGINE_PROFILE` (and `-DHWV1_DEBUG_UI` for `[UI]`).
  The default `platformio.ini` has them; all are dev only (remove before a release, see [DEVELOPING.md](../DEVELOPING.md) "Build flags").
- Port COM8, 115200 baud. **One program per port**: close the serial monitor before running `serial_test.py`, and the other way round.
- One command per line (end with Enter / `\n`), at most 47 characters. Every command answers with a `[CMD] ...` line; an unknown one with `[CMD] unknown '...'`.
- Notes are MIDI numbers: 60 = C4 (middle C), 69 = A4 (440 Hz).
- Commands that load a patch or change the voice mode **rebuild the synth** (like leaving the menu): held notes are released, the screen goes back to the first page,
  and the audio stalls once for about 100-170 ms. The patch lives in RAM until the next reset.

Two ways to send commands:

```
C:\.platformio\penv\Scripts\pio.exe device monitor        # type commands by hand (never a bare "pio": see ../DEVELOPING.md)
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py --cmd "chord 3" --raw --seconds 5
```

`serial_test.py` opens the port without resetting the board and waits 1.5 s after each `--cmd`.

## Command reference

### Notes

| Command | Does |
| --- | --- |
| `on N` | starts note N (0..127); playing it again retriggers it |
| `off N` | stops note N (only notes started by `on` / `chord`) |
| `chord K` | releases everything, then holds K notes (0..48): C3 E3 G3 B3 D4 F4 A4 C5, then chromatic notes above C5 (C#5, D5, ...) for K > 8. `chord 0` = silence |
| `release` | stops every note started over serial |

### Patches and voice modes (each one rebuilds the synth)

| Command | Does |
| --- | --- |
| `patch startup` | the default patch: four oscillator engines (Karplus, Modal, Supersaw, Additive) into one filter, delay and reverb |
| `patch sampler F L` | one sampler into one filter, every effect off. F = file index from `samples`; L = loop mode: 0 as in the file, 1 off, 2 forward, 3 ping-pong |
| `patch para N V` | N Strng oscillators (1..8, saw, detuned, every third one an octave up) into one filter with Para on (ADR-041), V voices (1..8). Defaults: 4 and 4 |
| `patch strings` | switches the current synth to the Strings type (keeps its pages and row M's effects) |
| `mode mono [G] [L]` | Voices 1 = mono (Modular or FM, ADR-041). G = glide index 0..6 (Off, 25, 50, 100, 200, 400, 800 ms), L = legato 0 / 1. Omitted values stay as they are |
| `mode poly` | 8 voices when the synth was mono (a voice count above 1 stays) |
| `mode para [E]` | Para on the first filter of branch 1 (8 voices if it was mono). E = envelope policy of that Para point: 0 Legato, 1 Retrig, 2 Voice |
| `voices N` | voice count of Modular / FM, 1..8 (1 = mono; Strings has its own 1..32, set on its GENERAL page) |

### Sound settings (live, no rebuild unless noted)

| Command | Does |
| --- | --- |
| `flt T` | type of every FL module in the rack: 0 Off, 1 LP, 2 BP, 3 HP, 4 LP24, 5 Notch, 6 LP6, 7 Ladr, 8 ChLP |
| `eng a b c d` | engines of the first 1..4 oscillators that already use an engine (Wav set to an engine): 0 Karplus, 1 Modal, 2 FM2, 3 Fold, 4 Supersaw, 5 Vowel, 6 Additive, 7 Dust, 8 Strng. Plain-wave oscillators are skipped |
| `str F V` | Strings settings (rebuilds): `wave` 0 Saw / 1 Pulse / 2 Tri, `osc` 0 Naive / 1 Mip, `det` cents, `mix` 0..1, `lvl` 0..1, `lp` 0 / 1 (voice low-pass), `ftype` 0..8 (shared filter, same numbers as `flt`), `fx 0` = row M emptied (no effects) |
| `fx C [v0 .. v7]` | appends module C to row M (ADR-041: the effects are rack modules) with its values in the module's own units, as its page shows them (missing ones = defaults; rebuilds). C = the RACK tab's code: SA, FL, TR, EQ, RG (Ring / Shift), PH, FG (Flanger), CP, DL, RV, CH, SP (Spectral), CB (Cab), ES (Ensemble). Spectral values: Mode (0 Thru, 1 Freeze, 2 Gate, 3 Robot, 4 Whisper, 5 Pitch), Shft (semitones), Amt (0..1), Mix (0..1), Hold (0 / 1), Lo, Hi (Hz). Example: `fx SP 5 7` = a Spectral module pitching up a fifth. `fx clear` empties row M |

### Information and measurement

| Command | Does |
| --- | --- |
| `ping` | answers `[CMD] pong`: the board runs a firmware with commands |
| `status` | notes held by `chord`, uptime, free heap |
| `pieconv` | checks the Convolver's PIE dot products (`pie_conv2_s16`) against the C sums at full scale, 1..64 vectors, and prints the cost of a 128-tap call for both channels |
| `samples` | the sample catalog of the TF card: index, name, frames, rate, root note, loop points and mode (the index is the F of `patch sampler`) |
| `dump N` | captures the next N output samples (1..16384, before the dev output gain) and prints them as `[DUMP] @index v v v ...` lines, 32 per line, between `[DUMP] begin` and `[DUMP] end`. For offline spectrum checks |

### Key LEDs (HWV1)

| Command | Does |
| --- | --- |
| `leds N` | for 5 s only chain LED N (0..35) is lit, in red: to check the chain order |
| `leds all` | for 5 s every LED is lit in dim white |
| `leds off` | stops every transfer to the LED chain and blanks it (to measure the LEDs' effect on the audio) |
| `leds on` | resumes the transfers (the keys show the play feedback again) |

### Popups (UI test)

| Command | Does |
| --- | --- |
| `popup info` | a short note ("SD CARD / Card inserted") that goes after 2 s |
| `popup error` | an ERROR popup with OK; closing it prints `[CMD] popup answer yes` |
| `popup ask` | a Yes / No question (No focused); joystick left / right, push answers: `[CMD] popup answer yes` or `no` |

## What the board prints by itself

| Line | When | Says |
| --- | --- | --- |
| `[AUDIO] render avg ... worst ... budget ... blocks over budget ...` | every second (`HWV1_DEBUG_AUDIO`) | render time per block; a block over budget is a dropout only if the DMA ring runs dry: see the next line |
| `[AUDIO] DMA underruns N in the last second` | every second (`HWV1_DEBUG_AUDIO`) | **the real dropouts**: the I2S DMA ring (6 x 64 frames) ran empty and re-sent old data. Peaky loads (an STFT frame every 128 samples) can be over budget with 0 underruns; each graph rebuild gives about 10 |
| `[SEC] spectral: stft + frame work, analysis, resynthesis` | every second with `ENGINE_PROFILE`, while a Spectral FX slot runs | cycles per block inside SpectralFx (stereo / FX rack) |
| `[PROF] cycles per block (budget ...), total ...: Module=cycles(xinstances) ...` | every second (`ENGINE_PROFILE`) | cost per module type, summed over voices |
| `[OSC]` / `[SEC]` | every second | oscillator engines / reverb and delay stages, in cycles |
| `[HEAP] fast heap ... spilled ...` | every second | engine memory; "spilled" = module data that fell into the slower PSRAM |
| `[UI] events ..., slowest step ..., set_params ...` | every second while controls are used (`HWV1_DEBUG_UI`) | UI responsiveness |
| `[SD] ...`, `[SMP] ...` | card events, while streaming | card speed, reads, sampler underruns |
| `[KBD] key r4 c7 held at power-on` | boot | the keys-reset gesture was seen |

## serial_test.py

```
C:/.platformio/penv/Scripts/python.exe tools/serial_test.py [options]
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--cmd "..."` | | send a command first (repeat for several, sent in order, 1.5 s apart) |
| `--chords 1,3,6` | 1,3,6 | one measured phase per chord size, after an idle phase |
| `--idle S` | 4 | seconds measured with nothing held |
| `--hold S` | 6 | seconds measured per chord |
| `--settle S` | 2 | seconds skipped after each change (so note-on costs are **not** in the numbers) |
| `--engines "0,1,4,6;2,3,5,7"` | | measures each engine set: the first four oscillators are switched live, cost per engine and voice is printed |
| `--raw --seconds S` | 10 | just prints what the board sends for S seconds (after the `--cmd`s) |
| `--log file.txt` | | also writes every received line to a file (delete it afterwards, keep the numbers in [CONTINUE.md](../CONTINUE.md)) |
| `--port COM8 --baud 115200` | | |

The report ends with a table: notes held, cycles per block, % of the budget, render average / worst, blocks over budget.

## Examples

**Is the board alive, and which firmware does it run?**
```
serial_test.py --cmd ping --raw --seconds 3
```

**Play by hand** (in the monitor):
```
on 60
on 64
off 60
chord 3
release
```

**CPU of the default patch with 1, 3 and 6 notes:**
```
serial_test.py --cmd "patch startup" --chords 1,3,6
```

**How many voices fit: Poly with 4, then 8 voices:**
```
serial_test.py --cmd "patch startup" --cmd "mode poly" --cmd "voices 4" --chords 1,2,4
serial_test.py --cmd "voices 8" --chords 4,8
```

**Mono with glide and legato (then listen):**
```
mode mono 3 1          (100 ms glide, legato on)
on 48
on 55                  (glides up, the envelopes keep running)
off 55                 (back to the held note)
release
```

**Paraphonic: 8 oscillators on 4 voices, then each envelope policy:**
```
serial_test.py --cmd "patch para 8 4" --chords 1,2,4
mode para 1            (Retrig)    then: chord 3
mode para 2            (one amp envelope per voice)
```

**Compare the filter types by cost (4 voices):**
```
serial_test.py --cmd "patch startup" --cmd "mode poly" --cmd "voices 4" --cmd "flt 6" --chords 4 --hold 4
```
Repeat with `flt 1` (LP), `4` (LP24), `7` (Ladr), `8` (ChLP) and compare `Filter` in the modules line. To listen: `chord 3`, then `flt 7`, `flt 8`, ... while it plays.

**Cost of each oscillator engine:**
```
serial_test.py --cmd "patch startup" --engines "0,1,4,6;2,3,5,7" --chords 2
```

**Strings: 16 and 32 voices, naive vs mipmap oscillators, dry:**
```
serial_test.py --cmd "patch strings" --cmd "str fx 0" --cmd "str osc 1" --chords 8,16,32
str osc 0              (naive)    then the same chords again
```

**Sampler: list the card, play file 3 looped, measure card and CPU together:**
```
samples
serial_test.py --cmd "patch sampler 3 2" --chords 1,3,6,8 --hold 6
```
Over budget with no underruns = CPU limit; underruns with no over budget = card limit.

**Note-on cost (dropouts on played notes): notes every 100 ms while reading `[AUDIO]`.** `serial_test.py` skips the settle time, so use the monitor or a small
script that sends `off N` / `on N` in a loop and watches `blocks over budget`. Example result: 0 over budget after the 2026-10-06 fix.

**Spectrum / aliasing check of one note, dry:**
```
patch para 1 1
flt 0                  (no filter)
on 72
dump 8192              (copy the [DUMP] lines into a file, run a DFT offline)
release
```

**Check the LED chain and its effect on the audio:**
```
leds 0                 (the first LED of the chain lights red for 5 s; leds 35 = the last)
leds all
leds off               (measure while playing keys) ... leds on
```

**Back to the normal synth after tests:** `patch startup`, or reset the board.
