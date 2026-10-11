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
void     audio_pads_fx(const pads_t *pads, const rack_t *rack);   // the FX chains (pads->fx) changed: a value, a module, the routing
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
// (cents), 11 Crop (frames amount..amount2), 12 Decimate (Hz), 13 Bits, 14 Drive (0.1 dB), 15 Low-pass / 16 High-pass (Hz), 17 Tonal stretch (ppm), 18 Tonal pitch (cents), 19 Paulstretch (ppm):
// engine/offline/process.h ProcOp, core/pads.h PROC_*) with `amount` into a new library file `dst` (names without extension). poll: 0 idle,
// 1 running, 2 done, 3 failed, and 0..100; after done the app takes the new file into the catalog (audio_sample_use) and calls audio_render_done.
enum { RENDER_STATE_IDLE, RENDER_STATE_RUNNING, RENDER_STATE_DONE, RENDER_STATE_FAILED };
bool audio_render_start(const char *src, const char *dst, int op, int amount, int amount2);
int  audio_render_poll(int *percent);
void audio_render_done(void);
// Capture / resampling (engine_synth.h capture_*): what plays, before the volume, mono, into a new library file `name`. It begins at the
// next bar line (`at_bar`, the clock running) or at the first sound over `threshold` (q15; 0 = at once), ends at stop or after
// `limit_frames` (0 = none). poll: 0 idle, 1 armed, 2 recording, 3 finishing, 4 done, 5 failed, 6 empty (no sound came: no file), with
// the frames recorded and those lost to a slow card; after done / failed / empty call audio_capture_done (done: rescan the library first).
enum { CAPTURE_ST_IDLE, CAPTURE_ST_ARMED, CAPTURE_ST_RECORDING, CAPTURE_ST_FINISHING, CAPTURE_ST_DONE, CAPTURE_ST_FAILED, CAPTURE_ST_EMPTY };
bool audio_capture_start(const char *name, int threshold, int at_bar, uint32_t limit_frames);
void audio_capture_stop(void);
int  audio_capture_poll(uint32_t *frames, uint32_t *dropped);
void audio_capture_done(void);
int  audio_rate(void);                    // the engine's sample rate (what a recording is made at)
// Takes an entry out of the catalog (a version whose file was deleted, or a file nothing uses any more when the catalog is full): its
// name goes, its sample is unloaded as soon as nothing plays it, and a later file may take its index. The application first makes sure
// no pad and no rack sampler refers to it.
void audio_sample_hide(int index);

// The sample library: the .smp files below the samples folder of the TF card (the simulator: sdcard/system/samples; .wav and .mp3 are
// listed there too and converted on first use), in folders of any depth. A file's NAME is its path below that folder without the
// extension, at most AUDIO_NAME_MAX - 1 characters. Nothing is scanned at start: the CATALOG holds the files in use (at most
// AUDIO_SAMPLES_MAX), each added when it is asked for by name (audio_sample_use: a pad's file, the browser's highlighted file, a new
// recording). An entry keeps its index while it is in the catalog; the rack refers to a file by index + 1 (0 = none).
#define AUDIO_NAME_MAX PAD_NAME_MAX
#define AUDIO_PEAKS 64
typedef struct {
    char     name[AUDIO_NAME_MAX]; // the file's path below the samples folder, without extension ("Drums/Kicks/808 long")
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
// The catalog by name: find = the index of the entry called `name`, -1 when it is not in the catalog. use = the same, and a file that
// is not there yet is read from the card (its header: a few ms on the board) and added; -1 when there is no such file or it is not a
// sample, -2 when the catalog is full (the application then takes out an entry nothing uses, audio_sample_hide, and asks again).
int  audio_sample_find(const char *name);
int  audio_sample_use(const char *name);
// A folder of the library, for the browser. open reads the folder `dir` ("" = the samples folder itself, else a path below it) and
// returns how many entries it has (-1: no card, no such folder): its sub-folders first, then its samples, each group by name (case
// ignored); versions (name~n) are left out. read copies up to `max` of them from rank `first` (no card access) and returns how many.
// An entry's name is the folder's or the file's own name (no path, no extension).
// The platform keeps the list until another folder is opened or audio_dir_close: opening the same folder again then costs no card
// read (a folder of several hundred files takes most of a second on the board). close gives the list's memory back (the board:
// 34 KB of PSRAM, which the sample jobs want) and must follow anything that adds a listed sample (a recording).
typedef struct { char name[AUDIO_NAME_MAX]; uint8_t dir; } audio_dir_entry_t;
int  audio_dir_open(const char *dir);
int  audio_dir_read(int first, audio_dir_entry_t *out, int max);
void audio_dir_close(void);

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
