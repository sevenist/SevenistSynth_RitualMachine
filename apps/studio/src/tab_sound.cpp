// The SOUND tab: the synth's values as knobs, grouped by its sections (they flow left to right, the wheel scrolls), and the eight
// macros on the left. A macro is assigned by dragging the handle under its knob onto a value's knob (or: a knob focused, Ctrl+1..8);
// the knob then shows the macro's range as a ring in the macro's colour (dragging the ring moves Max, with Shift Min; a right click
// removes it). The selected macro's destinations are listed under the macros, with Min, Max, Curve and the response.
#include <algorithm>
#include <cstdio>
#include <vector>
#include "params.h"
#include "studio.h"
#include "synth.h"

namespace studio {

namespace {
constexpr int kPanelW = 198;
constexpr int kCellW = 46, kCellH = 58;

bool inside(gui_rect_t r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
bool contains(gui_rect_t outer, gui_rect_t r) { return r.x >= outer.x && r.y >= outer.y && r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h; }

void fill_clipped(wg_ctx_t &c, gui_rect_t r, gui_rect_t clip, uint16_t color) {
    const int x0 = std::max(r.x, clip.x), y0 = std::max(r.y, clip.y);
    const int x1 = std::min(r.x + r.w, clip.x + clip.w), y1 = std::min(r.y + r.h, clip.y + clip.h);
    if (x1 > x0 && y1 > y0) wg_fill(&c, gui_rect(x0, y0, x1 - x0, y1 - y0), color);
}

void select_macro(int k) {
    if (st.macro_sel != k) { st.macro_sel = k; st.dest_sel = 0; }
}

// Adds (or with `toggle`, removes) macro k's destination on value i.
void assign(int k, int i, bool toggle) {
    Macro &m = st.macros[synth_selected()].m[k];
    char path[64];
    param_path(cur(), i, path, sizeof path);
    const int at = macro_find(m, i);
    if (at >= 0 && toggle) {
        macro_remove(m, at);
        message("%s no longer drives %s", m.name, path);
    } else {
        const int d = macro_add(m, i, value_norm(i));
        if (d < 0) { message("%s already drives %d values (the most)", m.name, kDests); return; }
        st.dest_sel = d;
        message("%s drives %s: drag the ring on its knob to set how far", m.name, path);
    }
    select_macro(k);
    mark_dirty();
}

void draw_macros(wg_ctx_t &c, gui_rect_t area) {
    const wg_theme_t &t = c.theme;
    const wg_input_t &in = *c.in;
    MacroSet &set = st.macros[synth_selected()];
    wg_fill(&c, gui_rect(area.x, area.y, kPanelW, area.h), t.panel);
    wg_text(&c, area.x + 8, area.y + 14, "MACROS", t.dim);

    for (int k = 0; k < kMacros; k++) {
        Macro &m = set.m[k];
        const gui_rect_t cell = gui_rect(area.x + 4 + (k % 4) * 48, area.y + 20 + (k / 4) * 70, 46, 68);
        if (k == st.macro_sel) wg_frame(&c, gui_inset(cell, -1), macro_color(k));
        char val[16], label[16];
        std::snprintf(val, sizeof val, "%d%%", static_cast<int>(m.pos * 100.f + 0.5f));
        std::snprintf(label, sizeof label, "%.9s", m.name);
        wg_knob_t kd{};
        kd.label = label;
        kd.value = val;
        kd.def = 0.5f;
        kd.color = macro_color(k);
        const int id = WG_ID(F_MACRO, k);
        if (wg_knob(&c, id, gui_rect(cell.x, cell.y, 46, 56), &m.pos, &kd) & WG_CHANGED) { macro_apply(cur(), m); mark_dirty(); }
        if (c.active == id || wg_has_focus(&c, id)) select_macro(k);
        if (c.hot == id) wg_set_hint(&c, "%s %s: drives %d value(s). Drag the handle under it onto a knob to add one", m.name, val, m.n);

        const int hid = WG_ID(F_HANDLE, k);
        const gui_rect_t h = gui_rect(cell.x + 5, cell.y + 58, 36, 8);
        const bool was = c.active == hid;
        wg_behavior(&c, hid, h, false);
        if (was && (in.released & WG_BTN_LEFT)) select_macro(k);
        wg_fill(&c, h, c.hot == hid ? t.line : t.panel2);
        for (int x = h.x + 6; x < h.x + h.w - 5; x += 4) { wg_fill(&c, gui_rect(x, h.y + 2, 2, 1), macro_color(k)); wg_fill(&c, gui_rect(x, h.y + 5, 2, 1), macro_color(k)); }
        wg_drag_source(&c, hid, kDndMacro, k, m.name);
        if (c.hot == hid) wg_set_hint(&c, "Drag onto a value's knob: %s drives it", m.name);
    }

    // the selected macro: its name, its destinations, the selected one's mapping and response
    Macro &m = cur_macro();
    int y = area.y + 166;
    wg_fill(&c, gui_rect(area.x + 6, y - 7, 8, 8), macro_color(st.macro_sel));
    if (st.editing) {
        std::string s = st.edit_text + "_";
        wg_text(&c, area.x + 18, y + 1, s.c_str(), t.accent);
    } else {
        wg_text(&c, area.x + 18, y + 1, m.name, t.text);
    }
    if (wg_button(&c, WG_ID(F_MACRO_ED, 0), gui_rect(area.x + 138, y - 9, 54, 13), st.editing ? "Done" : "Rename", st.editing)) {
        if (st.editing) { std::snprintf(m.name, sizeof m.name, "%s", st.edit_text.c_str()); st.editing = false; mark_dirty(); }
        else { st.editing = true; st.edit_what = 0; st.edit_text = m.name; }
    }
    y += 10;
    wg_text_small(&c, area.x + 6, y + 6, m.n ? "DESTINATIONS (click: select, x: remove)" : "NO DESTINATION YET", t.dim);
    y += 10;
    if (m.n == 0) {
        static const char *const help[] = {"Drag the handle under a macro", "onto a value's knob, or focus", "a knob (Tab, click) and press", "Ctrl+1..8. Then drag the ring", "around the knob: how far the", "macro moves it."};
        for (int i = 0; i < 6; i++) wg_text_small(&c, area.x + 8, y + 8 + i * 9, help[i], t.dim);
        return;
    }
    if (st.dest_sel >= m.n) st.dest_sel = m.n - 1;
    for (int j = 0; j < m.n; j++) {
        const gui_rect_t r = gui_rect(area.x + 4, y + j * 13, 172, 12);
        const int id = WG_ID(F_DEST, j);
        const bool was = c.active == id;
        wg_behavior(&c, id, r, true);
        if ((was && (in.released & WG_BTN_LEFT)) || wg_has_focus(&c, id)) st.dest_sel = j;
        const wg_keys_t keys = wg_focus_keys(&c, id);
        wg_fill(&c, r, j == st.dest_sel ? t.panel2 : (c.hot == id ? t.line : t.panel));
        char path[64], line[96], val[24];
        param_path(cur(), m.d[j].param, path, sizeof path);
        format_value(cur().params[m.d[j].param], synth_value(m.d[j].param), val, sizeof val);
        std::snprintf(line, sizeof line, "%.22s", path);
        wg_text_small(&c, r.x + 3, r.y + 9, line, j == st.dest_sel ? t.text : t.dim);
        wg_text_small(&c, r.x + r.w - 3 - wg_text_w(&c, val, true), r.y + 9, val, macro_color(st.macro_sel));
        if (wg_has_focus(&c, id)) wg_focus_ring(&c, gui_inset(r, -1));
        bool remove = keys.reset;
        if (wg_button(&c, WG_ID(F_DEST_X, j), gui_rect(r.x + r.w + 2, r.y, 14, 12), "x", false)) remove = true;
        if (remove) { macro_remove(m, j); mark_dirty(); return; }
    }
    y += m.n * 13 + 6;
    Dest &d = m.d[st.dest_sel];
    char text[32];
    const char *labels[2] = {"Min", "Max"};
    float *vals[2] = {&d.min, &d.max};
    for (int i = 0; i < 2; i++) {
        wg_text_small(&c, area.x + 6, y + 9, labels[i], t.dim);
        std::snprintf(text, sizeof text, "%d%%", static_cast<int>(*vals[i] * 100.f + 0.5f));
        if (wg_slider(&c, WG_ID(F_DEST_ED, i), gui_rect(area.x + 30, y, 162, 12), vals[i], i ? 1.f : 0.f, text, macro_color(st.macro_sel))) { macro_apply(cur(), m); mark_dirty(); }
        y += 15;
    }
    wg_text_small(&c, area.x + 6, y + 10, "Curve", t.dim);
    const int cs = wg_stepper(&c, WG_ID(F_DEST_ED, 2), gui_rect(area.x + 30, y, 162, 14), curve_name(d.curve));
    if (cs) { d.curve = (d.curve + cs + kCurveCount * 4) % kCurveCount; macro_apply(cur(), m); mark_dirty(); }
    y += 18;

    // the response: the macro's travel left to right, the value's range bottom to top; the macro where it is: a dot
    const gui_rect_t box = gui_rect(area.x + 4, y, kPanelW - 8, area.y + area.h - y - 4);
    if (box.h < 20) return;
    wg_fill(&c, box, t.bg);
    wg_frame(&c, box, t.line);
    const int x0 = box.x + 3, x1 = box.x + box.w - 4, y0 = box.y + 3, y1 = box.y + box.h - 4;
    for (int x = x0; x <= x1; x += 3) {
        wg_fill(&c, gui_rect(x, y1 - static_cast<int>(d.min * static_cast<float>(y1 - y0)), 1, 1), t.dim);
        wg_fill(&c, gui_rect(x, y1 - static_cast<int>(d.max * static_cast<float>(y1 - y0)), 1, 1), t.dim);
    }
    int px = x0, py = y1;
    for (int x = x0; x <= x1; x++) {
        const float v = dest_apply(d, static_cast<float>(x - x0) / static_cast<float>(x1 - x0));
        const int yy = y1 - static_cast<int>(v * static_cast<float>(y1 - y0) + 0.5f);
        if (x > x0) wg_line(&c, px, py, x, yy, macro_color(st.macro_sel));
        px = x; py = yy;
    }
    const int mx = x0 + static_cast<int>(m.pos * static_cast<float>(x1 - x0) + 0.5f);
    const int my = y1 - static_cast<int>(dest_apply(d, m.pos) * static_cast<float>(y1 - y0) + 0.5f);
    wg_fill(&c, gui_rect(mx - 2, my - 2, 5, 5), t.text);
}

struct Group { int title; std::vector<int> params; };

void draw_values(wg_ctx_t &c, gui_rect_t R) {
    const wg_theme_t &t = c.theme;
    const wg_input_t &in = *c.in;
    const lab::Synth &s = cur();
    MacroSet &set = st.macros[synth_selected()];
    std::vector<Group> groups;
    for (int i = 0; i < s.n_params; i++) {
        if (s.params[i].curve == lab::Head) { groups.push_back(Group{i, {}}); continue; }
        if (groups.empty()) groups.push_back(Group{-1, {}});
        groups.back().params.push_back(i);
    }

    const int maxcols = (R.w - 16) / kCellW;
    int x = R.x + 4, y = R.y + 4 - st.sound_scroll, line_h = 0;
    bool want = false;                          // the focused knob is out of view: the scroll that shows it
    int want_scroll = 0;
    for (const Group &gr : groups) {
        const int n = static_cast<int>(gr.params.size());
        if (n == 0) continue;
        const int cols = std::min(n, maxcols), rows = (n + cols - 1) / cols;
        const int bw = cols * kCellW + 8, bh = 16 + rows * kCellH + 4;
        if (x + bw > R.x + R.w - 8) { x = R.x + 4; y += line_h + 6; line_h = 0; }
        const gui_rect_t box = gui_rect(x, y, bw, bh);
        fill_clipped(c, box, R, t.panel);
        if (y + 12 >= R.y && y + 12 < R.y + R.h)
            wg_text_small(&c, x + 6, y + 11, gr.title >= 0 ? s.params[gr.title].name : s.name, t.accent);
        for (int j = 0; j < n; j++) {
            const int i = gr.params[j];
            const lab::Param &p = s.params[i];
            const gui_rect_t cell = gui_rect(x + 4 + (j % cols) * kCellW, y + 16 + (j / cols) * kCellH, kCellW, kCellH);
            const int id = WG_ID(F_KNOB, i);
            if (!contains(R, cell)) {           // out of view: Tab still reaches it (and the view follows)
                wg_focusable(&c, id);
                if (wg_has_focus(&c, id)) want = true, want_scroll = st.sound_scroll + (cell.y < R.y ? cell.y - R.y - 20 : cell.y + cell.h - (R.y + R.h) + 4);
                continue;
            }
            float v = value_norm(i);
            char val[24];
            format_value(p, synth_value(i), val, sizeof val);
            Macro &sel = set.m[st.macro_sel];
            const int di = macro_find(sel, i);
            uint16_t dots[kMacros];
            int nd = 0;
            for (int k = 0; k < kMacros; k++) if (macro_find(set.m[k], i) >= 0) dots[nd++] = macro_color(k);
            wg_knob_t kd{};
            kd.label = p.name;
            kd.value = val;
            kd.def = to_norm(p, p.def);
            kd.steps = knob_steps(p);
            if (di >= 0) { kd.ring = true; kd.ring_a = &sel.d[di].min; kd.ring_b = &sel.d[di].max; kd.ring_color = macro_color(st.macro_sel); }
            kd.dots = dots;
            kd.ndots = nd;
            const int res = wg_knob(&c, id, cell, &v, &kd);
            if (res & WG_CHANGED) set_value_norm(i, v);
            if (res & WG_RING_CHANGED) { st.dest_sel = di; macro_apply(s, sel); mark_dirty(); }
            if (c.hot == id && di >= 0)
                wg_set_hint(&c, "%s %s   %s: %d%%..%d%% (drag the ring; Shift: its start; right click: remove)", p.name, val, sel.name,
                            static_cast<int>(sel.d[di].min * 100.f + 0.5f), static_cast<int>(sel.d[di].max * 100.f + 0.5f));
            else if (c.hot == id)
                wg_set_hint(&c, "%s %s   (%g..%g%s%s)  drag, wheel, arrows; double click: default", p.name, val, static_cast<double>(p.min),
                            static_cast<double>(p.max), p.unit ? " " : "", p.unit ? p.unit : "");
            if ((in.pressed & WG_BTN_RIGHT) && inside(cell, in.x, in.y) && di >= 0) assign(st.macro_sel, i, true);
            if (wg_has_focus(&c, id) && (in.mods & WG_MOD_CTRL))
                for (int k = 0; k < kMacros; k++) if (wg_key_pressed(&in, '1' + k)) assign(k, i, true);
            int k;
            if (wg_drop_target(&c, cell, kDndMacro, &k)) assign(k, i, false);
        }
        x += bw + 6;
        line_h = std::max(line_h, bh);
    }
    const int content = y + line_h + 4 - (R.y + 4 - st.sound_scroll);
    const int max_scroll = std::max(0, content - R.h + 8);
    if (in.wheel && c.hot == 0 && inside(R, in.x, in.y)) st.sound_scroll -= in.wheel * 40;
    if (want) st.sound_scroll = want_scroll;
    st.sound_scroll = std::max(0, std::min(max_scroll, st.sound_scroll));
    if (max_scroll > 0) {                       // the scroll bar
        const int bar_h = std::max(20, R.h * R.h / (content + 8));
        const int by = R.y + (R.h - bar_h) * st.sound_scroll / max_scroll;
        wg_fill(&c, gui_rect(R.x + R.w - 5, R.y, 4, R.h), t.panel);
        wg_fill(&c, gui_rect(R.x + R.w - 5, by, 4, bar_h), t.line);
    }
}
}  // namespace

void draw_sound(wg_ctx_t &c, gui_rect_t area) {
    draw_macros(c, area);                       // (first: Tab visits the macros, then the values)
    draw_values(c, gui_rect(area.x + kPanelW + 2, area.y, area.w - kPanelW - 2, area.h));
}

}  // namespace studio
