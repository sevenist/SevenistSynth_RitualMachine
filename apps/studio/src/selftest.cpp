#ifdef PLATFORM_SIM
// studio.exe --selftest: the models (values <-> knob positions, macros, layouts, their text files) and the widgets (ui/widgets.h) driven by
// scripted pointer and keyboard input. Prints each failure and a total; returns 0 when everything passed.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>
#include "engine/modules/mi_osc.h"
#include "engine/modules/osc_engines.h"
#include "export.h"
#include "modular/engine_adapter.h"
#include "modular/modules.h"
#include "modular_synth.h"
#include "nulltest.h"
#include "params.h"
#include "studio.h"
#include "synth.h"
#include "host/png.h"
#include "tests/test.h"
#include "hal/hal_storage.h"

using namespace studio;

extern int shot_scale;                                                                  // main.cpp: --scale

namespace {

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

void test_params() {
    for (int si = 0; si < lab::kSynthCount; si++) {
        const lab::Synth &s = *lab::kSynths[si];
        for (int i = 0; i < s.n_params; i++) {
            const lab::Param &p = s.params[i];
            if (p.curve == lab::Head) continue;
            const float back = pdesc_from_norm(&p, pdesc_to_norm(&p, p.def));
            CHECK(near(back, p.def, 1e-3f * (p.max - p.min) + 1e-4f), "%s %s: default %g comes back as %g", s.name, p.name, (double)p.def, (double)back);
            CHECK(near(pdesc_from_norm(&p, 0.f), p.min, 1e-4f * (p.max - p.min) + 1e-5f) && near(pdesc_from_norm(&p, 1.f), p.max, 1e-3f * (p.max - p.min)), "%s %s: the ends", s.name, p.name);
            char path[64];
            param_path(s, i, path, sizeof path);
            CHECK(find_param(s, path) == i, "%s: %s is not found back (%d)", s.name, path, find_param(s, path));
        }
    }
}

void test_layouts() {
    for (int si = 0; si < lab::kSynthCount; si++) {
        const lab::Synth &s = *lab::kSynths[si];
        for (int style : {static_cast<int>(kRow), static_cast<int>(kKnob)}) {
            Layout l;
            layout_auto(s, l, style);
            int seen[lab::kMaxParams] = {};
            for (const Page &p : l.pages)
                for (size_t a = 0; a < p.els.size(); a++) {
                    const El &e = p.els[a];
                    if (el_has_value(s, e)) seen[e.param]++;
                    CHECK(e.x >= 0 && e.y >= 0 && e.x + e.w <= kScreenW && e.y + e.h <= kScreenH, "%s %s: an element off the screen", s.name, kind_name(style));
                    for (size_t b = a + 1; b < p.els.size(); b++) {
                        const El &f = p.els[b];
                        CHECK(!(e.x < f.x + f.w && f.x < e.x + e.w && e.y < f.y + f.h && f.y < e.y + e.h), "%s %s: two elements overlap", s.name, kind_name(style));
                    }
                }
            for (int i = 0; i < s.n_params; i++)
                if (s.params[i].curve != lab::Head) CHECK(seen[i] == 1, "%s %s: %s placed %d times", s.name, kind_name(style), s.params[i].name, seen[i]);
            Layout back;
            const std::string text = layout_to_text(s, l);
            CHECK(layout_from_text(s, back, text.c_str()) && layout_to_text(s, back) == text, "%s: the layout's text does not read back", s.name);
        }
    }
}

void test_macros() {
    CHECK(near(curve_eval(kExp, 0.f), 0.f, 1e-6f) && near(curve_eval(kExp, 1.f), 1.f, 1e-5f) && curve_eval(kExp, 0.5f) < 0.5f && curve_eval(kLog, 0.5f) > 0.5f, "the curves");
    synth_select(0);
    const lab::Synth &s = cur();
    int a = -1, b = -1;
    for (int i = 0; i < s.n_params && b < 0; i++) if (s.params[i].curve != lab::Head && s.params[i].curve != lab::Int) (a < 0 ? a : b) = i;
    CHECK(a >= 0 && b >= 0, "the first synth has two continuous values");
    if (a < 0 || b < 0) return;
    MacroSet set;
    Macro &m = set.m[2];
    m.pos = 0.3f;
    const int da = macro_add(m, a, 0.5f);
    CHECK(da == 0 && near(dest_apply(m.d[0], m.pos), 0.5f, 1e-5f), "a new destination keeps the value where it is (%g)", (double)dest_apply(m.d[0], m.pos));
    CHECK(macro_add(m, a, 0.2f) == 0 && m.n == 1, "the same value twice is one destination");
    const int db = macro_add(m, b, 0.9f);
    m.d[db].curve = kS;
    m.d[db].min = 0.8f;
    m.d[db].max = 0.1f;                         // inverted
    std::snprintf(m.name, sizeof m.name, "Big Punch");
    macro_apply(s, set, 2);
    CHECK(near(synth_value(a), pdesc_from_norm(&s.params[a], dest_apply(m.d[0], 0.3f)), 1e-3f), "macro_apply sets the value");
    CHECK(near(value_norm(b), 0.8f + (0.1f - 0.8f) * curve_eval(kS, 0.3f), 1e-3f), "an inverted S destination");
    MacroSet back;
    const std::string text = macros_to_text(s, set);
    CHECK(macros_from_text(s, back, text.c_str()), "the macros' text reads");
    CHECK(std::strcmp(back.m[2].name, "Big Punch") == 0 && back.m[2].n == 2 && back.m[2].d[1].curve == kS && near(back.m[2].d[1].max, 0.1f, 1e-3f) &&
          near(back.m[2].pos, 0.3f, 1e-3f), "the macros' text reads back");
    macro_remove(m, 0);
    CHECK(m.n == 1 && dest_param(m.d[0]) == b, "removing a destination");
    int dropped = 0;
    CHECK(macros_from_text(s, back, "macro 1 \"x\" 0.5\ndest NOPE/Nothing 0 1 Lin\n", &dropped) && dropped == 1 && back.m[0].n == 0, "an unknown value is dropped");

    // a macro drives another one (framework/control/macro.h): macro 1 -> macro 3, which drives value b
    set.m[0].pos = 0.5f;
    const int l = macro_link(set, 0, 2);
    CHECK(l == 0 && dest_macro(set.m[0].d[0]) == 2 && near(dest_apply(set.m[0].d[0], 0.5f), 0.3f, 1e-5f), "a link keeps the macro it drives where it is");
    CHECK(macro_link(set, 0, 2) == 0 && set.m[0].n == 1, "the same link twice is one");
    CHECK(macro_link(set, 2, 0) == -1 && macro_link(set, 1, 1) == -1, "a link that loops back, or to itself, is refused");
    set.m[0].pos = 1.f;
    macro_apply(s, set, 0);
    CHECK(near(set.m[2].pos, dest_apply(set.m[0].d[0], 1.f), 1e-5f) && set.m[2].pos > 0.3f, "the macro it drives moves (%g)", (double)set.m[2].pos);
    CHECK(near(value_norm(b), 0.8f + (0.1f - 0.8f) * curve_eval(kS, set.m[2].pos), 1e-3f), "... and moves its own values");
    const std::string lt = macros_to_text(s, set);
    CHECK(lt.find("\nlink 3 ") != std::string::npos, "a link is written as a link line");
    CHECK(macros_from_text(s, back, lt.c_str()) && back.m[0].n == 1 && dest_macro(back.m[0].d[0]) == 2 &&
          near(back.m[0].d[0].max, set.m[0].d[0].max, 1e-3f), "the link reads back");
    CHECK(macros_from_text(s, back, "macro 1 \"a\" 0.5\nlink 2 0 1 Lin\nmacro 2 \"b\" 0.5\nlink 1 0 1 Lin\nlink 9 0 1 Lin\n", &dropped) &&
          dropped == 2 && back.m[0].n == 1 && back.m[1].n == 0, "a link that loops, or to no macro, is dropped (%d)", dropped);
}

// The sound file (framework/control/preset.h, what a board reads), on every synth: the values come back, a name with a space too (its
// path quoted: until 2026-11-15 it was not, and the value was lost on reading), and a line of that older kind still reads.
void test_sound_file() {
    for (int si = 0; si < lab::kSynthCount; si++) {
        const lab::Synth &s = *lab::kSynths[si];
        synth_select(si);
        std::vector<float> keep(static_cast<size_t>(s.n_params)), want(static_cast<size_t>(s.n_params));
        int spaced = -1;
        for (int i = 0; i < s.n_params; i++) {
            if (s.params[i].curve == lab::Head) continue;
            keep[i] = synth_value(i);
            set_value_norm(i, std::fmod(0.37f * static_cast<float>(i + 1) + 0.11f * static_cast<float>(si), 1.f));
            want[i] = synth_value(i);
            char path[64];
            param_path(s, i, path, sizeof path);
            if (spaced < 0 && std::strchr(path, ' ')) spaced = i;
        }
        const std::string text = sound_to_text(si);
        const size_t values = text.find("\n# SynthLab Studio macros");
        CHECK(si == modular_index() || (values != std::string::npos && values + 1 < STORAGE_FILE_MAX),
              "%s: its values take %d bytes, a board reads %d of a file", s.name, static_cast<int>(values), STORAGE_FILE_MAX - 1);
        int lines = 0;
        for (int i = 0; i < s.n_params; i++) {
            if (s.params[i].curve == lab::Head) continue;
            char q[72], line[120];
            quote_path(s, i, q, sizeof q);
            std::snprintf(line, sizeof line, "\nvalue %s %g\n", q, static_cast<double>(want[i]));
            lines += text.find(line) != std::string::npos;
            set_value_norm(i, 0.5f);
        }
        CHECK(lines == s.n_params - std::count_if(s.params, s.params + s.n_params, [](const lab::Param &p) { return p.curve == lab::Head; }),
              "%s: every value has its line (%d)", s.name, lines);
        sound_from_text(si, text.c_str());
        int back = 0;
        for (int i = 0; i < s.n_params; i++)
            if (s.params[i].curve != lab::Head) back += near(synth_value(i), want[i], 1e-5f * (s.params[i].max - s.params[i].min) + 1e-6f);
        CHECK(back == lines, "%s: %d of %d values come back from the text", s.name, back, lines);
        if (spaced >= 0) {
            char path[64], old[120];
            param_path(s, spaced, path, sizeof path);
            const lab::Param &p = s.params[spaced];
            std::snprintf(old, sizeof old, "value %s %g\n", path, static_cast<double>(p.max));     // (unquoted, as before)
            sound_from_text(si, old);
            CHECK(near(synth_value(spaced), p.max, 1e-6f), "%s: the unquoted %s reads", s.name, path);
        }
        for (int i = 0; i < s.n_params; i++) if (s.params[i].curve != lab::Head) synth_set_value(i, keep[i]);
    }
    synth_select(0);
}

// The SOUND tab's file block (studio.h: the saved sounds), in a folder of its own: a name, Save as under it, New, Load back, the
// export to the board's card (framework/control/preset.h: the name is in the file).
void test_sound_library() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "studio_selftest";
    fs::remove_all(root, ec);
    const std::string keep_dir = st.preset_dir;
    st.preset_dir = (root / "apps" / "studio" / "presets").string() + "/";
    const int si = 0;
    synth_select(si);
    const lab::Synth &s = *lab::kSynths[si];
    int v = -1;
    for (int i = 0; i < s.n_params && v < 0; i++) if (s.params[i].curve != lab::Head && s.params[i].max > s.params[i].def) v = i;
    if (v < 0) { st.preset_dir = keep_dir; return; }
    const float keep_v = synth_value(v);
    const MacroSet keep_m = st.macros[si];
    char keep_name[kSoundName];
    std::snprintf(keep_name, sizeof keep_name, "%s", st.sound_name[si]);

