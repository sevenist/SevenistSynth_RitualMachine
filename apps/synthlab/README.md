# SynthLab

A workbench for home-made synths and oscillator algorithms on the SevenSynthCore library. SynthLab is a collection of small, all-in-one
synths, each made for one kind of sound. Every synth is one file of plain float C++: a list of parameters and a voice. The screen is a list
of the synth's parameters with their values. Encoder A picks a row and encoder B changes it.

Made for the second prototype (HWV2, GRV-mini: 128 x 160 screen, two encoders, 16 keys); it runs on the desktop first.

The synths, what each is for and how it works: [docs/SYNTHS.md](docs/SYNTHS.md), the design notebook. What changed:
[CHANGELOG.md](CHANGELOG.md); the state and the todo: [CONTINUE.md](CONTINUE.md).

## Build and run

From the repo root:

```powershell
.\build.ps1 -App synthlab               # the desktop program, drawn as HWV2 (needs MSYS2 UCRT64 with gcc, g++ and SDL2, and lib/u8g2)
.\run.ps1 -App synthlab                 # two windows: the screen, and the panel (its controls work with the mouse)
.\apps\synthlab\tools\build_tests.ps1   # its host tests (also run by .\test.ps1): every synth checked, a WAV of each in build/wav/
```

The firmware: open `oled_sim.code-workspace` in VS Code, pick the folder `synthlab` in the PlatformIO status bar, build and upload
(environment `hwv2`). From a shell, only `C:\.platformio\penv\Scripts\pio.exe run -d apps/synthlab`, never a bare `pio`.

## The controls

| Control | On the desktop | What it does |
| --- | --- | --- |
| Encoder A | `[` / `]` | the row: the synth, then its parameters (it steps over the sections' titles). **Push** (`\`): back to the top |
| Encoder B | `;` / `'` | the row's value: the synth, or the parameter. **Push** (`/`): the parameter's default |
| The 16 keys | `1`..`4`, `Q`..`R`, `A`..`F`, `Z`..`V` (by position) | notes: bottom left = C3, +1 to the right, +4 per row up |
| F1 / F2 | `F1` / `F2` | octave down / up |
| F3 / F4 | `F3` / `F4` | the previous / next synth |
| PLAY | `Space` | holds the last note played, so you can turn values while it sounds. Press again to let it go. On a synth with a tempo (the Kick: 135 BPM) it loops a hit on every beat instead; a key changes the note it repeats, the bar shows the tempo |
| VOLUME | the mouse, on the panel | the master volume |

The panel window writes each part's PC keys on it. The keys are named by their position on a QWERTY keyboard: on AZERTY, `Q` is the key
labelled A.

## Writing a synth

1. Copy `src/synths/basic.cpp` to `src/synths/<name>.cpp`. Rename its `synth_basic` and its title.
2. Add it to `src/synths/synth_list.cpp`: one `extern` line and one line in the list.
3. Write its parameters and its `Voice` (the API is in `src/lab.h`; small parts to build with are in `src/lab_dsp.h`).
4. `.\apps\synthlab\tools\build_tests.ps1 <name>`: checks that it sounds, has no NaN at any value's min or max, and falls silent after
   release. It writes `build/wav/<name>.wav`, and times it (with a warning when denormals slow it: `lab::flush` the states that decay).
   With other values: `build_tests.ps1 <name> -Values Click=0,"AMP ENV/Decay=10"` (a name: the first value with it; `SECTION/name`:
   the one in that section; `#n`: the n-th row; kept in the rows' ranges).
5. `.\build.ps1 -App synthlab`, `.\run.ps1 -App synthlab`, and play it.

```cpp
enum { P_CUTOFF, P_DECAY };
const lab::Param kParams[] = {
    {"Cutoff", 40, 16000, 1200, "Hz", lab::Exp},   // name, min, max, default, unit, how encoder B moves it
    {"Decay",  5,  4000,  300,  "ms", lab::Exp},
};
struct Voice {
    void start(const lab::Note &n, const float *p) { /* a key went down */ }
    void release(const lab::Note &n, const float *p) { /* the key went up */ }
    bool render(float *out, int frames, const lab::Note &n, const float *p) {
        // ADD `frames` samples to out (about +-0.5); p[P_CUTOFF] is the value as the screen shows it
        return true;   // false once silent: the voice is freed
    }
};
extern const lab::Synth synth_mine = lab::make_synth<Voice>("Mine", kParams);
// a mono synth with glide: lab::make_synth<Voice>("Mine", kParams, {true, true, 80})   (mono, legato, glide in ms)
// PLAY loops a hit every beat at 128 BPM instead of holding: {false, false, 0, 128}       (play_bpm)
```

A long list can be cut into sections: `lab::section("PITCH ENV")` is a title row (it takes an index of `p[]` too, so keep your enum in
step with the list; see `kick.cpp`). Macros are ordinary values that the synth's `render` adds to others: see the Kick.

A synth can add one effect that runs **once**, on the sum of its voices (a reverb, a delay: one copy of its memory, and its tail goes on
after the voices end): a `struct Fx { void render(float *io, int frames, float sr, const float *p); }` and
`lab::make_synth<Voice, Fx>(...)`. See the Kick's reverb.

The rules: up to 80 rows (titles included); in `start`, `release` and `render`, no `new`/`malloc`, no `printf` and no locks (they run on the audio
thread); use floats, not doubles (`0.5f`), because the ESP32-S3's FPU is single precision.

`Lin` steps 1/100 of the range per detent, `Exp` steps an equal ratio per detent (100 detents from min to max; good for Hz and ms), and
`Int` steps by 1. Give an `Int` parameter `labels` to show names instead of numbers (see Basic's `Wave`).

## What is here

| File | What it is |
| --- | --- |
| `src/lab.h` | The synth API: `Param`, `Note`, `Options`, `make_synth`. |
| `src/lab_dsp.h` | Small float parts: oscillators with PolyBLEP, a state-variable filter, ADSR, decay, noise, smoothing, soft clip. |
| `src/synths/` | The synths, one file each, and `synth_list.cpp` (their order on screen). |
| `src/lab_voice.h`, `.cpp` | The two engine modules: the selected synth's Voice on every voice, its Fx once on their sum. You do not need to touch them. |
| `src/synth.h`, `.cpp` | The engine and its graph (6 voices), picking a synth, the values (each synth keeps its own). |
| `src/app.h`, `src/app.cpp` | The controls and the screen. |
| `src/main_sim.cpp`, `src/main_esp32.cpp` | The desktop and the firmware: memory, the loop, the board's start-up. |
| `tests/` | The host tests (`tools/build_tests.ps1`): every synth and its Fx, the engine, the screen (`build/screen_*.png`), the WAVs, the host's time per sample (`bench`), the null test of Kick Lite against the Kick (`test_null.cpp`, its renders in `build/wav/null/`). |
| `tools/esp32_report.ps1` | What a synth costs on the ESP32-S3, from the compiler: its render's instructions and every call (a float division, a libm function). |
| `docs/SYNTHS.md` | The design notebook: every synth's idea, algorithm and parameters, and the ideas still to try. |
