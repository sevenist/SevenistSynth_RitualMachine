#pragma once
// A synth's values as the studio's widgets see them: each value as a position 0..1 along its range (a knob's travel, a macro's Min / Max),
// as text, and by name ("SECTION/Name", what the saved files use: an index changes when a synth gains a value).
#include <cstddef>
#include "lab.h"

namespace studio {

float to_norm(const lab::Param &p, float v);        // Lin, Int: along the range; Exp: along its logarithm (as the encoder moves it)
float from_norm(const lab::Param &p, float n);      // the inverse (Int: rounded)
int   knob_steps(const lab::Param &p);              // the number of values of an Int parameter, 0 for the others (wg_knob_t::steps)
// A value moved by `detents` of an encoder: SynthLab's encoder B (100 detents over the range, Int by 1).
float step_value(const lab::Param &p, float v, int detents);
void  format_value(const lab::Param &p, float v, char *out, size_t size);   // "1.20 s", "440 Hz", a label
// The value's name with its section: "BODY/Tune" ("Tune" when no section comes before it).
void  param_path(const lab::Synth &s, int i, char *out, size_t size);
int   find_param(const lab::Synth &s, const char *path);                    // -1: none (a bare name matches the first of that name)
int   section_of(const lab::Synth &s, int i);       // the index of the section title over value i (-1: none)
// The saved files' words: a name in quotes when it holds a space ("RUMBLE/Dly Mix"). quote_path: param_path, quoted when needed.
// read_word: the next word at *p (quotes removed), *p moved past it; false at the end of the line.
void  quote_path(const lab::Synth &s, int i, char *out, size_t size);
bool  read_word(const char *&p, char *out, size_t size);

}  // namespace studio
