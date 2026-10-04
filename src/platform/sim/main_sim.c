#ifdef PLATFORM_SIM
#define SDL_MAIN_HANDLED          // main() stays a plain main (SDL.h comes in through panel_sim.h)
#include "core/app.h"
#include "hal/hal_audio.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "platform/sim/panel_sim.h"

int main(void) {
    app_t app;
    audio_init();
    app_init(&app, display_init());
    while (app_step(&app, input_poll())) panel_set_status(app.status, app.in.octave, app.in.shift);
    audio_shutdown();
    return 0;
}
#endif // PLATFORM_SIM