    std::snprintf(st.sound_name[si], kSoundName, "Hollow Kick");
    synth_set_value(v, s.params[v].max);
    std::string path;
    CHECK(save_sound_as(si, path) && path.find("hollow_kick.sound") != std::string::npos, "Save as: under its name (%s)", path.c_str());
    const std::vector<std::string> files = sound_files(si);
    CHECK(files.size() == 1 && files[0] == "hollow_kick", "the saved sounds list it (%zu)", files.size());
    new_sound(si);
    CHECK(std::strcmp(st.sound_name[si], "Init") == 0 && synth_value(v) == s.params[v].def, "New: Init, the defaults (%g)", synth_value(v));
    CHECK(load_sound(si, "hollow_kick", path) && std::strcmp(st.sound_name[si], "Hollow Kick") == 0 && synth_value(v) == s.params[v].max,
          "Load: its name and values back (%s, %g)", st.sound_name[si], synth_value(v));
    CHECK(!load_sound(si, "nothing_here", path), "Load of a file that is not there fails");
    CHECK(export_sound(si, path), "Export: written (%s)", path.c_str());
    char card[64];
    pset_file_name(s.name, ".sound", card, sizeof card);
    const fs::path on_card = root / "apps" / "synthlab" / "sdcard" / "synthlab" / "sounds" / card;
    FILE *f = std::fopen(on_card.string().c_str(), "rb");
    char text[256] = {};
    if (f) { const size_t n = std::fread(text, 1, sizeof text - 1, f); text[n] = 0; std::fclose(f); }
    CHECK(f && std::strstr(text, "name \"Hollow Kick\"\n"), "Export: in SynthLab's card folder, as the board reads it (%s)", on_card.string().c_str());

    st.preset_dir = keep_dir;
    synth_set_value(v, keep_v);
    st.macros[si] = keep_m;
    std::snprintf(st.sound_name[si], kSoundName, "%s", keep_name);
    st.dirty[si] = false;
    fs::remove_all(root, ec);
}

// The widgets, with input written by hand: a frame is wg_begin, the widgets, wg_end.
struct Rig {
    uint16_t px[200 * 104];
    canvas_t cv;
    wg_ctx_t c;
    wg_input_t in{};
    Rig() { CHECK(canvas_init(&cv, px, 200, 104), "the rig's canvas"); wg_init(&c, &cv); }
    void next() { in.pressed = in.released = 0; in.double_click = 0; in.wheel = 0; in.nkeys = 0; in.text[0] = 0; }
    void key(int k) { in.keys[in.nkeys++] = wg_key_t{static_cast<uint16_t>(k), 1, 0}; }
};

void test_widgets() {
    Rig r;
    float v = 0.5f, w = 0.2f;
    wg_knob_t k{};
    k.def = 0.5f;
    const gui_rect_t ka = gui_rect(10, 10, 46, 58), kb = gui_rect(60, 10, 46, 58);
    auto frame = [&] {
        wg_begin(&r.c, &r.in);
        wg_knob(&r.c, 1, ka, &v, &k);
        wg_knob(&r.c, 2, kb, &w, &k);
        wg_end(&r.c);
        r.next();
    };
    r.in.x = 33; r.in.y = 30; r.in.buttons = WG_BTN_LEFT; r.in.pressed = WG_BTN_LEFT;
    frame();
    r.in.y = 10;
    frame();                                    // dragged up 20 px
    CHECK(near(v, 0.6f, 1e-4f), "a knob dragged up 20 px moves 0.1 (%g)", (double)v);
    r.in.buttons = 0; r.in.released = WG_BTN_LEFT;
    frame();
    CHECK(r.c.focus == 1 && r.c.active == 0, "the knob pressed has the focus, nothing is active after the release");
    r.key(WG_KEY_TAB);
    frame();
    CHECK(r.c.focus == 2, "Tab moves the focus to the next widget (%d)", r.c.focus);
    r.key(WG_KEY_UP); r.key(WG_KEY_UP);
    frame();
    CHECK(near(w, 0.22f, 1e-4f), "two Up keys on the focused knob (%g)", (double)w);
    r.key(WG_KEY_DELETE);
    frame();
    CHECK(near(w, 0.5f, 1e-6f), "Delete: the default");
    r.in.x = 83; r.in.y = 30; r.in.wheel = -3;
    frame();
    CHECK(near(w, 0.47f, 1e-4f), "the wheel over a knob (%g)", (double)w);
    r.in.mods = WG_MOD_SHIFT; r.key(WG_KEY_TAB);
    frame();
    r.in.mods = 0;
    CHECK(r.c.focus == 1, "Shift+Tab goes back");

    // lines with ends off the canvas (a cable scrolled off the top or the left): only their visible part, no strokes from far away
    {
        auto lit = [&] { int n = 0; for (int i = 0; i < 200 * 104; i++) n += r.px[i] == 0xFFFF; return n; };
        canvas_clear(&r.cv, 0x0000);
        wg_line(&r.c, -100, 10, -10, 10, 0xFFFF);
        wg_line(&r.c, 20, -80, 60, -5, 0xFFFF);
        CHECK(lit() == 0, "a line off the top or the left draws nothing (%d pixels)", lit());
        wg_line(&r.c, -50, -50, 50, 50, 0xFFFF);
        const int n = lit();
        CHECK(n >= 50 && n <= 52 && r.px[10 * 200 + 10] == 0xFFFF && r.px[0 * 200 + 199] == 0, "a line across the top-left corner: its visible part only (%d pixels)", n);
        wg_line(&r.c, 150, -300, 150, 400, 0xFFFF);
        CHECK(lit() - n == 104, "a line through the canvas, both ends off it: the canvas's height (%d)", lit() - n);
        canvas_clear(&r.cv, 0x0000);
        wg_text(&r.c, -20, 40, "ABCDEFGH", 0xFFFF);         // (a module's title scrolled off the left: 6 px a letter, 48 px long)
        wg_text(&r.c, 30, 4, "ABCDEFGH", 0xFFFF);           // (and one off the top)
        int stray = 0;
        for (int y = 0; y < 104; y++)
            for (int x = 0; x < 200; x++)
                if (r.px[y * 200 + x] == 0xFFFF && !((x < 30 && y >= 30 && y <= 42) || (x >= 30 && x < 80 && y <= 6))) stray++;
        CHECK(stray == 0 && lit() > 0, "text off the top or the left: only its visible part (%d stray pixels)", stray);
        canvas_clear(&r.cv, 0x0000);
    }

    // drag and drop: a handle at (150, 80), a target over the first knob
    int got = -1;
    bool dropped = false;
    auto dnd_frame = [&] {
        wg_begin(&r.c, &r.in);
        int value;
        if (wg_drop_target(&r.c, ka, 7, &value)) { got = value; dropped = true; }
        wg_behavior(&r.c, 9, gui_rect(140, 75, 30, 10), false);
        wg_drag_source(&r.c, 9, 7, 42, "M3");
        wg_end(&r.c);
        r.next();
    };
    r.in.x = 150; r.in.y = 80; r.in.buttons = WG_BTN_LEFT; r.in.pressed = WG_BTN_LEFT;
    dnd_frame();
    r.in.x = 160;
    dnd_frame();
    CHECK(wg_dragging(&r.c, 7), "a drag starts once the pointer moved");
    r.in.x = 30; r.in.y = 40;
    dnd_frame();
    CHECK(!dropped, "nothing is dropped while the button is held");
    r.in.buttons = 0; r.in.released = WG_BTN_LEFT;
    dnd_frame();
    CHECK(dropped && got == 42 && !wg_dragging(&r.c, 7), "the payload is dropped on the target (%d)", got);
}

