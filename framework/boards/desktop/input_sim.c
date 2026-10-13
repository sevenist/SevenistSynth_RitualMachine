#ifdef PLATFORM_SIM
#include "hal/hal_input.h"
#include "boards/desktop/sim_board.h"
#include "boards/desktop/panel_sim.h"
#include "boards/desktop/sim_card.h"
#include "SDL.h"
#include <stdio.h>

// The simulator's input: the keyboard and the mouse (on the panel window, see panel_sim.c) both act as the controls of the board the
// program was built for (sim_board.h) and report them as hardware events (see hal/hal_input.h). What a control DOES is the app's
// business.
//
// The PC keys are given to a board's parts by their kind and their place, not by the board (build_keys below; the panel window writes
// each part's keys on it). Keys are named by PHYSICAL position (SDL scancodes, QWERTY names), so on an AZERTY keyboard "Q" is the key
// labelled A, "A" the key labelled Q, "Z" is W, "W" is Z, "M" is ",", "[" is "^", "]" is "$" and ";" is "M":
//
//   matrix keys   F1 F2 F3 F4 F5 F6 F7 F8     row 0 (the function row)       a key takes the PC key of its row and column:
//                 1  2  3  4  5  6  7  8      \  rows 1..4; the bottom row    a board with 4 columns stops at F4 / 4 / R / F / V
//                 Q  W  E  R  T  Y  U  I       > holds the lowest notes
//                 A  S  D  F  G  H  J  K      /
//                 Z  X  C  V  B  N  M  ,
//   what turns    encoders and endless pots, a pair of keys each: [ ]   ; '   - =   9 0   O P   L .   keypad - +
//                 (the two parts of SIM_TURN_KEYS_FIRST first, then the table's order); the push of the first two: \ and /
//   buttons       in the table's order: Space, Enter, Backspace / Delete, Left Shift
//   joystick      arrows, push = Right Ctrl           Esc / close a window = quit       F12 = the card out / in
//   knobs         mouse only (panel window): wheel or drag
//
// An endless pot reports steps, or a position when the app asked for one (input_control_mode): a key press, a wheel notch or a few
// pixels of drag are one step, and a step moves the position by POT_STEP. The real board's noise thresholds are not simulated.

typedef enum {
    K_BUTTON,       // key down = IN_PRESS of `ctl`, key up = IN_RELEASE
    K_TURN,         // every key down (also auto-repeat) turns `ctl` by `value` steps
    K_AXIS,         // key down = the axis `ctl` goes to `value`, key up = back to the centre
} key_role_t;

typedef struct { SDL_Scancode sc; key_role_t role; int ctl; int value; } keymap_t;

#define KEYMAP_MAX (2 * SIM_CONTROL_COUNT + 8)
static keymap_t keymap[KEYMAP_MAX];
static int  keymap_count;
static bool keys_built;
static char hints[SIM_CONTROL_COUNT][12];           // each control's PC keys as text
static uint8_t pot_reading[SIM_CONTROL_COUNT];      // an endless pot: IN_VALUE when it reports a position
static int     pot_pos[SIM_CONTROL_COUNT];
#define POT_STEP 24

static void map_key(SDL_Scancode sc, key_role_t role, int ctl, int value) {
    if (ctl >= 0 && ctl < SIM_CONTROL_COUNT && sc != SDL_SCANCODE_UNKNOWN && keymap_count < KEYMAP_MAX) keymap[keymap_count++] = (keymap_t){sc, role, ctl, value};
}
static void hint(int ctl, const char *keys) { if (ctl >= 0 && ctl < SIM_CONTROL_COUNT) snprintf(hints[ctl], sizeof hints[ctl], "%s", keys); }

