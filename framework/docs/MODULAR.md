# The modular catalog (`framework/modular`)

SynthLab's synths were each written as one long chain. The parts they share are now **modules**: small float structs that
anything can wire together. Wire them into a **patch**, play it with the **runner**, then turn the patch into a SynthLab synth
or a Ritual Machine engine graph. The editor is SynthLab Studio's PATCH tab ([apps/studio/README.md](../../apps/studio/README.md)).
The decisions behind it are [DESIGN.md](DESIGN.md) ADR-049.

| File | What it gives |
| --- | --- |
| `modular/mdsp.h` | the float DSP parts: oscillator shapes, the SVF, the biquad, a 3-band EQ, envelopes, noise, fast math (SynthLab's `lab_dsp.h` until 2026-11-14; `lab::` still names them) |
| `modular/modular.h` | what a module is: its description (`ModuleDef`: inputs, outputs, values, scope), the block it runs on (`Ctx`, `IO`) |
| `modular/modules.h` | the modules, header only: a generated synth uses them directly, also on the board |
| `modular/catalog.cpp` | their descriptions, and the catalog: `catalog()`, `find_module()` |
| `modular/patch.h`, `patch.cpp` | a patch (modules, cables, two areas), its text form, the runner |

## The modules

| Module | Where it comes from | Engine module | Inputs -> outputs |
| --- | --- | --- | --- |
| Note | the engine's NoteIn | NoteIn | -> Gate, Trig, Vel, Pitch (octaves over C4) |
| VoiceOut / Voices / Output | the engine's VoiceOut, BusIn, MasterOut | the same | the ends of the two areas |
| Osc | `mdsp.h` Osc (PolyBLEP) | Osc | Pitch, FM -> Out |
| Op | FM2's operators (phase modulation, feedback, phase reset at each note) | none yet | PM -> Out |
| Body | the Kick's oscillator (`kick.cpp`, `drumlab.cpp`); Steps Block: the Kick's pitch sweep | none yet | Pitch, Harm -> Out |
| Noise | `mdsp.h` Noise, with a low pass, a seed and two streams (B: the Kick's click) | Osc (noise wave), without Tone / Seed | -> Out |
| Metal | DrumLab's hat (six squares, or FM, then a band pass) | none yet | -> Out |
| Rumble | the Kick's rumble chain, whole, at 1/8 of the rate | none yet | Kick, Key, Vel -> Out |
| ADSR | `mdsp.h` Adsr | Eg | Gate -> Env |
| Perc | the Kick's envelopes: Law Blend (line to exponential) or Power (the Kick's amp); Steps Sample or Block | Eg (one-shot) | Trig -> Env |
| Ratchet | the Snare's wires (bursts, spacing, accel) | none yet | Trig -> Env |
| LFO | new | Lfo | Trig -> Out |
| Filter | `mdsp.h` Svf: LP / HP 12 and 24, BP, notch | Filter | In, Cutoff -> Out |
| EQ3 | `mdsp.h` Eq3 | none yet | In -> Out |
| Drive | the Kick's drive (tube, fold, sat, clip) | Shaper (one mode) | In -> Out |
| Comp3 | the Kick's 3-band compressor | none yet | In -> Out |
| Limit | the Kick's soft limit at the end | none yet | In -> Out |
| VCA, Mixer, Ring, Const | new | Vca, Mix4, Mult, Const | |
| Delay | the Kick's rumble echo | Delay (global only) | In -> Out |
| Room | the Kick's reverb | Reverb (a different one) | In -> Out |
| Glue | the Kick's master shaper | none yet | In -> Out |
| Gate | the Snare's gate | none yet | In, Key -> Out |

The "none yet" modules are what the engine lacks to play SynthLab's sounds: the Export > Engine report lists them for each patch.

## The Ritual Machine's modules

`modular/engine_adapter.h` adds the engine's own modules to the catalog: **34 entries**, named "RM" + the engine's name, each filed
under its kind (the catalog's own categories: a kind stays one group, the catalog's modules first, then the engine's):
- **Sources:** RMOsc, RMOscEngines (KARP, MODAL, FM2, FOLD, SSAW, VOWEL, ADD, DUST, STR), RMMiOsc, RMStrings.
- **Envelopes and modulators:** RMEnv / RMEnvG, RMEg, RMLfo.
- **Filters, shapers, utilities:** RMFilter, RMShaper, RMVca, RMMix4, RMMult, RMConst, RMTunedComb.
- **Effects, global and per-voice versions:** RMPhaser, RMFlanger, RMTremolo, RMCompressor, RMEq3, RMShifter (each with a V version),
  RMDelay, RMReverb, RMChorus, RMEnsemble, RMSpectralFx, RMVocoder.

`studio.exe --modules` lists them with their memory.

How the adapter works:
- **Each entry hosts the real engine module.** It is built in the runner's block with its own heap (measured once, when the catalog
  is filled). Its signals are converted between float and q15 each block, and its values are sent with `set_param` when they change.
  A voice's state is given as the engine gives it: pitch, gate, velocity, and a reset at the note's first block. So it sounds as in
  the Ritual Machine.
- **One entry for both scopes:** a type registered in both scopes is one entry; the VOICE area builds the Voice type, the FX area the
  Global one. A type the engine has in one scope only goes in both areas too. On the FX area, a Voice type gets the last note's
  state. In a voice, a Global type is copied per voice, with its memory each time. Export > Engine lists such a module as needed
  (the engine would need it in that scope).
- **Units:** values are the engine's own, except q15 levels (shown as %) and 1/256-semitone pitches (shown as semitones).
- **Inputs:** a `pitch` input follows the key unless a cable comes in (the patch's octaves become the engine's CV), and a `gate` input
  reads the voice's gate.
- **Exports:** Export > Engine writes them as themselves, in their scope. A generated SynthLab synth cannot hold them: they are not
  header-only.
- **Not hosted,** because they need what only the Ritual Machine gives:
  - the in / out ends (the patch's own stand for them);
  - Sampler, Granular and Pads (the card's samples);
  - MotionSeq (the step sequencer);
  - GateIn and ParaGate (every voice at once);
  - Convolver (its impulse response) and Dx7 (its FM patch), both sent by the app.

Checked:
- An RMOsc follows the key: C4 has twice C3's cycles.
- RMOscEngines through RMFilter sounds, and an RMReverb leaves a tail.
- A patch of them, exported, loaded into the real engine with every value and cable set, and played.

## SynthLab's synths as patches

A SynthLab synth can be rebuilt as a patch, so the PATCH tab can rewire it (`apps/studio/library/<synth>.patch`). Each rebuild is
checked with a **null test**: the synth (at its default values) and the patch play the same notes, and the depth of their difference
is measured (`studio.exe --null <synth>`; 60 dB and more: the same sound, SynthLab's Kick Lite rule). On 2026-11-14:

| Synth | Rebuilt from | Null depth |
| --- | --- | --- |
| Basic | Osc, two ADSRs, Filter, VCAs, the velocity's scale (Mixer + Const) | 74.3 dB |
| FM2 | two Ops, Perc (the index), ADSR, Rings, VCAs | 90.7 dB |
| Kick | Body, three Percs, Noise + band pass (the click), Rumble, Drive, Comp3, EQ3, Limit; Room and Glue on the sum | 71.6 dB |

To null, a module sometimes has to compute the way the synth does, not only sound alike. The Kick computes its envelopes once a block
and draws a line between (Perc and Body: Steps Block; 33 dB without, 72 with). Basic and the Kick set their filters from the
envelope at the block's start (the Filter's Cutoff input is read at its first sample; Basic: 53 dB with the block's mean, 74 with
the first sample). These rules are written in each module's comment.

## Rules

- **Signals** are float blocks of up to `mod::kMaxFrames` (64) samples, full scale ±1: audio, envelopes (0..1) and pitch
  (octaves) alike. A module's **values** arrive once a block in the units shown (Hz, ms, %, dB), already modulated.
- **Limits** are the engine's: 4 inputs, 8 outputs, 16 values; a scope (Voice, Global or Both). A module can therefore become an
  engine module one for one.
- **An unconnected input** reads its default: silence, 1 (a VCA's CV), the voice's gate (an ADSR), a pulse at the note's start
  (a Perc), or the module's first input (a Gate's key).
- **A cable to an input** carries the signal times its depth; several cables into one input are summed. **A cable to a value**
  (a modulation) moves the value along its range by the source's mean over the block, times the depth: depth 1 = the whole
  range. Audio-rate modulation goes through inputs (an Osc's Pitch and FM, a Body's Pitch, a VCA's CV).
- **Order and loops:** the runner plays a module after the ones it listens to. Where a loop closes, the cable reads the block
  before (as the engine's delayed edge).
- **Two areas:** VOICE (copied for each voice, Note -> ... -> VoiceOut) and FX (once, on the sum, Voices -> ... -> Output),
  the engine's scopes. A cable stays inside its area. Every module but those ends goes in either.
- **The FX area's note** is the last note played (`mod::LastNote` in `modular.h`): its pitch and velocity, a gate held while any key
  is held, and a start at each new note. So an ADSR, a Perc, a Body or a Note on the FX area plays like a mono voice on the sum. The
  voices write it and the FX reads it, on the audio thread. A stand-in until a mono mode is designed.
- **No allocation on the audio thread:** the runner builds a patch's modules in memory the caller gives
  (`Runner::bytes_for` says how much: a Delay alone is 128 KB).

## A patch as text (`.patch`)

```
patch "Hollow Kick"
node 1 voice Note 20 40                         id, area, module, x y (where the studio draws it)
node 2 voice Body 180 40 Tune=48 Harm=10        then its values by name (the ones not given: their defaults)
cable 1.Trig 3.Trig 1                           output -> input, depth
mod 3.Env 2.Level 0.5                           output -> value, depth (-1..1)
```

Lines it cannot read are skipped and counted, values are kept in their ranges, and a module in the wrong area is refused.

## Adding a module

1. Its struct in `modules.h`, with `start(const Ctx &)` and `process(const Ctx &, const IO &)`, and its values' indexes in `P::`.
2. Its description in `catalog.cpp`: name (no spaces), category, a one-line doc, scope, inputs with their defaults, outputs,
   values, the engine module that does the same (or `nullptr`), where it comes from, `MOD_OPS(Struct)`; add it to `kCatalog`.
3. `apps/studio/build/studio.exe --selftest`: every module is run on noise and checked to stay finite and bounded.
4. If the engine has an equivalent: its conversion in `apps/studio/src/export.cpp` (`engine_node`).

## From a patch to the Ritual Machine

The studio's Export > Engine writes the patch as C++ that builds an engine `GraphDesc`. Values are converted to the engine
modules' units (pitch × 256, q15 levels, ms × 16 ...), the original values are kept in comments, and the approximations are
named. The modules the engine lacks are listed with their values and cables: those are the modules to write. Checked on
2026-11-14: a patch whose modules all exist in the engine loaded into the engine and played (a 1 s note peaking at 22690 of
32767), and a drum patch's graph loaded with its six missing modules listed.

Export > Synth writes a SynthLab synth that calls the modules directly (no runner). It needs only these headers, so it builds
for the board as it is (checked with the ESP32-S3 compiler), and SynthLab's tests judge it like any other synth.
