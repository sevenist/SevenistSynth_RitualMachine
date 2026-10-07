"""Turns the images of assets/UI_Sprites/ into src/core/ui_sprites_gen.c / .h (the format: assets/UI_Sprites/README.md).

Run from oled_sim:   python tools/gen_ui_sprites.py          (build.ps1 and the PlatformIO pre-build script run it)
                     python tools/gen_ui_sprites.py --selftest
Plain Python (zlib only): the PlatformIO Python has no Pillow, and installing into it is not worth the risk.
The output files are only rewritten when their content changes, so an unchanged image set does not trigger a rebuild.
"""
import os
import re
import struct
import sys
import zlib

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ASSETS = os.path.join(ROOT, "assets", "UI_Sprites")
OUT_C = os.path.join(ROOT, "src", "core", "ui_sprites_gen.c")
OUT_H = os.path.join(ROOT, "src", "core", "ui_sprites_gen.h")
SIZES = (8, 16, 24, 64)                 # one folder per size; an image must be exactly size x size
FRAME = re.compile(r"^(.*)_(\d\d)$")    # name_00 .. name_99: the frames of one sprite


# ---------------- PNG reading (non-interlaced, any colour type, bit depth 1..8; 16-bit is reduced to 8) ----------------

def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, plte, trns = 8, b"", None, None
    w = h = depth = ctype = 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if interlace:
                raise ValueError("interlaced PNG: save it without interlacing")
        elif kind == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, chans * depth // 8)                          # bytes per pixel for the filters
    stride = (w * chans * depth + 7) // 8
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    def samples(line):                                        # the channel values of a row, scaled to 0..255
        if depth == 8: return list(line)
        if depth == 16: return [line[i] for i in range(0, len(line), 2)]
        out, mask, scale = [], (1 << depth) - 1, 255 // ((1 << depth) - 1)
        for byte in line:
            for s in range(8 - depth, -1, -depth):
                out.append(((byte >> s) & mask) * (1 if ctype == 3 else scale))
        return out
    px = []                                                   # (lum 0..255, alpha 0..255) per pixel
    for line in rows:
        s = samples(line)
        row = []
        for x in range(w):
            if ctype == 0:
                v = s[x]; row.append((v, 0 if trns and depth <= 8 and len(trns) >= 2 and v == trns[1] else 255))
            elif ctype == 2:
                r, g, b = s[3 * x:3 * x + 3]; row.append(((r * 299 + g * 587 + b * 114) // 1000, 255))
            elif ctype == 3:
                i = s[x]; r, g, b = plte[i]
                row.append(((r * 299 + g * 587 + b * 114) // 1000, trns[i] if trns and i < len(trns) else 255))
            elif ctype == 4:
                row.append((s[2 * x], s[2 * x + 1]))
            else:
                r, g, b, a = s[4 * x:4 * x + 4]; row.append(((r * 299 + g * 587 + b * 114) // 1000, a))
        px.append(row)
    return w, h, px


def write_png(path, rows):
    """A greyscale + alpha PNG from rows of (lum, alpha): the self test, and handy for placeholders."""
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\0" + bytes(v for p in row for v in p) for row in rows)
    def chunk(k, b): return struct.pack(">I", len(b)) + k + b + struct.pack(">I", zlib.crc32(k + b) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 4, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


# ---------------- conversion ----------------

def bits(px, w, h, test):
    rb = (w + 7) // 8
    out = []
    for y in range(h):
        for bx in range(rb):
            v = 0
            for b in range(8):
                x = bx * 8 + b
                if x < w and test(px[y][x]): v |= 0x80 >> b
            out.append(v)
    return out


def collect(errors):
    sprites = {}                                              # (size, name) -> [(frame, path)]
    for size in SIZES:
        folder = os.path.join(ASSETS, str(size))
        if not os.path.isdir(folder): continue
        for fn in sorted(os.listdir(folder)):
            base, ext = os.path.splitext(fn)
            if ext.lower() != ".png": continue
            m = FRAME.match(base)
            name, frame = (m.group(1), int(m.group(2))) if m else (base, 0)
            if not re.match(r"^[a-z][a-z0-9_]*$", name):
                errors.append("%d/%s: the name must be lower case letters, digits, _ (it becomes a C name)" % (size, fn)); continue
            sprites.setdefault((size, name), []).append((frame, os.path.join(folder, fn), m is not None))
    return sprites


def generate():
    errors, items = [], []
    for (size, name), frames in sorted(collect(errors).items()):
        frames.sort()
        if any(numbered for _, _, numbered in frames) and [f for f, _, _ in frames] != list(range(len(frames))):
            errors.append("%d/%s: frames must be numbered _00, _01 ... without gaps (and no unnumbered file of that name)" % (size, name)); continue
        data, mask, opaque = [], [], True
        for _, path, _ in frames:
            try:
                w, h, px = read_png(path)
            except Exception as e:
                errors.append("%s: %s" % (os.path.relpath(path, ROOT), e)); break
            if (w, h) != (size, size):
                errors.append("%s: %dx%d, the folder %d/ takes %dx%d images" % (os.path.relpath(path, ROOT), w, h, size, size, size)); break
            data += bits(px, w, h, lambda p: p[1] >= 128 and p[0] >= 128)     # lit: opaque and bright
            mask += bits(px, w, h, lambda p: p[1] >= 128)                      # drawn (lit or off): opaque
            opaque = opaque and all(p[1] >= 128 for row in px for p in row)
        else:
            items.append((size, name, len(frames), data, None if opaque else mask))
    return items, errors


def c_bytes(v):
    return ",\n".join("    " + ", ".join("0x%02X" % b for b in v[i:i + 16]) for i in range(0, len(v), 16))


def render(items):
    h = ["#pragma once",
         "// GENERATED by tools/gen_ui_sprites.py from assets/UI_Sprites/ -- do not edit by hand (the format: assets/UI_Sprites/README.md).",
         "#include \"core/gui.h\"", "", "#ifdef __cplusplus", "extern \"C\" {", "#endif", ""]
    c = ["// GENERATED by tools/gen_ui_sprites.py from assets/UI_Sprites/ -- do not edit by hand.", "#include \"core/ui_sprites_gen.h\"",
         "#include <string.h>", ""]
    for size, name, frames, data, mask in items:
        sym = "spr%d_%s" % (size, name)
        h.append("extern const gui_sprite_t %s;%s" % (sym, "   // %d frames" % frames if frames > 1 else ""))
        c.append("static const uint8_t %s_data[%d] = {\n%s\n};" % (sym, len(data), c_bytes(data)))
        if mask: c.append("static const uint8_t %s_mask[%d] = {\n%s\n};" % (sym, len(mask), c_bytes(mask)))
        c.append("const gui_sprite_t %s = {%d, %d, %s_data, %d, %s};\n" % (sym, size, size, sym, frames, sym + "_mask" if mask else "NULL"))
    h += ["", "// The sprite of an image by its path without .png and frame number (\"24/mod_osc\"); NULL when there is no such image (the caller then",
          "// draws its own fallback, so the art can arrive one file at a time).", "const gui_sprite_t *ui_sprite(const char *name);",
          "", "#ifdef __cplusplus", "}", "#endif", ""]
    c.append("static const struct { const char *name; const gui_sprite_t *sp; } table[] = {")
    c += ["    {\"%d/%s\", &spr%d_%s}," % (s, n, s, n) for s, n, _, _, _ in items] or ["    {NULL, NULL},"]
    c += ["};", "",
          "const gui_sprite_t *ui_sprite(const char *name) {",
          "    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) if (table[i].name && !strcmp(table[i].name, name)) return table[i].sp;",
          "    return NULL;", "}", ""]
    return "\n".join(c), "\n".join(h)


def write_if_changed(path, text):
    old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
    if old == text: return False
    with open(path, "w", encoding="utf-8", newline="\n") as f: f.write(text)
    return True


def selftest():
    import tempfile
    d = tempfile.mkdtemp()
    p = os.path.join(d, "t.png")
    rows = [[(255, 255) if (x + y) % 3 == 0 else (0, 255) if x < 4 else (0, 0) for x in range(8)] for y in range(8)]
    write_png(p, rows)
    w, h, px = read_png(p)
    assert (w, h) == (8, 8) and px == rows, "PNG round trip"
    lit = bits(px, 8, 8, lambda q: q[1] >= 128 and q[0] >= 128)
    drawn = bits(px, 8, 8, lambda q: q[1] >= 128)
    assert lit[0] == 0x92 and drawn[0] == 0xF0 | 0x92, (hex(lit[0]), hex(drawn[0]))
    print("selftest ok")


def main():
    if "--selftest" in sys.argv: selftest(); return 0
    items, errors = generate()
    for e in errors: print("gen_ui_sprites: " + e, file=sys.stderr)
    src, hdr = render(items)
    changed = write_if_changed(OUT_H, hdr) | write_if_changed(OUT_C, src)
    print("gen_ui_sprites: %d sprites%s%s" % (len(items), ", written" if changed else ", unchanged", ", %d errors" % len(errors) if errors else ""))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
