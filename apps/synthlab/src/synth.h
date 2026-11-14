#pragma once
// SynthLab's sound: the engine with one graph, loaded once (NoteIn -> LabVoice -> VoiceOut on every voice, the bus into the master),
// and the calls that pick a synth and move its values. Portable: the library's entry points (framework/app) give it memory and call
// synth_render() from their audio thread; src/app.cpp sends it notes.
#include <stddef.h>
#include <stdint.h>
#include "app/app_main.h"

#define APP_NAME "synthlab"

// synth_init, synth_shutdown, synth_render (the audio thread's only call), synth_update and synth_sample_rate: app/app_main.h.
// [CONTROL] everything below is called from one thread, never from the audio thread.
void synth_note_on(int midi_note, int velocity127 = 100);
void synth_note_off(int midi_note);
void synth_all_notes_off();
void synth_select(int index);                   // lab::kSynths[index] plays: its voice mode, then its values (synth_value)
int  synth_selected();
void synth_set_value(int param, float value);   // a value of the selected synth (clamped to its range), applied live
float synth_value(int param);                   // as the screen shows it; each synth keeps its own values while another plays
void synth_set_volume(float volume);            // the master, 0..1
// A loop of one note, one hit per beat, played by the engine's step sequencer on its clock (on time to the sample, whatever this thread
// does). A synth with grooves (lab::Options::grooves) loops its groove instead, at its tempo value (bpm is then not used); its groove,
// tempo and swing values act live. synth_select stops it.
void synth_loop_start(int midi_note, int bpm);
void synth_loop_note(int midi_note);            // the note it plays from its next hit on
void synth_loop_stop();
bool synth_looping();
int  synth_loop_bpm();                          // the loop's tempo
int  synth_active_voices();
