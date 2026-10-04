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
    UI_PLAY                     // start / stop the sequencer
} ui_event_t;

// Knobs. Slots 0..3 drive rows 1..4 of the current page, 4..6 are the macros (right-hand knobs), 7 is the master volume.
#define SYNTH_UI_COL_KNOBS    4
#define SYNTH_UI_MACROS       3
#define SYNTH_UI_KNOB_MACRO   SYNTH_UI_COL_KNOBS
#define SYNTH_UI_KNOB_VOLUME  (SYNTH_UI_COL_KNOBS + SYNTH_UI_MACROS)
#define SYNTH_UI_KNOBS        (SYNTH_UI_KNOB_VOLUME + 1)

typedef enum { MACRO_NONE, MACRO_MODULE, MACRO_GLOBAL, MACRO_SEQ } macro_kind_t;
typedef struct { uint8_t kind, id, prm; } macro_t;      // MODULE: rack slot id + parameter index in slot.v[]; GLOBAL: param_id_t; SEQ: seq_param_id_t

typedef struct {
    int page;           // index into the generated page list
    int row;     // 0 = page selector, 1..n = parameter rows
    int cursor;  // selected sequencer step
    int rack_cur;   // selected rack slot (== rack.count: the empty slot at the end)
    int rack_scroll; // first visible cell of the rack strip
    int rack_type;  // module type that Insert will add
    int  fm_op;            // FM editor: selected operator 0..5
    int  fm_pt;            // FM editor: selected envelope point 0..3
    int  ms_lane;          // motion sequencer pages: selected lane 0..3 and step
    int  ms_step;
    int  fx_slot;          // FX RACK tab: selected slot 0..3
    int  eg_pt;            // EG page: selected point 0..3
    int  smp_cur;          // SAMPLES tab: highlighted file (catalog index) and the target (0 = a new sampler, k = the k-th sampler of the rack)
    int  smp_tgt;
    int  menu_tab;         // 0 = RACK, 1 = GENERAL (tabs of the menu, row 0 switches them)
    bool latched;          // the focused element is latched: the joystick changes its value (declarative screens, see ui_screen.h)
    bool in_rack;          // the rack editor (special page, opened with MENU) is showing
    bool rack_dirty;       // rack edited since the synth was last built
    // Page list generated from the rack (see synth_ui_rebuild_pages)
    uint8_t pg_slot[SYNTH_UI_MAX_PAGES];   // rack slot of the module owning the page, or 255 = global page
    uint8_t pg_def[SYNTH_UI_MAX_PAGES];
    int     page_count;
    macro_t macro[SYNTH_UI_MACROS];        // what the right-hand knobs drive
    int     knob_key[SYNTH_UI_KNOBS];      // what each knob last drove and where it was set (see knob_new_position)
    int     knob_pos[SYNTH_UI_KNOBS];
    bool rebuild;          // set when the RACK page is left with edits: app must audio_build()
    int run_anim;   // gui animation id shown while the sequencer runs (owned by app.c)
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
bool synth_ui_set_volume(synth_ui_t *ui, rack_t *rack, int value);                                              // master volume
bool synth_ui_macro(synth_ui_t *ui, synth_params_t *params, seq_t *seq, rack_t *rack, int k, int value);       // macro k = 0..2
bool synth_ui_macro_learn(synth_ui_t *ui, const rack_t *rack, int k);        // macro k takes the parameter under the cursor
void synth_ui_macro_describe(const synth_ui_t *ui, const rack_t *rack, int k, char *out, int n);   // "R1 > FL1 Cut"

// True while the sequencer page is on screen (it needs redrawing on every step).
bool synth_ui_shows_seq(const synth_ui_t *ui);
// True while a page with a playhead (sequencer, motion steps) is on screen.
bool synth_ui_shows_playhead(const synth_ui_t *ui, const rack_t *rack);

void synth_ui_draw(const synth_ui_t *ui, const synth_params_t *params, const seq_t *seq, const rack_t *rack, u8g2_t *g);

#ifdef __cplusplus
}
#endif
