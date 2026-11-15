# SynthLab Studio

A desktop program for [SynthLab](../synthlab/README.md)'s synths, worked with the mouse and the keyboard. It has three tabs:

- **SOUND**: every value of the synth as a knob, grouped by its sections, and eight macros. The macros follow the Ritual Machine's
  model (the library's, `framework/control/macro.h`): a macro is one knob that moves up to 8 values at once, or other macros, which
  then move their own values. Each thing it moves (a destination) has a Min, a Max and a curve (Lin, Exp, Log, S).
- **LAYOUT**: the board's screen (128 x 160, HWV2), drawn as the board would draw it and shown at twice its size. You arrange it
  by hand: rows, knobs, bars, big values, faders, toggles, choices and section titles, with labels, lines and boxes around them,
  over several pages; any element's name can be replaced by your own words. TRY then works the screen as the board's encoders
  would.
- **PATCH**: a synth built from modules and cables, the **Modular** synth. The modules are the library's modular catalog
  ([framework/docs/MODULAR.md](../../framework/docs/MODULAR.md)): SynthLab's parts (the Kick's body, drive, room and glue, the
  Snare's wires and gate, the hat's metal ...) and the basics (oscillator, envelopes, LFO, filter, EQ, VCA, mixer, delay). A patch
  can be exported as a SynthLab synth (C++ that runs on the board) or as the Ritual Machine engine's graph, with a list of the
  modules the engine still lacks.

It runs on the desktop only. It uses SynthLab's synths and sound (`apps/synthlab/src`: `lab.h`, `synth.cpp`, `lab_voice.cpp`,
`synths/`), and the library's drawing kit with its pointer widgets (`framework/ui/widgets.h`). A new synth in SynthLab appears here
at the next build.

## Build and run

```powershell
.\apps\studio\build.ps1                     # build\studio.exe  (-Clean: everything again)
.\apps\studio\build\studio.exe              # the window: resize it freely, F11 (or Alt+Enter) for full screen; the picture fills it
.\apps\studio\build\studio.exe --selftest   # the models, the widgets and the window driven by scripted input
.\apps\studio\build\studio.exe --shot a.png --tab layout --synth 5 [--try] [--auto rows|knobs]   # one frame into a PNG (--tab patch too)
.\apps\studio\build\studio.exe --export synth|engine|patch [--patch my.patch]   # the PATCH tab's exports, of the saved Modular patch (or that file)
.\apps\studio\build\studio.exe --null Kick [--patch my.patch]   # how close a patch is to a SynthLab synth (default: its rebuild in library/)
```

## Controls

Everywhere:

