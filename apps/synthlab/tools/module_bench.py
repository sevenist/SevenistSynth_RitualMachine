"""Reads the modules' benchmark from a board running SynthLab's bench build (platformio.ini: hwv2_bench / hwv1_bench, src/bench_esp32.cpp)
and prints it as tables; --log keeps the raw lines. The bench starts 6 s after the board starts, or when it receives 'b' (sent here when
no bench line comes within a few seconds). Do not touch the board while it runs. One program per serial port: close the monitor first.

    C:/.platformio/penv/Scripts/python.exe apps/synthlab/tools/module_bench.py --port COM13 --log bench.txt
    C:/.platformio/penv/Scripts/python.exe apps/synthlab/tools/module_bench.py --from bench.txt        (a saved log, no board)

The float / q15 comparison (ADR-050 step 0) needs two runs: hwv1_bench (the catalog's float modules, their F types) and hwv1_bench_rm
(the engine's modules through the adapter, then natively: the [MBENCH] native lines). Then both logs, the second one adding the table
"per function":  module_bench.py --from bench.txt bench_rm.txt
"""
import argparse
import sys
import time


def read_board(port, timeout_s):
    import serial
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False                                   # (no reset)
    s.rts = False
    s.open()
    lines, buf, began, sent = [], b'', False, False
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        buf += s.read(4096)
        while b'\n' in buf:
            raw, buf = buf.split(b'\n', 1)
            line = raw.decode('utf-8', 'replace').strip()
            if '[MBENCH]' not in line:
                continue
            line = line[line.index('[MBENCH]'):]
            if line.startswith('[MBENCH] begin'):
                began, lines = True, []
            if began:
                lines.append(line)
                print(line, flush=True)
            if began and line == '[MBENCH] done':
                s.close()
                return lines
        if not began and not sent and time.time() - t0 > 4:
            s.write(b'b')                           # (the bench ran before we listened: once more)
            sent = True
    s.close()
    sys.exit('no complete bench within %d s (is the bench build flashed? the monitor closed?)' % timeout_s)


def split_lines(lines):
    """The [MBENCH] lines, a line split where the console lost a newline (two lines run together: the USB console drops bytes)."""
    out = []
    for line in lines:
        parts = line.split('[MBENCH]')
        out += ['[MBENCH]' + x.rstrip() for x in parts[1:] if x.strip()]
    return out


def parse(lines):
    head, mods, glue, synths, voices = {}, [], [], [], {}
    for line in lines:
        try:
            parse_line(line, head, mods, glue, synths, voices)
        except (IndexError, ValueError, KeyError):
            print('(a line cut by the console, skipped: %s)' % line[:90], file=sys.stderr)
    return head, mods, glue, synths, voices


def parse_line(line, head, mods, glue, synths, voices):
    if True:
        w = line.split()[1:]
        if not w:
            return
        if w[0] == 'begin':
            # begin cpu 240 MHz rate 44100 block 32 budget 174149 free-internal N free-psram N
            head.update({'cpu': w[2], 'rate': w[5], 'block': w[7], 'budget': w[9]})
        elif w[0] == 'heap':
            # heap fast used U of C spilled S
            head['heap'] = 'fast heap: %s of %s bytes used, %s spilled into PSRAM' % (w[3], w[5], w[7])
        elif w[0] == 'voice':
            # voice <name...> start A later B psram-later C
            k = w.index('start')
            voices[' '.join(w[1:k])] = (int(w[k + 1]), int(w[k + 3]), int(w[k + 5]), int(w[k + 7]) if len(w) > k + 7 else 0)
        elif w[0] == 'module':
            if 'no-memory' in w:
                mods.append({'name': w[1], 'size': int(w[3]), 'missing': True})
                return
            d = {'name': w[1], 'size': int(w[3]), 'mem': w[5], 'def': int(w[7]), 'def_min': int(w[8]), 'driven': int(w[10]),
                 'max': int(w[13]), 'max_at': w[16], 'psram': int(w[18]), 'start': int(w[20]),
                 'cold': int(w[22]) if len(w) > 22 else 0}
            mods.append(d)
        elif w[0] == 'glue':
            glue.append((w[1], int(w[2])))
        elif w[0] == 'native' and len(w) > 3 and w[1].isdigit() and w[3] == 'scope':
            # native <id> <name> scope voice|global fast F spilled S def D driven V modconst C modramp R modaudio A max M at P=v cold X start T silent 0|1
            k = dict(zip(w[3::2], w[4::2]))
            head.setdefault('native', []).append({'id': int(w[1]), 'name': w[2], 'scope': k['scope'], 'fast': int(k['fast']),
                'spilled': int(k.get('spilled', -1)), 'spill': int(k.get('spill', -1)), 'def': int(k['def']), 'driven': int(k['driven']), 'modconst': int(k['modconst']),
                'modramp': int(k['modramp']), 'modaudio': int(k['modaudio']), 'modslow': int(k.get('modslow', -1)), 'max': int(k['max']), 'max_at': k['at'],
                'cold': int(k['cold']), 'start': int(k['start']), 'silent': k['silent'] == '1'})
        elif w[0] == 'equiv' and len(w) == 3:
            head.setdefault('equiv', {})[w[1]] = w[2]
        elif w[0] == 'ftype' and len(w) >= 3 and w[2].isdigit():
            head.setdefault('ftype', {})[w[1]] = int(w[2])     # (the module as an engine type: engine_modules.h)
        elif w[0] == 'synth':
            # synth <name...> voice V bytes B fx F bytes FB render-idle I render-1 R1 voices N render-6 R6 voices N6
            k = w.index('voice')
            name = ' '.join(w[1:k])
            r = w[k:]
            synths.append({'name': name, 'voice': int(r[1]), 'bytes': int(r[3]), 'fx': int(r[5]), 'fx_bytes': int(r[7]),
                           'idle': int(r[9]), 'one': int(r[11]), 'v1': int(r[13]), 'six': int(r[15]), 'v6': int(r[17])})


