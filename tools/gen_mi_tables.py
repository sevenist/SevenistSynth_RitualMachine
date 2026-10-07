#!/usr/bin/env python3
"""Generates src/engine/mi/braids/resources_sr.cpp: the Braids tables that depend on the sample rate, for every rate the engine supports.

Braids (Mutable Instruments, MIT, braids/resources/lookup_tables.py and waveforms.py) was tuned for 96 kHz. These are the same formulas with
our rate; the file holds one set per supported ENGINE_SR (32000 / 44100 / 48000 / 96000) and the build picks one. The values are the ones
the original resource compiler writes (int() truncation of the float, like Python 2's '%d').

  oscillator_increments, oscillator_delays   pitch -> phase increment / period (every oscillator)
  resonator_coefficient, resonator_scale     the resonators of the noise models
  svf_cutoff, svf_damp, svf_scale            Braids' SVF (filtered noise, morph, the drums)
  bandlimited_comb_0..14                     the band-limited pulses of BUZZ

Run:  python tools/gen_mi_tables.py            (writes the file; it is committed)
      python tools/gen_mi_tables.py --check R  (compares the 96 kHz set with the original braids/resources.cc in folder R)
Plain Python (no numpy).
"""
import math
import os
import re
import sys

RATES = (32000, 44100, 48000, 96000)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "engine", "mi", "braids", "resources_sr.cpp")
EXCURSION = 65536 * 65536.0



def tables(sr):
    t = {}
    # pitch: notes 128*128 .. (128+12)*128 + 16 step 16 (the top octave; lower octaves are shifts)
    notes = [128 * 128.0 + 16 * i for i in range(int((12 * 128 + 16) / 16))]
    pitches = [440.0 * 2 ** ((n - 69 * 128) / (128 * 12)) for n in notes]
    t["oscillator_increments"] = ("uint32_t", [int(EXCURSION / sr * p) for p in pitches])
    t["oscillator_delays"] = ("uint32_t", [int(sr / p * 65536 * 4096) for p in pitches])

    # resonators
    coef, gain = [], []
    for i in range(129):
        f = 440.0 * 2 ** ((i - 69) / 12.0) / (sr / 2)
        f = min(f, 0.25)
        coef.append(int(2 * math.cos(2 * math.pi * f) * 32768.0))
        s = math.sin(2 * math.pi * f)
        peak = 0.0
        r = 1.0
        for n in range(2000):
            v = abs(math.sin((n + 1) * 2 * math.pi * f) / s * r / (2 * f) ** 0.5)
            if v > peak: peak = v
            r *= 0.99985
        gain.append(int(max(1.0, min(256.0, 16384.0 / peak))))
    t["resonator_coefficient"] = ("uint16_t", coef)
    t["resonator_scale"] = ("uint16_t", gain)

    # SVF
    cut, damp, scl = [], [], []
    for i in range(257):
        f = 440.0 * 2 ** ((i - 69) / 12.0) / sr
        f = min(f, 1 / 8.0)
        f = 2 * math.sin(math.pi * f)
        res = i / 260.0
        d = min(2 * (1 - res ** 0.25), min(2, 2 / f - f * 0.5))
        cut.append(int(f * 32767.0)); damp.append(int(d * 32767.0)); scl.append(int((d / 2) ** 0.5 * 32767.0))
    t["svf_cutoff"] = ("uint16_t", cut)
    t["svf_damp"] = ("uint16_t", damp)
    t["svf_scale"] = ("uint16_t", scl)

    # band-limited pulses (BUZZ)
    W = 256
    fill = [i % W for i in range(W + 1)]
    quad = [(i + W // 4) % W for i in range(W + 1)]
    for zone in range(15):
        f0 = 440.0 * 2.0 ** ((18 + 8 * zone - 69) / 12.0)
        f0 = sr / 2.0 - 1 if zone == 14 else min(f0, sr / 2.0)
        period = sr / f0
        m = 2 * math.floor(period / 2) + 1.0
        pulse = []
        for k in range(W):
            x = (k - W // 2) / float(W)
            pulse.append(math.sin(math.pi * x * m) / (m * math.sin(math.pi * x) + 1e-9))
        pulse[W // 2] = 1.0
        pulse = [pulse[j] for j in fill]
        t["bandlimited_comb_%d" % zone] = ("int16_t", scale([pulse[j] for j in quad]))
    return t


def scale(a, lo=-32766, hi=32766):
    """waveforms.py scale(): centre, normalise to lo..hi, second-order dither (numpy.round = half to even, like Python 3's round)."""
    mean = sum(a) / len(a)
    a = [v - mean for v in a]
    mx = max(abs(v) for v in a)
    a = [((v + mx) / (2 * mx)) * (hi - lo) + lo for v in a]
    x = a
    for _ in range(2):                                        # integrate twice (a leading zero each time)
        acc, y = 0.0, [0.0]
        for v in x: acc += v; y.append(acc)
        x = y
    x = [round(v) for v in x]
    for _ in range(2):
        x = [x[i + 1] - x[i] for i in range(len(x) - 1)]
    return [max(-32768, min(32767, int(v))) for v in x]


def emit(name, ctype, vals):
    lines = []
    for i in range(0, len(vals), 8):
        lines.append("  " + ", ".join(("%dUL" % v if ctype == "uint32_t" and v >= 1 << 31 else "%d" % v) for v in vals[i:i + 8]) + ",")
    prefix = "lut_" if not name.startswith("bandlimited") else "wav_"
    return "const %s %s%s[] = {\n%s\n};\n" % (ctype, prefix, name, "\n".join(lines))


def original(root, name):
    src = open(os.path.join(root, "braids", "resources.cc"), encoding="latin-1").read()
    prefix = "wav_" if name.startswith("bandlimited") else "lut_"
    m = re.search(r"const \w+ %s%s\[\] = \{(.*?)\};" % (prefix, name), src, re.S)
    return [int(v.replace("UL", "")) for v in re.findall(r"-?\d+(?:UL)?", m.group(1))]


def check(root):
    t = tables(96000)
    bad = 0
    for name, (_, vals) in t.items():
        ref = original(root, name)
        if len(ref) != len(vals):
            print("%-24s length %d vs %d" % (name, len(vals), len(ref))); bad += 1; continue
        diff = max(abs(a - b) for a, b in zip(vals, ref))
        print("%-24s max difference %d" % (name, diff))
        bad += diff > 1
    return bad


def main():
    if "--check" in sys.argv:
        return 1 if check(sys.argv[sys.argv.index("--check") + 1]) else 0
    out = ["// GENERATED by tools/gen_mi_tables.py -- do not edit by hand.",
           "// Braids' sample-rate dependent tables (Mutable Instruments Braids, (c) Emilie Gillet, MIT: braids/resources/lookup_tables.py,",
           "// waveforms.py), computed for each ENGINE_SR the engine supports; resources.cc no longer defines them.",
           '#include "braids/resources.h"', '#include "engine/dsp/config.h"', "", "namespace braids {", ""]
    for i, sr in enumerate(RATES):
        out.append("#%s ENGINE_SR == %d" % ("if" if i == 0 else "elif", sr))
        for name, (ctype, vals) in tables(sr).items():
            out.append(emit(name, ctype, vals))
    out += ["#else", '#error "Braids tables: no set for this ENGINE_SR (add it to RATES in tools/gen_mi_tables.py)"', "#endif", "", "}  // namespace braids", ""]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print("wrote", os.path.relpath(OUT, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
