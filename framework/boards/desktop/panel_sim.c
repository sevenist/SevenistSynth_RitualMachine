#ifdef PLATFORM_SIM
#include "boards/desktop/panel_sim.h"
#include "boards/desktop/leds_sim.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* The simulator's second window: a drawing of the front panel of the board the program was built for (sim_board.h).
 *
 * Nothing here knows a board. The parts, their places and the size of the drawing come from the board's table (sim_board.c): a key,
 * a button, an encoder, an endless pot, a knob, a joystick, each drawn by its kind at its place and found under the mouse by its
 * body. The PC key written on a part is input_sim.c's. Under the parts: what the application last did (panel_set_status). */

extern SDL_Surface *u8g_sdl_screen;       // the display window's surface (u8g2 SDL backend)

#define NONE INPUT_NO_CONTROL

static sim_panel_t layout;
static bool laid_out;

static SDL_Window   *window;
static SDL_Renderer *rend;
static int  val[SIM_CONTROL_COUNT];            // knob / axis positions; an endless pot's position when it reports one
static int  angle[SIM_CONTROL_COUNT];          // encoder / endless pot rotation in detents (drawing only)
static bool down[SIM_CONTROL_COUNT];
static char status[64];
static int  oct, shift;
static bool dirty = true;
static Uint32 last_draw;