def report(head, mods, glue, synths, voices):
    budget = int(head.get('budget', '160000'))
    print('\n%s MHz, %s Hz, %s-frame blocks: budget %d cycles per block' % (head.get('cpu'), head.get('rate'), head.get('block'), budget))
    if 'heap' in head:
        print(head['heap'])
    print('\n%-10s %7s %8s %8s %8s  %-16s %8s %7s %8s' % ('module', 'bytes', 'default', 'driven', 'max', 'max at', 'psram', 'start', 'cold'))
    for d in mods:
        if d.get('missing'):
            print('%-10s %7d  (no memory)' % (d['name'], d['size']))
            continue
        print('%-10s %7d %8d %8d %8d  %-16s %8d %7d %8d%s' % (d['name'], d['size'], d['def'], d['driven'], d['max'], d['max_at'], d['psram'],
                                                             d['start'], d['cold'], '' if d['mem'] == 'internal' else '  (psram only)'))
    print('\nglue (a generated synth around its modules), cycles per block:')
    for name, c in glue:
        print('  %-18s %6d' % (name, c))
    # the engine's own cost per voice: what a voice adds to the render less the Voice alone (100 ms into the note, as the render's
    # measure: 'later'); a mono synth (6 notes, 1 voice) from its one note
    print('\n%-10s %7s %7s %8s %7s %6s %7s %7s %8s %3s %8s %3s %9s' % ('synth', 'voice', 'later', 'psram', 'cold', 'bytes', 'fx', 'idle',
                                                                       '1 note', 'v', '6 notes', 'v', 'eng/voice'))
    for s in synths:
        start, later, psram, cold = voices.get(s['name'], (s['voice'], s['voice'], 0, 0))
        if s['v6'] > 1:
            per = (s['six'] - s['idle']) / s['v6'] - later
        elif s['v1']:
            per = s['one'] - s['idle'] - later
        else:
            per = float('nan')
        print('%-10s %7d %7d %8d %7d %6d %7d %7d %8d %3d %8d %3d %9.0f' % (s['name'][:10], start, later, psram, cold, s['bytes'], s['fx'], s['idle'],
                                                                      s['one'], s['v1'], s['six'], s['v6'], per))


