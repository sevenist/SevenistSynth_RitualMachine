#pragma once
// The engine as seen by the HAL (C API). One synth per process; the platform supplies the memory and the audio
// output and calls engine_synth_render() from its audio thread / DMA task. Everything else is called from the
// UI thread. See ENGINE_DESIGN.md (ADR-024) for the threading rules.
#include <stddef.h>
#include <stdint.h>
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

void engine_synth_build(const rack_t *rack, const synth_params_t *params);        // structure changed: (re)load the voice graph
void engine_synth_set_params(const rack_t *rack, const synth_params_t *params);   // a value changed: update live, rebuild only if needed
// The note sequencer's timing for the motion sequencers (call when bpm / steps / swing / running change), and the moment the
// note sequencer starts a run (the motion lanes restart at their first step).
void engine_synth_set_clock(int bpm, int steps, int swing, int running);
void engine_synth_motion_restart(void);
void engine_synth_note_on(int midi_note);
void engine_synth_note_off(int midi_note);

// [AUDIO THREAD] renders `frames` stereo frames of interleaved signed 16-bit samples (any frame count).
void engine_synth_render(int16_t *stereo, int frames);

// Sample library (see hal_audio.h). The platform attaches a storage device and the catalog (sample_catalog.h).
int  engine_synth_sample_count(void);
bool engine_synth_sample_info(int index, audio_sample_info_t *out);
// [I/O THREAD or test loop] runs the sample loader; call it every millisecond or so (the desktop starts a thread for it).
void engine_synth_io_pump(void);

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
