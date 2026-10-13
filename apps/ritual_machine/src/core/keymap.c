#include "core/keymap.h"
#include "core/seq.h"
#include "core/card_layout.h"
#include <stdio.h>
#include <string.h>

/* ---------------- the functions a key can take ---------------- */

typedef struct { uint8_t act; int8_t arg; const char *name; const char *tok; } fn_def_t;     // tok: the word in keys.cfg

static const fn_def_t fns[] = {
    {ACT_NONE,         0,         "-",      "none"},
    {ACT_SHIFT,        0,         "Shift",  "shift"},
    {ACT_MENU,         0,         "Menu",   "menu"},
    {ACT_BACK,         0,         "Back",   "back"},
    {ACT_PLAY,         0,         "Play",   "play"},
    {ACT_OCTAVE,       -1,        "Oct -",  "oct-"},
    {ACT_OCTAVE,       +1,        "Oct +",  "oct+"},
    {ACT_NAV,          NAV_UP,    "Up",     "up"},
    {ACT_NAV,          NAV_DOWN,  "Down",   "down"},
    {ACT_NAV,          NAV_LEFT,  "Left",   "left"},
    {ACT_NAV,          NAV_RIGHT, "Right",  "right"},
    {ACT_LATCH,        0,         "Latch",  "latch"},
    {ACT_SELECT,       0,         "Select", "select"},
    {ACT_ROW_TOP,      0,         "Top",    "top"},
    {ACT_ROW_MOVE,     -1,        "Row -",  "row-"},
    {ACT_ROW_MOVE,     +1,        "Row +",  "row+"},
    {ACT_PAGE_MOVE,    -1,        "Page -", "page-"},
    {ACT_PAGE_MOVE,    +1,        "Page +", "page+"},
    {ACT_VALUE_ADJUST, -1,        "Val -",  "val-"},
    {ACT_VALUE_ADJUST, +1,        "Val +",  "val+"},
    {ACT_JUMP,         0,         "Jump 1", "jump1"},
    {ACT_JUMP,         1,         "Jump 2", "jump2"},
    {ACT_JUMP,         2,         "Jump 3", "jump3"},
    {ACT_JUMP,         3,         "Jump 4", "jump4"},
    {ACT_JUMP,         4,         "Jump 5", "jump5"},
    {ACT_JUMP,         5,         "Jump 6", "jump6"},
    {ACT_JUMP,         6,         "Jump 7", "jump7"},
    {ACT_JUMP,         7,         "Jump 8", "jump8"},
    {ACT_MOD,          0,         "Mod",    "mod"},
    {ACT_JOY_MODE,     0,         "Joy XY", "joyxy"},
    {ACT_VALUE_FINE,   -1,        "Fine -", "fine-"},
    {ACT_VALUE_FINE,   +1,        "Fine +", "fine+"},
    {ACT_SECTION,      0,         "Sectn",  "section"},
    {ACT_PAD_BANK,     0,         "Bank A", "banka"},
    {ACT_PAD_BANK,     1,         "Bank B", "bankb"},
    {ACT_PAD_BANK,     2,         "Bank C", "bankc"},
    {ACT_PAD_BANK,     3,         "Bank D", "bankd"},
    {ACT_PAD_STOP,     0,         "Stop",   "padstop"},
    {ACT_PAD_SELECT,   0,         "PSel",   "padsel"},
    {ACT_PAD_MUTE,     0,         "PMute",  "padmute"},
    {ACT_PAD,          0,  "Pad 1",  "pad1"},  {ACT_PAD, 1,  "Pad 2",  "pad2"},  {ACT_PAD, 2,  "Pad 3",  "pad3"},  {ACT_PAD, 3,  "Pad 4",  "pad4"},
    {ACT_PAD,          4,  "Pad 5",  "pad5"},  {ACT_PAD, 5,  "Pad 6",  "pad6"},  {ACT_PAD, 6,  "Pad 7",  "pad7"},  {ACT_PAD, 7,  "Pad 8",  "pad8"},
    {ACT_PAD,          8,  "Pad 9",  "pad9"},  {ACT_PAD, 9,  "Pad 10", "pad10"}, {ACT_PAD, 10, "Pad 11", "pad11"}, {ACT_PAD, 11, "Pad 12", "pad12"},
    {ACT_PAD,          12, "Pad 13", "pad13"}, {ACT_PAD, 13, "Pad 14", "pad14"}, {ACT_PAD, 14, "Pad 15", "pad15"}, {ACT_PAD, 15, "Pad 16", "pad16"},
    {ACT_TEMPO,        -1,        "BPM -1", "bpm-1"},
    {ACT_TEMPO,        +1,        "BPM +1", "bpm+1"},
    {ACT_TEMPO,        -5,        "BPM -5", "bpm-5"},
    {ACT_TEMPO,        +5,        "BPM +5", "bpm+5"},
    {ACT_TAP,          0,         "Tap",    "tap"},
    {ACT_METRONOME,    0,         "Click",  "click"},
    {ACT_PAD_REC,      0,         "Rec",    "padrec"},
};
#define N_FNS ((int)(sizeof fns / sizeof fns[0]))