static void build_keys(void) {
    static const struct { SDL_Scancode sc; const char *name; } grid[5][8] = {
        {{SDL_SCANCODE_F1, "F1"}, {SDL_SCANCODE_F2, "F2"}, {SDL_SCANCODE_F3, "F3"}, {SDL_SCANCODE_F4, "F4"}, {SDL_SCANCODE_F5, "F5"}, {SDL_SCANCODE_F6, "F6"}, {SDL_SCANCODE_F7, "F7"}, {SDL_SCANCODE_F8, "F8"}},
        {{SDL_SCANCODE_1, "1"}, {SDL_SCANCODE_2, "2"}, {SDL_SCANCODE_3, "3"}, {SDL_SCANCODE_4, "4"}, {SDL_SCANCODE_5, "5"}, {SDL_SCANCODE_6, "6"}, {SDL_SCANCODE_7, "7"}, {SDL_SCANCODE_8, "8"}},
        {{SDL_SCANCODE_Q, "Q"}, {SDL_SCANCODE_W, "W"}, {SDL_SCANCODE_E, "E"}, {SDL_SCANCODE_R, "R"}, {SDL_SCANCODE_T, "T"}, {SDL_SCANCODE_Y, "Y"}, {SDL_SCANCODE_U, "U"}, {SDL_SCANCODE_I, "I"}},
        {{SDL_SCANCODE_A, "A"}, {SDL_SCANCODE_S, "S"}, {SDL_SCANCODE_D, "D"}, {SDL_SCANCODE_F, "F"}, {SDL_SCANCODE_G, "G"}, {SDL_SCANCODE_H, "H"}, {SDL_SCANCODE_J, "J"}, {SDL_SCANCODE_K, "K"}},
        {{SDL_SCANCODE_Z, "Z"}, {SDL_SCANCODE_X, "X"}, {SDL_SCANCODE_C, "C"}, {SDL_SCANCODE_V, "V"}, {SDL_SCANCODE_B, "B"}, {SDL_SCANCODE_N, "N"}, {SDL_SCANCODE_M, "M"}, {SDL_SCANCODE_COMMA, ","}},
    };
    static const struct { SDL_Scancode less, more; const char *name; } turn[] = {
        {SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET, "[ ]"}, {SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE, "; '"},
        {SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, "- ="}, {SDL_SCANCODE_9, SDL_SCANCODE_0, "9 0"}, {SDL_SCANCODE_O, SDL_SCANCODE_P, "O P"},
        {SDL_SCANCODE_L, SDL_SCANCODE_PERIOD, "L ."}, {SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_PLUS, "KP- KP+"},
    };
    static const struct { SDL_Scancode sc; const char *name; } push[] = {{SDL_SCANCODE_BACKSLASH, "\\"}, {SDL_SCANCODE_SLASH, "/"}};
    static const struct { SDL_Scancode sc, also; const char *name; } button[] = {
        {SDL_SCANCODE_SPACE, SDL_SCANCODE_UNKNOWN, "SPACE"}, {SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, "ENTER"},
        {SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_DELETE, "BKSP"}, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_UNKNOWN, "L SHIFT"},
    };
    static const int turn_first[] = {SIM_TURN_KEYS_FIRST};
    enum { TURNS = sizeof turn / sizeof turn[0], PUSHES = sizeof push / sizeof push[0], BUTTONS = sizeof button / sizeof button[0], FIRST = sizeof turn_first / sizeof turn_first[0] };

    const sim_panel_t *panel = panel_layout();
    int turning[SIM_PARTS_MAX], n_turning = 0, n_buttons = 0;
    bool stick = false;
    for (int f = 0; f < FIRST; f++)                                  // what turns: the parts named first, then the others as the table has them
        for (int i = 0; i < panel->count; i++)
            if ((panel->parts[i].kind == PART_ENCODER || panel->parts[i].kind == PART_ENDLESS) && panel->parts[i].ctl == turn_first[f]) turning[n_turning++] = i;
    for (int i = 0; i < panel->count; i++) {
        const sim_part_t *p = &panel->parts[i];
        if (p->kind != PART_ENCODER && p->kind != PART_ENDLESS) continue;
        bool named = false;
        for (int f = 0; f < FIRST; f++) if (p->ctl == turn_first[f]) named = true;
        if (!named) turning[n_turning++] = i;
    }
    for (int k = 0; k < n_turning && k < TURNS; k++) {
        const sim_part_t *p = &panel->parts[turning[k]];
        map_key(turn[k].less, K_TURN, p->ctl, -1);
        map_key(turn[k].more, K_TURN, p->ctl, +1);
        hint(p->ctl, turn[k].name);
        if (k < PUSHES && p->sw != INPUT_NO_CONTROL) { map_key(push[k].sc, K_BUTTON, p->sw, 0); hint(p->sw, push[k].name); }
    }
    for (int i = 0; i < panel->count; i++) {
        const sim_part_t *p = &panel->parts[i];
        const input_control_t *c = &SIM_CONTROLS[p->ctl];
        if (p->kind == PART_KEY && c->row < 5 && c->col < 8) {
            map_key(grid[c->row][c->col].sc, K_BUTTON, p->ctl, 0);
            hint(p->ctl, grid[c->row][c->col].name);
        } else if (p->kind == PART_BUTTON && n_buttons < BUTTONS) {
            map_key(button[n_buttons].sc, K_BUTTON, p->ctl, 0);
            map_key(button[n_buttons].also, K_BUTTON, p->ctl, 0);
            hint(p->ctl, button[n_buttons++].name);
        } else if (p->kind == PART_JOYSTICK && !stick) {             // the arrows work one joystick: the first
            stick = true;
            map_key(SDL_SCANCODE_LEFT, K_AXIS, p->ctl, 0);  map_key(SDL_SCANCODE_RIGHT, K_AXIS, p->ctl, INPUT_VALUE_MAX);
            map_key(SDL_SCANCODE_UP, K_AXIS, p->ctl2, 0);   map_key(SDL_SCANCODE_DOWN, K_AXIS, p->ctl2, INPUT_VALUE_MAX);
            map_key(SDL_SCANCODE_RCTRL, K_BUTTON, p->sw, 0);
            hint(p->ctl, "ARROWS");
            hint(p->sw, "R CTRL");
        }
    }
}

