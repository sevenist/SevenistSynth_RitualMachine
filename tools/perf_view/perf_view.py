"""Live performance view of the ESP32 engine: reads the perf stream of a DEV_PERF_STREAM firmware (env hwv2_perf) on the
board's serial port and shows it in the browser, or prints a summary without one.

  python perf_view.py                          the board found on USB (VID 303A), page on http://localhost:8765
  python perf_view.py --port COM13 --http 8800
  python perf_view.py --demo                   no board: made-up data, to try the page
  python perf_view.py --summary 5 --cmd "patch startup" --cmd "chord 6"       no page: the commands, then 5 s measured, a table
  python perf_view.py --summary 5 --cmd "chord 6" --save now.json --baseline before.json             ... compared with an earlier run

Use PlatformIO's Python (it has pyserial): C:\\.platformio\\penv\\Scripts\\python.exe. One program per serial port: close the
serial monitor first. The packet format is in README.md (next to this file); the firmware side is
apps/ritual_machine/src/platform/esp32/perf_stream_esp32.cpp.
"""
import argparse
import base64
import json
import math
import os
import queue
import random
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PMU_MODES = ["off", "stalls", "cache", "insn"]
PMU_LABELS = {  # what the two counters count in each mode (perf_stream_esp32.cpp, kPmu)
    "off": [None, None],
    "stalls": ["code stalls", "data stalls"],
    "cache": ["I-cache miss", "D-cache miss"],
    "insn": ["instructions", "bubbles"],
}
KINDS = ["module", "osc", "mi", "sec"]


# ---------------------------------------------------------------- decoding

class Reader:
    def __init__(self, b):
        self.b, self.o = b, 0

    def u8(self):
        v = self.b[self.o]; self.o += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.b, self.o)[0]; self.o += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.o)[0]; self.o += 4; return v

    def s(self):
        n = self.u8(); v = self.b[self.o:self.o + n].decode("ascii", "replace"); self.o += n; return v


def fletcher16(data):
    a = b = 0
    for x in data:
        a = (a + x) % 255
        b = (b + a) % 255
    return b << 8 | a


def decode_line(line):
    """'@PF <base64>' -> dict, or None when the line is not a whole packet."""
    try:
        raw = base64.b64decode(line[4:].strip(), validate=True)
    except Exception:
        return None
    if len(raw) < 4 or fletcher16(raw[:-2]) != struct.unpack_from("<H", raw, len(raw) - 2)[0]:
        return None
    r = Reader(raw[:-2])
    try:
        kind = chr(r.u8())
        if r.u8() != 1:
            return None
        if kind == "N":
            k, first, n = r.u8(), r.u8(), r.u8()
            return {"type": "N", "kind": KINDS[k], "names": {first + i: r.s() for i in range(n)}}
        if kind == "S":
            p = {"type": "S", "seq": r.u16(), "t": r.u32(), "budget": r.u32(), "block": r.u16(), "sr": r.u32(), "mhz": r.u32()}
            pm = r.u8()
            p["pmu"] = PMU_MODES[pm] if pm < len(PMU_MODES) else str(pm)
            for k in ("renders", "blocks", "sum", "max", "over"):
                p[k] = r.u32()
            p["ev"] = [r.u32(), r.u32()]
            for k in ("stalls", "builds", "drops"):
                p[k] = r.u32()
            p["heap"] = {k: r.u32() for k in ("fast_used", "fast_cap", "fast_high", "bulk_used", "spilled", "int_free", "int_largest", "psram_free")}
            p["tl"] = [r.u32() for _ in range(r.u16())]
            p["mods"] = [{"id": r.u8(), "calls": r.u32(), "cyc": r.u32(), "ev0": r.u32(), "ev1": r.u32(), "worst": r.u32()} for _ in range(r.u8())]
            p["subs"] = [{"kind": KINDS[r.u8()], "idx": r.u8(), "cyc": r.u32()} for _ in range(r.u8())]
            return p
    except (IndexError, struct.error):
        return None
    return None


# ---------------------------------------------------------------- the hub: one source, many listeners

class Hub:
    def __init__(self):
        self.names = {k: {} for k in KINDS}
        self.listeners = []
        self.lock = threading.Lock()
        self.send_cmd = None          # set by the source: text -> board

    def subscribe(self):
        q = queue.Queue(maxsize=2000)
        with self.lock:
            self.listeners.append(q)
            q.put({"type": "names", "names": self.names})
        return q

    def unsubscribe(self, q):
        with self.lock:
            if q in self.listeners:
                self.listeners.remove(q)

    def publish(self, ev):
        if ev["type"] == "N":
            self.names[ev["kind"]].update(ev["names"])
        with self.lock:
            for q in self.listeners:
                try:
                    q.put_nowait(ev)
                except queue.Full:
                    pass

    def name(self, kind, i):
        return self.names[kind].get(i, f"{kind}{i}")


