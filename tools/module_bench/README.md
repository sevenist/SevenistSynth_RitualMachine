# Module bench

Every engine module type, alone, per 32-frame block: the board's own (q15) and SynthLab Studio's float modules (the `F` types of
`framework/modular/engine_modules.h`, ids 128..191). One firmware, two targets:

| Env | Where | Unit | Use |
|---|---|---|---|
| `qemu` | Espressif's QEMU, machine `esp32s3`, `-icount shift=5,sleep=off` (`--shift`) | instructions (the same count on every run) | most optimization rounds: did the instruction count go down, is the output the same |
| `hwv2` | the second prototype (flash COM12, console COM13) | CPU cycles | now and then: what QEMU cannot model (flash cache, PSRAM, FPU latency stalls) |

Per module: `def` (inputs silent), `driven` (a saw into the audio inputs), `modramp` (every value modulated per sample by a slow
ramp), `modslow` (the same ramp marked slow, ADR-050: the block-rate reading), `start` (a note's reset), the fast heap used, what
spilled, `silent` (the output stayed 0: the module needs a sample, a patch or an impulse response), and **`sum`**: a checksum of
everything it wrote in the driven and modulated cases (a module without outputs, VoiceOut: the left halves of the buses it adds to). A change that keeps `sum` is exact (bit for bit the same output); one that
changes it changed the sound.

```powershell
C:\.platformio\penv\Scripts\pio.exe run -d tools/module_bench -e qemu                       # build (never a bare pio)
C:\.platformio\penv\Scripts\python.exe tools\module_bench\module_bench.py --qemu --save before.json
# ... change a module ...
C:\.platformio\penv\Scripts\python.exe tools\module_bench\module_bench.py --qemu --only Filter FFilter --baseline before.json
C:\.platformio\penv\Scripts\python.exe tools\module_bench\module_bench.py --port COM13 --only Filter   # the board (env hwv2 flashed)
```

`--max` adds each value at its extremes (slow). Among the `--only` names, `param=value` sets a value after the defaults (every listed
module that has it) and `audiomod` makes the modulated cases an audio-rate sine (1.5 kHz) instead of the slow ramp, e.g. the Filter's
per-sample path at its three qualities: `--only Filter audiomod fm_q=2`, or the reverb's half-rate tank: `--only Reverb quality=1`. `--qemu-args "-m 4M"` gives QEMU PSRAM if its build supports it; without PSRAM the
bulk heap is what internal RAM is left, and big modules (Delay, Pads) print `nomem`.

QEMU's counter counts instructions, not cycles: an optimization that removes **stalls** (FPU latency chains, flash or PSRAM waits)
without removing instructions does not show there. Check those on the board.

Installing QEMU (Windows): extract `qemu-xtensa-softmmu-esp_develop_9.2.2_20260417-x86_64-w64-mingw32.tar.xz` from
github.com/espressif/qemu/releases into `%USERPROFILE%\.espressif\tools\qemu-xtensa\` (the runner looks for
`qemu\bin\qemu-system-xtensa.exe` there, or pass `--qemu-bin`).

Host-side exactness of the same kind: `framework/tests/test_exact.cpp` (a checksum per module pinned, default configuration).
