"""Measures the Pads section's FX chains on the prototype over its serial port (`pad fx`, docs/guides/SERIAL_COMMANDS.md): 8 looping pads
(a short sample, kept in memory) sent into chain A, with nothing in the chains, then light, usual and full chains, and prints the
cycles per engine block, the per-module table and the dropouts of each case.
Needs the firmware built with -DDEV_SERIAL_CMD -DHWV1_DEBUG_AUDIO -DENGINE_PROFILE. The chains set here are not saved; a reset brings
the card's own back.

    C:\\.platformio\\penv\\Scripts\\python.exe tools\\padsfx_test.py [--sample N] [--hold 5] [--log run.txt]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial_test as st  # noqa: E402

SAMPLE_RE = re.compile(r"\[CMD\] sample (\d+) (\S+) (\d+) frames (\d+) Hz")
LABELS = [f"{b}{k}" for b in "ABCD" for k in range(1, 17)]

# (name, routing 0 A/B/C 1 A+B/C 2 A+B+C 3 A+C/A+B, the modules as "chain code")
CASES = [
    ("no effect", 0, []),
    ("A: filter + saturator", 0, ["A FL", "A SA"]),
    ("A: delay + reverb", 0, ["A DL", "A RV"]),
    ("A: ring / shift", 0, ["A RG"]),
    ("A: spectral", 0, ["A SP"]),
    ("A: cab", 0, ["A CB"]),
    ("A: EQ, comp, delay, reverb", 0, ["A EQ", "A CP", "A DL", "A RV"]),
    ("A+B+C: 4 light each (12)", 2, ["A EQ", "A CP", "A PH", "A TR", "B EQ", "B CP", "B FG", "B SA", "C EQ", "C CP", "C FL", "C SA"]),
    ("A+B+C: delay, reverb, chorus x3 (9)", 2, ["A DL", "A RV", "A CH", "B DL", "B RV", "B CH", "C DL", "C RV", "C CH"]),
    ("A+B+C: 12 heavy", 2, ["A DL", "A RV", "A CH", "A ES", "B DL", "B RV", "B CH", "B ES", "C DL", "C RV", "C CH", "C ES"]),
]


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
    ap.add_argument("--sample", type=int, help="catalog index of the pads' sample (default: the shortest over 0.3 s)")
    ap.add_argument("--hold", type=float, default=5.0)
    ap.add_argument("--settle", type=float, default=2.0)
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
        smp = next((c for c in cat if c[0] == args.sample), None) or min(longer or cat, key=lambda c: c[2])
        print(f"pads: 8 x {smp[1]} ({smp[2] * 2 // 1024} KB), looping, into chain A\n")
        print(ask(port, "pad on", "[CMD] pad section") or "pad on: no answer")
        time.sleep(2.0)
        for k in range(8):
            ask(port, f"pad def {LABELS[k]} loop 1 bus a file {smp[1]}", "[CMD] pad def")
        time.sleep(2.0)
        rows = []
        for name, route, mods in CASES:
            ask(port, "pad fx clear", "[CMD] pad fx")
            ask(port, f"pad fx route {route}", "[CMD] pad fx")
            for m in mods:
                if not ask(port, f"pad fx {m}", "[CMD] pad fx: "):
                    print(f"   {m}: no answer")
            time.sleep(1.0)
            for k in range(8):
                port.send(f"pad push {k}")
            text, s = st.phase(port, name, None, args.settle, args.hold)
            print(text + "\n")
            rows.append((name, s))
            port.send("pad stop")
            time.sleep(0.5)
        print(ask(port, "pad mem", "[CMD] pad mem") or "")
        ask(port, "pad fx clear", "[CMD] pad fx")
        print(ask(port, "pad off", "[CMD] pad section") or "")

        print("\n== summary: 8 pads into the chains, cycles per engine block ==")
        print("   case                                    cycles   %budget   render avg us   worst us   blocks over   DMA underruns")
        for label, r in rows:
            if not r["a_avg"]:
                print(f"   {label:<38} (no data)")
                continue
            pct = 100.0 * st.mean(r["totals"]) / r["budget"] if r["budget"] and r["totals"] else 0
            print(f"   {label:<38} {st.mean(r['totals']):>6.0f}   {pct:>6.0f}%   {st.mean(r['a_avg']):>13.0f}   {max(r['a_worst']):>8}   {r['over']:>5} / {r['blocks']:<6}   {r['dma'] if r['dma'] is not None else '-':>6}")
    finally:
        try:
            port.send("pad stop")
        except Exception:
            pass
        port.close()


if __name__ == "__main__":
    main()
