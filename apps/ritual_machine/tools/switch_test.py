#!/usr/bin/env python3
"""Switches the prototype between the synth and the Pads section over its serial port and checks that each section sounds after the
switch (the user's report of 2026-11-16: "only one of the two makes sounds after a section switch"). Each round: `pad off` (the synth),
a note held, the output captured (`dump`); `pad on` (the Pads section), a pad pushed, the output captured. Prints the level of each
capture, the console lines that tell why a graph did not load (`[AUDIO] a graph did not load ...`) and the engine's heap lines.

Sends `nosave 1` first: nothing is written to the card (restart the board afterwards). Plays the user's own sound and pads as they are.
Needs a firmware with -DDEV_SERIAL_CMD (the default environment has it). Run it with the PlatformIO python (it has pyserial):

    C:/.platformio/penv/Scripts/python.exe apps/ritual_machine/tools/switch_test.py --port COM8 [--rounds 12] [--pad 0] [--note 60] [--log run.txt]
"""
import argparse
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial_test as st  # noqa: E402


def capture(port, n):
    """The next n output samples (`dump n`), as integers."""
    t0 = time.time()
    port.send(f"dump {n}")
    if not st.wait_for(port, "[DUMP] end", 10.0):
        return None
    vals = []
    for _, x in port.take(t0):
        if x.startswith("[DUMP] @"):
            vals += [int(v) for v in x.split()[2:]]
    return vals


def rms(v):
    return math.sqrt(sum(x * x for x in v) / len(v)) if v else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--rounds", type=int, default=12)
    ap.add_argument("--pad", type=int, default=0, help="the pad pushed in the Pads section (0..63)")
    ap.add_argument("--note", type=int, default=60)
    ap.add_argument("--samples", type=int, default=4096)
    ap.add_argument("--settle", type=float, default=0.8, help="seconds after a switch before playing")
    ap.add_argument("--quiet", type=float, default=30.0, help="rms under this = silent")
    ap.add_argument("--hold", type=float, default=0.4, help="seconds between the note / the push and the capture")
    ap.add_argument("--log")
    a = ap.parse_args()

    port = st.Port(a.port, 115200, a.log)
    try:
        port.send("ping")
        if not st.wait_for(port, "[CMD] pong", 5.0):
            sys.exit("no answer to ping: is the firmware built with DEV_SERIAL_CMD, the port the console's?")
        port.send("nosave 1")
        time.sleep(0.3)
        t_start = time.time()
        silent = {"synth": 0, "pads": 0}
        for r in range(a.rounds):
            for section in ("synth", "pads"):
                port.send("pad off" if section == "synth" else "pad on")
                time.sleep(a.settle)
                port.send(f"on {a.note}" if section == "synth" else f"pad push {a.pad}")
                time.sleep(a.hold)
                v = capture(port, a.samples)
                level = rms(v) if v else -1.0
                port.send(f"off {a.note}" if section == "synth" else "pad stop")
                time.sleep(0.3)
                bad = level < a.quiet
                silent[section] += bad
                print(f"round {r:2d} {section:5s} rms {level:8.1f}{'   SILENT' if bad else ''}", flush=True)
        port.send("pad off")
        time.sleep(0.5)
        print(f"\nsilent after a switch: synth {silent['synth']} of {a.rounds}, pads {silent['pads']} of {a.rounds}")
        lines = [x for _, x in port.take(t_start)]
        fails = [x for x in lines if "did not load" in x]
        print(f"graph loads that failed: {len(fails)}")
        for x in fails[:10]:
            print("  " + x)
        heap = [x for x in lines if x.startswith("[HEAP]")]
        if heap:
            print("heap lines (first / last):")
            print("  " + heap[0])
            print("  " + heap[-1])
    finally:
        port.close()


if __name__ == "__main__":
    main()
