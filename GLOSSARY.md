# Glossary

The domain's words, as the code and the docs use them. Started 2026-11-08 by an architecture review; add a term when a module is
named after it or a word gets sharpened. Decisions live in each part's `DESIGN.md` (the index of every ADR: [framework/docs/DESIGN.md](framework/docs/DESIGN.md)), the UI in
[apps/ritual_machine/docs/UI.md](apps/ritual_machine/docs/UI.md).

## Ritual Machine UI

- **Section**: Synth or Pads. Each has its own screens, keys and views; the music goes on across a switch.
- **View**: a group of screens (EDIT, SEQ, RACK, PERFORM, PRESETS, SETUP), shown by the badge at the header's left (ADR-046 phase 3).
- **Pot**: an absolute knob: the four **column knobs** (K1..K4, which drive what the screen shows), R1..R3 (macros 1..3) and the volume
  knob. Encoders are not pots.
- **Layer**: what the controls do while Shift or Mod is held (`core/modifiers.h`). An entry is a parameter, an action, a macro, or
  Default (the control keeps its job; a column knob then drives nothing). Layers act in both sections (ADR-047).
- **Catch**: a column knob does not write a value until it has reached it (Catch mode); **Direct** mode writes at once. **Away** /
  **Caught** are the two shades of a pot's LED.
- **Knob legend**: the zone at the bottom of a tall screen naming what each column knob drives, with a bar of the value and the catch tick
  (ADR-046 phase 1).
- **Knob resolver**: the module that answers "what does each pot drive now", in both sections and every layer (`app/app_knobs.c`,
  ADR-047). The knob legend, the pots' LEDs and the catch all read its answer, so they cannot disagree.
- **Macro**: one control that moves up to 8 destinations through their mappings; has a colour (its knob's LED).