const sim_panel_t *panel_layout(void) {
    if (!laid_out) {
        laid_out = true;
        sim_panel_build(&layout, SIM_CONTROLS, SIM_CONTROL_COUNT);
        for (int i = 0; i < layout.count; i++) {                    // where the parts rest
            const sim_part_t *p = &layout.parts[i];
            if (p->kind == PART_KNOB) val[p->ctl] = INPUT_VALUE_MAX / 2;
            if (p->kind == PART_JOYSTICK) { val[p->ctl] = INPUT_AXIS_CENTER; if (p->ctl2 != NONE) val[p->ctl2] = INPUT_AXIS_CENTER; }
        }
    }
    return &layout;
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

/* ---------------- parts ---------------- */

static bool is_down(int ctl) { return ctl != NONE && down[ctl]; }

// The two lines under a round part: its name, then its PC keys (the turn, then the push).
static void texts_under(const sim_part_t *p, const char *name) {
    char keys[32];
    const char *turn = input_sim_key_hint(p->ctl), *push = p->sw != NONE ? input_sim_key_hint(p->sw) : "";
    snprintf(keys, sizeof keys, "%s%s%s", turn, turn[0] && push[0] ? " " : "", push);
    color(190, 196, 208);
    text_centered(p->x, p->y + p->hh + 5, 1, name);
    if (keys[0]) text_centered(p->x, p->y + p->hh + 15, 1, keys);
}

static void body(const sim_part_t *p, bool pressed) {
    const int r = p->hw;
    color(pressed ? 255 : 48, pressed ? 190 : 52, pressed ? 60 : 60); disc(p->x, p->y, r);
    color(150, 156, 168); ring(p->x, p->y, r); ring(p->x, p->y, r - 1);
}

// A key shows its PC key (its name when it has none); a button its name, and its PC key under it.
static void draw_key(const sim_part_t *p) {
    const bool pressed = down[p->ctl];
    const input_control_t *c = &SIM_CONTROLS[p->ctl];
    const char *key = input_sim_key_hint(p->ctl);
    SDL_Rect r = {p->x - p->hw, p->y - p->hh, 2 * p->hw, 2 * p->hh};
    color(pressed ? 255 : 48, pressed ? 190 : 52, pressed ? 60 : 60); SDL_RenderFillRect(rend, &r);
    color(150, 156, 168); SDL_RenderDrawRect(rend, &r);
    if (c->led) {                                                    // its LED (hal_leds.h): a strip along the top edge
        const led_color_t l = leds_sim_get(p->ctl);
        SDL_Rect led = {r.x + 4, r.y + 3, r.w - 8, 5};
        if (l.r | l.g | l.b) { color(l.r, l.g, l.b); SDL_RenderFillRect(rend, &led); }
        else { color(70, 74, 82); SDL_RenderDrawRect(rend, &led); }
    }
    color(pressed ? 30 : 200, pressed ? 30 : 205, pressed ? 30 : 215);
    if (p->kind == PART_KEY) {
        if (key[0]) text_centered(p->x, p->y - 7, 2, key);
        else text_centered(p->x, p->y - 3, 1, c->name);
        return;
    }
    const int scale = text_w(c->name, 2) <= 2 * p->hw - 2 ? 2 : 1;
    text_centered(p->x, key[0] ? r.y + 5 : p->y - 7 * scale / 2, scale, c->name);
    if (key[0]) text_centered(p->x, r.y + r.h - 11, 1, key);
}

static void draw_knob(const sim_part_t *p) {
    const int r = p->hw;
    body(p, is_down(p->sw));
    color(255, 190, 60); pointer(p->x, p->y, -135.0f + 270.0f * (float)val[p->ctl] / (float)INPUT_VALUE_MAX, r / 3, r - 3);
    texts_under(p, SIM_CONTROLS[p->ctl].name);
}

static void draw_encoder(const sim_part_t *p) {
    const int r = p->hw;
    const bool pressed = is_down(p->sw);
    body(p, pressed);
    color(pressed ? 40 : 190, pressed ? 40 : 196, pressed ? 40 : 208);
    for (int i = 0; i < 12; i++) pointer(p->x, p->y, (float)i * 30.0f + (float)angle[p->ctl] * 15.0f, r - 7, r - 2);
    texts_under(p, SIM_CONTROLS[p->ctl].name);
}

// A pot without end stops: one pointer that goes all the way round (steps and position both turn it), a ring in the middle.
static void draw_endless(const sim_part_t *p) {
    const int r = p->hw;
    const bool pressed = is_down(p->sw);
    body(p, pressed);
    color(pressed ? 40 : 150, pressed ? 40 : 156, pressed ? 40 : 168); ring(p->x, p->y, r / 3);
    color(pressed ? 40 : 255, pressed ? 40 : 190, pressed ? 40 : 60);
    pointer(p->x, p->y, (float)angle[p->ctl] * 15.0f + 360.0f * (float)val[p->ctl] / (float)(INPUT_VALUE_MAX + 1), r / 3, r - 3);
    texts_under(p, SIM_CONTROLS[p->ctl].name);
}

static void draw_joystick(const sim_part_t *p) {
    const int R = p->hw, cap = 13, vy = p->ctl2 != NONE ? val[p->ctl2] : INPUT_AXIS_CENTER;
    const int sx = p->x + (val[p->ctl] - INPUT_AXIS_CENTER) * (R - cap) / INPUT_AXIS_CENTER, sy = p->y + (vy - INPUT_AXIS_CENTER) * (R - cap) / INPUT_AXIS_CENTER;
    const bool pressed = is_down(p->sw);
    color(32, 36, 44); disc(p->x, p->y, R);
    color(150, 156, 168); ring(p->x, p->y, R);
    color(70, 76, 88); SDL_RenderDrawLine(rend, p->x - R, p->y, p->x + R, p->y); SDL_RenderDrawLine(rend, p->x, p->y - R, p->x, p->y + R);
    color(pressed ? 255 : 110, pressed ? 190 : 116, pressed ? 60 : 130); disc(sx, sy, cap);
    color(220, 225, 235); ring(sx, sy, cap);
    char name[24];                                                   // "JOY X" -> "JOY": the axis's name without its last word
    snprintf(name, sizeof name, "%s", SIM_CONTROLS[p->ctl].name);
    char *cut = strrchr(name, ' ');
    if (cut) *cut = 0;
    texts_under(p, name);
}

// The display window is borderless and follows the panel: it sits to its right (outside the title bar's frame), the tops line up.
static int off_y;                  // the window can be taller than the drawing (it is at least as tall as the display window): the drawing is centred
static int win_h;
static int placed_x = -100000, placed_y = -100000;

static void place_oled(void) {
    SDL_Window *oled = SDL_GetWindowFromID(1);
    if (!window || !oled) return;
    int px, py, top = 0, left = 0, bottom = 0, right = 0;
    SDL_GetWindowPosition(window, &px, &py);
    if (px == placed_x && py == placed_y) return;                   // the panel has not moved
    placed_x = px; placed_y = py;
    SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
    SDL_SetWindowPosition(oled, px + layout.width + right + 4, py);
}

static int shot_done;

void panel_render(void) {
    place_oled();
    const Uint32 now = SDL_GetTicks();
    if (getenv("OLED_SIM_PANEL_SHOT") && !shot_done && now > 700) dirty = true;
    if (!window || !dirty || now - last_draw < 15) return;
    dirty = false; last_draw = now;
    color(22, 24, 30); SDL_RenderClear(rend);
    SDL_Rect vp = {0, off_y, layout.width, layout.height};
    SDL_RenderSetViewport(rend, &vp);
    const int frame_top = SIM_PANEL_TOP - 8, parts_bottom = layout.height - SIM_PANEL_STATUS - 4;
    color(90, 96, 110);                                            // the board's outline
    SDL_Rect frame = {6, frame_top, layout.width - 12, parts_bottom - frame_top};
    SDL_RenderDrawRect(rend, &frame);
    for (int i = 0; i < layout.count; i++) {
        const sim_part_t *p = &layout.parts[i];
        switch (p->kind) {
            case PART_KEY:
            case PART_BUTTON:   draw_key(p); break;
            case PART_ENCODER:  draw_encoder(p); break;
            case PART_ENDLESS:  draw_endless(p); break;
            case PART_KNOB:     draw_knob(p); break;
            case PART_JOYSTICK: draw_joystick(p); break;
            default: break;
        }
    }
    color(190, 196, 208);
    text(8, 6, 1, "BOARD " SIM_BOARD_NAME "   (DISPLAY = OTHER WINDOW)");
    char line[96];
    snprintf(line, sizeof line, "%s", status[0] ? status : "-");
    color(255, 190, 60); text(8, layout.height - SIM_PANEL_STATUS + 4, 2, line);
    snprintf(line, sizeof line, "OCTAVE %+d    SHIFT %s", oct, shift ? "ON" : "OFF");
    color(shift ? 255 : 150, shift ? 190 : 156, shift ? 60 : 168); text(8, layout.height - SIM_PANEL_STATUS + 26, 2, line);
    const char *shot = getenv("OLED_SIM_PANEL_SHOT");                 // debugging aid: save the first frames of the panel as a BMP
    if (shot && !shot_done && SDL_GetTicks() > 700) {
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, layout.width, win_h, 32, SDL_PIXELFORMAT_ARGB8888);
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
    panel_layout();
    int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    SDL_Window *oled = SDL_GetWindowFromID(1);                      // the display window (borderless): the panel takes its place, the display goes to its right
    int ox = 0, oy = 0;
    if (oled) { SDL_GetWindowPosition(oled, &ox, &oy); x = ox; y = oy; }
    int oh = 0, ow = 0;
    if (oled) SDL_GetWindowSize(oled, &ow, &oh);
    win_h = oh > layout.height ? oh : layout.height;                // at least as tall as the display window
    off_y = (win_h - layout.height) / 2;
    window = SDL_CreateWindow("Panel", x, y, layout.width, win_h, 0);
    if (window) rend = SDL_CreateRenderer(window, -1, 0);
    if (!rend) { if (window) SDL_DestroyWindow(window); window = NULL; }
    dirty = true;
}

int panel_owns_window(Uint32 id) { return window && SDL_GetWindowID(window) == id; }

void panel_track(int ctl, input_kind_t kind, int value) {
    if (ctl < 0 || ctl >= SIM_CONTROL_COUNT) return;
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
    for (int i = layout.count - 1; i >= 0; i--) {
        const sim_part_t *p = &layout.parts[i];
        const int dx = mx - p->x, dy = my - p->y;
        if (p->kind == PART_KEY || p->kind == PART_BUTTON) { if (abs(dx) <= p->hw && abs(dy) <= p->hh) return i; }
        else if (dx * dx + dy * dy <= (p->hw + 3) * (p->hw + 3)) return i;
    }
    return -1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int held = NONE, held_right = NONE;   // the controls the left / the right mouse button holds down
static int drag = -1, drag_y0, drag_v0;      // knob or endless pot being dragged: where the drag started, the knob's value then
static int drag_sent;                        // endless pot: the steps the drag has reported
static int drag_joy = -1;                    // joystick being dragged

#define DRAG_PER_STEP 6                      // pixels of drag for one step of an endless pot

static void joy_from_mouse(const sim_part_t *p, int mx, int my, panel_emit_fn emit) {
    const int x = clampi(INPUT_AXIS_CENTER + (mx - p->x) * INPUT_AXIS_CENTER / p->hw, 0, INPUT_VALUE_MAX);
    const int y = clampi(INPUT_AXIS_CENTER + (my - p->y) * INPUT_AXIS_CENTER / p->hw, 0, INPUT_VALUE_MAX);
    if (x != val[p->ctl]) emit(p->ctl, IN_VALUE, x);
    if (p->ctl2 != NONE && y != val[p->ctl2]) emit(p->ctl2, IN_VALUE, y);
}

void panel_handle_event(const SDL_Event *ev, panel_emit_fn emit) {
    switch (ev->type) {
        case SDL_MOUSEBUTTONDOWN: {
            const int i = hit(ev->button.x, ev->button.y - off_y);
            if (i < 0) break;
            const sim_part_t *p = &layout.parts[i];
            if (ev->button.button == SDL_BUTTON_RIGHT) {            // the push of a round part
                if (p->sw != NONE && held_right == NONE) { held_right = p->sw; emit(p->sw, IN_PRESS, 0); }
                break;
            }
            if (ev->button.button != SDL_BUTTON_LEFT) break;
            switch (p->kind) {
                case PART_KNOB:
                case PART_ENDLESS:  drag = i; drag_y0 = ev->button.y - off_y; drag_v0 = val[p->ctl]; drag_sent = 0; break;
                case PART_JOYSTICK: drag_joy = i; joy_from_mouse(p, ev->button.x, ev->button.y - off_y, emit); break;
                case PART_ENCODER:  if (p->sw != NONE && held == NONE) { held = p->sw; emit(p->sw, IN_PRESS, 0); } break;
                default:            if (held == NONE) { held = p->ctl; emit(p->ctl, IN_PRESS, 0); } break;
            }
            break;
        }
        case SDL_MOUSEMOTION:
            if (drag >= 0) {
                const sim_part_t *p = &layout.parts[drag];
                const int moved = drag_y0 - (ev->motion.y - off_y);
                if (p->kind == PART_KNOB) {
                    const int v = clampi(drag_v0 + moved * 6, 0, INPUT_VALUE_MAX);
                    if (v != val[p->ctl]) emit(p->ctl, IN_VALUE, v);
                } else if (moved / DRAG_PER_STEP != drag_sent) {
                    const int steps = moved / DRAG_PER_STEP - drag_sent;
                    drag_sent += steps;
                    emit(p->ctl, IN_DELTA, steps);
                }
            } else if (drag_joy >= 0) joy_from_mouse(&layout.parts[drag_joy], ev->motion.x, ev->motion.y - off_y, emit);
            break;
        case SDL_MOUSEBUTTONUP:
            if (ev->button.button == SDL_BUTTON_RIGHT) {
                if (held_right != NONE) { emit(held_right, IN_RELEASE, 0); held_right = NONE; }
                break;
            }
            if (ev->button.button != SDL_BUTTON_LEFT) break;
            if (held != NONE) { emit(held, IN_RELEASE, 0); held = NONE; }
            if (drag_joy >= 0) {                                  // the stick springs back
                const sim_part_t *p = &layout.parts[drag_joy];
                emit(p->ctl, IN_VALUE, INPUT_AXIS_CENTER);
                if (p->ctl2 != NONE) emit(p->ctl2, IN_VALUE, INPUT_AXIS_CENTER);
            }
            drag = drag_joy = -1;
            break;
        case SDL_MOUSEWHEEL: {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            my -= off_y;
            const int i = hit(mx, my);
            if (i < 0) break;
            const sim_part_t *p = &layout.parts[i];
            const int d = ev->wheel.y * (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
            if (p->kind == PART_ENCODER || p->kind == PART_ENDLESS) emit(p->ctl, IN_DELTA, d);
            else if (p->kind == PART_KNOB) { const int v = clampi(val[p->ctl] + d * 24, 0, INPUT_VALUE_MAX); if (v != val[p->ctl]) emit(p->ctl, IN_VALUE, v); }
            break;
        }
        default: break;
    }
}
#endif // PLATFORM_SIM
