#ifdef PLATFORM_SIM
// SynthLab Studio: a desktop program for SynthLab's synths, worked with the mouse and the keyboard. Two tabs: SOUND (every value as a knob,
// eight macros assigned by dragging) and LAYOUT (the board's screen, arranged by hand). The synths and the engine are SynthLab's
// (apps/synthlab/src), the widgets the library's (framework/ui/widgets.h), the window boards/desktop/desk_window.h.
//
//   studio.exe                         the window
//   studio.exe --shot out.png [--tab layout] [--synth N] [--try] [--auto rows|knobs] [--scale N] [--size WxH]   one frame into a PNG
//   studio.exe --export synth|engine|patch [--patch file.patch]   the PATCH tab's exports of the saved Modular patch (or that file)
//   studio.exe --selftest              checks the models and the widgets, prints PASS / FAIL
#define SDL_MAIN_HANDLED
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "SDL.h"
#include "boards/desktop/desk_window.h"
#include "hal/hal_audio_out.h"
#include "engine/modules/builtin.h"
#include "engine/modules/dx7_voice.h"
#include "engine/modules/fx2_modules.h"
#include "engine/modules/fx_modules.h"
#include "engine/modules/mi_osc.h"
#include "engine/modules/motion_seq.h"
#include "engine/modules/osc_engines.h"
#include "engine/modules/para_modules.h"
#include "engine/modules/sampler_modules.h"
#include "engine/modules/strings_modules.h"
#include "engine/modules/synth_modules.h"
#include "export.h"
#include "modular/engine_adapter.h"
#include "modular_synth.h"
#include "nulltest.h"
#include "params.h"
#include "studio.h"
#include "synth.h"

bool write_png(const char *path, const uint16_t *frame, int fw, int fh, int scale);   // (SynthLab's tests/png.cpp)
int shot_scale = 1;                                                                    // --scale: the PNGs pixels x N
int studio_selftest(wg_ctx_t &c, const char *shot);                                   // selftest.cpp

namespace studio {

State st;
int g_w = kMinW, g_h = kMinH;

const lab::Synth &cur() { return *lab::kSynths[synth_selected()]; }
Macro &cur_macro() { return st.macros[synth_selected()].m[st.macro_sel]; }
Layout &cur_layout() { return st.layouts[synth_selected()]; }

void message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(st.message, sizeof st.message, fmt, ap);
    va_end(ap);
    st.message_frames = 240;
}

void mark_dirty() { st.dirty[synth_selected()] = true; }
float value_norm(int param) { return to_norm(cur().params[param], synth_value(param)); }
void set_value_norm(int param, float n) { synth_set_value(param, from_norm(cur().params[param], n)); }

/* ---------------- files ---------------- */

namespace {
std::string file_name(int synth, const char *ext) {
    std::string n = lab::kSynths[synth]->name;
    for (char &ch : n) if (ch == ' ' || ch == '/' || ch == '\\') ch = '_';
    return st.preset_dir + n + ext;
}

bool read_file(const std::string &path, std::string &out) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool write_file(const std::string &path, const std::string &text) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}
}  // namespace

std::string sound_to_text(int synth) {
    const lab::Synth &s = *lab::kSynths[synth];
    std::string out = "# SynthLab Studio sound: ";
    out += s.name;
    out += "\n";
    char line[160], path[64];
    for (int i = 0; i < s.n_params; i++) {
        if (s.params[i].curve == lab::Head) continue;
        param_path(s, i, path, sizeof path);
        std::snprintf(line, sizeof line, "value %s %g\n", path, static_cast<double>(synth_value(i)));
        out += line;
    }
    return out + macros_to_text(s, st.macros[synth]);
}

bool sound_from_text(int synth, const char *text) {
    const lab::Synth &s = *lab::kSynths[synth];
    for (const char *p = text; *p;) {
        const char *e = std::strchr(p, '\n');
        const size_t n = e ? static_cast<size_t>(e - p) : std::strlen(p);
        char line[200], path[64];
        float v;
        std::snprintf(line, sizeof line, "%.*s", static_cast<int>(n < sizeof line ? n : sizeof line - 1), p);
        p += n + (e ? 1 : 0);
        const char *q = line;
        char word[16];
        if (read_word(q, word, sizeof word) && std::strcmp(word, "value") == 0 && read_word(q, path, sizeof path) && read_word(q, word, sizeof word)) {
            v = static_cast<float>(std::atof(word));
            const int i = find_param(s, path);
            if (i >= 0) synth_set_value(i, v);
        }
    }
    return macros_from_text(s, st.macros[synth], text);
}

