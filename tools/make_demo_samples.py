#!/usr/bin/env python3
"""Writes a few synthetic demo samples (WAV, then cooked with wav2smp.py) into samples/ so the sampler has something to play.

  python tools/make_demo_samples.py            (run from oled_sim; the simulator reads samples/*.smp at startup)

  pad_c4    2.4 s warm pad, loop 0.6 .. 2.0 s          root C4
  pluck_c4  1.5 s plucked string (Karplus-Strong)      root C4
  bell_c5   2.0 s FM bell                              root C5
  kick      0.45 s kick drum                           root C2 (use Trk = Drum)
  break8    2 s drum pattern, 8 slices of 0.25 s       use Slc = 1..8 (Trk = Drum)
"""
import math
import os
import random
import struct
import subprocess
import sys
import wave

RATE = 48000
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "samples")


def write_wav(path, x):
    peak = max(1e-9, max(abs(v) for v in x))
    g = 0.85 / peak if peak > 0.85 else 1.0
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, v * g)) * 32767)) for v in x))


def pad(seconds=2.4, f=261.6256):
    n = int(seconds * RATE)
    # the loop (0.6 .. 2.0 s) has to be seamless: whole periods of every partial, so partials sit on exact multiples of 1 / 1.4 s
    loop = 1.4
    partials = [(1, 1.0), (2, 0.5), (3, 0.33), (4, 0.25), (5, 0.12), (7, 0.06)]
    x = []
    for i in range(n):
        t = i / RATE
        env = min(1.0, t / 0.35) * min(1.0, (seconds - t) / 0.3)
        s = 0.0
        for k, a in partials:
            fk = round(f * k * loop) / loop            # snap to a multiple of the loop frequency
            s += a * (math.sin(2 * math.pi * fk * t) + 0.6 * math.sin(2 * math.pi * (fk + 0.7) * t))
        x.append(s * env)
    return x


def pluck(seconds=1.5, f=261.6256):
    n = int(seconds * RATE)
    period = int(round(RATE / f))
    rnd = random.Random(7)
    buf = [rnd.uniform(-1, 1) for _ in range(period)]
    x = []
    for i in range(n):
        j = i % period
        v = buf[j]
        buf[j] = 0.4985 * (buf[j] + buf[(j + 1) % period])
        x.append(v)
    return x


def bell(seconds=2.0, f=523.2511):
    n = int(seconds * RATE)
    x = []
    for i in range(n):
        t = i / RATE
        idx = 3.0 * math.exp(-t * 3.0)
        s = math.sin(2 * math.pi * f * t + idx * math.sin(2 * math.pi * f * 3.5 * t))
        x.append(s * math.exp(-t * 2.2) * min(1.0, t / 0.002))
    return x


def kick(seconds=0.45):
    n = int(seconds * RATE)
    x = []
    ph = 0.0
    for i in range(n):
        t = i / RATE
        fr = 45 + 140 * math.exp(-t * 28)
        ph += 2 * math.pi * fr / RATE
        click = math.exp(-t * 400) * 0.4
        x.append((math.sin(ph) * math.exp(-t * 7) + click) * min(1.0, t / 0.0005))
    return x


def snare(seconds=0.25):
    n = int(seconds * RATE)
    rnd = random.Random(3)
    return [(rnd.uniform(-1, 1) * math.exp(-i / RATE * 22) * 0.7 + math.sin(2 * math.pi * 190 * i / RATE) * math.exp(-i / RATE * 30) * 0.5)
            for i in range(n)]


def hat(seconds=0.25, open_=False):
    n = int(seconds * RATE)
    rnd = random.Random(11 if open_ else 5)
    d = 9 if open_ else 55
    prev = 0.0
    x = []
    for i in range(n):
        v = rnd.uniform(-1, 1)
        hp = v - prev                                # crude high-pass
        prev = v
        x.append(hp * math.exp(-i / RATE * d) * 0.5)
    return x


def break8():
    step = int(0.25 * RATE)
    parts = [kick(), hat(), snare(), hat(), kick(), kick(), snare(), hat(open_=True)]
    x = [0.0] * (8 * step)
    for k, p in enumerate(parts):
        for i, v in enumerate(p[:step]):
            x[k * step + i] += v
    return x


def cook(name, x, *args):
    wav = os.path.join(OUT, name + ".wav")
    smp = os.path.join(OUT, name + ".smp")
    write_wav(wav, x)
    subprocess.check_call([sys.executable, os.path.join(HERE, "wav2smp.py"), wav, smp] + [str(a) for a in args])
    os.remove(wav)


def main():
    os.makedirs(OUT, exist_ok=True)
    cook("pad_c4", pad(), "--root", 60, "--loop", int(0.6 * RATE), int(2.0 * RATE), "--loop-mode", "fwd")
    cook("pluck_c4", pluck(), "--root", 60)
    cook("bell_c5", bell(), "--root", 72)
    cook("kick", kick(), "--root", 36)
    cook("break8", break8(), "--root", 60, "--slices", *[k * int(0.25 * RATE) for k in range(8)])


if __name__ == "__main__":
    main()
