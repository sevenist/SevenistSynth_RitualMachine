#ifdef PLATFORM_SIM
// Desktop build: an SDL window for the keyboard, SDL audio for the sound. The engine renders on SDL's audio thread;
// the main thread sends it notes.
//
//   A W S E D F T G Y H U J K    a piano octave from C (the keys' position, whatever the keyboard's language)
//   Z / X                        octave down / up
//   Left / Right                 filter cutoff
//   Esc                          quit
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include "synth.h"

namespace {
constexpr size_t kFastBytes = 1u << 20, kBulkBytes = 1u << 20;

void audio_callback(void *, Uint8 *stream, int len) { synth_render(reinterpret_cast<int16_t *>(stream), len / 4); }   // 2 channels x 16 bits

// The semitone above C of a piano key, -1 for any other key.
int key_semitone(SDL_Scancode sc) {
    static const SDL_Scancode keys[] = {SDL_SCANCODE_A, SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_E, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_T,
                                        SDL_SCANCODE_G, SDL_SCANCODE_Y, SDL_SCANCODE_H, SDL_SCANCODE_U, SDL_SCANCODE_J, SDL_SCANCODE_K};
    for (int i = 0; i < static_cast<int>(sizeof keys / sizeof keys[0]); i++) if (keys[i] == sc) return i;
    return -1;
}
}  // namespace

int main() {
    void *fast = std::malloc(kFastBytes), *bulk = std::malloc(kBulkBytes);
    if (!fast || !bulk || !synth_init(fast, kFastBytes, bulk, kBulkBytes)) { std::fprintf(stderr, "the synth did not start\n"); return 1; }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow(APP_NAME ": A W S E D F T G Y H U J K = notes, Z X = octave, arrows = cutoff", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, 640, 120, 0);
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = synth_sample_rate();
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 256;                                 // about 5 ms per callback
    want.callback = audio_callback;
    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!win || !dev) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_PauseAudioDevice(dev, 0);

    int octave = 4;                                     // C4 = MIDI note 60
    int held[SDL_NUM_SCANCODES] = {};                   // note + 1 a key started, so its release stops the same note after an octave change
    for (bool run = true; run;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) run = false;
            if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP) continue;
            const SDL_Scancode sc = e.key.keysym.scancode;
            const int semi = key_semitone(sc);
            if (e.type == SDL_KEYUP) {
                if (held[sc]) { synth_note_off(held[sc] - 1); held[sc] = 0; }
                continue;
            }
            if (e.key.repeat && semi >= 0) continue;
            if (semi >= 0) { const int note = 12 * (octave + 1) + semi; synth_note_on(note); held[sc] = note + 1; }
            else if (sc == SDL_SCANCODE_ESCAPE) run = false;
            else if (sc == SDL_SCANCODE_Z && octave > 0) octave--;
            else if (sc == SDL_SCANCODE_X && octave < 8) octave++;
            else if (sc == SDL_SCANCODE_LEFT) synth_set_cutoff(synth_cutoff() - 2);
            else if (sc == SDL_SCANCODE_RIGHT) synth_set_cutoff(synth_cutoff() + 2);
        }
        synth_update();
        SDL_Delay(2);
    }

    SDL_CloseAudioDevice(dev);                          // stops the callback before the engine goes away
    synth_shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::free(fast);
    std::free(bulk);
    return 0;
}
#endif  // PLATFORM_SIM