def report_native(head, mods):
    """ADR-050 step 0: the engine's own modules (q15, natively) and, per function, the float catalog's module against them."""
    nat = head.get('native', [])
    if not nat:
        return
    print("\nthe engine's modules, native q15, cycles per block (mod*: every value's modulation input fed, per sample):")
    print('%-4s %-12s %-6s %7s %7s %8s %8s %8s %8s %8s %8s %8s  %-18s %6s %7s' % ('id', 'module', 'scope', 'fast', 'spill', 'default', 'driven',
          'modconst', 'modramp', 'modaudio', 'modslow', 'max', 'max at', 'cold', 'start'))
    prev = None                                         # (the heap's count of spilled bytes adds up: each module's own share)
    for n in nat:
        if n['spill'] >= 0:                             # (a newer bench prints the module's own share)
            n['spill_own'] = n['spill']
            continue
        n['spill_own'] = n['spilled'] - prev if prev is not None and n['spilled'] >= prev else 0
        prev = n['spilled']
    for n in nat:
        print('%-4d %-12s %-6s %7d %7d %8d %8d %8d %8d %8d %8s %8d  %-18s %6d %7d%s' % (n['id'], n['name'], n['scope'], n['fast'], n['spill_own'],
              n['def'], n['driven'], n['modconst'], n['modramp'], n['modaudio'], n['modslow'] if n['modslow'] >= 0 else '-', n['max'], n['max_at'], n['cold'], n['start'],
              ('  (silent: needs a service)' if n['silent'] else '') + ('  (state partly in PSRAM)' if n['spill_own'] else '')))
    by_name = {}
    for n in nat:                                       # (a name in both scopes: the voice's type)
        if n['name'] not in by_name or n['scope'] == 'voice':
            by_name[n['name']] = n
    fmods = {d['name']: d for d in mods if not d.get('missing')}
    ft = head.get('ftype', {})
    print("\nper function: the float catalog module against the engine's q15 one, cycles per block (driven, default values)")
    print('%-10s %-12s %8s %8s %8s %8s %8s %8s %7s' % ('float', 'engine', 'float', 'F type', 'via RM', 'q15', 'q15 mod', 'q15 aud', 'q15/flt'))
    pairs = sorted(head.get('equiv', {}).items())
    # (close functions the catalog does not name as its engine module: a plain ADSR against the engine's Env as well as its Eg)
    pairs += [p for p in (('ADSR', 'Env'), ('EQ3', 'Eq3V'), ('Comp3', 'CompressorV')) if p not in pairs]
    for fname, ename in pairs:
        f, n = fmods.get(fname), by_name.get(ename)
        if not f or not n:
            print('%-10s %-12s  (%s)' % (fname, ename, 'float not measured' if not f else 'engine module not measured'))
            continue
        fl = max(f['def'], f['driven'])
        rm = fmods.get('RM' + ename, {})
        print('%-10s %-12s %8d %8d %8s %8d %8d %8d %7.2f' % (fname, ename, fl, ft.get(fname, 0), max(rm.get('def', 0), rm.get('driven', 0)) or '-',
              n['driven'], n['modramp'], n['modaudio'], n['driven'] / fl if fl else float('nan')))


