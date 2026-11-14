# The framework: SynthCore, the library

What every app of this repo is built on, in `framework/`: the DSP engine, the hardware abstraction layer (HAL) and one
implementation of it per board, the drawing kit over u8g2, the desktop helpers and the library's own tests. An app (`apps/<name>/`)
brings its own screens, controls' roles and data, and links this. The repo's layout, the builds and the tools common to every part
are in [DEVELOPING.md](../DEVELOPING.md); a new app starts from the template ([apps/template/README.md](../apps/template/README.md)).

| Folder | What | Included as |
| --- | --- | --- |
| `engine/` | the DSP engine (C++17; includes nothing from an app): the graph of modules, the modules, the sampler, the step sequencers | `"engine/..."` |
| `hal/` | what a board gives an app: display, controls, key LEDs, the card's small files, power, large memory, the audio output. Headers only | `"hal/..."` |
| `ui/` | the drawing kit: style, layout, text, sprites, animations, popups, the colour canvas, widgets for a mouse and a keyboard | `"ui/..."` |
| `modular/` | the modular catalog: float modules from SynthLab's synths, patches of them, their runner (docs/MODULAR.md) | `"modular/..."` |
| `boards/` | the HAL for each board: `esp32/` (what the ESP32 boards share), `hwv1/`, `hwv2/`, `desktop/` | `"boards/..."` |
| `host/` | desktop-only helpers (a slow-card model for tests and simulators, the .wav / .mp3 converter) | `"host/..."` |
| `tests/`, `tools/` | the library's own tests (the engine alone); table generators, sample tools, the shared desktop build | |

## Its documents

| Document | What |
| --- | --- |
| [docs/DESIGN.md](docs/DESIGN.md) | the decisions (ADRs) of the engine, the boards and the repo, the budget, the guidelines; the index of every ADR of the repo |
| [docs/BOARDS.md](docs/BOARDS.md) | the boards: the desktop board of the simulator, the two prototypes, the ESP32 firmware, its tasks and performance rules |
| [docs/DRAWING_KIT.md](docs/DRAWING_KIT.md) | putting something on the screen from any app, with `ui/` |
| [docs/MODULAR.md](docs/MODULAR.md) | the modular catalog: its modules, patches, the runner, adding a module, the exports |
| [CHANGELOG.md](CHANGELOG.md) | what changed in the library and the repo |
| [../CONTINUE.md](../CONTINUE.md) | the library's state and todo, the working style and the traps (each app keeps its own) |

## The rules that keep it portable

1. `core/` only includes `hal/*.h`, `ui/*.h`, `u8g2.h` and the C standard library; `app/` the same plus `core/*.h`. `core/` includes nothing from `app/`. Anything platform-specific goes behind a HAL function. The library's `hal/`, `ui/` and `boards/` include nothing from an app: what a board needs from one is handed over (the render function, the card's folder list, the boot animation's frames, the card task's hooks).
2. `engine/` (the library, `framework/engine`) includes nothing from `core/`, `hal/` or the platform, nor from any app. `platform/engine/` is the only bridge between the two.
3. Every file under `platform/<x>/` and `framework/boards/<x>/` is wrapped in its guard, because PlatformIO compiles all of the app's `src/` and of the board's folder.
4. The display is created by the board (`display_init()`), then handed to the app: `app_init(&app, display_init())`.
5. HAL headers have `extern "C"`, so C core code links from C++ files. `core/*.c` and `app/*.c` stay C (they use C designated initializers).
6. In the engine, nothing on the audio path allocates, locks, logs or uses `float` (see the guidelines in framework/docs/DESIGN.md).

## The engine in short

Everything is a graph of modules compiled into a flat plan: `NoteIn` per voice, any number of modules joined by **cables with a
signed depth** (any output can drive any input or any parameter; there is no distinction between audio and control, so audio-rate
FM / AM / filter FM are just cables), `VoiceOut` summing the voices, then global modules (effects). All signals are q15 blocks of
`ENGINE_BLOCK` (32) frames at `ENGINE_SR` (48 kHz); state is q31 / Q28 where it matters. Modules in the box:

