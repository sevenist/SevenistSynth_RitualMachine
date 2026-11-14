#pragma once
// The studio's state and the parts of its window. main.cpp: the window, the loop, the top bar, notes, files. tab_sound.cpp: the values as
// knobs and the macros. tab_layout.cpp: the arrangement editor.
#include <string>
#include "lab.h"
#include "layout.h"
#include "macros.h"
#include "modular/patch.h"
#include "ui/widgets.h"

namespace studio {

// The window's frame, in pixels: it follows the window (boards/desktop/desk_window.h), at least kMinW x kMinH, at most kMaxW x kMaxH (the
// canvas: -DCANVAS_MAX_W / H in build.ps1). The tabs lay themselves out in g_w x g_h; --shot and --selftest draw at the smallest size.
constexpr int kMinW = 800, kMinH = 480, kMaxW = 2040, kMaxH = 1280;
extern int g_w, g_h;
constexpr int kBarH = 24, kStatusH = 14;

enum Tab { kTabSound, kTabLayout, kTabPatch };
enum DndKind { kDndMacro = 1, kDndParam, kDndTitle, kDndModule, kDndJack };

// Widget id families (WG_ID(family, index)).
enum Family { F_BAR = 1, F_KNOB, F_MACRO, F_HANDLE, F_DEST, F_DEST_X, F_DEST_ED, F_MACRO_ED, F_KIND, F_LAYOUT_BTN, F_PREVIEW, F_LIST, F_INSPECT,
              F_PALETTE, F_NODE, F_JACK, F_PROW, F_PATCH_BTN, F_CANVAS, F_PINSP };

struct State {
    int tab = kTabSound;
    MacroSet macros[lab::kMaxSynths];
    Layout layouts[lab::kMaxSynths];
    bool loaded[lab::kMaxSynths] = {};
    bool dirty[lab::kMaxSynths] = {};
    // sound tab
    int macro_sel = 0, dest_sel = 0;
    int sound_scroll = 0;
    // layout tab
    int page = 0, sel_el = -1, new_kind = kRow, snap = 4;
    bool try_mode = false;
    int try_focus = 0;                          // in nav_order of the page
    int list_scroll = 0;
    int drag_mode = 0, drag_dx = 0, drag_dy = 0;    // the preview: 1 move, 2 resize; the press's offset in the element
    // patch tab: the patch the Modular synth plays (modular_synth.h), the canvas's view, the selection
    mod::Patch patch;
    int patch_area = 0;                         // mod::kVoiceArea / kFxArea
    int pan_x[2] = {}, pan_y[2] = {};
    int sel_node = 0;                           // a node id (0: none)
    int palette_scroll = 0;
    int pick_out = 0, pick_in = 0;              // the inspector's "connect" choices
    int patch_file = 0;                         // the Load list's position
    int open_synth = 0;                         // the "open a synth" list's position
    bool panning = false;
    int pan_mx = 0, pan_my = 0;
    // renaming a macro or the patch (the keyboard types into it; notes are off)
    int edit_what = 0;                          // 0 the macro, 1 the patch
    bool editing = false;
    std::string edit_text;
    // playing
    int octave = 0, last_note = 48, hold_note = -1;
    bool keys_on = true;                        // KEYS: the computer keyboard plays notes (F4)
    float volume = 0.7f;
    char message[128] = {};
    int message_frames = 0;
    std::string preset_dir;
};
extern State st;

const lab::Synth &cur();
Macro &cur_macro();
Layout &cur_layout();
void message(const char *fmt, ...);
void mark_dirty();
float value_norm(int param);                    // the selected synth's value as its knob's position
void set_value_norm(int param, float n);

void run_frame(wg_ctx_t &c, const wg_input_t &in);     // one frame of the window: input, drawing
void draw_sound(wg_ctx_t &c, gui_rect_t area);
void draw_layout(wg_ctx_t &c, gui_rect_t area);
void draw_patch(wg_ctx_t &c, gui_rect_t area);

// The patch (tab_patch.cpp): after a change of modules or cables, patch_apply() makes it the Modular synth's (its list, its voices; the
// macros and the layout follow their values by name). patch_sync() copies the synth's values into the patch (each frame).
void patch_apply();
void patch_sync();
bool save_patch(const std::string &path);
bool load_patch(const std::string &path);
bool read_patch_file(const std::string &path, mod::Patch &out);   // a .patch file, not applied (false: unreadable)
std::string patch_dir();                        // presets/patches/
std::string patch_ident(const char *name);      // "Kick (patch)" -> "kick_patch": file names and C++ symbols

// Files (presets/<synth>.sound: values and macros; presets/<synth>.layout).
std::string sound_to_text(int synth);
bool sound_from_text(int synth, const char *text);
bool save_presets(int synth);
void load_presets(int synth);                   // once per synth: what is on disk, else the defaults and an automatic layout

}  // namespace studio
