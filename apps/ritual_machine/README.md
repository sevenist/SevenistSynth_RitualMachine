# The Ritual Machine

A polyphonic synthesizer and sampler on one ESP32-S3, the first instrument built on the repo's library
([framework/README.md](../../framework/README.md)): a modular rack, a six-op FM synth and a 32-voice Strings synth in one section,
64 sample pads in the other, a 128 x 128 OLED, RGB keys. It runs on the first prototype (HWV1), on the second (HWV2, GRV-mini, its
128 x 160 colour screen) and in a desktop window that draws either board.

**Status: beta 1.1** (pre-release). The firmware for the first prototype and a Windows simulator are on the
[releases page](https://github.com/sevenist/SevenistSynth_RitualMachine/releases): unzip the simulator and play it from the PC keyboard.

## The instrument

Two sections, one switch away from each other: a synth and a sampler.

| | |
| --- | --- |
| **Modular rack** | Up to 16 modules chained freely in two parallel branches and a shared master row. Mono, paraphonic or polyphonic, up to 8 voices. |
| **Six-op FM** | DX7-style engine: 32 algorithms, 128 patches, every operator editable on the machine. Because why not. |
| **Strings** | Up to 32 voices. Detuned oscillator pairs, key-tracked low-pass, three-tap ensemble chorus on the bus. |
| **Pads** | 64 sample pads in 4 banks, 8 voices, streamed from the TF card. Loops with crossfade, choke groups, starts on the beat, three FX chains. |

- **Oscillators**: 6 classic waves, 9 engines written for it (Karplus string, modal, FM, wavefolder, supersaw, vowel,
  additive, dust, string machine) and 47 models from Mutable Instruments' Braids and Plaits.
- **Filters and shaping**: 9 filter types (12 / 24 dB, ladder, notch, all-pass ...), 10 saturation modes, a comb resonator.
- **Modulation, at audio rate**: LFOs, envelopes with curves, a multi-stage envelope, a 4-lane motion sequencer, velocity.
- **Effects**: delay, plate reverb, chorus, ensemble, phaser, flanger, tremolo, EQ, compressor, ring modulator,
  FFT spectral effects (freeze, gate, pitch, robot, whisper), cabinet convolver.
- **Sequencers**: 64 steps with chords, velocity, probability and ratchets for the synth; a step sequencer over all
  64 pads; 8 patterns and a chain for each; step and live recording.
- **Sample tools on the machine**: chop to pads, then trim, normalize, reverse, time-stretch, pitch, Paulstretch and
  lo-fi, with a version history to undo. **Capture** records whatever plays onto a pad: resample, mangle, repeat.
- **Hands on**: 8 macros, the joystick as an XY controller, Shift and Mod layers for every control, editable key
  layouts, presets, and a sound that comes back as you left it.

<!-- ![signal flow diagram](../../docs/images/signal_flow.png) -->
<!-- ![OLED screen pages walkthrough](../../docs/images/oled_pages.png) -->

## Build and run

From the repo root (the requirements: [DEVELOPING.md](../../DEVELOPING.md), "Requirements"; every option: [BUILDING.md](../../BUILDING.md)):

```powershell
.\build.ps1                    # the simulator, drawn as the desktop's panel
.\run.ps1                      # two windows: the screen and the front panel
.\build.ps1 -Board hwv1; .\run.ps1 -Board hwv1      # the same, drawn as the first prototype
.\test.ps1                     # every host test (this app's: tests/engine, tests/ui)
.\upload.ps1                   # build and flash the first prototype (-Env hwv2 for the second); never a bare `pio`
```

## Its documents

| Document | What |
| --- | --- |
| [docs/DEVELOPING.md](docs/DEVELOPING.md) | the developer guide: the controls, the folder map, the architecture, where to make a change, the tools, the firmware's flags and serial log |
| [docs/UI.md](docs/UI.md) | how the screens are built in the code, and where to cut before changing one |
| [docs/UX_REORGANIZATION.md](docs/UX_REORGANIZATION.md) | the GUI reorganization's plan (ADR-046): the UX rules, the views, the controls per board |
| [docs/SERIAL_COMMANDS.md](docs/SERIAL_COMMANDS.md) | the dev commands over the board's serial port, and the measuring tools |
| [docs/DESIGN.md](docs/DESIGN.md) | the app's decisions (ADRs): the rack, the sections, the sequencers, the GUI |
| [CHANGELOG.md](CHANGELOG.md) | what changed, release by release |
| [CONTINUE.md](CONTINUE.md) | the state, the todo and the history of the work |
| [assets/UI_Sprites/README.md](assets/UI_Sprites/README.md) | the sprites: how to paint them and how they get into the code |

The library under it (the engine, the boards, the drawing kit) has its own: [framework/README.md](../../framework/README.md).
