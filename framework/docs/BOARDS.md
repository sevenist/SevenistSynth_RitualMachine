# The boards and the ESP32 firmware

A board is a folder of `framework/boards/` that implements the HAL (`framework/hal/`) and lists its controls in a table; an app maps
those controls onto its own roles (the Ritual Machine: `core/boards/map_*.h`). The firmware of an app is built from its own
`platformio.ini`; what is said here of the app's side is the Ritual Machine's, the first app on the boards. Its build flags, its
serial log and its serial test tool are in its guide: [apps/ritual_machine/docs/DEVELOPING.md](../../apps/ritual_machine/docs/DEVELOPING.md).

## The desktop board (the simulator)

**The simulated board (`-Board`).** The panel window is drawn from a board's control table (`hal/hal_input.h`: each control's kind
and its place `px`, `py`, the centre of its part in half key pitches), and the app sees that board: its table, its name, so the
Ritual Machine picks that board's map. The board is chosen when the program is built, and each board is a program of its own with its own objects:

| Command | Board | Program | What is drawn |
| --- | --- | --- | --- |
| `.\build.ps1`, `.\run.ps1` | `panel` (default) | `build\oled_sim.exe` (`build\obj`) | the future prototype: 4 key columns, 2 encoders, 8 knobs, joystick, Play, B1..B3 |
| `.\build.ps1 -Board hwv1`, `.\run.ps1 -Board hwv1` | `hwv1` | `build\oled_sim_hwv1.exe` (`build\obj_hwv1`) | the first prototype: 4 function keys + 4 x 8 keys, 7 pots without end stops, joystick |

