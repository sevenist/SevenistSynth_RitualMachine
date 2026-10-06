#ifdef PLATFORM_SIM
#include "hal/hal_input.h"
#include "platform/sim/panel_sim.h"
#include "platform/sim/sim_card.h"
#include "SDL.h"

// The simulator's input: the keyboard and the mouse (on the panel window, see panel_sim.c) both act as the prototype's physical
// controls and report them as hardware events (see hal/hal_input.h). What a control DOES is decided in core/bindings.c (and, for the
// matrix keys, by the key layout of core/keymap.h), not here.
//
// To change the keyboard shortcuts edit keymap[] below. Keys are named by PHYSICAL position (SDL scancodes, QWERTY names), so on an
// AZERTY keyboard "Q" is the key labelled A, "A" the key labelled Q, "Z" is W, "W" is Z, "M" is ",", "[" is "^", "]" is "$" and
// ";" is "M":
//
//   matrix keys   F1 F2 F3 F4 F5 F6 F7 F8     function row (top)
//                 1  2  3  4  5  6  7  8      \  the 4 x 8 note rows; the bottom row (Z X C V B N M ,)
//                 Q  W  E  R  T  Y  U  I       > holds the lowest notes
//                 A  S  D  F  G  H  J  K      /
//                 Z  X  C  V  B  N  M  ,
//   encoder A     [ ] turn, \ push          encoder B   ; ' turn, / push
//   play          Space                     buttons     B1 Enter, B2 Backspace / Delete, B3 (Shift) Left Shift
//   joystick      arrows, push = Right Ctrl           Esc / close a window = quit
//   knobs         mouse only (panel window): wheel or drag

typedef enum {
    K_BUTTON,       // key down = IN_PRESS of `ctl`, key up = IN_RELEASE
    K_TURN,         // every key down (also auto-repeat) turns the encoder `ctl` by `value` detents
    K_AXIS,         // key down = the axis `ctl` goes to `value`, key up = back to the centre
} key_role_t;

typedef struct { SDL_Scancode sc; key_role_t role; control_id_t ctl; int value; } keymap_t;

static const keymap_t keymap[] = {
    // matrix keyboard: CTL_KEY(row, col)
    {SDL_SCANCODE_F1, K_BUTTON, CTL_KEY(0, 0), 0}, {SDL_SCANCODE_F2, K_BUTTON, CTL_KEY(0, 1), 0}, {SDL_SCANCODE_F3, K_BUTTON, CTL_KEY(0, 2), 0}, {SDL_SCANCODE_F4, K_BUTTON, CTL_KEY(0, 3), 0},
    {SDL_SCANCODE_F5, K_BUTTON, CTL_KEY(0, 4), 0}, {SDL_SCANCODE_F6, K_BUTTON, CTL_KEY(0, 5), 0}, {SDL_SCANCODE_F7, K_BUTTON, CTL_KEY(0, 6), 0}, {SDL_SCANCODE_F8, K_BUTTON, CTL_KEY(0, 7), 0},
    {SDL_SCANCODE_1,  K_BUTTON, CTL_KEY(1, 0), 0}, {SDL_SCANCODE_2,  K_BUTTON, CTL_KEY(1, 1), 0}, {SDL_SCANCODE_3,  K_BUTTON, CTL_KEY(1, 2), 0}, {SDL_SCANCODE_4,  K_BUTTON, CTL_KEY(1, 3), 0},
    {SDL_SCANCODE_5,  K_BUTTON, CTL_KEY(1, 4), 0}, {SDL_SCANCODE_6,  K_BUTTON, CTL_KEY(1, 5), 0}, {SDL_SCANCODE_7,  K_BUTTON, CTL_KEY(1, 6), 0}, {SDL_SCANCODE_8,  K_BUTTON, CTL_KEY(1, 7), 0},
    {SDL_SCANCODE_Q,  K_BUTTON, CTL_KEY(2, 0), 0}, {SDL_SCANCODE_W,  K_BUTTON, CTL_KEY(2, 1), 0}, {SDL_SCANCODE_E,  K_BUTTON, CTL_KEY(2, 2), 0}, {SDL_SCANCODE_R,  K_BUTTON, CTL_KEY(2, 3), 0},
    {SDL_SCANCODE_T,  K_BUTTON, CTL_KEY(2, 4), 0}, {SDL_SCANCODE_Y,  K_BUTTON, CTL_KEY(2, 5), 0}, {SDL_SCANCODE_U,  K_BUTTON, CTL_KEY(2, 6), 0}, {SDL_SCANCODE_I,  K_BUTTON, CTL_KEY(2, 7), 0},
    {SDL_SCANCODE_A,  K_BUTTON, CTL_KEY(3, 0), 0}, {SDL_SCANCODE_S,  K_BUTTON, CTL_KEY(3, 1), 0}, {SDL_SCANCODE_D,  K_BUTTON, CTL_KEY(3, 2), 0}, {SDL_SCANCODE_F,  K_BUTTON, CTL_KEY(3, 3), 0},
    {SDL_SCANCODE_G,  K_BUTTON, CTL_KEY(3, 4), 0}, {SDL_SCANCODE_H,  K_BUTTON, CTL_KEY(3, 5), 0}, {SDL_SCANCODE_J,  K_BUTTON, CTL_KEY(3, 6), 0}, {SDL_SCANCODE_K,  K_BUTTON, CTL_KEY(3, 7), 0},
    {SDL_SCANCODE_Z,  K_BUTTON, CTL_KEY(4, 0), 0}, {SDL_SCANCODE_X,  K_BUTTON, CTL_KEY(4, 1), 0}, {SDL_SCANCODE_C,  K_BUTTON, CTL_KEY(4, 2), 0}, {SDL_SCANCODE_V,  K_BUTTON, CTL_KEY(4, 3), 0},
    {SDL_SCANCODE_B,  K_BUTTON, CTL_KEY(4, 4), 0}, {SDL_SCANCODE_N,  K_BUTTON, CTL_KEY(4, 5), 0}, {SDL_SCANCODE_M,  K_BUTTON, CTL_KEY(4, 6), 0}, {SDL_SCANCODE_COMMA, K_BUTTON, CTL_KEY(4, 7), 0},
    // left strip
    {SDL_SCANCODE_LEFTBRACKET,  K_TURN,   CTL_ENC_A,    -1}, {SDL_SCANCODE_RIGHTBRACKET, K_TURN, CTL_ENC_A, +1}, {SDL_SCANCODE_BACKSLASH, K_BUTTON, CTL_ENC_A_SW, 0},
    {SDL_SCANCODE_SEMICOLON,    K_TURN,   CTL_ENC_B,    -1}, {SDL_SCANCODE_APOSTROPHE,   K_TURN, CTL_ENC_B, +1}, {SDL_SCANCODE_SLASH,     K_BUTTON, CTL_ENC_B_SW, 0},
    {SDL_SCANCODE_SPACE,        K_BUTTON, CTL_PLAY,      0},
    // right section
    {SDL_SCANCODE_LEFT,  K_AXIS, CTL_JOY_X, 0}, {SDL_SCANCODE_RIGHT, K_AXIS, CTL_JOY_X, INPUT_VALUE_MAX},
    {SDL_SCANCODE_UP,    K_AXIS, CTL_JOY_Y, 0}, {SDL_SCANCODE_DOWN,  K_AXIS, CTL_JOY_Y, INPUT_VALUE_MAX},
    {SDL_SCANCODE_RCTRL, K_BUTTON, CTL_JOY_SW, 0},
    {SDL_SCANCODE_RETURN, K_BUTTON, CTL_BTN_1, 0}, {SDL_SCANCODE_KP_ENTER, K_BUTTON, CTL_BTN_1, 0},
    {SDL_SCANCODE_BACKSPACE, K_BUTTON, CTL_BTN_2, 0}, {SDL_SCANCODE_DELETE, K_BUTTON, CTL_BTN_2, 0},
    {SDL_SCANCODE_LSHIFT, K_BUTTON, CTL_BTN_3, 0},
};
#define KEYMAP_COUNT ((int)(sizeof keymap / sizeof keymap[0]))