def serial_source(hub, port, baud):
    import serial
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, baud, 0.1
    ser.dtr = False  # before open(): DTR/RTS asserted can reset the ESP32
    ser.rts = False
    ser.open()
    wlock = threading.Lock()

    def send(text):
        with wlock:
            ser.write((text.strip() + "\n").encode())
        hub.publish({"type": "log", "text": "> " + text.strip(), "t": time.time()})
    hub.send_cmd = send

    def run():
        buf = b""
        while True:
            try:
                data = ser.read(4096)
            except Exception as e:  # unplugged, board reset
                hub.publish({"type": "status", "text": f"serial error: {e}"})
                time.sleep(1)
                try:
                    ser.close(); ser.open()
                    hub.publish({"type": "status", "text": f"reconnected to {port}"})
                except Exception:
                    pass
                continue
            if not data:
                continue
            buf += data
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                line = raw.decode("ascii", "replace").rstrip("\r")
                if line.startswith("@PF "):
                    p = decode_line(line)
                    if p:
                        hub.publish(p)
                    else:
                        hub.publish({"type": "bad"})
                elif line:
                    hub.publish({"type": "log", "text": line, "t": time.time()})
    threading.Thread(target=run, daemon=True).start()


def find_port():
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports() if p.vid == 0x303A]
    return ports[0].device if ports else None


def demo_source(hub):
    """Made-up packets in the board's shape: a 6-module patch, notes that come and go, a spike now and then."""
    mods = ["MiOsc", "Filter", "Env", "VoiceOut", "Delay", "Reverb", "Shaper", "Lfo"]
    hub.publish({"type": "N", "kind": "module", "names": dict(enumerate(mods))})
    hub.publish({"type": "N", "kind": "sec", "names": {0: "reverb in", 2: "reverb left", 8: "delay time", 9: "delay reads"}})
    budget, block, sr, renders = 348298, 32, 44100, 34
    state = {"pmu": "stalls", "voices": 3}

    def send(text):
        hub.publish({"type": "log", "text": "> " + text, "t": time.time()})
        w = text.split()
        if w[:2] == ["perf", "pmu"] and len(w) > 2 and w[2] in PMU_MODES:
            state["pmu"] = w[2]
        if w[:1] == ["chord"] and len(w) > 1:
            state["voices"] = int(w[1])
        hub.publish({"type": "log", "text": f"[CMD] {text} (demo)", "t": time.time()})
    hub.send_cmd = send

    def run():
        seq, stalls, t0 = 0, 0, time.time()
        while True:
            time.sleep(0.05)
            t = time.time() - t0
            v = state["voices"]
            per = {"MiOsc": 9000 * v, "Filter": 3800 * v, "Env": 700 * v, "VoiceOut": 400 * v, "Delay": 6000,
                   "Reverb": 21000 + 2000 * math.sin(t), "Shaper": 1200 * v, "Lfo": 300 * v}
            calls = {"MiOsc": v, "Filter": v, "Env": 2 * v, "VoiceOut": v, "Delay": 1, "Reverb": 1, "Shaper": v, "Lfo": v}
            tl, worst_t, worst = [], 0, {}
            spike = random.random() < 0.04
            for i in range(renders):
                f = random.gauss(1.0, 0.03) * (1.9 if spike and i == 17 else 1.0)
                c = {m: per[m] * 2 * f * random.uniform(0.97, 1.03) for m in mods}
                tot = int(sum(c.values()) * 1.08)
                tl.append(tot)
                if tot > worst_t:
                    worst_t, worst = tot, c
            if spike and random.random() < 0.3:
                stalls += 1
            mod_rows = []
            for i, m in enumerate(mods):
                cyc = int(per[m] * 2 * renders)
                ist = {"MiOsc": .18, "Filter": .05, "Reverb": .12, "Delay": .08}.get(m, .1)
                dst = {"Reverb": .30, "Delay": .35}.get(m, .04)
                if state["pmu"] == "insn":
                    ist, dst = .7, .15
                mod_rows.append({"id": i, "calls": calls[m] * 2 * renders, "cyc": cyc, "ev0": int(cyc * ist), "ev1": int(cyc * dst), "worst": int(worst[m])})
            p = {"type": "S", "seq": seq, "t": int(t * 1000), "budget": budget, "block": block, "sr": sr, "mhz": 240, "pmu": state["pmu"],
                 "renders": renders, "blocks": 2 * renders, "sum": sum(tl), "max": max(tl), "over": sum(x > budget for x in tl),
                 "ev": [int(sum(tl) * .12), int(sum(tl) * .1)], "stalls": stalls, "builds": 3, "drops": 0,
                 "heap": {"fast_used": 61000 + 4000 * v, "fast_cap": 106000, "fast_high": 90000, "bulk_used": 900000, "spilled": 0,
                          "int_free": 40000, "int_largest": 30000, "psram_free": 5_000_000},
                 "tl": tl, "mods": mod_rows,
                 "subs": [{"kind": "sec", "idx": 0, "cyc": 1500 * 2 * renders}, {"kind": "sec", "idx": 2, "cyc": 8000 * 2 * renders},
                          {"kind": "sec", "idx": 8, "cyc": 900 * 2 * renders}, {"kind": "sec", "idx": 9, "cyc": 3000 * 2 * renders}]}
            seq += 1
            hub.publish(p)
    threading.Thread(target=run, daemon=True).start()


