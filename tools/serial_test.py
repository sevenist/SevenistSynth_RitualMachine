#!/usr/bin/env python3
"""Talks to the prototype over its serial port: holds chords of 1, 3, 6 ... notes and reports what the audio task measured.

Needs the firmware built with -DDEV_SERIAL_CMD (commands) and -DHWV1_DEBUG_AUDIO -DENGINE_PROFILE (the [AUDIO] / [PROF] lines).
Run it with the PlatformIO python (it has pyserial):

    C:/.platformio/penv/Scripts/python.exe tools/serial_test.py                  # idle, then chords of 1, 3 and 6 notes
    ... tools/serial_test.py --chords 1,2,4,8 --hold 8 --log run.txt
    ... tools/serial_test.py --raw                                               # just print what the board sends
    ... tools/serial_test.py --cmd "chord 4" --raw                               # send a command, then print

The port is opened with DTR and RTS low so that opening it does not reset the board. Close any serial monitor first (one program per port).
"""
import argparse
import re
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing: run this with the PlatformIO python (C:/.platformio/penv/Scripts/python.exe)")

AUDIO_RE = re.compile(r"render avg (\d+) us, worst (\d+) us, budget (\d+) us per (\d+) frames, (\d+) blocks over budget of (\d+)(?:, graph builds (\d+) \(last: ([^)]*)\))?")
PROF_RE = re.compile(r"\[PROF\] cycles per block \(budget (\d+)\), total (\d+):(.*)")
MOD_RE = re.compile(r"(\w+)=(\d+)\(x(\d+)\)")
SEC_RE = re.compile(r"\[SEC\] (.*)")
UNDER_RE = re.compile(r"\[AUDIO\] DMA underruns (\d+)")   # real dropouts: the I2S DMA ring ran empty (HWV1_DEBUG_AUDIO)
OSC_RE = re.compile(r"\[OSC\] [^:]*:(.*)")
SD_RE = re.compile(r"\[SD\] (\d+) reads in (\d+) ms: (\d+) KB/s, avg (\d+) us, worst ever (\d+) us; opens (\d+), seeks (\d+), errors (\d+) \| \[SMP\] stream blocks \+(\d+), underruns (\d+) \(\+(\d+)\)")
ENGINE_NAMES = ["karp", "modal", "fm2", "fold", "ssaw", "vowel", "add", "dust"]


class Port:
    """Reads lines on a thread and keeps them with their arrival time."""

    def __init__(self, name, baud, log):
        self.ser = serial.Serial()
        self.ser.port = name
        self.ser.baudrate = baud
        self.ser.timeout = 0.1
        self.ser.dtr = False          # set before open(): opening with DTR/RTS asserted can reset the ESP32 (auto-reset circuit)
        self.ser.rts = False
        self.ser.open()
        self.lines = []               # (time, text)
        self.lock = threading.Lock()
        self.log = open(log, "w", encoding="utf-8") if log else None
        self.alive = True
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        buf = b""
        while self.alive:
            try:
                data = self.ser.read(512)
            except serial.SerialException as e:
                print("serial error:", e, file=sys.stderr)
                break
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").strip("\r")
                with self.lock:
                    self.lines.append((time.time(), text))
                if self.log:
                    self.log.write(text + "\n")
                    self.log.flush()

    def send(self, cmd):
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()

    def take(self, since=0.0):
        with self.lock:
            return [(t, x) for (t, x) in self.lines if t >= since]

    def close(self):
        self.alive = False
        self.thread.join(timeout=1)
        self.ser.close()
        if self.log:
            self.log.close()


