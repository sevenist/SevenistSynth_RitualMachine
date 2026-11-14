#pragma once
// TechnoGen's sound: the engine, one graph loaded once (seven track modules chained into the master), and the pattern played by the engine's
// pads sequencer routed to the tracks. Everything a roll of the dice changes is a live write (set_param, the pattern's hits): the graph
// never reloads while it plays (ADR-048).
// Portable (no SDL, no Arduino): the library's entry points (framework/app) give it memory and call synth_render() from their audio
// thread; everything else is called from one other thread.
#include <stddef.h>
#include <stdint.h>
#include "app/app_main.h"
#include "tg_model.h"

#define APP_NAME "technogen"             // the project's name (tools/new_project.ps1 sets it)

// synth_init, synth_shutdown, synth_render (the audio thread's only call), synth_update and synth_sample_rate: app/app_main.h.
// [CONTROL] everything below is called from one thread, never from the audio thread.
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
