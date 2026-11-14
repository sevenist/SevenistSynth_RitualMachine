#pragma once
// A synth's values by name ("SECTION/Name", what the saved files use: an index changes when a synth gains a value). A value's position
// 0..1 along its range (a knob's travel, a macro's Min / Max), its steps and its text are the library's (framework/control/param.h:
// pdesc_to_norm, pdesc_from_norm, pdesc_detents, pdesc_choices, pdesc_format; a lab::Param is a param_desc_t).
#include <cstddef>
#include "lab.h"

namespace studio {

// The value's name with its section: "BODY/Tune" ("Tune" when no section comes before it).
void  param_path(const lab::Synth &s, int i, char *out, size_t size);
int   find_param(const lab::Synth &s, const char *path);                    // -1: none (a bare name matches the first of that name)
int   section_of(const lab::Synth &s, int i);       // the index of the section title over value i (-1: none)
// The saved files' words: a name in quotes when it holds a space ("RUMBLE/Dly Mix"). quote_path: param_path, quoted when needed.
// read_word: the next word at *p (quotes removed), *p moved past it; false at the end of the line.
void  quote_path(const lab::Synth &s, int i, char *out, size_t size);
bool  read_word(const char *&p, char *out, size_t size);

}  // namespace studio
