#ifdef PLATFORM_SIM
// Desktop, the audio output (hal/hal_audio_out.h): the app's render function is called on SDL's audio thread; everything else in an
// app runs on the main thread. Also the desktop's large memory (hal/hal_mem.h): the heap.
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "hal/hal_audio_out.h"
#include "hal/hal_mem.h"

static SDL_AudioDeviceID dev;
static audio_render_fn g_render;

#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
#if defined(__SSE__) || defined(__x86_64__)
    // Flush denormal floats to zero on the audio thread: a decaying float state (filters, the Plaits drums' pulses) reaches denormals in a few
    // ms, and each operation on one costs ~100 cycles on x86 (the Plaits analog bass drum ran 8x slower). Inaudible (below -700 dB).
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    g_render((int16_t *)stream, len / 4);                    // 2 channels x 16 bit
}

bool audio_out_start(audio_render_fn render, int sample_rate) {
    if (!render || dev) return false;
    g_render = render;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return false; }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 256;                                      // about 5 ms per callback
    want.callback = audio_callback;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);     // SDL converts if the device cannot do exactly this
    if (!dev) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return false; }
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

void audio_out_stop(void) {
    if (dev) { SDL_CloseAudioDevice(dev); dev = 0; }         // the callback has ended when this returns
}

void audio_out_set_report(audio_report_fn report) { (void)report; }   // nothing is measured here

void *mem_bulk_alloc(size_t bytes) { return calloc(1, bytes); }
#endif // PLATFORM_SIM
