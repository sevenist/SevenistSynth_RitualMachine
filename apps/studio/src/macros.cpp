#include "macros.h"
#include <cstdio>
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

std::string sound_text(const pset_sound_t &ps, const std::string &title) {
    const int n = pset_to_text(&ps, title.c_str(), nullptr, 0);
    std::string out(static_cast<size_t>(n) + 1, '\0');
    pset_to_text(&ps, title.c_str(), &out[0], n + 1);
    out.resize(static_cast<size_t>(n));
    return out;
}

std::string macros_to_text(const lab::Synth &s, const MacroSet &set) {
    const pset_sound_t ps = {s.params, s.n_params, nullptr, nullptr, nullptr, const_cast<mac_t *>(set.m)};
    return sound_text(ps, std::string("SynthLab Studio macros: ") + s.name);
}

bool macros_from_text(const lab::Synth &s, MacroSet &set, const char *text, int *dropped) {
    MacroSet r;
    const pset_sound_t ps = {s.params, s.n_params, nullptr, nullptr, nullptr, r.m};
    if (!(pset_from_text(&ps, text, dropped) & PSET_HAD_MACROS)) return false;
    set = r;
    return true;
}

}  // namespace studio
