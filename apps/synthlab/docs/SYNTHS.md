# SynthLab: the synth design notebook

Each synth of SynthLab gets an entry in this file **before** it gets code: the sound it is for, the algorithm, the parameters. The code
follows the entry (`src/synths/<name>.cpp`), and the entry keeps what we learned while tuning it. One synth is one sound, so the entries
stay short.

**The loop:** an idea (in "Ideas" at the end) → an entry, from the template below → the code → `tools/build_tests.ps1 <name>` (it
sounds, no NaN, it ends; a WAV in `build/wav/`) → play it in the simulator → tune → write down what was learned → on the board.

**The budget on the board (HWV2, ESP32-S3 at 240 MHz, 48 kHz):** the audio core has 160 000 cycles per 32-frame block for everything,
which is about 5 000 cycles per sample. With 6 voices and room for the rest, aim at **500 to 700 cycles per voice per sample**. That is
a few oscillators, one filter and the envelopes. A `sinf` or `expf` costs about 100 cycles: keep them out of the sample loop. The host's
time printed by the tests (µs per voice per ms of sound) compares synths with each other; it does not predict the board.

---

## Entry template

```markdown
## <Name>  (`src/synths/<file>.cpp`)

**Status:** idea | in progress | playable | tuned | measured on the board (<cycles per voice per sample>)

**The sound:** what it should sound like, in words and references (an instrument, a record, a machine).

**The algorithm:** the signal flow, in a line or a small diagram, and the trick that makes the sound.

    osc ──► filter ──► amp
     ▲        ▲         ▲
    pitch    env 2     env 1

**Parameters:**

| # | Name | Range | Default | Curve | What it does |
| --- | --- | --- | --- | --- | --- |

**Voice mode:** poly | mono (legato, glide)

**Notes:** what was tried, what worked, what to try next.
```

---

## Basic  (`src/synths/basic.cpp`)

**Status:** playable (the reference synth, and the one to copy)

**The sound:** a plain subtractive voice. It is here to check that the chain works end to end and to compare other synths against.

**The algorithm:** one oscillator (saw or square with PolyBLEP, a triangle, a sine) into a resonant low pass (TPT state-variable). One
envelope opens the filter by up to six octaves; another shapes the amplitude. The cutoff is computed once a block, from the filter
envelope's level at the start of the block.

```text
Osc (wave) ──► Svf low pass ──► × amp ADSR
                    ▲
     cutoff × 2^(F.Env × filter ADSR / 12)
```

**Parameters:**

