#pragma once
// Macros: the Ritual Machine's model (apps/ritual_machine/src/core/scr_macros.c, curves.h), over a SynthLab synth's values. A macro is one
// knob that moves up to kDests values at once; each destination maps the knob's travel through a curve onto Min..Max of its value's
// range (Min > Max inverts). Positions are 0..1 along a value's range as its knob shows it (params.h: to_norm).
// Saved as text, one file per synth (presets/<synth>.macros):
//   macro 1 "Punch" 0.500
//   dest BODY/Tune 0.20 0.80 Exp
#include <string>
#include "lab.h"

namespace studio {

constexpr int kMacros = 8;
constexpr int kDests = 8;

// The built-in curves of the Ritual Machine (core/curves.c): the same shapes.
enum Curve { kLin, kExp, kLog, kS, kCurveCount };
float       curve_eval(int curve, float x);         // 0..1 -> 0..1
const char *curve_name(int curve);
int         curve_by_name(const char *name);        // -1: unknown

struct Dest {
    int   param = -1;                               // the value's index in the synth's list
    float min = 0.f, max = 1.f;                     // 0..1 of its range
    int   curve = kLin;
};

struct Macro {
    char  name[16] = {};
    float pos = 0.5f;                               // the knob, 0..1
    int   n = 0;
    Dest  d[kDests];
};

struct MacroSet {
    Macro m[kMacros];
    MacroSet();                                     // M1 .. M8, no destinations
};

uint16_t macro_color(int k);                        // RGB565

float dest_apply(const Dest &d, float pos);         // the value's position 0..1 for the macro at pos
int   macro_find(const Macro &m, int param);        // the destination on `param`, -1: none
// Adds `param` as a destination: a range of 0.3 placed so that the macro, where it is, gives the value where it is (`now`, its position):
// nothing jumps; widen it by dragging its ring. Returns its index; -1 when the macro is full; the existing one when it is there already.
int   macro_add(Macro &m, int param, float now);
void  macro_remove(Macro &m, int k);
// Sets every destination's value for the macro's position (through synth_set_value).
void  macro_apply(const lab::Synth &s, const Macro &m);

std::string macros_to_text(const lab::Synth &s, const MacroSet &set);
// Replaces `set` with the text's macros. Destinations whose value the synth does not have are dropped (counted in *dropped).
bool macros_from_text(const lab::Synth &s, MacroSet &set, const char *text, int *dropped = nullptr);

}  // namespace studio
