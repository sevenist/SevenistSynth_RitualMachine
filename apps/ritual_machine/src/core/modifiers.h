#pragma once
// The modifier layers: what a control does while Shift or Mod is held (edited in the MODIFIERS menu tab, scr_mods.c).
//
// Every control (keys included) has one entry per layer:
//   Default  nothing set: the control does what it does without a modifier, except a column knob, which drives nothing (a popup
//            says "No target"), and a jump key, which saves its slot. The same for Shift and Mod. What Shift did before the layers
//            existed (EncA pages, EncB x4, joystick octave / pages, R knobs learn, K1 speaker, K2 volume) are the Shift layer's
//            starting entries (modifiers_init), shown and editable in the tab; on a board without R knobs Shift + K3 / K4 = macro 1 / 2
//   Macro    knobs and encoders: "Macro 1..8" plays a macro (ui->macro[], learned with "Learn M1..8"), so any board can reach them
//   Action   an action + argument (keys, buttons, the joystick: the key functions; knobs and encoders: steps of rows, pages, values ...)
//   Param    knobs and encoders only: a parameter (macro_t: a module's parameter by module id, a global or sequencer parameter, a GENERAL
//            setting). Assigned by "learn": the chord Shift + Mod + turning the knob, or the Learn button of the tab
// When Shift and Mod are both held the Mod layer is used (and Shift + Mod + a knob is the learn chord).
// Shift and Mod themselves are key functions (core/keymap.h); Mod is not on any built-in layout.
// The layers are the same for every key layout. They are saved in ui.cfg (core/ui_settings.c) with modifiers_to_text().
#include <stdbool.h>
#include <stdint.h>
#include "core/bindings.h"
#include "core/synth_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MODL_SHIFT, MODL_MOD, MODL_COUNT };
typedef enum { ME_DEFAULT, ME_ACTION, ME_PARAM } mod_entry_kind_t;

typedef struct {
    uint8_t kind;           // mod_entry_kind_t
    uint8_t act;            // ME_ACTION: action_id_t
    int8_t  arg;
    macro_t m;              // ME_PARAM: the target ...
    uint8_t min, max_off, curve;   // ... and its mapping (mapping_t: zero = full range, linear)
} mod_entry_t;

static inline mapping_t modifiers_mapping(const mod_entry_t *e) { return (mapping_t){e->m, e->min, e->max_off, e->curve}; }

void        modifiers_init(void);                           // the defaults (the Shift layer's starting entries, see above)
mod_entry_t modifiers_get(int layer, control_id_t c);
void        modifiers_set(int layer, control_id_t c, mod_entry_t e);
void        modifiers_reset(void);                          // back to the defaults
unsigned    modifiers_rev(void);                            // changes on every edit (the settings file compares it)
void        modifiers_prune(const rack_t *rack);            // clears Param entries whose module is no longer in the rack
const char *modifiers_layer_name(int layer);                // "Shift", "Mod"

// The controls the tab lists, in order (only those the board has); knob = a knob or encoder (can take a Param).
int          modifiers_ctl_count(void);
control_id_t modifiers_ctl_at(int i);
int          modifiers_ctl_index(control_id_t c);           // -1 when not listed
bool         modifiers_ctl_is_knob(control_id_t c);
void         modifiers_ctl_name(control_id_t c, char *out, int n);   // "K1", "EncA", "F1", "A3", "Joy U"

// The functions an entry can take for a control, in the order the tab steps through them: Default, None (an action that does nothing),
// then the actions (keys: the key functions without Shift / Mod; knobs: the live GENERAL settings Vol Spk Out Glide Legato Patch Knob, then
// steps). Any other parameter is not in the list (learn sets it; modifiers_fn_index() returns -1 for it).
int         modifiers_fn_count(control_id_t c);
mod_entry_t modifiers_fn_at(control_id_t c, int i);
int         modifiers_fn_index(control_id_t c, mod_entry_t e);       // -1 for a Param entry
// "Default", "-", "Page +", "Rows", "FL1 Cut" (a Param needs the rack for its name)
void        modifiers_entry_name(mod_entry_t e, const rack_t *rack, char *out, int n);

// ui.cfg lines ("shift k1 param cfg vol", "mod a3 act page+", "shift enca act default").
// Only the entries that differ from the defaults are written. from_line returns true when the line was a modifier line (it then replaces
// that entry); modifiers_from_text_begin() puts every entry back to its default first.
int  modifiers_to_text(char *buf, int cap);
void modifiers_from_text_begin(void);
bool modifiers_from_line(const char *line);

// A target and its mapping as file words, shared with the macros of ui.cfg: "mod 3 12 range 10 90 curve exp" (range / curve only when not
// the plain mapping). parse reads them from the start of `s`; false when they are not a target.
int  mapping_to_text(const mapping_t *m, char *out, int n);
bool mapping_parse(const char *line, mapping_t *out);

#ifdef __cplusplus
}
#endif
