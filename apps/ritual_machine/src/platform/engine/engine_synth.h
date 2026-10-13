#pragma once
// The engine as seen by the HAL (C API). One synth per process; the platform supplies the memory and the audio
// output and calls engine_synth_render() from its audio thread / DMA task. Everything else is called from the
// UI thread. See ENGINE_DESIGN.md (ADR-024) for the threading rules.
#include <stddef.h>
#include <stdint.h>
#include "core/pad_seq.h"
#include "core/pads.h"
#include "core/rack.h"
#include "core/seq.h"
#include "core/synth_params.h"
#include "core/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

// fast: internal RAM (modules, plans); bulk: large and slower RAM (delay lines, pre-delay; may be the same block).
// Roughly 400 KB fast + 400 KB bulk are needed for the full rack with all effects at 48 kHz, 8 voices.
int  engine_synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes);   // returns 0 on success
void engine_synth_shutdown(void);

const char *engine_synth_build_reason(void);
// [CONTROL] call often (the platforms: audio_update): once the audio thread runs the graph of the last build, the old graph's modules are
// freed (a build can only free what the running plan no longer uses: before, they stayed until the next build, so a Synth <-> Pads switch
// kept both graphs in memory).
void engine_synth_housekeeping(void);                                      // why the last set_params had to rebuild ("first" = an explicit build)
void engine_synth_set_osc_engine(int index, int engine);                          // dev: switch the index-th oscillator engine of the loaded graph (the next rack edit may undo it)
void engine_synth_heap_stats(size_t *fast_used, size_t *fast_cap, size_t *fast_high, size_t *bulk_used, size_t *spilled);   // dev: how full the two heaps are
unsigned engine_synth_build_count(void);                                           // how many full graph (re)loads happened (a rebuild is slow: tests and diagnostics watch it)
void engine_synth_build(const rack_t *rack, const synth_params_t *params);        // structure changed: (re)load the voice graph
void engine_synth_set_params(const rack_t *rack, const synth_params_t *params);   // a value changed: update live, rebuild only if needed
// The synth's step sequencer (core/seq.h; ADR-042): seq_set hands the pattern and its settings to the engine's sequencer, which plays
// them on the machine clock while seq->running (call it often: only the steps and settings that changed are sent, and before set_clock
// when both start together). The motion sequencers follow it. seq_pos: the step that plays, -1 while none does.
void engine_synth_seq_set(const seq_t *seq);
int  engine_synth_seq_pos(void);
// Several patterns: seq_queue hands over the pattern the sequencer goes on to when the playing one has played all its steps (NULL:
// none after all); 0 when the command queue was full (call again). seq_taken answers `id` once, when the audio thread has gone on to
// it (-1 otherwise): the application then makes that pattern the one it edits and hands to seq_set, which sends it whole again.
int  engine_synth_seq_queue(const seq_pattern_t *p, int id);
int  engine_synth_seq_taken(void);
int  engine_synth_seq_where(int *step, int *frac, uint32_t *count);
// The Pads section's sequencer (core/pad_seq.h), the same five: pseq_set hands over the pattern that plays and whether it runs (it is
// sent whole when it changed), its hits push the pads of the loaded pads player.
void engine_synth_pseq_set(const pseq_pattern_t *p, int run);
int  engine_synth_pseq_pos(void);
int  engine_synth_pseq_where(int *step, int *frac, uint32_t *count);
int  engine_synth_pseq_queue(const pseq_pattern_t *p, int id);
int  engine_synth_pseq_taken(void);   // 1 while a step plays: it, how far into it (0..255), the steps before it
// The machine clock (ADR-041 decision 24): its tempo, and whether it runs (a start is bar 1 beat 1). The metronome click on / off, and
// where the clock is: returns 1 while it runs; bar from 1, beat 1..4, frac = 0..255 of the beat.
void engine_synth_set_clock(int bpm, int running);
void engine_synth_set_metronome(int on);
int  engine_synth_transport(int *bar, int *beat, int *frac);
void engine_synth_note_on(int midi_note);
void engine_synth_note_off(int midi_note);

// The Pads section (core/pads.h, ADR-041): pads_build replaces the synth's graph with the pads player (the Pads module into the master
// output, at the rack's master volume) and sends every pad; the next engine_synth_build brings the synth back. While the pads play,
// engine_synth_set_params only follows the master volume. pad_set: one pad changed (its sample is loaded when needed).
void engine_synth_pads_build(const pads_t *pads, const rack_t *rack);
void engine_synth_pad_set(int pad, const pad_t *p);
void engine_synth_pads_fx(const pads_t *pads, const rack_t *rack);   // the pads' FX chains changed (pads->fx): values live, a new shape reloads the graph
void engine_synth_pad_push(int pad);
void engine_synth_pad_release(int pad);
void engine_synth_pads_stop(void);
uint64_t engine_synth_pads_playing(void);           // bit i = pad i sounds
int32_t  engine_synth_pad_position(int pad);         // where a pad's sound is in its file (frames), -1 when it does not sound
// The attacks of a library file (the transient chop): asks the loader to scan it (once) and returns how many onsets it copied (frame,
// strength = log10 of the energy rise x 1000), -1 while the scan is not done (call again), 0 for none / a file that is not there.
int engine_synth_sample_onsets(int index, uint32_t *frames, uint16_t *strength, int max);
// The zero crossing nearest to `frame` of a library file (dir 0), or the next one after (+1) / before (-1) it; only where the sample is in
// memory (a resident pad sample, the start of a streamed one): -1 elsewhere.
int64_t engine_synth_sample_zero(int index, uint32_t frame, int dir);

