#pragma once
// What every key of the matrix does (the keys only: knobs, encoders, the joystick and the buttons keep the fixed table of bindings.c).
//
// A LAYOUT gives each key one function: a note (semitones above the keyboard's base note, the octave is added) or a control action
// (Shift, Menu, Back, Play, Octave, the navigation moves). Three layouts are built in (in flash, never changed); the fourth, User, is
// the user's own: editing a key of a built-in layout copies it into User first. The chosen layout and the User keys are kept in
// keys.cfg at the root of the TF card (a text file, readable and editable on a PC), saved when the menu closes.
//
// Edited in the KEYS tab of the menu (scr_keys.c). Holding the top-left function key during the first seconds after power-on resets
// everything to the first built-in layout (app.c), so a layout without a Menu key can always be undone.
#include <stdbool.h>
#include <stdint.h>
#include "core/bindings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t act; int8_t arg; } key_fn_t;      // action_id_t + its argument (ACT_NOTE: semitones 0..KEYMAP_NOTE_MAX)

enum { KEYMAP_USER = 3, KEYMAP_LAYOUTS = 4 };              // layouts 0..2 are built in, 3 is the user's
#define KEYMAP_NOTE_MAX 47                                  // a key can play base note + 0..47 (four octaves)
#define KEYMAP_FILE "keys.cfg"
#define KEYMAP_RESET_KEY        CTL_KEY(0, 0)               // held at power-on: back to the first built-in layout
#define KEYMAP_RESET_WINDOW_MS  4000                        // ... when pressed this soon after start (the board's boot delay is not counted)
#define KEYMAP_RESET_HOLD_MS    2000                        // ... and held this long

void        keymap_init(void);                              // first built-in layout, User = a copy of it, nothing to save
int         keymap_layout(void);
const char *keymap_layout_name(int layout);
void        keymap_select(int layout);
key_fn_t    keymap_get(int key);                            // key = row * KEY_COLS + col, in the active layout
void        keymap_set(int key, key_fn_t f);                // switches to User (copying the active layout into it first)
void        keymap_reset(void);                             // first built-in layout, User = a copy of it, saved at the next keymap_save()
bool        keymap_dirty(void);                             // changed since the last load / save

// The card. keymap_load() replaces the state with keys.cfg (false: no card or no file, the state is kept);
// keymap_save() writes it when it changed (true: written or nothing to do).
bool keymap_load(void);
bool keymap_save(void);

// The functions a key can take, in the order the KEYS tab steps through them: the controls, then the notes.
int      keymap_fn_count(void);
key_fn_t keymap_fn_at(int i);
int      keymap_fn_index(key_fn_t f);                       // 0 (None) for a function not in the list
void     keymap_fn_name(key_fn_t f, char *out, int n);      // "Shift", "Oct +", "D#5"

// The text form of keys.cfg (exposed for the tests). keymap_from_text() returns false and keeps the state when the text is not a key file.
int  keymap_to_text(char *buf, int cap);
bool keymap_from_text(const char *txt);

#ifdef __cplusplus
}
#endif
