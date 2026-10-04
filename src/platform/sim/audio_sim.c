#ifdef PLATFORM_SIM
// Desktop audio: the engine renders on SDL's audio thread (blocks of ENGINE_BLOCK frames); everything else in the
// application runs on the main thread and talks to the engine through its command queue (see engine_synth.h).
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "hal/hal_audio.h"
#include "platform/engine/engine_synth.h"

int sim_samples_init(void);        // sample_sim.cpp
void sim_samples_shutdown(void);
int sim_samples_rescan(void);
int sim_samples_prepare(int index);

#define FAST_BYTES (6u << 20)
#define BULK_BYTES (6u << 20)

static SDL_AudioDeviceID dev;
static void *fast_mem, *bulk_mem;

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    engine_synth_render((int16_t *)stream, len / 4);       // 2 channels x 16 bit
}

void audio_init(void) {
    fast_mem = malloc(FAST_BYTES);
    bulk_mem = malloc(BULK_BYTES);
    if (!fast_mem || !bulk_mem || engine_synth_init(fast_mem, FAST_BYTES, bulk_mem, BULK_BYTES) != 0) {
        fprintf(stderr, "audio: out of memory for the synth engine\n");
        return;
    }
    sim_samples_init();                                      // the samples/ folder: library, import of .wav / .mp3, loader thread
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = engine_synth_sample_rate();
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 256;                                      // about 5 ms per callback
    want.callback = audio_callback;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);     // SDL converts if the device cannot do exactly this
    if (!dev) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return; }
    SDL_PauseAudioDevice(dev, 0);
}

void audio_shutdown(void) {
    if (dev) { SDL_CloseAudioDevice(dev); dev = 0; }          // stops the callback before the engine goes away
    sim_samples_shutdown();
    engine_synth_shutdown();
    free(fast_mem); free(bulk_mem);
    fast_mem = bulk_mem = NULL;
}

void audio_set_params(const rack_t *r, const synth_params_t *p) { engine_synth_set_params(r, p); }
void audio_build(const rack_t *r, const synth_params_t *p)      { engine_synth_build(r, p); }
int audio_sample_count(void)                                     { return engine_synth_sample_count(); }
bool audio_sample_info(int i, audio_sample_info_t *out)         { return engine_synth_sample_info(i, out); }
int audio_samples_rescan(void)                                  { return sim_samples_rescan(); }
bool audio_sample_prepare(int i)                                { return sim_samples_prepare(i) != 0; }
void audio_set_clock(int bpm, int steps, int swing, int running) { engine_synth_set_clock(bpm, steps, swing, running); }
void audio_motion_restart(void)                                 { engine_synth_motion_restart(); }
void audio_note_on(int midi_note)                               { engine_synth_note_on(midi_note); }
void audio_note_off(int midi_note)                              { engine_synth_note_off(midi_note); }
uint32_t audio_millis(void)                                     { return dev ? engine_synth_millis() : SDL_GetTicks(); }   // no audio device: the sequencer still runs
void audio_update(void)                                         {}
#endif // PLATFORM_SIM
