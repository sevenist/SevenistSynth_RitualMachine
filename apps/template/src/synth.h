#pragma once
// The project's own synth: the one place that knows which engine modules it uses and how they are cabled.
// Everything here is portable (no SDL, no Arduino): the library's entry points (framework/app) give it memory and call
// synth_render() from their audio thread; src/app.cpp sends it notes.
#include <stddef.h>
#include <stdint.h>
#include "app/app_main.h"

#define APP_NAME "template"             // the project's name (tools/new_project.ps1 sets it)

// synth_init, synth_shutdown, synth_render (the audio thread's only call), synth_update and synth_sample_rate: app/app_main.h.
// [CONTROL] everything below is called from one thread, never from the audio thread.
void synth_note_on(int midi_note);
void synth_note_off(int midi_note);
void synth_set_cutoff(int semitones);   // the filter's cutoff as a MIDI note number (0..127), applied live
int  synth_cutoff();
