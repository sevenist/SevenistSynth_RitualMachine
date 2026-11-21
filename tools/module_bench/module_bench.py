"""The module bench's runner (README.md): runs the bench firmware in QEMU or reads it from the board, then prints / saves / compares.

  python module_bench.py --qemu                                  every module in QEMU (instructions per 32-frame block)
  python module_bench.py --qemu --only Filter FFilter --save before.json
  python module_bench.py --qemu --only Filter FFilter --baseline before.json     a round: what moved, and whether each output stayed
  python module_bench.py --port COM13 --only Filter              the same on the board (CPU cycles; firmware env hwv2)

Run it with C:\\.platformio\\penv\\Scripts\\python.exe (pyserial; never a bare python on this machine). The QEMU image is made from the
last build of env qemu (pio run -d tools/module_bench -e qemu) with esptool; QEMU is Espressif's (qemu-system-xtensa, found under
~/.espressif/tools/qemu-xtensa or with --qemu-bin).
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, ".pio", "build", "qemu")
PIO = r"C:\.platformio"

LINE = re.compile(r"\[MB\] mod (\d+) (\S+) (voice|global) (q15|f32) def (\d+) driven (\d+) modramp (\d+) modslow (\d+) start (\d+) "
                  r"fast (\d+) spill (\d+) silent (\d) sum ([0-9a-f]+)")
MAX = re.compile(r"\[MB\] max (\d+) (\S+) (\d+) (\S+)")
CALIB = re.compile(r"\[MB\] calib nop1024 (\d+)")


def find_qemu(given):
    if given:
        return given
    pats = [os.path.expanduser("~/.espressif/tools/qemu-xtensa/*/qemu/bin/qemu-system-xtensa.exe"),
            os.path.expanduser("~/.espressif/tools/qemu-xtensa/qemu/bin/qemu-system-xtensa.exe")]
    for p in pats:
        hits = sorted(glob.glob(p))
        if hits:
            return hits[-1]
    sys.exit("qemu-system-xtensa not found: install Espressif's QEMU (README.md) or pass --qemu-bin")


def make_image():
    fw = os.path.join(BUILD, "firmware.bin")
    if not os.path.exists(fw):
        sys.exit("no firmware: build env qemu first (pio run -d tools/module_bench -e qemu)")
    boot_app0 = glob.glob(os.path.join(PIO, "packages", "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin"))
    out = os.path.join(BUILD, "flash_qemu.bin")
    if os.path.exists(out) and os.path.getmtime(out) > os.path.getmtime(fw):
        return out
    esptool = os.path.join(PIO, "penv", "Scripts", "esptool.exe")
    parts = ["0x0", os.path.join(BUILD, "bootloader.bin"), "0x8000", os.path.join(BUILD, "partitions.bin")]
    if boot_app0:
        parts += ["0xe000", boot_app0[0]]
    parts += ["0x10000", fw]
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    subprocess.run([esptool, "--chip", "esp32s3", "merge-bin", "--pad-to-size", "4MB", "-o", out] + parts, check=True, env=env,
                   stdout=subprocess.DEVNULL)
    return out


def collect(lines_iter, send, command, timeout):
    """Reads [MB] lines until the bench's 'done' after our command; returns (modules, maxes, calib, log)."""
    mods, maxes, calib, log = {}, {}, None, []
    started = False
    t_end = time.time() + timeout
    for line in lines_iter:
        if time.time() > t_end:
            break
        line = line.strip()
        if not line:
            continue
        log.append(line)
        if not started:
            if "[MB] done" in line or "[MB] begin" in line:      # the boot run is over (or we caught its start): ask ours
                if "[MB] done" in line:
                    send(command)
                    started = True
            continue
        m = CALIB.search(line)
        if m:
            calib = int(m.group(1))
        m = LINE.search(line)
        if m:
            g = m.groups()
            key = f"{g[1]} {g[2]}"
            mods[key] = {"id": int(g[0]), "name": g[1], "scope": g[2], "sig": g[3], "def": int(g[4]), "driven": int(g[5]),
                         "modramp": int(g[6]), "modslow": int(g[7]), "start": int(g[8]), "fast": int(g[9]), "spill": int(g[10]),
                         "silent": int(g[11]), "sum": g[12]}
        m = MAX.search(line)
        if m:
            maxes[m.group(2)] = {"cycles": int(m.group(3)), "at": m.group(4)}
        if "[MB] done" in line:
            break
    return mods, maxes, calib, log


def run_qemu(args, command):
    img = make_image()
    qemu = find_qemu(args.qemu_bin)
    cmd = [qemu, "-machine", "esp32s3", "-display", "none", "-monitor", "none", "-serial", "stdio",
           "-drive", f"file={img},if=mtd,format=raw", "-icount", f"shift={args.shift},sleep=off"] + (args.qemu_args.split() if args.qemu_args else [])
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    q = []
    done = threading.Event()

    def reader():
        buf = b""
        while True:
            ch = p.stdout.read(1)
            if not ch:
                break
            buf += ch
            if ch == b"\n":
                q.append(buf.decode("utf-8", "replace"))
                buf = b""
        done.set()

    threading.Thread(target=reader, daemon=True).start()

    def lines():
        while not (done.is_set() and not q):
            if q:
                yield q.pop(0)
            else:
                time.sleep(0.01)

    def send(s):
        p.stdin.write((s + "\n").encode())
        p.stdin.flush()

    try:
        return collect(lines(), send, command, args.timeout)
    finally:
        p.kill()


