#pragma once
// The engine as seen by the HAL (C API). One synth per process; the platform supplies the memory and the audio
// output and calls engine_synth_render() from its audio thread / DMA task. Everything else is called from the
// UI thread. See ENGINE_DESIGN.md (ADR-024) for the threading rules.
#include <stddef.h>
#include <stdint.h>
#include "core/pads.h"
#include "core/rack.h"
#include "core/synth_params.h"
#include "hal/hal_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

// fast: internal RAM (modules, plans); bulk: large and slower RAM (delay lines, pre-delay; may be the same block).
// Roughly 400 KB fast + 400 KB bulk are needed for the full rack with all effects at 48 kHz, 8 voices.
int  engine_synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes);   // returns 0 on success
void engine_synth_shutdown(void);

const char *engine_synth_build_reason(void);                                      // why the last set_params had to rebuild ("first" = an explicit build)
void engine_synth_set_osc_engine(int index, int engine);                          // dev: switch the index-th oscillator engine of the loaded graph (the next rack edit may undo it)
void engine_synth_heap_stats(size_t *fast_used, size_t *fast_cap, size_t *fast_high, size_t *bulk_used, size_t *spilled);   // dev: how full the two heaps are
unsigned engine_synth_build_count(void);                                           // how many full graph (re)loads happened (a rebuild is slow: tests and diagnostics watch it)
void engine_synth_build(const rack_t *rack, const synth_params_t *params);        // structure changed: (re)load the voice graph
void engine_synth_set_params(const rack_t *rack, const synth_params_t *params);   // a value changed: update live, rebuild only if needed
// The note sequencer's timing for the motion sequencers (call when bpm / steps / swing / running change), and the moment the
// note sequencer starts a run (the motion lanes restart at their first step).
void engine_synth_set_clock(int bpm, int steps, int swing, int running);
void engine_synth_motion_restart(void);
// The machine clock (ADR-041 decision 24) follows set_clock (bpm, running: a start is bar 1 beat 1). The metronome click on / off, and
// where the clock is: returns 1 while it runs; bar from 1, beat 1..4, frac = 0..255 of the beat.
void engine_synth_set_metronome(int on);
int  engine_synth_transport(int *bar, int *beat, int *frac);
void engine_synth_note_on(int midi_note);
void engine_synth_note_off(int midi_note);

// The Pads section (core/pads.h, ADR-041): pads_build replaces the synth's graph with the pads player (the Pads module into the master
// output, at the rack's master volume) and sends every pad; the next engine_synth_build brings the synth back. While the pads play,
// engine_synth_set_params only follows the master volume. pad_set: one pad changed (its sample is loaded when needed).
void engine_synth_pads_build(const pads_t *pads, const rack_t *rack);
void engine_synth_pad_set(int pad, const pad_t *p);
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

// Sample library (see hal_audio.h). The platform attaches a storage device and the catalog (sample_catalog.h).
int  engine_synth_sample_count(void);
bool engine_synth_sample_info(int index, audio_sample_info_t *out);
int  engine_synth_osc_preview(int model, float timbre, float morph, float harm, int period, int16_t *out, int n);   // hal_audio.h audio_osc_preview
// [I/O THREAD or test loop] runs the sample loader; call it every millisecond or so (the desktop starts a thread for it).
// Returns true while reads are still in flight (call again at once); false when the streams are filled (sleep a tick).
bool engine_synth_io_pump(void);
// The card was replaced: forgets the catalog and which samples are loaded (loaded slots stay in memory until reboot: a playing voice may still read them).
void engine_synth_catalog_reset(void);
// Streaming health since boot: playheads that found a block missing (audible), and stream blocks read from the card.
void engine_synth_sampler_stats(uint32_t *underruns, uint32_t *block_reads);

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
