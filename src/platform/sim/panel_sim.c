#ifdef PLATFORM_SIM
#include "platform/sim/panel_sim.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* The simulator's second window: a drawing of the front panel. Layout in pixels (window PANEL_W x PANEL_H):
 *
 *   left strip (x 20..90)    matrix keyboard (x 130..556)                right section (x 586..796)
 *     master volume knob       a knob above each of the first 4 columns    3 knobs
 *     encoder A                function row of 8 keys, then 4 x 8 notes    joystick pad + push button
 *     encoder B                                                            3 buttons
 *     play button
 *
 * Each control is one entry of widgets[] (built by build_layout), used both to draw it and to find it under the mouse. */

extern SDL_Surface *u8g_sdl_screen;       // the display window's surface (u8g2 SDL backend)

#define PANEL_W 816
#define KEY_PITCH 54             // matrix keys: distance between columns
#define RIGHT_X 156              // the right section sits this far right of where the 4-column panel had it
#define COL_KNOBS 4
#define PANEL_H PANEL_DRAW_HEIGHT

typedef enum { W_KNOB, W_ENC, W_BTN, W_JOY } wtype_t;

typedef struct {
    wtype_t      type;
    control_id_t ctl;        // knob / button / key: the control; encoder: the turn; joystick: the X axis
    control_id_t ctl2;       // encoder: its switch; joystick: the Y axis
    int          x, y;       // centre (knob, encoder, joystick) or top-left (button)
    int          w, h;       // button size; for round widgets w = radius
    const char  *label;
    const char  *key;        // the computer key that does the same (shown on the widget)
} widget_t;

#define MAX_WIDGETS 64
static widget_t widgets[MAX_WIDGETS];
static int n_widgets;

static SDL_Window   *window;
static SDL_Renderer *rend;
static int  val[CTL_COUNT];          // knob / axis positions
static int  angle[CTL_COUNT];        // encoder rotation in detents (drawing only)
static bool down[CTL_COUNT];
static char status[64];
static int  oct, shift;
static bool dirty = true;
static Uint32 last_draw;

static void add(widget_t w) { if (n_widgets < MAX_WIDGETS) widgets[n_widgets++] = w; }

static void build_layout(void) {
    static const char *const key_labels[KEY_ROWS][KEY_COLS] = {{"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"}, {"1", "2", "3", "4", "5", "6", "7", "8"},
                                                               {"Q", "W", "E", "R", "T", "Y", "U", "I"}, {"A", "S", "D", "F", "G", "H", "J", "K"},
                                                               {"Z", "X", "C", "V", "B", "N", "M", ","}};
    static const char *const col_labels[COL_KNOBS] = {"K1", "K2", "K3", "K4"};
    // left strip
    add((widget_t){W_KNOB, CTL_VOLUME, CTL_NONE,       55, 62, 26, 0, "VOLUME", ""});
    add((widget_t){W_ENC,  CTL_ENC_A,  CTL_ENC_A_SW,   55, 150, 22, 0, "ENC A", "[ ] \\"});
    add((widget_t){W_ENC,  CTL_ENC_B,  CTL_ENC_B_SW,   55, 236, 22, 0, "ENC B", "; ' /"});
    add((widget_t){W_BTN,  CTL_PLAY,   CTL_NONE,       20, 292, 70, 34, "PLAY", "SPACE"});
    // matrix: a knob above each of the first 4 columns, then the function row and the 4 x 8 note keys
    for (int c = 0; c < COL_KNOBS; c++)
        add((widget_t){W_KNOB, (control_id_t)(CTL_COL_KNOB_0 + c), CTL_NONE, 130 + c * KEY_PITCH + 24, 62, 17, 0, col_labels[c], ""});
    for (int r = 0; r < KEY_ROWS; r++)
        for (int c = 0; c < KEY_COLS; c++)
            add((widget_t){W_BTN, (control_id_t)CTL_KEY(r, c), CTL_NONE, 130 + c * KEY_PITCH, 104 + r * 52 + (r >= 1 ? 10 : 0), KEY_PITCH - 6, 44, "", key_labels[r][c]});
    // right section
    for (int k = 0; k < 3; k++) {
        static const char *const names[3] = {"R1", "R2", "R3"};
        add((widget_t){W_KNOB, (control_id_t)(CTL_KNOB_R1 + k), CTL_NONE, RIGHT_X + 470 + k * 65, 62, 19, 0, names[k], ""});
    }
    add((widget_t){W_JOY, CTL_JOY_X, CTL_JOY_Y, RIGHT_X + 535, 172, 50, 0, "JOYSTICK", "ARROWS"});
    add((widget_t){W_BTN, CTL_JOY_SW, CTL_NONE, RIGHT_X + 495, 258, 80, 40, "PUSH", "R CTRL"});
    add((widget_t){W_BTN, CTL_BTN_1, CTL_NONE, RIGHT_X + 440, 330, 56, 36, "B1", "ENTER"});
    add((widget_t){W_BTN, CTL_BTN_2, CTL_NONE, RIGHT_X + 505, 330, 56, 36, "B2", "BKSP"});
    add((widget_t){W_BTN, CTL_BTN_3, CTL_NONE, RIGHT_X + 570, 330, 56, 36, "B3", "L SHIFT"});
}