| # | Name | Range | Default | Curve | What it does |
| --- | --- | --- | --- | --- | --- |
| 0 | Wave | Saw, Square, Tri, Sine | Saw | Int | the oscillator's shape |
| 1 | Cutoff | 40 Hz .. 16 kHz | 900 Hz | Exp | the filter's cutoff at rest |
| 2 | Reso | 0 .. 100 % | 30 % | Lin | resonance: Q from 0.5 to 10 |
| 3 | F.Env | 0 .. 72 st | 30 st | Lin | how far the filter envelope opens the cutoff |
| 4 | Attack | 1 ms .. 4 s | 4 ms | Exp | both envelopes |
| 5 | Decay | 5 ms .. 8 s | 400 ms | Exp | both envelopes (the filter's decays to 0) |
| 6 | Sustain | 0 .. 100 % | 50 % | Lin | the amplitude's sustain |
| 7 | Release | 5 ms .. 8 s | 300 ms | Exp | both envelopes |

**Voice mode:** poly

**Notes:** the first test run found the filter at Q = 50 (resonance 100 %) peaking at 9.6 × full scale. `lab::Svf` now stops at Q = 10.

---

## FM2  (`src/synths/fm2.cpp`)

**Status:** playable

**The sound:** two-operator FM: bells, electric piano, plucked metal, glassy keys. The attack is bright and the tail is pure.

**The algorithm:** a sine modulator, with feedback (averaged over two samples so it does not squeal), moves the phase of a sine carrier.
The modulation index falls with its own exponential decay, so the spectrum starts rich and ends as a sine. Both phases restart at every
note, so every attack is the same.

```text
Mod sine (ratio × f, feedback) ── × Index × index decay ──► phase of Carrier sine (f) ──► × amp ADSR
```

**Parameters:**

| # | Name | Range | Default | Curve | What it does |
| --- | --- | --- | --- | --- | --- |
| 0 | Ratio | 0.25 .. 16 | 2 | Exp | the modulator's frequency over the carrier's (whole numbers: harmonic; others: bells) |
| 1 | Index | 0 .. 12 | 4 | Lin | the depth of modulation, in radians (the brightness of the attack) |
| 2 | I.Decay | 5 ms .. 8 s | 300 ms | Exp | how fast the brightness goes |
| 3 | Feedbk | 0 .. 100 % | 0 % | Lin | the modulator on itself: towards a saw, then noise |
| 4 | Attack | 1 ms .. 4 s | 2 ms | Exp | |
| 5 | Decay | 5 ms .. 8 s | 1.2 s | Exp | |
| 6 | Sustain | 0 .. 100 % | 0 % | Lin | |
| 7 | Release | 5 ms .. 8 s | 400 ms | Exp | |

**Voice mode:** poly

**Notes:** the velocity scales the index as well as the level, so soft notes are darker. The first test run found `sin01` wrong for
negative phases, which deep modulation produces: fixed in `lab_dsp.h`.

---

## Kick  (`src/synths/kick.cpp`)

**Status:** playable, optimised (not tuned by ear yet)

**The sound (the user's brief, 2026-11-12):** a kick from a sine that morphs to a distorted sine, through several light distortions and
saturations, then straight into a 3-band compressor, then an EQ. A harmonics level morphs the sine to a triangle. Three envelopes:
pitch (with a Lin / Exp switch), harmonics, and one for the user to shape (here the amplitude). Free to add to the chain; macros group
the values under easier names. Then (same day): optimise it; add a noise generator with a constant seed (the same waveform at every
hit), a low pass, and a lightweight reverb ("really optimised, not necessarily good sounding"). Then: the noise is for **rumble**:
distortion before and after its filter, a cutoff of 20 .. 500 Hz, resonance if cheap ("a noisy rumble is better anyway"). Then: a
small delay on the rumble. Then: a master shaper on the output ("a kind of glue compressor and saturation and tone filter, keep
optimised as possible"). Then: a ducking value for the rumble. Then: a mix at the rumble's input, the kick's signal summed with the
noise into its chain. Then: a switch to put the reverb inside the rumble's chain or not.

**The algorithm:**

```text
sine ⇄ triangle (Harmonic + harm env) ──► sine ⇄ distorted sine (Shape) ──► + click (band-passed noise, 4 ms)
  ▲ pitch: Tune × 2^(Amount × pitch env / 12)                                      │
                                                                          × amp env (attack, hold, decay, curve)
                                                                                   │
  noise ─► Pre ┐
               + (In Mix) ─► filter 20..500 Hz (8 types, + env) ─► Post ─► × decay ─► [reverb] ─► echo ─► × duck ─► +
  osc ─► Pre ──┘   (the kick's oscillator, before the amp env)
                                                                                   ▼
Tube (asymmetric) ─► Fold (sine folder) ─► Sat (soft clip) ─► Clip (hard) ─► DC block
                                                                                   ▼
3-band comp: low < 120 Hz, mid, high > 2.5 kHz (the mid = the rest: the bands sum back exactly)
                                                                                   ▼
EQ: low shelf 90 Hz, bell at Mid Hz, high shelf 5 kHz ─► Level ─► soft limit ─► (the voice's end)
                                                                                   ▼
the voices' sum ─► [reverb] (4-line FDN, half rate, 16-bit lines; once, not per voice)
                   (Route: the reverb is on the output, or in the rumble's chain above)
                                                                                   ▼
MASTER: glue compressor ─► saturation ─► tilt (around 800 Hz) ─► output ─► the engine's master
```

- **Mono, one-shot:** a new hit chokes the last one, and the key's length does not matter. The old level fades in 1 ms under the new hit
  (no click). C3 plays the Tune; the keys transpose it.
- **The envelopes** are computed once a block, at its end (its start is the last block's end), and followed in straight lines in
  between: per-sample pitch, harmonics and level, with `fast_exp2` / `fast_log2`.
- **The noise and the click are seeded** from Seed at every hit, and their filters start from rest: each hit is the same waveform (two
  hits a second apart differ by 38 dB below the peak: the reverb's tail of the hit before). Another Seed, another noise.
- **The rumble:** the noise, Pre (a soft clip mixed in: denser, louder noise), a 12 dB/oct low pass (the state-variable filter: about
  ten operations a sample, resonance free, its coefficients computed on a change of value), its level kept as the cutoff moves (×
  √(Nyquist / cutoff)), Post (the same soft clip: the lows growl), then its own decay; it goes into the distortions with the body. Grit
  adds to Pre and Post, Tone moves the cutoff ± 1 octave. Measured alone (Level 100 %, the body cut): over 90 % of its energy under
  100 Hz, the growl from Pre / Post at −26 dB (default) to −21 dB (both at 100 %) above 500 Hz; Reso 100 % puts the peak on the cutoff.
- **The rumble's filter** (Filter, 2026-11-12: "a filter envelope for the rumble, and filter selection, all the filters we have"):
  LP 6 (a one-pole, the same trapezoidal form; Reso does nothing), LP 12 (the default: as before, bit for bit), LP 24 (two stages: the
  first with Reso, the second at Q 0.707, so the peak is Reso's and not its square), BP (normalised: 1 at its peak, whatever the Q),
  HP 12, HP 24, Notch (low + high), Peak (low − high: the lows and highs out of phase, a bump at the cutoff). All from the
  state-variable filter of `lab_dsp.h` (low, band and high pass in one run), a `switch` at each tick: no cost at 6 kHz. The level kept
  as Cutoff moves, by type: the low passes × √(Nyquist / cutoff) (LP 6 × 0.84: its noise bandwidth is 1.57 × the cutoff, against
  about 1.1 ×), BP the same ÷ √(1.57 k) (its bandwidth is the cutoff / Q), the high passes, Notch and Peak × 1 (most of the band passes).
  Measured (the rumble alone, Level 100 %, Cutoff 120 Hz, Reso 60 %, its RMS frequency 10–60 ms after the hit): LP 6 519 Hz, LP 12
  181, LP 24 130, BP 347, HP 12 1934, HP 24 1937, Notch 1886, Peak 1691; the levels within 1.5 dB of each other (early) and 2.6 dB
  (late). The high passes, Notch and Peak keep the noise up to the rumble's 3 kHz (its rate's limit), grainy above 500 Hz: a hiss
  more than a rumble, there for the sound design.
- **The filter envelope** (Env Amt, Env Atk, Env Dec): the cutoff × 2^(Amt × env), env up in a line over Atk, then down to 1 % at Dec;
  ± 5 octaves, held between 10 Hz and a third of the rumble's rate (2 kHz at 48 kHz). Computed once a block for its middle (a step
  every 0.67 ms), its `g` by `fast_tan` (no libm call; 1.5 % low at 2 kHz, exact under 500 Hz), one division (two for LP 6, LP 24, HP
  24); off at Amt 0, and once the noise has faded. The level correction stays the Cutoff's: the filter opening lets more noise through
  (an LP 12 opened by 3 octaves is louder), as on an analog synth. Measured (LP 12, as above): +3 oct, 181 → 525 Hz early, the same
  late (134 Hz: the envelope has gone); −2 oct, 109 Hz early. Host: no change at the defaults (Env Amt 0).
- **The rumble's echo** runs at an eighth of the sample rate (the rumble is low: 8 samples averaged in, a line between two ticks out),
  on a 16-bit line: 250 ms in 3 KB per voice, about 2 ns per sample on the host. It is per voice (the rumble is part of the voice, before
  the main chain), so the voice lasts until its echoes are down to −60 dB (5 s at most); its line keeps sounding across hits. Dly Mix at
  100 % leaves only the echoes: the rumble then starts one Delay after the hit (measured: silence, then the rumble at 120 ms for 120 ms).
  Measured with 150 ms and 60 % feedback: repeats at 150, 300, 450 ms, each about 3 dB down (the compressor lifts the tail a little).
- **The rumble's input** (In Mix) sums the noise and the kick: the kick's oscillator (with the click) before its amp envelope, times
  the velocity: a tone that follows the sweep and then holds the tune, as long as the rumble's Decay makes it (after the amp envelope
  it would end with the body, and Duck, keyed by the body, would take it away). Pre works on each at its own level (one soft clip
  each), then they are summed into the filter: the noise as it is, the kick divided by the level correction (the low pass hardly lowers
  it), so that both leave the filter at their own level. A DJ's crossfade (the user's wish: noise, kick and noise, or only kick):
  0 → 50 % the noise full and the kick coming in, 50 % both full, 50 → 100 % the kick full and the noise going out. Measured (the
  rumble alone, the chain linear, 0 / 25 / 50 / 75 / 100 %): the share of its energy at the tune (45–55 Hz) 17 / 47 / 69 / 87 / 99 %;
  the ends at the same level (−34.5, −34.0 dB), the middle 2.7 dB above them (both full). (It was an equal-power crossfade at first:
  each source at 71 % in the middle.)
- **The whole rumble runs at an eighth of the sample rate** (6 kHz at 48 kHz), once every 8 samples (`rumble_tick`): its sources,
  Pre, the low pass, Post, its decay, its reverb, its echo and its duck. Its low pass stops at 500 Hz, far under that rate's 3 kHz.
  The sample loop only averages the kick's oscillator into the next tick and draws a line between two ticks (about 5 operations a
  sample); the ticks start with each hit (the same waveform every hit). Before, the chain up to the decay ran at the full rate and the
  reverb and the echo had a decimation each. Host: the Kick 51.8 → 40.6 ns per sample (defaults), 56.5 → 42.1 ns (the rumble in full
  use); the rumble itself about 20 → 5 ns. Measured against the full rate (`kDecim = 1`, Hann-windowed bands of the rumble alone):
  within 1 to 2 dB up to 2 kHz, at default and at Pre and Post 100 %; 3 to 7 dB less from 2 to 4 kHz (the tick rate's limit, on what
  was already 30 to 50 dB under the rumble); no images to speak of around 6 kHz. (A first comparison without a window showed extra
  highs that were the window's leakage, not the sound: two filters added against them darkened the growl and were taken out.)
- **The reverb's Route:** Output (on the voices' sum, once: its tail outlives the hit), or Rumble (in the rumble's chain, after its
  decay; the output's reverb then does not run). In the rumble it comes after the decay because its input must be a burst: the noise
  and the kick's oscillator are continuous until the decay shapes them, and a reverb on steady noise is the same noise. Its tail is the
  rumble's (the voice lasts its Decay longer); Mix there goes from dry to wet. It is a second, smaller copy of the same reverb (the
  `Room` template), run at the rumble's eighth of the sample rate: 3.2 KB per voice. Measured (the rumble
  alone, Decay 1.5 s): without it the rumble is gone 0.6 s after the hit; with it at 100 %, the tail reaches −60 dB below its top at
  1.5 s, the level kept (its top −18 dB, the dry rumble's −21 dB). Host cost: the voice +3 ns per sample, the output's effect −4 ns
  (its reverb off).
- **The rumble's ducking** pulls it under the body, as a sidechain would: the key is the amp envelope (already there), +12 dB (so the
  rumble is all the way down while the body is within 12 dB of its top), followed (up at once, down at Duck Rel); the rumble × (1 −
  Duck × follower), after its echo. About 4 operations a sample. Measured (the chain made linear, the rumble alone: a render with it
  minus one without): Duck 100 %, the rumble at −90 dB for the first 75 ms (−20 dB without), back to −37 dB at 175 ms; Rel 400 ms,
  12 dB further down at 150 ms; Duck 50 %, 6 dB down under the body. (Keyed by the amp envelope as it is, a first version let the
  rumble back 2 to 4 dB under its full level from 75 ms, the body falling fast: hence the +12 dB.)
