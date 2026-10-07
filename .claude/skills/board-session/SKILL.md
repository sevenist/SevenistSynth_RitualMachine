---
name: board-session
description: Authorizes Claude, for the current session only, to build and upload the firmware to the ESP32-S3 prototype (HWV1, COM8), to add or change the dev serial commands (DEV_SERIAL_CMD), and to test the machine over serial. Only run it when the user explicitly invokes /board-session.
disable-model-invocation: true
---

# Board session: flash, serial commands, tests (THIS SESSION ONLY)

The standing rule of this project is **"the user flashes, Claude never does"** (CONTINUE.md, memory). Invoking this skill lifts that rule
**for the current conversation only**. It does not carry over: a new session, a resumed summary that does not show the user invoking
`/board-session`, or another task in a later session falls back to "the user flashes". Do not write this permission into memory,
CONTINUE.md or any doc as a standing permission.

When the skill loads, say in one line: "Board session: I may build, upload to COM8, change the dev serial commands and test, until this session ends."
Then ask what to work on if the user has not said it.

## 1. What is authorized, and what is not

| Authorized in this session | Still needs the user |
|---|---|
| Build and upload with the penv pio (section 2) | Anything audible: pause and ask for listening feedback (section 6) |
| Toggle the **existing** dev flags of `platformio.ini` for a build (section 3) | Adding a new build flag (section 3.3: ask first) |
| Add / change dev commands in `src/platform/esp32/serial_cmd_esp32.cpp` behind `DEV_SERIAL_CMD` | Changing release behaviour, pins (`#ifdef HWV1` rule), or `platformio.ini` permanently |
| Run `tools/serial_test.py` and read the board (section 4) | Commits (only when asked) |

Never flash a change that does not build cleanly and, for engine code, has not passed the host tests (`powershell -NoProfile -File tools/build_engine_tests.ps1`).

## 2. Tools: exact paths on this machine (confirmed by the user, 2026-10-07)

| What | Path | Notes |
|---|---|---|
| PlatformIO | `C:\.platformio\penv\Scripts\pio.exe` | **Never bare `pio`**: the one on PATH (Python313\Scripts) tries to recreate the penv and destroys it (happened 2026-10-05) |
| Python | `C:\.platformio\penv\Scripts\python.exe` (3.11.7, pyserial 3.5) | **Never bare `python`**: on PATH it is MSYS2 `C:\msys64\ucrt64\bin\python.exe` 3.12.9, no pyserial. Not `py` either (3.13) |
| Board port | `COM8`, 115200 baud | ESP32-S3 USB-Serial/JTAG (VID 303A). `COM9` is a CH343 adapter, not the board |
| addr2line | `C:\.platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32s3-elf-addr2line.exe` | crash backtraces, against `.pio\build\waveshare_esp32s3_pico\firmware.elf` of the flashed build |

If any of these is missing or the version differs, stop and ask the user; do not repair or reinstall a Python environment yourself.

Build and upload (PowerShell; log to a file):

```
C:\.platformio\penv\Scripts\pio.exe run *> $env:TEMP\pio_build.log ; $LASTEXITCODE
C:\.platformio\penv\Scripts\pio.exe run -t upload *> $env:TEMP\pio_upload.log ; $LASTEXITCODE
```

- **Never pipe pio into `Select-Object -First`**: it closes the pipe and kills the upload half way (half an image on the board). Read the log file afterwards.
- One program per serial port: an open monitor (user's or PlatformIO's) makes the upload fail with "port busy". Retry once (often transient), then ask the user to close their monitor.
- The board reboots after the upload. To see the boot log, run `serial_test.py --raw --seconds 25` right after the upload returns.
- After an upload, **prove the new build runs**: a reply or a number that only the new build can give (a new command answering, a changed log line). The old binary still running has fooled this project before.

## 3. Build flags: use the ones that exist

### 3.1 The existing dev flags (`platformio.ini`, `build_flags`)

| Flag | State in the file | Use |
|---|---|---|
| `DEV_SERIAL_CMD` | on | text commands over serial, needed by every test here |
| `DEV_OUTPUT_GAIN_PCT=25` | on | headphone protection; leave it |
| `DEV_SPEAKER_DEFAULT=0` | on | built-in speaker off at boot |
| `DEV_BOOT_DELAY_MS=500` | on | the monitor catches the boot log |
| `HWV1_DEBUG_AUDIO` | commented | `[AUDIO]` render time per block, once a second |
| `ENGINE_PROFILE` | commented | `[PROF]` / `[SEC]` / `[OSC]` cycles per module (with `HWV1_DEBUG_AUDIO`) |
| `HWV1_DEBUG_UI` | commented | slowest UI step while the controls are used |
| `HWV1_DEBUG_INPUT` | commented | raw key events |
| `HWV1_BENCH` | commented | DSP micro benchmark at boot (`[BENCH]`) |
| `HWV1_TEST_TONE` | commented | 440 Hz sine instead of the engine (DAC / amp path) |