// The whole window, driven like a user: macro 1's handle dragged onto the Kick's first knob, the knob's ring dragged, the macro turned.
void test_app(wg_ctx_t &c, const char *shot) {
    const int kick = 2;
    synth_select(kick);
    if (!st.loaded[kick]) load_presets(kick);
    st.macros[kick] = MacroSet();
    st.tab = kTabSound;
    st.sound_scroll = 0;
    st.macro_sel = 0;
    const lab::Synth &s = cur();
    CHECK(std::strcmp(s.name, "Kick") == 0, "synth 2 is the Kick (%s)", s.name);
    int first = 0;
    while (first < s.n_params && s.params[first].curve == lab::Head) first++;
    wg_input_t in{};
    in.x = in.y = -100;
    auto step = [&] { run_frame(c, in); in.pressed = in.released = 0; in.double_click = 0; in.wheel = 0; in.nkeys = 0; };
    auto press = [&](int x, int y) { in.x = x; in.y = y; in.buttons = WG_BTN_LEFT; in.pressed = WG_BTN_LEFT; step(); };
    auto release = [&] { in.buttons = 0; in.released = WG_BTN_LEFT; step(); };
    step();
    // macro 1's handle (its cell at 4, 44; the handle 58 px under) onto the first knob (the first section's first cell: 208, 44)
    press(27, 106);
    in.x = 100; step();
    in.x = 231; in.y = 70; step();
    release();
    Macro &m = st.macros[kick].m[0];
    CHECK(m.n == 1 && dest_param(m.d[0]) == first, "the macro dropped on the first knob drives it (%d destination(s))", m.n);
    if (m.n != 1) return;
    const float before = value_norm(first);
    CHECK(near(dest_apply(m.d[0], m.pos), before, 0.02f), "assigning does not move the value");
    // the ring: 20 px above the knob's centre (231, 66), dragged up 20 px: Max + 0.1
    const float max0 = m.d[0].max;
    press(231, 46);
    in.y = 26; step();
    release();
    CHECK(near(m.d[0].max, std::fmin(1.f, max0 + 0.1f), 1e-3f), "the ring moves Max (%g -> %g)", (double)max0, (double)m.d[0].max);
    // the macro's knob (centre 27, 65) up 20 px: 50 % -> 60 %, and the value follows
    press(27, 65);
    in.y = 45; step();
    release();
    CHECK(near(m.pos, 0.6f, 1e-3f), "the macro's knob turned (%g)", (double)m.pos);
    CHECK(near(value_norm(first), dest_apply(m.d[0], m.pos), 0.01f), "the value follows the macro");
    in.x = 231; in.y = 70;
    step();
    CHECK(c.hint[0] != 0, "a hint for the knob under the pointer");
    if (shot) std::printf("%s %s\n", png_write_rgb565(shot, c.cv->px, c.cv->w, c.cv->h, shot_scale) ? "wrote" : "could not write", shot);
}
// The modular catalog, the patch, the runner, the Modular synth, the exports.
void test_modular() {
    int n;
    const mod::ModuleDef *const *cat = mod::catalog(&n);
    alignas(16) static unsigned char mem[1024 * 1024];
    static float ins[mod::kMaxIn][mod::kMaxFrames], outs[mod::kMaxOut][mod::kMaxFrames];
    mod::Noise noise;
    for (int m = 0; m < n; m++) {
        const mod::ModuleDef &d = *cat[m];
        CHECK(d.n_in <= mod::kMaxIn && d.n_out <= mod::kMaxOut && d.n_param <= mod::kMaxParams && d.size <= sizeof mem, "%s: within the limits", d.name);
        CHECK(mod::find_module(d.name) == &d && !std::strchr(d.name, ' '), "%s: found by its name", d.name);
        float v[mod::kMaxParams] = {};
        for (int j = 0; j < d.n_param; j++) {
            const mod::Param &q = d.param[j];
            CHECK(q.def >= q.min && q.def <= q.max && !std::strchr(q.name, ' '), "%s %s: its default in its range", d.name, q.name);
            CHECK(std::fabs(mod::from_norm(q, mod::to_norm(q, q.def)) - q.def) <= 1e-3f * (q.max - q.min) + 1e-4f, "%s %s: the knob's travel", d.name, q.name);
            v[j] = q.def;
        }
        if (d.construct_in) d.construct_in(mem, d.scope == mod::Scope::Global ? mod::kFxArea : mod::kVoiceArea);
        else d.construct(mem);
        mod::Ctx c;
        c.gate = true;
        d.start(mem, c);
        mod::IO io;
        for (int i = 0; i < mod::kMaxIn; i++) io.in[i] = ins[i];
        for (int o = 0; o < mod::kMaxOut; o++) io.out[o] = outs[o];
        io.p = v;
        io.frames = 32;
        float peak = 0.f;
        bool finite = true;
        for (int b = 0; b < 300; b++) {
            for (int i = 0; i < mod::kMaxIn; i++) for (int f = 0; f < 32; f++) ins[i][f] = (i == 0 && b < 150) ? 0.5f * noise.next() : (b == 0 && f == 0 ? 1.f : 0.f);
            d.process(mem, c, io);
            for (int o = 0; o < d.n_out; o++) for (int f = 0; f < 32; f++) { finite = finite && std::isfinite(outs[o][f]); peak = std::fmax(peak, std::fabs(outs[o][f])); }
        }
        d.destroy(mem);
        CHECK(finite && peak < 8.f, "%s: finite and bounded (peak %g)", d.name, (double)peak);
    }

    // the patch's text
    static mod::Patch p, back;
    p.init_default();
    static char t1[16384], t2[16384];
    mod::patch_to_text(p, t1, sizeof t1);
    char err[64];
    CHECK(mod::patch_from_text(back, t1, err, sizeof err), "the init patch reads back (%s)", err);
    mod::patch_to_text(back, t2, sizeof t2);
    CHECK(std::strcmp(t1, t2) == 0, "the init patch's text is the same after a round trip");
    int skipped = 0;
    CHECK(mod::patch_from_text(back, "patch \"x\"\nnode 1 voice Nope 0 0\nnode 2 voice Osc 0 0 Wave=9 Tune=-100\ncable 2.Out 9.In 1\n", err, sizeof err, &skipped) &&
          skipped == 2 && back.n_nodes == 1 && back.node[0].v[0] == 3.f && back.node[0].v[1] == -48.f, "bad lines are skipped, values kept in range");

    // the runner: the init patch sounds while the key is held, then falls silent
    alignas(16) static unsigned char vmem[512 * 1024];
    static mod::Runner r;
    CHECK(r.build(p, mod::kVoiceArea, vmem, sizeof vmem), "the init patch's voice is built");
    mod::Ctx c;
    c.hz = 220.f;
    c.gate = true;
    r.start(c);
    static float buf[48000];
    for (float &x : buf) x = 0.f;
    float peak_on = 0.f, peak_off = 0.f;
    for (int i = 0; i < 9600; i += 32) peak_on = std::fmax(peak_on, r.process(c, nullptr, nullptr, buf + i, 32));
    c.gate = false;
    for (int i = 9600; i < 48000; i += 32) peak_off = r.process(c, nullptr, nullptr, buf + i, 32);
    bool finite = true;
    for (float x : buf) finite = finite && std::isfinite(x);
    CHECK(finite && peak_on > 0.05f && peak_off < 1e-4f, "the init patch: a note, then silence (peaks %g, %g)", (double)peak_on, (double)peak_off);
    static mod::Runner fx;
    CHECK(fx.build(p, mod::kFxArea, vmem, sizeof vmem), "the init patch's FX is built");
    static float in[4800], out[4800];
    in[0] = 1.f;
    for (int i = 0; i < 4800; i += 32) fx.process(c, nullptr, in + i, out + i, 32);
    float tail = 0.f;
    for (int i = 100; i < 4800; i++) tail = std::fmax(tail, std::fabs(out[i]));
    CHECK(out[0] > 0.9f && tail > 1e-4f, "the FX: the dry hit and the Room's tail (%g, %g)", (double)out[0], (double)tail);

    // the Modular synth through the engine
    const int mi = modular_index();
    CHECK(mi >= 0, "the Modular synth is in the list");
    synth_select(mi);
    if (!st.loaded[mi]) load_presets(mi);
    st.patch.init_default();
    patch_apply();
    CHECK(std::strcmp(cur().params[0].name, "Note 1") == 0 && cur().n_params > 20, "the Modular synth lists the patch's values (%s, %d)", cur().params[0].name, cur().n_params);
    synth_note_on(57);
    static int16_t pcm[2 * 4096];
    int16_t pk = 0;
    for (int b = 0; b < 8; b++) {
        synth_render(pcm, 4096);
        for (int16_t s : pcm) pk = static_cast<int16_t>(std::max<int>(pk, std::abs(static_cast<int>(s))));
    }
    synth_note_off(57);
    CHECK(pk > 500, "the Modular synth plays through the engine (peak %d)", pk);

    // the exports
    const std::string syn = synth_source(st.patch, "init"), eng = engine_source(st.patch, "init");
    CHECK(syn.find("extern const lab::Synth synth_gen_init") != std::string::npos && syn.find("mod::OscM m2;") != std::string::npos, "the synth's source");
    CHECK(eng.find("bool build_init_graph(") != std::string::npos && eng.find("add(g, reg, 2, \"Osc\", sc::Scope::Voice)") != std::string::npos &&
          eng.find("Every module of the patch has an engine module") != std::string::npos, "the engine's source");
    static mod::Patch drum;
    drum.clear();
    drum.add(mod::find_module("Note"), mod::kVoiceArea, 0, 0);
    const int body = drum.add(mod::find_module("Body"), mod::kVoiceArea, 0, 0)->id;
    const int vo = drum.add(mod::find_module("VoiceOut"), mod::kVoiceArea, 0, 0)->id;
    drum.connect(body, 0, vo, 0, false, 1.f);
    CHECK(engine_source(drum, "d").find("NEW MODULES NEEDED") != std::string::npos && engine_source(drum, "d").find("Body 2") != std::string::npos,
          "a module the engine lacks is listed");
}

