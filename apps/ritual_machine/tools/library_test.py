"""Measures the sample library on the prototype over its serial port (the `dir`, `use` and `pad` dev commands, human_docs/SERIAL_COMMANDS.md).

1. reads the library's root folder twice (the card, then the list that is kept) and the heaps;
2. takes samples spread over the folder into the catalog, timing each (the header read, and the card's walk through the folder to the file);
3. puts --cycle different samples on pad A1 one after the other, playing each: the catalog must stay at 32 entries or under (the ones
   nothing uses leave) and the memory must come back;
4. with --stream NAME: plays that sample (a long one: it streams from the card) on pad A2 and reads a folder meanwhile, and reports the
   stream's underruns (needs the firmware built with -DHWV1_DEBUG_AUDIO).

    C:\\.platformio\\penv\\Scripts\\python.exe tools\\library_test.py [--cycle 40] [--stream "1 - BASS C"] [--log run.txt]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial_test as st  # noqa: E402

DIR_RE = re.compile(r'\[CMD\] dir "(.*)": (-?\d+) entries in (\d+) us')
USE_RE = re.compile(r'\[CMD\] use "(.*)": index (-?\d+) in (\d+) us')
ENT_RE = re.compile(r'\[CMD\] entry \d+ "(.*)"$')
CAT_RE = re.compile(r"\[CMD\] catalog: (\d+) in use of (\d+) entries")
MEM_RE = re.compile(r"PSRAM free (\d+) largest (\d+).*bulk used (\d+).*playing (\d+)")
SMP_RE = re.compile(r"underruns (\d+) \(\+(\d+)\)")


def ask(port, cmd, needle, timeout=20.0):
    t0 = time.time()
    port.send(cmd)
    while time.time() - t0 < timeout:
        for _, x in port.take(t0):
            if needle in x:
                return x
        time.sleep(0.02)
    return None


def mem(port):
    m = MEM_RE.search(ask(port, "pad mem", "[CMD] pad mem") or "")
    return tuple(int(v) for v in m.groups()) if m else (0, 0, 0, 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--cycle", type=int, default=40, help="different samples put on pad A1 one after the other")
    ap.add_argument("--stream", help="name of a long sample to stream while a folder is read")
    ap.add_argument("--log")
    args = ap.parse_args()
    port = st.Port(args.port, 115200, args.log)
    try:
        if not ask(port, "ping", "[CMD] pong", 3.0):
            sys.exit("no answer to 'ping' (firmware with -DDEV_SERIAL_CMD? the right port? the monitor closed?)")
        print("== the root folder ==")
        total = 0
        for k in range(2):
            m = DIR_RE.search(ask(port, "dir", "[CMD] dir") or "")
            if not m:
                sys.exit("dir: no answer")
            total = int(m.group(2))
            print(f"   read {k + 1}: {total} entries in {int(m.group(3)) / 1000:.1f} ms" + (" (the list that is kept)" if k else " (the card)"))
        free, largest, bulk, _ = mem(port)
        print(f"   PSRAM free {free} (largest {largest}) with the list held, engine bulk {bulk}")
        if total <= 0:
            sys.exit("no samples in the root folder")

        print("\n== a sample into the catalog, by its place in the sorted list ==")
        ranks = sorted({min(total - 1, int(total * f)) for f in (0.02, 0.2, 0.4, 0.6, 0.8, 0.98)})
        names = {}
        for r in ranks:
            m = USE_RE.search(ask(port, f"use #{r}", "[CMD] use") or "")
            if m:
                names[r] = m.group(1)
                print(f"   #{r:<5} {m.group(1):<34} index {m.group(2):>3} in {int(m.group(3)) / 1000:7.1f} ms")
        m = USE_RE.search(ask(port, "use no such sample", "[CMD] use") or "")
        if m:
            print(f"   a name that is not on the card: {int(m.group(3)) / 1000:.1f} ms")

        print(f"\n== {args.cycle} different samples on pad A1, one after the other ==")
        print(ask(port, "pad on", "[CMD] pad section") or "pad on: no answer")
        time.sleep(1.5)
        free0, _, bulk0, _ = mem(port)
        worst = 0
        step = max(1, total // (args.cycle + 1))
        played = 0
        ask(port, "dir", "[CMD] dir")                      # (the list: the names come from it, nothing joins the catalog by this)
        bulk_top = 0
        for k in range(args.cycle):
            m = ENT_RE.search(ask(port, f"dir #{(k * step + 7) % total}", "[CMD] entry") or "")
            if not m:
                continue
            ask(port, f"pad def A1 file {m.group(1)}", "[CMD] pad def")
            time.sleep(1.2)                                # the app takes it into the catalog (old unused entries leave), the sample loads
            port.send("pad push 0")
            time.sleep(0.25)
            _, _, bulk, playing = mem(port)
            played += playing & 1
            bulk_top = max(bulk_top, bulk)
            c = CAT_RE.search(ask(port, "use", "[CMD] catalog") or "")
            if c:
                worst = max(worst, int(c.group(1)))
        port.send("pad stop")
        time.sleep(1.0)
        free1, _, bulk1, _ = mem(port)
        c = CAT_RE.search(ask(port, "use", "[CMD] catalog") or "")
        print(f"   {played} of {args.cycle} sounded; catalog at the end: {c.group(1) if c else '?'} in use of {c.group(2) if c else '?'} entries (most in use: {worst})")
        print(f"   engine bulk {bulk0} -> {bulk1} bytes (most: {bulk_top}), PSRAM free {free0} -> {free1}")

        if args.stream:
            print(f"\n== a streamed pad ({args.stream}) while a folder is read ==")
            ask(port, f"pad def A2 loop 1 file {args.stream}", "[CMD] pad def")
            time.sleep(1.5)
            port.send("pad push 1")
            time.sleep(3.0)
            t0 = time.time()
            for k in range(4):
                ask(port, "dir -", "[CMD] dir closed")            # (forgets the list: the next read is from the card)
                m = DIR_RE.search(ask(port, "dir", "[CMD] dir") or "")
                print(f"   read {k + 1}: {int(m.group(3)) / 1000:.1f} ms" if m else "   read: no answer")
                time.sleep(1.0)
            time.sleep(3.0)
            under = [int(m.group(2)) for _, x in port.take(t0) for m in [SMP_RE.search(x)] if m]
            print(f"   stream underruns reported during it: {sum(under)} (in {len(under)} report lines; none = no [SD] report line: HWV1_DEBUG_AUDIO?)")
            port.send("pad stop")
        print(ask(port, "pad off", "[CMD] pad section") or "")
    finally:
        port.close()


if __name__ == "__main__":
    main()
