#ifdef PLATFORM_SIM
#include "hal/hal_display.h"
#include "platform/sim/panel_sim.h"
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The u8g2 SDL backend (lib/u8g2/sys/sdl/common) only ships a few fixed sizes, so the simulator has its own display callback for
// any DISPLAY_WIDTH x DISPLAY_HEIGHT (hal_display.h). It reuses the backend's window and palette variables: the key handling of
// the backend is not used (see input_sim.c).
extern SDL_Window  *u8g_sdl_window;
extern SDL_Surface *u8g_sdl_screen;
extern uint32_t u8g_sdl_color[256];
extern int u8g_sdl_height, u8g_sdl_width, u8g_sdl_multiple;

static u8g2_t u8g2;
static int light_palette;

static const u8x8_display_info_t sim_info = {
    /* chip_enable_level = */ 0, /* chip_disable_level = */ 1,
    /* post_chip_enable_wait_ns = */ 0, /* pre_chip_disable_wait_ns = */ 0, /* reset_pulse_width_ms = */ 0, /* post_reset_wait_ms = */ 0,
    /* sda_setup_time_ns = */ 0, /* sck_pulse_width_ns = */ 0, /* sck_clock_hz = */ 4000000UL, /* spi_mode = */ 1,
    /* i2c_bus_clock_100kHz = */ 0, /* data_setup_time_ns = */ 0, /* write_pulse_width_ns = */ 0,
    /* tile_width = */ DISPLAY_WIDTH / 8, /* tile_height = */ DISPLAY_HEIGHT / 8,
    /* default_x_offset = */ 0, /* flipmode_x_offset = */ 0,
    /* pixel_width = */ DISPLAY_WIDTH, /* pixel_height = */ DISPLAY_HEIGHT
};

// Plain monochrome palette instead of the backend's green-on-checkerboard.
// Default white on black (like an OLED); OLED_SIM_PALETTE=light gives black on white.
static void set_mono_palette(void) {
    uint32_t on  = SDL_MapRGB(u8g_sdl_screen->format, light_palette ? 0 : 255, light_palette ? 0 : 255, light_palette ? 0 : 255);
    uint32_t off = SDL_MapRGB(u8g_sdl_screen->format, light_palette ? 255 : 0, light_palette ? 255 : 0, light_palette ? 255 : 0);
    u8g_sdl_color[0] = off;
    u8g_sdl_color[1] = u8g_sdl_color[2] = on;
    u8g_sdl_color[3] = on;
    u8g_sdl_color[4] = off;
}

static void sdl_init(void) {
    u8g_sdl_width = DISPLAY_WIDTH;
    u8g_sdl_height = DISPLAY_HEIGHT;
    u8g_sdl_multiple = (PANEL_DRAW_HEIGHT + DISPLAY_HEIGHT - 1) / DISPLAY_HEIGHT;   // pixels per display pixel: the window is at least as tall as the panel
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError()); exit(1); }
    u8g_sdl_window = SDL_CreateWindow("OLED", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, u8g_sdl_width * u8g_sdl_multiple, u8g_sdl_height * u8g_sdl_multiple, SDL_WINDOW_BORDERLESS);   // no title bar: the panel window places it
    if (!u8g_sdl_window) { fprintf(stderr, "Couldn't create window: %s\n", SDL_GetError()); exit(1); }
    u8g_sdl_screen = SDL_GetWindowSurface(u8g_sdl_window);
    if (!u8g_sdl_screen) { fprintf(stderr, "Couldn't create screen: %s\n", SDL_GetError()); exit(1); }
    const char *mode = getenv("OLED_SIM_PALETTE");
    light_palette = mode && strcmp(mode, "light") == 0;
    set_mono_palette();
    SDL_FillRect(u8g_sdl_screen, NULL, u8g_sdl_color[0]);
    SDL_UpdateWindowSurface(u8g_sdl_window);
    atexit(SDL_Quit);
}

static void put_pixel(int x, int y, int on) {
    const int m = u8g_sdl_multiple, bpp = u8g_sdl_screen->format->BytesPerPixel, stride = u8g_sdl_width * m;
    const uint32_t c = u8g_sdl_color[on ? 3 : 0];
    for (int i = 0; i < m; i++)
        for (int j = 0; j < m; j++)
            memcpy((uint8_t *)u8g_sdl_screen->pixels + (size_t)(((y * m + i) * stride + (x * m + j)) * bpp), &c, sizeof c < (size_t)bpp ? sizeof c : (size_t)bpp);
}

static uint8_t sim_gpio(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    (void)msg; (void)arg_int; (void)arg_ptr;
    u8x8_SetGPIOResult(u8x8, 1);
    return 1;
}

static uint8_t sim_display(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_DISPLAY_SETUP_MEMORY: u8x8_d_helper_display_setup_memory(u8x8, &sim_info); sdl_init(); break;
        case U8X8_MSG_DISPLAY_INIT:         u8x8_d_helper_display_init(u8x8); break;
        case U8X8_MSG_DISPLAY_DRAW_TILE: {
            const u8x8_tile_t *t = (const u8x8_tile_t *)arg_ptr;
            int x = t->x_pos * 8 + u8x8->x_offset;
            const int y = t->y_pos * 8;
            do {                                                  // arg_int repeats of the same tile data
                const uint8_t *p = t->tile_ptr;
                for (int col = 0; col < t->cnt * 8; col++, p++)
                    for (int bit = 0; bit < 8; bit++)
                        if (x + col < u8g_sdl_width && y + bit < u8g_sdl_height) put_pixel(x + col, y + bit, (*p >> bit) & 1);
                x += t->cnt * 8;
            } while (--arg_int > 0);
            SDL_UpdateWindowSurface(u8g_sdl_window);
            break;
        }
        case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
        case U8X8_MSG_DISPLAY_SET_FLIP_MODE:
        case U8X8_MSG_DISPLAY_SET_CONTRAST: break;
        default: return 0;
    }
    return 1;
}

void display_send(u8g2_t *g) { u8g2_SendBuffer(g); }

u8g2_t *display_init(void) {
    static uint8_t buf[DISPLAY_WIDTH * (DISPLAY_HEIGHT / 8)];       // full frame buffer: the whole screen is sent at once
    u8x8_t *u8x8 = u8g2_GetU8x8(&u8g2);
    u8x8_SetupDefaults(u8x8);
    u8x8->display_cb = sim_display;
    u8x8->gpio_and_delay_cb = sim_gpio;
    u8x8_SetupMemory(u8x8);
    u8g2_SetupBuffer(&u8g2, buf, DISPLAY_HEIGHT / 8, u8g2_ll_hvline_vertical_top_lsb, &u8g2_cb_r0);
    u8x8_InitDisplay(u8x8);
    u8x8_SetPowerSave(u8x8, 0);
    return &u8g2;
}
#endif // PLATFORM_SIM