/* ---------------- drawing primitives ---------------- */

// Classic 5x7 font, ASCII 0x20..0x5D (upper case only), one byte per column, bit 0 = top row.
static const unsigned char font5x7[62][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0x00,0x08,0x07,0x03,0x00},{0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
    {0x2A,0x1C,0x7F,0x1C,0x2A},{0x08,0x08,0x3E,0x08,0x08},{0x00,0x80,0x70,0x30,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x00,0x60,0x60,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x72,0x49,0x49,0x49,0x46},{0x21,0x41,0x49,0x4D,0x33},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x31},{0x41,0x21,0x11,0x09,0x07},{0x36,0x49,0x49,0x49,0x36},
    {0x46,0x49,0x49,0x29,0x1E},{0x00,0x00,0x14,0x00,0x00},{0x00,0x40,0x34,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x59,0x09,0x06},{0x3E,0x41,0x5D,0x59,0x4E},{0x7C,0x12,0x11,0x12,0x7C},{0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x41,0x3E},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x41,0x51,0x73},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x1C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},{0x26,0x49,0x49,0x49,0x32},{0x03,0x01,0x7F,0x01,0x03},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x59,0x49,0x4D,0x43},
    {0x00,0x7F,0x41,0x41,0x00},{0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},
};

static int text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

static void text(int x, int y, int scale, const char *s) {
    for (; *s; s++, x += 6 * scale) {
        int c = toupper((unsigned char)*s);
        if (c < 0x20 || c > 0x5D) c = '?';
        const unsigned char *g = font5x7[c - 0x20];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1 << row)) { SDL_Rect r = {x + col * scale, y + row * scale, scale, scale}; SDL_RenderFillRect(rend, &r); }
    }
}

static void text_centered(int cx, int y, int scale, const char *s) { text(cx - text_w(s, scale) / 2, y, scale, s); }

static void color(int r, int g, int b) { SDL_SetRenderDrawColor(rend, (Uint8)r, (Uint8)g, (Uint8)b, 255); }

