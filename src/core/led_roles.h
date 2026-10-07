#pragma once
// Key LED colours by role (core/key_leds.c paints them, the LEDS menu tab edits them: scr_leds.c). Every key gets a role from its function in
// the key layout (keymap.h); a role has an Idle and an Active colour, each one of LED_PALETTE named colours at a brightness 0..100 %.
// Active: a note held (Note / Sharp / Root), Shift or Mod held, the menu open, the sequencer running, the keyboard shifted that way (Octave),
// a jump slot saved (Jump). Back, Nav and None have no active state. The defaults are the colours of the first version (play feedback).
// Saved in ui.cfg (core/ui_settings.c): "leds" then "led <role> <colour> <pct> <colour> <pct>" for the roles that differ from the defaults.
// The LED driver applies the global brightness cap on top (HW_LED_MAX_BRIGHTNESS on the prototype).
#include <stdbool.h>
#include <stdint.h>
#include "core/keymap.h"
#include "hal/hal_leds.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LR_NOTE, LR_SHARP, LR_ROOT, LR_SHIFT, LR_MOD, LR_MENU, LR_BACK, LR_PLAY, LR_OCTAVE, LR_JUMP, LR_NAV, LR_NONE, LR_COUNT } led_role_t;
typedef struct { uint8_t color, pct; } led_shade_t;                 // a palette index and a brightness 0..100
typedef struct { led_shade_t idle, active; } led_role_def_t;

#define LED_PALETTE 16

void                  led_roles_init(void);                         // the defaults
const led_role_def_t *led_role(int r);
void                  led_role_set(int r, led_role_def_t d);
const char           *led_role_name(int r);                         // "Note", "Sharp", "Root", "Shift" ...
bool                  led_role_has_active(int r);
int                   led_role_of(key_fn_t f);                       // the role of a key with this function
led_color_t           led_role_rgb(int r, bool active);
const char           *led_palette_name(int c);                      // "Off", "White", "Red" ...
led_color_t           led_shade_rgb(led_shade_t s);
unsigned              led_roles_rev(void);                          // changes on every edit

int  led_roles_to_text(char *buf, int cap);                         // "leds" + the roles that differ from the defaults
void led_roles_from_text_begin(void);                               // back to the defaults (the file lists the differences)
bool led_roles_from_line(const char *line);                         // true when it was a "led" line

#ifdef __cplusplus
}
#endif