/* ---- event queue: one SDL event can make several hardware events ---- */

#define QUEUE_SIZE 128
static input_event_t queue[QUEUE_SIZE];
static int q_head, q_tail;

static void emit(control_id_t ctl, input_kind_t kind, int value) {
    const int next = (q_tail + 1) % QUEUE_SIZE;
    if (next == q_head) return;                        // full: drop
    queue[q_tail] = (input_event_t){ctl, kind, value, false};
    q_tail = next;
    panel_track(ctl, kind, value);
}

static void emit_quit(void) {
    const int next = (q_tail + 1) % QUEUE_SIZE;
    if (next == q_head) return;
    queue[q_tail] = (input_event_t){CTL_NONE, IN_NONE, 0, true};
    q_tail = next;
}

static void key_event(const SDL_KeyboardEvent *k) {
    const bool down = k->type == SDL_KEYDOWN;
    if (down && k->keysym.scancode == SDL_SCANCODE_ESCAPE) { emit_quit(); return; }
    if (down && !k->repeat && k->keysym.scancode == SDL_SCANCODE_F12) { sim_card_toggle(); return; }   // pull the simulated TF card out / put it back
    for (int i = 0; i < KEYMAP_COUNT; i++) {
        const keymap_t *m = &keymap[i];
        if (m->sc != k->keysym.scancode) continue;
        switch (m->role) {
            case K_BUTTON: if (!k->repeat) emit(m->ctl, down ? IN_PRESS : IN_RELEASE, 0); break;
            case K_TURN:   if (down) emit(m->ctl, IN_DELTA, m->value); break;
            case K_AXIS:   if (!k->repeat) emit(m->ctl, IN_VALUE, down ? m->value : INPUT_AXIS_CENTER); break;
        }
    }
}

bool input_key_present(int row, int col) { return row >= 0 && row < KEY_ROWS && col >= 0 && col < 4; }     // future prototype: 4 columns only
bool input_boot_reset(void) { return false; }      // no keys before the window opens: F1 held in the first seconds does it (app.c)

bool input_pending(void) { return q_head != q_tail; }

input_event_t input_poll(void) {
    static bool opened;
    if (!opened) { opened = true; panel_open(); }
    panel_render();

    SDL_Event ev;
    while (q_head == q_tail && SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT: emit_quit(); break;
            case SDL_WINDOWEVENT: if (ev.window.event == SDL_WINDOWEVENT_CLOSE) emit_quit(); break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: key_event(&ev.key); break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:   if (panel_owns_window(ev.button.windowID)) panel_handle_event(&ev, emit); break;
            case SDL_MOUSEMOTION:     if (panel_owns_window(ev.motion.windowID)) panel_handle_event(&ev, emit); break;
            case SDL_MOUSEWHEEL:      if (panel_owns_window(ev.wheel.windowID))  panel_handle_event(&ev, emit); break;
            default: break;
        }
    }
    if (q_head == q_tail) {
        SDL_Delay(1);                                  // idle: do not spin a core
        return (input_event_t){CTL_NONE, IN_NONE, 0, false};
    }
    const input_event_t e = queue[q_head];
    q_head = (q_head + 1) % QUEUE_SIZE;
    return e;
}
#endif // PLATFORM_SIM
