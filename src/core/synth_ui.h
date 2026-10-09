#pragma once
// Synth editor screen: pages of parameters (selected with Up/Down, changed with
// Left/Right) plus a graph of the current page (waveform / envelope / filter).
#include <stdbool.h>
#include <stdint.h>
#include "u8g2.h"
#include "core/rack.h"
#include "core/seq.h"
#include "core/synth_params.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_UI_MAX_PAGES (RACK_MAX * 4 + 4)

// What the user interface understands. The input layer (core/bindings.c) turns hardware controls into these.
typedef enum {
    UI_NONE,
    UI_UP, UI_DOWN,             // previous / next row (row 0 is the page selector); on a declarative screen: previous / next element
    UI_LEFT, UI_RIGHT,          // change the value of the row; on row 0 change page
    UI_NAV_UP, UI_NAV_DOWN, UI_NAV_LEFT, UI_NAV_RIGHT,   // the joystick: moves the focus (on screens not converted yet: the same as UI_UP..UI_RIGHT)
    UI_VALUE_DEC, UI_VALUE_INC, // encoder B: changes the focused value without a latch (on screens not converted yet: UI_LEFT / UI_RIGHT)
    UI_LATCH,                   // joystick push: latches the focused value so the joystick edits it, or activates a button (not converted yet: UI_SELECT)
    UI_PAGE_PREV, UI_PAGE_NEXT, // change page from any row
    UI_ROW_TOP,                 // jump to row 0
    UI_SELECT,                  // push-to-activate: like UI_RIGHT on a row, nothing on row 0
    UI_MENU,                    // open / close the menu
    UI_BACK,                    // delete the selected module (RACK tab)
    UI_PLAY,                    // start / stop the sequencer
    UI_JUMP_1,                  // 8 jump slot so a key can be used to go directly to a location
    UI_JUMP_2,                  // Shift-jump_key_1 saves the current ui screen as target
    UI_JUMP_3,
    UI_JUMP_4,
    UI_JUMP_5,
    UI_JUMP_6,
    UI_JUMP_7,
    UI_JUMP_8,
} ui_event_t;

// Knobs. Slots 0..3 drive rows 1..4 of the current page, then one per macro (the right-hand knobs play macros 1..3), then the master volume.
#define SYNTH_UI_COL_KNOBS    4
#define SYNTH_UI_MACROS       8
#define SYNTH_UI_KNOB_MACRO   SYNTH_UI_COL_KNOBS
#define SYNTH_UI_KNOB_VOLUME  (SYNTH_UI_COL_KNOBS + SYNTH_UI_MACROS)
#define SYNTH_UI_KNOBS        (SYNTH_UI_KNOB_VOLUME + 1)

typedef enum { MACRO_NONE, MACRO_MODULE, MACRO_GLOBAL, MACRO_SEQ, MACRO_CFG, MACRO_FM } macro_kind_t;
// MODULE: rack slot id + parameter index in slot.v[]; GLOBAL: param_id_t; SEQ: seq_param_id_t; CFG: cfg_param_id_t;
// FM: id = operator 0..5 or DX7_GLOBAL_OP, prm = dx7_value_t / DXG_* (core/dx7.h)
typedef struct { uint8_t kind, id, prm; } macro_t;

// A knob's mapping onto a target (core/curves.h): the knob's travel goes through the curve, then onto Min..Max of the target's range
// (% of it; Min > Max inverts). All zero = the whole range, linear: a cleared struct is the plain mapping. Max = 100 - max_off.
typedef struct { macro_t t; uint8_t min, max_off, curve; } mapping_t;
static inline int mapping_max(const mapping_t *m) { return 100 - m->max_off; }

// A macro: one knob, up to SYNTH_UI_MACRO_DESTS parameters at once, each through its own mapping. Learn adds a destination.
#define SYNTH_UI_MACRO_DESTS 8
typedef struct { mapping_t dest[SYNTH_UI_MACRO_DESTS]; uint8_t n; } macro_def_t;

// A jump slot remembers WHAT it points at, not where it was: page indexes and tab positions change when the rack or the synth type changes.
//   a module page:  mod_id (the module's rack id, stable while it exists; never reused) + mod_type (a guard against a replaced rack) + def (which of its pages)
//   a global page:  mod_id 0 + def (GP_*)
//   a menu tab:     in_rack + tab (tab_t, the kind of tab)
// `at` is derived (synth_ui_jump_resolve, after every page rebuild): the page index now, -1 = not shown in this synth type (the slot waits).
typedef struct {
    bool    valid;
    bool    in_rack;
    uint8_t mod_id, mod_type, def;
    uint8_t tab;
    uint8_t row;
    int16_t at;
} jump_slot_t;