// The Ritual Machine's own modules in a patch (modular/engine_adapter.h): they follow the key, sound, and export as themselves.
void test_engine_modules() {
    int n;
    mod::catalog(&n);
    CHECK(n >= 50 && mod::find_module("RMOscEngines") && mod::find_module("RMReverb") && mod::find_module("RMFilter"), "the Ritual Machine's modules are in the catalog (%d modules)", n);
    if (!mod::find_module("RMOsc")) return;
    alignas(16) static unsigned char vmem[2 * 1024 * 1024];
    static mod::Runner r;
    static float buf[24000];
    // RMOsc on the key (its pitch input: the voice's by default): C4 has twice C3's cycles
    static mod::Patch p;
    p.clear();
    const int osc = p.add(mod::find_module("RMOsc"), mod::kVoiceArea, 0, 0)->id;
    const int out = p.add(mod::find_module("VoiceOut"), mod::kVoiceArea, 0, 0)->id;
    p.connect(osc, 0, out, 0, false, 1.f);
    int crossings[2] = {};
    for (int k = 0; k < 2; k++) {
        CHECK(r.build(p, mod::kVoiceArea, vmem, sizeof vmem), "RMOsc builds");
        mod::Ctx c;
        c.hz = k ? 261.6256f : 130.8128f;
        c.gate = true;
        r.start(c);
        for (float &x : buf) x = 0.f;
        for (int i = 0; i < 24000; i += 32) r.process(c, nullptr, nullptr, buf + i, 32);
        for (int i = 1; i < 24000; i++) crossings[k] += buf[i - 1] < 0.f && buf[i] >= 0.f;
    }
    const float ratio = static_cast<float>(crossings[1]) / static_cast<float>(crossings[0] ? crossings[0] : 1);
    CHECK(crossings[0] > 50 && ratio > 1.9f && ratio < 2.1f, "RMOsc follows the key (C3 %d cycles, C4 %d in 0.5 s)", crossings[0], crossings[1]);
    // RMOscEngines -> RMFilter: a sound
    p.clear();
    const int eng = p.add(mod::find_module("RMOscEngines"), mod::kVoiceArea, 0, 0)->id;
    const int flt = p.add(mod::find_module("RMFilter"), mod::kVoiceArea, 0, 0)->id;
    const int vo = p.add(mod::find_module("VoiceOut"), mod::kVoiceArea, 0, 0)->id;
    p.connect(eng, 0, flt, 0, false, 1.f);
    p.connect(flt, 0, vo, 0, false, 1.f);
    CHECK(r.build(p, mod::kVoiceArea, vmem, sizeof vmem), "RMOscEngines -> RMFilter builds");
    mod::Ctx c;
    c.hz = 220.f;
    c.gate = true;
    r.start(c);
    float peak = 0.f;
    bool finite = true;
    for (int i = 0; i < 24000; i += 32) { for (int f = 0; f < 32; f++) buf[i + f] = 0.f; peak = std::fmax(peak, r.process(c, nullptr, nullptr, buf + i, 32)); }
    for (float x : buf) finite = finite && std::isfinite(x);
    CHECK(finite && peak > 0.01f, "RMOscEngines -> RMFilter sounds (peak %g)", (double)peak);
    // the exports: the engine graph names the module itself, in its scope, with the key's pitch and gate; a synth cannot hold it
    const std::string g = engine_source(p, "rm");
    CHECK(g.find("add(g, reg, 1, \"OscEngines\", sc::Scope::Voice)") != std::string::npos && g.find("cable(g, reg, 254, \"pitch\", 1, \"pitch\"") != std::string::npos &&
          g.find("NEW MODULES NEEDED") == std::string::npos, "the engine export writes the Ritual Machine's modules as themselves");
    std::string path, err;
    CHECK(!export_synth(p, "rm_test", path, err) && err.find("RMOscEngines") != std::string::npos, "a synth export refuses them (%s)", err.c_str());
    // RMReverb on the sum: an impulse leaves a tail
    p.clear();
    const int vin = p.add(mod::find_module("Voices"), mod::kFxArea, 0, 0)->id;
    const int rv = p.add(mod::find_module("RMReverb"), mod::kFxArea, 0, 0)->id;
    const int mo = p.add(mod::find_module("Output"), mod::kFxArea, 0, 0)->id;
    p.connect(vin, 0, rv, 0, false, 1.f);
    p.connect(rv, 0, mo, 0, false, 1.f);
    CHECK(r.build(p, mod::kFxArea, vmem, sizeof vmem), "RMReverb builds in the FX area");
    static float in[24000];
    for (float &x : in) x = 0.f;
    in[0] = 0.5f;
    for (float &x : buf) x = 0.f;
    mod::Ctx g2;
    for (int i = 0; i < 24000; i += 32) r.process(g2, nullptr, in + i, buf + i, 32);
    float tail = 0.f;
    for (int i = 4800; i < 24000; i++) tail = std::fmax(tail, std::fabs(buf[i]));
    CHECK(tail > 1e-4f, "RMReverb leaves a tail (%g after 100 ms)", (double)tail);
    r.clear();
}