bool save_presets(int synth) {
    std::error_code ec;
    std::filesystem::create_directories(st.preset_dir, ec);
    const std::string sound = file_name(synth, ".sound"), lay = file_name(synth, ".layout");
    bool ok = write_file(sound, sound_to_text(synth)) && write_file(lay, layout_to_text(*lab::kSynths[synth], st.layouts[synth]));
    if (ok && synth == modular_index()) { patch_sync(); ok = save_patch(file_name(synth, ".patch")); }
    if (ok) { st.dirty[synth] = false; message("Saved %s and %s", sound.c_str(), lay.c_str()); }
    else message("Could not write in %s (does the folder exist?)", st.preset_dir.c_str());
    return ok;
}

void load_presets(int synth) {
    if (synth == modular_index() && st.loaded[synth]) load_patch(file_name(synth, ".patch"));     // (Revert: the patch too)
    const lab::Synth &s = *lab::kSynths[synth];
    std::string text;
    st.macros[synth] = MacroSet();
    if (read_file(file_name(synth, ".sound"), text)) sound_from_text(synth, text.c_str());
    else for (int i = 0; i < s.n_params; i++) if (s.params[i].curve != lab::Head) synth_set_value(i, s.params[i].def);
    int dropped = 0;
    if (!read_file(file_name(synth, ".layout"), text) || !layout_from_text(s, st.layouts[synth], text.c_str(), &dropped))
        layout_auto(s, st.layouts[synth], kRow);
    else if (dropped) message("%d element(s) of the saved layout name values %s no longer has", dropped, s.name);
    st.loaded[synth] = true;
    st.dirty[synth] = false;
}

/* ---------------- playing ---------------- */