#define SYNTH_UI_JUMP_SLOTS 8

typedef struct {
    int page;           // index into the generated page list
    int row;     // 0 = page selector, 1..n = parameter rows
    int cursor;  // selected sequencer step
    int rack_cur;   // selected rack slot (== rack.count: the empty slot at the end)
    int rack_scroll; // first visible cell of the rack strip
    int rack_type;  // module type that Insert will add
    int rack_row;   // RACK tab (ADR-041): the row whose cell is selected (the menu acts on it)
    int rack_col[RACK_ROWS];    // the selected column of each row (row M: the MIX, its modules, the + cell, the OUT; a branch: its modules, the + cell)
    bool rack_menu; // the push menu of the selected cell is open
    int  fm_op;            // FM editor: selected operator 0..5
    int  fm_pt;            // FM editor: selected envelope point 0..3
    int  cog_page;         // the page whose cog is open (its module's hidden settings shown in place of its rows), -1 = none
    int  ms_lane;          // motion sequencer pages: selected lane 0..3 and step
    int  ms_step;
    int  eg_pt;            // EG page: selected point 0..3
    int  smp_cur;          // SAMPLES tab: highlighted file (catalog index) and the target (0 = a new sampler, k = the k-th sampler of the rack)
    int  smp_tgt;
    int  key_cur;          // KEYS tab: the selected key (row * KEY_COLS + col)
    int  menu_tab;         // 0 = RACK, 1 = GENERAL (tabs of the menu, row 0 switches them)
    bool latched;          // the focused element is latched: the joystick changes its value (declarative screens, see ui_screen.h)
    bool in_rack;          // the rack editor (special page, opened with MENU) is showing
    bool rack_dirty;       // rack edited since the synth was last built
    // Page list generated from the rack (see synth_ui_rebuild_pages)
    uint8_t pg_slot[SYNTH_UI_MAX_PAGES];   // rack slot of the module owning the page, or 255 = global page
    uint8_t pg_def[SYNTH_UI_MAX_PAGES];
    int     page_count;
    macro_def_t macro[SYNTH_UI_MACROS];    // the macros (R1..R3 play 1..3; any knob can play one through a Shift / Mod entry)
    int     macro_cur, macro_dest;         // MACROS tab (scr_macros.c): the macro and the destination shown
    int     curve_cur, curve_pt;           // CURVES tab (scr_curves.c): the user curve (0..7) and its selected point
    int     led_role;                      // LEDS tab (scr_leds.c): the key role shown (led_role_t)
    // The joystick as an XY controller (synth_ui_joy_*): what X (0) and Y (1) drive, through a mapping (t.kind MACRO_NONE: nothing).
    mapping_t joy[2];
    mapping_t joy_prev[2];                 // the bindings before the last new one (a second push on it moves it to the other axis and restores them)
    int8_t  joy_fresh;                     // the axis of the parameter just bound while a push on it moves it (-1: none)
    uint8_t joy_next;                      // the axis a new parameter replaces (the older binding)
    bool    joy_xy;                        // XY mode: the stick drives joy[] instead of navigating
    float   joy_rest[2];                   // XY mode, during a push: the target's value (0..1) when the stick left the centre, -1 = none ...
    float   joy_centre[2];                 // ... the mapping position 0..1 that gives it (the stick's centre) ...
    float   joy_wrote[2];                  // ... and the value the stick set last (a different value on release: changed elsewhere, kept)
    int16_t joy_last[2];                   // last stick position applied (1..255, JOY_POS_CENTRE = centred)
    int     joy_tab;                       // JOY tab (scr_joy.c): the axis shown
    int     knob_key[SYNTH_UI_KNOBS];      // what each knob last drove and where it was set (see knob_new_position)
    int     knob_pos[SYNTH_UI_KNOBS];
    int8_t  knob_catch_dir[SYNTH_UI_COL_KNOBS]; // catch state per col knob: 0 = caught, -1 = turn left to catch, +1 = turn right
    int     knob_cur[SYNTH_UI_COL_KNOBS];       // step of the driven parameter at the last catch refresh; -1 unknown, -2 the knob itself just set it
    int     knob_val[SYNTH_UI_COL_KNOBS];       // last physical position of each col knob (0..INPUT_VALUE_MAX), -1 = not moved yet
    uint8_t knob_away;                          // bit k: col knob k drives a Shift / Mod target now, not its page row (set by the app): that row hides its catch arrow
    // MODIFIERS tab (scr_mods.c): the layer and the control shown; learn_req = a Learn button was pressed (the app closes the menu and sets
    // learn_wait: the next push on a page row assigns that row's parameter to learn_layer + learn_ctl, or, with learn_macro >= 0, adds it to
    // that macro as a destination: the MACROS tab's Learn)
    int     mods_layer, mods_ctl;
    bool    learn_req, learn_wait;
    uint8_t learn_layer, learn_ctl;
    int8_t  learn_macro;
    bool rebuild;          // set when the RACK page is left with edits: app must audio_build()
    int run_anim;   // gui animation id shown while the sequencer runs (owned by app.c)
    jump_slot_t jump[SYNTH_UI_JUMP_SLOTS]; // rapid-navigation slots: save with Shift+key, recall with key
} synth_ui_t;

