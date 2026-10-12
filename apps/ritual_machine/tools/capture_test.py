"""Checks the capture (recording what the Pads section plays onto the card) on the prototype over its serial port (`pad rec`, see
human_docs/SERIAL_COMMANDS.md): records a few seconds while pads kept in memory play, with effects, and while pads stream from the
card, and prints the frames the card could not take in time and the blocks the streams missed.
Needs the firmware built with -DDEV_SERIAL_CMD. Each run writes a scratch file `zzrec.smp` on the card and deletes it.

    C:\\.platformio\\penv\\Scripts\\python.exe tools\\capture_test.py [--secs 6] [--log run.txt]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial_test as st  # noqa: E402

SAMPLE_RE = re.compile(r"\[CMD\] sample (\d+) (\S+) (\d+) frames (\d+) Hz")
REC_RE = re.compile(r"\[CMD\] pad rec (\d+) s: (\w+), (\d+) frames, (\d+) lost, finished after (\d+) ms, stream underruns \+(\d+)")
LABELS = [f"{b}{k}" for b in "ABCD" for k in range(1, 17)]


def ask(port, cmd, needle, timeout=3.0):
    t0 = time.time()
    port.send(cmd)
    while time.time() - t0 < timeout:
        for _, x in port.take(t0):
            if needle in x:
                return x
        time.sleep(0.05)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--secs", type=int, default=6)
    ap.add_argument("--log")
    args = ap.parse_args()
    port = st.Port(args.port, 115200, args.log)
    try:
        if not ask(port, "ping", "[CMD] pong"):
            sys.exit("no answer to 'ping' (firmware with -DDEV_SERIAL_CMD? the right port? the monitor closed?)")
        t0 = time.time()
        port.send("samples")
        time.sleep(1.0)
        cat = []
        for _, x in port.take(t0):
            m = SAMPLE_RE.search(x)
            if m:
                cat.append((int(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4))))
        if not cat:
            sys.exit("no samples on the card")
        longer = [c for c in cat if c[2] / c[3] > 0.3]
        short = min(longer or cat, key=lambda c: c[2])
        long_ = max(cat, key=lambda c: c[2])
        print(f"in memory: {short[1]} ({short[2] * 2 // 1024} KB), streamed: {long_[1]} ({long_[2] * 2 // 1024} KB)\n")
        print(ask(port, "pad on", "[CMD] pad section") or "pad on: no answer")
        time.sleep(2.0)

        def run(name, sample, pads, fx):
            ask(port, "pad stop", "[CMD] pad stop")
            ask(port, "pad fx clear", "[CMD] pad fx")
            for m in fx:
                ask(port, f"pad fx {m}", "[CMD] pad fx: ")
            for k in range(8):
                ask(port, f"pad def {LABELS[k]} loop 1 bus {'a' if fx else 'dry'} file {sample}", "[CMD] pad def")
            time.sleep(2.0)
            for k in range(pads):
                port.send(f"pad push {k}")
            time.sleep(1.5)
            line = ask(port, f"pad rec {args.secs}", "[CMD] pad rec", timeout=args.secs + 15.0)
            m = REC_RE.search(line or "")
            if not m:
                print(f"   {name:<44} {line or 'no answer'}")
                return
            frames, lost = int(m.group(3)), int(m.group(4))
            print(f"   {name:<44} {m.group(2):<6} {frames:>7} frames   lost {lost:>6} ({100.0 * lost / frames if frames else 0:.1f} %)   stream underruns +{m.group(6)}")

        print(f"   case                                         state  recorded        lost to the card      streams")
        run("silence", short[1], 0, [])
        run("8 pads in memory", short[1], 8, [])
        run("8 pads in memory + delay, reverb", short[1], 8, ["A DL", "A RV"])
        run("1 streamed pad", long_[1], 1, [])
        run("2 streamed pads", long_[1], 2, [])
        run("4 streamed pads", long_[1], 4, [])
        ask(port, "pad stop", "[CMD] pad stop")
        ask(port, "pad fx clear", "[CMD] pad fx")
        print(ask(port, "pad mem", "[CMD] pad mem") or "")
        print(ask(port, "pad off", "[CMD] pad section") or "")
    finally:
        try:
            port.send("pad stop")
        except Exception:
            pass
        port.close()


if __name__ == "__main__":
    main()