int keymap_fn_count(void) { return N_FNS + KEYMAP_NOTE_MAX + 1; }

key_fn_t keymap_fn_at(int i) {
    if (i < 0 || i >= keymap_fn_count()) return (key_fn_t){ACT_NONE, 0};
    if (i < N_FNS) return (key_fn_t){fns[i].act, fns[i].arg};
    return (key_fn_t){ACT_NOTE, (int8_t)(i - N_FNS)};
}

int keymap_fn_index(key_fn_t f) {
    if (f.act == ACT_NOTE) return f.arg >= 0 && f.arg <= KEYMAP_NOTE_MAX ? N_FNS + f.arg : 0;
    for (int i = 0; i < N_FNS; i++) if (fns[i].act == f.act && fns[i].arg == f.arg) return i;
    return 0;
}

void keymap_fn_name(key_fn_t f, char *out, int n) {
    if (f.act == ACT_NOTE) { seq_note_name(KEYBOARD_BASE_NOTE + f.arg, out, n); return; }
    snprintf(out, (size_t)n, "%s", fns[keymap_fn_index(f)].name);
}

void keymap_fn_token(key_fn_t f, char *out, int n) {
    if (f.act == ACT_NOTE) snprintf(out, (size_t)n, "note %d", f.arg);
    else                   snprintf(out, (size_t)n, "%s", fns[keymap_fn_index(f)].tok);
}

bool keymap_fn_parse(const char *s, key_fn_t *out) {
    char w[16];
    int note;
    if (sscanf(s, "%15s", w) != 1) return false;
    if (!strcmp(w, "note")) {
        if (sscanf(s, "%*s %d", &note) != 1 || note < 0 || note > KEYMAP_NOTE_MAX) return false;
        *out = (key_fn_t){ACT_NOTE, (int8_t)note};
        return true;
    }
    for (int i = 0; i < N_FNS; i++) if (!strcmp(w, fns[i].tok)) { *out = (key_fn_t){fns[i].act, fns[i].arg}; return true; }
    return false;
}

/* ---------------- the built-in layouts ---------------- */

#define NOTE(s) ((key_fn_t){ACT_NOTE, (int8_t)(s)})
#define FN(i)   ((key_fn_t){fns[i].act, fns[i].arg})
enum { F_NONE, F_SHIFT, F_MENU, F_BACK, F_PLAY, F_OCT_DN, F_OCT_UP, F_UP, F_DOWN, F_LEFT, F_RIGHT, F_LATCH, F_SELECT, F_TOP,
       F_ROW_DN, F_ROW_UP, F_PAGE_DN, F_PAGE_UP, F_VAL_DN, F_VAL_UP,
       F_SECTION = 32, F_BANK_A, F_BANK_B, F_BANK_C, F_BANK_D, F_PAD_STOP, F_PAD_SEL, F_PAD_MUTE, F_PAD_1,
       F_BPM_DN = F_PAD_1 + 16, F_BPM_UP, F_BPM_DN5, F_BPM_UP5, F_TAP, F_CLICK, F_REC };    // (fns[] order)