// Both areas: every module but the ends goes in either; the FX area plays the last note (mod::LastNote); the engine export names the
// ones the engine has in the other scope only.
void test_both_areas() {
    int n;
    const mod::ModuleDef *const *cat = mod::catalog(&n);
    int both = 0;
    for (int i = 0; i < n; i++) {
        const char *nm = cat[i]->name;
        const bool end = !std::strcmp(nm, "VoiceOut") || !std::strcmp(nm, "Voices") || !std::strcmp(nm, "Output");
        CHECK(end ? cat[i]->scope != mod::Scope::Both : cat[i]->scope == mod::Scope::Both, "%s: %s", nm, end ? "one area (an end)" : "both areas");
        both += cat[i]->scope == mod::Scope::Both;
    }
    CHECK(both == n - 3, "%d of %d modules go in both areas", both, n);

    // the last note: a start, the gate while a voice holds its key, the pitch of the last voice
    mod::LastNote ln;
    mod::Ctx c;
    uint32_t seen = 0;
    int va = 0, vb = 0;
    CHECK(!ln.fx_block(c, seen) && !c.gate, "the last note: nothing before a note");
    ln.note_start(&va, 220.f, 0.5f);
    ln.voice_block(&va, true, 220.f);
    CHECK(ln.fx_block(c, seen) && c.gate && c.hz == 220.f && c.velocity == 0.5f, "the last note: a start, its gate, pitch, velocity");
    ln.note_start(&vb, 330.f, 1.f);
    ln.voice_block(&va, true, 225.f);
    ln.voice_block(&vb, true, 331.f);
    CHECK(ln.fx_block(c, seen) && c.hz == 331.f, "the last note: the newest voice's pitch (%g)", (double)c.hz);
    ln.voice_block(&va, false, 225.f);
    ln.voice_block(&vb, false, 331.f);
    CHECK(!ln.fx_block(c, seen) && !c.gate, "the last note: the gate falls when no key is held");

    // on the FX area: Body through a VCA opened by a Perc, silent until a note, then a hit on it
    alignas(16) static unsigned char mem[4 * 1024 * 1024];
    static mod::Runner r;
    static mod::Patch p;
    p.clear();
    const int body = p.add(mod::find_module("Body"), mod::kFxArea, 0, 0)->id;
    const int perc = p.add(mod::find_module("Perc"), mod::kFxArea, 0, 0)->id;
    const int vca = p.add(mod::find_module("VCA"), mod::kFxArea, 0, 0)->id;
    const int out = p.add(mod::find_module("Output"), mod::kFxArea, 0, 0)->id;
    CHECK(body && perc && vca && out, "Body and Perc go on the FX area");
    p.connect(body, 0, vca, 0, false, 1.f);
    p.connect(perc, 0, vca, 1, false, 1.f);
    p.connect(vca, 0, out, 0, false, 1.f);
    CHECK(r.build(p, mod::kFxArea, mem, sizeof mem), "the FX patch builds");
    static float buf[4800];
    mod::LastNote fx;
    uint32_t fseen = 0;
    auto run = [&](int blocks) {
        float peak = 0.f;
        for (int b = 0; b < blocks; b++) {
            mod::Ctx g;
            if (fx.fx_block(g, fseen)) r.start(g);
            for (int f = 0; f < 32; f++) buf[f] = 0.f;
            peak = std::fmax(peak, r.process(g, nullptr, buf, buf + 32, 32));
        }
        return peak;
    };
    const float before = run(20);
    fx.note_start(&va, 110.f, 1.f);
    const float after = run(20);
    CHECK(before < 1e-4f && after > 0.05f, "a Perc on the FX area waits for a note, then hits (%g, %g)", (double)before, (double)after);
    r.clear();

    // the Ritual Machine's one-scope types in the other area: RMEg (a voice's) on the FX, RMDelay (global) in a voice
    if (mod::find_module("RMEg") && mod::find_module("RMDelay")) {
        p.clear();
        const int eg = p.add(mod::find_module("RMEg"), mod::kFxArea, 0, 0)->id;
        const int o2 = p.add(mod::find_module("Output"), mod::kFxArea, 0, 0)->id;
        p.connect(eg, 0, o2, 0, false, 1.f);
        CHECK(r.build(p, mod::kFxArea, mem, sizeof mem), "RMEg builds on the FX area");
        mod::Ctx g;
        g.gate = true;
        r.start(g);
        float peak = 0.f;
        for (int b = 0; b < 100; b++) { for (int f = 0; f < 32; f++) buf[f] = 0.f; peak = std::fmax(peak, r.process(g, nullptr, buf, buf + 32, 32)); }
        CHECK(peak > 0.1f, "RMEg on the FX area rises on the last note's gate (%g)", (double)peak);
        std::string eng = engine_source(p, "fxeg");
        CHECK(eng.find("NEW MODULES NEEDED") != std::string::npos && eng.find("the engine's Eg is per voice only") != std::string::npos,
              "the engine export lists RMEg on the FX as needed");
        p.clear();
        const int dl = p.add(mod::find_module("RMDelay"), mod::kVoiceArea, 0, 0)->id;
        const int vo = p.add(mod::find_module("VoiceOut"), mod::kVoiceArea, 0, 0)->id;
        const int osc = p.add(mod::find_module("Osc"), mod::kVoiceArea, 0, 0)->id;
        p.connect(osc, 0, dl, 0, false, 1.f);
        p.connect(dl, 0, vo, 0, false, 1.f);
        CHECK(mod::Runner::bytes_for(p, mod::kVoiceArea) <= kModularVoiceBytes && r.build(p, mod::kVoiceArea, mem, sizeof mem), "RMDelay builds in a voice (%u KB)",
              static_cast<unsigned>(mod::Runner::bytes_for(p, mod::kVoiceArea) >> 10));
        g = mod::Ctx{};
        g.gate = true;
        r.start(g);
        peak = 0.f;
        for (int b = 0; b < 100; b++) { for (int f = 0; f < 32; f++) buf[f] = 0.f; peak = std::fmax(peak, r.process(g, nullptr, nullptr, buf, 32)); }
        CHECK(peak > 0.01f, "RMDelay in a voice passes the sound (%g)", (double)peak);
        eng = engine_source(p, "vdl");
        CHECK(eng.find("the engine's Delay is global only") != std::string::npos && eng.find("add(g, reg, 3, \"Osc\", sc::Scope::Voice)") != std::string::npos,
              "the engine export lists RMDelay in a voice as needed, the rest as before");
        r.clear();
    }

    // a generated synth whose FX has an envelope: it reads the last note
    p.clear();
    p.init_default();
    const int fperc = p.add(mod::find_module("Perc"), mod::kFxArea, 0, 0)->id;
    CHECK(fperc > 0, "a Perc on the init patch's FX");
    const std::string syn = synth_source(p, "fxnote");
    CHECK(syn.find("mod::LastNote g_last;") != std::string::npos && syn.find("g_last.fx_block(c, seen)") != std::string::npos &&
          syn.find("g_last.note_start(this") != std::string::npos, "the generated synth's FX follows the last note");
}

// KEYS: the computer keyboard plays notes only while it is latched (F4 or the top bar's button).
void test_keys(wg_ctx_t &c) {
    wg_input_t in{};
    in.x = in.y = -100;
    auto key = [&](int k, bool down) { in.nkeys = 1; in.keys[0] = wg_key_t{static_cast<uint16_t>(k), static_cast<uint8_t>(down), 0}; run_frame(c, in); in.nkeys = 0; };
    st.octave = 0;
    st.keys_on = true;
    st.last_note = 0;
    key('z', true);
    CHECK(st.last_note == 48, "KEYS latched: Z plays C3 (%d)", st.last_note);
    key('z', false);
    key(WG_KEY_F4, true);
    key(WG_KEY_F4, false);
    CHECK(!st.keys_on, "F4 unlatches KEYS");
    key('x', true);
    CHECK(st.last_note == 48, "KEYS unlatched: X plays nothing (%d)", st.last_note);
    key('x', false);
    key(WG_KEY_F4, true);
    key(WG_KEY_F4, false);
    CHECK(st.keys_on, "F4 latches KEYS again");
    in.x = 359; in.y = 12; in.buttons = WG_BTN_LEFT; in.pressed = WG_BTN_LEFT;      // the button itself
    run_frame(c, in);
    in.buttons = 0; in.pressed = 0; in.released = WG_BTN_LEFT;
    run_frame(c, in);
    CHECK(!st.keys_on, "the KEYS button unlatches it");
    st.keys_on = true;
}

// The SynthLab synths rebuilt from modules (apps/studio/library): each nulls against its synth (nulltest.h).
void test_rebuilds() {
    struct { const char *synth; float min_db; } want[] = {{"Basic", 70.f}, {"FM2", 85.f}, {"Kick", 65.f}};
    for (const auto &w : want) {
        const lab::Synth *s = nullptr;
        for (int i = 0; i < lab::kSynthCount; i++) if (!std::strcmp(lab::kSynths[i]->name, w.synth)) s = lab::kSynths[i];
        CHECK(s && has_rebuild(*s), "%s has a rebuild in the library", w.synth);
        if (!s) continue;
        static mod::Patch p;
        CHECK(read_patch_file(library_path(*s), p), "%s's rebuild reads", w.synth);
        const NullResult r = null_test(*s, p);
        CHECK(r.depth_db >= w.min_db, "%s's rebuild nulls at %.1f dB (at least %.0f)", w.synth, (double)r.depth_db, (double)w.min_db);
    }
}

// The PATCH tab driven by hand: a module dragged from the list onto the canvas, a cable dragged from the Osc's output to the VCA's input.
void test_patch_tab(wg_ctx_t &c, const char *shot) {
    st.tab = kTabPatch;
    st.patch.init_default();
    patch_apply();
    st.patch_area = mod::kVoiceArea;
    st.pan_x[0] = st.pan_y[0] = 0;
    st.palette_scroll = 0;
    st.sel_node = 0;
    wg_input_t in{};
    in.x = in.y = -100;
    auto step = [&] { run_frame(c, in); in.pressed = in.released = 0; in.double_click = 0; in.wheel = 0; in.nkeys = 0; };
    auto press = [&](int x, int y) { in.x = x; in.y = y; in.buttons = WG_BTN_LEFT; in.pressed = WG_BTN_LEFT; step(); };
    auto release = [&] { in.buttons = 0; in.released = WG_BTN_LEFT; step(); };
    step();
    const int nodes = st.patch.n_nodes, cables = st.patch.n_cables;
    // the list: In / Out (title), Note, VoiceOut, Voices, Output, Sources (title), Osc: row 6, at y 40 + 66
    press(60, 40 + 6 * 11 + 5);
    in.x = 200; step();
    in.x = 420; in.y = 330; step();
    release();
    CHECK(st.patch.n_nodes == nodes + 1 && !std::strcmp(st.patch.node[nodes].def->name, "Osc"), "a module dragged onto the canvas (%d modules)", st.patch.n_nodes);
    // Osc 2 (at 150, 20; the view's origin at 160, 48): its output jack at (431, 86); VCA 5 (430, 40): its In at (592, 106)
    press(431, 86);
    in.x = 500; step();
    in.x = 592; in.y = 106; step();
    release();
    CHECK(st.patch.n_cables == cables + 1, "a cable dragged from an output onto an input (%d cables)", st.patch.n_cables);
    const mod::Cable &cb = st.patch.cable[st.patch.n_cables - 1];
    CHECK(cb.src == 2 && cb.dst == 5 && cb.dst_port == 0 && !cb.to_param, "it goes from Osc 2 to VCA 5's In");
    // the view moved so that Osc 2's header lies under the FX tab (x 226..296, y 25..39): the tab is drawn over it, and a click on it is
    // the tab's
    st.pan_x[0] = 80; st.pan_y[0] = 40;
    in.x = in.y = -100;
    step();
    CHECK(c.cv->px[30 * c.cv->w + 236] == c.theme.bg, "the area tabs are drawn over a module scrolled under them (%04x)", c.cv->px[30 * c.cv->w + 236]);
    press(261, 32);
    release();
    CHECK(st.patch_area == mod::kFxArea, "a click on the FX tab over a module is the tab's (area %d)", st.patch_area);
    st.patch_area = mod::kVoiceArea;
    st.pan_x[0] = st.pan_y[0] = 0;
    in.x = 300; in.y = 160;
    step();
    if (shot) std::printf("%s %s\n", png_write_rgb565(shot, c.cv->px, c.cv->w, c.cv->h, shot_scale) ? "wrote" : "could not write", shot);
}

