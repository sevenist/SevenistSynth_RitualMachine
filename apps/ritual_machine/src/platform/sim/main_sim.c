#ifdef PLATFORM_SIM
#define SDL_MAIN_HANDLED          // main() stays a plain main (SDL.h comes in through panel_sim.h)
#include "app/app.h"
#include "core/audio.h"
#include "core/card_layout.h"
#include "hal/hal_display.h"
#include "core/controls.h"
#include "boards/desktop/panel_sim.h"
#include "core/ui_theme.h"

int main(void) {
    app_t app;
    controls_init();                  // the board's controls as the app's roles, before the app asks which it has
    card_layout_apply();              // the card's folders, before anything looks at the card
    audio_init();
    u8g2_t *display = display_init();
#ifndef UI_COLOUR_MODE                  // core/ui_theme.h; by default the second prototype's screen (160 px high) is a colour one, so is its window
#define UI_COLOUR_MODE (DISPLAY_HEIGHT >= 160 ? UI_RGB : UI_MONO)
#endif
#if UI_COLOUR_MODE == UI_RGB
    static uint16_t px[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    static canvas_t canvas;
    if (canvas_init(&canvas, px, DISPLAY_WIDTH, DISPLAY_HEIGHT)) { ui_colour_attach(&canvas); display = canvas_u8g2(&canvas); }
#elif UI_COLOUR_MODE == UI_PALETTE
    static uint8_t px[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    static canvas_t canvas;
    if (canvas_init_indexed(&canvas, px, DISPLAY_WIDTH, DISPLAY_HEIGHT, ui_palette())) { ui_colour_attach(&canvas); display = canvas_u8g2(&canvas); }
#endif
    app_init(&app, display);
    app_boot_animation(&app);
    while (app_step(&app, controls_event(input_poll()))) panel_set_status(app.status, app.in.octave, app.in.shift);
    audio_shutdown();
    return 0;
}
#endif // PLATFORM_SIM