static const char *const layout_names[KEYMAP_LAYOUTS] = {"Keys 8x4", "Two 4x4", "Notes+Nav", "User"};
static const char *const layout_toks[KEYMAP_LAYOUTS]  = {"keys8x4", "two4x4", "notesnav", "user"};

// The navigation block of "Notes+Nav" (the right half of the note rows, top to bottom): a cursor cross with Latch in the middle.
static const uint8_t nav_block[4][4] = {
    {F_OCT_DN,  F_UP,   F_OCT_UP,  F_TOP},
    {F_LEFT,    F_LATCH, F_RIGHT,  F_SELECT},
    {F_VAL_DN,  F_DOWN, F_VAL_UP,  F_ROW_DN},
    {F_PAGE_DN, F_NONE, F_PAGE_UP, F_ROW_UP},
};

// Every layout: the function row is Shift, Menu, Back, Play (then Octave and Page on a board with 8 function keys). Notes go up to the right
// and up the rows; the bottom-left note key is the base note.
//   Keys 8x4   one keyboard of 32 notes: +1 per key to the right, +8 per row up
//   Two 4x4    the left block 0..15 (+4 per row, as on the 4 x 4 block of the panel), the right block continues with 16..31
//   Notes+Nav  the left block 0..15, the right block navigates the menus (nav_block)
static void preset(int p, key_fn_t out[KEY_COUNT]) {
    static const uint8_t fn_row[KEY_COLS] = {F_SHIFT, F_MENU, F_BACK, F_PLAY, F_OCT_DN, F_OCT_UP, F_PAGE_DN, F_PAGE_UP};
    for (int c = 0; c < KEY_COLS; c++) out[c] = FN(fn_row[c]);
    for (int r = 1; r < KEY_ROWS; r++) {
        const int up = KEY_ROWS - 1 - r;                          // note rows counted from the bottom
        for (int c = 0; c < KEY_COLS; c++) {
            key_fn_t *k = &out[r * KEY_COLS + c];
            const bool left = c < 4;
            switch (p) {
                case 1:  *k = NOTE((left ? 0 : 16) + up * 4 + (c & 3)); break;
                case 2:  *k = left ? NOTE(up * 4 + c) : FN(nav_block[r - 1][c - 4]); break;
                default: *k = NOTE(up * 8 + c); break;
            }
        }
    }
}

// The Pads section's layout (ADR-041 decision 18-19): the function row as everywhere, the left 4 x 4 block = pads 1..16 (top left = 1, in
// reading order, as on the screen's grid), the right block = Bank A-D on its top row, then Select, Mute and Stop all, then Tap, Click (the
// metronome), BPM -1 / +1. A board with only
// the 4 left columns gets the pads; its functions are Shift + pad (app/app_pads.c) and the encoders.
static void preset_pads(key_fn_t out[KEY_COUNT]) {
    static const uint8_t fn_row[KEY_COLS] = {F_SHIFT, F_MENU, F_BACK, F_PLAY, F_NONE, F_NONE, F_NONE, F_NONE};
    static const uint8_t right[4][4] = {
        {F_BANK_A,  F_BANK_B,   F_BANK_C, F_BANK_D},
        {F_PAD_SEL, F_PAD_MUTE, F_REC,    F_PAD_STOP},
        {F_TAP,     F_CLICK,    F_BPM_DN, F_BPM_UP},
        {F_NONE,    F_NONE,     F_NONE,   F_NONE},
    };
    for (int c = 0; c < KEY_COLS; c++) out[c] = FN(fn_row[c]);
    for (int r = 1; r < KEY_ROWS; r++)
        for (int c = 0; c < KEY_COLS; c++)
            out[r * KEY_COLS + c] = c < 4 ? FN(F_PAD_1 + (r - 1) * 4 + c) : FN(right[r - 1][c - 4]);
}

/* ---------------- state ---------------- */

static bool     g_pads;                     // the Pads section is on: its layout replaces the chosen one (not saved, not editable yet)
static int      g_layout;
static key_fn_t g_active[KEY_COUNT];        // the keys of g_layout (a built-in one is expanded here)
static key_fn_t g_user[KEY_COUNT];
static bool     g_dirty;

static void expand(void) {
    if (g_pads) preset_pads(g_active);
    else if (g_layout == KEYMAP_USER) memcpy(g_active, g_user, sizeof g_active);
    else preset(g_layout, g_active);
}