// The math modules (Math, Func, Range, Slew) on known blocks, and the names of the engine's choices.
void test_math_and_names() {
    using namespace mod;
    constexpr int N = 8;
    float a[N], b[N], o[N], p[kMaxParams] = {};
    Ctx cx;
    IO io{};
    io.in[0] = a; io.in[1] = b; io.out[0] = o; io.p = p; io.frames = N;
    for (int i = 0; i < N; i++) { a[i] = -1.f + 0.25f * static_cast<float>(i); b[i] = 0.5f; }
    MathM m;
    p[P::MATH_OP] = P::MATH_SUB; p[P::MATH_B] = 0.25f; m.process(cx, io);
    CHECK(near(o[0], -1.75f, 1e-6f) && near(o[7], 0.f, 1e-6f), "Math A-B: B is its input + the B value (%g %g)", (double)o[0], (double)o[7]);
    p[P::MATH_OP] = P::MATH_MUL; p[P::MATH_B] = 0.f; m.process(cx, io);
    CHECK(near(o[0], -0.5f, 1e-6f), "Math AxB (%g)", (double)o[0]);
    p[P::MATH_OP] = P::MATH_GT; m.process(cx, io);
    CHECK(o[6] == 0.f && o[7] == 1.f, "Math A>B: 1 or 0 (%g %g)", (double)o[6], (double)o[7]);
    p[P::MATH_OP] = P::MATH_MAX; p[P::MATH_B] = -0.5f; m.process(cx, io);
    CHECK(o[0] == 0.f && near(o[7], 0.75f, 1e-6f), "Math Max");
    FuncM f;
    a[0] = 0.30f; a[1] = 1.25f; a[2] = -0.04f;
    p[P::FUNC_FN] = P::FN_QUANT; p[P::FUNC_STEPS] = 12; f.process(cx, io);
    CHECK(near(o[0], 4.f / 12.f, 1e-6f) && near(o[2], 0.f, 1e-6f), "Func Quant: to the nearest 1/12 (a semitone of a pitch in octaves) (%g)", (double)o[0]);
    p[P::FUNC_FN] = P::FN_WRAP; f.process(cx, io);
    CHECK(near(o[1], 0.25f, 1e-6f) && near(o[2], 0.96f, 1e-6f), "Func Wrap into 0..1 (%g %g)", (double)o[1], (double)o[2]);
    p[P::FUNC_FN] = P::FN_INV; f.process(cx, io);
    CHECK(near(o[0], 0.7f, 1e-6f), "Func 1-x");
    RangeM r;
    const ModuleDef *rd = find_module("Range");
    CHECK(rd && rd->n_param == 6, "the Range module");
    if (rd) for (int j = 0; j < rd->n_param; j++) p[j] = rd->param[j].def;
    a[0] = 0.f; a[1] = -1.f; a[2] = 2.f; a[3] = 0.5f;
    r.process(cx, io);
    CHECK(near(o[0], 0.5f, 1e-6f) && near(o[1], 0.f, 1e-6f) && near(o[2], 1.f, 1e-6f), "Range: -1..1 onto 0..1, clamped (%g %g %g)", (double)o[0], (double)o[1], (double)o[2]);
    p[P::RANGE_CLAMP] = 0; r.process(cx, io);
    CHECK(near(o[2], 1.5f, 1e-6f), "Range without Clamp goes past OutHi (%g)", (double)o[2]);
    p[P::RANGE_CURVE] = 100; r.process(cx, io);
    CHECK(near(o[0], 0.0625f, 1e-5f), "Range Curve +100 %%: 0.5^4 (%g)", (double)o[0]);
    p[P::RANGE_OUTLO] = 1; p[P::RANGE_OUTHI] = 0; p[P::RANGE_CURVE] = 0; r.process(cx, io);
    CHECK(near(o[3], 0.25f, 1e-6f), "Range OutLo above OutHi: upside down (%g)", (double)o[3]);
    // Slew: a step from 0 to 1, Lin at 10 ms a unit (480 samples at 48 kHz): half way after 240
    SlewM s;
    p[P::SLEW_RISE] = 10; p[P::SLEW_FALL] = 10; p[P::SLEW_MODE] = 1;
    for (int i = 0; i < N; i++) a[i] = 0.f;
    s.process(cx, io);
    for (int i = 0; i < N; i++) a[i] = 1.f;
    for (int k = 0; k < 30; k++) s.process(cx, io);
    CHECK(near(o[N - 1], 0.5f, 1e-3f), "Slew Lin: half a unit in half the time (%g)", (double)o[N - 1]);
    for (int k = 0; k < 40; k++) s.process(cx, io);
    CHECK(near(o[N - 1], 1.f, 1e-6f), "Slew Lin: there, and no further (%g)", (double)o[N - 1]);
    p[P::SLEW_MODE] = 0;
    for (int i = 0; i < N; i++) a[i] = 0.f;
    for (int k = 0; k < 60; k++) s.process(cx, io);
    CHECK(near(o[N - 1], 0.01f, 0.003f), "Slew Exp: about 99 %% of the way in Fall ms (%g)", (double)o[N - 1]);
    for (const char *name : {"Math", "Func", "Range", "Slew"}) {
        const ModuleDef *d = find_module(name);
        CHECK(d && !std::strcmp(d->category, "Math") && fits_area(d, kVoiceArea) && fits_area(d, kFxArea) && d->cpp, "%s: a Math module, in both areas, in a generated synth", name);
    }

    // the engine's choices by name (the adapter's table), not by number
    auto label = [](const char *module, const char *param, int v) -> const char * {
        const ModuleDef *d = find_module(module);
        const int j = d ? param_index(*d, param) : -1;
        if (j < 0 || !d->param[j].labels || d->param[j].curve != Int) return "(none)";
        return d->param[j].labels[v - static_cast<int>(d->param[j].min)];
    };
    CHECK(!std::strcmp(label("RMOscEngines", "engine", 0), "Karp") && !std::strcmp(label("RMOscEngines", "engine", sc::OSCX_ENGINES - 1), "Strng"),
          "RMOscEngines' engine by name (%s)", label("RMOscEngines", "engine", 0));
    CHECK(!std::strcmp(label("RMMiOsc", "model", 0), "CSaw") && !std::strcmp(label("RMMiOsc", "model", sc::MI_MODELS - 1), "PHat2"), "RMMiOsc's model by name");
    CHECK(!std::strcmp(label("RMFilter", "mode", 4), "AP") && !std::strcmp(label("RMFilter", "sections", 4), "48dB"), "RMFilter's mode and slope by name");
    CHECK(!std::strcmp(label("RMShaper", "mode", 9), "Decim") && !std::strcmp(label("RMDelay", "pingpong", 1), "On"), "RMShaper's mode, a 0..1 value Off / On");
    CHECK(!std::strcmp(label("RMShaper", "bits", 8), "(none)"), "a plain number stays a number (RMShaper's bits)");
    const char *unused = nullptr;
    const int nu = engine_names_unused(&unused);
    CHECK(nu == 0, "every list of the engine's names found its value (%d did not; the first: %s)", nu, unused ? unused : "-");
}