def wait_for(port, needle, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        for _, x in port.take(t0):
            if needle in x:
                return x
        time.sleep(0.05)
    return None


def summarize(lines):
    """Mean / max of the numbers in the [AUDIO] and [PROF] lines of one phase."""
    a_avg, a_worst, over, blocks, builds, reason, dma = [], [], 0, 0, None, "", None
    totals, budget, mods, sec, osc = [], 0, {}, None, {}
    sd = dict(reads=0, ms=0, kbs=[], avg=[], worst=0, errors=0, blocks=0, under=0, seen=0)
    for _, x in lines:
        m = AUDIO_RE.search(x)
        if m:
            a_avg.append(int(m.group(1)))
            a_worst.append(int(m.group(2)))
            over += int(m.group(5))
            blocks += int(m.group(6))
            if m.group(7):
                builds, reason = int(m.group(7)), m.group(8)
        m = UNDER_RE.search(x)
        if m:
            dma = (dma or 0) + int(m.group(1))
        m = PROF_RE.search(x)
        if m:
            budget = int(m.group(1))
            totals.append(int(m.group(2)))
            for name, cyc, calls in MOD_RE.findall(m.group(3)):
                mods.setdefault(name, []).append(int(cyc))
        m = SEC_RE.search(x)
        if m:
            sec = m.group(1)
        m = SD_RE.search(x)
        if m:
            sd["seen"] += 1
            sd["reads"] += int(m.group(1)); sd["ms"] += int(m.group(2)); sd["kbs"].append(int(m.group(3))); sd["avg"].append(int(m.group(4)))
            sd["worst"] = max(sd["worst"], int(m.group(5))); sd["errors"] = int(m.group(8)); sd["blocks"] += int(m.group(9)); sd["under"] += int(m.group(11))
        m = OSC_RE.search(x)
        if m:
            for name, cyc in re.findall(r"(\w+)=(\d+)", m.group(1)):
                osc.setdefault(name, []).append(int(cyc))
            mb = re.search(r"inside the engine switch: (\d+)", x)
            if mb:
                osc.setdefault("switch", []).append(int(mb.group(1)))
    return dict(osc=osc, a_avg=a_avg, a_worst=a_worst, over=over, dma=dma, blocks=blocks, builds=builds, reason=reason, totals=totals, budget=budget, mods=mods, sec=sec, sd=sd)


def mean(v):
    return sum(v) / len(v) if v else 0.0


def report(name, s):
    out = [f"== {name} =="]
    if not s["a_avg"]:
        out.append("   no [AUDIO] lines received in this phase")
        return "\n".join(out)
    pct = 100.0 * mean(s["totals"]) / s["budget"] if s["budget"] and s["totals"] else 0.0
    out.append(f"   render: avg {mean(s['a_avg']):.0f} us, worst {max(s['a_worst'])} us (budget 1333 us per 64 frames); blocks over budget {s['over']} of {s['blocks']}" + (f"; DMA underruns {s['dma']} (real dropouts)" if s['dma'] is not None else ""))
    if s["totals"]:
        out.append(f"   cycles per engine block: {mean(s['totals']):.0f} of {s['budget']} ({pct:.0f} %)   [{len(s['totals'])} reports]")
        top = sorted(((mean(v), k) for k, v in s["mods"].items()), reverse=True)
        out.append("   modules: " + ", ".join(f"{k} {c:.0f}" for c, k in top if c >= 1)[:600])
    sd = s["sd"]
    if sd["seen"]:
        rate = 1000.0 * sd["reads"] / sd["ms"] if sd["ms"] else 0.0
        out.append(f"   card: {rate:.0f} reads/s, {mean(sd['kbs']):.0f} KB/s, avg read {mean(sd['avg']):.0f} us (worst ever {sd['worst']} us), errors {sd['errors']}; sampler underruns in this phase: {sd['under']}")
    if s["builds"] is not None:
        out.append(f"   graph builds so far: {s['builds']} (last: {s['reason']})")
    if s["sec"]:
        out.append(f"   [SEC] {s['sec']}")
    if s["osc"]:
        out.append("   oscillator engines (cycles per block, all voices): " + ", ".join(f"{k} {mean(v):.0f}" for k, v in s["osc"].items()))
    return "\n".join(out)


def phase(port, label, cmd, settle, hold):
    if cmd:
        port.send(cmd)
    t_cmd = time.time()
    time.sleep(settle + hold)
    lines = port.take(t_cmd + settle)             # the first reports after a change straddle it: skipped
    return report(label, summarize(lines)), summarize(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--chords", default="1,3,6", help="comma separated note counts to hold, one phase each")
    ap.add_argument("--idle", type=float, default=4.0, help="seconds measured with nothing held")
    ap.add_argument("--hold", type=float, default=6.0, help="seconds measured per chord")
    ap.add_argument("--settle", type=float, default=2.0, help="seconds skipped after each change")
    ap.add_argument("--engines", help="engine sets to measure, e.g. \"0,1,4,6;2,3,5,7\" (0 karp 1 modal 2 fm2 3 fold 4 ssaw 5 vowel 6 add 7 dust): the first four oscillators of the patch are switched live and each engine's cost per voice is printed")
    ap.add_argument("--log", help="also write every received line to this file")
    ap.add_argument("--cmd", action="append", help="send this command line first (repeatable)")
    ap.add_argument("--raw", action="store_true", help="print the board's output for --seconds instead of running the scenario")
    ap.add_argument("--seconds", type=float, default=10.0)
    args = ap.parse_args()

    try:
        port = Port(args.port, args.baud, args.log)
    except serial.SerialException as e:
        sys.exit(f"cannot open {args.port}: {e}\n(close the serial monitor / any other program that uses it)")

    try:
        for c in args.cmd or []:
            port.send(c)
            time.sleep(1.5)                         # a patch change rebuilds the synth; sampler heads load from the card
        if args.raw:
            t0 = time.time()
            seen = 0
            while time.time() - t0 < args.seconds:
                lines = port.take(t0)
                for _, x in lines[seen:]:
                    print(x)
                seen = len(lines)
                time.sleep(0.1)
            return

        port.send("ping")
        if not wait_for(port, "[CMD] pong", 3.0):
            sys.exit("no answer to 'ping': is the firmware built with -DDEV_SERIAL_CMD and flashed, and is this the right port?")
        port.send("release")
        time.sleep(0.3)
        print(f"connected on {args.port}; measuring idle {args.idle:.0f} s, then chords {args.chords} ({args.hold:.0f} s each, {args.settle:.0f} s settle)\n")

        if args.engines:
            per_engine = {}
            for group in args.engines.split(";"):
                engines = [int(x) for x in group.split(",") if x.strip()][:4]
                port.send("eng " + " ".join(str(e) for e in engines))
                time.sleep(0.3)
                for k in [int(x) for x in args.chords.split(",") if x.strip()]:
                    text, s = phase(port, f"engines {[ENGINE_NAMES[e] for e in engines]}, {k} note(s) held", f"chord {k}", args.settle, args.hold)
                    print(text + "\n")
                    for name, vals in s["osc"].items():
                        per_engine.setdefault(name, []).append(mean(vals) / k)
            port.send("release")
            print("== engine cost: cycles per block per voice (one oscillator) ==")
            for name in ENGINE_NAMES:
                if name in per_engine:
                    print(f"   {name:<6} {mean(per_engine[name]):>7.0f}   ({mean(per_engine[name]) / 32:.0f} per sample)")
            return

        results = {}
        text, s = phase(port, "idle (nothing held)", "release", args.settle, args.idle)
        print(text + "\n")
        results[0] = s
        for k in [int(x) for x in args.chords.split(",") if x.strip()]:
            text, s = phase(port, f"{k} note{'s' if k != 1 else ''} held", f"chord {k}", args.settle, args.hold)
            print(text + "\n")
            results[k] = s
        port.send("release")

        print("== summary: cycles per engine block (budget %d) ==" % (next((r["budget"] for r in results.values() if r["budget"]), 0)))
        print("   held   cycles   %budget   render avg us   worst us   blocks over   DMA underruns")
        for k, r in results.items():
            if not r["a_avg"]:
                print(f"   {k:>4}   (no data)")
                continue
            print(f"   {k:>4}   {mean(r['totals']):>6.0f}   {100.0 * mean(r['totals']) / r['budget'] if r['budget'] and r['totals'] else 0:>6.0f}%   {mean(r['a_avg']):>13.0f}   {max(r['a_worst']):>8}   {r['over']:>5} / {r['blocks']}   {r['dma'] if r['dma'] is not None else '-':>6}")
    finally:
        try:
            port.send("release")
        except Exception:
            pass
        port.close()


if __name__ == "__main__":
    main()