static void disc(int cx, int cy, int r) {
    for (int dy = -r; dy <= r; dy++) {
        const int dx = (int)sqrtf((float)(r * r - dy * dy));
        SDL_RenderDrawLine(rend, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void ring(int cx, int cy, int r) {
    for (int a = 0; a < 360; a += 2) {
        const float t = (float)a * 3.14159265f / 180.0f;
        SDL_RenderDrawPoint(rend, cx + (int)lroundf(cosf(t) * (float)r), cy + (int)lroundf(sinf(t) * (float)r));
    }
}

// Line from the centre in a direction given in degrees clockwise from "up".
static void pointer(int cx, int cy, float deg, int from, int to) {
    const float t = deg * 3.14159265f / 180.0f, sx = sinf(t), sy = -cosf(t);
    SDL_RenderDrawLine(rend, cx + (int)(sx * (float)from), cy + (int)(sy * (float)from), cx + (int)(sx * (float)to), cy + (int)(sy * (float)to));
}

/* ---------------- widgets ---------------- */

static void draw_knob(const widget_t *w) {
    const int r = w->w, v = val[w->ctl];
    color(48, 52, 60); disc(w->x, w->y, r);
    color(150, 156, 168); ring(w->x, w->y, r); ring(w->x, w->y, r - 1);
    color(255, 190, 60); pointer(w->x, w->y, -135.0f + 270.0f * (float)v / (float)INPUT_VALUE_MAX, r / 3, r - 3);
    color(190, 196, 208); text_centered(w->x, w->y + r + 5, 1, w->label);
}

static void draw_encoder(const widget_t *w) {
    const int r = w->w;
    const bool pressed = down[w->ctl2];
    color(pressed ? 255 : 48, pressed ? 190 : 52, pressed ? 60 : 60); disc(w->x, w->y, r);
    color(150, 156, 168); ring(w->x, w->y, r); ring(w->x, w->y, r - 1);
    color(pressed ? 40 : 190, pressed ? 40 : 196, pressed ? 40 : 208);
    for (int i = 0; i < 12; i++) pointer(w->x, w->y, (float)i * 30.0f + (float)angle[w->ctl] * 15.0f, r - 7, r - 2);
    color(190, 196, 208); text_centered(w->x, w->y + r + 5, 1, w->label);
    text_centered(w->x, w->y + r + 15, 1, w->key);
}

static void draw_button(const widget_t *w) {
    const bool pressed = down[w->ctl];
    SDL_Rect r = {w->x, w->y, w->w, w->h};
    color(pressed ? 255 : 48, pressed ? 190 : 52, pressed ? 60 : 60); SDL_RenderFillRect(rend, &r);
    color(150, 156, 168); SDL_RenderDrawRect(rend, &r);
    color(pressed ? 30 : 200, pressed ? 30 : 205, pressed ? 30 : 215);
    if (w->label[0]) text_centered(w->x + w->w / 2, w->key[0] ? w->y + 5 : w->y + w->h / 2 - 7, 2, w->label);
    if (w->key[0] && !w->label[0]) text_centered(w->x + w->w / 2, w->y + w->h / 2 - 7, 2, w->key);
    else if (w->key[0]) text_centered(w->x + w->w / 2, w->y + w->h - 11, 1, w->key);
}

static void draw_joystick(const widget_t *w) {
    const int R = w->w, sx = w->x + (val[w->ctl] - INPUT_AXIS_CENTER) * (R - 14) / INPUT_AXIS_CENTER, sy = w->y + (val[w->ctl2] - INPUT_AXIS_CENTER) * (R - 14) / INPUT_AXIS_CENTER;
    color(32, 36, 44); disc(w->x, w->y, R);
    color(150, 156, 168); ring(w->x, w->y, R);
    color(70, 76, 88); SDL_RenderDrawLine(rend, w->x - R, w->y, w->x + R, w->y); SDL_RenderDrawLine(rend, w->x, w->y - R, w->x, w->y + R);
    color(110, 116, 130); disc(sx, sy, 14);
    color(220, 225, 235); ring(sx, sy, 14);
    color(190, 196, 208); text_centered(w->x, w->y + R + 5, 1, w->label);
    text_centered(w->x, w->y + R + 15, 1, w->key);
}

// The display window is borderless and follows the panel: it sits to its right (outside the title bar's frame), centred vertically.
static int off_y;                  // the window can be taller than the drawing (it matches the display window): the drawing is centred
static int win_h = PANEL_H;
static int placed_x = -100000, placed_y = -100000;

static void place_oled(void) {
    SDL_Window *oled = SDL_GetWindowFromID(1);
    if (!window || !oled) return;
    int px, py, ow, oh, top = 0, left = 0, bottom = 0, right = 0;
    SDL_GetWindowPosition(window, &px, &py);
    if (px == placed_x && py == placed_y) return;                   // the panel has not moved
    placed_x = px; placed_y = py;
    SDL_GetWindowSize(oled, &ow, &oh);
    SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
    (void)oh;
    SDL_SetWindowPosition(oled, px + PANEL_W + right + 4, py);       // same height as the panel: the tops line up
}

static int shot_done;

void panel_render(void) {
    place_oled();
    const Uint32 now = SDL_GetTicks();
    if (getenv("OLED_SIM_PANEL_SHOT") && !shot_done && now > 700) dirty = true;
    if (!window || !dirty || now - last_draw < 15) return;
    dirty = false; last_draw = now;
    color(22, 24, 30); SDL_RenderClear(rend);
    SDL_Rect vp = {0, off_y, PANEL_W, PANEL_H};
    SDL_RenderSetViewport(rend, &vp);
    color(90, 96, 110);                                            // section frames
    SDL_Rect f1 = {10, 20, 90, 330}, f2 = {120, 20, 290 + RIGHT_X, 350}, f3 = {420 + RIGHT_X, 20, 230, 360};
    SDL_RenderDrawRect(rend, &f1); SDL_RenderDrawRect(rend, &f2); SDL_RenderDrawRect(rend, &f3);
    for (int i = 0; i < n_widgets; i++) {
        switch (widgets[i].type) {
            case W_KNOB: draw_knob(&widgets[i]); break;
            case W_ENC:  draw_encoder(&widgets[i]); break;
            case W_BTN:  draw_button(&widgets[i]); break;
            case W_JOY:  draw_joystick(&widgets[i]); break;
        }
    }
    color(190, 196, 208);
    text(20, 8, 1, "SEVEN SYNTH PANEL   (OLED = OTHER WINDOW)");
    char line[96];
    snprintf(line, sizeof line, "%s", status[0] ? status : "-");
    color(255, 190, 60); text(20, 385, 2, line);
    snprintf(line, sizeof line, "OCTAVE %+d    SHIFT %s", oct, shift ? "ON" : "OFF");
    color(shift ? 255 : 150, shift ? 190 : 156, shift ? 60 : 168); text(20, 410, 2, line);
    const char *shot = getenv("OLED_SIM_PANEL_SHOT");                 // debugging aid: save the first frames of the panel as a BMP
    if (shot && !shot_done && SDL_GetTicks() > 700) {
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, PANEL_W, win_h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surf && SDL_RenderReadPixels(rend, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0) SDL_SaveBMP(surf, shot);
        if (surf) SDL_FreeSurface(surf);
        char oled_path[300];                                          // and the display window next to it
        snprintf(oled_path, sizeof oled_path, "%s.oled.bmp", shot);
        if (u8g_sdl_screen) SDL_SaveBMP(u8g_sdl_screen, oled_path);
        shot_done = 1;
    }
    SDL_RenderPresent(rend);
}

/* ---------------- window, state, mouse ---------------- */

void panel_open(void) {
    if (window) return;
    build_layout();
    for (int i = 0; i < CTL_COUNT; i++) val[i] = INPUT_VALUE_MAX / 2;
    val[CTL_JOY_X] = val[CTL_JOY_Y] = INPUT_AXIS_CENTER;
    int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    SDL_Window *oled = SDL_GetWindowFromID(1);                      // the display window (borderless): the panel takes its place, the display goes to its right
    int ox = 0, oy = 0;
    if (oled) { SDL_GetWindowPosition(oled, &ox, &oy); x = ox; y = oy; }
    int oh = 0, ow = 0;
    if (oled) SDL_GetWindowSize(oled, &ow, &oh);
    win_h = oh > PANEL_H ? oh : PANEL_H;                            // as tall as the display window (display_sim.c picks its scale for that)
    off_y = (win_h - PANEL_H) / 2;
    window = SDL_CreateWindow("Panel", x, y, PANEL_W, win_h, 0);
    if (window) rend = SDL_CreateRenderer(window, -1, 0);
    if (!rend) { if (window) SDL_DestroyWindow(window); window = NULL; }
    dirty = true;
}

int panel_owns_window(Uint32 id) { return window && SDL_GetWindowID(window) == id; }

void panel_track(control_id_t ctl, input_kind_t kind, int value) {
    if (ctl <= CTL_NONE || ctl >= CTL_COUNT) return;
    switch (kind) {
        case IN_PRESS:   down[ctl] = true; break;
        case IN_RELEASE: down[ctl] = false; break;
        case IN_DELTA:   angle[ctl] += value; break;
        case IN_VALUE:   val[ctl] = value; break;
        default: break;
    }
    dirty = true;
}

void panel_set_status(const char *s, int octave, int sh) {
    if (strcmp(status, s) || oct != octave || shift != sh) dirty = true;
    snprintf(status, sizeof status, "%s", s);
    oct = octave; shift = sh;
}

static int hit(int mx, int my) {
    for (int i = n_widgets - 1; i >= 0; i--) {
        const widget_t *w = &widgets[i];
        if (w->type == W_BTN) { if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) return i; }
        else { const int dx = mx - w->x, dy = my - w->y; if (dx * dx + dy * dy <= (w->w + 3) * (w->w + 3)) return i; }
    }
    return -1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int active = -1;                      // widget held down by the mouse (button / encoder switch)
static int drag_knob = -1, drag_y0, drag_v0; // knob being dragged
static int drag_joy = -1;                    // joystick being dragged

static void joy_from_mouse(const widget_t *w, int mx, int my, panel_emit_fn emit) {
    const int x = clampi(INPUT_AXIS_CENTER + (mx - w->x) * INPUT_AXIS_CENTER / w->w, 0, INPUT_VALUE_MAX);
    const int y = clampi(INPUT_AXIS_CENTER + (my - w->y) * INPUT_AXIS_CENTER / w->w, 0, INPUT_VALUE_MAX);
    if (x != val[w->ctl])  emit(w->ctl, IN_VALUE, x);
    if (y != val[w->ctl2]) emit(w->ctl2, IN_VALUE, y);
}

void panel_handle_event(const SDL_Event *ev, panel_emit_fn emit) {
    switch (ev->type) {
        case SDL_MOUSEBUTTONDOWN: {
            if (ev->button.button != SDL_BUTTON_LEFT) break;
            const int i = hit(ev->button.x, ev->button.y - off_y);
            if (i < 0) break;
            const widget_t *w = &widgets[i];
            if (w->type == W_KNOB) { drag_knob = i; drag_y0 = ev->button.y - off_y; drag_v0 = val[w->ctl]; }
            else if (w->type == W_JOY) { drag_joy = i; joy_from_mouse(w, ev->button.x, ev->button.y - off_y, emit); }
            else { active = i; emit(w->type == W_ENC ? w->ctl2 : w->ctl, IN_PRESS, 0); }
            break;
        }
        case SDL_MOUSEMOTION:
            if (drag_knob >= 0) {
                const widget_t *w = &widgets[drag_knob];
                const int v = clampi(drag_v0 + (drag_y0 - (ev->motion.y - off_y)) * 6, 0, INPUT_VALUE_MAX);
                if (v != val[w->ctl]) emit(w->ctl, IN_VALUE, v);
            } else if (drag_joy >= 0) joy_from_mouse(&widgets[drag_joy], ev->motion.x, ev->motion.y - off_y, emit);
            break;
        case SDL_MOUSEBUTTONUP:
            if (ev->button.button != SDL_BUTTON_LEFT) break;
            if (active >= 0) { const widget_t *w = &widgets[active]; emit(w->type == W_ENC ? w->ctl2 : w->ctl, IN_RELEASE, 0); active = -1; }
            if (drag_joy >= 0) {                                  // the stick springs back
                const widget_t *w = &widgets[drag_joy];
                emit(w->ctl, IN_VALUE, INPUT_AXIS_CENTER); emit(w->ctl2, IN_VALUE, INPUT_AXIS_CENTER);
            }
            drag_knob = drag_joy = -1;
            break;
        case SDL_MOUSEWHEEL: {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            my -= off_y;
            const int i = hit(mx, my);
            if (i < 0) break;
            const widget_t *w = &widgets[i];
            const int d = ev->wheel.y * (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
            if (w->type == W_ENC) emit(w->ctl, IN_DELTA, d);
            else if (w->type == W_KNOB) { const int v = clampi(val[w->ctl] + d * 24, 0, INPUT_VALUE_MAX); if (v != val[w->ctl]) emit(w->ctl, IN_VALUE, v); }
            break;
        }
        default: break;
    }
}
#endif // PLATFORM_SIM
