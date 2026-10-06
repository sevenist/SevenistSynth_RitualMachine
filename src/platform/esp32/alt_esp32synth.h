// Dev: ESP32Synth (a third-party oscillator-bank synth library) as a second engine next to ours, to compare their CPU cost and try its ideas.
// Built only with -DALT_ESP32SYNTH; switched at run time with the serial command "alt on|off" (DEV_SERIAL_CMD). See alt_esp32synth.cpp.
#pragma once
#include <stdint.h>

bool alt_init(int sample_rate);          // once, from audio_init; false when the library failed to start
bool alt_active(void);                   // true: the audio task renders ESP32Synth and the notes go to it
void alt_set_active(bool on);            // releases every ESP32Synth voice; the caller releases and rebuilds our engine
void alt_note_on(int midi_note);
void alt_note_off(int midi_note);
void alt_render(int16_t *stereo, int frames);   // [AUDIO] interleaved stereo (the library is mono: both channels get the same signal)
void alt_report(void);                   // [AUDIO] once a second with HWV1_DEBUG_AUDIO: cycles per 32 frames, voices sounding
void alt_command(const char *args);      // serial "alt <args>" except on / off: wave, env, vol, poly, vib, crush, status
