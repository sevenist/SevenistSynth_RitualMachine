#ifdef PLATFORM_SIM
// studio.exe --selftest: the models (values <-> knob positions, macros, layouts, their text files) and the widgets (ui/widgets.h) driven by
// scripted pointer and keyboard input. Prints each failure and a total; returns 0 when everything passed.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include "export.h"
#include "modular/engine_adapter.h"
#include "modular/modules.h"
#include "modular_synth.h"
#include "nulltest.h"
#include "params.h"
#include "studio.h"
#include "synth.h"

using namespace studio;

bool write_png(const char *path, const uint16_t *frame, int fw, int fh, int scale);   // (SynthLab's tests/png.cpp)
extern int shot_scale;                                                                  // main.cpp: --scale

namespace {
int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

void test_params() {
    for (int si = 0; si < lab::kSynthCount; si++) {
        const lab::Synth &s = *lab::kSynths[si];
        for (int i = 0; i < s.n_params; i++) {
            const lab::Param &p = s.params[i];
            if (p.curve == lab::Head) continue;
            const float back = from_norm(p, to_norm(p, p.def));
            CHECK(near(back, p.def, 1e-3f * (p.max - p.min) + 1e-4f), "%s %s: default %g comes back as %g", s.name, p.name, (double)p.def, (double)back);
            CHECK(near(from_norm(p, 0.f), p.min, 1e-4f * (p.max - p.min) + 1e-5f) && near(from_norm(p, 1.f), p.max, 1e-3f * (p.max - p.min)), "%s %s: the ends", s.name, p.name);
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
    macro_apply(s, m);
    CHECK(near(synth_value(a), from_norm(s.params[a], dest_apply(m.d[0], 0.3f)), 1e-3f), "macro_apply sets the value");
    CHECK(near(value_norm(b), 0.8f + (0.1f - 0.8f) * curve_eval(kS, 0.3f), 1e-3f), "an inverted S destination");
    MacroSet back;
    const std::string text = macros_to_text(s, set);
    CHECK(macros_from_text(s, back, text.c_str()), "the macros' text reads");
    CHECK(std::strcmp(back.m[2].name, "Big Punch") == 0 && back.m[2].n == 2 && back.m[2].d[1].curve == kS && near(back.m[2].d[1].max, 0.1f, 1e-3f) &&
          near(back.m[2].pos, 0.3f, 1e-3f), "the macros' text reads back");
    macro_remove(m, 0);
    CHECK(m.n == 1 && m.d[0].param == b, "removing a destination");
    int dropped = 0;
    CHECK(macros_from_text(s, back, "macro 1 \"x\" 0.5\ndest NOPE/Nothing 0 1 Lin\n", &dropped) && dropped == 1 && back.m[0].n == 0, "an unknown value is dropped");
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
    CHECK(m.n == 1 && m.d[0].param == first, "the macro dropped on the first knob drives it (%d destination(s))", m.n);
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
    if (shot) std::printf("%s %s\n", write_png(shot, c.cv->px, c.cv->w, c.cv->h, shot_scale) ? "wrote" : "could not write", shot);
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
    in.x = 300; in.y = 160;
    step();
    if (shot) std::printf("%s %s\n", write_png(shot, c.cv->px, c.cv->w, c.cv->h, shot_scale) ? "wrote" : "could not write", shot);
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
    test_widgets();
    test_app(c, shot);
    test_keys(c);
    std::string shot2 = shot ? std::string(shot) : std::string();
    if (shot2.size() > 4) shot2.insert(shot2.size() - 4, "_patch");
    test_patch_tab(c, shot ? shot2.c_str() : nullptr);
    std::printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
#endif  // PLATFORM_SIM
