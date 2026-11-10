# TechnoGen: handoff notes

The app's state and todo. Its decisions are in [docs/DESIGN.md](docs/DESIGN.md), its controls in [README.md](README.md). The library's
notes, the user's working style and the traps of the environment are in the root [CONTINUE.md](../../CONTINUE.md).

## Session todo (updated 2026-11-08)

**Where things stand (2026-11-08, second session: TechnoGen):** the user started **a new app from the library: TechnoGen**, a techno loop
generator groovebox for HWV2, from their design canvas ("TechnoGen Firmware Design": the 7-track main view, the signal and core map, the ideas;
outside the repo). After a round of options (apps/technogen/docs/DESIGN.md ADR-048: a colour first version in the simulator; `gui-reorg` fast-forwarded into
`main`, local, not pushed; one commit per step), **the first version was built on the new branch `technogen`**, `f3bde8d` .. the docs commit:
the app, the library's colour canvas, the dice and the track model, the engine's routed pad sequencer with placeholder track modules, the
main and engine views with every control. Host-tested (TechnoGen 33, every other run unchanged), the simulator run; the changed ESP32 files
compile with the xtensa compiler; **the firmware was NOT built with PlatformIO, NOT flashed**. My 13 choices: ADR-048 "Chosen by me". The
user's uncommitted edits (the deleted `apps/HWV2_scanner`, `HWV2_USB_STORAGE=1` in the Ritual Machine's `hwv2`, `board_reader_joystick`'s 4
reads, `usb_storage_esp32.cpp`) were left out of every commit (the user committed them since, `63e034d`, and removed the scanner). **Open:**
- [ ] **Try TechnoGen in the simulator** (`.\build.ps1 -App technogen`, `.\run.ps1 -App technogen`; its README lists the controls): the screens,
  the dice, the patterns at the bar, the knobs' catch. Say what to change; the sounds are placeholders.
- [ ] **Flash TechnoGen on HWV2** (VS Code, folder `technogen`, env `hwv2`): the first colour frames on the real screen (byte order, the
  changed tiles from PSRAM), the LEDs. No sound until the ES8388 is fitted.
- [ ] **Next phases (ADR-048 "Next")**: (2) the budget test on HWV2, before any real engine; (3) the engines, KICK first; (4) the knob
  legend, PERF, the joystick; (5) the master chain; (6) saving; (7) the codec's input, MIDI.
