# TechnoGen: design document (living)

The app's decisions. ADR numbers are shared by the whole repo: the index of all of them, and the engine's and the boards'
decisions, are in [framework/docs/DESIGN.md](../../../framework/docs/DESIGN.md).

## Decisions (ADRs)

### ADR-048: TechnoGen, a second instrument on the library (Accepted; the user's design, "TechnoGen Firmware Design", and their choices of 2026-11-08; first version built on branch `technogen` 2026-11-08, `f3bde8d` .. the docs commit; host-tested, NOT flashed)
**What it is.** A techno loop generator groovebox for the second prototype (HWV2, GRV-mini): seven tracks, each one fixed engine
(T1 KICK, T2 BASS, T3 DRUM with 6 lanes, T4 PERC with 4 lanes, T5 PAD, T6 TEXT, T7 SAMP), sixteen steps, and a dice that rolls a track's
sound and steps from a seed. The design is the user's canvas (three boards, outside the repo: the 7-track main view with its controls,
the signal and core map, the ideas on the table); `apps/technogen/README.md` lists the controls.

**Decided in the design** (the user's, recorded, not to be re-litigated):
1. **The new prototype, full colour with modes, drums and percussion as two tracks, lanes picked with SHFT + a step key or SHFT +
   encoder A, the design's control map for now.**
2. **One graph, seven global modules** that each play their own hits (as the Pads player does), not seven Engines (seven plans in a
   73.7 KB fast heap).
3. **The dice never rebuilds the graph**: an `Engine::load` stalls 85-250 ms (ADR-035), so every roll is live `set_param` writes and
   hits. Hence **super-modules**: ALGO is a value inside a module, which also fixes each track's worst case.
4. **Budget targets per 32-frame block (of 160k cycles)**: KICK 6k, BASS 14k, DRUM 16k, PERC 12k, PAD 20k + MANGLE 14k, TEXT 8k,
   SAMP 10k, MASTER 35k (mono delay 8.4k and half-rate reverb 20.5k measured): 135k (84 %). The escape hatch: T5 and the reverb on core
   0, one block late. The startup patch measures 30k a voice: these modules must be far thinner than a rack voice.
5. **The genome**: each value has a sweet spot and a curve; rules link values (a long kick decay caps the rumble, reso x drive under a
   limit); **a locked value still takes its random number**; saves store **the final values and the seed** (a firmware that retunes
   the ranges must not change an old song).
6. **The rest of the design, still to build**: TENSE (one macro over the seven tracks), MORPH (the joystick between two rolls), EVOLVE,
   PRINT and ROLL (Capture into T7, beat repeat), FILL, the colour modes (RGB565 41 KB, I8, I4, I1), the ES8388's input (TAKE,
   THROUGH), MIDI in and out (clock, channels 1-7), MENU > KEYS (every control's three layers take a TechnoGen verb).

**The user's choices of 2026-11-08** (a round of options): **the first step is a colour first version in the simulator** (my
recommendation; the alternatives: a 1-bit first version, or the budget measured first); **`gui-reorg` merged into `main` first**
(a fast-forward, local, not pushed), then the branch `technogen` from it; **one commit per step**.

**Chosen by me while building** (say if one is wrong):
1. **The colour layer is a canvas under u8g2, not a new drawing API** (`ui/canvas.h`): u8g2 draws through its line routine, so every
   u8g2 and `gui.h` call paints RGB565 (draw colour 1 = the foreground, 0 = the background, 2 swaps them). `display_send_rgb565` on every
   board: the desktop paints it; HWV2 keeps three frames in PSRAM (3 x 41 KB) and sends the changed tiles through its 2 KB DMA buffer;
   a one-colour board lights the bright pixels. RGB565 only for now; the other modes when RAM asks for them. Not measured yet: the PSRAM
   traffic against the audio core.