namespace {
// Notes on the computer keyboard, by place (a US keyboard's letters; on AZERTY the same keys): two rows from C, then the upper octave.
struct NoteKey { char key; int semi; };
const NoteKey kNoteKeys[] = {
    {'z', 0}, {'s', 1}, {'x', 2}, {'d', 3}, {'c', 4}, {'v', 5}, {'g', 6}, {'b', 7}, {'h', 8}, {'n', 9}, {'j', 10}, {'m', 11},
    {',', 12}, {'l', 13}, {'.', 14}, {';', 15}, {'/', 16},
    {'q', 12}, {'2', 13}, {'w', 14}, {'3', 15}, {'e', 16}, {'r', 17}, {'5', 18}, {'t', 19}, {'6', 20}, {'y', 21}, {'7', 22}, {'u', 23},
    {'i', 24}, {'9', 25}, {'o', 26}, {'0', 27}, {'p', 28},
};
int held_note[128];                             // per key: the note + 1 it started

void select_synth(int index) {
    index = index < 0 ? lab::kSynthCount - 1 : index >= lab::kSynthCount ? 0 : index;
    if (index == synth_selected()) return;
    st.hold_note = -1;
    std::memset(held_note, 0, sizeof held_note);
    synth_select(index);
    if (!st.loaded[index]) load_presets(index);
    st.page = 0;
    st.sel_el = -1;
    st.try_focus = 0;
    st.dest_sel = 0;
    st.sound_scroll = 0;
    st.list_scroll = 0;
}

void play() {
    const lab::Synth &s = cur();
    if (s.opt.play_bpm > 0) {
        if (synth_looping()) synth_loop_stop();
        else synth_loop_start(st.last_note, s.opt.play_bpm);
    } else if (st.hold_note >= 0) {
        synth_note_off(st.hold_note);
        st.hold_note = -1;
    } else {
        st.hold_note = st.last_note;
        synth_note_on(st.hold_note);
    }
}

bool playing() { return synth_looping() || st.hold_note >= 0; }

// KEYS (the top bar, F4): the computer keyboard plays notes while it is latched. Unlatched, the notes its keys hold are let go.
void set_keys(bool on) {
    if (!on)
        for (int k = 0; k < 128; k++)
            if (held_note[k]) {
                if (held_note[k] - 1 != st.hold_note) synth_note_off(held_note[k] - 1);
                held_note[k] = 0;
            }
    st.keys_on = on;
}

void note_keys(const wg_input_t &in) {
    if (!st.keys_on) return;
    for (int i = 0; i < in.nkeys; i++) {
        const wg_key_t &k = in.keys[i];
        if (k.repeat || k.key >= 128) continue;
        for (const NoteKey &nk : kNoteKeys) {
            if (nk.key != static_cast<char>(k.key)) continue;
            if (k.down) {
                if ((in.mods & (WG_MOD_CTRL | WG_MOD_ALT)) || held_note[k.key]) break;
                const int note = 48 + 12 * st.octave + nk.semi;
                if (note < 0 || note > 127) break;
                synth_note_on(note);
                held_note[k.key] = note + 1;
                st.last_note = note;
                synth_loop_note(note);
            } else if (held_note[k.key]) {
                if (held_note[k.key] - 1 != st.hold_note) synth_note_off(held_note[k.key] - 1);
                held_note[k.key] = 0;
            }
            break;
        }
    }
}

void global_keys(const wg_input_t &in) {
    const bool ctrl = (in.mods & WG_MOD_CTRL) != 0;
    if (wg_key_pressed(&in, WG_KEY_F1)) st.tab = kTabSound;
    if (wg_key_pressed(&in, WG_KEY_F2)) st.tab = kTabLayout;
    if (wg_key_pressed(&in, WG_KEY_F3)) st.tab = kTabPatch;
    if (wg_key_pressed(&in, WG_KEY_F4)) set_keys(!st.keys_on);
    if (wg_key_pressed(&in, WG_KEY_F5) && st.octave > -3) st.octave--;
    if (wg_key_pressed(&in, WG_KEY_F6) && st.octave < 3) st.octave++;
    if (wg_key_pressed(&in, WG_KEY_F7)) select_synth(synth_selected() - 1);
    if (wg_key_pressed(&in, WG_KEY_F8)) select_synth(synth_selected() + 1);
    if (wg_key_pressed(&in, ' ')) play();
    if (ctrl && wg_key_pressed(&in, 's')) save_presets(synth_selected());
    note_keys(in);
}

// Renaming a macro or the patch: the typed text goes into the name; Enter keeps it, Esc drops it.
void edit_keys(const wg_input_t &in) {
    const size_t max = st.edit_what == 1 ? sizeof st.patch.name - 1 : 15;
    for (const char *p = in.text; *p; p++)
        if (*p >= 32 && *p < 127 && *p != '"' && st.edit_text.size() < max) st.edit_text += *p;
    if (wg_key_pressed(&in, WG_KEY_BACKSPACE) && !st.edit_text.empty()) st.edit_text.pop_back();
    if (wg_key_pressed(&in, WG_KEY_ENTER)) {
        if (st.edit_what == 1) std::snprintf(st.patch.name, sizeof st.patch.name, "%s", st.edit_text.c_str());
        else std::snprintf(cur_macro().name, sizeof cur_macro().name, "%s", st.edit_text.c_str());
        st.editing = false;
        mark_dirty();
    }
    if (wg_key_pressed(&in, WG_KEY_ESC)) st.editing = false;
}

void draw_bar(wg_ctx_t &c) {
    const wg_theme_t &t = c.theme;
    wg_fill(&c, gui_rect(0, 0, g_w, kBarH), t.panel);
    if (wg_tab(&c, WG_ID(F_BAR, 0), gui_rect(4, 2, 62, kBarH - 4), "SOUND", st.tab == kTabSound)) st.tab = kTabSound;
    if (wg_tab(&c, WG_ID(F_BAR, 1), gui_rect(68, 2, 62, kBarH - 4), "LAYOUT", st.tab == kTabLayout)) st.tab = kTabLayout;
    if (wg_tab(&c, WG_ID(F_BAR, 8), gui_rect(132, 2, 62, kBarH - 4), "PATCH", st.tab == kTabPatch)) st.tab = kTabPatch;
    if (c.hot == WG_ID(F_BAR, 8)) wg_set_hint(&c, "F3: build the Modular synth from modules and cables");
    char text[64];
    std::snprintf(text, sizeof text, "%s%s", cur().name, st.dirty[synth_selected()] ? " *" : "");
    const int step = wg_stepper(&c, WG_ID(F_BAR, 2), gui_rect(200, 3, 130, kBarH - 6), text);
    if (step) select_synth(synth_selected() + step);
    if (c.hot == WG_ID(F_BAR, 2)) wg_set_hint(&c, "The synth (F7 / F8). Each keeps its own values, macros and layout");
    if (wg_button(&c, WG_ID(F_BAR, 9), gui_rect(336, 3, 46, kBarH - 6), "KEYS", st.keys_on)) set_keys(!st.keys_on);
    if (c.hot == WG_ID(F_BAR, 9))
        wg_set_hint(&c, "F4: the computer keyboard plays notes while latched (Z S X D C ... and Q 2 W 3 E ...); now %s", st.keys_on ? "on" : "off");
    if (wg_button(&c, WG_ID(F_BAR, 3), gui_rect(386, 3, 48, kBarH - 6), cur().opt.play_bpm > 0 ? "LOOP" : "HOLD", playing())) play();
    if (c.hot == WG_ID(F_BAR, 3)) wg_set_hint(&c, "Space: %s the last note played", cur().opt.play_bpm > 0 ? "loop" : "hold");
    std::snprintf(text, sizeof text, "Oct %+d", st.octave);
    const int o = wg_stepper(&c, WG_ID(F_BAR, 4), gui_rect(438, 3, 66, kBarH - 6), text);
    st.octave = std::max(-3, std::min(3, st.octave + o));
    if (c.hot == WG_ID(F_BAR, 4)) wg_set_hint(&c, "Octave (F5 / F6). Notes, with KEYS latched: Z S X D C ... and Q 2 W 3 E ... (the keys' places)");
    std::snprintf(text, sizeof text, "Vol %d%%", static_cast<int>(st.volume * 100.f + 0.5f));
    if (wg_slider(&c, WG_ID(F_BAR, 5), gui_rect(508, 5, 80, kBarH - 10), &st.volume, 0.7f, text, 0)) synth_set_volume(st.volume);
    if (wg_button(&c, WG_ID(F_BAR, 6), gui_rect(592, 3, 44, kBarH - 6), "Save", false)) save_presets(synth_selected());
    if (c.hot == WG_ID(F_BAR, 6)) wg_set_hint(&c, "Ctrl+S: the values, the macros, the layout (and the Modular synth's patch), in %s", st.preset_dir.c_str());
    if (wg_button(&c, WG_ID(F_BAR, 7), gui_rect(640, 3, 52, kBarH - 6), "Revert", false)) { load_presets(synth_selected()); message("Back to what was saved"); }
    std::snprintf(text, sizeof text, "voices %d", synth_active_voices());
    wg_text_small(&c, 700, 15, text, t.dim);
    wg_text(&c, g_w - 6 - wg_text_w(&c, "STUDIO", false), 16, "STUDIO", t.accent);
}

void draw_status(wg_ctx_t &c) {
    const wg_theme_t &t = c.theme;
    const gui_rect_t r = gui_rect(0, g_h - kStatusH, g_w, kStatusH);
    wg_fill(&c, r, t.panel);
    const char *left = st.editing ? "Renaming: type, Enter keeps, Esc drops" : (c.hint[0] ? c.hint : (st.message_frames > 0 ? st.message : ""));
    wg_text_small(&c, 6, r.y + 10, left, st.message_frames > 0 && !c.hint[0] ? t.accent : t.text);
    const char *help = "Tab focus  arrows edit  Space play  F4 keys  F1/F2/F3 tabs  F7/F8 synth  Ctrl+S save";
    const int hw = wg_text_w(&c, help, true);
    if (6 + wg_text_w(&c, left, true) + 16 + hw < g_w) wg_text_small(&c, g_w - 6 - hw, r.y + 10, help, t.dim);   // (a long hint takes the line)
    if (st.message_frames > 0) st.message_frames--;
}

}  // namespace

