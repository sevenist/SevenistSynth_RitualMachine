// The PATCH tab: the Modular synth's patch (modular_synth.h) on a canvas. On the left the catalog (drag a module onto the canvas, or Enter /
// double click: it goes in the middle), the patch's file and the exports; in the middle the canvas, VOICE or FX (a module's header drags it,
// its values are rows like sliders, its jacks take cables: drag from an output onto an input, or onto a value for a modulation; drag an
// input's cable away to move or remove it; right click a jack: its cables go); on the right the selected module: what it is, its cables
// with their depths, a way to connect with the keyboard. Empty canvas: drag it to look around (or the wheel; Shift: sideways).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "export.h"
#include "modular/engine_adapter.h"
#include "nulltest.h"
#include "modular_synth.h"
#include "params.h"
#include "studio.h"
#include "synth.h"

namespace studio {

namespace {
constexpr int kPanelW = 150, kInspW = 170, kStripH = 16;
constexpr int kBoxW = 124, kHeadH = 13, kRowH = 10;

bool inside(gui_rect_t r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

uint16_t category_color(const char *cat) {
    if (!std::strcmp(cat, "Sources")) return CANVAS_HEX(0xFFA14A);
    if (!std::strcmp(cat, "Envelopes")) return CANVAS_HEX(0x9BE36A);
    if (!std::strcmp(cat, "Filters")) return CANVAS_HEX(0x5ED6E8);
    if (!std::strcmp(cat, "Shapers")) return CANVAS_HEX(0xFF6F7D);
    if (!std::strcmp(cat, "Utilities")) return CANVAS_HEX(0xA48BFF);
    if (!std::strcmp(cat, "Effects")) return CANVAS_HEX(0xE86BD9);
    return CANVAS_HEX(0xDCE2E8);
}
uint16_t cable_color(int src_id) {
    static const uint16_t c[6] = {CANVAS_HEX(0xFFC14A), CANVAS_HEX(0x5ED6E8), CANVAS_HEX(0xF27DD0), CANVAS_HEX(0x9BE36A), CANVAS_HEX(0xFF8A65), CANVAS_HEX(0xB39DFF)};
    return c[src_id % 6];
}

bool allowed(const mod::ModuleDef *d, int area) {
    return d->scope == mod::Scope::Both || (d->scope == mod::Scope::Voice) == (area == mod::kVoiceArea);
}

// Whether Export > Engine can write the module in that area: the engine has its type in that area's scope (yes), only in the other
// scope (a voice's Eg on the FX, a global Delay in a voice: the engine would need that module), or not at all.
enum EngFit { kEngNone, kEngOther, kEngYes };
EngFit engine_fit(const mod::ModuleDef *d, int area) {
    if (!d->engine) return kEngNone;
    if (mod::engine_type_in(d->engine, area)) return kEngYes;
    return mod::engine_type_in(d->engine, area == mod::kVoiceArea ? mod::kFxArea : mod::kVoiceArea) ? kEngOther : kEngNone;
}
const char *other_scope(int area) { return area == mod::kVoiceArea ? "global only" : "per voice only"; }

int rows_of(const mod::ModuleDef *d) { return std::max<int>(d->n_in, d->n_out); }
int box_h(const mod::ModuleDef *d) { return kHeadH + rows_of(d) * kRowH + 2 + d->n_param * kRowH + 3; }

// Where things are, on the screen, for the canvas's view.
struct View {
    gui_rect_t cv;
    int ox, oy;                                     // screen = node + o
    gui_rect_t box(const mod::Node &n) const { return gui_rect(n.x + ox, n.y + oy, kBoxW, box_h(n.def)); }
    gui_point_t jack(const mod::Node &n, bool out, int port) const {
        const gui_rect_t b = box(n);
        return gui_point_t{out ? b.x + b.w - 3 : b.x + 2, b.y + kHeadH + port * kRowH + 5};
    }
    gui_rect_t prow(const mod::Node &n, int j) const {
        const gui_rect_t b = box(n);
        return gui_rect(b.x + 3, b.y + kHeadH + rows_of(n.def) * kRowH + 2 + j * kRowH, kBoxW - 6, kRowH - 1);
    }
};

std::string edit_note;                              // what the last edit did (shown once)

void structural(const char *what) {                 // modules or cables changed
    patch_apply();
    if (what) message("%s", what);
}

int payload_of(int id, int port) { return id * 16 + port; }

void draw_cable(wg_ctx_t &c, gui_point_t a, gui_point_t b, uint16_t color, bool dotted) {
    const float dx = static_cast<float>(b.x - a.x), dy = static_cast<float>(b.y - a.y);
    const float sag = 12.f + std::sqrt(dx * dx + dy * dy) * 0.18f;
    const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f + sag;
    int px = a.x, py = a.y;
    const int steps = 20;
    for (int i = 1; i <= steps; i++) {
        const float t = static_cast<float>(i) / steps, u = 1.f - t;
        const int x = static_cast<int>(u * u * a.x + 2 * u * t * mx + t * t * b.x);
        const int y = static_cast<int>(u * u * a.y + 2 * u * t * my + t * t * b.y);
        if (!dotted || (i & 1)) { wg_line(&c, px, py, x, y, color); wg_line(&c, px, py + 1, x, y + 1, color); }
        px = x; py = y;
    }
}

/* ---------------- the canvas ---------------- */

void draw_node(wg_ctx_t &c, const View &v, mod::Node &n, int idx) {
    const wg_theme_t &t = c.theme;
    const wg_input_t &in = *c.in;
    mod::Patch &p = st.patch;
    const mod::ModuleDef *d = n.def;
    const gui_rect_t b = v.box(n);
    if (b.x > v.cv.x + v.cv.w || b.y > v.cv.y + v.cv.h || b.x + b.w < v.cv.x || b.y + b.h < v.cv.y) return;
    const bool pin = inside(v.cv, in.x, in.y);      // (the pointer on the canvas: its widgets may take it)
    const uint16_t cat = category_color(d->category);
    wg_fill(&c, b, t.panel);
    wg_frame(&c, b, n.id == st.sel_node ? t.accent : t.line);
    const gui_rect_t head = gui_rect(b.x + 1, b.y + 1, b.w - 2, kHeadH - 1);
    wg_fill(&c, head, wg_mix(t.panel2, cat, 70));
    char title[24];
    std::snprintf(title, sizeof title, "%s %d", d->name, n.id);
    wg_text(&c, head.x + 3, head.y + 9, title, t.text);

    // the header: drag moves, click selects; its x removes the module
    const int hid = WG_ID(F_NODE, n.id);
    const gui_rect_t xr = gui_rect(head.x + head.w - 10, head.y + 1, 9, 10);
    const bool was = c.active == hid;
    if (pin) wg_behavior(&c, hid, head, false);
    if (c.active == hid && !was) { st.sel_node = n.id; st.pan_mx = in.x - b.x; st.pan_my = in.y - b.y; }
    if (c.active == hid && (in.buttons & WG_BTN_LEFT)) {
        n.x = static_cast<int16_t>((in.x - st.pan_mx - v.ox) / 4 * 4);
        n.y = static_cast<int16_t>((in.y - st.pan_my - v.oy) / 4 * 4);
    }
    wg_text_small(&c, xr.x + 2, xr.y + 7, "x", c.hot == hid && inside(xr, in.x, in.y) ? t.warn : t.dim);
    if (was && (in.released & WG_BTN_LEFT) && inside(xr, in.x, in.y) && std::abs(in.x - (b.x + st.pan_mx)) < 3) {
        p.remove(n.id);
        st.sel_node = 0;
        structural("Module removed");
        return;
    }
    if (c.hot == hid) wg_set_hint(&c, "%s: %s", d->name, d->doc);

    // the jacks
    for (int side = 0; side < 2; side++) {
        const bool out = side == 1;
        const int count = out ? d->n_out : d->n_in;
        for (int k = 0; k < count; k++) {
            const gui_point_t j = v.jack(n, out, k);
            const gui_rect_t jr = gui_rect(j.x - 4, j.y - 4, 9, 9);
            int ncab = 0, last = -1;
            for (int q = 0; q < p.n_cables; q++) {
                const mod::Cable &cb = p.cable[q];
                if (out ? (cb.src == n.id && cb.src_port == k) : (cb.dst == n.id && !cb.to_param && cb.dst_port == k)) { ncab++; last = q; }
            }
            const char *name = out ? d->out_name[k] : d->in_name[k];
            if (out) wg_text_small(&c, b.x + b.w - 8 - wg_text_w(&c, name, true), j.y + 3, name, t.dim);
            else wg_text_small(&c, b.x + 8, j.y + 3, name, t.dim);
            const int jid = WG_ID(F_JACK, n.id * 32 + (out ? 16 : 0) + k);
            const bool jwas = c.active == jid;
            if (pin) wg_behavior(&c, jid, jr, false);
            const bool hot = c.hot == jid;
            wg_fill(&c, gui_rect(j.x - 3, j.y - 3, 7, 7), ncab ? cable_color(out ? n.id : p.cable[last].src) : t.bg);
            wg_frame(&c, gui_rect(j.x - 3, j.y - 3, 7, 7), hot ? t.accent : t.text);
            if (hot) wg_set_hint(&c, "%s %d.%s: %d cable(s). %s", d->name, n.id, name, ncab,
                                 out ? "Drag onto an input, or onto a value (modulation)" : "Drop a cable here; drag its cable away to move it; right click: none");
            if ((in.pressed & WG_BTN_RIGHT) && inside(jr, in.x, in.y) && ncab) {
                for (int q = p.n_cables - 1; q >= 0; q--) {
                    const mod::Cable &cb = p.cable[q];
                    if (out ? (cb.src == n.id && cb.src_port == k) : (cb.dst == n.id && !cb.to_param && cb.dst_port == k)) p.disconnect(q);
                }
                structural("Cables removed");
                return;
            }
            if (out) {
                char label[32];
                std::snprintf(label, sizeof label, "%s %d.%s", d->name, n.id, name);
                wg_drag_source(&c, jid, kDndJack, payload_of(n.id, k), label);
            } else {
                // pick up the last cable into this input: it leaves, and travels from its source
                if (c.active == jid && jwas && last >= 0 && (in.buttons & WG_BTN_LEFT) && std::abs(in.x - c.press_x) + std::abs(in.y - c.press_y) >= 4) {
                    const mod::Cable cb = p.cable[last];
                    const mod::Node *s = p.find(cb.src);
                    p.disconnect(last);
                    char label[32];
                    std::snprintf(label, sizeof label, "%s %d.%s", s ? s->def->name : "?", cb.src, s ? s->def->out_name[cb.src_port] : "?");
                    wg_drag_source(&c, jid, kDndJack, payload_of(cb.src, cb.src_port), label);
                    structural(nullptr);
                }
                int pay;
                if (wg_drop_target(&c, jr, kDndJack, &pay)) {
                    if (p.connect(pay / 16, pay % 16, n.id, k, false, 1.f)) structural("Connected");
                    else message("Not connected: the two modules are in different areas, or the input has too many cables");
                }
            }
        }
    }

    // the values: rows like sliders (drag sideways, Shift finer; wheel; double click: the module's default); a cable dropped on one
    // modulates it
    for (int j = 0; j < d->n_param; j++) {
        const gui_rect_t r = v.prow(n, j);
        const int slot = modular_slot(p, idx, j);
        const mod::Param &q = d->param[j];
        const int rid = WG_ID(F_PROW, n.id * 16 + j);
        float val = slot >= 0 ? synth_value(slot) : n.v[j];
        float pos = mod::to_norm(q, val);
        const bool rwas = c.active == rid;
        if (pin || wg_has_focus(&c, rid)) wg_behavior(&c, rid, r, true);
        else wg_focusable(&c, rid);
        const float before = pos;
        if (c.active == rid && !rwas) { c.press_v = pos; if (in.double_click) pos = c.press_v = mod::to_norm(q, q.def); }
        if (c.active == rid && (in.buttons & WG_BTN_LEFT)) {
            c.press_v += static_cast<float>(in.x - c.press_x) / static_cast<float>(r.w) * ((in.mods & WG_MOD_SHIFT) ? 0.2f : 1.f);
            c.press_x = in.x;
            c.press_v = std::max(0.f, std::min(1.f, c.press_v));
            pos = c.press_v;
        }
        const float step = q.curve == mod::Int ? 1.f / std::max(1.f, q.max - q.min) : ((in.mods & WG_MOD_SHIFT) ? 0.002f : 0.01f);
        if (c.hot == rid && in.wheel && c.active == 0) pos += static_cast<float>(in.wheel) * step;
        const wg_keys_t keys = wg_focus_keys(&c, rid);
        pos += static_cast<float>(keys.steps) * step;
        if (keys.reset) pos = mod::to_norm(q, q.def);
        pos = std::max(0.f, std::min(1.f, pos));
        if (pos != before) {
            val = mod::from_norm(q, pos);
            if (slot >= 0) synth_set_value(slot, val);
            n.v[j] = val;
            mark_dirty();
        }
        const bool hot = c.hot == rid;
        wg_fill(&c, r, hot ? t.line : t.panel2);
        wg_fill(&c, gui_rect(r.x, r.y, static_cast<int>(pos * static_cast<float>(r.w)), r.h), wg_mix(t.panel2, cat, 90));
        // the modulations onto it: a line from its value to where full scale takes it
        int nm = 0;
        for (int k = 0; k < p.n_cables; k++) {
            const mod::Cable &cb = p.cable[k];
            if (cb.dst != n.id || !cb.to_param || cb.dst_port != j) continue;
            const float to = std::max(0.f, std::min(1.f, pos + cb.depth));
            const int x0 = r.x + static_cast<int>(std::min(pos, to) * r.w), x1 = r.x + static_cast<int>(std::max(pos, to) * r.w);
            wg_fill(&c, gui_rect(x0, r.y + r.h - 2 - nm * 2, std::max(1, x1 - x0), 2), cable_color(cb.src));
            nm++;
        }
        char txt[24];
        wg_text_small(&c, r.x + 2, r.y + 7, q.name, t.text);
        if (slot >= 0) format_value(cur().params[slot], val, txt, sizeof txt);
        else std::snprintf(txt, sizeof txt, "%g", static_cast<double>(val));
        wg_text_small(&c, r.x + r.w - 2 - wg_text_w(&c, txt, true), r.y + 7, txt, t.text);
        if (wg_has_focus(&c, rid)) wg_focus_ring(&c, gui_inset(r, -1));
        if (hot) wg_set_hint(&c, "%s %d %s %s   drag sideways, wheel, arrows; double click: default; drop a cable: modulation", d->name, n.id, q.name, txt);
        int pay;
        if (wg_drop_target(&c, r, kDndJack, &pay)) {
            if (p.connect(pay / 16, pay % 16, n.id, j, true, 0.5f)) structural("Modulation: its depth is in the inspector (on the right)");
            else message("Not connected: the two modules are in different areas");
            st.sel_node = n.id;
        }
        if (rwas || c.active == rid) st.sel_node = n.id;
    }
}

void draw_canvas(wg_ctx_t &c, gui_rect_t area) {
    const wg_theme_t &t = c.theme;
    const wg_input_t &in = *c.in;
    mod::Patch &p = st.patch;
    const int a = st.patch_area;
    // the area tabs
    const gui_rect_t strip = gui_rect(area.x, area.y, area.w, kStripH);
    wg_fill(&c, strip, t.panel);
    if (wg_tab(&c, WG_ID(F_CANVAS, 1), gui_rect(strip.x + 2, strip.y + 1, 70, kStripH - 2), "VOICE", a == mod::kVoiceArea)) st.patch_area = mod::kVoiceArea;
    if (wg_tab(&c, WG_ID(F_CANVAS, 2), gui_rect(strip.x + 74, strip.y + 1, 70, kStripH - 2), "FX", a == mod::kFxArea)) st.patch_area = mod::kFxArea;
    if (c.hot == WG_ID(F_CANVAS, 1)) wg_set_hint(&c, "Copied for each voice: from Note to VoiceOut");
    if (c.hot == WG_ID(F_CANVAS, 2)) wg_set_hint(&c, "Once, on the sum of the voices: from Voices to Output");
    wg_text_small(&c, strip.x + 152, strip.y + 11, a == mod::kVoiceArea ? "per voice: Note -> ... -> VoiceOut" : "on the sum: Voices -> ... -> Output", t.dim);

    View v;
    v.cv = gui_rect(area.x, area.y + kStripH, area.w, area.h - kStripH);
    v.ox = v.cv.x + 8 - st.pan_x[a];
    v.oy = v.cv.y + 8 - st.pan_y[a];
    wg_fill(&c, v.cv, t.bg);
    for (int x = v.cv.x + ((-st.pan_x[a]) % 24 + 24) % 24; x < v.cv.x + v.cv.w; x += 24)
        for (int y = v.cv.y + ((-st.pan_y[a]) % 24 + 24) % 24; y < v.cv.y + v.cv.h; y += 24) wg_fill(&c, gui_rect(x, y, 1, 1), t.panel2);

    for (int i = 0; i < p.n_nodes; i++) {
        if (p.node[i].area != a) continue;
        const int before = p.n_nodes;
        draw_node(c, v, p.node[i], i);
        if (p.n_nodes != before) break;             // (a module was removed: the rest next frame)
    }
    // the cables, over the modules
    for (int k = 0; k < p.n_cables; k++) {
        const mod::Cable &cb = p.cable[k];
        const mod::Node *s = p.find(cb.src), *d = p.find(cb.dst);
        if (!s || !d || s->area != a) continue;
        const gui_point_t from = v.jack(*s, true, cb.src_port);
        gui_point_t to;
        if (cb.to_param) { const gui_rect_t r = v.prow(*d, cb.dst_port); to = gui_point_t{r.x, r.y + r.h / 2}; }
        else to = v.jack(*d, false, cb.dst_port);
        draw_cable(c, from, to, cable_color(cb.src), cb.to_param);
    }

    // the canvas itself: drag to look around; a module dropped on it
    const int bid = WG_ID(F_CANVAS, 0);
    const bool was = c.active == bid;
    wg_behavior(&c, bid, v.cv, false);
    if (c.active == bid && !was) { st.pan_mx = in.x; st.pan_my = in.y; }
    if (c.active == bid && (in.buttons & WG_BTN_LEFT)) {
        st.pan_x[a] -= in.x - st.pan_mx;
        st.pan_y[a] -= in.y - st.pan_my;
        st.pan_mx = in.x;
        st.pan_my = in.y;
    }
    if (was && (in.released & WG_BTN_LEFT) && std::abs(in.x - c.press_x) + std::abs(in.y - c.press_y) < 3) st.sel_node = 0;
    if (in.wheel && c.hot == bid) {
        if (in.mods & WG_MOD_SHIFT) st.pan_x[a] -= in.wheel * 40;
        else st.pan_y[a] -= in.wheel * 40;
    }
    int which;
    if (wg_drop_target(&c, v.cv, kDndModule, &which)) {
        int n;
        const mod::ModuleDef *d = mod::catalog(&n)[which];
        if (!allowed(d, a)) message("%s belongs in the %s area", d->name, a == mod::kVoiceArea ? "FX" : "VOICE");
        else if (mod::Node *nd = p.add(d, a, (in.x - v.ox) / 4 * 4, (in.y - v.oy) / 4 * 4)) { st.sel_node = nd->id; structural(nullptr); }
        else message("The patch is full (%d modules)", mod::kMaxNodes);
    }
    // keys, when nothing else has the focus: Delete removes the selection, Ctrl+D copies it, the arrows move the view
    if (c.focus == 0 && !st.editing) {
        mod::Node *sel = p.find(st.sel_node);
        if (sel && (wg_key_pressed(&in, WG_KEY_DELETE) || wg_key_pressed(&in, WG_KEY_BACKSPACE))) { p.remove(sel->id); st.sel_node = 0; structural("Module removed"); }
        else if (sel && (in.mods & WG_MOD_CTRL) && wg_key_pressed(&in, 'd')) {
            const mod::Node copy = *sel;
            if (mod::Node *nd = p.add(copy.def, copy.area, copy.x + 16, copy.y + 16)) {
                std::memcpy(nd->v, copy.v, sizeof nd->v);
                st.sel_node = nd->id;
                structural("Module copied (without its cables)");
            }
        }
        if (wg_key_pressed(&in, WG_KEY_LEFT)) st.pan_x[a] -= 40;
        if (wg_key_pressed(&in, WG_KEY_RIGHT)) st.pan_x[a] += 40;
        if (wg_key_pressed(&in, WG_KEY_UP)) st.pan_y[a] -= 40;
        if (wg_key_pressed(&in, WG_KEY_DOWN)) st.pan_y[a] += 40;
    }
}

/* ---------------- the catalog and the patch's file ---------------- */

std::vector<std::string> patch_files() {
    std::vector<std::string> f;
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator(patch_dir(), ec))
        if (e.path().extension() == ".patch") f.push_back(e.path().stem().string());
    std::sort(f.begin(), f.end());
    return f;
}

void add_module(int which) {
    int n;
    const mod::ModuleDef *d = mod::catalog(&n)[which];
    const int a = allowed(d, st.patch_area) ? st.patch_area : (st.patch_area == mod::kVoiceArea ? mod::kFxArea : mod::kVoiceArea);
    int x = 40 + st.pan_x[a], y = 40 + st.pan_y[a];
    for (int i = 0; i < st.patch.n_nodes; i++)          // (not on top of another one)
        if (st.patch.node[i].area == a && std::abs(st.patch.node[i].x - x) < 20 && std::abs(st.patch.node[i].y - y) < 20) { x += 24; y += 24; i = -1; }
    if (mod::Node *nd = st.patch.add(d, a, x, y)) {
        st.patch_area = a;
        st.sel_node = nd->id;
        structural(nullptr);
    } else {
        message("The patch is full (%d modules)", mod::kMaxNodes);
    }
}

void draw_left(wg_ctx_t &c, gui_rect_t area) {
    const wg_theme_t &t = c.theme;
    const wg_input_t &in = *c.in;
    wg_fill(&c, gui_rect(area.x, area.y, kPanelW, area.h), t.panel);
    wg_text(&c, area.x + 6, area.y + 12, "MODULES", t.dim);
    int n;
    const mod::ModuleDef *const *cat = mod::catalog(&n);
    // the rows: a category's title, then its modules
    struct Row { const char *title; int module; };
    std::vector<Row> rows;
    const char *last = nullptr;
    for (int i = 0; i < n; i++) {
        if (!last || std::strcmp(last, cat[i]->category)) { rows.push_back(Row{cat[i]->category, -1}); last = cat[i]->category; }
        rows.push_back(Row{nullptr, i});
    }
    const int patch_y = area.y + area.h - 172;          // (the patch block at the panel's bottom: the list takes the height above it)
    const gui_rect_t list = gui_rect(area.x + 2, area.y + 16, kPanelW - 4, patch_y - 6 - (area.y + 16));
    const int visible = list.h / 11;
    if (in.wheel && inside(list, in.x, in.y)) st.palette_scroll -= in.wheel * 2;
    st.palette_scroll = std::max(0, std::min(static_cast<int>(rows.size()) - visible, st.palette_scroll));
    for (int r = 0; r < static_cast<int>(rows.size()); r++) {
        const Row &row = rows[r];
        const int y = list.y + (r - st.palette_scroll) * 11;
        const bool shown = r >= st.palette_scroll && r < st.palette_scroll + visible;
        if (row.module < 0) { if (shown) wg_text_small(&c, list.x + 2, y + 8, row.title, category_color(row.title)); continue; }
        const mod::ModuleDef *d = cat[row.module];
        const int id = WG_ID(F_PALETTE, row.module);
        if (!shown) { wg_focusable(&c, id); if (wg_has_focus(&c, id)) st.palette_scroll = r < st.palette_scroll ? r : r - visible + 1; continue; }
        const gui_rect_t rr = gui_rect(list.x, y, list.w, 10);
        wg_behavior(&c, id, rr, true);
        const bool ok = allowed(d, st.patch_area);
        wg_fill(&c, rr, c.hot == id ? t.line : t.panel);
        wg_fill(&c, gui_rect(rr.x + 3, rr.y + 3, 4, 4), category_color(d->category));
        wg_text_small(&c, rr.x + 11, rr.y + 8, d->name, ok ? t.text : t.dim);
        const EngFit fit = engine_fit(d, st.patch_area);     // (dimmed: the engine has it in the other area only)
        if (mod::is_engine_module(d)) wg_text_small(&c, rr.x + rr.w - 3 - wg_text_w(&c, "RM", true), rr.y + 8, "RM", fit == kEngYes ? CANVAS_HEX(0xF2D64B) : CANVAS_HEX(0x7A6E33));
        else if (d->engine) wg_text_small(&c, rr.x + rr.w - 3 - wg_text_w(&c, "eng", true), rr.y + 8, "eng", fit == kEngYes ? t.dim : t.line);
        if (wg_has_focus(&c, id)) wg_focus_ring(&c, gui_inset(rr, -1));
        if (wg_focus_keys(&c, id).enter || (inside(rr, in.x, in.y) && (in.pressed & WG_BTN_LEFT) && in.double_click)) add_module(row.module);
        wg_drag_source(&c, id, kDndModule, row.module, d->name);
        if (c.hot == id) {
            if (fit == kEngOther) wg_set_hint(&c, "%s: %s  (the engine's %s is %s: here it plays, Export > Engine lists it as needed)", d->name, d->doc, d->engine, other_scope(st.patch_area));
            else wg_set_hint(&c, "%s: %s%s", d->name, d->doc, mod::is_engine_module(d) ? "" : (d->engine ? "  (eng: the engine has it)" : ""));
        }
    }

    // the patch: its name, its file, the exports
    int y = patch_y;
    char text[64];
    wg_fill(&c, gui_rect(area.x + 2, y - 2, kPanelW - 4, 1), t.line);
    wg_text_small(&c, area.x + 6, y + 8, "PATCH", t.dim);
    if (st.editing && st.edit_what == 1) { std::string s = st.edit_text + "_"; wg_text_small(&c, area.x + 36, y + 8, s.c_str(), t.accent); }
    else wg_text_small(&c, area.x + 36, y + 8, st.patch.name, t.text);
    y += 12;
    if (wg_button(&c, WG_ID(F_PATCH_BTN, 0), gui_rect(area.x + 4, y, 70, 14), st.editing && st.edit_what == 1 ? "Done" : "Rename", false)) {
        if (st.editing) { std::snprintf(st.patch.name, sizeof st.patch.name, "%s", st.edit_text.c_str()); st.editing = false; }
        else { st.editing = true; st.edit_what = 1; st.edit_text = st.patch.name; }
    }
    if (wg_button(&c, WG_ID(F_PATCH_BTN, 1), gui_rect(area.x + 76, y, 70, 14), "New", false)) {
        st.patch.init_default();
        st.sel_node = 0;
        structural("A new patch: Osc -> Filter -> VCA (ADSR), and a Room on the sum");
    }
    y += 17;
    if (wg_button(&c, WG_ID(F_PATCH_BTN, 2), gui_rect(area.x + 4, y, 142, 14), "Save as <name>.patch", false)) {
        patch_sync();
        const std::string path = patch_dir() + patch_ident(st.patch.name) + ".patch";
        if (save_patch(path)) message("Saved %s", path.c_str()); else message("Could not write %s", path.c_str());
    }
    y += 17;
    const std::vector<std::string> files = patch_files();
    if (!files.empty()) st.patch_file = std::max(0, std::min(static_cast<int>(files.size()) - 1, st.patch_file));
    std::snprintf(text, sizeof text, "%s", files.empty() ? "(no file yet)" : files[st.patch_file].c_str());
    st.patch_file += wg_stepper(&c, WG_ID(F_PATCH_BTN, 3), gui_rect(area.x + 4, y, 100, 14), text);
    if (wg_button(&c, WG_ID(F_PATCH_BTN, 4), gui_rect(area.x + 106, y, 40, 14), "Load", false) && !files.empty()) {
        const std::string path = patch_dir() + files[st.patch_file] + ".patch";
        if (load_patch(path)) message("Loaded %s", path.c_str()); else message("Could not read %s", path.c_str());
    }
    y += 17;
    // a SynthLab synth rebuilt from modules (apps/studio/library): opened as the patch, its null depth against the synth itself
    std::vector<int> rebuilt;
    for (int i = 0; i < lab::kSynthCount; i++) if (lab::kSynths[i] != &synth_modular && has_rebuild(*lab::kSynths[i])) rebuilt.push_back(i);
    if (!rebuilt.empty()) {
        const int nr = static_cast<int>(rebuilt.size());
        st.open_synth = ((st.open_synth % nr) + nr) % nr;
        const lab::Synth &os = *lab::kSynths[rebuilt[static_cast<size_t>(st.open_synth)]];
        st.open_synth += wg_stepper(&c, WG_ID(F_PATCH_BTN, 8), gui_rect(area.x + 4, y, 100, 14), os.name);
        if (c.hot == WG_ID(F_PATCH_BTN, 8)) wg_set_hint(&c, "A SynthLab synth rebuilt from modules (apps/studio/library): open it to rewire it");
        if (wg_button(&c, WG_ID(F_PATCH_BTN, 9), gui_rect(area.x + 106, y, 40, 14), "Open", false)) {
            static mod::Patch rp;
            if (read_patch_file(library_path(os), rp)) {
                const NullResult r = null_test(os, rp);
                st.patch = rp;
                st.sel_node = 0;
                patch_apply();
                message("%s rebuilt from %d modules: null depth %.1f dB against the synth itself (60 dB and more: the same sound)", os.name,
                        rp.n_nodes, static_cast<double>(r.depth_db));
            } else {
                message("Could not read %s", library_path(os).c_str());
            }
        }
        if (c.hot == WG_ID(F_PATCH_BTN, 9)) wg_set_hint(&c, "Opens %s as the Modular synth's patch (Save as keeps your changes apart)", os.name);
    }
    y += 21;
    wg_text_small(&c, area.x + 6, y + 6, "EXPORT", t.dim);
    y += 10;
    std::string out, err;
    if (wg_button(&c, WG_ID(F_PATCH_BTN, 5), gui_rect(area.x + 4, y, 70, 14), "Synth", false)) {
        patch_sync();
        if (export_synth(st.patch, patch_ident(st.patch.name), out, err)) message("Wrote %s (built at SynthLab's next build)", out.c_str());
        else message("Export failed: %s", err.c_str());
    }
    if (c.hot == WG_ID(F_PATCH_BTN, 5)) wg_set_hint(&c, "A SynthLab synth (C++, apps/synthlab/src/synths/gen_<name>.cpp) that plays this patch without the runner");
    if (wg_button(&c, WG_ID(F_PATCH_BTN, 6), gui_rect(area.x + 76, y, 70, 14), "Engine", false)) {
        patch_sync();
        if (export_engine(st.patch, patch_ident(st.patch.name), out, err)) message("Wrote %s", out.c_str());
        else message("Export failed: %s", err.c_str());
    }
    if (c.hot == WG_ID(F_PATCH_BTN, 6)) wg_set_hint(&c, "The engine's graph (C++ for the Ritual Machine), and what the engine lacks: apps/studio/exports/");
    y += 17;
    if (wg_button(&c, WG_ID(F_PATCH_BTN, 7), gui_rect(area.x + 4, y, 142, 14), "Patch file", false)) {
        patch_sync();
        if (export_patch(st.patch, patch_ident(st.patch.name), out, err)) message("Wrote %s", out.c_str());
        else message("Export failed: %s", err.c_str());
    }
    if (c.hot == WG_ID(F_PATCH_BTN, 7)) wg_set_hint(&c, "The patch as text (modules, values, cables), in apps/studio/exports/");
}

/* ---------------- the inspector ---------------- */

struct Target { int node, port; bool param; };

void draw_inspector(wg_ctx_t &c, gui_rect_t r) {
    const wg_theme_t &t = c.theme;
    mod::Patch &p = st.patch;
    wg_fill(&c, r, t.panel);
    const int x = r.x + 6;
    int y = r.y + 12;
    char text[96];
    const int si = p.index_of(st.sel_node);
    if (si < 0) {
        wg_text(&c, x, y, "THE PATCH", t.dim);
        y += 14;
        int nv = 0, nf = 0, newmods = 0;
        for (int i = 0; i < p.n_nodes; i++) { (p.node[i].area == mod::kVoiceArea ? nv : nf)++; if (engine_fit(p.node[i].def, p.node[i].area) != kEngYes) newmods++; }
        std::snprintf(text, sizeof text, "%d modules a voice, %d on the sum", nv, nf); wg_text_small(&c, x, y, text, t.text); y += 10;
        std::snprintf(text, sizeof text, "%d cables", p.n_cables); wg_text_small(&c, x, y, text, t.text); y += 10;
        std::snprintf(text, sizeof text, "%u KB a voice, %u KB the FX", static_cast<unsigned>(mod::Runner::bytes_for(p, mod::kVoiceArea) / 1024),
                      static_cast<unsigned>(mod::Runner::bytes_for(p, mod::kFxArea) / 1024));
        const bool big = mod::Runner::bytes_for(p, mod::kVoiceArea) > kModularVoiceBytes || mod::Runner::bytes_for(p, mod::kFxArea) > kModularFxBytes;
        wg_text_small(&c, x, y, text, big ? t.warn : t.text); y += 10;
        if (big) { std::snprintf(text, sizeof text, "too big: %u KB / %u KB at most", static_cast<unsigned>(kModularVoiceBytes >> 10), static_cast<unsigned>(kModularFxBytes >> 10)); wg_text_small(&c, x, y, text, t.warn); y += 10; }
        std::snprintf(text, sizeof text, "%d module(s) the engine lacks", newmods); wg_text_small(&c, x, y, text, newmods ? t.warn : t.dim); y += 16;
        static const char *const help[] = {"Click a module's header to", "see it here. Drag a module", "from the list; drag from an", "output onto an input or a", "value. Right click a jack:", "its cables go. Drag the", "empty canvas to look around.", "", "Its values are the Modular", "synth's: the SOUND tab's", "knobs and macros, the", "LAYOUT tab, all work on it."};
        for (const char *h : help) { wg_text_small(&c, x, y, h, t.dim); y += 9; }
        return;
    }
    mod::Node &n = p.node[si];
    const mod::ModuleDef *d = n.def;
    std::snprintf(text, sizeof text, "%s %d", d->name, n.id);
    wg_fill(&c, gui_rect(r.x, r.y, r.w, 18), wg_mix(t.panel2, category_color(d->category), 70));
    wg_text(&c, x, y + 1, text, t.text);
    y += 16;
    // its doc, cut into lines
    {
        const char *s = d->doc;
        while (*s && y < r.y + 90) {
            int len = static_cast<int>(std::strlen(s));
            if (len > 31) { len = 31; while (len > 10 && s[len] != ' ') len--; }
            std::snprintf(text, sizeof text, "%.*s", len, s);
            wg_text_small(&c, x, y, text, t.dim);
            y += 9;
            s += len;
            while (*s == ' ') s++;
        }
    }
    const EngFit fit = engine_fit(d, n.area);
    if (fit == kEngOther) std::snprintf(text, sizeof text, "engine: %s, %s", d->engine, other_scope(n.area));
    else std::snprintf(text, sizeof text, "engine: %s", d->engine ? d->engine : "new module needed");
    wg_text_small(&c, x, y, text, fit == kEngYes ? t.text : t.warn);
    y += 9;
    std::snprintf(text, sizeof text, "from: %.26s", d->origin);
    wg_text_small(&c, x, y, text, t.dim);
    y += 12;

    // its cables, with their depths
    wg_text_small(&c, x, y, "CABLES", t.dim);
    y += 4;
    int shown = 0;
    for (int k = 0; k < p.n_cables && y < r.y + r.h - 120; k++) {
        mod::Cable &cb = p.cable[k];
        if (cb.src != n.id && cb.dst != n.id) continue;
        const mod::Node *s = p.find(cb.src), *dn = p.find(cb.dst);
        if (!s || !dn) continue;
        std::snprintf(text, sizeof text, "%s%d.%s>%s%d.%s", s->def->name, s->id, s->def->out_name[cb.src_port], dn->def->name, dn->id,
                      cb.to_param ? dn->def->param[cb.dst_port].name : dn->def->in_name[cb.dst_port]);
        wg_fill(&c, gui_rect(x - 2, y + 2, 3, 3), cable_color(cb.src));
        wg_text_small(&c, x + 3, y + 7, text, t.text);
        y += 9;
        float pos = (cb.depth + 1.f) * 0.5f;            // the depth -1..1 on the slider
        if (!cb.to_param) pos = std::max(0.f, std::min(1.f, cb.depth * 0.5f));     // (an input's cable: 0..2)
        std::snprintf(text, sizeof text, "%s %+.2f", cb.to_param ? "depth" : "gain", static_cast<double>(cb.depth));
        if (wg_slider(&c, WG_ID(F_PINSP, k), gui_rect(x, y, r.w - 30, 10), &pos, cb.to_param ? 0.75f : 0.5f, text, cable_color(cb.src))) {
            cb.depth = cb.to_param ? pos * 2.f - 1.f : pos * 2.f;
            structural(nullptr);
        }
        if (wg_button(&c, WG_ID(F_PINSP, 200 + k), gui_rect(r.x + r.w - 20, y, 14, 10), "x", false)) { p.disconnect(k); structural("Cable removed"); return; }
        y += 13;
        shown++;
    }
    if (!shown) { wg_text_small(&c, x, y + 7, "none yet", t.dim); y += 12; }

    // connecting with the keyboard: one of its outputs -> an input or a value of another module of its area
    y = std::max(y + 4, r.y + r.h - 108);
    wg_text_small(&c, x, y + 7, "CONNECT (keyboard)", t.dim);
    y += 11;
    std::vector<Target> targets;
    for (int i = 0; i < p.n_nodes; i++) {
        const mod::Node &o = p.node[i];
        if (o.area != n.area || o.id == n.id) continue;
        for (int k = 0; k < o.def->n_in; k++) targets.push_back(Target{o.id, k, false});
        for (int k = 0; k < o.def->n_param; k++) targets.push_back(Target{o.id, k, true});
    }
    if (d->n_out == 0 || targets.empty()) { wg_text_small(&c, x, y + 7, d->n_out ? "no other module here" : "it has no output", t.dim); }
    else {
        st.pick_out = ((st.pick_out % d->n_out) + d->n_out) % d->n_out;
        const int nt = static_cast<int>(targets.size());
        st.pick_in = ((st.pick_in % nt) + nt) % nt;
        std::snprintf(text, sizeof text, "from %s", d->out_name[st.pick_out]);
        st.pick_out += wg_stepper(&c, WG_ID(F_PINSP, 400), gui_rect(x, y, r.w - 12, 14), text);
        y += 17;
        const Target &tg = targets[st.pick_in];
        const mod::Node *o = p.find(tg.node);
        std::snprintf(text, sizeof text, "to %s%d.%s%s", o->def->name, o->id, tg.param ? o->def->param[tg.port].name : o->def->in_name[tg.port], tg.param ? " (mod)" : "");
        st.pick_in += wg_stepper(&c, WG_ID(F_PINSP, 401), gui_rect(x, y, r.w - 12, 14), text);
        y += 17;
        if (wg_button(&c, WG_ID(F_PINSP, 402), gui_rect(x, y, r.w - 12, 14), "Connect", false)) {
            if (p.connect(n.id, st.pick_out, tg.node, tg.port, tg.param, tg.param ? 0.5f : 1.f)) structural("Connected");
            else message("Not connected");
        }
        y += 17;
    }
    if (wg_button(&c, WG_ID(F_PINSP, 403), gui_rect(x, r.y + r.h - 18, 74, 14), "Remove", false)) { p.remove(n.id); st.sel_node = 0; structural("Module removed"); }
}
}  // namespace

/* ---------------- the patch as the Modular synth ---------------- */

std::string patch_dir() { return st.preset_dir + "patches/"; }

std::string patch_ident(const char *name) {               // "Hollow Kick" -> "hollow_kick"
    std::string s;
    for (const char *p = name; *p; p++) {
        const char ch = *p;
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) s += ch;
        else if (ch >= 'A' && ch <= 'Z') s += static_cast<char>(ch - 'A' + 'a');
        else if (!s.empty() && s.back() != '_') s += '_';
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    if (s.empty() || (s[0] >= '0' && s[0] <= '9')) s = "patch_" + s;
    return s;
}


void patch_sync() { modular_pull_values(st.patch); }

void patch_apply() {
    const int mi = modular_index();
    const lab::Synth &s = *lab::kSynths[mi];
    // what points at the list (the macros, the layout), by name: after the change the same names find their values again
    std::vector<std::string> mac[kMacros], lay;
    char path[64];
    const bool remap = st.loaded[mi];
    if (remap) {
        for (int k = 0; k < kMacros; k++)
            for (int d = 0; d < st.macros[mi].m[k].n; d++) { param_path(s, st.macros[mi].m[k].d[d].param, path, sizeof path); mac[k].push_back(path); }
        for (const Page &pg : st.layouts[mi].pages)
            for (const El &e : pg.els) { if (e.param >= 0) param_path(s, e.param, path, sizeof path); else path[0] = 0; lay.push_back(path); }
    }
    modular_publish(st.patch);
    if (remap) {
        for (int k = 0; k < kMacros; k++) {
            Macro &m = st.macros[mi].m[k];
            int w = 0;
            for (int d = 0; d < m.n; d++) {
                const int i = find_param(s, mac[k][d].c_str());
                if (i >= 0) { m.d[w] = m.d[d]; m.d[w].param = i; w++; }
            }
            m.n = w;
        }
        size_t at = 0;
        for (Page &pg : st.layouts[mi].pages) {
            std::vector<El> keep;
            for (El &e : pg.els) {
                const std::string &pth = lay[at++];
                if (e.param < 0) { keep.push_back(e); continue; }
                const int i = find_param(s, pth.c_str());
                if (i >= 0) { e.param = i; keep.push_back(e); }
            }
            pg.els = keep;
        }
        st.dirty[mi] = true;
    }
    modular_push_values(st.patch);
}

bool save_patch(const std::string &path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    static char buf[64 * 1024];
    const int n = mod::patch_to_text(st.patch, buf, sizeof buf);
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(buf, 1, static_cast<size_t>(std::min<int>(n, sizeof buf - 1)), f) == static_cast<size_t>(std::min<int>(n, sizeof buf - 1));
    return std::fclose(f) == 0 && ok;
}

bool read_patch_file(const std::string &path, mod::Patch &out) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    char err[64];
    int skipped = 0;
    if (!mod::patch_from_text(out, text.c_str(), err, sizeof err, &skipped)) return false;
    if (skipped) message("%d line(s) of %s could not be read", skipped, path.c_str());
    return true;
}

bool load_patch(const std::string &path) {
    static mod::Patch p;
    if (!read_patch_file(path, p)) return false;
    st.patch = p;
    st.sel_node = 0;
    patch_apply();
    return true;
}

void draw_patch(wg_ctx_t &c, gui_rect_t area) {
    // the canvas first: what overflows it is covered by the panels drawn after it
    draw_canvas(c, gui_rect(area.x + kPanelW + 2, area.y, area.w - kPanelW - kInspW - 4, area.h));
    draw_left(c, area);
    draw_inspector(c, gui_rect(area.x + area.w - kInspW, area.y, kInspW, area.h));
}

}  // namespace studio
