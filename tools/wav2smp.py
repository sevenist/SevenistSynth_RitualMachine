#!/usr/bin/env python3
"""Converts a WAV file to the engine's cooked .smp format (see src/engine/sampler/smp_format.h).

  python tools/wav2smp.py kick.wav kick.smp --root 36
  python tools/wav2smp.py pad.wav pad.smp --root 60 --loop 24000 96000 --loop-mode fwd
  python tools/wav2smp.py break.wav break.smp --slices 0 12000 24000 36000

Accepts 8/16/24/32-bit PCM WAV, mono or stereo (stereo is averaged to mono). Loop points are taken from the
WAV 'smpl' chunk when present, unless --loop is given. The audio is written as 16-bit PCM in 4 KB blocks after
a 4 KB header, so reads on the card are always sector aligned.
"""
import argparse
import struct
import sys
import wave

BLOCK = 4096


def read_smpl_loop(path):
    """Loop start/end (frames) from a WAV 'smpl' chunk, or None."""
    with open(path, "rb") as f:
        data = f.read()
    pos = 12
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"smpl" and size >= 60:
            n = struct.unpack_from("<I", data, pos + 8 + 28)[0]
            if n >= 1:
                start, end = struct.unpack_from("<II", data, pos + 8 + 36 + 8)
                return start, end + 1
        pos += 8 + size + (size & 1)
    return None


def peaks(pcm, buckets=64):
    """Amplitude overview stored in the header (same rule as smp_compute_peaks in smp_format.h)."""
    frames = len(pcm)
    out = bytearray(buckets)
    for b in range(buckets):
        f0 = frames * b // buckets
        f1 = max(f0 + 1, frames * (b + 1) // buckets)
        stride = max(1, (f1 - f0) // 512)
        peak = max((abs(pcm[f]) for f in range(f0, min(f1, frames), stride)), default=0)
        out[b] = min(255, peak >> 7)
    return out


def load_pcm(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    samples = []
    step = width * ch
    for i in range(n):
        acc = 0
        for c in range(ch):
            o = i * step + c * width
            if width == 1:
                v = (raw[o] - 128) << 8
            elif width == 2:
                v = struct.unpack_from("<h", raw, o)[0]
            elif width == 3:
                v = int.from_bytes(raw[o:o + 3], "little", signed=True) >> 8
            elif width == 4:
                v = struct.unpack_from("<i", raw, o)[0] >> 16
            else:
                raise SystemExit("unsupported sample width %d" % width)
            acc += v
        samples.append(max(-32768, min(32767, acc // ch)))
    return samples, rate


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav")
    ap.add_argument("smp")
    ap.add_argument("--root", type=int, default=60, help="MIDI root note (default 60)")
    ap.add_argument("--tune", type=int, default=0, help="fine tune in cents")
    ap.add_argument("--loop", type=int, nargs=2, metavar=("START", "END"), help="loop points in frames (END exclusive)")
    ap.add_argument("--loop-mode", choices=["off", "fwd", "pingpong"], help="default loop mode (fwd when a loop exists)")
    ap.add_argument("--slices", type=int, nargs="*", default=[], help="slice start frames (max 16)")
    a = ap.parse_args()

    pcm, rate = load_pcm(a.wav)
    frames = len(pcm)
    loop = tuple(a.loop) if a.loop else read_smpl_loop(a.wav)
    ls, le = loop if loop else (0, 0)
    if loop and not (0 <= ls < le <= frames):
        sys.exit("loop points outside the sample")
    mode = {"off": 0, "fwd": 1, "pingpong": 2}[a.loop_mode] if a.loop_mode else (1 if loop else 0)
    slices = sorted(set(s for s in a.slices if 0 <= s < frames))[:16]

    header = bytearray(BLOCK)
    header[0:4] = b"SMP1"
    struct.pack_into("<IIIHHHhIIBB", header, 4, 1, rate, frames, 1, 16, a.root, a.tune, ls, le, mode, len(slices))
    for i, s in enumerate(slices):
        struct.pack_into("<I", header, 36 + 4 * i, s)
    header[100:164] = peaks(pcm)

    nblocks = (frames + BLOCK // 2 - 1) // (BLOCK // 2)
    body = bytearray(nblocks * BLOCK)
    struct.pack_into("<%dh" % frames, body, 0, *pcm)
    with open(a.smp, "wb") as f:
        f.write(header)
        f.write(body)
    print("%s: %d frames at %d Hz, root %d, loop %s, %d slices, %d bytes" %
          (a.smp, frames, rate, a.root, "%d..%d" % (ls, le) if loop else "none", len(slices), BLOCK + len(body)))


if __name__ == "__main__":
    main()
