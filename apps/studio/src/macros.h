#pragma once
// Macros: the library's (framework/control/macro.h, the Ritual Machine's too), over a SynthLab synth's values. A macro is one knob that
// moves up to kDests destinations at once: a value, or another macro (a link: it moves that macro, which moves its own values, down the
// chain; a macro never drives itself back, the link that would loop is refused). Each destination maps the knob's travel through a curve
// onto Min..Max of its range (Min > Max inverts). Positions are 0..1 along a value's range as its knob shows it (params.h: to_norm), or a
// macro's travel.
// Saved as text, one file per synth (presets/<synth>.macros):
//   macro 1 "Punch" 0.500
//   dest BODY/Tune 0.20 0.80 Exp
//   link 2 0.00 0.50 Lin                  (macro 2, driven by macro 1)
#include <string>
#include "control/curve.h"
#include "control/macro.h"
#include "lab.h"

namespace studio {

constexpr int kMacros = MAC_MAX;
constexpr int kDests = MAC_DESTS;
constexpr int kValueTarget = 1;                     // a destination's kind: one of the synth's values (prm: its index)

// The curves: the library's built-ins (framework/control/curve.h, the Ritual Machine's own: the same tables), no user curves.
enum Curve { kLin = CRV_LIN, kExp = CRV_EXP, kLog = CRV_LOG, kS = CRV_S, kCurveCount = CRV_BUILTIN };
inline float       curve_eval(int curve, float x) { return crv_eval(nullptr, curve, x); }     // 0..1 -> 0..1
inline const char *curve_name(int curve) { return crv_name(curve); }
inline int         curve_by_name(const char *name) { return crv_by_name(name); }           // -1: unknown (case-insensitive)

using Dest = ctl_map_t;                             // t: the target, min / max: 0..1 of its range, curve
using Macro = mac_t;                                // name, pos (the knob, 0..1), n destinations in d[]

struct MacroSet {
    Macro m[kMacros];
    MacroSet();                                     // "Macro 1" .. "Macro 8" in the middle, no destinations
};

inline int dest_param(const Dest &d) { return d.t.kind == kValueTarget ? d.t.prm : -1; }   // the value it drives, -1: a macro
inline int dest_macro(const Dest &d) { return mac_of(&d.t); }                             // the macro it drives, -1: a value

uint16_t macro_color(int k);                        // RGB565

float dest_apply(const Dest &d, float pos);         // the destination's position 0..1 for the macro at pos
int   macro_find(const Macro &m, int param);        // the destination on `param`, -1: none
int   macro_find_link(const Macro &m, int j);       // the destination on macro j, -1: none
// Adds `param` as a destination: a range of 0.3 placed so that the macro, where it is, gives the value where it is (`now`, its position):
// nothing jumps; widen it by dragging its ring. Returns its index; -1 when the macro is full; the existing one when it is there already.
int   macro_add(Macro &m, int param, float now);
// Macro k drives macro j, placed the same way (j stays where it is). Returns its index; -1 when k is full, j is k or j drives k (a
// loop); the existing one when it is there already.
int   macro_link(MacroSet &set, int k, int j);
void  macro_remove(Macro &m, int k);
// Puts every destination of macro k where its position says (values through synth_set_value), and the macros it drives with theirs.
void  macro_apply(const lab::Synth &s, MacroSet &set, int k);

std::string macros_to_text(const lab::Synth &s, const MacroSet &set);
// Replaces `set` with the text's macros. Destinations whose value the synth does not have, and links that would loop, are dropped
// (counted in *dropped).
bool macros_from_text(const lab::Synth &s, MacroSet &set, const char *text, int *dropped = nullptr);

}  // namespace studio
