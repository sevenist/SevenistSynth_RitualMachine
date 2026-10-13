"""Turns a monochrome frame array (a C header: `NAME_WIDTH`, `NAME_HEIGHT`, `NAME_FRAME_COUNT`, then the frames as rows of bytes,
LSB = left pixel, as u8g2's drawXBM takes them) into the boot animation's images: assets/UI_Sprites/64/boot_00.png ... (README.md
there). Each frame is centred in a 64 x 64 image, black behind it; tools/gen_ui_sprites.py then makes the sprite `spr64_boot`.

Run from apps/ritual_machine:   python tools/import_boot_anim.py path/to/loading7.h [--sheet out.png]
The existing boot_NN.png files are replaced (frames painted by hand are lost). --sheet also writes every frame side by side, x4.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_ui_sprites as gs  # noqa: E402

SIZE = 64
FOLDER = os.path.join(gs.ASSETS, str(SIZE))


def read_header(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    def define(word):
        m = re.search(r"#define\s+\w*_%s\s+(\d+)" % word, text)
        if not m: raise ValueError("no #define ..._%s in %s" % (word, path))
        return int(m.group(1))
    w, h, count = define("WIDTH"), define("HEIGHT"), define("FRAME_COUNT")
    body = text[text.index("{", text.index("=")):]
    data = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{1,2})", body)]
    row_bytes = (w + 7) // 8
    if len(data) != count * h * row_bytes:
        raise ValueError("%d bytes, %d frames of %dx%d need %d" % (len(data), count, w, h, count * h * row_bytes))
    frames = []
    for f in range(count):
        base = f * h * row_bytes
        frames.append([[data[base + y * row_bytes + x // 8] >> (x % 8) & 1 for x in range(w)] for y in range(h)])
    return w, h, frames


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 1: print(__doc__); return 1
    w, h, frames = read_header(args[0])
    if w > SIZE or h > SIZE or len(frames) > 100:
        print("import_boot_anim: %dx%d, %d frames: at most %dx%d and 100 frames" % (w, h, len(frames), SIZE, SIZE)); return 1
    os.makedirs(FOLDER, exist_ok=True)
    for fn in os.listdir(FOLDER):
        if re.match(r"^boot_\d\d\.png$", fn): os.remove(os.path.join(FOLDER, fn))
    x0, y0 = (SIZE - w) // 2, (SIZE - h) // 2
    images = []
    for k, fr in enumerate(frames):
        rows = [[(255 if y0 <= y < y0 + h and x0 <= x < x0 + w and fr[y - y0][x - x0] else 0, 255) for x in range(SIZE)] for y in range(SIZE)]
        gs.write_png_indexed(os.path.join(FOLDER, "boot_%02d.png" % k), rows)
        images.append(rows)
    print("import_boot_anim: %d frames of %dx%d -> %s" % (len(frames), w, h, os.path.relpath(FOLDER, gs.ROOT)))
    if "--sheet" in sys.argv:                                 # every frame, x4, 8 a row, a grey gap between them
        out, scale, per_row, gap = sys.argv[sys.argv.index("--sheet") + 1], 4, 8, 4
        cell = SIZE * scale + gap
        lines = (len(images) + per_row - 1) // per_row
        sheet = [[(96, 255)] * (per_row * cell + gap) for _ in range(lines * cell + gap)]
        for k, rows in enumerate(images):
            ox, oy = gap + (k % per_row) * cell, gap + (k // per_row) * cell
            for y in range(SIZE * scale):
                for x in range(SIZE * scale):
                    sheet[oy + y][ox + x] = rows[y // scale][x // scale]
        gs.write_png(out, sheet)
    return 0


if __name__ == "__main__":
    sys.exit(main())