| | |
| --- | --- |
| Tab / Shift+Tab | the next / previous control (the focus: a dotted frame) |
| arrows, Page Up / Down | the focused control's value (Shift: finer); Home / End: its ends; Delete: its default; Enter: a button |
| Esc | no focus |
| F11, Alt+Enter | full screen, and back. The window can also be resized freely: the pixels are scaled by a whole number (the largest that leaves at least 800 x 480), and the tabs use all the room there is, with no border |
| Space | PLAY: holds the last note, or loops it on a synth with a tempo (the Kick, the Snare, DrumLab) |
| Z S X D C V G B H N J M , L . ; / and Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P | notes, by the keys' places (on AZERTY: W S X D C V ... and A é Z " E ...), while KEYS is latched |
| F4 | KEYS (the top bar's button): the computer keyboard plays notes, or not (latched at start; unlatching lets the held notes go) |
| F1 / F2 / F3 | SOUND / LAYOUT / PATCH |
| F5 / F6 | octave down / up |
| F7 / F8 | the previous / next synth |
| Ctrl+S | save (Save in the top bar); Revert goes back to what was saved |

The status line at the bottom says what the control under the pointer is and what it does.

**SOUND**

- A knob: drag up / down (Shift: finer), or use the wheel. A double click sets its default.
- A macro: turn its knob. To give it a value, drag the handle (the striped bar under the macro's knob) onto that value's knob.
  With the keyboard: focus the knob, then press Ctrl+1..8 (again: removes it).
- A knob that the selected macro drives shows the macro's range as a ring in the macro's colour. Drag the ring to move Max (Shift:
  Min). A right click removes the macro from that knob. The small squares at a knob's top right are the macros that drive it.
- Under the macros: the selected macro's name (Rename), its destinations (click to select, x to remove), the selected one's Min,
  Max and Curve, and its response: the macro's travel left to right, the value bottom to top, the macro's position as a dot.
- A new destination is placed so that the value does not jump: its range is 30 % wide, around where the value is now.
- A macro can drive another macro: drop its handle on the other macro's knob. That knob then shows the range as a ring, like a
  value's (right click: unlink). Turning the first macro moves the second, which moves its own values. A macro cannot end up
  driving itself: a link that would loop back is refused.
- At the bottom left, the sound (the same block as the PATCH and LAYOUT tabs): its name (Rename), New (every value at its default,
  no macro), Save as `presets/sounds/<Synth>/<name>.sound`, the synth's saved sounds and Load. EXPORT > To the card writes the
  sound as the board reads it, `synthlab/sounds/<Synth>.sound` in SynthLab's card folder (`apps/synthlab/sdcard/`, the desktop's
  card): copy `synthlab/sounds/` to the board's card. Ctrl+S keeps the sound being edited, whatever its name, in `presets/<Synth>.sound`.

**LAYOUT**

- On the left: the kind that new elements get, the snap (1, 2, 4 or 8 px), the pages, Auto rows / Auto knobs (they replace every
  page), Clear page, TRY; at the bottom the layout (the same block as the SOUND and PATCH tabs: one layout per synth, Save and
  Revert of the layout alone; no export, the board reads no layout yet). The kinds that show a value:
  - **Row** (the name, the value on the right), **Knob**, **Bar**, **Big value**.
  - **Fader:** a vertical slot filled to the value, the name under it (the value while the encoders are on it).
  - **Toggle:** a box, filled when on (a whole number above its least, any other value past half its travel), then the name.
  - **Choice:** a whole number's choices side by side, the value's inverted (Saw | Squar | Tri | Sine); with too many to fit, or a
    continuous value, `< value >`. 18 px high or more: the name above.
  - **Title:** a section (drag a section from the list), or the synth's name in an inverted bar.
- **+ Label, + Line, + Box** (the decorations: no value, the encoders skip them) add one on a click. A label is words of your own
  (typed at once; left, centred or right). A line runs along its longer side. A box is a frame with its words in the top edge; a
  box and a line may lie across other elements (no overlap is reported, a click picks what is inside the box first).
- On the right: the synth's values. Drag one onto the screen, or press Enter on it (or double click it): it goes in the first free
  room. A section drags as a title. A square marks the values already placed (bright: on this page).
- On the screen: click selects, drag moves, the bottom right corner resizes. With the screen focused: the arrows move the selection
  by the snap (Shift: 1 px), Ctrl+arrows resize it, Delete removes it, Ctrl+D copies it, Page Up / Down select another element.
- **An element's text:** Enter (the screen focused) or a double click on it, or the inspector's Text, types it; Enter keeps it,
  Esc drops it. It replaces the name the element shows (a title's too: the synth's name or a section's); the inspector's Name goes
  back to the value's own. 31 characters at most, no quotes.
- Under the screen: the selection's kind and value (a label: its alignment), its text, x, y, w, h; overlaps and elements off the
  screen (also drawn in red); the values on no page.
- TRY: Up / Down are encoder A (the next value, top to bottom then left to right), Left / Right encoder B (Shift: 10 steps),
  Delete the default, Page Up / Down the page. A click on a value goes to it.

**PATCH** (opening it selects the Modular synth)

- **The areas:** VOICE (copied for each voice: from Note to VoiceOut) and FX (once, on the sum of the voices: from Voices to
  Output), the tabs above the canvas. Every module goes in either, except the ends (VoiceOut; Voices and Output).
  - The FX area follows **the last note played**: its pitch and velocity, a gate held while any key is held, a start at each new
    note. An ADSR, a Perc or a Body on the FX plays once, on the sum, like a mono voice.
  - A dimmed RM / eng tag: the engine has that module in the other area only. It plays here, but Export > Engine lists it as a
    module to write (the inspector says "global only" or "per voice only").
- **Adding a module:** drag it from the list on the left onto the canvas, or press Enter on it (or double click it). "eng"
  marks the modules the Ritual Machine's engine already has.
- **The Ritual Machine's modules** ("RM" names, marked RM in the list, each filed under its kind with the others): its own oscillators, oscillator engines, envelopes,
  LFO, filter, shapers and effects, played by the engine's code, so they sound as on the Ritual Machine. A `pitch` input follows the
  key and a `gate` input the voice unless a cable comes in. Their values are the engine's (levels in %, pitches in semitones, the
  rest in its own units; a choice by its name: an oscillator's engine "Fold", a filter's mode "Notch", any on / off value Off / On).
  Export > Engine writes them one to one; Export > Synth cannot hold them.
- **Math** (the list's Math kind): Math (A op B: + - x, min, max, mean, |A-B|, A>B), Func (abs, -x, 1-x, powers, sqrt, sign, half
  rectify, quantize / floor to Steps a unit, wrap), Range (a range converter: InLo..InHi onto OutLo..OutHi, a curve, clamp; by
  default -1..1 onto 0..1) and Slew (a lag: glide, smoothing). Signals are numbers here: audio, envelopes and pitch (octaves) alike.
- **A module:** click its header to select it (Ctrl / Shift click: add it to the selection or take it away), drag the header to
  move the selection; its x removes it. Its values are rows: drag sideways (Shift: finer), use the wheel or the arrows when
  focused; a double click sets the default.
- **Cables:**
  - Drag from an output jack (on the right of a module) onto an input jack (on the left), or onto a value: a modulation, drawn
    dotted, with a coloured bar on the value showing how far it moves it.
  - Drag an input's cable away to move it to another input, or drop it on nothing to remove it.
  - Right click a jack: its menu, "Remove cable" (or "Remove N cables").
- **The canvas:** drag the empty canvas to select a zone (the modules it touches; Ctrl / Shift: added to the selection); a click
  on nothing drops the selection. Middle drag or Alt + drag looks around (or the wheel; Shift: sideways; or the arrows when
  nothing has the focus).
- **The selection's keys** (nothing focused): Delete removes it; Ctrl+C copies it with the cables between its modules, Ctrl+X
  cuts it, Ctrl+V pastes it (at the pointer when it is on the canvas, into the area shown), Ctrl+D duplicates it, Ctrl+A selects
  the area's modules, Esc drops the selection. The keys go by their place on the keyboard: Ctrl+A is also an AZERTY's A. The
  clipboard lives while the studio runs. An area's end (VoiceOut, Voices, Output) is not pasted where there is one already.
- **The inspector (right):**
  - What the selected module does, which engine module matches it (or "new module needed") and where it comes from.
  - Its cables with their depths (a slider each, x removes).
  - CONNECT: one of its outputs to an input or a value of another module, with the keyboard.
  - With nothing selected: the patch's size and memory, and how many of its modules the engine lacks.
- **The patch:** Rename, New (the starting patch), Save as `presets/patches/<name>.patch`, the list of saved patches and Load.
- **SynthLab's synths as patches:** the stepper under Load lists the synths rebuilt from modules (`apps/studio/library/`: Basic, FM2,
  the Kick). Open one to rewire it: the status line gives its null depth against the synth itself (60 dB and more: the same
  sound). It opens as the Modular synth's patch; Save as keeps your version apart, and the library stays as it is.
  Ctrl+S (or Save) also keeps the patch the Modular synth plays: `presets/Modular.patch`, read again at the next start.