# ---------------------------------------------------------------- the page

def serve(hub, port):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    body = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path == "/events":
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()
                q = hub.subscribe()
                try:
                    while True:
                        try:
                            ev = q.get(timeout=5)
                            self.wfile.write(b"data: " + json.dumps(ev).encode() + b"\n\n")
                        except queue.Empty:
                            self.wfile.write(b": keepalive\n\n")
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass
                finally:
                    hub.unsubscribe(q)
            else:
                self.send_error(404)

        def do_POST(self):
            if self.path == "/cmd":
                n = int(self.headers.get("Content-Length", 0))
                text = self.rfile.read(n).decode("utf-8", "replace")
                if hub.send_cmd:
                    for line in text.splitlines():
                        if line.strip():
                            hub.send_cmd(line)
                self.send_response(204)
                self.end_headers()
            else:
                self.send_error(404)

    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    srv.daemon_threads = True
    print(f"perf view on http://localhost:{port}  (Ctrl+C to stop)")
    srv.serve_forever()


# ---------------------------------------------------------------- headless summary

def summarize(packets, hub):
    if not packets:
        return None
    blocks = sum(p["blocks"] for p in packets) or 1
    renders = sum(p["renders"] for p in packets) or 1
    budget_render = packets[-1]["budget"]
    budget_block = budget_render * renders / blocks
    mods = {}
    for p in packets:
        for m in p["mods"]:
            name = hub.name("module", m["id"])
            a = mods.setdefault(name, {"cyc": 0, "calls": 0, "ev0": 0, "ev1": 0, "worst": 0})
            for k in ("cyc", "calls", "ev0", "ev1"):
                a[k] += m[k]
            a["worst"] = max(a["worst"], m["worst"])
    subs = {}
    for p in packets:
        for s in p["subs"]:
            key = f"{s['kind']}:{hub.name(s['kind'], s['idx'])}"
            subs[key] = subs.get(key, 0) + s["cyc"]
    total = sum(p["sum"] for p in packets)
    tl = [x for p in packets for x in p["tl"]]
    return {
        "seconds": (packets[-1]["t"] - packets[0]["t"]) / 1000.0 if len(packets) > 1 else 0.0,
        "pmu": packets[-1]["pmu"], "budget_block": budget_block, "blocks": blocks, "renders": renders,
        "block_avg": total / blocks, "render_max_pct": 100.0 * max(p["max"] for p in packets) / budget_render,
        "render_p99_pct": 100.0 * sorted(tl)[int(len(tl) * 0.99)] / budget_render if tl else 0.0,
        "over": sum(p["over"] for p in packets), "stalls": packets[-1]["stalls"] - packets[0]["stalls"],
        "builds": packets[-1]["builds"] - packets[0]["builds"], "heap": packets[-1]["heap"],
        "modules": {n: {"cyc_block": a["cyc"] / blocks, "calls_block": a["calls"] / blocks,
                        "ev0_pct": 100.0 * a["ev0"] / a["cyc"] if a["cyc"] else 0.0,
                        "ev1_pct": 100.0 * a["ev1"] / a["cyc"] if a["cyc"] else 0.0,
                        "worst_render": a["worst"]} for n, a in mods.items()},
        "subs": {k: v / blocks for k, v in subs.items()},
    }