`serial_test.py` measurements need `ENGINE_PROFILE` and `HWV1_DEBUG_AUDIO`. Re-read `platformio.ini` at the start of the session: the user changes it, and their setting wins over this table.

### 3.2 Turn a commented flag on for a build, without editing `platformio.ini`

PlatformIO appends `PLATFORMIO_BUILD_FLAGS` to `build_flags`:

```
$env:PLATFORMIO_BUILD_FLAGS = "-DENGINE_PROFILE -DHWV1_DEBUG_AUDIO"
C:\.platformio\penv\Scripts\pio.exe run -t upload *> $env:TEMP\pio_upload.log ; $LASTEXITCODE
Remove-Item Env:\PLATFORMIO_BUILD_FLAGS
```

Always remove the variable after the build so the next build is the one the file describes. Say which extra flags the flashed build has.
Edit `platformio.ini` only if the user asks for a permanent change.

### 3.3 A new flag: only if no existing one fits

Before adding one, check that no existing flag or serial command can do the job (a runtime serial command is usually better than a build flag:
no rebuild, no flag to clean up). If a new flag is really needed:
- ask the user first, with the reason and why the existing ones do not fit;
- name it `DEV_*` (dev feature) or `HWV1_DEBUG_*` (hardware logging), keep it removable (code compiles to nothing without it), hardware code inside `#ifdef HWV1`;
- document it in `platformio.ini` (commented line with "dev only: ...") and in the flag table of `DEVELOPING.md` ("ESP32 firmware and performance work").

## 4. Testing the machine over serial

```
C:\.platformio\penv\Scripts\python.exe tools\serial_test.py --raw --seconds 3                       # what runs on the board (safe, no reset)
C:\.platformio\penv\Scripts\python.exe tools\serial_test.py --cmd "ping" --raw --seconds 2           # commands answer? ("no answer" = no DEV_SERIAL_CMD in the flashed build)
C:\.platformio\penv\Scripts\python.exe tools\serial_test.py --cmd "patch startup" --chords 1,3,6 --hold 6 --log run.txt
```

- The port opens with DTR / RTS low: opening it does not reset the board. Default port COM8, 115200.
- `--raw` prints only lines newer than the `--cmd` pauses; to see the replies to `--cmd`, use the normal mode or read the `--log` file.
- The command list and examples: `human_docs/SERIAL_COMMANDS.md`. Patches set over serial live in RAM until the next reset.
- Report CPU (`cycles per engine block`, `blocks over budget`; any > 0 is an audible dropout) and, for the sampler, the card counters, before / now in a small table.
- Delete `run*.txt` logs when done; numbers that matter go into CONTINUE.md.
- For optimization work follow the skill `esp32-optimize` (its loop in section 2b), with the paths of section 2 above.

## 5. Changing the serial commands

- File: `src/platform/esp32/serial_cmd_esp32.cpp` (everything inside `DEV_SERIAL_CMD`). Follow its pattern: `if (!strcmp(line, "name") ...) { ...; Serial.printf("[CMD] ...\n", ...); return; }`, a one-line comment with the syntax, and always a `[CMD]` reply so the tool can see it worked.
- Commands run on the UI core: a change to the synth goes through the same path as the UI (rebuild like leaving the menu, or a live engine command). Never change the graph shape from a frequently sent command (100-170 ms audio stall).
- When a command is added or changed, update in the same session: `human_docs/SERIAL_COMMANDS.md` (syntax + example), the `DEV_SERIAL_CMD` row of the flag table in `DEVELOPING.md`, and `tools/serial_test.py` if the tool should use it.
- Edit traps: lines with `\n` inside C strings go through the Edit tool (scripted edits through this harness lose one backslash level; a heredoc turns `\n` into a real newline). Many files are CRLF: keep the line endings (never `sed -i` in Git Bash on them). After a scripted printf edit, grep that the format string is on one line.

## 6. Pause for the user

Stop the loop and ask with `AskUserQuestion` (concrete options, e.g. "clean / click / crackle with many notes / other"):
- after any change that can alter the sound (not after an exact optimization proven by an identical checksum), saying what to play and what to listen for;
- when a result gets worse for a reason you cannot explain, or two rounds in a row change nothing;
- before a quality trade-off or a new build flag (options with pros and cons; the user decides).

## 7. Before the session ends

- Make sure no `PLATFORMIO_BUILD_FLAGS` is left set, and say which build is on the board (with which extra flags).
- Update CONTINUE.md (what was flashed, measured, verified by ear or not) and the docs touched in section 5. Do not record this permission as permanent.
- Delete scratch logs. Commit only if asked.
