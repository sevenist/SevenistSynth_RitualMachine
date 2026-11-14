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

void draw_el(u8g2_t *g, const lab::Synth &s, const El &e, int npages, int page, const float *values, bool focus) {
    char name[32], val[32], cut[32];
    const bool has = el_has_value(s, e);
    const lab::Param *p = has ? &s.params[e.param] : nullptr;
    const float v = has ? values[e.param] : 0.f;
    const float n = has ? to_norm(*p, v) : 0.f;
    if (has) format_value(*p, v, val, sizeof val); else val[0] = 0;
    if (e.kind == kTitle) std::snprintf(name, sizeof name, "%s", e.param < 0 ? s.name : (is_head(s, e.param) ? s.params[e.param].name : "?"));
    else std::snprintf(name, sizeof name, "%s", p ? p->name : "?");

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
    default: break;
    }
    u8g2_SetMaxClipWindow(g);
}
}  // namespace

const char *kind_name(int kind) {
    static const char *const n[kKindCount] = {"row", "knob", "bar", "title", "big"};
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
    default: w = kScreenW; h = 10; break;
    }
}

bool el_has_value(const lab::Synth &s, const El &e) {
    return e.kind != kTitle && e.param >= 0 && e.param < s.n_params && !is_head(s, e.param);
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
    for (int i = 0; i < static_cast<int>(p.els.size()); i++)
        draw_el(g, s, p.els[i], static_cast<int>(l.pages.size()), page, values, i == focus);
}

std::string layout_to_text(const lab::Synth &s, const Layout &l) {
    std::string out = "# SynthLab Studio layout: ";
    out += s.name;
    out += "  (screen 128 x 160)\n# page, then: kind value x y w h   (title \"-\": the synth's name; a section title by its name)\n";
    char line[160], path[64];
    for (const Page &p : l.pages) {
        out += "page\n";
        for (const El &e : p.els) {
            if (e.param < 0) std::snprintf(path, sizeof path, "-");
            else quote_path(s, e.param, path, sizeof path);
            std::snprintf(line, sizeof line, "%s %s %d %d %d %d\n", kind_name(e.kind), path, e.x, e.y, e.w, e.h);
            out += line;
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
        el.param = std::strcmp(path, "-") == 0 ? -1 : find_param(s, path);
        if (el.param < 0 && std::strcmp(path, "-") != 0) { lost++; continue; }
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