// [AUDIO THREAD] renders `frames` stereo frames of interleaved signed 16-bit samples (any frame count).
void engine_synth_render(int16_t *stereo, int frames);

// Sample library (see core/audio.h). The platform attaches a storage device and the catalog (sample_catalog.h).
int  engine_synth_sample_count(void);
bool engine_synth_sample_info(int index, audio_sample_info_t *out);
int  engine_synth_osc_preview(int model, float timbre, float morph, float harm, int period, int16_t *out, int n);   // core/audio.h audio_osc_preview
// [I/O THREAD or test loop] runs the sample loader; call it every millisecond or so (the desktop starts a thread for it).
// Returns true while reads are still in flight (call again at once); false when the streams are filled (sleep a tick).
bool engine_synth_io_pump(void);
// The card was replaced: forgets the catalog and which samples are loaded (loaded slots stay in memory until reboot: a playing voice may still read them).
void engine_synth_catalog_reset(void);
// Streaming health since boot: playheads that found a block missing (audible), and stream blocks read from the card.
void engine_synth_sampler_stats(uint32_t *underruns, uint32_t *block_reads);

// Sample processing to the card (the pads' sample tools, ADR-041 decisions 36-38; platform/engine/render_job.cpp): library file `src`
// through operation `op` (engine/offline/process.h ProcOp: 0 Normalize, 1 Gain, 2 Fade in, 3 Fade out, 4 Trim silence, 5 Reverse, 6
// Remove DC, 7 Pad to (amount = frames), 8 Stretch, 9 Resample (ppm of the length), 10 Pitch (cents), 11 Crop (frames amount..amount2),
// 12 Decimate (Hz), 13 Bits, 14 Drive (0.1 dB), 15 Low-pass, 16 High-pass (Hz), 17 Tonal stretch (ppm), 18 Tonal pitch (cents),
// 19 Paulstretch (ppm)) with `amount` into a new file `dst` (names without extension, in the samples folder). One job at a time; the thread that
// owns the card runs it (render_step, a few chunks per call: true while there is more). poll: the state and 0..100; DONE / FAILED stay
// until render_done (the UI then rescans the library). set_card_root: where the card is on this platform ("/sdcard", the desktop folder).
typedef enum { RENDER_IDLE, RENDER_RUNNING, RENDER_DONE, RENDER_FAILED } render_state_t;
void engine_synth_set_card_root(const char *root);
bool engine_synth_render_start(const char *src, const char *dst, int op, int amount, int amount2);
render_state_t engine_synth_render_poll(int *percent);
void engine_synth_render_done(void);
bool engine_synth_render_step(void);
const char *engine_synth_card_root(void);

// Capture / resampling (ADR-041; platform/engine/capture_job.cpp, engine/sampler/capture.h): what the master output receives (before
// the volume, mono) is recorded into a new library file `name` on the card. start arms it: it begins at the next bar line when `at_bar`
// and the clock runs, else at the first block louder than `threshold` (q15, 0 = at once), and ends at stop or after `limit_frames`
// (0 = none). The thread that owns the card writes it (capture_step: true while it has more to do at once). poll: the state, the frames
// recorded and the frames lost because the card was too slow; DONE / FAILED / EMPTY (stopped before any sound: no file) stay until
// capture_done. Not while a processing job runs (they share the card and the spare memory).
typedef enum { CAPTURE_IDLE, CAPTURE_ARMED, CAPTURE_RECORDING, CAPTURE_FINISHING, CAPTURE_DONE, CAPTURE_FAILED, CAPTURE_EMPTY } capture_state_t;
bool engine_synth_capture_start(const char *name, int threshold, int at_bar, uint32_t limit_frames);
void engine_synth_capture_stop(void);
capture_state_t engine_synth_capture_poll(uint32_t *frames, uint32_t *dropped);
void engine_synth_capture_done(void);
bool engine_synth_capture_step(void);

#ifdef ENGINE_PROFILE
// [AUDIO THREAD] per-module CPU cost since the previous call: `cb` gets the module name, its cycles per rendered block (all voices summed) and its
// calls per block; `blocks` is the number of blocks the figures cover. Only exists when built with -DENGINE_PROFILE.
void engine_synth_profile(void (*cb)(const char *name, uint32_t cycles_per_block, uint32_t calls_per_block, void *user), void *user, uint32_t *blocks);
#endif

int      engine_synth_sample_rate(void);
uint32_t engine_synth_millis(void);        // time of the audio clock (advances by one block per block rendered)

#ifdef __cplusplus
}
#endif
