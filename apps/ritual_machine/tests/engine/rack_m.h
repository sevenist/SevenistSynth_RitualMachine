#pragma once
// Row M helpers for the application-level tests (ADR-041: the effects are rack modules in row M, after the voices of every synth type).
#include "core/rack.h"

namespace tst {

inline void rack_m_clear(rack_t &r) { for (int i = r.count - 1; i >= 0; i--) if (r.slot[i].row == ROW_M) rack_delete(&r, i); }
inline int rack_m_find(const rack_t &r, module_type_t t) {
    for (int i = 0; i < r.count; i++) if (r.slot[i].row == ROW_M && r.slot[i].type == t) return i;
    return RACK_NONE;
}
inline int rack_m_get(rack_t &r, module_type_t t) { const int k = rack_m_find(r, t); return k != RACK_NONE ? k : rack_add_m(&r, t); }
// The demo / startup patches have the delay and the reverb on: these tests start dry.
inline void rack_m_dry(rack_t &r) {
    const int dl = rack_m_find(r, MOD_DELAY), rv = rack_m_find(r, MOD_REVERB);
    if (dl != RACK_NONE) r.slot[dl].v[MP_DL_MIX] = 0;
    if (rv != RACK_NONE) r.slot[rv].v[MP_RV_MIX] = 0;
}

}  // namespace tst
