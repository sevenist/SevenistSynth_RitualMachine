# Update log: SynthLab

The library's changes: [framework/CHANGELOG.md](../../framework/CHANGELOG.md).

## 2026-11-12: the rumble's filter: 8 types and an envelope

- **Kick**: the rumble's filter has a type (Filter: LP 6, LP 12, LP 24, BP, HP 12, HP 24, Notch, Peak) and an envelope (Env Amt in
  octaves, up or down; Env Atk; Env Dec). The level stays even from one type to the next. The defaults (LP 12, no envelope) sound
  exactly as before; Kick Lite does not change.

## 2026-11-12: Kick Lite, a null-tested clone of the Kick

- **Kick Lite**: the Kick with 8 values (Tune, Punch, Body, Grit, Tone, Click, Rumble, Room) and six presets, a quarter cheaper (on the
  PC 38.6 to 28.9 ns a sample). It sounds the same: each preset rendered by both synths and subtracted leaves a difference 75 to 97 dB
  under the sound.
- **For developers:** the null test (`tests/test_null.cpp`) renders Kick Lite's presets with both synths and checks the difference;
  the three renders of each are in `build/wav/null/`.

## 2026-11-12: the rumble's In Mix, a crossfade

- **Kick**: In Mix is a DJ's crossfade: the noise alone at 0 %, the noise and the kick both at full level at 50 %, the kick alone at
  100 % (it was an equal-power mix, each at 71 % in the middle).

## 2026-11-12: the Kick's rumble at an eighth of the sample rate

- **Kick**: the whole rumble (its sources, filter, distortions, decay, reverb, echo, duck) runs at 6 kHz, as its low pass never goes
  above 500 Hz: the Kick costs about a fifth less (on the PC, 52 to 41 ns a sample), the rumble a quarter of what it did. It sounds
  the same up to 2 kHz (measured within 1 to 2 dB); above, a little less of what was already 30 dB down.

## 2026-11-12: the Kick's reverb on the output or in the rumble

- **Kick**: the reverb's **Route**: on the output (as before), or inside the rumble's chain, after its decay, so that its tail becomes
  the rumble's tail. There, Mix goes from dry to wet.

## 2026-11-12: PLAY loops the Kick at 135 BPM

- **Kick**: PLAY starts a hit on every beat at 135 BPM, and stops it; a key changes the note it repeats; the bar shows "135 BPM".
  The timing is the engine's: its step sequencer plays the hits on its clock, on time to the sample.
- **For developers:** `lab::Options::play_bpm` gives any synth that PLAY (0: PLAY holds the note, as before).

## 2026-11-12: the kick into the rumble

- **Kick**: the rumble's input can be the noise, the kick, or a mix of both (**In Mix**): at 100 % the rumble is a tone at the kick's
  tune, distorted, filtered, echoed and ducked like the noise, as long as the rumble's Decay. The section is now called **RUMBLE**.

## 2026-11-12: ducking for the Kick's rumble

- **Kick**: the rumble ducks under the body (**Duck**, **Duck Rel**): at 100 % it is gone while the body is loud and comes back as it
  falls, like a sidechain. It costs almost nothing: the key is the body's own envelope.
- **For developers:** `-Values` takes up to 64 values and refuses more (it kept 16 and dropped the rest without a word).

## 2026-11-12: a master shaper on the Kick

- **Kick**: a **MASTER** section after the reverb, on the sum of the voices: a glue compressor (**Glue**, **Attack**, **Release**), a
  soft saturation (**Sat**: denser, not just louder), a tone tilt around 800 Hz (**Tilt**: darker or brighter), and **Output**. About
  11 ns a sample on the PC, once for all voices; every part is skipped at zero.
- A synth can have up to 80 rows (its values reach the engine in several commands when they do not fit in one).
- **For developers:** `LAB_COLD` (lab_dsp.h) keeps a synth's `update()` out of its render.

## 2026-11-12: an echo on the Kick's rumble