- **EXPORT:**
  - **Synth** writes `apps/synthlab/src/synths/gen_<name>.cpp`, a SynthLab synth that calls the modules directly. It adds its
    line to SynthLab's `synth_list.cpp`; build SynthLab (or the studio) to play it, and its tests judge it like any other synth.
    Exporting again overwrites the file: copy it under another name before changing it by hand. Its values are named as the
    Modular synth's (`Osc 2/Tune`), so a sound made on the patch is its sound too: the export also writes `presets/<name>.sound`
    (these values, the macros, the sound's name) when there is none yet, so the synth starts as the patch sounded; then SOUND >
    EXPORT > To the card takes it to the board. A patch of more than 128 values (a title per module included) is refused: the
    board's synths have 128 at most.
  - **Engine** writes `apps/studio/exports/<name>_graph.cpp`: a function that builds the patch as the engine's graph, values
    converted, notes on the approximations, and at the end the modules the engine lacks with their values and cables.
  - **Patch file** writes `apps/studio/exports/<name>.patch`.
- The Modular synth's values are the patch's (a section per module: "Osc 2", "Filter 3" ...): the SOUND tab's knobs and macros
  and the LAYOUT tab work on it. After a change of modules, the macros and the layout keep the values whose module is still
  there.

## Files

Saved in `apps/studio/presets/`, one pair per synth. They are plain text, readable and editable by hand:

- `<synth>.sound`: the sound being edited: its name (`name "Hollow Kick"`), the values (`value BODY/Tune 50`; a name with a space in
  quotes: `value "RUMBLE/Dly Mix" 0.3`) and the macros
  (`macro 1 "Punch" 0.500`, then `dest BODY/Tune 0.200 0.800 Exp`; Min and Max are positions 0..1 along the value's range, the way its
  knob shows it; `link 2 0.000 0.500 Lin`: macro 2, driven by it). The format is the library's (`framework/control/preset.h`): values by
  name, so a sound still opens after its synth gains, loses or reorders values, and a board running the synth reads the same file.
- `<synth>.layout`: the pages (`page`), then an element a line: `row BODY/Tune 0 14 128 10` (the kind, the value, x y w h; a title
  shows a section by its name, `-` is the synth's name). Then, when it has one, its text in quotes (`knob BODY/Tune 0 24 32 34
  "Pitch"`), and a label's alignment (`label - 0 60 128 10 "Hello" center`). The kinds: row, knob, bar, big, fader, toggle, choice,
  title, label, line, box (the last three show no value: `-`).

A value is named by its section and its name. A name with a space is in quotes (`"RUMBLE/Dly Mix"`). A value that a synth no
longer has is dropped when the file is read.

The saved sounds: `presets/sounds/<Synth>/<name>.sound`, the same text (the file named after the sound: "Hollow Kick" ->
`hollow_kick.sound`).

Patches: `presets/Modular.patch` (the one the Modular synth plays) and `presets/patches/*.patch`, in the text form of
[framework/docs/MODULAR.md](../../framework/docs/MODULAR.md) (`node 2 voice Osc 150 20 Wave=2 ...`, `cable 2.Out 3.In 1`,
`mod 4.Env 3.Cutoff 0.5`).

## Limits

- The board reads a synth's sound (SynthLab, `synthlab/sounds/<Synth>.sound`: one per synth, its values; not its macros yet), not
  the layouts.
- One layout per synth. No undo (Revert reloads the file).
- The layout's drawing (`layout_draw`) uses u8g2 and the device's 5x7 font only, so the board could use it as it is.
- Patches: a modulation moves its value once a block (32 samples), by its source's mean; audio-rate modulation goes through
  inputs (an Osc's Pitch and FM, a VCA's CV). A change of modules or cables rebuilds the voices (a note that sounds is cut). The
  Modular synth is polyphonic, with no mono or glide setting yet; the FX area's last note stands in. A Modular voice reserves 2 MB,
  its FX 4 MB (the desktop only): a bigger patch plays nothing, and the inspector says "too big".
