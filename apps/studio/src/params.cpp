#include "params.h"
#include "control/preset.h"

namespace studio {

// (the library's: framework/control/preset.h, what a board reads too)
int  section_of(const lab::Synth &s, int i) { return pset_section(s.params, s.n_params, i); }
void param_path(const lab::Synth &s, int i, char *out, size_t size) { pset_path(s.params, s.n_params, i, out, static_cast<int>(size)); }
void quote_path(const lab::Synth &s, int i, char *out, size_t size) { pset_quote_path(s.params, s.n_params, i, out, static_cast<int>(size)); }
bool read_word(const char *&p, char *out, size_t size) { return pset_word(&p, out, static_cast<int>(size)); }
int  find_param(const lab::Synth &s, const char *path) { return pset_find(s.params, s.n_params, path); }

}  // namespace studio