- **The master shaper** runs once, on the sum, after the reverb (in the Fx). **Glue**: a peak detector every sample; every 8 samples
  the gain for the level detected (a 6 dB soft knee, threshold 0 to −30 dB and ratio 1 to 4 from the one amount, a quarter of the
  reduction given back), followed in a line. The detector runs over the 8 samples before their gain is set: computed once a block from
  the level before it, the gain came 0.67 ms late and the hit passed with the makeup whatever the attack (measured: −5.2 dBFS at 1 ms
  attack, −8.8 now). **Sat**: `soft_clip(x g) / √g` (g up to 8), mixed by its amount: the body denser and louder, the peaks held near
  the bus's level (a first `/ soft_clip(g)` gave quiet parts ×4: +11 dB; a `/ g` held the peaks at −18 dBFS). **Tilt**: one one-pole
  low pass at 800 Hz, the lows and the rest weighted ±6 dB against each other. Each part is skipped at zero; about 11 ns per sample on
  the host, once for all voices.
  Measured (the test's phrase; peak / RMS / crest in dB): off −10.6 / −22.8 / 12.3; defaults −8.8 / −21.0 / 12.1; Sat 100 % −9.0 /
  −16.6 / 7.5; Glue 100 % fast (1 ms) −8.8 / −30.3 / 21.5, slow (30 ms) −5.0 / −24.7 / 19.7 (a slow attack lets the hit through: punch;
  at 100 % and 300 ms release the body is held well down); Tilt −100 %: +6 dB under 200 Hz.
- **The reverb** is the synth's Fx (lab.h): one copy, on the voices' sum, so its tail goes on after the hit. Four 16-bit delay lines
  (12.7 KB) fed back through a Hadamard matrix with a one-pole low pass in each loop, run at half the sample rate (a pair of samples in,
  a line between two ticks out). Lean, not lush.
- **Each distortion is mixed in by its amount** and normalised so full scale stays full scale: they are light, and they add up. Drive is
  how hard Tube and Sat are hit (Fold and Clip have their own gain).
- **The compressor's amounts** set each band's threshold (0 to −36 dB) and ratio (1 to 8) together, with an automatic makeup of 40 % of
  what the threshold takes. A band's gain is computed once a block from its detector (peak, Attack / Release) and moves in a line.

**Parameters** (72 rows, 12 of them section titles):

| Section | Name | Range | Default | Curve | What it does |
| --- | --- | --- | --- | --- | --- |
| MACROS | Punch | 0 .. 100 % | 50 | Lin | ± 12 st of pitch amount, pitch decay ÷/× 1.6, ± 40 % click, comp attack ×/÷ 2.8 (slower: the hit gets through) |
| | Body | 0 .. 100 % | 50 | Lin | amp decay ×/÷ 2.3, ± 60 ms hold, ± 6 dB low shelf, ± 30 % low-band comp |
| | Grit | 0 .. 100 % | 50 | Lin | ± 9 dB drive, ± 35 % shape, ± 30 % tube and sat, up to + 30 % fold (above 50 only) |
| | Tone | 0 .. 100 % | 50 | Lin | ± 30 % harmonic and harm env, ± 6 dB high shelf, ∓ 2 dB mid |
| OSC | Tune | 30 .. 150 Hz | 50 | Exp | the pitch at the end of the sweep, on C3 |
| | Shape | 0 .. 100 % | 15 | Lin | sine → distorted sine (a soft clip up to 7× gain) |
| | Harmonic | 0 .. 100 % | 0 | Lin | sine → triangle |
| | Click | 0 .. 100 % | 25 | Lin | noise around 3.2 kHz, 4 ms (seeded) |
| RUMBLE | Level | 0 .. 100 % | 20 | Lin | the rumble, before the distortions (at 100 % it peaks as high as the hit) |
| | In Mix | 0 .. 100 % | 0 | Lin | its input, a crossfade: 0 = the noise, 50 = both full, 100 = the kick's oscillator |
| | Decay | 5 ms .. 4 s | 300 ms | Exp | to 1 % |
| | Pre | 0 .. 100 % | 30 | Lin | distortion before the filter (Grit: ± 30 %) |
| | Filter | LP 6 .. Peak | LP 12 | Int | LP 6 / 12 / 24, BP, HP 12 / 24, Notch, Peak |
| | Cutoff | 20 .. 500 Hz | 90 Hz | Exp | the filter's cutoff (Tone: ± 1 octave); the level kept as it moves |
| | Reso | 0 .. 100 % | 40 | Lin | Q = 0.5 × 20^reso: 0.5 .. 10 (1.7 at 40 %); not in LP 6 |
| | Env Amt | −5 .. +5 oct | 0 | Lin | the filter envelope: the cutoff moved this far at its top (10 Hz .. 2 kHz) |
| | Env Atk | 0 .. 500 ms | 0 | Lin | its rise (0: open at the hit) |
| | Env Dec | 5 ms .. 4 s | 150 ms | Exp | back to the Cutoff (to 1 %) |
| | Post | 0 .. 100 % | 40 | Lin | distortion after the filter (Grit: ± 30 %) |
| | Delay | 10 .. 250 ms | 80 ms | Exp | the echo's time |
| | Feedbk | 0 .. 90 % | 35 | Lin | how much of each echo comes back |
| | Dly Mix | 0 .. 100 % | 30 | Lin | dry → echoes (100 %: only the echoes, the rumble late by one Delay) |
| | Duck | 0 .. 100 % | 50 | Lin | the rumble under the body (100 %: gone while the body is within 12 dB of its top) |
| | Duck Rel | 10 ms .. 1 s | 80 ms | Exp | how fast it comes back |
| | Seed | 1 .. 99 | 1 | Int | which noise: the same at every hit |
| PITCH ENV | Amount | 0 .. 60 st | 30 | Int | where the sweep starts, above the tune |
| | Decay | 2 ms .. 1 s | 45 ms | Exp | Lin: the time to reach the tune; Exp: the time to 1 % of the way |
| | Curve | Lin, Exp | Exp | Int | |
| HARM ENV | Amount | 0 .. 100 % | 40 | Lin | added to Harmonic at the hit |
| | Decay | 2 ms .. 2 s | 25 ms | Exp | to 1 % |
| AMP ENV | Attack | 0.1 .. 50 ms | 0.5 ms | Exp | a straight rise |
| | Hold | 0 .. 500 ms | 30 ms | Lin | |
| | Decay | 10 ms .. 4 s | 350 ms | Exp | to silence |
| | Curve | 0 .. 100 % | 60 | Lin | the decay's shape: (1 − x)^(1 + 7 × curve): 0 a line, 100 a fast drop and a long tail |
| DRIVE | Drive | 0 .. 24 dB | 6 | Int | into Tube and Sat |
| | Tube | 0 .. 100 % | 25 | Lin | asymmetric soft clip (bias up to 0.3): even harmonics |
| | Fold | 0 .. 100 % | 0 | Lin | sine wavefolder (gain 1 to 4) |
| | Sat | 0 .. 100 % | 35 | Lin | symmetric soft clip: odd harmonics |
| | Clip | 0 .. 100 % | 0 | Lin | hard clip, the threshold from 1 down to 0.25 |
| 3-BAND COMP | Low, Mid, High | 0 .. 100 % | 40, 25, 30 | Lin | each band's compression |
| | Attack | 0.1 .. 50 ms | 6 ms | Exp | the detectors' |
| | Release | 10 ms .. 1 s | 120 ms | Exp | |
| EQ | Low | ± 12 dB | 0 | Int | shelf at 90 Hz |
| | Mid | ± 12 dB | 0 | Int | bell (Q 0.9) |
| | Mid Hz | 100 Hz .. 8 kHz | 600 Hz | Exp | |
| | High | ± 12 dB | 0 | Int | shelf at 5 kHz |
| OUT | Level | −30 .. +6 dB | 0 | Int | before the soft limit |
| REVERB | Route | Output, Rumble | Output | Int | where the reverb is: on the output, or in the rumble's chain after its decay |
| | Mix | 0 .. 100 % | 15 | Lin | Output: the wet signal added; Rumble: dry → wet (0: the reverb does not run) |
| | Decay | 100 ms .. 4 s | 500 ms | Exp | to −60 dB |
| | Size | 10 .. 100 % | 60 | Lin | the lines' lengths: 5 to 83 ms at 100 % |
| | Damp | 0 .. 100 % | 50 | Lin | a low pass in the loop |
| MASTER | Glue | 0 .. 100 % | 30 | Lin | bus compressor: threshold 0 .. −30 dB, ratio 1 .. 4, soft knee, a quarter of the reduction back |
| | Attack | 0.1 .. 30 ms | 3 ms | Exp | fast: glue (the hit and the body evened); slow: punch (the hit passes) |
| | Release | 30 ms .. 1 s | 150 ms | Exp | |
| | Sat | 0 .. 100 % | 20 | Lin | soft saturation: the body denser and louder, the peaks rounded |
| | Tilt | −100 .. +100 % | 0 | Lin | around 800 Hz: − darker, + brighter (±6 dB on each side at the ends) |
| | Output | −12 .. +6 dB | 0 | Int | the last gain |

**Voice mode:** mono, no legato (each hit restarts). **PLAY:** a hit on every beat at 135 BPM (`Options::play_bpm`), the last note
played; a key changes the note it repeats. The engine's step sequencer plays it (two eighth steps, the note on the first) on the
machine clock, in the audio thread: measured from the sound, 7 hits in 3 s, the first at once, every one 444.4 ms after the last
within the 2 ms of the measure.

**Measured (the test's WAV, defaults, 2026-11-12):** the sweep starts near 280 Hz and settles on 50 Hz within about 50 ms; the peak is
−11 dBFS (−6 dBFS out of the engine's voice level); the tail is down 30 dB at 250 ms. No step at a hit that chokes a sounding one. The
host spends about 4× FM2's time per voice. That is fine for one mono voice; it still has to be measured on the board.

**Optimised (2026-11-12):**

| | Before | After (with the noise layer) |
| --- | --- | --- |
| Host, per sample of a sounding voice (`build_tests.ps1 bench`) | 60.7 ns | 32.8 ns, and 4.6 ns for the reverb (once for all voices) |
| ESP32-S3 render (`tools/esp32_report.ps1 kick`) | 1724 instructions; 41 `__divsf3`, 9 `exp2f`, 8 `powf`, 4 `expf`, 3 `sinf`, 3 `cosf`, `log10f` ... | 994; no call in the sample loop |

What did it: no division in the sample loop (`soft_clip` became a cubic; `1 / x` computed when a value changes); everything the values
decide computed in `update()` when one changes (a `memcmp` of the values per block), not every block; the envelopes once a block with
`fast_exp2` / `fast_log2`; the parts at zero skipped (the triangle, each distortion, the compressor's split, an EQ band at 0 dB, the
click and the noise once faded, the reverb at 0 %). The cubic `soft_clip` saturates a little earlier (flat from 1.5, not 3).

**Denormals (found with the rumble):** the states that decay with no input (the 1 ms declick, the filters' memories, the DC blocker,
the compressor's detectors) ended in the tiniest floats, where every operation is slow: 40 % of the Kick's time on the PC (71 ns per
sample, 42 ns with them flushed by the FPU). Now flushed once a block (`lab::flush`, below −300 dB to 0): 46 ns with the rumble. The
bench now warns when a synth is more than 25 % slower than with denormals flushed. (The ESP32-S3's handling of denormals is not known
here; the flush costs a few instructions a block either way.)

**Notes:**
- "User parametrable env" was read as the amplitude envelope. If it should be a free envelope with a chosen destination (drive, fold,
  the EQ ...), it is a small change: a destination row and an amount.
- First pass: the click at 1.5× was too quiet (1–5 kHz at −33 dB over the first 60 ms); it is now 3×.
- To try: the click as a short high sine rather than noise; a sub sine under the body; the compressor's crossover points as values.

---

## Kick Lite  (`src/synths/kick_lite.cpp`, presets in `kick_lite.h`)

**Status:** playable; null-tested against the Kick

**The brief (2026-11-12):** clone the Kick's engine, simplify its values to the most, optimise the algorithm as far as it cannot be
heard; presets to compare by rendering, under a null test (one render in reverse phase summed with the other, before and after the
optimisation).

**The values** (8; everything else fixed at the Kick's defaults):

| # | Name | Range | Default | The Kick's row |
| --- | --- | --- | --- | --- |
| 0 | Tune | 30 .. 150 Hz | 50 | OSC / Tune |
| 1 | Punch | 0 .. 100 % | 50 | MACROS / Punch |
| 2 | Body | 0 .. 100 % | 50 | MACROS / Body |
| 3 | Grit | 0 .. 100 % | 50 | MACROS / Grit |
| 4 | Tone | 0 .. 100 % | 50 | MACROS / Tone |
| 5 | Click | 0 .. 100 % | 25 | OSC / Click |
| 6 | Rumble | 0 .. 100 % | 20 | RUMBLE / Level |
| 7 | Room | 0 .. 100 % | 15 | REVERB / Mix |

**The presets** (Tune, Punch, Body, Grit, Tone, Click, Rumble, Room): Default (50 50 50 50 50 25 20 15), Techno Rumble (48 60 65 60 40
30 70 30), Hard Punch (55 85 35 80 70 60 10 10), Deep Sub (40 30 90 20 20 10 30 5), Gritty (52 70 50 100 60 45 40 20), Clean Short
(60 50 10 0 50 15 0 0).

**The null test** (`tests/test_null.cpp`, run by `build_tests.ps1`; `build_tests.ps1 null` alone): each preset rendered by the Kick
(the same values on its rows, the rest at its defaults) and by Kick Lite, a phrase of 7 hits (a soft one, a choke, three notes) and
the tails, through their Fx; the difference is the residual, its level under the signal the null depth. Each must reach 60 dB (a
residual far under what a mix lets through). The three renders of each preset, 32-bit float, in `build/wav/null/`.

**How it was made, in stages, each one null-tested:**

- *0. The clone*: the Kick's code, the 8 values mapped on its rows: identical bit for bit on every preset (the test and the mapping
  proved).
- *1. What the fixed values make dead or constant*, removed: the reverb in the rumble (Route: Output; 3.2 KB less a voice), the kick
  into the rumble (In Mix 0), the linear pitch curve, Clip, Tilt, the checks of values that cannot be zero (the compressor's bands, the
  echo, the duck, the glue). Still identical (to the float rounding: 175 dB and more). No time saved (those paths did not run).
- *2. Optimisations that may change the sound*, one at a time (host, ns per sample of the voice; null depths of the 6 presets):

| | Optimisation | Voice | Null (dB) | |
| --- | --- | --- | --- | --- |
| | none (stage 1) | 42.7 | 175 .. exact | |
| O1 | tube → fold → sat as one table, 512 steps over ±4 | 34.1 | 85 77 61 85 **40** 88 | Gritty fails |
| O1 | ... 1024 steps over ±2 | 32.7 | 96 84 82 86 64 88 | passes, little margin |
| **O1** | **... 2048 steps over ±2 (8 KB, shared)** | 32.9 | 97 84 87 86 75 90 | **kept** |
| O2 | soft_clip without a branch | 43.9 | exact | no gain: out |
| O3 | the compressor's detectors once a block (the block's peak) | 36.1 | ~20 | out |
| **O4** | **the triangle skipped once the harmonics envelope has gone** | 34.8 | 97 85 exact 87 exact 94 | **kept** |
| O5 | the rumble at 1/16 of the rate | 37.6 | 5 .. 21 | out (*) |
| O6 | the output's reverb at 1/4 of the rate | 39.1 (Fx −1.7) | 36 .. 59 | out |
| O7 | the compressor's detectors every 2 samples | 38.1 | 47 .. 57 | out |

(*) A noise made at another rate is another noise: the same spectrum, another waveform, which no null test can cancel. It may well be
inaudible; the null test cannot say so.

**Result (O1 + O4):** the voice 38.6 ns → 28.9 ns per sample on the host (−25 %), the Fx unchanged (10.5 ns); on the ESP32 the voice's
render 1651 → 1173 instructions, no call in its sample loop. Null depths: Default 96.7, Techno Rumble 83.7, Hard Punch 86.5, Deep Sub
86.2, Gritty 75.4, Clean Short 90.1 dB (residual peaks 64 to 89 dB under the signal's). Memory: a voice 7.2 → 3.9 KB, the table 8 KB
once. (The engine sizes every voice for the largest synth of the list: the saving shows on the board when the Kick is not in it.)

**On the board: an estimate, not a measurement** (2026-11-12). The budget is 160 000 cycles per 32-frame block (240 MHz, 48 kHz), the
one the Ritual Machine was measured against (CONTINUE.md, "Latest measurements": 30.1k with no note, 60.3k with one, 119.4k with three;
one voice of its startup patch ~24k; its reverb 20.5k, its delay 8.4k). Kick Lite's sample loop, read from the ESP32 compiler's
output: about 280 instructions on a typical sample (the rumble's tick spread over its 8 samples; about 335 with the EQ on), against
about 380 for the Kick; at 1.3 to 2.2 cycles an instruction (an in-order core, float chains: a guess, no float module has been
measured on the board), about 12k to 20k cycles per block for the voice, plus about 2.5k to 4k for the Fx: **9 to 15 % of the
budget** (the Kick: about 12 to 19 %). Under SynthLab's 500 to 700 cycles per voice per sample; 2 to 3 times TechnoGen's KICK target
(6k per block, apps/technogen/docs/DESIGN.md). To be measured: a cycle counter around the render in main_esp32.cpp.

**Where the time goes** (the Lite before stage 2, each part skipped in turn): the 3-band compressor ~16 ns, the oscillator ~12, the
distortions ~12, the final soft clip ~3, the rumble ~3, the rest ~5. The compressor is what is left to attack: its detectors are the
hard part (O3 and O7 failed).

---

## Snare  (`src/synths/snare.cpp`)

**Status:** playable (not heard by the user yet)

**The sound (the user's brief, 2026-11-13):** a snare drum: a noise burst with a ratchet-like envelope; a steady sine and a pitch-swept
sine (the resonance and the drum's skin); the same flavour of reverb, sine shaping and saturation as the Kick. Then (same day): mono
and Seed 0 confirmed, macros later; a 3-band EQ on the noise instead of its high and low pass, a 3-band EQ in the shaping chain, a gate
after the reverb. Then: a master chain as the Kick's, and the noise EQ's low and high bands made a high pass and a low pass.

**The algorithm:**

```text
skin: sine, Tune × 2^(Sweep × sweep env / 12) ─► Shape ─► × skin decay ─┐
ring: sine, Tune × Ratio (steady)             ─► Shape ─► × ring decay ─┼─► Fold (sine shaper) ─► Sat (soft clip, Drive) ─► DC block
wires: noise ─► NOISE EQ (HP, bell, LP) ─► × ratchet env ──────────────┘                                                    │
                                                                                     Level ─► soft limit ◄─ EQ (3 bands) ◄─┘
the voices' sum ─► reverb (the Kick's Room: a 4-line FDN at half rate, 16-bit lines) ─► gate (keyed by the dry sum or the output)
                   ─► MASTER (the Kick's: glue compressor ─► saturation ─► tilt ─► output)     (once, not per voice)
```

- **The ratchet envelope:** Ratchets bursts at the hit (1 to 8; 1 is one plain burst). Each burst jumps back to full and decays at
  Burst (to 1 %) until the next one comes, Spacing later; the last one decays at Decay (the tail). Accel makes each gap shorter (−100 %:
  half the gap before) or longer (+100 %: twice), at least 1 sample. The gaps are computed when a value changes; the sample loop counts
  down to the next burst. Measured (the noise alone, defaults 3 / 8 ms / 15 ms): bursts at 0, 7 and 15 ms, each about 15 dB down
  when the next comes, then the tail.
- **The skin:** the Kick's exponential pitch envelope (computed once a block with `fast_exp2`, in a line between): the hit starts Sweep
  semitones above the tune and gets 99 % of the way down in Sw Time. Measured (defaults): 270 Hz at the hit, 210 Hz at 4 ms, 181 Hz at
  25 ms, 180 Hz from 36 ms.
- **The ring:** a steady sine at Tune × Ratio (1.59 by default: a circular drum head's second mode over its first), no sweep, its own
  decay. The keys transpose both sines (C3 plays the Tune).
- **The noise's EQ** (NOISE EQ): a high pass, a bell and a low pass (the Biquad's: 12 dB / octave at Q 0.707, the bell at Q 0.9;
  `Biquad::set_highpass` / `set_lowpass`, new in `lab_dsp.h`), each skipped when it does nothing (HP at 20 Hz, the bell at 0 dB, LP
  at 20 kHz). Its level is kept as HP and LP move (× √(Nyquist / bandwidth), the bandwidth 1.11 × LP − HP, at least 300 Hz). Its
  history: first a high pass and a one-pole low pass, then (the user) three shelving bands, then (the user) the two shelves made a
  high pass and a low pass. Measured (the noise alone, the first 60 ms): RMS −28.4 / −28.9 / −28.2 dB for the three versions at their
  defaults; HP 4 kHz with LP 8 kHz −28.5 dB (the level kept); bands within 1 dB of the first version from 100 Hz to 8 kHz; the 12 dB
  low pass takes 3 dB more at 8 to 16 kHz, 16 dB more above.
- **The voice's EQ** (EQ): `lab::Eq3` (`lab_dsp.h`, new: a low shelf, a bell at Q 0.9, a high shelf, each skipped at 0 dB, its
  coefficients computed when a value changes), after the shapers and the DC block, before the level; ±12 dB, flat by default.
- **The gate** (in the Fx, after the reverb): a peak follower of its key, falling in 10 ms; open above Thresh, closed 3 dB under it
  (no chatter at the edge), held open Hold after the key falls; its gain goes to 1 in Attack and to 1 − Depth in Release (99 % of the
  way). Key Dry: the voices before the reverb, so the reverb's tail is cut once the hit has gone (the gated reverb); Out: the output, so
  the tail holds it open. At Depth 0 it does not run. Measured (the test's phrase, the 20 ms peaks after a hit): without it −41 / −47 /
  −52 dB at 250 / 300 / 350 ms; Dry (defaults) −44 / −90 / silent; Out −41 / −47 / −54, silent at 400 ms. The next hit's peak is
  unchanged (Attack 0.3 ms).
- **The master** is the Kick's (its code copied, the same defaults), after the gate: the last stage on the bus. Measured (the test's
  phrase): peak −11.2 → −9.4 dBFS, RMS −29.8 → −27.9 dB (the Kick's: −10.6 → −8.8 peak).
- **Seed:** 0 (the default) gives another noise at every hit, as a real snare does; 1 to 99 the same waveform at every hit (the filters
  start from rest), as the Kick's. (The noise EQ is linear: before or after the ratchet envelope is the same sound.)
- **The shapers are the Kick's**, each mixed in by its amount and normalised: Shape (a soft clip up to 7× on each sine, before its
  envelope, so the timbre stays as it decays), Fold (the sine wavefolder, gain 1 to 4, on the sum), Sat (the soft clip hit at Drive).
- **The reverb is the Kick's** (the same code, copied: one file per synth), as the synth's Fx: on the voices' sum, its tail outlives
  the hit. No master shaper (the Kick's glue / tilt) for now.

**Parameters** (57 rows, 10 of them section titles):

| Section | Name | Range | Default | Curve | What it does |
| --- | --- | --- | --- | --- | --- |
| SKIN | Tune | 100 .. 400 Hz | 180 | Exp | the skin's pitch at the end of the sweep, on C3 |
| | Level | 0 .. 100 % | 70 | Lin | |
| | Sweep | 0 .. 24 st | 7 | Int | where the hit starts, above the tune |
| | Sw Time | 2 .. 200 ms | 30 | Exp | 99 % of the way down |
| | Decay | 10 ms .. 1 s | 110 | Exp | to 1 % |
| RING | Ratio | 0.5 .. 4 × | 1.59 | Exp | its pitch over the tune |
| | Level | 0 .. 100 % | 35 | Lin | |
| | Decay | 10 ms .. 2 s | 220 | Exp | to 1 % |
| NOISE | Level | 0 .. 100 % | 55 | Lin | the wires |
| | Ratchets | 1 .. 8 | 3 | Int | bursts at the hit; the last one is the tail |
| | Spacing | 1 .. 50 ms | 8 | Exp | from one burst to the next |
| | Accel | −100 .. +100 % | 0 | Lin | each gap × 0.5 .. × 2 the one before |
| | Burst | 1 .. 100 ms | 15 | Exp | each burst's decay (to 1 %) |
| | Decay | 10 ms .. 2 s | 180 | Exp | the tail's (to 1 %) |
| | Seed | 0 .. 99 | 0 | Int | 0: free noise; 1 .. 99: the same at every hit |
| NOISE EQ | HP | 20 Hz .. 8 kHz | 1.2 k | Exp | high pass, 12 dB / octave (20 Hz: off); the level kept as it moves |
| | Mid | −24 .. +12 dB | 0 | Int | bell, Q 0.9 (the sizzle) |
| | Mid Hz | 200 Hz .. 12 kHz | 4 k | Exp | |
| | LP | 1 .. 20 kHz | 10 k | Exp | low pass, 12 dB / octave (20 kHz: off); the level kept as it moves |
| SHAPE | Shape | 0 .. 100 % | 20 | Lin | each sine → distorted sine |
| | Drive | 0 .. 24 dB | 6 | Int | into Sat |
| | Fold | 0 .. 100 % | 10 | Lin | sine wavefolder on the sum |
| | Sat | 0 .. 100 % | 30 | Lin | symmetric soft clip |
| EQ | Low | ±12 dB | 0 | Int | low shelf, on the whole voice after the shapers |
| | Low Hz | 40 Hz .. 1 kHz | 200 | Exp | |
| | Mid | ±12 dB | 0 | Int | bell, Q 0.9 |
| | Mid Hz | 100 Hz .. 8 kHz | 1 k | Exp | |
| | High | ±12 dB | 0 | Int | high shelf |
| | High Hz | 1 .. 16 kHz | 6 k | Exp | |
| OUT | Level | −30 .. +6 dB | 0 | Int | before the soft limit |
| REVERB | Mix | 0 .. 100 % | 20 | Lin | the wet added |
| | Decay | 100 ms .. 4 s | 700 | Exp | to −60 dB |
| | Size | 10 .. 100 % | 50 | Lin | the lines' lengths: 5 to 83 ms at 100 % |
| | Damp | 0 .. 100 % | 40 | Lin | a low pass in the loop |
| GATE | Depth | 0 .. 100 % | 100 | Lin | how far it closes (0: off) |
| | Key | Dry, Out | Dry | Int | what opens it: the voices before the reverb, or the output |
| | Thresh | −60 .. 0 dB | −40 | Int | it opens above (closes 3 dB under) |
| | Attack | 0.1 .. 20 ms | 0.3 | Exp | how fast it opens |
| | Hold | 0 .. 1000 ms | 80 | Lin | held open after the key falls |
| | Release | 5 ms .. 1 s | 50 | Exp | how fast it closes (99 %) |
| MASTER | Glue | 0 .. 100 % | 30 | Lin | bus compressor: threshold 0 .. −30 dB, ratio 1 .. 4, soft knee, a quarter of the reduction back |
| | Attack | 0.1 .. 30 ms | 3 ms | Exp | fast: glue; slow: punch (the hit passes) |
| | Release | 30 ms .. 1 s | 150 ms | Exp | |
| | Sat | 0 .. 100 % | 20 | Lin | soft saturation: the body denser and louder, the peaks rounded |
| | Tilt | −100 .. +100 % | 0 | Lin | around 800 Hz: − darker, + brighter (±6 dB on each side at the ends) |
| | Output | −12 .. +6 dB | 0 | Int | the last gain |

**Voice mode:** mono, no legato: a new hit chokes the last (the old level fades in 1 ms), as on a drum machine. **PLAY:** a hit on
every beat at 135 BPM, as the Kick.

**Measured (2026-11-13):** the test's WAV peaks at −11.2 dBFS (the Kick: −8.7); the skin leads the hit, the noise peaks about 5 dB
under it. Host 23.7 ns per sample of a sounding voice (the Kick about 40), the reverb 5.0 ns once for all voices. ESP32-S3 (from the
compiler): the voice's render 669 instructions (the Kick's 2193), no call in its sample loop. A voice is under 0.5 KB; the Fx
12.7 KB (the reverb). With the two EQs and the gate (at their defaults: the noise EQ's two shelves on, the voice's EQ off, the gate on):
host 25.6 ns the voice, 6.8 ns the Fx; ESP32 render 784 instructions, the Fx's 406, still no call in either sample loop. With the
master and the high / low pass: host 23.0 ns the voice, 14.2 ns the Fx (the master about 7); ESP32 783 and 662 instructions, no call
in either sample loop.

**Notes:**
- Mono and Seed 0 confirmed by the user; macros later (Snap: noise level and burst shorter; Body: the sines' decays; Tone: the EQs).
- To try: a Q for the noise EQ's mid (a narrow, resonant band on the wires), a resonance on its high pass.

---

## DrumLab  (`src/synths/drumlab.cpp`)

**Status:** playable (not heard by the user yet)

**The brief (the user, 2026-11-13):** "a DrumLab page with kick and snare parts mixed together", then, through their answers: not the
two lists side by side but the **modules merged** ("the skin module is shared etc."): one drum synthesizer for all kinds of
percussion, **one long chain** with bits of the Kick and the Snare. Their choices: all four sources (the body, the ring, the noise with
its ratchet, and a new metal source with both kinds, a Type switch); the Kick's rumble as a module; the keys play notes as on the other
synths; PLAY loops **preset grooves** (four on the floor, backbeat, offbeat / 16ths, roll / fills); its own values (not shared with the
Kick and the Snare synths); the list raised from 80 to 128 rows. Macros later.

**The algorithm:**

```text
BODY: sine ⇄ triangle (Harmonic + harm env) ⇄ distorted sine (Shape), pitch env (Lin/Exp) ─► × amp env × Level ─┐
RING: steady sine at Tune × Ratio ─► Shape ─► × its decay ───────────────────────────────────────────────────────┤
NOISE: noise ─► HP / bell / LP ─► × ratchet env (1..8 bursts, then the tail) ────────────────────────────────────┤
METAL: 6 squares (the classic hat's ratios, Ratio spreads them; Index: sum → products of pairs)                  │
       or FM (carrier Tune, modulator × Ratio, Index falling with the decay) ─► band pass (2 stages) ─► × decay ──┤
RUMBLE (the Kick's, 1/8 rate): noise + body ─► Pre ─► filter (8 types, env) ─► Post ─► decay ─► [reverb] ─► echo ─► duck ─┤
                                                                                                                 ▼
DRIVE: tube ─► fold ─► sat ─► clip ─► DC block ─► 3-BAND COMP ─► EQ (low shelf, bell, high shelf, each with its Hz) ─► Level ─► soft limit
                                                                                                                 ▼
the voices' sum ─► REVERB (Route Output) ─► GATE (keyed Dry or Out) ─► MASTER (glue ─► sat ─► tilt ─► output)
```

- **What comes from where:** the body, its three envelopes, the rumble, the drive, the 3-band compressor, the reverb (both routes) and
  the master are the Kick's code (without its macros); the ring, the noise with its ratchet and its HP / bell / LP, and the gate are
  the Snare's; the EQ is the Snare's (`lab::Eq3`, with a frequency for each band); the metal is new. The Kick's click became the noise
  (its default: one burst of 18 ms, HP 1.5 kHz, +6 dB at 3.2 kHz, LP 6 kHz); the Snare's skin became the body (Tune, Pitch Amount and
  Decay; its decay is the amp envelope's). Seed is shared by the noise and the rumble (1 by default, as the Kick: the same waveform at
  every hit; 0: another at every hit).
- **The metal:** Type Squares: six naive square waves (their aliasing is noise among noise, under a band pass) at the classic analog
  hat's ratios (205.3, 304.4, 369.6, 522.7, 540, 800 Hz over the lowest: 1, 1.48, 1.80, 2.55, 2.63, 3.90), Tune the lowest, Ratio
  spreading them (1 + (r − 1) × Ratio), starting at fixed phases a golden ratio apart (all at 0, they jumped together: a click at the
  hit, 4 dB of peak); Index crossfades their sum to the products of three pairs (a ring modulation: a harsher, cymbal-like metal). Type
  FM: a sine at Tune phase-modulated by one at Tune × Ratio, the depth Index × 8 rad × the envelope (bright at the hit, pure as it
  decays). Both through a band pass at Tone, two stages (12 dB / octave each side; one stage let the squares' fundamentals, 4 octaves
  under a hat's Tone, outweigh their highs: the energy sat at 500 Hz – 2 kHz), times 3.5. Measured (the metal alone, the test's phrase):
  Squares, Tune 205 Hz, Tone 8 kHz, Decay 60 ms: peak −11.6 dBFS, its energy at 2 to 12 kHz (500 Hz – 2 kHz −11 dB, under 500 Hz
  −25 dB and less), 7 dB under the noise at 100 % with HP 7 kHz; FM, Tune 560 Hz, Ratio 1.48, Index 60 %, Tone 1.5 kHz, Decay 400 ms (a
  bell): peak −8.7 dBFS, its energy around the carrier.
- **The grooves** (`lab::Options::grooves`, new in `lab.h`, played by `synth.cpp` through the engine's step sequencer: no change in the
  framework): the GROOVE section's rows are values like the others, read by the app. Four (every beat, the one accented), Backbeat (2
  and 4, a ghost note before the one), Offbeat (the eighths between the beats), 16ths (accents on the beats), Broken (a broken beat),
  Roll (2 bars of 32nds: quarters, eighths, 16ths, 32nds, the velocity rising from 40 to 127), Fill (a bar of 16ths falling from +12 to
  −5 semitones: a tom fill). On the note last played (a key changes it from its next hit). Tempo and Swing act at once; a new Groove
  starts at the end of the playing one (the sequencer's queue). Tested through the engine (`tests/test_app.cpp`, measured from the sound):
  Offbeat at 120 BPM hits at 250, 750, 1250, 1750 ms; Tempo 100 while it plays: a hit every 600 ms; Groove → Four while it plays: the
  offbeats to the bar's end, then a hit on every beat.
- **Mono, one-shot** (a new hit chokes the last, the old level fading in 1 ms), as the Kick and the Snare.

**Measured (2026-11-13):** at its defaults it is the Kick: the test's WAV peak −8.8 dBFS and RMS −20.5 dB, both the Kick's, the spectrum
within 4 dB in every band (the click from the noise, not the Kick's band-passed noise). The Snare's defaults set on its rows (Tune 180,
Pitch 7 st / 30 ms, Amp Decay 220 ms, Ring 35 %, Noise 55 % with 3 ratchets, HP 1.2 k / LP 10 k, the rumble and compressor off,
reverb 20 % / 700 ms): peak −9.4 dBFS as the Snare, RMS 0.8 dB above, the bands within 2 dB. Host 39 to 46 ns per sample of a sounding
voice (the Kick 38.6), its Fx 11.7 ns. ESP32-S3 (the compiler): the render 2818 instructions (the Kick's 2193), its 4 divisions once a
block (the filter envelope's coefficients, as the Kick's), none in the sample loop; the Fx 662. **Memory:** a voice is 7.7 KB (the
Kick's 7.2), the Fx 12.9 KB; the engine makes 6 voices the size of the largest synth: about 46 KB + 12.9 KB, near the 64 KB of fast
RAM (main_esp32.cpp; beyond it the heap spills into PSRAM). A mono synth needs one voice: loading it with one would save 38 KB.

**Parameters** (104 rows, 17 of them section titles). The rows of the Kick and the Snare keep their ranges and meanings (the tables of
the Kick and the Snare above); the defaults are the Kick's. What is new or different:

| Section | Name | Range | Default | Curve | What it does |
| --- | --- | --- | --- | --- | --- |
| GROOVE | Groove | Four, Backbeat, Offbeat, 16ths, Broken, Roll, Fill | Four | Int | what PLAY loops |
| | Tempo | 60 .. 200 BPM | 130 | Int | live |
| | Swing | 0 .. 75 % | 0 | Lin | the off steps late by this much of a step |
| BODY | Tune | 20 Hz .. 1 kHz | 50 | Exp | the end of the sweep, on C3 (a kick 50, a snare 180, toms 80 .. 300) |
| | Level | 0 .. 100 % | 100 | Lin | the body (0: a voice of ring, noise, metal and rumble only) |
| | Shape | 0 .. 100 % | 15 | Lin | the body and the ring |
| RING | Level | 0 .. 100 % | 0 | Lin | off by default |
| NOISE | Level / Ratchets / Decay | | 25 % / 1 / 18 ms | | the Kick's click; Decay from 1 ms |
| | Seed | 0 .. 99 | 1 | Int | the noise's and the rumble's |
| NOISE EQ | HP / Mid / Mid Hz / LP | | 1.5 k / +6 dB / 3.2 k / 6 k | | the click's band |
| METAL | Type | Squares, FM | Squares | Int | |
| | Level | 0 .. 100 % | 0 | Lin | off by default |
| | Tune | 50 Hz .. 4 kHz | 205 | Exp | Squares: the lowest of the six; FM: the carrier (on C3) |
| | Ratio | 0.25 .. 4 × | 1 | Exp | Squares: their spread (1: the classic ratios); FM: the modulator over the carrier |
| | Index | 0 .. 100 % | 30 | Lin | Squares: sum → ring-modulated pairs; FM: depth 0 .. 8 rad, falling with the decay |
| | Tone | 500 Hz .. 16 kHz | 8 k | Exp | the band pass's centre (2 stages, Q 0.87 each) |
| | Decay | 5 ms .. 2 s | 80 | Exp | to 1 % |
| RUMBLE | (the Kick's, without its Seed) | | | | |
| EQ | Low Hz / Mid Hz / High Hz | | 90 / 600 / 5 k | | the Kick's bands, now movable |
| GATE | Depth | 0 .. 100 % | 0 | Lin | off by default (the Kick's sound) |

**Voice mode:** mono, no legato. **PLAY:** the Groove at the Tempo (the bar shows it).

**Notes:**
- To try: macros (later, the user's wish); presets of sounds (kick, snare, toms, clap, closed / open hat, cowbell) on a row that sets
  the others; a choke group so an open hat stops on a closed one (it needs two sounds at once: today one voice plays); one voice for a
  mono synth (memory).

---

## Ideas

Not decided: what to try next, one line each. Move one up into an entry when we start on it.

- (none yet)
