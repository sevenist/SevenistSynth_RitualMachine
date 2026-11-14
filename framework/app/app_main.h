#pragma once
// What an app gives the library's entry points (app/main_esp32.cpp: setup and loop on a board; app/main_sim.cpp: main on the desktop).
// An app made from apps/template defines these and needs no entry point of its own:
//   its sound (src/synth.cpp)       synth_init ... synth_sample_rate: the engine, usually through engine/core/host.h
//   its controls and screen (src/app.cpp)    app_name, app_start, app_event, app_draw
// Both builds pick these files: the firmware's build_src_filter has +<framework/app/>, the desktop's build.ps1 adds Get-AppMainSources
// (framework/tools/sim_build.ps1). The host tests have their own main and leave them out.
//
// The start, on both: memory for the engine, synth_init, the display, app_start, then the audio output. Then the loop: a control's event
// to app_event, synth_update, app_draw. On the board the audio task (core 1) calls synth_render; the loop runs on core 0.
// The engine's memory: on the board APP_FAST_BYTES of internal RAM (default 64 KB) and APP_BULK_BYTES of PSRAM (default 256 KB, when
// the board has it; build flags to change them); on the desktop 1 MB of each.
#include <stddef.h>
#include <stdint.h>
#include "hal/hal_display.h"
#include "hal/hal_input.h"

// The sound. fast: internal RAM (module state, the plan's buffers). bulk: large, slower RAM; may be the same block, or none.
// False when the engine could not start (out of memory, a graph that does not compile).
bool synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes);
void synth_shutdown();                      // after the audio output has stopped
void synth_render(int16_t *stereo, int frames);     // [AUDIO] `frames` stereo frames of 16-bit audio, interleaved L R: the audio thread's only call
void synth_update();                        // [CONTROL] call it often: frees what the engine no longer uses
int  synth_sample_rate();

// The controls and the screen: the library's HAL reports the board's controls (hal/hal_input.h) and gives the display (hal/hal_display.h,
// a u8g2 instance); ui/gui.h draws.
const char *app_name();                     // the log lines' tag on the board ("[SYNTHLAB] ...")
void app_start(u8g2_t *display);            // once, after synth_init()
void app_event(const input_event_t &e);     // a control was used
void app_draw();                            // call it often: redraws and sends the screen only when something changed
