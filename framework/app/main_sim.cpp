#if defined(PLATFORM_SIM) && !defined(PLATFORM_TEST)
// The desktop program's entry point for an app made from apps/template (app/app_main.h). The library's desktop board gives the display
// window, a panel window whose controls work with the mouse, the PC keyboard as the key matrix, and the audio output (SDL); this file
// gives the synth its memory and runs the loop. Esc or closing a window quits.
#define SDL_MAIN_HANDLED
#include <cstdio>
#include <cstdlib>
#include "app/app_main.h"
#include "hal/hal_audio_out.h"

namespace {
constexpr size_t kFastBytes = 1u << 20, kBulkBytes = 1u << 20;
}

int main() {
    void *fast = std::malloc(kFastBytes), *bulk = std::malloc(kBulkBytes);
    if (!fast || !bulk || !synth_init(fast, kFastBytes, bulk, kBulkBytes)) { std::fprintf(stderr, "the synth did not start\n"); return 1; }
    app_start(display_init());
    if (!audio_out_start(synth_render, synth_sample_rate())) std::fprintf(stderr, "no audio output: the synth runs without sound\n");

    for (;;) {
        const input_event_t e = input_poll();       // (it sleeps a millisecond when nothing happened)
        if (e.quit) break;
        if (e.kind != IN_NONE) app_event(e);
        synth_update();
        app_draw();
    }

    audio_out_stop();                               // the render calls have ended before the engine goes away
    synth_shutdown();
    std::free(fast);
    std::free(bulk);
    return 0;
}
#endif  // PLATFORM_SIM && !PLATFORM_TEST
