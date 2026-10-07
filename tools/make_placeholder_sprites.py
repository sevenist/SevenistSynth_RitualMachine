"""Writes placeholder PNGs into assets/UI_Sprites/ for every sprite the UI looks for, so each one can be opened and painted over.

Run from oled_sim:   python tools/make_placeholder_sprites.py            (only the files that do not exist yet)
                     python tools/make_placeholder_sprites.py --force    (rewrites every placeholder: your own art with the same name is lost)
                     python tools/make_placeholder_sprites.py --convert  (rewrites every PNG of assets/UI_Sprites/ in the 3-colour format
                                                                          below; a pixel keeps what the converter made of it)
Then build as usual (build.ps1 / PlatformIO convert them). The format: assets/UI_Sprites/README.md.

The placeholders follow the rules of the README: transparent outside the drawing, the module connectors marked by a 1 px stub at their
fixed points (x, y): mix in (0, 6), mod in (0, 18), the output (23, 18: mix out or mod out), the 16 px top-bar icons with their
last row transparent (the bar's rule shows through).
Files are indexed PNGs with a 3-colour palette (0 transparent, 1 black, 2 white), so a pixel editor offers only what the screen can show.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_ui_sprites import ASSETS, SIZES, read_png, write_png_indexed   # noqa: E402

# 5x7 glyphs, '#' = lit
GLYPHS = {
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."],
    "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "N": ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "V": ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "X": ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": ["####.", "....#", "....#", ".###.", "....#", "....#", "####."],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "I": [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "Z": ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
}

CLEAR, OFF, LIT = (0, 0), (0, 255), (255, 255)   # (luminance, alpha) per pixel

# module name -> (code, connectors): i = mix in, o = an output (mix out or mod out, the same point); every module gets a mod in (it can be a target)
MODULES = {
    "osc": ("OC", "io"), "filter": ("FL", "io"), "sat": ("SA", "io"), "lfo": ("LF", "o"), "mseq": ("MS", "o"),
    "env": ("EN", "o"), "sampler": ("SM", "io"), "eg": ("EG", "o"), "comb": ("RS", "io"),
}
# menu tab -> code (the names of tab_icon() in ui_pages.c)
# oscillator waves and engines (wave_names in core/rack.c, Wav values 0..14) -> a drawn shape, or a 3-letter code for an engine
OSC_WAVES = ["sine", "pulse", "sawdn", "sawup", "tri", "noise"]
OSC_ENGINES = {"karp": "KRP", "modal": "MOD", "fm2": "FM2", "fold": "FLD", "ssaw": "SSW", "vowel": "VOW", "add": "ADD",
               "dust": "DST", "strng": "STR"}
# the Mutable Instruments models (rack Wav >= OC_FIRST_MI; the icon name is the Wav name in lower case, see ui_module_sprite)
MI_MODELS = {"csaw": "CSW", "morph": "MRF", "sawsq": "SSQ", "sintri": "STR", "buzz": "BUZ", "sqsub": "SQB", "sawsub": "SWB", "sqsync": "SQY",
             "swsync": "SWY", "toy": "TOY", "zlp": "ZLP", "zpk": "ZPK", "zbp": "ZBP", "zhp": "ZHP", "vosim": "VOS", "fbfm": "FBF", "chaos": "CHS",
             "kick": "KIK", "fnoise": "FNS", "twin": "TWN", "clock": "CLK", "digmod": "DGM", "morse": "MRS", "3saw": "3SW", "3sq": "3SQ",
             "3tri": "3TR", "3sine": "3SN", "3ring": "3RG", "wtbl": "WTB", "wmap": "WMP", "wline": "WLN", "cloud": "CLD", "cymbal": "CYM",
             "bsnare": "BSN", "pnoise": "PNS", "chip": "CHP", "phdist": "PHD", "va": "VA", "vavcf": "VCF", "grain": "GRN", "terrn": "TRN",
             "pbass": "PBD", "pbasss": "PBS", "psnare": "PSD", "psnrs": "PSS", "phat": "PHH", "phat2": "PH2"}
TABS = {
    "rack": "RK", "general": "GN", "algorithm": "AL", "operator": "OP", "envelope": "EV", "fx": "FX", "samples": "SM",
    "keys": "KY", "modifiers": "MD", "macros": "MC", "curves": "CV", "leds": "LD", "joy": "JY",
}


def canvas(n):
    return [[CLEAR] * n for _ in range(n)]


def box(px, x0, y0, x1, y1, dashed=False):
    """A frame with cut corners from (x0, y0) to (x1, y1), filled OFF inside (opaque: links behind it do not show through)."""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            corner = (x in (x0, x1)) and (y in (y0, y1))
            edge = x in (x0, x1) or y in (y0, y1)
            if corner: continue
            px[y][x] = (LIT if not dashed or (x + y) % 2 == 0 else OFF) if edge else OFF


def text(px, s, cx, cy, scale=1):
    space = 1 if scale == 1 else 0                    # scale 2: the letters touch (2 letters = 20 px, the inside of a 24 px icon)
    w = len(s) * 5 * scale + (len(s) - 1) * space
    x0, y0 = cx - w // 2, cy - 7 * scale // 2
    for k, ch in enumerate(s):
        for r, row in enumerate(GLYPHS[ch]):
            for c, v in enumerate(row):
                if v != "#": continue
                for dy in range(scale):
                    for dx in range(scale):
                        px[y0 + r * scale + dy][x0 + k * (5 * scale + space) + c * scale + dx] = LIT


def module(code, conn):
    px = canvas(24)
    box(px, 1, 1, 22, 22)
    text(px, code, 12, 12)
    px[18][0] = LIT                                   # mod in (0, 18)
    if "i" in conn: px[6][0] = LIT                    # mix in (0, 6)
    if "o" in conn: px[18][23] = LIT                  # the output (23, 18): mix out or mod out
    return px


def slot_empty():
    px = canvas(24)
    box(px, 1, 1, 22, 22, dashed=True)
    for i in range(8, 17):
        px[12][i] = LIT
        px[i][12] = LIT
    return px


def slot_out():
    px = canvas(24)
    box(px, 1, 1, 22, 22)
    px[6][0] = LIT                                    # mix in (0, 6)
    for x in range(2, 13):
        px[11][x] = px[12][x] = LIT
    for r in range(-6, 7):
        for x in range(12, 19 - abs(r)):
            px[12 + r][x] = LIT
    return px


def wave_y(name, t):
    """The wave's height at t (0..1 over one period), -1..1."""
    import math
    if name == "sine": return math.sin(2 * math.pi * t)
    if name == "pulse": return 1 if t < 0.5 else -1
    if name == "sawdn": return 1 - 2 * t
    if name == "sawup": return 2 * t - 1
    if name == "tri": return 4 * t - 1 if t < 0.5 else 3 - 4 * t
    steps = [0.2, -0.8, 0.6, -0.2, 1.0, -0.6, 0.0, 0.8, -1.0]          # noise: fixed random-looking steps, one per pixel column
    return steps[int(t * 9) % 9]


