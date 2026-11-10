# Update log: TechnoGen

The library's changes: [framework/CHANGELOG.md](../../framework/CHANGELOG.md).

## 2026-11-08: TechnoGen, a second instrument on the library (ADR-048; branch `technogen`)

- **TechnoGen**, a techno loop generator for the second prototype, in a first version for the simulator: seven tracks (kick, bass,
  drums, percussion, pad, texture, sample) of sixteen steps, and a dice that rolls a track's sound and steps from a seed, at three
  depths (TIGHT, RAW, WEIRD), with locks and sixteen undos. Eight patterns that change at the end of the bar, mutes, lanes for the
  drums and the percussion. Its screen is the design's, in colour. **The sounds are placeholders** until the real engines come.
- **For developers:** the library draws in colour (`ui/canvas.h`: u8g2 and the drawing kit paint RGB565; the second prototype sends
  the changed parts); the pads' sequencer can play to several modules (`Engine::pseq_route`); `Engine::init` starts the clock again
  from the one; the desktop build rebuilds by itself when its flags change; `.\run.ps1` runs an app's own default board.
