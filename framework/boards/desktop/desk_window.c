#ifdef PLATFORM_SIM
// The desktop tool window (desk_window.h): SDL's renderer and one streaming texture in RGB565. The frame follows the window: a whole zoom
// (the largest that leaves the frame at least its smallest size) and the frame as large as the window divided by it, rounded up, so the
// picture fills the window with no border (its last pixel row and column can be cut). The mouse is turned into the frame's pixels here.
#include "boards/desktop/desk_window.h"
#include <stdio.h>
#include <string.h>
#include "SDL.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static int fw, fh, tw, th;              // the frame's size; the texture's (created again when the frame changes)
static int min_w, min_h, max_w, max_h;  // the frame's limits
static int zoom = 1;
static int out_w = 1, out_h = 1, win_w = 1, win_h = 1;   // the renderer's pixels, the window's units (they differ on a high-DPI screen)
static uint8_t buttons;                 // held, kept between polls (SDL reports changes)

// The zoom and the frame for the window's size now.
static void fit(void) {
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    SDL_GetWindowSize(window, &win_w, &win_h);
    if (out_w < 1) out_w = 1;
    if (out_h < 1) out_h = 1;
    if (win_w < 1) win_w = 1;
    if (win_h < 1) win_h = 1;
    zoom = out_w / min_w < out_h / min_h ? out_w / min_w : out_h / min_h;
    if (zoom < 1) zoom = 1;
    fw = (out_w + zoom - 1) / zoom;
    fh = (out_h + zoom - 1) / zoom;
    if (fw > max_w) fw = max_w;
    if (fh > max_h) fh = max_h;
    if (fw < 8) fw = 8;
    if (fh < 8) fh = 8;
}

bool desk_window_open(const char *title, int w, int h, int max_wi, int max_hi) {
    if (window) return true;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "window: %s\n", SDL_GetError()); return false; }
    min_w = w; min_h = h; max_w = max_wi; max_h = max_hi;
    int z = 1;                          // the first size: the largest whole zoom of the smallest frame that fits the monitor
    SDL_Rect area;
    if (SDL_GetDisplayUsableBounds(0, &area) == 0)
        while ((z + 1) * w <= area.w && (z + 1) * h + 40 <= area.h) z++;   // (40: the title bar)
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w * z, h * z, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { fprintf(stderr, "window: %s\n", SDL_GetError()); return false; }
    SDL_SetWindowMinimumSize(window, w, h);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) { fprintf(stderr, "window: %s\n", SDL_GetError()); return false; }
    fit();
    SDL_StartTextInput();
    return true;
}

void desk_window_close(void) {
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    texture = NULL; renderer = NULL; window = NULL;
}

void desk_window_title(const char *title) { if (window) SDL_SetWindowTitle(window, title); }

void desk_window_frame(int *w, int *h) {
    if (w) *w = fw;
    if (h) *h = fh;
}

void desk_window_fullscreen(bool on) {
    if (!window) return;
    SDL_SetWindowFullscreen(window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    fit();
}

bool desk_window_is_fullscreen(void) { return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0; }

void desk_window_present(const uint16_t *rgb565, int stride) {
    if (!renderer) return;
    if (!texture || tw != fw || th != fh) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, fw, fh);
        if (!texture) return;
        tw = fw;
        th = fh;
    }
    SDL_UpdateTexture(texture, NULL, rgb565, stride * 2);
    SDL_SetRenderDrawColor(renderer, 20, 24, 29, 255);      // (only seen past a frame at its largest)
    SDL_RenderClear(renderer);
    const SDL_Rect dst = {0, 0, fw * zoom, fh * zoom};      // (the frame rounded up: it overhangs the window by less than a zoom)
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
}

// The mouse in the frame's pixels.
static int mx(int x) { return (int)((long long)x * out_w / win_w) / zoom; }
static int my(int y) { return (int)((long long)y * out_h / win_h) / zoom; }
static uint8_t mods_now(void) {
    const SDL_Keymod m = SDL_GetModState();
    return (uint8_t)(((m & KMOD_SHIFT) ? WG_MOD_SHIFT : 0) | ((m & KMOD_CTRL) ? WG_MOD_CTRL : 0) | ((m & KMOD_ALT) ? WG_MOD_ALT : 0));
}

static uint8_t button_bit(Uint8 b) {
    return b == SDL_BUTTON_LEFT ? WG_BTN_LEFT : b == SDL_BUTTON_RIGHT ? WG_BTN_RIGHT : b == SDL_BUTTON_MIDDLE ? WG_BTN_MIDDLE : 0;
}