void keymap_init(void) {
    g_pads = false;
    g_layout = 0;
    preset(0, g_user);
    expand();
    g_dirty = false;
}

int keymap_layout(void) { return g_layout; }

void keymap_set_pads(bool on) { if (on != g_pads) { g_pads = on; expand(); } }
bool keymap_pads(void) { return g_pads; }
const char *keymap_layout_name(int l) { return l >= 0 && l < KEYMAP_LAYOUTS ? layout_names[l] : "?"; }

void keymap_select(int l) {
    if (l < 0 || l >= KEYMAP_LAYOUTS || l == g_layout) return;
    g_layout = l;
    expand();
    g_dirty = true;
}

key_fn_t keymap_get(int key) { return key >= 0 && key < KEY_COUNT ? g_active[key] : (key_fn_t){ACT_NONE, 0}; }

void keymap_set(int key, key_fn_t f) {
    if (key < 0 || key >= KEY_COUNT) return;
    if (g_layout != KEYMAP_USER) { memcpy(g_user, g_active, sizeof g_user); g_layout = KEYMAP_USER; }
    g_user[key] = f;
    expand();
    g_dirty = true;
}

void keymap_reset(void) { keymap_init(); g_dirty = true; }
bool keymap_dirty(void) { return g_dirty; }

/* ---------------- keys.cfg ---------------- */
//   # SynthCore key layout
//   layout user
//   key 0.0 shift            one line per key (User keys only; a built-in layout needs none): row.col and the function word
//   key 1.0 note 24          a note: semitones above the base note
// Lines that are not understood are skipped, so a file from a later version still loads.

int keymap_to_text(char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "# SynthCore key layout (keys.cfg). Layouts: keys8x4 two4x4 notesnav user\nlayout %s\n", layout_toks[g_layout]);
    for (int k = 0; k < KEY_COUNT && n < cap; k++) {
        const key_fn_t f = g_user[k];
        if (f.act == ACT_NOTE) n += snprintf(buf + n, (size_t)(cap - n), "key %d.%d note %d\n", k / KEY_COLS, k % KEY_COLS, f.arg);
        else                   n += snprintf(buf + n, (size_t)(cap - n), "key %d.%d %s\n", k / KEY_COLS, k % KEY_COLS, fns[keymap_fn_index(f)].tok);
    }
    return n < cap ? n : cap - 1;
}

bool keymap_from_text(const char *txt) {
    int layout = -1;
    key_fn_t user[KEY_COUNT];
    memcpy(user, g_user, sizeof user);
    for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        char w1[16], w2[16], w3[16];
        const int got = sscanf(line, "%15s %15s %15s", w1, w2, w3);
        if (got >= 2 && !strcmp(w1, "layout")) {
            for (int l = 0; l < KEYMAP_LAYOUTS; l++) if (!strcmp(w2, layout_toks[l])) layout = l;
        } else if (got >= 3 && !strcmp(w1, "key")) {
            int r, c;
            if (sscanf(w2, "%d.%d", &r, &c) != 2 || r < 0 || r >= KEY_ROWS || c < 0 || c >= KEY_COLS) continue;
            key_fn_t *k = &user[r * KEY_COLS + c];
            int at = 0;
            sscanf(line, "%*s %*s %n", &at);
            if (at > 0) keymap_fn_parse(line + at, k);
        }
    }
    if (layout < 0) return false;                                 // not a key file
    memcpy(g_user, user, sizeof g_user);
    g_layout = layout;
    expand();
    return true;
}

#define g_buf g_storage_buf                      // the shared settings buffer (hal_storage.h)

bool keymap_load(void) {
    if (storage_read(KEYMAP_FILE, g_buf, STORAGE_FILE_MAX) < 0 || !keymap_from_text(g_buf)) return false;
    g_dirty = false;
    return true;
}

bool keymap_save(void) {
    if (!g_dirty) return true;
    const int n = keymap_to_text(g_buf, STORAGE_FILE_MAX);
    if (!storage_write(KEYMAP_FILE, g_buf, n)) return false;
    g_dirty = false;
    return true;
}
