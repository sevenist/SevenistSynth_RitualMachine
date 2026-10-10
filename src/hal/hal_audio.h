#pragma once
// HAL: audio output + synth engine. Implemented once per platform.

#include <stdbool.h>
#include <stdint.h>
#include "core/pads.h"
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
// The machine clock (ADR-041 decision 24) runs at audio_set_clock's bpm while `running`: a start is bar 1, beat 1. The metronome click,
// and where the clock is (true while it runs; bar from 1, beat 1..4, frac 0..255 of the beat).
void audio_set_metronome(bool on);
bool audio_transport(int *bar, int *beat, int *frac);

// Polyphonic notes: every audio_note_on() must be followed by an audio_note_off().
void audio_note_on(int midi_note);
void audio_note_off(int midi_note);

// The Pads section (core/pads.h, ADR-041): audio_pads_build loads the pads player in place of the synth (audio_build brings the synth
// back); while it plays, audio_set_params only follows the master volume. audio_pad_set: one pad's settings changed. Push / release are the
// pad keys (gate pads stop on the release, the engine decides one-shot / gate / toggle); stop = every pad fades out.
void     audio_pads_build(const pads_t *pads, const rack_t *rack);
void     audio_pad_set(int pad, const pad_t *p);
void     audio_pad_push(int pad);
void     audio_pad_release(int pad);
void     audio_pads_stop(void);
uint64_t audio_pads_playing(void);       // bit i = pad i sounds now
int32_t  audio_pad_position(int pad);    // where a pad's sound is in its file (frames), -1 when it does not sound
// The attacks of a library file (frame, strength = log10 of the energy rise x 1000) for the transient chop: the first call starts a scan
// of the file; -1 while it runs (call again), else how many were copied (0: none / not there).
int audio_sample_onsets(int index, uint32_t *frames, uint16_t *strength, int max);
// The zero crossing nearest to `frame` (dir 0), or the next after (+1) / before (-1) it; -1 where the sample is not in memory.
int64_t audio_sample_zero(int index, uint32_t frame, int dir);

// Sample processing to the card (the pads' EDIT page, engine_synth.h render_*): `src` through `op` (0 Normalize, 1 Gain, 2 Fade in,
// 3 Fade out, 4 Trim silence, 5 Reverse, 6 Remove DC, 7 Pad to (frames), 8 Stretch / 9 Resample (the new length in ppm), 10 Pitch
// (cents), 11 Crop (frames amount..amount2), 12 Decimate (Hz), 13 Bits, 14 Drive (0.1 dB), 15 Low-pass / 16 High-pass (Hz):
// engine/offline/process.h ProcOp, core/pads.h PROC_*) with `amount` into a new library file `dst` (names without extension). poll: 0 idle,
// 1 running, 2 done, 3 failed, and 0..100; after done the app rescans the library (audio_samples_rescan) and calls audio_render_done.
enum { RENDER_STATE_IDLE, RENDER_STATE_RUNNING, RENDER_STATE_DONE, RENDER_STATE_FAILED };
bool audio_render_start(const char *src, const char *dst, int op, int amount, int amount2);
int  audio_render_poll(int *percent);
void audio_render_done(void);
// A library entry whose file was deleted (a trimmed version): its name goes, so no list shows it; the index stays (the rack's samplers keep theirs).
void audio_sample_hide(int index);

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
// Renders an oscillator model for the UI's preview (the Mutable Instruments models, rack Wav >= OC_FIRST_MI; model = Wav - OC_FIRST_MI):
// `n` samples of the model at a pitch whose period is `period` samples (after its start-up; a struck model: its first 4n samples from the strike,
// the peak of every 4); timbre,
// morph, harm 0..1. Returns 1 for a periodic model, 2 for a struck one (its decay was rendered), 0 when the platform has no engine (the UI then
// shows the model's name).
int  audio_osc_preview(int model, float timbre, float morph, float harm, int period, int16_t *out, int n);
bool audio_sample_prepare(int index);    // converts a pending .wav / .mp3 now (called when it is assigned); true when the file is ready to play
int  audio_samples_rescan(void);     // looks for new files (.smp, and .wav / .mp3 which are imported); returns the count

// The TF card. SD_SLOW = a card is in but its reads are too slow to stream samples from (the synth then runs as if there were no card, and the
// application shows an error screen). The generation changes on every insertion, removal and finished scan, so the application knows when to
// re-resolve the sample files of the rack.
typedef enum { SD_NONE = 0, SD_OK, SD_SLOW } sd_state_t;
sd_state_t audio_sd_state(void);
uint32_t   audio_sd_read_us(void);        // SD_SLOW: the measured time of one sector read, in microseconds
uint32_t   audio_sd_generation(void);

// Monotonic millisecond clock (the sequencer's time base).
uint32_t audio_millis(void);

// Call regularly (once per frame): platform housekeeping. Both current platforms render on their own audio thread / task, so it is empty.
void audio_update(void);

#ifdef __cplusplus
}
#endif