def write_header(path, board, head, mods, glue, synths, voices):
    """The cost table SynthLab Studio's cycles wallet reads (framework/modular/module_costs.h)."""
    g = dict(glue)
    # the engine's cost per voice: from the poly synths (6 notes, 6 voices), what a voice adds less the Voice alone
    per = [(s['six'] - s['idle']) / s['v6'] - voices.get(s['name'], (0, s['voice'], 0, 0))[1] for s in synths if s['v6'] > 1]
    fixed = [s['idle'] for s in synths if not s['fx_bytes']]
    engine_voice = int(round(max(per))) if per else 0
    engine_fixed = int(round(max(fixed))) if fixed else 0
    o = []
    o.append('#pragma once')
    o.append('// What each module of the modular catalog costs on the board, in CPU cycles per 32-frame block: measured by apps/synthlab\'s')
    o.append('// bench (src/bench_esp32.cpp, environment %s_bench) and written by apps/synthlab/tools/module_bench.py --write. Do not edit by' % board)
    o.append('// hand: run the bench again. SynthLab Studio\'s cycles wallet (the PATCH tab) adds them up against the board\'s budget.')
    o.append('//   cycles   the module alone with its inputs fed (a sine where an input reads silence by default), its default values')
    o.append('//   worst    the slowest of its values one at a time at their minimum / maximum (and each choice); worst_at: which')
    o.append('//   cold     the same block with the instruction cache emptied first: what code left in flash costs at worst')
    o.append('//   start    a note starting (once a note, not a block)        bytes: its state (a voice holds one per instance)')
    o.append('//   engine   the module as an engine type (modular/engine_modules.h, what an engine synth plays), q15 ports; 0: not measured')
    o.append('// The code is in internal RAM (SC_HOT, modules.h), so `cycles` is what a voice pays; what is left in flash (the C library\'s')
    o.append('// expf, powf, tanf ...) is why `cold` stays above it for some.')
    o.append('#include <cstdint>')
    o.append('#include <cstring>')
    o.append('')
    o.append('namespace mod {')
    o.append('')
    o.append('struct ModuleCost { const char *name; uint32_t cycles, worst; const char *worst_at; uint32_t cold, start, bytes, engine; };')
    o.append('struct BoardCost {')
    o.append('    const char *board, *measured;     // where and when')
    o.append('    int cpu_mhz, rate, block;')
    o.append('    uint32_t budget;                  // the cycles of one block of audio at this rate')
    o.append('    uint32_t engine_fixed;            // the engine with no note (its master, the bus): every block')
    o.append('    uint32_t engine_voice;            // what the engine adds for each voice playing (its note, the voice\'s mix)')
    o.append('    uint32_t voice_block;             // a generated synth\'s own work around its modules, a voice, a block')
    o.append('    uint32_t cable_lin, cable_exp, cable_int;   // a modulation cable onto a value of that curve (once a block)')
    o.append('    uint32_t summed_input;            // an input fed by a cable at a depth other than 1, or by two cables')
    o.append('};')
    o.append('')
    o.append('inline constexpr BoardCost kBoardCost = {"%s", "%s", %s, %s, %s, %s, %d, %d, %d, %d, %d, %d, %d};' % (
        board, time.strftime('%Y-%m-%d'), head['cpu'], head['rate'], head['block'], head['budget'], engine_fixed, engine_voice,
        g.get('voice-block', 0), g.get('modcable-Lin', 0), g.get('modcable-Exp', 0), g.get('modcable-Int', 0), g.get('summed-input', 0)))
    o.append('')
    o.append('inline constexpr ModuleCost kModuleCosts[] = {')
    for d in mods:
        if d.get('missing'):
            continue
        base = max(d['def'], d['driven'])
        worst = d['max'] if d['max'] > base * 1.1 else base           # (within 10 %: the timing's noise, not a costlier value)
        at = d['max_at'] if worst > base and not d['max_at'].startswith('-') else ''
        o.append('    {"%s", %d, %d, "%s", %d, %d, %d, %d},' % (d['name'], base, worst, at, d['cold'], d['start'], d['size'],
                                                       head.get('ftype', {}).get(d['name'], 0)))
    o.append('};')
    o.append('')
    o.append('inline const ModuleCost *module_cost(const char *name) {')
    o.append('    for (const ModuleCost &c : kModuleCosts) if (!std::strcmp(c.name, name)) return &c;')
    o.append('    return nullptr;                   // not measured (a module added since, an engine module)')
    o.append('}')
    o.append('')
    o.append('}  // namespace mod')
    data = '\r\n'.join(o) + '\r\n'
    open(path, 'w', encoding='utf-8', newline='').write(data)
    print('\nwrote %s (%d modules; engine %d + %d a voice)' % (path, len([d for d in mods if not d.get('missing')]), engine_fixed, engine_voice))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM8')
    ap.add_argument('--timeout', type=int, default=240)
    ap.add_argument('--log', help='keep the raw [MBENCH] lines in this file')
    ap.add_argument('--from', dest='src', nargs='+', help='read saved logs instead of the board (several: merged, the first one\'s '
                    'engine and synths; a module measured twice: the later log\'s)')
    ap.add_argument('--write', help='write the cost table (framework/modular/module_costs.h)')
    ap.add_argument('--board', default='hwv1', help='the board measured, for the table (hwv1 / hwv2)')
    a = ap.parse_args()
    if a.src:
        logs = [split_lines(open(f, encoding='utf-8').read().splitlines()) for f in a.src]
        lines = logs[0]
    else:
        logs = [read_board(a.port, a.timeout)]
        lines = logs[0]
    if a.log:
        open(a.log, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    parsed = parse(lines)
    for extra in logs[1:]:                              # (the RM build's modules: added, or replacing a module of the same name)
        mods = parsed[1]
        for d in parse(extra)[1]:
            mods[:] = [m for m in mods if m['name'] != d['name']] + [d]
    for extra in logs[1:]:                              # (the native engine modules, the pairs: from every log)
        h = parse(extra)[0]
        parsed[0].setdefault('native', []).extend(h.get('native', []))
        parsed[0].setdefault('equiv', {}).update(h.get('equiv', {}))
        parsed[2].extend(x for x in parse(extra)[2] if x[0] not in dict(parsed[2]))
    report(*parsed)
    report_native(parsed[0], parsed[1])
    if a.write:
        write_header(a.write, a.board, *parsed)


if __name__ == '__main__':
    main()
