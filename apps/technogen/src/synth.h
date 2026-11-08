#pragma once
// TechnoGen's sound: the engine, one graph loaded once (seven track modules chained into the master), and the pattern played by the engine's
// pads sequencer routed to the tracks. Everything a roll of the dice changes is a live write (set_param, the pattern's hits): the graph
// never reloads while it plays (ADR-048).
// Portable (no SDL, no Arduino): the platform files (main_sim.cpp, main_esp32.cpp) give it memory, call synth_render() from their audio
// thread and everything else from one other thread.
#include <stddef.h>
#include <stdint.h>
#include "tg_model.h"

#define APP_NAME "technogen"             // the project's name (tools/new_project.ps1 sets it)

// fast: internal RAM (module state, the plan's buffers). bulk: large, slower RAM; may be the same block.
// False when the engine could not start (out of memory, a graph that does not compile).
bool synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes);
void synth_shutdown();

// [AUDIO] `frames` stereo frames of 16-bit audio, interleaved L R. The only call the audio thread makes.
void synth_render(int16_t *stereo, int frames);

// [CONTROL] everything below is called from one thread, never from the audio thread.
void synth_update();                                    // call it often: frees what the engine no longer uses
void synth_set_track(int t, const tg::Track &tr);       // its seven values and its level (muted: silent)
void synth_set_steps(const tg::Pattern &p);             // every track's steps, as the sequencer's hits (the muted tracks' left out)
// The steps of the pattern to go on to at the end of the playing one (its last step), with an id; synth_taken() then changes to
// (a count << 8 | that id). The application sets the new pattern's values when it sees it (synth_set_track): one block late at most.
void synth_queue_steps(const tg::Pattern &p, int id);
uint32_t synth_taken();
void synth_set_volume(int percent);                     // the master volume, 0..100
void synth_set_transport(int bpm, bool running);
int  synth_step();                                      // the step that plays, -1 when stopped
int  synth_hits();                                      // hits in the sequencer's pattern now (at most synth_max_hits)
int  synth_max_hits();
int  synth_sample_rate();
