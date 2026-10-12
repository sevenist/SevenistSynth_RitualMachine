#include "platform/engine/dx7_convert.h"
#include <cmath>
#include "engine/dsp/phase.h"

namespace sc {

Dx7Patch dx7_convert(const dx7_patch_t &p) {
    Dx7Patch d{};
    d.algorithm = static_cast<uint8_t>(p.algorithm < 1 ? 1 : (p.algorithm > 32 ? 32 : p.algorithm));
    d.feedback_q15 = static_cast<int32_t>(std::lround(std::fmin(1.0f, std::fmax(0.0f, p.feedback)) * 32767.0f));
    d.release_ms = p.rel_ms;
    for (int k = 0; k < kDx7Ops; k++) {
        const dx7_op_t &o = p.op[k];
        Dx7OpCfg &c = d.op[k];
        c.ratio_q16 = static_cast<uint32_t>(std::lround(std::fmax(0.0f, dx7_ratio(&o)) * 65536.0f));
        c.fixed_inc = o.fixed_hz > 0.0f ? hz_to_inc(o.fixed_hz) : 0u;
        c.gain_q28 = o.level == 0 ? 0 : static_cast<int32_t>(std::lround(static_cast<double>(dx7_amp(o.level)) * 268435456.0));
        for (int i = 0; i < 4; i++) { c.eg_l[i] = o.eg_l[i] > 99 ? 99 : o.eg_l[i]; c.eg_ms[i] = o.eg_t[i]; }
    }
    return d;
}

}  // namespace sc