// The PATCH tab's editing by hand: a zone, Ctrl+C / Ctrl+V, Delete, a group drag, Ctrl+D, Ctrl+A, a jack's menu.
void test_patch_edit(wg_ctx_t &c) {
    st.tab = kTabPatch;
    mod::Patch &p = st.patch;
    p.clear();
    const int note = p.add(mod::find_module("Note"), mod::kVoiceArea, 0, 0)->id;
    const int osc = p.add(mod::find_module("Osc"), mod::kVoiceArea, 200, 0)->id;
    const int math = p.add(mod::find_module("Math"), mod::kVoiceArea, 200, 140)->id;
    const int out = p.add(mod::find_module("VoiceOut"), mod::kVoiceArea, 400, 0)->id;
    p.connect(note, 3, osc, 0, false, 1.f);
    p.connect(math, 0, osc, 1, false, 1.f);
    p.connect(osc, 0, out, 0, false, 1.f);
    patch_apply();
    st.patch_area = mod::kVoiceArea;
    st.pan_x[0] = st.pan_y[0] = 0;
    st.sel_node = 0;
    st.menu = false;
    c.focus = 0;
    wg_input_t in{};
    in.x = in.y = -100;
    auto step = [&] { run_frame(c, in); in.pressed = in.released = 0; in.double_click = 0; in.wheel = 0; in.nkeys = 0; };
    auto press = [&](int x, int y) { in.x = x; in.y = y; in.buttons = WG_BTN_LEFT; in.pressed = WG_BTN_LEFT; step(); };
    auto release = [&] { in.buttons = 0; in.released = WG_BTN_LEFT; step(); };
    auto ctrl_key = [&](int k) { in.mods = WG_MOD_CTRL; in.keys[0] = wg_key_t{static_cast<uint16_t>(k), 1, 0}; in.nkeys = 1; step(); in.mods = 0; };
    auto drag = [&](int x0, int y0, int x1, int y1) { press(x0, y0); in.x = (x0 + x1) / 2; in.y = (y0 + y1) / 2; step(); in.x = x1; in.y = y1; step(); release(); };
    // (the view's origin at 160, 48: a module at (x, y) is drawn from (160 + x, 48 + y))
    step();
    drag(340, 44, 370, 48 + 150);                 // a zone over Osc and Math (x 180..210), short of Note (0..124) and VoiceOut (400..)
    std::vector<int> sel = st.sel;
    std::sort(sel.begin(), sel.end());
    CHECK(sel.size() == 2 && sel[0] == osc && sel[1] == math, "a zone selects the modules it meets (%d selected)", static_cast<int>(sel.size()));
    ctrl_key('c');
    in.x = 560; in.y = 300; step();               // (the canvas: x 152..628 at the test's 800 x 480)
    const int n0 = p.n_nodes, c0 = p.n_cables;
    ctrl_key('v');
    CHECK(p.n_nodes == n0 + 2 && p.n_cables == c0 + 1, "Ctrl+V: the two modules and the cable between them (%d modules, %d cables)", p.n_nodes, p.n_cables);
    const mod::Node *pasted = p.find(st.sel_node);
    CHECK(st.sel.size() == 2 && pasted && pasted->id != osc && pasted->id != math, "the pasted modules are the selection");
    bool at_pointer = false;
    for (int id : st.sel) if (const mod::Node *n = p.find(id)) if (n->x == 400 && n->y == 252) at_pointer = true;
    CHECK(at_pointer, "pasted at the pointer (its top left)");
    in.keys[0] = wg_key_t{WG_KEY_DELETE, 1, 0}; in.nkeys = 1; step();
    CHECK(p.n_nodes == n0 && p.n_cables == c0, "Delete removes the selection, its cables with it (%d modules)", p.n_nodes);
    // a group drag: Osc and Math selected, Osc's header dragged 40 right, 20 down: both move
    drag(340, 44, 370, 48 + 150);
    const int mx = p.find(math)->x, my = p.find(math)->y;
    drag(160 + 200 + 30, 48 + 5, 160 + 240 + 30, 48 + 25);
    CHECK(p.find(osc)->x == 240 && p.find(osc)->y == 20 && p.find(math)->x == mx + 40 && p.find(math)->y == my + 20, "the selection moves together (Osc at %d, %d)", p.find(osc)->x, p.find(osc)->y);
    CHECK(st.sel.size() == 2, "a drag keeps the selection");
    press(160 + 240 + 30, 48 + 25); release();
    CHECK(st.sel.size() == 1 && st.sel_node == osc, "a click on a selected module's header: that module alone");
    ctrl_key('d');
    CHECK(p.n_nodes == n0 + 1 && st.sel_node != osc, "Ctrl+D duplicates the selection");
    ctrl_key('a');
    CHECK(static_cast<int>(st.sel.size()) == p.n_nodes, "Ctrl+A selects the area's modules (%d of %d)", static_cast<int>(st.sel.size()), p.n_nodes);
    in.keys[0] = wg_key_t{WG_KEY_ESC, 1, 0}; in.nkeys = 1; step();
    CHECK(st.sel.empty() && st.sel_node == 0, "Esc drops the selection");
    // a jack's menu: a right click on Osc's output (at 240, 20: the jack at 160 + 240 + 121, 48 + 20 + 18) opens it, "Remove cable" removes it
    const int jx = 160 + 240 + 121, jy = 48 + 20 + 18;
    int from_osc = 0;
    for (int k = 0; k < p.n_cables; k++) if (p.cable[k].src == osc) from_osc++;
    in.x = jx; in.y = jy; in.buttons = WG_BTN_RIGHT; in.pressed = WG_BTN_RIGHT; step();
    in.buttons = 0; in.released = WG_BTN_RIGHT; step();
    CHECK(st.menu && st.menu_node == osc && st.menu_out && st.menu_port == 0, "a right click on a jack opens its menu");
    int still = 0;
    for (int k = 0; k < p.n_cables; k++) if (p.cable[k].src == osc) still++;
    CHECK(from_osc == 1 && still == 1, "the right click alone removes nothing (%d cable(s))", still);
    press(st.menu_x + 20, st.menu_y + 20); release();
    still = 0;
    for (int k = 0; k < p.n_cables; k++) if (p.cable[k].src == osc) still++;
    CHECK(!st.menu && still == 0, "the menu's \"Remove cable\" removes it (%d left)", still);
    in.x = jx; in.y = jy; in.buttons = WG_BTN_RIGHT; in.pressed = WG_BTN_RIGHT; step();
    in.buttons = 0; in.released = WG_BTN_RIGHT; step();
    press(560, 400); release();
    CHECK(!st.menu, "a press elsewhere closes the menu");
    st.patch.init_default();
    patch_apply();
    st.sel_node = 0;
}

// The layout's kinds drawn alone on the board's screen: each draws, inside its rectangle only; a toggle, a choice and a fader follow
// their value; the text replaces the name; the file keeps the texts and the alignments.
canvas_t g_cv;
uint16_t g_cv_px[kScreenW * kScreenH];

int ink(int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = std::max(0, y0); y < std::min(kScreenH, y1); y++)
        for (int x = std::max(0, x0); x < std::min(kScreenW, x1); x++) n += g_cv_px[y * kScreenW + x] != 0;
    return n;
}

void draw_alone(const lab::Synth &s, const El &e, const float *vals, bool focus) {
    canvas_color(&g_cv, 0xFFFF, 0x0000);
    canvas_clear(&g_cv, 0x0000);
    Layout l;
    l.pages.assign(1, Page{});
    l.pages[0].els.push_back(e);
    layout_draw(canvas_u8g2(&g_cv), s, l, 0, vals, focus ? 0 : -1);
}

