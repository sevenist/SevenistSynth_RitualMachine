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
| `mod_`  | a rack module | `mod_osc mod_filter mod_sat mod_lfo mod_mseq mod_env mod_sampler mod_eg mod_comb` |
| `slot_` | rack cells | `slot_empty slot_out` |
| `fx_`   | an effect | (to be wired) |
| `op_`   | FM operators | (to be wired) |
| `ui_`   | UI details | `ui_xy` |

A name the UI does not look for is converted anyway (ready for the code that will use it). A module or slot without an image keeps the
generated sprite of `tools/gen_module_sprites.py`, so the art can arrive one file at a time.

## Pixels

| PNG pixel | On the screen |
|-----------|---------------|
| bright (luminance >= 50 %), opaque | lit |
| dark (luminance < 50 %), opaque | off: drawn, it erases what is behind |
| alpha < 50 % (any colour) | transparent: what is behind stays |

Any PNG works (grey, RGB, palette, with or without alpha; not interlaced). A sprite whose PNG has no transparent pixel is stored as
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
| audio in  | left edge, middle: (0, 12) |
| audio out | right edge, middle: (23, 12) |
| mod out   | bottom edge, middle: (12, 23) |
| mod in    | top edge, middle: (12, 0) |