// A key by its place (wg_input_t): the named keys, then the character keys by their US place (SDL's scancodes are places).
static int key_of(SDL_Scancode s) {
    switch (s) {
    case SDL_SCANCODE_TAB: return WG_KEY_TAB;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return WG_KEY_ENTER;
    case SDL_SCANCODE_ESCAPE: return WG_KEY_ESC;
    case SDL_SCANCODE_BACKSPACE: return WG_KEY_BACKSPACE;
    case SDL_SCANCODE_DELETE: return WG_KEY_DELETE;
    case SDL_SCANCODE_LEFT: return WG_KEY_LEFT;
    case SDL_SCANCODE_RIGHT: return WG_KEY_RIGHT;
    case SDL_SCANCODE_UP: return WG_KEY_UP;
    case SDL_SCANCODE_DOWN: return WG_KEY_DOWN;
    case SDL_SCANCODE_HOME: return WG_KEY_HOME;
    case SDL_SCANCODE_END: return WG_KEY_END;
    case SDL_SCANCODE_PAGEUP: return WG_KEY_PGUP;
    case SDL_SCANCODE_PAGEDOWN: return WG_KEY_PGDN;
    case SDL_SCANCODE_SPACE: return ' ';
    case SDL_SCANCODE_COMMA: return ',';
    case SDL_SCANCODE_PERIOD: return '.';
    case SDL_SCANCODE_SLASH: return '/';
    case SDL_SCANCODE_SEMICOLON: return ';';
    case SDL_SCANCODE_APOSTROPHE: return '\'';
    case SDL_SCANCODE_MINUS: return '-';
    case SDL_SCANCODE_EQUALS: return '=';
    case SDL_SCANCODE_LEFTBRACKET: return '[';
    case SDL_SCANCODE_RIGHTBRACKET: return ']';
    default: break;
    }
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'a' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    if (s == SDL_SCANCODE_0) return '0';
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F8) return WG_KEY_F1 + (s - SDL_SCANCODE_F1);
    return 0;
}

static void add_key(wg_input_t *in, int key, bool down, bool repeat) {
    if (!key || in->nkeys >= WG_KEYS_MAX) return;
    in->keys[in->nkeys++] = (wg_key_t){(uint16_t)key, (uint8_t)down, (uint8_t)repeat};
}

static void handle(wg_input_t *in, const SDL_Event *ev) {
    in->any = true;
    switch (ev->type) {
    case SDL_QUIT: in->quit = true; break;
    case SDL_WINDOWEVENT:
        if (ev->window.event == SDL_WINDOWEVENT_CLOSE) in->quit = true;
        if (ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) fit();
        break;
    case SDL_MOUSEMOTION: in->x = mx(ev->motion.x); in->y = my(ev->motion.y); break;
    case SDL_MOUSEBUTTONDOWN:
        in->x = mx(ev->button.x); in->y = my(ev->button.y);
        buttons |= button_bit(ev->button.button);
        in->pressed |= button_bit(ev->button.button);
        if (ev->button.button == SDL_BUTTON_LEFT && ev->button.clicks >= 2) in->double_click = 1;
        break;
    case SDL_MOUSEBUTTONUP:
        in->x = mx(ev->button.x); in->y = my(ev->button.y);
        buttons &= (uint8_t)~button_bit(ev->button.button);
        in->released |= button_bit(ev->button.button);
        break;
    case SDL_MOUSEWHEEL: in->wheel += ev->wheel.y * (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1); break;
    case SDL_KEYDOWN:
        // F11, Alt+Enter: full screen (the window's own: the app never sees them)
        if (!ev->key.repeat && (ev->key.keysym.scancode == SDL_SCANCODE_F11 ||
                                (ev->key.keysym.scancode == SDL_SCANCODE_RETURN && (ev->key.keysym.mod & KMOD_ALT)))) {
            desk_window_fullscreen(!desk_window_is_fullscreen());
            break;
        }
        add_key(in, key_of(ev->key.keysym.scancode), true, ev->key.repeat != 0);
        break;
    case SDL_KEYUP: add_key(in, key_of(ev->key.keysym.scancode), false, false); break;
    case SDL_TEXTINPUT: {
        const size_t n = strlen(in->text);
        snprintf(in->text + n, sizeof in->text - n, "%s", ev->text.text);
        break;
    }
    default: break;
    }
}

void desk_window_poll(wg_input_t *in, int wait_ms) {
    const int x = in->x, y = in->y;
    memset(in, 0, sizeof *in);
    in->x = x;                          // (the pointer stays where it was when it did not move)
    in->y = y;
    SDL_Event ev;
    if (wait_ms > 0 && SDL_WaitEventTimeout(&ev, wait_ms)) handle(in, &ev);
    while (SDL_PollEvent(&ev)) handle(in, &ev);
    in->buttons = buttons;
    in->mods = mods_now();
}
#endif  // PLATFORM_SIM