static void need_keys(void) { if (!keys_built) { keys_built = true; build_keys(); } }

const char *input_sim_key_hint(int ctl) {
    need_keys();
    return ctl >= 0 && ctl < SIM_CONTROL_COUNT ? hints[ctl] : "";
}

/* ---- event queue: one SDL event can make several hardware events ---- */

#define QUEUE_SIZE 128
static input_event_t queue[QUEUE_SIZE];
static int q_head, q_tail;

static void emit(int ctl, input_kind_t kind, int value) {
    if (ctl < 0 || ctl >= SIM_CONTROL_COUNT) return;
    if (kind == IN_DELTA && pot_reading[ctl] == IN_VALUE) {       // an endless pot read as a position: the steps move it, it stops at both ends
        int pos = pot_pos[ctl] + value * POT_STEP;
        pos = pos < 0 ? 0 : pos > INPUT_VALUE_MAX ? INPUT_VALUE_MAX : pos;
        if (pos == pot_pos[ctl]) return;
        pot_pos[ctl] = pos;
        kind = IN_VALUE; value = pos;
    }
    const int next = (q_tail + 1) % QUEUE_SIZE;
    if (next == q_head) return;                        // full: drop
    queue[q_tail] = (input_event_t){ctl, kind, value, false};
    q_tail = next;
    panel_track(ctl, kind, value);
}

static void emit_quit(void) {
    const int next = (q_tail + 1) % QUEUE_SIZE;
    if (next == q_head) return;
    queue[q_tail] = (input_event_t){INPUT_NO_CONTROL, IN_NONE, 0, true};
    q_tail = next;
}

static void key_event(const SDL_KeyboardEvent *k) {
    const bool down = k->type == SDL_KEYDOWN;
    if (down && k->keysym.scancode == SDL_SCANCODE_ESCAPE) { emit_quit(); return; }
    if (down && !k->repeat && k->keysym.scancode == SDL_SCANCODE_F12) { sim_card_toggle(); return; }   // pull the simulated TF card out / put it back
    need_keys();
    for (int i = 0; i < keymap_count; i++) {
        const keymap_t *m = &keymap[i];
        if (m->sc != k->keysym.scancode) continue;
        switch (m->role) {
            case K_BUTTON: if (!k->repeat) emit(m->ctl, down ? IN_PRESS : IN_RELEASE, 0); break;
            case K_TURN:   if (down) emit(m->ctl, IN_DELTA, m->value); break;
            case K_AXIS:   if (!k->repeat) emit(m->ctl, IN_VALUE, down ? m->value : INPUT_AXIS_CENTER); break;
        }
    }
}

const input_control_t *input_controls(int *count) { if (count) *count = SIM_CONTROL_COUNT; return SIM_CONTROLS; }
const char *input_board_name(void) { return SIM_BOARD_NAME; }

void input_control_mode(int ctl, input_kind_t reading, int start) {
    if (ctl < 0 || ctl >= SIM_CONTROL_COUNT || SIM_CONTROLS[ctl].kind != CK_ENDLESS) return;
    pot_reading[ctl] = (uint8_t)reading;
    pot_pos[ctl] = start < 0 ? 0 : start > INPUT_VALUE_MAX ? INPUT_VALUE_MAX : start;
    if (reading == IN_VALUE) panel_track(ctl, IN_VALUE, pot_pos[ctl]);      // the drawn pot starts there too
}

bool input_boot_reset(void) { return false; }      // no keys before the window opens: F1 held in the first seconds does it (the Ritual Machine: app/app_input.c)

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
        return (input_event_t){INPUT_NO_CONTROL, IN_NONE, 0, false};
    }
    const input_event_t e = queue[q_head];
    q_head = (q_head + 1) % QUEUE_SIZE;
    return e;
}
#endif // PLATFORM_SIM