void synth_ui_init(synth_ui_t *ui, const rack_t *rack);

// Regenerates the pages from the rack: each module contributes its own pages in rack order,
// followed by the global ones (amp envelope, sequencer). Called by init and when the rack
// editor closes with changes.
void synth_ui_rebuild_pages(synth_ui_t *ui, const rack_t *rack);

// Applies a UI event. Returns true if a synth parameter changed.
bool synth_ui_handle(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, ui_event_t e);

// Knobs (value 0..INPUT_VALUE_MAX = the whole range of the parameter). All return true when a sound value changed.
bool synth_ui_knob_row(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int row, int value);   // row 1..4 of the current page
// Parameter targets through a mapping (mapping_t: what Shift / Mod + a knob drives, core/modifiers.h; Min / Max / curve: core/curves.h).
// A target whose module is gone does nothing (false).
//   knob_row_target  col knob row 1..4, with the catch (in knob positions: ui->knob_catch_dir[row - 1] says the way to turn)
//   knob_target      another absolute knob (`knob` = SYNTH_UI_KNOB_MACRO + k or SYNTH_UI_KNOB_VOLUME), no catch
//   target_step      an encoder: n steps of the parameter (the mapping is not used)
bool synth_ui_knob_row_target(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int row, const mapping_t *m, int value);
bool synth_ui_knob_target(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int knob, const mapping_t *m, int value);
bool synth_ui_target_step(synth_params_t *params, seq_t *seq, rack_t *rack, const macro_t *m, int n);
// The target's value as 0..1 of its range (the macro / mapping pictures); -1 when it has none.
float synth_ui_target_norm(synth_params_t *params, seq_t *seq, rack_t *rack, const macro_t *m);
// The target's name ("FL1 Cut", "Vol") and value text (value NULL: the name only, params / seq unused); false when it has none
// (MACRO_NONE, or its module was deleted).
bool synth_ui_target_describe(const synth_params_t *params, const seq_t *seq, const rack_t *rack, const macro_t *m, char *name, int nn, char *value, int nv);
// The parameter under the cursor as a target (a page row a knob may drive, or a GENERAL tab setting that changes live); false when none.
bool synth_ui_target_at_cursor(const synth_ui_t *ui, const rack_t *rack, macro_t *out);
// Recomputes whether each col knob is in sync with what it drives now. Call after a manual change (page, row, a modifier, a value edited by
// hand), not after a knob move. `tgt`: while a modifier is held, what the 4 col knobs drive (t.kind MACRO_NONE: nothing, MACRO_PAGE_ROW: their
// page row; a macro is given as its first destination); NULL: all drive their page rows.
#define MACRO_PAGE_ROW 0xFF
void synth_ui_catch_refresh(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, const mapping_t *tgt);
bool synth_ui_set_volume(synth_ui_t *ui, rack_t *rack, int value);                                              // master volume
// Macros. macro: knob `knob` (a slot as for knob_target) plays macro k (every destination, no catch); macro_row: col knob row 1..4 plays it
// with the catch on its first destination; macro_step: an encoder, n steps of every destination.
bool synth_ui_macro(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int knob, int k, int value);
bool synth_ui_macro_row(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int row, int k, int value);
bool synth_ui_macro_step(synth_params_t *params, seq_t *seq, rack_t *rack, const macro_def_t *m, int n);
// learn: adds the parameter under the cursor to macro k (full range, linear); false when there is none, it is already there or the macro is full.
bool synth_ui_macro_learn(synth_ui_t *ui, const rack_t *rack, int k);
bool synth_ui_macro_add(synth_ui_t *ui, int k, const macro_t *t);             // the same for a given target
void synth_ui_macro_remove(synth_ui_t *ui, int k, int dest);
void synth_ui_macro_describe(const synth_ui_t *ui, const rack_t *rack, int k, char *out, int n);   // "M1 > FL1 Cut +2"
void synth_ui_macros_prune(synth_ui_t *ui, const rack_t *rack);               // drops destinations whose module was deleted

