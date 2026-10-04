#pragma once
// HAL: audio output + synth engine. Implemented once per platform.

#include <stdbool.h>
#include <stdint.h>
#include "core/rack.h"
#include "core/synth_params.h"

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(void);
void audio_shutdown(void);

// (Re)builds the synth structure from the rack (module chain, LFO/ENV routes) and applies params.
// Call at startup and when the user finishes editing the rack.
void audio_build(const rack_t *rack, const synth_params_t *params);

// Applies the full sound definition (oscillator, envelopes, filter) to the synth.
void audio_set_params(const rack_t *rack, const synth_params_t *params);

// The note sequencer's timing, for the motion sequencers (cheap: call whenever it may have changed), and the start of a run.
void audio_set_clock(int bpm, int steps, int swing, int running);
void audio_motion_restart(void);

// Polyphonic notes: every audio_note_on() must be followed by an audio_note_off().
void audio_note_on(int midi_note);
void audio_note_off(int midi_note);

// The sample library (.smp files on the TF card / in the simulator's samples/ folder; .wav and .mp3 are listed too and converted on first use). Index 0.. in a stable order that
// only grows while the program runs (a rescan appends); the rack refers to a file by index + 1 (0 = none).
#define AUDIO_PEAKS 64
typedef struct {
    char     name[24];           // file name without extension
    uint32_t frames, rate;
    uint8_t  root, slices, loop_mode;
    uint32_t loop_start, loop_end;
    uint32_t slice[16];          // slice start frames
    uint8_t  peaks[AUDIO_PEAKS]; // 0..255 amplitude overview of the whole sample
    uint8_t  pending;            // 1 = a .wav / .mp3 that has not been converted yet (length, slices and peaks unknown until prepared)
    uint8_t  kind;               // 0 = .smp, 1 = .wav, 2 = .mp3
} audio_sample_info_t;
#define AUDIO_SAMPLES_MAX 32
int  audio_sample_count(void);
bool audio_sample_info(int index, audio_sample_info_t *out);
bool audio_sample_prepare(int index);    // converts a pending .wav / .mp3 now (called when it is assigned); true when the file is ready to play
int  audio_samples_rescan(void);     // looks for new files (.smp, and .wav / .mp3 which are imported); returns the count

// Monotonic millisecond clock (the sequencer's time base).
uint32_t audio_millis(void);

// Call regularly (once per frame): platform housekeeping. Both current platforms render on their own audio thread / task, so it is empty.
void audio_update(void);

#ifdef __cplusplus
}
#endif
