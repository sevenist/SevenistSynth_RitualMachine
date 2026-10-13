#pragma once
// The project's own synth: the one place that knows which engine modules it uses and how they are cabled.
// Everything here is portable (no SDL, no Arduino): the platform files (main_sim.cpp, main_esp32.cpp) give it memory,
// call synth_render() from their audio thread and send it notes.
#include <stddef.h>
#include <stdint.h>

#define APP_NAME "template"             // the project's name (tools/new_project.ps1 sets it)

// fast: internal RAM (module state, the plan's buffers). bulk: large, slower RAM (long delay lines, samples); may be the same block.
// False when the engine could not start (out of memory, a graph that does not compile).
bool synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes);
void synth_shutdown();

// [AUDIO] `frames` stereo frames of 16-bit audio, interleaved L R. The only call the audio thread makes.
void synth_render(int16_t *stereo, int frames);

// [CONTROL] everything below is called from one thread, never from the audio thread.
void synth_update();                    // call it often: frees what the engine no longer uses
void synth_note_on(int midi_note);
void synth_note_off(int midi_note);
void synth_set_cutoff(int semitones);   // the filter's cutoff as a MIDI note number (0..127), applied live
int  synth_cutoff();
int  synth_sample_rate();
