#include "macros.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "params.h"
#include "synth.h"
#include "ui/canvas.h"

namespace studio {

float curve_eval(int curve, float x) {
    x = x < 0.f ? 0.f : x > 1.f ? 1.f : x;
    const float k = 4.f, ek = std::exp(k) - 1.f;                // the Exp / Log bend of the Ritual Machine's curves.c
    switch (curve) {
    case kExp: return (std::exp(k * x) - 1.f) / ek;             // slow start, fast end
    case kLog: return std::log(1.f + ek * x) / k;               // the mirror
    case kS: return x * x * (3.f - 2.f * x);                    // smoothstep
    default: return x;
    }
}

const char *curve_name(int curve) {
    static const char *const n[kCurveCount] = {"Lin", "Exp", "Log", "S"};
    return curve >= 0 && curve < kCurveCount ? n[curve] : "Lin";
}

int curve_by_name(const char *name) {
    for (int i = 0; i < kCurveCount; i++)
        if (std::strcmp(curve_name(i), name) == 0) return i;
    return -1;
}

MacroSet::MacroSet() {
    for (int k = 0; k < kMacros; k++) std::snprintf(m[k].name, sizeof m[k].name, "Macro %d", k + 1);
}

uint16_t macro_color(int k) {
    static const uint16_t c[kMacros] = {
        CANVAS_HEX(0xFFA14A), CANVAS_HEX(0x5ED6E8), CANVAS_HEX(0xE86BD9), CANVAS_HEX(0x9BE36A),
        CANVAS_HEX(0xF2D64B), CANVAS_HEX(0xA48BFF), CANVAS_HEX(0xFF6F7D), CANVAS_HEX(0x4FD1A5),
    };
    return c[((k % kMacros) + kMacros) % kMacros];
}

float dest_apply(const Dest &d, float pos) { return d.min + (d.max - d.min) * curve_eval(d.curve, pos); }

int macro_find(const Macro &m, int param) {
    for (int i = 0; i < m.n; i++)
        if (m.d[i].param == param) return i;
    return -1;
}

int macro_add(Macro &m, int param, float now) {
    const int at = macro_find(m, param);
    if (at >= 0) return at;
    if (m.n >= kDests) return -1;
    Dest &d = m.d[m.n];
    d = Dest{};
    d.param = param;
    d.min = now - 0.3f * m.pos;
    d.max = d.min + 0.3f;
    if (d.min < 0.f) { d.max -= d.min; d.min = 0.f; }      // (kept inside the range: then it moves a little)
    if (d.max > 1.f) { d.min -= d.max - 1.f; d.max = 1.f; }
    return m.n++;
}

void macro_remove(Macro &m, int k) {
    if (k < 0 || k >= m.n) return;
    for (int i = k; i + 1 < m.n; i++) m.d[i] = m.d[i + 1];
    m.n--;
}

void macro_apply(const lab::Synth &s, const Macro &m) {
    for (int i = 0; i < m.n; i++) {
        const Dest &d = m.d[i];
        if (d.param < 0 || d.param >= s.n_params) continue;
        synth_set_value(d.param, from_norm(s.params[d.param], dest_apply(d, m.pos)));
    }
}

std::string macros_to_text(const lab::Synth &s, const MacroSet &set) {
    std::string out = "# SynthLab Studio macros: ";
    out += s.name;
    out += "\n# macro N \"name\" position     dest SECTION/Value min max curve   (positions 0..1 along the range)\n";
    char line[160], path[64];
    for (int k = 0; k < kMacros; k++) {
        const Macro &m = set.m[k];
        std::snprintf(line, sizeof line, "macro %d \"%s\" %.3f\n", k + 1, m.name, m.pos);
        out += line;
        for (int i = 0; i < m.n; i++) {
            quote_path(s, m.d[i].param, path, sizeof path);
            std::snprintf(line, sizeof line, "dest %s %.3f %.3f %s\n", path, m.d[i].min, m.d[i].max, curve_name(m.d[i].curve));
            out += line;
        }
    }
    return out;
}

bool macros_from_text(const lab::Synth &s, MacroSet &set, const char *text, int *dropped) {
    MacroSet r;
    int cur = -1, lost = 0;
    bool any = false;
    const char *p = text;
    while (*p) {
        const char *e = std::strchr(p, '\n');
        const size_t n = e ? static_cast<size_t>(e - p) : std::strlen(p);
        char line[200];
        std::snprintf(line, sizeof line, "%.*s", static_cast<int>(n < sizeof line ? n : sizeof line - 1), p);
        p += n + (e ? 1 : 0);
        int k;
        if (std::sscanf(line, "macro %d", &k) == 1 && k >= 1 && k <= kMacros) {
            cur = k - 1;
            any = true;
            Macro &m = r.m[cur];
            const char *q1 = std::strchr(line, '"'), *q2 = q1 ? std::strchr(q1 + 1, '"') : nullptr;
            if (q1 && q2) {
                std::snprintf(m.name, sizeof m.name, "%.*s", static_cast<int>(q2 - q1 - 1), q1 + 1);
                m.pos = static_cast<float>(std::atof(q2 + 1));
            }
            m.pos = m.pos < 0.f ? 0.f : m.pos > 1.f ? 1.f : m.pos;
        } else if (cur >= 0 && std::strncmp(line, "dest ", 5) == 0) {
            char path[64], w[3][16];
            const char *q = line + 5;
            if (!read_word(q, path, sizeof path) || !read_word(q, w[0], sizeof w[0]) || !read_word(q, w[1], sizeof w[1])) continue;
            if (!read_word(q, w[2], sizeof w[2])) std::snprintf(w[2], sizeof w[2], "Lin");
            const float mn = static_cast<float>(std::atof(w[0])), mx = static_cast<float>(std::atof(w[1]));
            const char *cname = w[2];
            const int prm = find_param(s, path);
            Macro &m = r.m[cur];
            if (prm < 0 || s.params[prm].curve == lab::Head || m.n >= kDests) { lost++; continue; }
            Dest &d = m.d[m.n++];
            d.param = prm;
            d.min = mn < 0.f ? 0.f : mn > 1.f ? 1.f : mn;
            d.max = mx < 0.f ? 0.f : mx > 1.f ? 1.f : mx;
            const int c = curve_by_name(cname);
            d.curve = c < 0 ? kLin : c;
        }
    }
    if (dropped) *dropped = lost;
    if (!any) return false;
    set = r;
    return true;
}

}  // namespace studio