2. **The track modules live in the app** (`apps/technogen/src/tg_tracks.*`, type id 112: apps register their own types), not in the
   library. The engine got only the general part: **the pads' sequencer routed to several modules** (`Engine::pseq_route`: pad p goes
   to the node of group p / 8 as lane p % 8, through the new `Module::hit`, a no-op by default so a wrong id cannot misread a blob) and
   **`ENGINE_PADSEQ_HITS`** (the hits of a pattern, 160 by default; TechnoGen 255: 15 lanes x 16 steps = 240).
3. **A sum chain instead of a mixer**: a module has at most 4 inputs, so each track adds its sound to its stereo input, T1 into T2 ...
   into T7, then MasterOut. DUCK, MANGLE, the sends and the master chain come with phase 5.
4. **The first sounds are placeholders**: a few lines of float DSP per track (a swept sine kick, a filtered saw bass with four ALGOs,
   drum lanes with a shared six-square metal bank, four percussion algorithms, a detuned minor chord, gated noise, a stab). Not tuned,
   not measured: the budget test (phase 2) comes before the real engines.
5. **The main view's knob per track (the design's "macro") is the track's level** for now.
6. **The patterns A1..A8 start as eight copies of the first roll**; a pattern change while playing waits for the end of the bar (the
   engine's queued pattern; the new values follow when the sequencer takes it, a block late at most).
7. **SHFT and MOD: held = on while held, tapped alone = latched** (the mock-up latched them, a mouse cannot hold). **SHFT + encoder B =
   the tempo** (60..200; the design gave the tempo no control). Encoder B stops at TIGHT and WEIRD instead of wrapping.
8. **MOD + a knob toggles that value's lock once** per knob while MOD is on (a pot moves continuously), in the engine view.
9. **A knob takes over once it reaches its value** (it is given another value whenever the track or the view changes); meanwhile the
   footer shows where it is, "CUTOF 40 (72)", and its LED is dim.
10. **The step keys cycle off -> hit -> accent** (the mock-up's); a held step (the pad's chord) comes only from the dice so far.
11. **The font is u8g2's 04b_03b**, the closest to the design's Silkscreen at 8 pixels.
12. **TEXT runs free only while the clock runs** (silent when stopped).
13. **Two library fixes found on the way**: `Engine::init` now starts the clock stopped at beat 0 (an engine started again kept the
    old clock, and its sequencer waited for a bar line); the desktop build keeps its flags in the object folder and rebuilds when they
    change (a flag that sizes a struct linked two layouts of the Engine).

**As built**: `framework/ui/canvas.*`, `hal_display.h` (`display_send_rgb565`), `boards/desktop/display_sim.c`,
`boards/esp32/display_esp32.cpp` and `display_panel.h`, `boards/hwv2/display_panel.cpp`, `HW_DISPLAY_RGB565` in `board_pins.h`;
the engine (`pseq_route`, `Module::hit`, `ENGINE_PADSEQ_HITS`, `init`); `framework/tools/sim_build.ps1`; `run.ps1` (an app's default
board); `apps/technogen/`: `tg_model.*` (the tracks, the dice, BACK), `tg_tracks.*`, `synth.*`, `tg_app.h`, `app.cpp`, its tests (33,
in `test.ps1`; the main and engine views written as PNG). Checked: every host test (library 123, the Ritual Machine 118 + 216, TechnoGen
33), the simulator run, the changed ESP32 files compiled with the xtensa compiler (syntax only: **the firmware was not built with
PlatformIO and not flashed**).

**Next, in this order**: (2) the budget test on HWV2 (seven dummy modules burning their targets, I2S without the codec); (3) the real
engines one at a time, KICK first, each measured; (4) the engine view's knob legend, PERF (FILL, ROLL, EVOLVE), the joystick (MORPH,
TENSE); (5) the master chain (DUCK, MANGLE, delay, reverb, bus, limiter); (6) saving on the card (values and seeds), PRINT, TAKE,
resynth; (7) the ES8388 in and out, MIDI.