def run_board(args, command):
    import serial
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = args.port, 115200, 0.2
    s.dtr = s.rts = False                                      # (opening the port does not reset the board)
    s.open()

    def lines():
        buf = b""
        while True:
            buf += s.read(4096)
            while b"\n" in buf:
                l, buf = buf.split(b"\n", 1)
                yield l.decode("utf-8", "replace")
            yield ""

    def send(c):
        s.write((c + "\n").encode())

    try:                                                       # (the boot run is long over on a running board: collect sends at once)
        return collect(lines_board_started(lines()), send, command, args.timeout)
    finally:
        s.close()


def lines_board_started(it):
    yield "[MB] done 0"                                        # the board's boot run: already over, our command already sent
    yield from it


def unit(calib, board):
    return 1.0 if board or not calib else calib / 1024.0      # QEMU: counts per instruction (1024 NOPs)


def table(mods, base, u, maxes):
    keys = sorted(mods, key=lambda k: (mods[k]["sig"], mods[k]["name"].lower(), mods[k]["scope"]))
    hdr = f"{'module':22} {'sig':4} {'driven':>8} {'modramp':>8} {'modslow':>8}"
    if base:
        hdr += f" {'before':>8} {'change':>7}  output"
    print(hdr)
    for k in keys:
        m = mods[k]
        row = f"{m['name'] + (' (G)' if m['scope'] == 'global' else ''):22} {m['sig']:4} {m['driven'] / u:8.0f} {m['modramp'] / u:8.0f} {m['modslow'] / u:8.0f}"
        if base and k in base["mods"]:
            b = base["mods"][k]
            bu = unit(base.get("calib"), base.get("board"))
            was = b["driven"] / bu
            now = m["driven"] / u
            row += f" {was:8.0f} {100.0 * (now - was) / was if was else 0:+6.1f}%  {'same' if b['sum'] == m['sum'] else 'DIFFERS'}"
        if m["silent"]:
            row += "  (silent: needs a sample / patch)"
        if m["name"] in maxes:
            row += f"  max {maxes[m['name']]['cycles'] / u:.0f} at {maxes[m['name']]['at']}"
        print(row)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--qemu", action="store_true", help="run the bench in QEMU (env qemu's last build)")
    src.add_argument("--port", help="read the board's console (env hwv2 flashed), e.g. COM13")
    ap.add_argument("--only", nargs="+", default=[], help="only these engine types (Filter, FFilter, Mix4 ...)")
    ap.add_argument("--max", action="store_true", help="also each value at its extremes (slow)")
    ap.add_argument("--save", help="write the results (JSON)")
    ap.add_argument("--baseline", help="compare with a saved run")
    ap.add_argument("--qemu-bin", help="qemu-system-xtensa.exe")
    ap.add_argument("--qemu-args", default="-m 4M", help="more QEMU arguments (default -m 4M: 4 MB of PSRAM)")
    ap.add_argument("--shift", type=int, default=5, help="QEMU -icount shift: 2^shift ns an instruction (5: ~1.3 counts an instruction)")
    ap.add_argument("--timeout", type=float, default=900)
    ap.add_argument("--log", help="write every console line here")
    args = ap.parse_args()
    command = ("max " if args.max else "run ") + " ".join(args.only)
    mods, maxes, calib, log = run_qemu(args, command.strip()) if args.qemu else run_board(args, command.strip())
    if args.log:
        with open(args.log, "w", encoding="utf-8") as f:
            f.write("\n".join(log) + "\n")
    if not mods:
        print("no [MB] results; the last lines:", *log[-15:], sep="\n  ")
        sys.exit(1)
    board = not args.qemu
    u = unit(calib, board)
    print(f"{'board: CPU cycles' if board else f'QEMU: instructions (calibration {calib} counts / 1024 NOPs)'} per {32}-frame block")
    base = None
    if args.baseline:
        with open(args.baseline, encoding="utf-8") as f:
            base = json.load(f)
        if base.get("board") != board:
            print("(the baseline was measured on the other target: cycles and instructions do not compare)")
    table(mods, base, u, maxes)
    if args.save:
        with open(args.save, "w", encoding="utf-8") as f:
            json.dump({"board": board, "calib": calib, "mods": mods, "max": maxes}, f, indent=1)
    if base:
        differ = [k for k in mods if k in base["mods"] and base["mods"][k]["sum"] != mods[k]["sum"]]
        print(f"\n{len(differ)} module(s) with a different output" + (": " + ", ".join(differ) if differ else ""))


if __name__ == "__main__":
    main()