def osc_wave(name):
    """An oscillator icon with the shape of its wave: two periods, x 3..20, y 7..17 (a vertical line joins the jumps)."""
    px = canvas(24)
    box(px, 1, 1, 22, 22)
    px[6][0] = px[18][0] = px[18][23] = LIT           # mix in, mod in, mix out
    prev = None
    for x in range(3, 21):
        y = 12 - round(wave_y(name, ((x - 3) / 9.0) % 1.0) * 5)
        if prev is not None:
            for yy in range(min(prev, y), max(prev, y) + 1): px[yy][x] = LIT
        px[y][x] = LIT
        prev = y
    return px


def osc_engine(code):
    return module(code, "io")


COG = ["  #  #  ",              # '#' lit, 'o' off (drawn black), ' ' transparent
       " ###### ",
       " ##oo## ",
       "##oooo##",
       "##oooo##",
       " ##oo## ",
       " ###### ",
       "  #  #  "]


def from_art(art):
    return [[LIT if c == "#" else OFF if c == "o" else CLEAR for c in row] for row in art]


def tab_icon(code):
    px = canvas(16)
    box(px, 0, 0, 15, 14)                             # row 15 stays transparent: the bar's rule
    text(px, code, 8, 7)
    return px


def convert():
    n = 0
    for size in SIZES:
        folder = os.path.join(ASSETS, str(size))
        if not os.path.isdir(folder): continue
        for fn in sorted(os.listdir(folder)):
            if not fn.lower().endswith(".png"): continue
            path = os.path.join(folder, fn)
            _, _, px = read_png(path)
            write_png_indexed(path, px)
            n += 1
    print("make_placeholder_sprites: %d files converted to the 3-colour palette" % n)


def main():
    if "--convert" in sys.argv: convert(); return
    force = "--force" in sys.argv
    jobs = [("24/mod_%s" % n, module(c, k)) for n, (c, k) in MODULES.items()]
    jobs += [("24/slot_empty", slot_empty()), ("24/slot_out", slot_out())]
    jobs += [("24/osc_%s" % n, osc_wave(n)) for n in OSC_WAVES]
    jobs += [("24/osc_%s" % n, osc_engine(c)) for n, c in OSC_ENGINES.items()]
    jobs += [("24/osc_%s" % n, osc_engine(c)) for n, c in MI_MODELS.items()]
    jobs += [("16/tab_%s" % n, tab_icon(c)) for n, c in TABS.items()]
    jobs += [("8/ui_cog", from_art(COG))]
    wrote = kept = 0
    for name, px in jobs:
        path = os.path.join(ASSETS, name + ".png")
        if os.path.exists(path) and not force:
            kept += 1; continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        write_png_indexed(path, px)
        wrote += 1
    print("make_placeholder_sprites: %d written, %d kept (already there)" % (wrote, kept))


if __name__ == "__main__":
    main()
