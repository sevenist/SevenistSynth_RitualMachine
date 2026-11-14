#include "macros.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "params.h"
#include "synth.h"
#include "ui/canvas.h"

namespace studio {

namespace {

// A range of 0.3 placed so that the macro at `pos` gives `now`, kept inside 0..1 (then it moves a little).
void place(Dest &d, float now, float pos) {
    d.min = now - 0.3f * pos;
    d.max = d.min + 0.3f;
    if (d.min < 0.f) { d.max -= d.min; d.min = 0.f; }
    if (d.max > 1.f) { d.min -= d.max - 1.f; d.max = 1.f; }
}

// What the library's macros call for a destination that is a value (ctx: the synth).
bool set_value(void *ctx, const ctl_target_t *t, float v) {
    const lab::Synth &s = *static_cast<const lab::Synth *>(ctx);
    if (t->kind != kValueTarget || t->prm >= s.n_params) return false;
    synth_set_value(t->prm, pdesc_from_norm(&s.params[t->prm], v));
    return true;
}

}  // namespace

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

float dest_apply(const Dest &d, float pos) { return ctl_map_apply(nullptr, &d, pos); }

int macro_find(const Macro &m, int param) {
    const ctl_target_t t = ctl_target(kValueTarget, 0, param);
    return param < 0 ? -1 : mac_find(&m, &t);
}

int macro_find_link(const Macro &m, int j) {
    const ctl_target_t t = mac_target(j);
    return j < 0 || j >= kMacros ? -1 : mac_find(&m, &t);
}

int macro_add(Macro &m, int param, float now) {
    const int at = macro_find(m, param);
    if (at >= 0) return at;
    if (m.n >= kDests || param < 0) return -1;
    Dest &d = m.d[m.n];
    d = ctl_map_plain(ctl_target(kValueTarget, 0, param));
    place(d, now, m.pos);
    return m.n++;
}

int macro_link(MacroSet &set, int k, int j) {
    if (k < 0 || k >= kMacros || j < 0 || j >= kMacros) return -1;
    const int at = macro_find_link(set.m[k], j);
    if (at >= 0) return at;
    Dest d = ctl_map_plain(mac_target(j));
    place(d, set.m[j].pos, set.m[k].pos);
    return mac_add(set.m, k, d);                    // (-1: full, itself, or j drives k)
}

void macro_remove(Macro &m, int k) { mac_remove(&m, k); }

void macro_apply(const lab::Synth &s, MacroSet &set, int k) {
    mac_apply(set.m, nullptr, k, set.m[k].pos, set_value, const_cast<lab::Synth *>(&s));
}

std::string macros_to_text(const lab::Synth &s, const MacroSet &set) {
    std::string out = "# SynthLab Studio macros: ";
    out += s.name;
    out += "\n# macro N \"name\" position     dest SECTION/Value min max curve     link M min max curve (macro M driven by it)"
           "   (positions 0..1 along the range)\n";
    char line[160], path[64];
    for (int k = 0; k < kMacros; k++) {
        const Macro &m = set.m[k];
        std::snprintf(line, sizeof line, "macro %d \"%s\" %.3f\n", k + 1, m.name, m.pos);
        out += line;
        for (int i = 0; i < m.n; i++) {
            const Dest &d = m.d[i];
            if (dest_macro(d) >= 0) {
                std::snprintf(line, sizeof line, "link %d %.3f %.3f %s\n", dest_macro(d) + 1, d.min, d.max, curve_name(d.curve));
            } else {
                quote_path(s, dest_param(d), path, sizeof path);
                std::snprintf(line, sizeof line, "dest %s %.3f %.3f %s\n", path, d.min, d.max, curve_name(d.curve));
            }
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
            d = ctl_map_plain(ctl_target(kValueTarget, 0, prm));
            d.min = mn < 0.f ? 0.f : mn > 1.f ? 1.f : mn;
            d.max = mx < 0.f ? 0.f : mx > 1.f ? 1.f : mx;
            const int c = curve_by_name(cname);
            d.curve = c < 0 ? kLin : c;
        } else if (cur >= 0 && std::strncmp(line, "link ", 5) == 0) {
            char w[4][16];
            const char *q = line + 5;
            if (!read_word(q, w[0], sizeof w[0]) || !read_word(q, w[1], sizeof w[1]) || !read_word(q, w[2], sizeof w[2])) continue;
            if (!read_word(q, w[3], sizeof w[3])) std::snprintf(w[3], sizeof w[3], "Lin");
            const int j = std::atoi(w[0]) - 1;
            const float mn = static_cast<float>(std::atof(w[1])), mx = static_cast<float>(std::atof(w[2]));
            Dest d = ctl_map_plain(mac_target(j < 0 ? kMacros : j));
            d.min = mn < 0.f ? 0.f : mn > 1.f ? 1.f : mn;
            d.max = mx < 0.f ? 0.f : mx > 1.f ? 1.f : mx;
            const int c = curve_by_name(w[3]);
            d.curve = c < 0 ? kLin : c;
            if (mac_add(r.m, cur, d) < 0) lost++;   // not a macro, full, or a loop
        }
    }
    if (dropped) *dropped = lost;
    if (!any) return false;
    set = r;
    return true;
}

}  // namespace studio