// The joystick as an XY controller (user design, 2026-10-07). On a page, a push on a parameter row binds it (joy_click):
//   a new parameter     replaces the older binding (X, then Y, then X ...)                    JOY_CLICK_BOUND
//   the same, again     moves it to the other axis and brings back what it replaced            JOY_CLICK_MOVED   (again: back)
//   a bound one         (not the one just bound) inverts its axis (Min <-> Max)               JOY_CLICK_INVERTED
//   not a parameter     JOY_CLICK_NONE (the push keeps its old meaning)
// *axis = the axis concerned. joy_mode: XY mode on / off (the app: Shift + push, ACT_JOY_MODE). joy_axis: the stick's raw axis value
// (0..INPUT_VALUE_MAX) moves the parameter from the value it has when the stick leaves the centre (so a value set by a knob, an encoder or the
// navigation is followed) towards the mapping's ends; back in the centre it is that value again (unless it was changed elsewhere during the
// push: kept). Returns true when a sound value changed. Ranges / curves: the JOY menu tab.
#define JOY_XY_DEADZONE 48
#define JOY_POS_CENTRE  128
typedef enum { JOY_CLICK_NONE, JOY_CLICK_BOUND, JOY_CLICK_MOVED, JOY_CLICK_INVERTED } joy_click_t;
joy_click_t synth_ui_joy_click(synth_ui_t *ui, const rack_t *rack, int *axis);
void synth_ui_joy_mode(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, bool on);
bool synth_ui_joy_axis(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int axis, int value);

// Jump slots (see jump_slot_t). save: the page / tab on screen goes into slot k. resolve: finds every slot's page again (called by
// synth_ui_rebuild_pages; a slot whose module was deleted is cleared). ready: the slot can be jumped to now (its LED is bright).
void synth_ui_jump_save(synth_ui_t *ui, const rack_t *rack, int k);
void synth_ui_jump_resolve(synth_ui_t *ui, const rack_t *rack);
bool synth_ui_jump_ready(const synth_ui_t *ui, const rack_t *rack, int k);

// True while the KEYS tab of the menu is on screen (a matrix key then selects itself in the list).
bool synth_ui_on_keys_tab(const synth_ui_t *ui, const rack_t *rack);
// Closes the menu (as MENU does) and shows the page of `slot` (a rack slot, or GLOBAL_PAGE) with page def `def`, row 1. False when
// no such page is shown (the main view then stays on its page).
bool synth_ui_open_page(synth_ui_t *ui, const rack_t *rack, int slot, int def);
// True while the MODIFIERS tab is on screen (a key or a control then selects itself; see scr_mods.c).
bool synth_ui_on_mods_tab(const synth_ui_t *ui, const rack_t *rack);
// True while the CURVES tab is on screen (the knobs CURVES_KNOB_X / _Y then move the selected point; see scr_curves.c).
bool synth_ui_on_curves_tab(const synth_ui_t *ui, const rack_t *rack);
// True while the LEDS tab is on screen (only the keys of the role shown light, the others are off; see key_leds.c).
bool synth_ui_on_leds_tab(const synth_ui_t *ui, const rack_t *rack);

// True while the sequencer page is on screen (it needs redrawing on every step).
bool synth_ui_shows_seq(const synth_ui_t *ui);
// True while a page with a playhead (sequencer, motion steps) is on screen.
bool synth_ui_shows_playhead(const synth_ui_t *ui, const rack_t *rack);

// Draws the screen into the display buffer. Does not send it: the app draws its popups on top, then sends (display_send).
void synth_ui_draw(const synth_ui_t *ui, const synth_params_t *params, const seq_t *seq, const rack_t *rack, u8g2_t *g);

#ifdef __cplusplus
}
#endif