| Group | Modules |
| --- | --- |
| structure | NoteIn (pitch CV, gate, velocity), VoiceOut (sum, pan, frees silent voices), BusIn, MasterOut |
| synthesis | Osc (sine, PolyBLEP saw / pulse, PolyBLAMP triangle, noise), **OscEngines** (Karplus-Strong, modal, FM2, folder, supersaw, vowel, additive, dust; ADR-033), Env (ADSR + hold / start / curves), **Eg** (4-point envelope), Lfo, Filter (TPT SVF, 12..48 dB/oct, LP/BP/HP/notch), Vca, Mult (ring / AM), Mix4, Shaper (10 modes), Const |
| FM | Dx7 (6 operators, 32 algorithms, feedback, 4-stage envelopes) |
| control | MotionSeq (4 lanes x 16 steps; it follows the engine's step sequencer, `ProcessCtx::seq`; ADR-029, ADR-042); the two step sequencers (notes, pads) are not modules: `engine/core/sequencer.*`, owned by the `Engine` |
| effects | Delay (stereo, Hermite, ping-pong), Chorus (Juno-like modes), Reverb (Dattorro plate), SpectralFx (freeze, gate/filter, robot, whisper, pitch shift), Vocoder, **Phaser, Flanger, Tremolo / Auto-pan, Compressor, EQ3, Ring mod / Shifter, Convolver (cab / body IRs), Comb (tuned resonator, voice scope)** (ADR-034) |
| samples | (rack module SM, ADR-030) Sampler (streaming, pitched, loops, reverse, slices, key/velocity zones), Granular; `.smp` files via `framework/tools/wav2smp.py` |

The Ritual Machine's rack -> engine mapping is its ADR-026 and its modulation depths ADR-032 (apps/ritual_machine/docs/DESIGN.md); the effects ADR-034 and the threading rules ADR-024 (docs/DESIGN.md).

## Write an engine module

Derive from `sc::Module` (`engine/core/module.h`): `info()` returns a static `ModuleInfo` (scope voice/global, ports,
parameters with defaults); `init(Memory&)` allocates buffers (`mem.fast` for hot state, `mem.bulk` for long lines) and the
destructor frees them; `reset()` clears state; `set_param(idx, value)` takes int32 in the module's own units (pitch values
are MIDI x 256, times in ms or 1/16 ms, levels q15); `process(ctx, ports)` renders `kBlock` frames of q15 from `in[]`
(always valid) and `mod[]` (null when no cable is connected; a bipolar q15 signal otherwise), writing `out[]`. Register it
(`Registry::add(id, type_of<T>())`), then test it against a float reference (`framework/tests/rig.h` has the helpers).

## Add a HAL function / a platform

Declare it in `framework/hal/hal_*.h`, implement it in **every** `framework/boards/*/` (and in the test stubs,
`tools/ui_stubs.c`). A new board (Teensy, RP2040...) is a folder `framework/boards/<name>/` with the HAL implementations wrapped in
a guard macro, added to the app's `build_src_filter` (`platformio.ini`), plus the app's own side in
`apps/ritual_machine/src/platform/<name>/`: its `main`, and `core/audio.h` (allocate two memory blocks, call `engine_synth_init`,
hand `engine_synth_render` to `audio_out_start`). The board's audio part is small too: call the render function from an audio
thread / DMA task (`hal/hal_audio_out.h`).

## GUI helpers (`framework/ui/gui.h`)

The full guide, written for any app: [framework/docs/DRAWING_KIT.md](docs/DRAWING_KIT.md). In short:

`gui_style_t` (font, margin, padding, gap, list column / graph box geometry, piano-roll and rack metrics; default in
`gui_default_style`, `framework/ui/gui.c`) feeds the helpers: rect layout (`gui_inset`, `gui_center`, `gui_below/above/right_of/left_of`,
`gui_take_top/left`, `gui_grid_cell`, `gui_center_box`), text (`gui_text_w/h`, `gui_row_h`, `gui_text_center`,
`gui_draw_text_centered/left/right`, `gui_draw_field`) and sprites. A sprite is a `gui_sprite_t {w, h, bytes, frames}`: 8 pixels
per byte, MSB = leftmost, rows padded to whole bytes; add small ones to `core/sprites.h`.

Animations: a sprite can be a sheet (`frames` images stacked vertically, e.g. `spr_eq`).
`gui_anim_add(&sprite, first, last, frame_ms, loop)` (or `gui_anim_add_total(...)`) returns an id; `first > last` plays
backwards. Call `gui_anim_tick(now_ms)` from the main loop (true = a frame changed, redraw), draw with
`gui_anim_draw[_centered|_right]`, and delete with `gui_anim_remove(id)` (also `gui_anim_clear`, `_restart`, `_finished`).
A non-looping animation stays on its last frame until removed. Up to `GUI_ANIM_MAX` at once. `app_step()` is the reference.

## Testing

`framework/tests` has a small test runner (`TEST(name) { CHECK(...); }`, filter by name on the command line), DSP helpers (SNR, DFT)
and `DspRig`, an engine with all modules registered; the app's `tests/engine` use the same runner and rig. A test that needs no app
code (no `core/`, no `platform/`) belongs to the library, the others to the app; `.\test.ps1` at the repo root runs both. The suite covers the numeric kernels against float64 references, the plan
compiler and the engine (feedback relocation per voice, rebuilds that keep state, a two-thread stress test), every module
against theory (filter responses against the analytic bilinear Butterworth, FM sidebands against Bessel functions, aliasing
against the naive waveforms, reverb decay rate, chorus delay swing...), the sampler against a simulated slow card (latency,
bandwidth, stalls), and the production path from `rack_t` to audio samples (`test_integration.cpp`). Run `-Matrix` after
touching anything in `engine/`: the six configurations catch hard-coded rates and block sizes.
