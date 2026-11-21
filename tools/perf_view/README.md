# perf_view: the engine's live performance on the board

A live view of what the ESP32-S3 spends its audio time on, measured on the board itself (an emulator does not model the flash
cache, PSRAM or FPU stalls, which are most of the cost here). The firmware streams its profile over the native USB port. This
tool shows it in the browser, or prints a table without one.

## Run it

1. Flash the profiling build of the Ritual Machine on HWV2: `.\upload.ps1 -Env hwv2_perf -Port COM12` (env `hwv2_perf` in
   `apps/ritual_machine/platformio.ini`: `ENGINE_PROFILE`, `ENGINE_PROFILE_PMU`, `DEV_PERF_STREAM`). Dev only, never released.
2. Close the serial monitor (one program per port), then:

```
C:\.platformio\penv\Scripts\python.exe tools\perf_view\perf_view.py              # finds the board (VID 303A, COM13 on HWV2)
```

and open http://localhost:8765. `--demo` runs the page on made-up data, with no board.

Without a browser (for the optimization loop: commands, settle, measure, table, compare):

```
python tools\perf_view\perf_view.py --summary 5 --cmd "patch startup" --cmd "chord 6" --save before.json
python tools\perf_view\perf_view.py --summary 5 --cmd "patch startup" --cmd "chord 6" --baseline before.json
```

The page's Export writes the same JSON, so a page snapshot and a headless run can be compared either way (Load baseline).

## What it shows

- **Render load**: every render of 64 frames, as % of its budget (`cpu_hz * 64 / sample_rate`). The band shows the fastest and
  slowest render of each pixel column, the line their mean. Red marks are **audio stalls**: the I2S DMA ran out (a real dropout).
  A render over budget alone is not a dropout: the DMA ring of 6 x 64 frames absorbs a few.
- **Where the cycles go**: per module type (all voices summed), cycles per engine block as % of its budget, over the last 60 s.
  "Outside modules" is the rest of the render: mixing steps, voice handling, the command ring.
- **Modules**: the table behind it, averaged over the window you choose (1 s, 5 s, 30 s or since Reset), with the per-call cost,
  the two counters, the module's cycles in the slowest render of the window, and the delta against a baseline.
- **Slowest render** since Reset, split by module: which module made the spike.
- **Distribution** of all renders since Reset (log count): a long tail means spikes, not average load.
- **Inside modules**: the stage timers (`SEC_MARK`: reverb, delay, spectral, pads), the oscillator engines and the MI models.
- **Console**: the board's text output and a command line to its serial port (all of `apps/ritual_machine/docs/SERIAL_COMMANDS.md`).

## The two counters (`perf pmu MODE`)

The Xtensa LX7 core has two hardware performance counters. The engine reads them around every module (an inline `rer`), so each
module's cycles split into what it waited for:

| mode | counter 0 | counter 1 | read it as |
|---|---|---|---|
| `stalls` (default) | instruction stalls (all) | data stalls (all) | code waiting on flash (I-cache misses), data waiting on PSRAM / the store buffer |
| `cache` | I-cache miss stalls only | D-cache miss stalls only | the same, cache misses alone |
| `insn` | instructions retired (shown as IPC) | pipeline bubbles | IPC near 1 = compute-bound; low IPC = stalls / latency chains |
| `off` | | | |

How to act on it: high code stalls point to a hot function that runs from flash and keeps missing the 16 KB cache (`IRAM_ATTR`,
smaller code, fewer functions in the loop). High data stalls point to state or tables in PSRAM (the fast heap's `spilled` bytes,
random access into big tables). Low IPC with few stalls means latency chains (FPU divide, dependency chains), and the fix is to
batch independent work.

They are per core and count at task level, set by the audio task itself (core 1). In `cache` mode, a module that waits on both
has both numbers; the two do not add up to its cycles.

## Cost of measuring

Per module, two ERI reads and the cycle counter (a few tens of cycles). Per render, one profile take (~40 rows) and the
accumulation. Compare totals only between builds with the same flags: the `[PROF]` text report of `HWV1_DEBUG_AUDIO` and this
stream do not read the counters the same way.

## Packet format (version 1)

One packet per line: `@PF ` + base64 + `\n`. The binary ends with a Fletcher-16 checksum (2 bytes, little endian) over what comes
before it. All integers are little endian. The firmware side is `apps/ritual_machine/src/platform/esp32/perf_stream_esp32.cpp`.

`N` names, sent when new ones appear and again every 2 s:
`u8 'N', u8 version, u8 kind (0 module, 1 osc engine, 2 MI model, 3 stage), u8 first, u8 count, count x (u8 len, chars)`

`S` stats, 20 a second by default (`perf rate N`):

```
u8 'S', u8 version, u16 seq, u32 t_ms, u32 budget (cycles a render), u16 engine block, u32 sample rate, u32 cpu MHz, u8 pmu mode,
u32 renders, u32 engine blocks, u32 cycles sum, u32 slowest render, u32 renders over budget, u32 counter0 sum, u32 counter1 sum,
u32 audio stalls (since boot), u32 graph builds (since boot), u32 packets dropped (since boot),
u32 fast heap used, cap, high water, u32 bulk heap used, u32 spilled, u32 internal free, largest block, u32 PSRAM free,
u16 n, n x u32 render cycles (the timeline),
u8 n, n x (u8 module id, u32 calls, u32 cycles, u32 counter0, u32 counter1, u32 cycles in the slowest render),
u8 n, n x (u8 kind, u8 index, u32 cycles)
```

All sums cover the renders since the previous packet. A packet that does not fit in the port's buffer is dropped and counted;
nothing waits for the host.
