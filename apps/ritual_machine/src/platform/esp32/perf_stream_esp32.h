// Dev only (-DDEV_PERF_STREAM, needs ENGINE_PROFILE; ENGINE_PROFILE_PMU adds the core's performance counters): the audio task's cost,
// streamed live over the serial port for the desktop viewer tools/perf_view (packet format: tools/perf_view/README.md).
// The audio task times every render and takes the engine's per-module profile; a small task on core 0 sends one packet 20 times a second.
#pragma once
#if defined(ARDUINO_ARCH_ESP32) && defined(DEV_PERF_STREAM)
void perf_stream_init(void);              // once the audio output runs: starts the sender task (core 0)
void perf_render_begin(void);             // [AUDIO] around the engine's render of one buffer
void perf_render_end(int frames);
void perf_stream_cmd(const char *arg);    // the serial command "perf [on|off|rate N|pmu MODE]" (serial_cmd_esp32.cpp)
#endif
