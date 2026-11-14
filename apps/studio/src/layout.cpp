#include "layout.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "params.h"

namespace studio {

namespace {
const uint8_t *const kFont = u8g2_font_5x7_tr;         // the device's (ui/gui.c's style)
const uint8_t *const kBigFont = u8g2_font_7x13B_tr;
constexpr float kPi = 3.14159265f;

bool is_head(const lab::Synth &s, int i) { return i >= 0 && i < s.n_params && s.params[i].curve == lab::Head; }

// Text cut to fit `w` pixels in the current font.
void fit(u8g2_t *g, const char *src, int w, char *out, size_t size) {
    std::snprintf(out, size, "%s", src);
    for (size_t n = std::strlen(out); n > 0 && u8g2_GetStrWidth(g, out) > w; n--) out[n - 1] = 0;
}

int baseline(int y, int h) { return y + (h - 7) / 2 + 6; }    // the 5x7 font centred in h

void draw_text_right(u8g2_t *g, int right, int base, const char *s) {
    u8g2_DrawStr(g, static_cast<u8g2_uint_t>(right - u8g2_GetStrWidth(g, s)), static_cast<u8g2_uint_t>(base), s);
}

// The text drawn in w pixels from x: left, centred or right.
void draw_aligned(u8g2_t *g, int x, int w, int base, const char *text, int align) {
    const int tw = u8g2_GetStrWidth(g, text);
    const int at = align == kCenter ? x + (w - tw) / 2 : (align == kRight ? x + w - tw : x);
    u8g2_DrawStr(g, static_cast<u8g2_uint_t>(at), static_cast<u8g2_uint_t>(base), text);
}

// Whether a value reads as "on": a whole number above its least, any other value past half its travel.
bool is_on(const lab::Param &p, float v, float n) { return p.curve == lab::Int ? v > p.min + 0.5f : n >= 0.5f; }

void draw_el(u8g2_t *g, const lab::Synth &s, const El &e, int npages, int page, const float *values, bool focus) {
    char name[40], val[32], cut[40];
    const bool has = el_has_value(s, e);
    const lab::Param *p = has ? &s.params[e.param] : nullptr;
    const float v = has ? values[e.param] : 0.f;
    const float n = has ? pdesc_to_norm(&*p, v) : 0.f;
    if (has) pdesc_format(&*p, v, val, sizeof val); else val[0] = 0;
    std::snprintf(name, sizeof name, "%s", el_name(s, e).c_str());

    u8g2_SetClipWindow(g, static_cast<u8g2_uint_t>(std::max(0, e.x)), static_cast<u8g2_uint_t>(std::max(0, e.y)),
                       static_cast<u8g2_uint_t>(std::max(0, e.x + e.w)), static_cast<u8g2_uint_t>(std::max(0, e.y + e.h)));
    u8g2_SetFont(g, kFont);
    u8g2_SetDrawColor(g, 1);
    switch (e.kind) {
    case kTitle: {
        if (e.param < 0) {                              // the synth's name: an inverted bar with the page
            u8g2_DrawBox(g, e.x, e.y, e.w, e.h);
            u8g2_SetDrawColor(g, 0);
            u8g2_DrawStr(g, e.x + 2, baseline(e.y, e.h), name);
            if (npages > 1) { std::snprintf(val, sizeof val, "%d/%d", page + 1, npages); draw_text_right(g, e.x + e.w - 2, baseline(e.y, e.h), val); }
            u8g2_SetDrawColor(g, 1);
        } else {                                        // a section: its name between two rules
            const int w = u8g2_GetStrWidth(g, name), cx = e.x + e.w / 2, my = e.y + e.h / 2;
            u8g2_DrawStr(g, cx - w / 2, baseline(e.y, e.h), name);
            if (cx - w / 2 - 3 > e.x) u8g2_DrawHLine(g, e.x, my, cx - w / 2 - 3 - e.x);
            if (e.x + e.w > cx + w / 2 + 3) u8g2_DrawHLine(g, cx + w / 2 + 3, my, e.x + e.w - (cx + w / 2 + 3));
        }
        break;
    }
    case kRow: {
        if (focus) { u8g2_DrawBox(g, e.x, e.y, e.w, e.h); u8g2_SetDrawColor(g, 0); }
        const int vw = u8g2_GetStrWidth(g, val);
        fit(g, name, e.w - vw - 6, cut, sizeof cut);
        u8g2_DrawStr(g, e.x + 2, baseline(e.y, e.h), cut);
        draw_text_right(g, e.x + e.w - 2, baseline(e.y, e.h), val);
        u8g2_SetDrawColor(g, 1);
        break;
    }
    case kKnob: {
        int r = (std::min(e.w, e.h - 9)) / 2 - 1;
        if (r < 3) r = 3;
        const int cx = e.x + e.w / 2, cy = e.y + r + 1;
        u8g2_DrawCircle(g, cx, cy, r, U8G2_DRAW_ALL);
        const float a0 = 1.25f * kPi, a = a0 - n * 1.5f * kPi;
        for (float t = a0; t >= a; t -= 0.6f / static_cast<float>(r))     // the value's arc, inside the circle
            u8g2_DrawPixel(g, cx + static_cast<int>(std::lround((r - 2) * std::cos(t))), cy - static_cast<int>(std::lround((r - 2) * std::sin(t))));
        u8g2_DrawLine(g, cx, cy, cx + static_cast<int>(std::lround(r * std::cos(a))), cy - static_cast<int>(std::lround(r * std::sin(a))));
        fit(g, focus ? val : name, e.w, cut, sizeof cut);              // the name, or the value while the encoders are on it
        const int tw = u8g2_GetStrWidth(g, cut), ty = e.y + e.h - 1;
        if (focus) { u8g2_DrawBox(g, cx - tw / 2 - 1, ty - 7, tw + 2, 8); u8g2_SetDrawColor(g, 0); }
        u8g2_DrawStr(g, cx - tw / 2, ty, cut);
        u8g2_SetDrawColor(g, 1);
        break;
    }
    case kBar: {
        const int vw = u8g2_GetStrWidth(g, val);
        fit(g, name, e.w - vw - 4, cut, sizeof cut);
        u8g2_DrawStr(g, e.x + 1, e.y + 7, cut);
        draw_text_right(g, e.x + e.w - 1, e.y + 7, val);
        const int by = e.y + 9, bh = std::max(3, e.h - 9);
        u8g2_DrawFrame(g, e.x, by, e.w, bh);
        const int fw = static_cast<int>(std::lround(n * static_cast<float>(e.w - 4)));
        if (fw > 0 && bh > 4) u8g2_DrawBox(g, e.x + 2, by + 2, fw, bh - 4);
        if (focus) u8g2_DrawFrame(g, e.x - 1, e.y - 1, e.w + 2, e.h + 2);   // (clipped: the inner frame shows)
        if (focus) u8g2_DrawFrame(g, e.x, e.y, e.w, by - e.y);
        break;
    }
    case kBig: {
        fit(g, name, e.w - 2, cut, sizeof cut);
        u8g2_DrawStr(g, e.x + 2, e.y + 7, cut);
        u8g2_SetFont(g, kBigFont);
        u8g2_DrawStr(g, e.x + 2, e.y + std::min(e.h - 2, 21), val);
        u8g2_SetFont(g, kFont);
        if (focus) u8g2_DrawFrame(g, e.x, e.y, e.w, e.h);
        break;
    }
    case kFader: {                                      // a vertical slot filled from the bottom, the name (the value when focused) under it
        const int th = std::max(6, e.h - 9), tw = std::min(7, std::max(3, e.w - 2)), tx = e.x + (e.w - tw) / 2;
        u8g2_DrawFrame(g, tx, e.y, tw, th);
        const int fh = static_cast<int>(std::lround(n * static_cast<float>(th - 4)));
        if (fh > 0 && tw > 4) u8g2_DrawBox(g, tx + 2, e.y + th - 2 - fh, tw - 4, fh);
        const int cap = std::max(e.y, std::min(e.y + th - 1, e.y + th - 2 - fh));     // the cap: a line wider than the slot
        u8g2_DrawHLine(g, std::max(e.x, tx - 3), cap, std::min(e.w, tw + 6));
        fit(g, focus ? val : name, e.w, cut, sizeof cut);
        const int cw = u8g2_GetStrWidth(g, cut), cx = e.x + e.w / 2, ty = e.y + e.h - 1;
        if (focus) { u8g2_DrawBox(g, cx - cw / 2 - 1, ty - 7, cw + 2, 8); u8g2_SetDrawColor(g, 0); }
        u8g2_DrawStr(g, cx - cw / 2, ty, cut);
        u8g2_SetDrawColor(g, 1);
        break;
    }
    case kToggle: {                                     // a box (filled: on), then the name
        if (focus) { u8g2_DrawBox(g, e.x, e.y, e.w, e.h); u8g2_SetDrawColor(g, 0); }
        const int bs = std::max(3, std::min(7, e.h - 2)), by = e.y + (e.h - bs) / 2;
        u8g2_DrawFrame(g, e.x + 2, by, bs, bs);
        if (has && is_on(*p, v, n) && bs > 4) u8g2_DrawBox(g, e.x + 4, by + 2, bs - 4, bs - 4);
        fit(g, name, e.w - bs - 6, cut, sizeof cut);
        u8g2_DrawStr(g, e.x + bs + 5, baseline(e.y, e.h), cut);
        u8g2_SetDrawColor(g, 1);
        break;
    }
    case kChoice: {                                     // the choices side by side, the value's inverted; too many to fit: < value >
        int top = e.y;
        if (e.h >= 18) {                                // room for the name above
            fit(g, name, e.w - 2, cut, sizeof cut);
            u8g2_DrawStr(g, e.x + 1, e.y + 7, cut);
            top = e.y + 9;
        }
        const int ch = e.y + e.h - top;
        const int nc = has ? pdesc_choices(&*p) : 0;
        const int cell = nc > 0 ? e.w / nc : 0;
        if (nc > 1 && cell >= 6) {
            const int at = static_cast<int>(std::lround(v - p->min));
            for (int k = 0; k < nc; k++) {
                const int cx = e.x + k * cell, cw = k == nc - 1 ? e.w - k * cell : cell;
                char lab[24];
                pdesc_format(&*p, p->min + static_cast<float>(k), lab, sizeof lab);
                fit(g, lab, cw - 2, cut, sizeof cut);
                if (k == at) { u8g2_DrawBox(g, cx, top, cw, ch); u8g2_SetDrawColor(g, 0); }
                else u8g2_DrawFrame(g, cx, top, k < nc - 1 ? cw + 1 : cw, ch);
                draw_aligned(g, cx, cw, baseline(top, ch), cut, kCenter);
                u8g2_SetDrawColor(g, 1);
            }
            if (focus) u8g2_DrawFrame(g, e.x, e.y, e.w, top > e.y ? top - e.y : e.h);   // (with a name: a frame round it)
        } else {
            if (focus) { u8g2_DrawBox(g, e.x, top, e.w, ch); u8g2_SetDrawColor(g, 0); }
            u8g2_DrawStr(g, e.x + 2, baseline(top, ch), "<");
            draw_text_right(g, e.x + e.w - 2, baseline(top, ch), ">");
            fit(g, val, e.w - 16, cut, sizeof cut);
            draw_aligned(g, e.x, e.w, baseline(top, ch), cut, kCenter);
            u8g2_SetDrawColor(g, 1);
        }
        break;
    }
    case kLabel:
        fit(g, name, e.w, cut, sizeof cut);
        draw_aligned(g, e.x, e.w, baseline(e.y, e.h), cut, e.align);
        break;
    case kLine:                                         // along its longer side, through its middle
        if (e.w >= e.h) u8g2_DrawHLine(g, e.x, e.y + e.h / 2, e.w);
        else u8g2_DrawVLine(g, e.x + e.w / 2, e.y, e.h);
        break;
    case kBox: {                                        // a frame; its text in the top edge
        const int top = name[0] ? e.y + 3 : e.y;
        u8g2_DrawFrame(g, e.x, top, e.w, e.y + e.h - top);
        if (name[0]) {
            fit(g, name, e.w - 8, cut, sizeof cut);
            const int tw = u8g2_GetStrWidth(g, cut);
            u8g2_SetDrawColor(g, 0);
            u8g2_DrawBox(g, e.x + 3, e.y, tw + 2, 7);
            u8g2_SetDrawColor(g, 1);
            u8g2_DrawStr(g, e.x + 4, e.y + 6, cut);
        }
        break;
    }
    default: break;
    }
    u8g2_SetMaxClipWindow(g);
}
}  // namespace

const char *kind_name(int kind) {
    static const char *const n[kKindCount] = {"row", "knob", "bar", "title", "big", "fader", "toggle", "choice", "label", "line", "box"};
    return kind >= 0 && kind < kKindCount ? n[kind] : "row";
}

int kind_by_name(const char *name) {
    for (int k = 0; k < kKindCount; k++)
        if (std::strcmp(kind_name(k), name) == 0) return k;
    return -1;
}

void kind_size(int kind, int &w, int &h) {
    switch (kind) {
    case kKnob: w = 32; h = 34; break;
    case kBar: w = 64; h = 16; break;
    case kTitle: w = kScreenW; h = 10; break;
    case kBig: w = 64; h = 24; break;
    case kFader: w = 24; h = 48; break;
    case kToggle: w = 64; h = 10; break;
    case kChoice: w = kScreenW; h = 20; break;
    case kLabel: w = 64; h = 10; break;
    case kLine: w = kScreenW; h = 4; break;
    case kBox: w = 64; h = 40; break;
    default: w = kScreenW; h = 10; break;
    }
}

bool el_has_value(const lab::Synth &s, const El &e) {
    return kind_shows_value(e.kind) && e.param >= 0 && e.param < s.n_params && !is_head(s, e.param);
}

std::string el_name(const lab::Synth &s, const El &e) {
    if (!e.text.empty() || kind_is_deco(e.kind)) return e.text;
    if (e.kind == kTitle) return e.param < 0 ? s.name : (is_head(s, e.param) ? s.params[e.param].name : "?");
    return e.param >= 0 && e.param < s.n_params ? s.params[e.param].name : "?";
}

std::vector<int> nav_order(const lab::Synth &s, const Page &p) {
    std::vector<int> o;
    for (int i = 0; i < static_cast<int>(p.els.size()); i++)
        if (el_has_value(s, p.els[i])) o.push_back(i);
    std::stable_sort(o.begin(), o.end(), [&](int a, int b) {
        const El &ea = p.els[a], &eb = p.els[b];
        return ea.y != eb.y ? ea.y < eb.y : ea.x < eb.x;
    });
    return o;
}

void layout_auto(const lab::Synth &s, Layout &l, int style) {
    l.pages.clear();
    const int top = 14;                                 // under the synth's name
    Page page;
    int x = 0, y = 0, row_h = 0;
    auto new_page = [&] {
        if (!page.els.empty()) l.pages.push_back(page);
        page.els.clear();
        page.els.push_back(El{kTitle, -1, 0, 0, kScreenW, 12});
        x = 0; y = top; row_h = 0;
    };
    new_page();
    for (int i = 0; i < s.n_params; i++) {
        if (is_head(s, i)) {
            if (x > 0) { y += row_h; x = 0; row_h = 0; }
            if (y + 10 + 10 > kScreenH) new_page();      // (a title needs a row under it on its page)
            page.els.push_back(El{kTitle, i, 0, y, kScreenW, 10});
            y += 10;
            continue;
        }
        int w, h;
        kind_size(style == kKnob ? kKnob : kRow, w, h);
        if (x + w > kScreenW) { y += row_h; x = 0; row_h = 0; }
        if (y + h > kScreenH) new_page();
        page.els.push_back(El{style == kKnob ? kKnob : kRow, i, x, y, w, h});
        x += w;
        row_h = std::max(row_h, h);
        if (style != kKnob) { x = 0; y += h; row_h = 0; }
    }
    if (page.els.size() > 1 || l.pages.empty()) l.pages.push_back(page);
}

void layout_draw(u8g2_t *g, const lab::Synth &s, const Layout &l, int page, const float *values, int focus) {
    if (page < 0 || page >= static_cast<int>(l.pages.size())) return;
    u8g2_SetFontMode(g, 1);
    const Page &p = l.pages[page];
    for (int pass = 0; pass < 2; pass++)                // the decorations first: a box under what it holds
        for (int i = 0; i < static_cast<int>(p.els.size()); i++)
            if (kind_is_deco(p.els[i].kind) == (pass == 0))
                draw_el(g, s, p.els[i], static_cast<int>(l.pages.size()), page, values, i == focus);
}

std::string layout_to_text(const lab::Synth &s, const Layout &l) {
    std::string out = "# SynthLab Studio layout: ";
    out += s.name;
    out += "  (screen 128 x 160)\n# page, then: kind value x y w h [\"text\" [align]]   (title \"-\": the synth's name; a section title by its"
           " name; label, line, box: \"-\"; the text replaces the name shown)\n";
    static const char *const aligns[3] = {"left", "center", "right"};
    char line[200], path[64];
    for (const Page &p : l.pages) {
        out += "page\n";
        for (const El &e : p.els) {
            if (e.param < 0) std::snprintf(path, sizeof path, "-");
            else quote_path(s, e.param, path, sizeof path);
            std::snprintf(line, sizeof line, "%s %s %d %d %d %d", kind_name(e.kind), path, e.x, e.y, e.w, e.h);
            out += line;
            if (!e.text.empty() || e.align != kLeft) { out += " \""; out += e.text; out += "\""; }
            if (e.align != kLeft) { out += " "; out += aligns[std::max(0, std::min(2, e.align))]; }
            out += "\n";
        }
    }
    return out;
}

bool layout_from_text(const lab::Synth &s, Layout &l, const char *text, int *dropped) {
    Layout r;
    int lost = 0;
    const char *p = text;
    while (*p) {
        const char *e = std::strchr(p, '\n');
        const size_t n = e ? static_cast<size_t>(e - p) : std::strlen(p);
        char line[200];
        std::snprintf(line, sizeof line, "%.*s", static_cast<int>(n < sizeof line ? n : sizeof line - 1), p);
        p += n + (e ? 1 : 0);
        if (line[0] == '#' || line[0] == 0) continue;
        if (std::strncmp(line, "page", 4) == 0) { r.pages.emplace_back(); continue; }
        char kname[16], path[64], num[4][16];
        El el;
        const char *q = line;
        if (!read_word(q, kname, sizeof kname) || !read_word(q, path, sizeof path)) continue;
        bool ok = true;
        for (int k = 0; k < 4; k++) ok = read_word(q, num[k], sizeof num[k]) && ok;
        if (!ok) { lost++; continue; }
        el.x = std::atoi(num[0]); el.y = std::atoi(num[1]); el.w = std::atoi(num[2]); el.h = std::atoi(num[3]);
        el.kind = kind_by_name(kname);
        if (el.kind < 0 || r.pages.empty()) { lost++; continue; }
        const bool none = std::strcmp(path, "-") == 0 || kind_is_deco(el.kind);
        el.param = none ? -1 : find_param(s, path);
        if (el.param < 0 && (!none || kind_shows_value(el.kind))) { lost++; continue; }      // (a value's element without its value)
        char text[kMaxText + 1], align[16];
        if (read_word(q, text, sizeof text)) el.text = text;
        if (read_word(q, align, sizeof align)) el.align = std::strcmp(align, "center") == 0 ? kCenter : (std::strcmp(align, "right") == 0 ? kRight : kLeft);
        el.w = std::max(4, el.w);
        el.h = std::max(4, el.h);
        r.pages.back().els.push_back(el);
    }
    if (dropped) *dropped = lost;
    if (r.pages.empty()) return false;
    l = r;
    return true;
}

}  // namespace studio