void test_layout_kinds() {
    CHECK(canvas_init(&g_cv, g_cv_px, kScreenW, kScreenH), "a canvas of the board's screen");
    int si = -1, ci = -1, vi = -1;                      // a synth with a choice of 2..6 named values and a continuous value
    for (int k = 0; k < lab::kSynthCount && si < 0; k++) {
        const lab::Synth &s = *lab::kSynths[k];
        int c2 = -1, v2 = -1;
        for (int i = 0; i < s.n_params; i++) {
            const lab::Param &p = s.params[i];
            if (c2 < 0 && p.curve == lab::Int && p.labels && pdesc_choices(&p) >= 2 && pdesc_choices(&p) <= 6) c2 = i;
            if (v2 < 0 && (p.curve == lab::Lin || p.curve == lab::Exp)) v2 = i;
        }
        if (c2 >= 0 && v2 >= 0) { si = k; ci = c2; vi = v2; }
    }
    CHECK(si >= 0, "a synth with a named choice and a continuous value");
    if (si < 0) return;
    const lab::Synth &s = *lab::kSynths[si];
    const lab::Param &cp = s.params[ci];
    float vals[lab::kMaxParams] = {};
    for (int i = 0; i < s.n_params; i++) vals[i] = s.params[i].curve == lab::Head ? 0.f : s.params[i].def;
    for (int k = 0; k < kKindCount; k++) {
        El e;
        e.kind = k;
        e.param = !kind_shows_value(k) ? -1 : (k == kChoice || k == kToggle ? ci : vi);
        kind_size(k, e.w, e.h);
        e.x = 20;
        e.y = 30;
        e.w = std::min(e.w, kScreenW - 24);
        if (k == kLabel || k == kBox) e.text = "Words";
        CHECK(el_has_value(s, e) == kind_shows_value(k), "%s: shows a value or not", kind_name(k));
        CHECK(kind_by_name(kind_name(k)) == k, "%s: its name reads back", kind_name(k));
        for (bool focus : {false, true}) {
            draw_alone(s, e, vals, focus);
            const int in = ink(e.x, e.y, e.x + e.w, e.y + e.h), all = ink(0, 0, kScreenW, kScreenH);
            CHECK(in > 0, "%s%s draws something", kind_name(k), focus ? " (focused)" : "");
            CHECK(all == in, "%s%s stays in its rectangle (%d pixels outside)", kind_name(k), focus ? " (focused)" : "", all - in);
        }
    }
    // a toggle fills when on; a fader rises with its value; a choice inverts the value's cell
    auto ink_at = [&](const El &e, float v) { const float keep = vals[e.param]; vals[e.param] = v; draw_alone(s, e, vals, false); vals[e.param] = keep; return ink(0, 0, kScreenW, kScreenH); };
    const El tg{kToggle, ci, 0, 0, 64, 10};
    CHECK(ink_at(tg, cp.max) > ink_at(tg, cp.min), "a toggle is filled when on");
    const El fd{kFader, vi, 0, 0, 24, 48};
    CHECK(ink_at(fd, s.params[vi].max) > ink_at(fd, s.params[vi].min) + 20, "a fader fills with its value");
    const El ch{kChoice, ci, 0, 0, kScreenW, 20};
    std::vector<uint16_t> lo, hi;
    ink_at(ch, cp.min); lo.assign(g_cv_px, g_cv_px + kScreenW * kScreenH);
    ink_at(ch, cp.max); hi.assign(g_cv_px, g_cv_px + kScreenW * kScreenH);
    int left_lo = 0, left_hi = 0;                       // the first cell's ink: inverted (much more ink) when it is the value
    const int cell = kScreenW / pdesc_choices(&cp);
    for (int y = 9; y < 20; y++) for (int x = 0; x < cell; x++) { left_lo += lo[y * kScreenW + x] != 0; left_hi += hi[y * kScreenW + x] != 0; }
    CHECK(left_lo > left_hi + 20, "a choice inverts its value's cell (%d against %d)", left_lo, left_hi);
    // the text replaces the name
    El row{kRow, vi, 0, 0, kScreenW, 10};
    CHECK(el_name(s, row) == s.params[vi].name, "a row shows its value's name");
    row.text = "Pitch";
    CHECK(el_name(s, row) == "Pitch", "its text replaces the name");
    El title{kTitle, -1, 0, 0, kScreenW, 12};
    CHECK(el_name(s, title) == s.name, "the top title is the synth's name");
    title.text = "My synth";
    CHECK(el_name(s, title) == "My synth", "and its text replaces it");
    // the file: every kind, texts (one with a space), alignments; a decoration is not on the encoders' path
    Layout l;
    l.pages.assign(1, Page{});
    for (int k = 0; k < kKindCount; k++) {
        El e;
        e.kind = k;
        e.param = !kind_shows_value(k) ? -1 : (k == kChoice || k == kToggle ? ci : vi);
        kind_size(k, e.w, e.h);
        e.y = k * 12;
        if (k % 3 == 0) e.text = "Text " + std::to_string(k);
        if (k == kLabel) { e.text = "Left words"; e.align = kRight; }
        if (k == kBox) e.align = kCenter;               // (an alignment without a text: the text is written as "")
        l.pages[0].els.push_back(e);
    }
    const std::string text = layout_to_text(s, l);
    Layout back;
    int dropped = -1;
    CHECK(layout_from_text(s, back, text.c_str(), &dropped) && dropped == 0 && layout_to_text(s, back) == text, "a layout with every kind reads back (%d dropped)", dropped);
    bool same = back.pages.size() == 1 && back.pages[0].els.size() == l.pages[0].els.size();
    for (size_t i = 0; same && i < l.pages[0].els.size(); i++) {
        const El &a = l.pages[0].els[i], &b = back.pages[0].els[i];
        same = a.kind == b.kind && a.param == b.param && a.text == b.text && a.align == b.align && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    }
    CHECK(same, "every element, its text and its alignment come back");
    for (int i : nav_order(s, l.pages[0])) CHECK(kind_shows_value(l.pages[0].els[i].kind), "the encoders visit values only (%s)", kind_name(l.pages[0].els[i].kind));
    CHECK(layout_from_text(s, back, "page\nrow - 0 0 128 10\n", &dropped) && dropped == 1, "a value's element without its value is left out");
}

// The LAYOUT tab by hand: "+ Label" (its text typed at once), a row's text by a double click, the inspector's Name and Text, "+ Box".
void test_layout_edit(wg_ctx_t &c) {
    const int kick = 2;
    synth_select(kick);
    if (!st.loaded[kick]) load_presets(kick);
    const lab::Synth &s = cur();
    const Layout saved = st.layouts[kick];
    const bool was_dirty = st.dirty[kick];
    int vi = 0;
    while (vi < s.n_params && s.params[vi].curve == lab::Head) vi++;
    Layout &l = st.layouts[kick];
    l.pages.assign(1, Page{});
    l.pages[0].els.push_back(El{kRow, vi, 0, 100, kScreenW, 10});
    st.tab = kTabLayout;
    st.try_mode = false;
    st.page = 0;
    st.snap = 4;
    st.sel_el = -1;
    st.editing = false;
    c.focus = 0;
    wg_input_t in{};
    in.x = in.y = -100;
    auto step = [&] { run_frame(c, in); in.pressed = in.released = 0; in.double_click = 0; in.wheel = 0; in.nkeys = 0; in.text[0] = 0; };
    auto press = [&](int x, int y) { in.x = x; in.y = y; in.buttons = WG_BTN_LEFT; in.pressed = WG_BTN_LEFT; step(); };
    auto release = [&] { in.buttons = 0; in.released = WG_BTN_LEFT; step(); };
    auto key = [&](int k) { in.keys[0] = wg_key_t{static_cast<uint16_t>(k), 1, 0}; in.nkeys = 1; step(); };
    auto type = [&](const char *t) { std::snprintf(in.text, sizeof in.text, "%s", t); step(); };
    const std::vector<El> &els = l.pages[0].els;
    step();
    // (the left panel's kinds from 6, 44: two columns of 82 px, rows of 17; "+ Label" the 9th, "+ Box" the 11th)
    press(45, 44 + 4 * 17 + 7); release();
    CHECK(els.size() == 2 && els.back().kind == kLabel && st.editing && st.edit_what == 2 && st.edit_text.empty(), "\"+ Label\" adds a label and its text is typed");
    type("Hi there");
    key(WG_KEY_ENTER);
    CHECK(!st.editing && els.size() == 2 && els.back().text == "Hi there", "Enter keeps the label's text (\"%s\")", els.size() == 2 ? els.back().text.c_str() : "?");
    step();
    CHECK(!st.editing, "and nothing starts typing again");
    // a double click on the row (the screen from 190, 46, twice its size: the row at y 100 is drawn from 46 + 200)
    in.double_click = 1;
    press(200, 46 + 205); release();
    CHECK(st.editing && st.sel_el == 0 && st.edit_text == s.params[vi].name, "a double click types the element's text, from its name (\"%s\")", st.edit_text.c_str());
    for (int k = 0; k < 40 && !st.edit_text.empty(); k++) key(WG_KEY_BACKSPACE);
    type("Gain");
    key(WG_KEY_ENTER);
    CHECK(els[0].text == "Gain" && els[0].param == vi, "the row's text replaces its name (\"%s\")", els[0].text.c_str());
    Layout back;
    CHECK(layout_from_text(s, back, layout_to_text(s, l).c_str()) && back.pages[0].els[0].text == "Gain" && back.pages[0].els[1].text == "Hi there", "the texts are saved");
    // the inspector under the screen (from 190, 374): its second row, the text, "Text" at 346, "Name" at 398
    press(420, 400); release();
    CHECK(els[0].text.empty(), "\"Name\" goes back to the value's name");
    press(370, 400); release();
    CHECK(st.editing && st.edit_text == s.params[vi].name, "\"Text\" types the text");
    key(WG_KEY_ENTER);
    step();
    CHECK(!st.editing && els[0].text.empty(), "Enter on its own name: no text of its own; the focused button is not pressed again");
    press(45, 44 + 5 * 17 + 7); release();
    CHECK(els.size() == 3 && els.back().kind == kBox && !st.editing, "\"+ Box\" adds a box");
    st.layouts[kick] = saved;
    st.dirty[kick] = was_dirty;
    st.sel_el = -1;
    st.tab = kTabSound;
}
}  // namespace

int studio_selftest(wg_ctx_t &c, const char *shot) {
    test_modular();
    test_rebuilds();
    test_engine_modules();
    test_both_areas();
    test_params();
    test_layouts();
    test_macros();
    test_sound_file();
    test_sound_library();
    test_widgets();
    test_app(c, shot);
    test_keys(c);
    std::string shot2 = shot ? std::string(shot) : std::string();
    if (shot2.size() > 4) shot2.insert(shot2.size() - 4, "_patch");
    test_patch_tab(c, shot ? shot2.c_str() : nullptr);
    test_math_and_names();
    test_patch_edit(c);
    test_layout_kinds();
    test_layout_edit(c);
    std::printf("%s: %d checks, %d failed\n", tst::failures() ? "FAIL" : "PASS", tst::checks(), tst::failures());
    return tst::failures() ? 1 : 0;
}
#endif  // PLATFORM_SIM