def print_summary(s, base=None):
    lab = PMU_LABELS.get(s["pmu"], [None, None])
    bb = s["budget_block"]
    print(f"\n{s['seconds']:.1f} s, {s['blocks']} engine blocks; budget {bb:.0f} cycles a block")
    line = f"cycles a block {s['block_avg']:.0f} ({100 * s['block_avg'] / bb:.1f} %)"
    if base:
        line += f"   before {base['block_avg']:.0f}  ({s['block_avg'] - base['block_avg']:+.0f})"
    print(line)
    print(f"worst render {s['render_max_pct']:.0f} % of its budget, 99th percentile {s['render_p99_pct']:.0f} %, "
          f"{s['over']} renders over budget, {s['stalls']} audio stalls, {s['builds']} graph builds")
    h = s["heap"]
    print(f"fast heap {h['fast_used']} of {h['fast_cap']} (spilled {h['spilled']}), PSRAM free {h['psram_free']}")
    cols = f"{'module':<14}{'cycles/blk':>11}{'%':>7}{'x':>6}{'per call':>10}"
    if lab[0]:
        cols += f"{lab[0][:11]:>13}{lab[1][:11]:>13}"
    if base:
        cols += f"{'before':>10}{'delta':>9}"
    print(cols)
    other = s["block_avg"] - sum(m["cyc_block"] for m in s["modules"].values())
    rows = sorted(s["modules"].items(), key=lambda kv: -kv[1]["cyc_block"])
    for n, m in rows + [("(outside)", {"cyc_block": other, "calls_block": 0, "ev0_pct": 0, "ev1_pct": 0})]:
        r = f"{n[:14]:<14}{m['cyc_block']:>11.0f}{100 * m['cyc_block'] / bb:>6.1f}%{m['calls_block']:>6.1f}"
        r += f"{m['cyc_block'] / m['calls_block']:>10.0f}" if m["calls_block"] else f"{'':>10}"
        if lab[0]:
            r += f"{m['ev0_pct']:>12.1f}%{m['ev1_pct']:>12.1f}%" if m["calls_block"] else ""
        if base and n in base["modules"]:
            b = base["modules"][n]["cyc_block"]
            r += f"{b:>10.0f}{m['cyc_block'] - b:>+9.0f}"
        print(r)
    if s["subs"]:
        print("inside modules (cycles a block): " + ", ".join(f"{k.split(':', 1)[1]} {v:.0f}" for k, v in sorted(s["subs"].items(), key=lambda kv: -kv[1])))


def headless(hub, args):
    q = hub.subscribe()
    deadline = time.time() + 6
    while not hub.send_cmd and time.time() < deadline:
        time.sleep(0.05)
    for c in args.cmd:
        hub.send_cmd(c)
        time.sleep(args.settle)
    while not q.empty():  # what straddles the commands is not measured
        q.get_nowait()
    packets, logs, end = [], [], time.time() + args.summary
    while time.time() < end:
        try:
            ev = q.get(timeout=0.5)
        except queue.Empty:
            continue
        if ev["type"] == "S":
            packets.append(ev)
        elif ev["type"] == "log":
            logs.append(ev["text"])
    for line in logs:
        print(line)
    s = summarize(packets, hub)
    if not s:
        print("no perf packets: is the board running a hwv2_perf build (DEV_PERF_STREAM), and is the stream on (serial `perf on`)?")
        return 1
    base = None
    if args.baseline:
        with open(args.baseline, encoding="utf-8") as f:
            base = json.load(f)
    print_summary(s, base)
    if args.save:
        with open(args.save, "w", encoding="utf-8") as f:
            json.dump(s, f, indent=1)
        print(f"saved {args.save}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: the first ESP32 native USB port, VID 303A)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http", type=int, default=8765, help="the page's port on localhost")
    ap.add_argument("--demo", action="store_true", help="no board: made-up data")
    ap.add_argument("--summary", type=float, metavar="S", help="no page: measure S seconds and print a table")
    ap.add_argument("--cmd", action="append", default=[], help="serial command sent before measuring (repeatable)")
    ap.add_argument("--settle", type=float, default=1.5, help="seconds after each --cmd before measuring")
    ap.add_argument("--save", help="--summary: write the result as JSON")
    ap.add_argument("--baseline", help="--summary: an earlier --save to compare with")
    args = ap.parse_args()

    hub = Hub()
    if args.demo:
        demo_source(hub)
    else:
        port = args.port or find_port()
        if not port:
            print("no ESP32 native USB port found (VID 303A); give --port, or --demo to try the page", file=sys.stderr)
            return 1
        try:
            serial_source(hub, port, args.baud)
        except Exception as e:
            print(f"cannot open {port}: {e} (is a serial monitor holding it?)", file=sys.stderr)
            return 1
        print(f"reading {port}")
    if args.summary:
        return headless(hub, args)
    try:
        serve(hub, args.http)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