- **Kick**: the rumble has a short **echo** (Delay 10 to 250 ms, Feedbk, Dly Mix). At 100 % mix only the echoes remain, so the rumble
  starts one Delay after the hit. It runs at an eighth of the sample rate, which the rumble's low notes allow: 3 KB, about 2 ns a sample.
- **For developers:** `-Values` also takes `SECTION/name` (`"AMP ENV/Decay=10"`) and keeps values in their ranges.

## 2026-11-12: the Kick's noise becomes a rumble; denormals

- **Kick**: the noise layer is now a **rumble**: distortion before (**Pre**) and after (**Post**) a resonant low pass (**Cutoff** 20 to
  500 Hz, **Reso**), louder (at 100 % it peaks as high as the hit), with a longer default decay. Grit drives Pre and Post.
- The Kick spent 40 % of its time on **denormals** (tiny numbers left by the states that decay); they are now cleared once a block
  (`lab::flush`, and `flush()` on the filters), in Basic and FM2 too. The test's bench warns when a synth is slowed by them.
- **For developers:** `build_tests.ps1 <synth> -Values name=value,...` renders and times a synth with other values.

## 2026-11-12: the Kick optimised; a seeded noise layer and a reverb

- **Kick**: about half the time per sample it took (on the PC: 60.7 to 32.8 ns, with the new noise layer), and on the ESP32-S3 no
  division and no library call left in its sample loop (there were 41 calls to the float division). A **noise layer** with a low pass
  and its own decay, seeded at every hit like the click (the same waveform each time; **Seed** picks another), and a small **reverb**
  (Mix, Decay, Size, Damp) after the voices, so its tail outlives the hit.
- A synth can have **an effect on the sum of its voices** (`lab.h`, `Fx`, `make_synth<Voice, Fx>`), run once by a second engine module.
  A synth can have up to 60 rows.
- **For developers:** `lab::fast_exp2`, `fast_log2`, `fast_db`, `fast_to_db`; `lab::soft_clip` is a cubic without a division (it flattens
  from 1.5 instead of 3). The tests check each Fx (no NaN, no blow-up at any value's min or max, the tail dies away), pass the WAVs
  through it, and time each synth (`bench`). `tools/esp32_report.ps1 <synth>` lists what the chip's compiler makes of a render.

## 2026-11-12: the Kick, sections and macros

- **Kick**: a sine that morphs to a triangle and to a distorted sine, with pitch (Lin / Exp), harmonics and amplitude envelopes, a click,
  four light distortions (tube, fold, saturation, clip), a 3-band compressor and a 3-band EQ. Four macros at the top, Punch, Body,
  Grit and Tone, each move several values at once. It is mono: a new hit chokes the last one without a click.
- The list can have **sections**: a title row that encoder A steps over. A synth can have up to 48 rows.
- **For developers:** the values travel to the voices in one engine command (`set_blob`, `lab::LabValues`) instead of one parameter
  each; `lab::section`, `lab::Biquad` (EQ shapes) and `lab::DcBlock` are new. The test's WAV plays a mono synth's hits on one voice.

## 2026-11-12: SynthLab, a workbench for home-made synths (branch `synthlab`)

- **SynthLab**, a third app on the library: a collection of small synths, each one file of float C++ (a parameter list and a voice).
  The screen is a list of the synth and its values: encoder A picks the row, encoder B changes it. Its push resets the value to its
  default. PLAY holds a note so values can be turned while it sounds. Two synths to start with: **Basic** (an oscillator, a resonant
  filter, two envelopes) and **FM2** (two operators with feedback). Made for the second prototype; runs in the simulator.
- **For developers:** the synth API is in `src/lab.h` and the DSP parts are in `src/lab_dsp.h`. One engine module (`src/lab_voice.cpp`)
  plays the selected synth on every voice, so changing synth needs no graph reload. The host tests check every synth of the list without
  changes (it sounds, no NaN at any value's min or max, it ends after release) and write a WAV of each. The design notebook is
  `docs/SYNTHS.md`.