After a change, build the board you run: `run.ps1` says so when the program it starts is older than another board's. `-App` and
`-Board` combine (`.\build.ps1 -App template -Board hwv1`). The files: `boards/desktop/sim_board.h` (the board behind `SIM_BOARD_<NAME>`;
`Get-SimBoard` in `framework/tools/sim_build.ps1` sets the define and adds the board's table to the sources), `sim_board.c` (the table as
parts and their places: controls at the same place are one part, an encoder with its push, a joystick's axes with its push; no SDL,
`tests/ui/test_controls.c` lays out both boards and checks that no part sits on another), `panel_sim.c` (draws the parts, the mouse),
`input_sim.c` (the PC keys, the pots' two readings). **PC keys come from a part's kind and place, not from the board**: a matrix key takes
the key of its row and column in the block F1.. / 1.. / Q.. / A.. / Z.. (8 wide); what turns (encoders, pots without end stops) takes a
pair each, `[ ]`, `; '`, `- =`, `9 0`, `O P`, `L .`, keypad `- +`, the two parts a board names first (`SIM_TURN_KEYS_FIRST`: the panel's
encoders, HWV1's pots 1 and 2, the Ritual Machine's rows and value) then the table's order, and `\` `/` push the first two; buttons take
Space, Enter, Backspace, Left Shift in the table's order; the joystick the arrows and Right Ctrl. On `hwv1`: `[ ]` = pot 1, `; '` = pot 2,
`- =` = pot 0 (volume), `9 0` / `O P` / `L .` / keypad `- +` = pots 3 / 4 / 5 / 6. A pot without end stops reports steps, or a position when the
app's map asks for one (a step moves it by 24 of 1023; the real board's noise thresholds are not simulated). **HWV1's panel places are a
guess** (`boards/hwv1/hwv1_controls.c`): correct `px`, `py` there against the board.


## ESP32 firmware and performance work

The firmware runs on the first prototype ("HWV1": Waveshare ESP32-S3-Pico module, 16 MB flash, 2 MB PSRAM) and was brought up and optimized on the real board. The hardware equals the
SynthBox repository (github.com/clement-chupin/SynthBox, `HWConfig.h`): a 4 x 8 + 4 Fn key keyboard on a TCA8418 over I2C, 7 relative (quadrature) knobs behind a 16-channel analog mux, a joystick,
an SH1107 128 x 128 OLED, a PCM5102A DAC + an LM4871 speaker amplifier (analog, mono, on the DAC's left channel; shutdown on GPIO 5, active high) and WS2812 LEDs. The second prototype
("HWV2", GRV-mini: its section below) has the controls of the simulator's panel. Each board lists its own (`hwv1_controls.h`, `hwv2_controls.h`; the wiring behind
them: `hwv1_layout.h`, `hwv2_layout.h`), and the Ritual Machine maps them onto its roles in `core/boards/map_hwv1.h`, `map_hwv2.h`.

```powershell
C:\.platformio\penv\Scripts\pio.exe run -d apps/ritual_machine                  # build (use this pio: another Python's pio recreates the penv and breaks it)
C:\.platformio\penv\Scripts\pio.exe run -d apps/ritual_machine -t upload        # flash. THE USER flashes the board, Claude never does
C:\.platformio\penv\Scripts\python.exe apps\ritual_machine\tools\serial_test.py   # measure on COM8 (close any serial monitor first)
.\upload.ps1 [-Port COM8] [-App template] [-Env card_reader] [-BuildOnly]        # the same from the repo root, with the right pio and a check that the board is there
```

In VS Code: PlatformIO's own Upload button only appears with the app's folder open (`apps/ritual_machine`, the one that holds
`platformio.ini`; open `oled_sim.code-workspace` and pick `ritual_machine` in PlatformIO's status bar). From the repo root use
**Terminal > Run Task > "Firmware: upload (Ritual Machine)"** (tasks in `oled_sim.code-workspace`; `upload.ps1`).

> **Warning: never type a bare `pio`.** `pio` on PATH is `Python313\Scripts\pio.exe` (PlatformIO 6.2.0 installed into the user's Python 3.13). It sees that
> `C:\.platformio\penv` was made with Python 3.11, tries to recreate it with 3.13, fails (`uv installation via pip failed with exit code 106`) and leaves the penv
> without `pio.exe` or `pyvenv.cfg`: no build, no flash. This happened twice, the last time on 2026-10-05. Repair: close VS Code (its PlatformIO extension keeps files in
> the penv open), delete `C:\.platformio\penv`, then `C:\.platformio\python3\python.exe -m venv C:\.platformio\penv` and
> `C:\.platformio\penv\Scripts\python.exe -m pip install platformio` (or reopen VS Code and let the extension reinstall it). The bundled `C:\.platformio\python3`
> (3.11.7) and the toolchains in `C:\.platformio\packages` / `platforms` are not touched by this failure.

### Files (the board: `framework/boards/hwv1/`; what the ESP32 boards share: `framework/boards/esp32/`; the app's side: `apps/ritual_machine/src/platform/esp32/`)

The app's own: `main_esp32.cpp`, `audio_esp32.cpp`, `samples_esp32.*`, `serial_cmd_esp32.*`, `bench_esp32.*`, `usb_storage_esp32.cpp`. In `boards/esp32`: `board_pins.h`, `board_esp32.h`, `display_esp32.cpp` (a board gives it its panel: `display_panel.h`), `leds_esp32.*` (a board gives it its chain's order: `board_led_of_control`), `audio_out_esp32.cpp`, `mux_esp32.*`, `power_esp32.*` (a board gives it how its switch and joystick read: `power_esp32.h`), `sd_card.h`, `card_task.*`, `storage_sd.*`. The rest is the first prototype's; the second prototype (`.\upload.ps1 -Env hwv2`; the default environment's pins do not fit it) has its own `boards/hwv2`. The debug flags keep their `HWV1_` names (`HWV1_DEBUG_AUDIO`, `HWV1_SD_BENCH`...) in the shared files too.

| File | What |
| --- | --- |
| `main_esp32.cpp` | setup / loop: power, serial (115200, non-blocking TX), the hand-over to the card reader's firmware when the power button is up (`board_card_reader`), optional boot delay, input task, audio, app. The UI loop runs on core 0; when the power button goes up it saves (`app_save_now`) and lets the supply go |
| `board_pins.h` | the pin map, inside `#ifdef HWV1` (I2S, OLED, mux, joystick, keyboard I2C, LED, power, SD), the power button's level and the battery's scale |
| `hwv1_controls.h`, `hwv1_controls.c` | the board's control table (`hal_input.h`): the ids (`HWV1_KEY(row, col)`: the 4 function keys = row 0, the 4 x 8 note keys = rows 1..4; `HWV1_POT_0..6`; `HWV1_JOY_X / _Y / _SW`) and, per control, its name, kind, matrix place, panel place (not measured: a guess to correct against the board; the simulator built with `-Board hwv1` draws the board from it) and LED. Plain data: it also compiles on the desktop (the host tests read it) |
| `hwv1_layout.h` | the wiring behind the table: the key controller's orientation (flip flags), each pot's direction (`kHwv1KnobSign`), counts per revolution and the noise thresholds, the joystick's levels, the mux's two other channels. Which role a control plays is the app's (`core/boards/map_hwv1.h`) |
| `kbd_tca8418.*`, `mux_esp32.*`, `input_esp32.cpp` | keyboard (Adafruit TCA8418 on Wire1, config watchdog, bus recovery), mux reads, and the scan task on core 0 that turns them into events with the board's ids (quadrature decode with hysteresis, joystick). A pot reports steps, or a position once the app asked for it (`input_control_mode`, before `input_esp32_init`) |
| `display_esp32.cpp` | `U8G2_SH1107_128X128_F_HW_I2C`, 400 kHz (a full frame takes ~120 ms). `display_send()` (HAL, in place of `u8g2_SendBuffer`) copies the frame and wakes the `display` task, which sends only the changed 8 x 8 tiles; this panel's column offset (96) wraps, so a run starting at tile x is sent with the offset moved to (96 + 8x) mod 128. `app_step` also puts a redraw off while input is queued (`input_pending()`) |
| `power_esp32.*`, `board_esp32.*` | `power_esp32.cpp` is shared by both prototypes (the logic below, the battery's smoothing); each board's `board_esp32.cpp` gives it its switch, joystick and audio resume (`power_esp32.h`). On the second prototype the switch has no pin: it is read with the battery on mux channel 7 (switched off or no battery = 0 V), so with no battery it cannot tell off from on, hence `HWV2_USB_STORAGE` = 0 by default. Power hold (PWR_ON_EN), amplifier / DAC enable (XSMT; the LM4871's SPK_SD driven LOW = on, HIGH = shutdown: at Spk Off, at power-off and in the card reader; read floating once at the start for the `[BOARD]` line). **The power button** is a latching switch, down = on, read on PWR_SENSE (`HW_PWR_SENSE_ON`, `board_power_button_on`); the USB cable feeds the board whatever it says, so a board that still runs after the latch was let go with the button up is on the cable (`board_power_started_off` at the start, `board_power_poll` / `board_power_off` later): on the battery the board stops, on the cable it restarts into the card reader (`board_card_reader`: `esp_ota_set_boot_partition` on the second slot, then a restart; with no firmware there the synth goes on). The joystick held at such a start keeps the synth (the button is ignored until it is next used). `board_usb_host`: a computer on the cable (the USB-Serial/JTAG port sees its frames). **The battery** (`hal_power.h`): mux channel 1 through a divider, millivolts on the pin x `HW_BATT_FULL_SCALE_MV` / 3300, smoothed, 6.0 .. 8.4 V = 0 .. 100 %, under 4 V = no battery; read by the input task every 100 ms with the mux's spare channel 0 (`HWV1_MUX_AUX`, kept as a button state, not a control yet). Serial (dev): `power`, `storage` |
| `usb_storage_esp32.cpp` | **the card reader, a second firmware** (`[env:card_reader]`, built with `HWV1_CARD_READER` from this file, `board_esp32`, `display_esp32`, `mux_esp32` and the card driver; 540 KB, in the second application slot at 0x650000; `pio run -d apps/ritual_machine -e card_reader -t upload`, once). With the power button up on the cable the TF card is a USB drive on the computer: TinyUSB mass storage over the card driver's raw sector access (`sd_card_raw_*`), its own screen, the USB transceiver taken from the serial console and given back before it restarts into the synth (the power button down, or the joystick held 2 s and kept held). Its first act is to point the bootloader back at the synth. Why not a mode of the synth: TinyUSB takes 23 KB of static internal RAM whether it runs or not (measured: the synth's link failed by 10 KB). `HWV1_USB_STORAGE=0` (synth) never starts it. On the board (2026-10-11): the drive mounts and reads at 543 KB/s (full-speed USB); writing and the way back to the synth not tried yet |
| `leds_esp32.cpp` | LED HAL (`hal_leds.h`): FastLED 3.10.6 (pinned; its first RMT channel uses DMA on the S3), SK6812 colour order RGB, key -> chain map (physical columns, checked on the board), brightness capped at `HW_LED_MAX_BRIGHTNESS` = 20 % (power: the user's limit), sends only when the frame changed. Serial (dev): `leds N` (chain LED N red for 5 s), `leds all`, `leds off` / `leds on` (stop / resume every transfer). What the keys show: `app/key_leds.c` (play feedback, called at the end of `app_step`) |
| `audio_out_esp32.cpp` | the audio output (`hal_audio_out.h`): I2S (MSB format, no MCLK) and the audio task on core 1, which calls the app's render function for every 64 frames; applies `DEV_OUTPUT_GAIN_PCT`, the speaker channel's level (`board_speaker_level`), the gate that stays closed until the loading screen has ended; with `HWV1_DEBUG_AUDIO` it measures and calls the app's report once a second |
| `audio_esp32.cpp` | the app's side: sizes the fast heap (internal RAM) and the bulk heap (PSRAM), starts the engine and hands `engine_synth_render` to the output; prints the `[AUDIO]` / `[PROF]` / `[SEC]` / `[OSC]` / `[HEAP]` lines from the output's report; `core/audio.h` forwarded to the bridge |
| `card_task.*` | **the card task** (core 0): mounts and watches the card, the small files of `hal_storage.h`, the folders the app listed, the card events; an app adds its own card work through `card_hooks_t` (in the loop's order: calm work, always, idle, pump). `samples_esp32.cpp` is the Ritual Machine's: the sample library on those hooks |
| `bench_esp32.*` | boot micro benchmark (`HWV1_BENCH`): cycles per operation, RAM vs PSRAM |
| `sd_card.*`, `sd_card_spi.cpp`, `storage_sd.*`, `samples_esp32.*` | the TF card: `sd_card.h` is the interface (mount as FAT at `/sdcard`, probe the read time, alive / identity check); `sd_card_spi.cpp` is HWV1's own small SPI-mode driver (reads with the data CRC16 checked and a bad block read again, `[SD] data CRC errors` in the log; single-block writes for the settings files, each checked with CMD13; the data token is polled `HWV1_SD_PROBE` (8) bytes per SPI call; above 25 MHz only after a CMD6 switch to high-speed mode, which the prototype's SL32G card does not have, so it runs at 25 MHz unless `-DHWV1_SD_OVERCLOCK=1`; `-DHWV1_SD_BENCH` prints raw, scattered and file read times at boot; registered with FATFS; ESP-IDF's sdspi host spent 40 ms before every command on this board); a new prototype with an SDMMC slot replaces that one file. The card is polled once a second (no card-detect pin): inserted -> a timed read test and a check of the folders of `STORAGE_FOLDERS` -> a card event (`storage_poll_event`, hal_storage.h) that the app shows as popups: "Card inserted", "SD CARD TOO SLOW" (slower than 15 ms per sector: not used for samples; the synth runs as if there were no card), and an ASK when folders are missing: Yes runs `storage_make_folders` on the card task (the old layout's `/samples` and `/keys.cfg` move into `/system`, then the missing folders are made, then a rescan); removed -> "Card removed"; removed or swapped -> the catalog is reset, `SdStorage` is the sampler's `StorageDevice`, `samples_esp32` is the I/O task (core 0, priority 5, see "Tasks and priorities"): mount, list `/sdcard/system/samples/*.smp` into the catalog (the header of each file carries the amplitude overview, so a scan reads one block per file), run the loader, and the settings-file jobs of `hal_storage.h` (`storage_read` / `storage_write` post a job and wait up to 1.5 s; the task runs it between loader reads; a write goes to `<name>.tmp`, then replaces the file). Cook files on the PC (`samples_src/` + build.ps1); there is no .wav / .mp3 import on the board. `SdStorage` reads through an 8 KB internal DMA-capable bounce buffer (a PSRAM destination would make the SD driver issue one command per 512 bytes) |
| `serial_cmd_esp32.*` | dev commands over the serial port (`DEV_SERIAL_CMD`) |

### The second prototype (HWV2, GRV-mini)

The KiCad project is outside the repo (`D:\PRODUCT DESIGN\PCBWORK\IN PROGRESS\GRV-mini`; its `REV-B-NOTES.md` lists the rev A faults and bodges). Its files
are KiCad 10: read the wiring from `GRV-mini.kicad_pcb` (the pads' nets), as KiCad 9's `kicad-cli` cannot load them. The mux nets `i0..i15` are not the channel
numbers: `hwv2_layout.h` has the channels by U2's pins. The default environment is the first prototype's, whose pins do not fit this board:

```powershell
.\upload.ps1 -Env hwv2 -Port COM12                 # the synth (the module's CH343 port; its log prints on the native USB port, COM13 here)
.\upload.ps1 -Env hwv2_card_reader                 # its card reader, in the second slot, once (the synth running, the serial monitor closed)
.\build.ps1 -Board hwv2; .\run.ps1 -Board hwv2     # the simulator with this board's table (the same as the panel); the UI tests at its height: tools\build_ui_tests.ps1 -Height 160
```

| File (`boards/hwv2/`) | What |
| --- | --- |
| `hwv2_controls.*`, `hwv2_layout.h` | the table (the panel's controls, in its order) and the wiring: the scanner's bits, the mux channels, the pots reversed on rev A (`HWV2_KNOBS_REVERSED`), the joystick's axes inverted (`HWV2_JOY_INVERT_X / _Y`), the encoders' signs and 4 counts a click, the VU meter's levels, the scan periods |
| `input_esp32.cpp` | the scan task on core 0: the CH32V006 on I2C (0x42, an 8-byte frame with a check byte: the 20 keys, the encoders and their pushes; `hwv2_layout.h` describes it; the CH32V006's own firmware is no longer in this repo), the 8 pots, the joystick and the 4 buttons on the mux, the switch and the battery every 100 ms. A button that reads pushed at the start is ignored until it is seen up (rev A's bodge 1) |
| `display_panel.cpp` | the GMT177-10 (128 x 160, ST7735S family) on SPI with DMA at 15 MHz: the app's 1-bit frame turned into RGB565, or its colour frame (`display_send_rgb565`, from PSRAM, through the 2 KB DMA buffer) as it is; changed tiles only. No reset line on rev A (a software reset at the start) |
| `hwv2_leds.cpp` | the chain's order (35 LEDs: the pots, the VU meter, the indicator, the function row, the 4 x 4 as a serpentine) and the VU meter (LEDs 5..10, the board's own: the engine's peak, so it plays without the codec). LED 11 (the indicator) stays off |
| `board_esp32.cpp` | the power switch, read with the battery on mux channel 7 (switched off, or no battery: 0 V), the joystick for the shared soft power (`boards/esp32/power_esp32.cpp`), the LM4871's shutdown pin, what the card reader reads |
| `sd_card.cpp` | the TF card on the 4-bit SDMMC host at 20 MHz (`HWV2_SD_FREQ_KHZ`) |

**Rev A, as of 2026-11-08:** the mux's common pin needs the wire to GP9 (bodge 4: GP38 has no ADC). The ES8388 codec and the MPU-6050 are not fitted, so
there is no sound yet (the audio runs on I2S all the same). Without a battery the switch reads off on either side, hence `HWV2_USB_STORAGE` = 0 (below).
**Known fault:** on the user's board the joystick's push also acts as B3 (Shift): most likely a bridge or a floating line between U2's pins 4 and 5
(B3's and the joystick push's channels, 4 and 5). It is the board's, not the code's; `DEV_SERIAL_CMD` builds log every button's raw reading at each change.

**The second app on it, TechnoGen** (`apps/technogen`, branch `technogen`, apps/technogen/docs/DESIGN.md ADR-048): `.\build.ps1 -App technogen` and `.\run.ps1 -App technogen`
(its default board is hwv2), its tests `apps\technogen\tools\build_tests.ps1`; its firmware has the one environment `hwv2`. It draws in colour on the
library's canvas, and plays its seven tracks through the pads' sequencer routed to its own track modules (`Engine::pseq_route`).

### Tasks and priorities

The firmware already runs on FreeRTOS (the Arduino core is built on ESP-IDF, dual core): nothing else has to be added, the work is deciding who runs where and how urgent each task is. Higher number = more urgent.

| Task | Core | Priority | Does | Why there |
| --- | --- | --- | --- | --- |
| `audio` | 1 | 23 (`configMAX_PRIORITIES - 2`) | engine render + I2S write | a missed block is a click; core 1 is otherwise idle |
| `input` | 0 | 8 | keyboard / mux / joystick scan | short and periodic, must not lose events |
| `sd_io` | 0 | 5; 1 for work without a deadline | card mount / scan, the sample loader | has deadlines (a dry stream is audible) but only milliseconds of CPU; sleeps 2 ticks after 8 reads in a row and during a scan (the card driver busy-waits, so without that the core-0 idle task starves and the task watchdog fires). A sample's header, a folder's list and the load of a sample (hundreds of ms each) run at the UI's priority (`Calm`, `HWV1_SD_CALM_PRIO`): at 5 they held the UI for that long, at 0 they starved |
| Arduino `loop()` | 0 | 1 | app, LEDs; draws frames into RAM (the sequencer's timing is the audio task's: ADR-042) | notes and knobs must not wait for the display bus |
| `display` | 0 | 1 | sends the changed tiles of the newest frame over I2C | the bus waits block, so `loop()` runs meanwhile |
| `boot` | 0 | 10 | the boot animation (`64/boot`): a frame every 50 ms (`BOOT_FRAME_MS`) from `display_init` on (the first thing `setup()` does), back and forth until the application reports its start (`display_boot_end`: the card read, the pads' files in), then on to the nearer end of the animation; under it a loading bar (`display_esp32_boot_stage` from `setup()` for its first 30 %, `display_boot_progress` from the application for the rest); it hands the panel to `display` and deletes itself | above everything else on core 0, so the frames keep their rhythm while the audio, the card and the application start (measured: a frame sent in 12 ms at most; the UI at 5.1 s on a card with 1800 samples in one folder, of which 2.3 s for three pads' files). While it plays, the card's work without a deadline keeps priority 5 |

Rules: nothing on core 1 except `audio`; anything that waits on hardware (card, I2C) must block (DMA / semaphore), never spin; the loader holds the engine mutex while it reads a ring block, so a UI build can wait a few ms behind a card read (never the audio task); a sample's header and heads are read outside it (`SdStorage::service`, `IoRead::pinned`), and the UI never waits for a card answer it can take later (`audio_sample_request`).
If something new needs the CPU on core 0, give it a priority by its deadline, not by its importance.

### Performance rules (details and numbers in DESIGN.md ADR-035)

- Budget 160000 cycles per 32-frame block. Check `blocks over budget` with 1, 3 and 6 held notes of the real patch (`rack_init_startup` = 4 oscillator engines into a filter, delay and reverb on).
- Module `process()` methods are `SC_HOT` (IRAM); lookup tables are `SC_TABLE` (internal RAM); module state and the plan buffers come from the fast heap (73.7 KB of internal RAM since 2026-10-28; it falls back whole to PSRAM when the largest internal block at the audio start is under 80 KB: watch the `[AUDIO] fast heap` boot line after adding static data), long delay lines and large buffers from the
  bulk heap (PSRAM). When the fast heap is full, allocations spill to PSRAM and the affected modules get 2-4x slower: watch `[HEAP] ... spilled`.
- Do not change the graph's shape from a parameter (nodes, cables, a cable at exactly unity): it rebuilds the plan and stalls the audio.
- Prefer int32 + `mulh` over 64-bit math; use float32 only where the benchmark shows a chain of multiply-adds on state (the filters); no float divide or bit tricks in per-sample code.
- The recipe for a new optimization round is the project skill `.claude/skills/esp32-optimize/SKILL.md`.
