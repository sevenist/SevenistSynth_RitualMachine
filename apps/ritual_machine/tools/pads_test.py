"""Measures the Pads section on the prototype over its serial port (the `pad` dev command, docs/guides/SERIAL_COMMANDS.md).

Switches to the Pads section, then for a short sample (kept whole in memory) and a long one (streamed from the card) plays 1, 4 and 8
pads looping (Trim loop), then the same 8 with a loop crossfade, and prints the render time, the cycles per engine block, the per-module
table, the card's reads and the heaps. Needs the firmware built with -DDEV_SERIAL_CMD -DHWV1_DEBUG_AUDIO -DENGINE_PROFILE.

    C:\\.platformio\\penv\\Scripts\\python.exe tools\\pads_test.py [--short N] [--long N] [--hold 5] [--log run.txt]
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
    ap.add_argument("--short", type=int, help="catalog index of the short sample (default: the shortest over 0.3 s)")
    ap.add_argument("--long", type=int, help="catalog index of the long sample (default: the longest)")
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
        for i, name, frames, rate in cat:
            print(f"   sample {i}: {name}, {frames / rate:.2f} s, {frames * 2 // 1024} KB")
        longer = [c for c in cat if c[2] / c[3] > 0.3]
        short = next((c for c in cat if c[0] == args.short), None) or min(longer or cat, key=lambda c: c[2])
        long_ = next((c for c in cat if c[0] == args.long), None) or max(cat, key=lambda c: c[2])
        print(f"short: {short[1]} ({short[2] * 2 // 1024} KB), long: {long_[1]} ({long_[2] * 2 // 1024} KB)\n")

        print(ask(port, "pad on", "[CMD] pad section") or "pad on: no answer")
        time.sleep(2.0)
        print(ask(port, "pad mem", "[CMD] pad mem") or "", "\n")
        rows = []
        for which, (idx, name, frames, rate) in (("short", short), ("long", long_)):
            for k in range(8):
                ask(port, f"pad def {LABELS[k]} loop 1 file {name}", "[CMD] pad def")
            time.sleep(2.0)                                    # the samples load (heads / whole)
            for n in (1, 4, 8):
                for k in range(n):
                    port.send(f"pad push {k}")
                text, s = st.phase(port, f"{which} sample, {n} pad(s) looping", None, args.settle, args.hold)
                print(text + "\n")
                rows.append((f"{which} x{n}", s))
                port.send("pad stop")
                time.sleep(0.5)
            if which == "short":
                for k in range(8):
                    ask(port, f"pad def {LABELS[k]} loop 1 xfade 200 file {name}", "[CMD] pad def")
                for k in range(8):
                    port.send(f"pad push {k}")
                text, s = st.phase(port, f"{which} sample, 8 pads, 200 ms loop crossfade", None, args.settle, args.hold)
                print(text + "\n")
                rows.append(("short x8 xfade", s))
                port.send("pad stop")
                time.sleep(0.5)
        print(ask(port, "pad mem", "[CMD] pad mem") or "")
        print(ask(port, "pad off", "[CMD] pad section") or "")
        time.sleep(2.0)
        print(ask(port, "pad mem", "[CMD] pad mem") or "", "\n")

        print("== summary: cycles per engine block ==")
        print("   case              cycles   %budget   render avg us   worst us   blocks over   DMA underruns   card underruns")
        for label, r in rows:
            if not r["a_avg"]:
                print(f"   {label:<16} (no data)")
                continue
            pct = 100.0 * st.mean(r["totals"]) / r["budget"] if r["budget"] and r["totals"] else 0
            print(f"   {label:<16} {st.mean(r['totals']):>6.0f}   {pct:>6.0f}%   {st.mean(r['a_avg']):>13.0f}   {max(r['a_worst']):>8}   {r['over']:>5} / {r['blocks']:<6}   {r['dma'] if r['dma'] is not None else '-':>6}   {r['sd']['under']:>6}")
    finally:
        try:
            port.send("pad stop")
        except Exception:
            pass
        port.close()


if __name__ == "__main__":
    main()