void run_frame(wg_ctx_t &c, const wg_input_t &in) {
    if (st.editing) edit_keys(in); else global_keys(in);
    wg_begin(&c, &in);
    wg_fill(&c, gui_rect(0, 0, c.cv->w, c.cv->h), c.theme.bg);
    const gui_rect_t area = gui_rect(0, kBarH, g_w, g_h - kBarH - kStatusH);
    if (st.tab == kTabPatch && synth_selected() != modular_index()) select_synth(modular_index());   // (the PATCH tab plays its patch)
    if (synth_selected() == modular_index()) patch_sync();
    if (st.tab == kTabSound) draw_sound(c, area);
    else if (st.tab == kTabLayout) draw_layout(c, area);
    else draw_patch(c, area);
    draw_bar(c);
    draw_status(c);
    wg_end(&c);
}

namespace {
std::string base_dir() {
    char *b = SDL_GetBasePath();                // apps/studio/build/
    std::string d = b ? b : "./";
    SDL_free(b);
    return d + "../presets/";
}
}  // namespace
}  // namespace studio

using namespace studio;

static bool same_name(const char *a, const char *b) {      // case aside
    for (; *a && *b; a++, b++) if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}

int main(int argc, char **argv) {
    const char *shot = nullptr;
    int want_synth = -1;
    bool selftest = false, try_mode = false;
    int auto_style = -1;
    const char *export_what = nullptr, *patch_file = nullptr, *null_synth = nullptr;
    bool list_modules = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--tab") && i + 1 < argc) { ++i; st.tab = !std::strcmp(argv[i], "layout") ? kTabLayout : !std::strcmp(argv[i], "patch") ? kTabPatch : kTabSound; }
        else if (!std::strcmp(argv[i], "--synth") && i + 1 < argc) want_synth = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--export") && i + 1 < argc) export_what = argv[++i];
        else if (!std::strcmp(argv[i], "--patch") && i + 1 < argc) patch_file = argv[++i];
        else if (!std::strcmp(argv[i], "--null") && i + 1 < argc) null_synth = argv[++i];
        else if (!std::strcmp(argv[i], "--try")) try_mode = true;
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc && std::sscanf(argv[++i], "%dx%d", &g_w, &g_h) == 2) {   // --shot at that frame size
            g_w = std::max(kMinW, std::min(kMaxW, g_w));
            g_h = std::max(kMinH, std::min(kMaxH, g_h));
        }
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) shot_scale = std::max(1, std::min(4, std::atoi(argv[++i])));
        else if (!std::strcmp(argv[i], "--auto") && i + 1 < argc) auto_style = !std::strcmp(argv[++i], "knobs") ? kKnob : kRow;
        else if (!std::strcmp(argv[i], "--selftest")) selftest = true;
        else if (!std::strcmp(argv[i], "--modules")) list_modules = true;
    }
    constexpr size_t kFastBytes = 32u << 20, kBulkBytes = 16u << 20;   // (a Modular voice reserves 2 MB, its FX 4 MB: kModularVoiceBytes)
    void *fast = std::malloc(kFastBytes), *bulk = std::malloc(kBulkBytes);
    if (!fast || !bulk || !synth_init(fast, kFastBytes, bulk, kBulkBytes)) { std::fprintf(stderr, "the synth did not start\n"); return 1; }
    st.preset_dir = base_dir();
    {                                           // the Ritual Machine's modules in the catalog (the families its engine_synth.cpp registers)
        static sc::Registry reg;
        sc::register_builtin_modules(reg);
        sc::register_sampler_modules(reg);
        sc::register_synth_modules(reg);
        sc::register_fx_modules(reg);
        sc::register_fx2_modules(reg);
        sc::register_strings_modules(reg);
        sc::register_para_modules(reg);
        sc::register_dx7_module(reg);
        sc::register_motion_module(reg);
        sc::register_osc_engines(reg);
        sc::register_mi_osc(reg);
        mod::register_engine_modules(reg);
    }
    if (list_modules) {                         // --modules: the catalog, one line each
        int n;
        const mod::ModuleDef *const *cat = mod::catalog(&n);
        for (int i = 0; i < n; i++) {
            const mod::ModuleDef &d = *cat[i];
            std::printf("%-16s %-14s %-6s in %d out %d values %2d  %6u KB  %s\n", d.name, d.category,
                        d.scope == mod::Scope::Voice ? "voice" : d.scope == mod::Scope::Global ? "fx" : "both", d.n_in, d.n_out, d.n_param,
                        static_cast<unsigned>((d.size + 1023) / 1024), d.engine ? d.engine : "-");
        }
        synth_shutdown();
        return 0;
    }
    static uint16_t pixels[kMaxW * kMaxH];
    static canvas_t cv;
    canvas_init(&cv, pixels, (g_w + 7) / 8 * 8, (g_h + 7) / 8 * 8);
    static wg_ctx_t c;
    wg_init(&c, &cv);
    if (!load_patch(st.preset_dir + "Modular.patch")) { st.patch.init_default(); patch_apply(); }
    load_presets(synth_selected());
    if (want_synth >= 0) select_synth(want_synth);
    st.try_mode = try_mode;
    if (null_synth) {                           // --null <synth> [--patch file]: the null test of a rebuild (nulltest.h)
        const lab::Synth *s = nullptr;
        for (int i = 0; i < lab::kSynthCount; i++)
            if (same_name(lab::kSynths[i]->name, null_synth)) s = lab::kSynths[i];
        if (!s) { std::fprintf(stderr, "no synth named %s\n", null_synth); return 1; }
        const std::string path = patch_file ? std::string(patch_file) : library_path(*s);
        static mod::Patch p;
        if (!read_patch_file(path, p)) { std::fprintf(stderr, "cannot read the patch %s\n", path.c_str()); return 1; }
        const NullResult r = null_test(*s, p);
        std::printf("%s vs %s: null depth %.1f dB (synth %.1f dB rms, patch %.1f dB rms, residual peak %.1f dB)\n", s->name, path.c_str(),
                    static_cast<double>(r.depth_db), static_cast<double>(r.synth_rms_db), static_cast<double>(r.patch_rms_db),
                    static_cast<double>(r.residual_peak_db));
        synth_shutdown();
        return 0;
    }
    if (patch_file && !load_patch(patch_file)) { std::fprintf(stderr, "cannot read the patch %s\n", patch_file); return 1; }
    if (export_what) {                          // --export synth | engine | patch [--patch file]: the PATCH tab's export, without a window
        std::string path, err;
        const std::string id = patch_ident(st.patch.name);
        const bool ok = !std::strcmp(export_what, "synth") ? export_synth(st.patch, id, path, err)
                      : !std::strcmp(export_what, "engine") ? export_engine(st.patch, id, path, err) : export_patch(st.patch, id, path, err);
        std::printf("%s %s\n", ok ? "wrote" : "export failed:", ok ? path.c_str() : err.c_str());
        synth_shutdown();
        return ok ? 0 : 1;
    }
    if (auto_style >= 0) layout_auto(cur(), cur_layout(), auto_style);     // (--auto rows | knobs: the selected synth's layout made again)
    if (selftest) { const int r = studio_selftest(c, shot); synth_shutdown(); return r; }

    if (shot) {                                 // one frame, the pointer outside the window
        wg_input_t in{};
        in.x = in.y = -100;
        run_frame(c, in);
        run_frame(c, in);
        const bool ok = write_png(shot, pixels, cv.w, g_h, shot_scale);
        std::printf("%s %s\n", ok ? "wrote" : "could not write", shot);
        synth_shutdown();
        return ok ? 0 : 1;
    }

    if (!desk_window_open("SynthLab Studio", kMinW, kMinH, kMaxW, kMaxH)) return 1;
    synth_set_volume(st.volume);
    if (!audio_out_start(synth_render, synth_sample_rate())) std::fprintf(stderr, "no audio output: the studio runs without sound\n");
    wg_input_t in{};
    for (;;) {
        desk_window_poll(&in, 8);
        if (in.quit) break;
        synth_update();
        int w, h;                               // the window's frame now: a new size, a canvas of that size (rounded up to 8: u8g2's tiles)
        desk_window_frame(&w, &h);
        if (w != g_w || h != g_h) {
            g_w = w;
            g_h = h;
            canvas_init(&cv, pixels, (w + 7) / 8 * 8, (h + 7) / 8 * 8);
        }
        run_frame(c, in);
        desk_window_present(pixels, cv.w);
    }
    audio_out_stop();
    desk_window_close();
    synth_shutdown();
    std::free(fast);
    std::free(bulk);
    return 0;
}
#endif  // PLATFORM_SIM
