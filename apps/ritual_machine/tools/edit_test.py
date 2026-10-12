"""Times the pads' sample processing (the EDIT page's card jobs) on the prototype over its serial port (`pad edit`, see
human_docs/SERIAL_COMMANDS.md): one job per kind of operation on a short sample of the card, with the time it takes, the PSRAM it
borrows and whether it fits; then a heavy job while streamed pads play, to see if the card keeps up with both.
Needs the firmware built with -DDEV_SERIAL_CMD. Each job writes a scratch file `zzedit.smp` on the card and deletes it.

    C:\\.platformio\\penv\\Scripts\\python.exe tools\\edit_test.py [--sample N] [--long N] [--streams 4] [--log run.txt]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial_test as st  # noqa: E402

SAMPLE_RE = re.compile(r"\[CMD\] sample (\d+) (\S+) (\d+) frames (\d+) Hz")
EDIT_RE = re.compile(r"\[CMD\] pad edit (\S+) frames (\d+) rate (\d+) op (\d+) amount (-?\d+): (\w+) in (\d+) ms, memory (\d+) \(PSRAM free (\d+) largest (\d+) before\), underruns \+(\d+)")
LABELS = [f"{b}{k}" for b in "ABCD" for k in range(1, 17)]

# (ProcOp number, amount, name, the result's length as a factor of the source's)
JOBS = [
    (0, -3, "Normalize", 1.0),
    (5, 0, "Reverse", 1.0),
    (15, 2000, "Low-pass 2 kHz", 1.0),
    (9, 500000, "Speed 200 %", 0.5),
    (8, 1500000, "Stretch 150 % (Beat)", 1.5),
    (10, 700, "Pitch +7 (Beat)", 1.0),
    (17, 1500000, "Stretch 150 % (Tonal)", 1.5),
    (18, 700, "Pitch +7 (Tonal)", 1.0),
    (19, 4000000, "Paulstretch x4", 4.0),
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


def job(port, index, op, amount):
    line = ask(port, f"pad edit {index} {op} {amount}", "[CMD] pad edit", timeout=620.0)
    m = EDIT_RE.search(line or "")
    return line, m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--sample", type=int, help="catalog index of the sample to process (default: the shortest over 1 s)")
    ap.add_argument("--long", type=int, help="catalog index of the sample the pads stream (default: the longest)")
    ap.add_argument("--streams", type=int, default=4, help="pads streaming during the last job (0 = skip)")
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
        longer = [c for c in cat if c[2] / c[3] > 1.0]
        src = next((c for c in cat if c[0] == args.sample), None) or min(longer or cat, key=lambda c: c[2])
        long_ = next((c for c in cat if c[0] == args.long), None) or max(cat, key=lambda c: c[2])
        secs = src[2] / src[3]
        print(f"\nprocessing: {src[1]} ({secs:.2f} s, {src[2] * 2 // 1024} KB)\n")
        print(ask(port, "pad mem", "[CMD] pad mem") or "", "\n")

        print("   operation                 result    time     x real time   memory    state")
        for op, amount, name, factor in JOBS:
            line, m = job(port, src[0], op, amount)
            if not m:
                print(f"   {name:<24}  {line or 'no answer'}")
                continue
            ms, mem, state = int(m.group(7)), int(m.group(8)), m.group(6)
            out_s = secs * factor
            print(f"   {name:<24} {out_s:>6.2f} s  {ms / 1000:>6.2f} s   {out_s / (ms / 1000) if ms else 0:>8.1f}     {mem / 1024:>5.1f} KB   {state}")
            time.sleep(0.5)

        if args.streams > 0:
            print(f"\n{args.streams} pads streaming {long_[1]} while a Tonal stretch runs:")
            print("  ", ask(port, "pad on", "[CMD] pad section") or "pad on: no answer")
            time.sleep(2.0)
            for k in range(args.streams):
                ask(port, f"pad def {LABELS[k]} loop 1 file {long_[1]}", "[CMD] pad def")
            time.sleep(2.0)
            print("  ", ask(port, "pad mem", "[CMD] pad mem") or "")
            for k in range(args.streams):
                port.send(f"pad push {k}")
            time.sleep(3.0)
            idle, m0 = job(port, src[0], 0, -3)                        # a light job first, as the reference
            heavy, m1 = job(port, src[0], 17, 1500000)
            for label, line, m in (("Normalize", idle, m0), ("Stretch 150 % (Tonal)", heavy, m1)):
                if m:
                    print(f"   {label:<24} {int(m.group(7)) / 1000:>6.2f} s   stream underruns +{m.group(11)}   memory {int(m.group(8)) / 1024:.1f} KB   {m.group(6)}")
                else:
                    print(f"   {label:<24} {line or 'no answer'}")
            port.send("pad stop")
            time.sleep(0.5)
            print("  ", ask(port, "pad off", "[CMD] pad section") or "")
    finally:
        try:
            port.send("pad stop")
        except Exception:
            pass
        port.close()


if __name__ == "__main__":
    main()
