# UI sprites

Source images for the screen's sprites. `tools/gen_ui_sprites.py` turns them into `src/core/ui_sprites_gen.c` / `.h`; `build.ps1` and the
PlatformIO pre-build script (`tools/pio_gen_sprites.py`) run it before every build, so the simulator and the firmware always use the
current images. The generated files are committed too, so a build works even where the converter cannot run. Nothing in this folder is
read by the firmware or the simulator directly.

## Folders: one per size

| Folder | Size  | For |
|--------|-------|-----|
| `24/`  | 24x24 | operators, modules, FX and other large icons (3 x 3 grid cells) |
| `16/`  | 16x16 | list and menu icons (one top-bar row) |
| `8/`   | 8x8   | small UI details: arrows, indicators (e.g. `ui_xy`, the joystick XY mode mark) |
| `64/`  | 64x64 | images, the future boot animation |

An image must be exactly the size of its folder (the converter reports any other size and skips the file).

## Names

`<folder>/<name>.png` -> the C sprite `spr<size>_<name>` and the lookup name `"<size>/<name>"` (`ui_sprite("24/mod_osc")`).
Names are lower case letters, digits and `_`, and start with a letter. The kind is a prefix of the name:

| Prefix  | Meaning | Names the UI looks for |
|---------|---------|------------------------|
| `mod_`  | a rack module (24/) | `mod_osc mod_filter mod_sat mod_lfo mod_mseq mod_env mod_sampler mod_eg mod_comb` |
| `slot_` | rack cells (24/) | `slot_empty slot_out` |
| `osc_`  | an oscillator wave / engine (24/; the icon of an OSC module's first page, else `mod_osc`) | `osc_sine osc_pulse osc_sawdn osc_sawup osc_tri osc_noise osc_karp osc_modal osc_fm2 osc_fold osc_ssaw osc_vowel osc_add osc_dust osc_strng` |
| `tab_`  | a menu tab's icon at the left of the top bar (16/) | `tab_rack tab_general tab_algorithm tab_fx tab_samples tab_keys tab_modifiers tab_macros tab_curves tab_leds tab_joy` (`tab_operator tab_envelope`: tabs no longer shown) |
| `fx_`   | an effect | (to be wired) |
| `op_`   | FM operators | (to be wired) |
| `ui_`   | UI details | `ui_xy` (8/), `ui_cog` (8/: the cog of a module's first page; inverted while focused) |

A name the UI does not look for is converted anyway (ready for the code that will use it). A module or slot without an image keeps the
generated sprite of `tools/gen_module_sprites.py`, and a tab without an icon shows its name alone, so the art can arrive one file at a time.

**Placeholders**: `python tools/make_placeholder_sprites.py` writes a placeholder PNG for every name above that has no file yet (a frame
with a 2-letter code and the connector stubs), so each one can be opened and painted over. It never overwrites an existing file
(`--force` does: your art with the same name is lost).

A 16 px tab icon is drawn at (0, 0) over the bar's rule (the bar's last pixel row, y 15): keep its last row transparent.

## Pixels

| PNG pixel | On the screen |
|-----------|---------------|
| bright (luminance >= 50 %), opaque | lit |
| dark (luminance < 50 %), opaque | off: drawn, it erases what is behind |
| alpha < 50 % (any colour) | transparent: what is behind stays |

**The editing format**: every file here is an indexed PNG with a 3-colour palette: index 0 = transparent (magenta in the palette,
alpha 0), 1 = black, 2 = white. Keep the image in indexed mode while drawing (Aseprite: Indexed, transparent colour = index 0;
GIMP: Image > Mode > Indexed; Photoshop: Indexed Color) so only these three exist. `python tools/make_placeholder_sprites.py --convert`
rewrites every PNG of the folder in this format (after art was saved as RGB, for example) without changing what the screen shows.

The converter itself still reads any PNG (grey, RGB, palette, with or without alpha; not interlaced). A sprite whose PNG has no transparent pixel is stored as
one bit plane (`w * h / 8` bytes per frame); with transparency a second plane (the mask) doubles that.

## Animation frames

Numbered files of the same size, `_00` first, no gaps: `64/boot_00.png`, `64/boot_01.png` ... -> one sprite `spr64_boot` with the
frames stacked (`gui_anim_add`, core/gui.h). A 64x64 frame is 512 bytes (1 KB with a mask).

## Placement: the 8 x 8 grid

Everything is placed on an 8 px grid (the 128 x 128 screen = 16 x 16 cells): sprites at multiples of 8, the top bar 16 px high (exactly
one 16 px row). 24 px icons take 3 x 3 cells; the rack shows them in 32 px cells (the icon + one cell of gap), four across.

## Module connectors (fixed convention)

Links attach at fixed points of every 24 x 24 module icon, so the art must leave them free (no metadata in the image):

| Connector | Pixel (x, y) |
|-----------|--------------|
| mix in (sound input) | (0, 6) |
| mod in (modulation input) | (0, 18) |
| output: mix out (a sound module) or mod out (a modulator) | (23, 18) |

A module has one output: sound modules send their mix out there, modulators their mod out. The rack draws a mix link from a module's
output up the gap to the next sound module's mix in (between neighbours; else along a lane under the strip), and a modulation link
(dotted) from the output down to a lane under the strip and up beside the target into its mod in (straight across to a right-hand
neighbour). The icon's pixels are drawn over the links: a transparent background lets them show up to the frame.
