#ifdef PLATFORM_SIM
#define SDL_MAIN_HANDLED          // main() stays a plain main (SDL.h comes in through panel_sim.h)
#include "app/app.h"
#include "core/audio.h"
#include "core/card_layout.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "boards/desktop/panel_sim.h"

int main(void) {
    app_t app;
    card_layout_apply();              // the card's folders, before anything looks at the card
    audio_init();
    app_init(&app, display_init());
    app_boot_animation(&app);
    while (app_step(&app, input_poll())) panel_set_status(app.status, app.in.octave, app.in.shift);
    audio_shutdown();
    return 0;
}
#endif // PLATFORM_SIM
