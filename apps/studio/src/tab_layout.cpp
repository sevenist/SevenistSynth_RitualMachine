// The LAYOUT tab: the board's screen (128 x 160) drawn by layout_draw, as the board would draw it, shown twice its size; the synth's values
// listed on the right, dragged onto the screen as the kind of element chosen on the left (Enter on a focused value: placed in the first
// free room). On the screen: click selects, drag moves (snapped), the corner resizes; with the screen focused the arrows move the
// selection (Shift: 1 px), Ctrl+arrows resize it, Delete removes it, Ctrl+D copies it, Page Up / Down select another. TRY makes the
// screen work as on the board: Up / Down are encoder A (the next value), Left / Right encoder B (the value), Delete its default.
#include <algorithm>
#include <cstdio>
#include <vector>
#include "params.h"
#include "studio.h"
#include "synth.h"

namespace studio {

namespace {
constexpr int kZoom = 2;
uint16_t dev_px[kScreenW * kScreenH];
canvas_t dev;
bool dev_ready = false;

bool inside(gui_rect_t r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
bool overlap(const El &a, const El &b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
bool outside(const El &e) { return e.x < 0 || e.y < 0 || e.x + e.w > kScreenW || e.y + e.h > kScreenH; }
int snapv(int v) { return st.snap > 1 ? (v >= 0 ? (v + st.snap / 2) / st.snap * st.snap : -((-v + st.snap / 2) / st.snap * st.snap)) : v; }

Page &page() {
    Layout &l = cur_layout();
    if (l.pages.empty()) l.pages.emplace_back();
    st.page = std::max(0, std::min(static_cast<int>(l.pages.size()) - 1, st.page));
    return l.pages[st.page];
}

bool is_head(int i) { return i >= 0 && i < cur().n_params && cur().params[i].curve == lab::Head; }

// The first place on the page where an element of w x h fits without covering another (scanning rows of 2 px, steps of 8 px).
void find_free(const Page &p, int w, int h, int &x, int &y) {
    for (int yy = 0; yy + h <= kScreenH; yy += 2)
        for (int xx = 0; xx + w <= kScreenW; xx += 8) {
            const El t{kRow, 0, xx, yy, w, h};
            bool ok = true;
            for (const El &e : p.els) if (overlap(t, e)) { ok = false; break; }
            if (ok) { x = xx; y = yy; return; }
        }
    x = 0;
    y = kScreenH - h;
}

void add_element(int param, int at_x, int at_y, bool free_room) {
    Page &p = page();
    El e;
    e.kind = is_head(param) ? kTitle : (st.new_kind == kTitle ? kRow : st.new_kind);
    e.param = param;
    kind_size(e.kind, e.w, e.h);
    if (free_room) find_free(p, e.w, e.h, e.x, e.y);
    else { e.x = std::max(0, std::min(kScreenW - e.w, snapv(at_x - e.w / 2))); e.y = std::max(0, std::min(kScreenH - e.h, snapv(at_y - e.h / 2))); }
    if (e.kind == kTitle) e.x = 0;
    p.els.push_back(e);
    st.sel_el = static_cast<int>(p.els.size()) - 1;
    mark_dirty();
}

// The kind of an element changed: a title shows a section (or the synth's name), the others a value.
void set_kind(El &e, int kind) {
    if ((kind == kTitle) != (e.kind == kTitle)) {
        if (kind == kTitle) e.param = e.param >= 0 ? section_of(cur(), e.param) : -1;
        else {
            int i = std::max(0, e.param + 1);
            while (i < cur().n_params && is_head(i)) i++;
            e.param = i < cur().n_params ? i : 0;
        }
    }
    e.kind = kind;
    int w, h;
    kind_size(kind, w, h);
    e.w = w;
    e.h = h;
}

// The value an element shows, stepped through the ones its kind can show.
void step_param(El &e, int dir) {
    const int n = cur().n_params;
    int i = e.param;
    for (int k = 0; k < n + 1; k++) {
        i += dir;
        if (i < -1) i = n - 1;
        if (i >= n) i = -1;
        if (e.kind == kTitle ? (i == -1 || is_head(i)) : (i >= 0 && !is_head(i))) { e.param = i; return; }
    }
}

void draw_left(wg_ctx_t &c, gui_rect_t area) {
    const wg_theme_t &t = c.theme;
    Layout &l = cur_layout();
    wg_fill(&c, gui_rect(area.x, area.y, 172, area.h), t.panel);
    int y = area.y + 14;
    wg_text(&c, area.x + 8, y, "NEW ELEMENT", t.dim);
    y += 6;
    static const char *const names[kKindCount] = {"Row", "Knob", "Bar", "Title", "Big value"};
    for (int k = 0; k < kKindCount; k++) {
        if (wg_button(&c, WG_ID(F_KIND, k), gui_rect(area.x + 6, y, 160, 15), names[k], st.new_kind == k)) st.new_kind = k;
        y += 17;
    }
    y += 6;
    char text[48];
    std::snprintf(text, sizeof text, "Snap %d px", st.snap);
    const int ss = wg_stepper(&c, WG_ID(F_LAYOUT_BTN, 0), gui_rect(area.x + 6, y, 160, 15), text);
    if (ss) { static const int snaps[4] = {1, 2, 4, 8}; int at = 0; for (int i = 0; i < 4; i++) if (snaps[i] == st.snap) at = i; st.snap = snaps[std::max(0, std::min(3, at + ss))]; }
    y += 21;
    std::snprintf(text, sizeof text, "Page %d / %d", st.page + 1, static_cast<int>(l.pages.size()));
    const int ps = wg_stepper(&c, WG_ID(F_LAYOUT_BTN, 1), gui_rect(area.x + 6, y, 160, 15), text);
    if (ps) { st.page += ps; page(); st.sel_el = -1; st.try_focus = 0; }
    y += 18;
    if (wg_button(&c, WG_ID(F_LAYOUT_BTN, 2), gui_rect(area.x + 6, y, 78, 15), "Add page", false)) {
        l.pages.insert(l.pages.begin() + st.page + 1, Page{{El{kTitle, -1, 0, 0, kScreenW, 12}}});
        st.page++; st.sel_el = -1; mark_dirty();
    }
    if (wg_button(&c, WG_ID(F_LAYOUT_BTN, 3), gui_rect(area.x + 88, y, 78, 15), "Del page", false) && l.pages.size() > 1) {
        l.pages.erase(l.pages.begin() + st.page);
        page(); st.sel_el = -1; mark_dirty();
    }
    y += 18;
    if (wg_button(&c, WG_ID(F_LAYOUT_BTN, 4), gui_rect(area.x + 6, y, 78, 15), "Auto rows", false)) { layout_auto(cur(), l, kRow); st.page = 0; st.sel_el = -1; mark_dirty(); }
    if (wg_button(&c, WG_ID(F_LAYOUT_BTN, 5), gui_rect(area.x + 88, y, 78, 15), "Auto knobs", false)) { layout_auto(cur(), l, kKnob); st.page = 0; st.sel_el = -1; mark_dirty(); }
    if (c.hot == WG_ID(F_LAYOUT_BTN, 4) || c.hot == WG_ID(F_LAYOUT_BTN, 5)) wg_set_hint(&c, "Replaces every page (Revert brings back what was saved)");
    y += 18;
    if (wg_button(&c, WG_ID(F_LAYOUT_BTN, 6), gui_rect(area.x + 6, y, 160, 15), "Clear page", false)) { page().els.clear(); st.sel_el = -1; mark_dirty(); }
    y += 24;
    if (wg_button(&c, WG_ID(F_LAYOUT_BTN, 7), gui_rect(area.x + 6, y, 160, 18), st.try_mode ? "TRY: on (encoders)" : "TRY: off (editing)", st.try_mode)) {
        st.try_mode = !st.try_mode;
        wg_set_focus(&c, WG_ID(F_PREVIEW, 0));
    }
    if (c.hot == WG_ID(F_LAYOUT_BTN, 7)) wg_set_hint(&c, "The screen as on the board: Up / Down encoder A, Left / Right encoder B, Delete the default");
    y += 28;
    static const char *const edit_help[] = {"Drag a value onto the screen", "Click: select, drag: move", "Corner: resize", "Screen focused:", " arrows move (Shift 1 px)", " Ctrl+arrows resize", " Del remove, Ctrl+D copy", " PgUp / PgDn select"};
    static const char *const try_help[] = {"Screen focused:", " Up / Down: encoder A", " Left / Right: encoder B", "  (Shift: x10)", " Del: the default", " PgUp / PgDn: the page", "Click a value: go to it", ""};
    for (int i = 0; i < 8; i++) wg_text_small(&c, area.x + 8, y + i * 9, st.try_mode ? try_help[i] : edit_help[i], t.dim);
}

void draw_list(wg_ctx_t &c, gui_rect_t R) {
    const wg_theme_t &t = c.theme;
    const wg_input_t &in = *c.in;
    const lab::Synth &s = cur();
    const Layout &l = cur_layout();
    wg_fill(&c, R, t.panel);
    wg_text(&c, R.x + 6, R.y + 14, "VALUES", t.dim);
    wg_text_small(&c, R.x + 52, R.y + 13, "drag onto the screen, or Enter / double click", t.dim);
    const int row_h = 11, top = R.y + 20, rows = (R.h - 24) / row_h;
    const gui_rect_t list = gui_rect(R.x, top, R.w, rows * row_h);
    int focus_row = -1;
    for (int i = 0; i < s.n_params; i++) {
        const int id = WG_ID(F_LIST, i);
        const int yy = top + (i - st.list_scroll) * row_h;
        if (i < st.list_scroll || i >= st.list_scroll + rows) {
            wg_focusable(&c, id);
            if (wg_has_focus(&c, id)) focus_row = i;
            continue;
        }
        const gui_rect_t r = gui_rect(R.x + 4, yy, R.w - 14, row_h - 1);
        const bool was = c.active == id;
        wg_behavior(&c, id, r, true);
        const bool head = is_head(i);
        wg_fill(&c, r, c.hot == id ? t.line : (head ? t.panel2 : t.panel));
        bool here = false, elsewhere = false;
        for (int pg = 0; pg < static_cast<int>(l.pages.size()); pg++)
            for (const El &e : l.pages[pg].els) if (e.param == i) (pg == st.page ? here : elsewhere) = true;
        if (head) {
            wg_text_small(&c, r.x + 4, r.y + 8, s.params[i].name, t.accent);
        } else {
            char val[24];
            format_value(s.params[i], synth_value(i), val, sizeof val);
            wg_text_small(&c, r.x + 14, r.y + 8, s.params[i].name, t.text);
            wg_text_small(&c, r.x + r.w - 4 - wg_text_w(&c, val, true), r.y + 8, val, t.dim);
            if (here || elsewhere) wg_fill(&c, gui_rect(r.x + 5, r.y + 3, 4, 4), here ? t.accent : t.line);
        }
        if (wg_has_focus(&c, id)) wg_focus_ring(&c, gui_inset(r, -1));
        if (wg_focus_keys(&c, id).enter || (inside(r, in.x, in.y) && (in.pressed & WG_BTN_LEFT) && in.double_click)) add_element(i, 0, 0, true);
        else if (was && (in.released & WG_BTN_LEFT)) {      // a click: select its element on this page
            const Page &p = page();
            for (int k = 0; k < static_cast<int>(p.els.size()); k++) if (p.els[k].param == i) st.sel_el = k;
        }
        wg_drag_source(&c, id, head ? kDndTitle : kDndParam, i, s.params[i].name);
        if (c.hot == id) wg_set_hint(&c, head ? "A section: drag it onto the screen as a title" : "Drag onto the screen as a %s (the kind chosen on the left)", kind_name(st.new_kind));
    }
    if (focus_row >= 0) st.list_scroll = focus_row < st.list_scroll ? focus_row : focus_row - rows + 1;
    if (in.wheel && inside(list, in.x, in.y)) st.list_scroll -= in.wheel * 3;
    st.list_scroll = std::max(0, std::min(std::max(0, s.n_params - rows), st.list_scroll));
    if (s.n_params > rows) {
        const int bar_h = std::max(16, list.h * rows / s.n_params);
        const int by = list.y + (list.h - bar_h) * st.list_scroll / std::max(1, s.n_params - rows);
        wg_fill(&c, gui_rect(R.x + R.w - 7, by, 4, bar_h), t.line);
    }
}

// The screen's keys: editing or trying.
void preview_keys(wg_ctx_t &c, const std::vector<int> &order) {
    const wg_input_t &in = *c.in;
    Page &p = page();
    const bool shift = in.mods & WG_MOD_SHIFT, ctrl = in.mods & WG_MOD_CTRL;
    for (int k = 0; k < in.nkeys; k++) {
        if (!in.keys[k].down) continue;
        const int key = in.keys[k].key;
        if (key == WG_KEY_PGUP || key == WG_KEY_PGDN) {
            if (st.try_mode) { st.page += key == WG_KEY_PGDN ? 1 : -1; page(); st.try_focus = 0; }
            else if (!p.els.empty()) st.sel_el = (std::max(0, st.sel_el) + (key == WG_KEY_PGDN ? 1 : -1) + static_cast<int>(p.els.size())) % static_cast<int>(p.els.size());
            continue;
        }
        if (st.try_mode) {
            if (order.empty()) continue;
            const int f = std::max(0, std::min(static_cast<int>(order.size()) - 1, st.try_focus));
            const int prm = p.els[order[f]].param;
            if (key == WG_KEY_UP) st.try_focus = std::max(0, f - 1);
            if (key == WG_KEY_DOWN) st.try_focus = std::min(static_cast<int>(order.size()) - 1, f + 1);
            if (key == WG_KEY_LEFT || key == WG_KEY_RIGHT)
                synth_set_value(prm, step_value(cur().params[prm], synth_value(prm), (key == WG_KEY_RIGHT ? 1 : -1) * (shift ? 10 : 1)));
            if (key == WG_KEY_DELETE || key == WG_KEY_BACKSPACE) synth_set_value(prm, cur().params[prm].def);
            continue;
        }
        if (st.sel_el < 0 || st.sel_el >= static_cast<int>(p.els.size())) continue;
        El &e = p.els[st.sel_el];
        const int d = shift ? 1 : std::max(1, st.snap);
        int dx = 0, dy = 0;
        if (key == WG_KEY_LEFT) dx = -d;
        if (key == WG_KEY_RIGHT) dx = d;
        if (key == WG_KEY_UP) dy = -d;
        if (key == WG_KEY_DOWN) dy = d;
        if (dx || dy) {
            if (ctrl) { e.w = std::max(4, e.w + dx); e.h = std::max(4, e.h + dy); }
            else { e.x += dx; e.y += dy; }
            mark_dirty();
        }
        if (key == WG_KEY_DELETE || key == WG_KEY_BACKSPACE) { p.els.erase(p.els.begin() + st.sel_el); st.sel_el = std::min(st.sel_el, static_cast<int>(p.els.size()) - 1); mark_dirty(); return; }
        if (ctrl && key == 'd') {
            El copy = e;
            copy.y = std::min(kScreenH - copy.h, copy.y + copy.h);
            p.els.push_back(copy);
            st.sel_el = static_cast<int>(p.els.size()) - 1;
            mark_dirty();
        }
    }
}

void draw_preview(wg_ctx_t &c, gui_rect_t P) {
    const wg_theme_t &t = c.theme;
    const wg_input_t &in = *c.in;
    const lab::Synth &s = cur();
    Page &p = page();
    const std::vector<int> order = nav_order(s, p);
    if (st.try_focus >= static_cast<int>(order.size())) st.try_focus = std::max(0, static_cast<int>(order.size()) - 1);

    // the pointer on the screen
    const int id = WG_ID(F_PREVIEW, 0);
    const bool was = c.active == id;
    const bool active = wg_behavior(&c, id, P, true);
    const int dx = (in.x - P.x) / kZoom, dy = (in.y - P.y) / kZoom;
    if (active && !was) {
        int hit = -1;
        for (int i = static_cast<int>(p.els.size()) - 1; i >= 0 && hit < 0; i--) {
            const El &e = p.els[i];
            if (dx >= e.x && dy >= e.y && dx < e.x + e.w && dy < e.y + e.h) hit = i;
        }
        if (st.try_mode) {
            for (int k = 0; k < static_cast<int>(order.size()); k++) if (order[k] == hit) st.try_focus = k;
            st.drag_mode = 0;
        } else {
            st.sel_el = hit;
            st.drag_mode = 0;
            if (hit >= 0) {
                const El &e = p.els[hit];
                st.drag_mode = (dx >= e.x + e.w - 4 && dy >= e.y + e.h - 4) ? 2 : 1;
                st.drag_dx = dx - e.x;
                st.drag_dy = dy - e.y;
            }
        }
    }
    if (active && (in.buttons & WG_BTN_LEFT) && st.drag_mode && st.sel_el >= 0 && st.sel_el < static_cast<int>(p.els.size())) {
        El &e = p.els[st.sel_el];
        const El before = e;
        if (st.drag_mode == 1) {
            e.x = std::max(4 - e.w, std::min(kScreenW - 4, snapv(dx - st.drag_dx)));
            e.y = std::max(4 - e.h, std::min(kScreenH - 4, snapv(dy - st.drag_dy)));
        } else {
            e.w = std::max(4, snapv(dx - e.x + 1));
            e.h = std::max(4, snapv(dy - e.y + 1));
        }
        if (e.x != before.x || e.y != before.y || e.w != before.w || e.h != before.h) mark_dirty();
    }
    if (st.try_mode && c.hot == id && in.wheel && !order.empty())
        st.try_focus = std::max(0, std::min(static_cast<int>(order.size()) - 1, st.try_focus - in.wheel));
    if (wg_has_focus(&c, id)) preview_keys(c, order);
    int v;
    if (wg_drop_target(&c, P, kDndParam, &v) || wg_drop_target(&c, P, kDndTitle, &v)) add_element(v, dx, dy, false);

    // the board's drawing, twice its size
    if (!dev_ready) { canvas_init(&dev, dev_px, kScreenW, kScreenH); dev_ready = true; }
    canvas_color(&dev, 0xFFFF, 0x0000);
    canvas_clear(&dev, 0x0000);
    float vals[lab::kMaxParams];
    for (int i = 0; i < s.n_params; i++) vals[i] = synth_value(i);
    const int focus_el = st.try_mode && !order.empty() ? order[std::min(st.try_focus, static_cast<int>(order.size()) - 1)] : -1;
    layout_draw(canvas_u8g2(&dev), s, cur_layout(), st.page, vals, focus_el);
    uint16_t *dst = c.cv->px;
    for (int y = 0; y < kScreenH; y++)
        for (int x = 0; x < kScreenW; x++) {
            const uint16_t px = dev_px[y * kScreenW + x];
            uint16_t *o = dst + (P.y + y * kZoom) * c.cv->w + P.x + x * kZoom;
            o[0] = o[1] = o[c.cv->w] = o[c.cv->w + 1] = px;
        }
    wg_frame(&c, gui_inset(P, -2), wg_has_focus(&c, id) ? t.accent : t.line);
    for (int x = 0; x <= kScreenW; x += 8) wg_fill(&c, gui_rect(P.x + x * kZoom - (x == kScreenW), P.y - (x % 32 ? 5 : 8), 1, x % 32 ? 2 : 5), t.dim);
    for (int y = 0; y <= kScreenH; y += 8) wg_fill(&c, gui_rect(P.x - (y % 32 ? 5 : 8), P.y + y * kZoom - (y == kScreenH), y % 32 ? 2 : 5, 1), t.dim);
    if (c.hot == id && !st.try_mode && dx >= 0 && dy >= 0 && dx < kScreenW && dy < kScreenH) wg_set_hint(&c, "x %d  y %d", dx, dy);

    if (st.try_mode) return;
    for (int i = 0; i < static_cast<int>(p.els.size()); i++) {      // the elements' outlines: grey, red when in the way, the selection's
        const El &e = p.els[i];
        bool bad = outside(e);
        for (int k = 0; k < static_cast<int>(p.els.size()) && !bad; k++) if (k != i && overlap(e, p.els[k])) bad = true;
        const gui_rect_t r = gui_rect(P.x + e.x * kZoom, P.y + e.y * kZoom, e.w * kZoom, e.h * kZoom);
        if (i == st.sel_el) {
            wg_frame(&c, r, t.accent);
            wg_fill(&c, gui_rect(r.x + r.w - 6, r.y + r.h - 6, 6, 6), t.accent);
        } else if (bad) {
            wg_frame(&c, r, t.warn);
        } else {
            for (int x = r.x; x < r.x + r.w; x += 4) { wg_fill(&c, gui_rect(x, r.y, 1, 1), t.line); wg_fill(&c, gui_rect(x, r.y + r.h - 1, 1, 1), t.line); }
            for (int y = r.y; y < r.y + r.h; y += 4) { wg_fill(&c, gui_rect(r.x, y, 1, 1), t.line); wg_fill(&c, gui_rect(r.x + r.w - 1, y, 1, 1), t.line); }
        }
    }
}

void draw_inspector(wg_ctx_t &c, gui_rect_t r) {
    const wg_theme_t &t = c.theme;
    const lab::Synth &s = cur();
    const Layout &l = cur_layout();
    Page &p = page();
    char text[64];
    int y = r.y;
    if (st.sel_el >= 0 && st.sel_el < static_cast<int>(p.els.size()) && !st.try_mode) {
        El &e = p.els[st.sel_el];
        const int ks = wg_stepper(&c, WG_ID(F_INSPECT, 0), gui_rect(r.x, y, 100, 15), kind_name(e.kind));
        if (ks) { set_kind(e, (e.kind + ks + kKindCount * 4) % kKindCount); mark_dirty(); }
        char path[64];
        if (e.param < 0) std::snprintf(path, sizeof path, "(the synth's name)"); else param_path(s, e.param, path, sizeof path);
        const int vs = wg_stepper(&c, WG_ID(F_INSPECT, 1), gui_rect(r.x + 104, y, r.w - 104, 15), path);
        if (vs) { step_param(e, vs); mark_dirty(); }
        y += 19;
        int *f[4] = {&e.x, &e.y, &e.w, &e.h};
        static const char *const n[4] = {"x", "y", "w", "h"};
        const int fw = (r.w - 12) / 4;
        for (int i = 0; i < 4; i++) {
            std::snprintf(text, sizeof text, "%s %d", n[i], *f[i]);
            const int d = wg_stepper(&c, WG_ID(F_INSPECT, 2 + i), gui_rect(r.x + i * (fw + 4), y, fw, 15), text);
            if (d) { *f[i] += d * ((c.in->mods & WG_MOD_SHIFT) ? 1 : std::max(1, st.snap)); if (i >= 2) *f[i] = std::max(4, *f[i]); mark_dirty(); }
        }
        y += 21;
    } else {
        wg_text_small(&c, r.x, y + 9, st.try_mode ? "TRY: the screen works as on the board" : "No element selected", t.dim);
        y += 40;
    }
    // what is wrong with the arrangement
    int over = 0, out = 0, missing = 0;
    for (int i = 0; i < static_cast<int>(p.els.size()); i++) {
        if (outside(p.els[i])) out++;
        for (int k = i + 1; k < static_cast<int>(p.els.size()); k++) if (overlap(p.els[i], p.els[k])) over++;
    }
    for (int i = 0; i < s.n_params; i++) {
        if (is_head(i)) continue;
        bool found = false;
        for (const Page &pg : l.pages) for (const El &e : pg.els) if (e.param == i && e.kind != kTitle) found = true;
        if (!found) missing++;
    }
    std::snprintf(text, sizeof text, "%d element(s), %d value(s) the encoders reach", static_cast<int>(p.els.size()), static_cast<int>(nav_order(s, p).size()));
    wg_text_small(&c, r.x, y + 7, text, t.dim);
    y += 10;
    if (over) { std::snprintf(text, sizeof text, "%d overlap(s) on this page", over); wg_text_small(&c, r.x, y + 7, text, t.warn); y += 10; }
    if (out) { std::snprintf(text, sizeof text, "%d element(s) off the screen", out); wg_text_small(&c, r.x, y + 7, text, t.warn); y += 10; }
    std::snprintf(text, sizeof text, missing ? "%d value(s) on no page" : "every value is on a page", missing);
    wg_text_small(&c, r.x, y + 7, text, missing ? t.text : t.dim);
}
}  // namespace

void draw_layout(wg_ctx_t &c, gui_rect_t area) {
    draw_left(c, area);
    const gui_rect_t P = gui_rect(area.x + 190, area.y + 22, kScreenW * kZoom, kScreenH * kZoom);
    wg_text_small(&c, P.x, area.y + 10, "THE SCREEN, 128 x 160 (shown x2)", c.theme.dim);
    draw_preview(c, P);
    draw_inspector(c, gui_rect(P.x, P.y + P.h + 8, P.w, area.y + area.h - (P.y + P.h + 8)));
    draw_list(c, gui_rect(area.x + 460, area.y, area.w - 460, area.h));
}

}  // namespace studio
