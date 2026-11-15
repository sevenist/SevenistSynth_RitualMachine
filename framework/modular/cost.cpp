// The patch's cost on the board (cost.h), from the measured table (module_costs.h).
#include "modular/cost.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace mod {

namespace {
// "Curve=100": the value of the table's worst case is set on this node
bool at_worst(const Node &n, const char *worst_at) {
    const char *eq = std::strchr(worst_at, '=');
    if (!eq) return false;
    char name[24];
    const size_t len = static_cast<size_t>(eq - worst_at);
    if (len == 0 || len >= sizeof name) return false;
    std::memcpy(name, worst_at, len);
    name[len] = 0;
    const int k = param_index(*n.def, name);
    return k >= 0 && std::fabs(n.v[k] - static_cast<float>(std::atof(eq + 1))) < 1e-3f;
}

struct AreaSum { uint32_t cycles = 0, worst = 0, bytes = 0; int nodes = 0, unmeasured = 0; };

AreaSum area_sum(const Patch &p, int area) {
    AreaSum s;
    for (int i = 0; i < p.n_nodes; i++) {
        const Node &n = p.node[i];
        if (n.area != area) continue;
        s.nodes++;
        const ModuleCost *mc = module_cost(n.def->name);
        if (!mc) { s.unmeasured++; continue; }
        const uint32_t cab = cable_cycles(p, n);
        s.cycles += node_cycles(n) + cab;
        s.worst += mc->worst + cab;
        s.bytes += mc->bytes;
    }
    return s;
}

bool fx_does_something(const Patch &p) {
    for (int i = 0; i < p.n_nodes; i++) {
        const Node &n = p.node[i];
        if (n.area == kFxArea && std::strcmp(n.def->name, "Voices") != 0 && std::strcmp(n.def->name, "Output") != 0) return true;
    }
    return false;
}
}  // namespace

uint32_t node_cycles(const Node &n, bool worst) {
    const ModuleCost *mc = n.def ? module_cost(n.def->name) : nullptr;
    if (!mc) return 0;
    if (worst || (mc->worst_at[0] && at_worst(n, mc->worst_at))) return mc->worst;
    return mc->cycles;
}

uint32_t cable_cycles(const Patch &p, const Node &n) {
    const BoardCost &b = kBoardCost;
    uint32_t c = 0;
    int into[kMaxIn] = {};
    bool unit[kMaxIn] = {};
    for (int k = 0; k < p.n_cables; k++) {
        const Cable &cb = p.cable[k];
        if (cb.dst != n.id) continue;
        if (cb.to_param) {
            const Curve cv = n.def->param[cb.dst_port].curve;
            c += cv == Exp ? b.cable_exp : (cv == Int ? b.cable_int : b.cable_lin);
        } else if (cb.dst_port < kMaxIn) {
            into[cb.dst_port]++;
            unit[cb.dst_port] = cb.depth == 1.f;
        }
    }
    for (int k = 0; k < kMaxIn; k++)                    // (one cable at depth 1 is read as it is: no sum)
        if (into[k] > 1 || (into[k] == 1 && !unit[k])) c += b.summed_input;
    return c;
}

PatchCost patch_cost(const Patch &p, int voices) {
    const BoardCost &b = kBoardCost;
    PatchCost r;
    const AreaSum v = area_sum(p, kVoiceArea), f = area_sum(p, kFxArea);
    r.voice = b.engine_voice + b.voice_block + v.cycles;
    const uint32_t vw = b.engine_voice + b.voice_block + v.worst;
    const bool fx = fx_does_something(p);
    r.fx = fx ? b.voice_block + f.cycles : 0;
    const uint32_t fw = fx ? b.voice_block + f.worst : 0;
    r.fixed = b.engine_fixed;
    if (voices < 0) voices = 0;
    r.total = r.fixed + static_cast<uint32_t>(voices) * r.voice + r.fx;
    r.worst = r.fixed + static_cast<uint32_t>(voices) * vw + fw;
    r.budget = b.budget;
    r.voice_bytes = v.bytes;
    r.fx_bytes = f.bytes;
    r.unmeasured = v.unmeasured + f.unmeasured;
    return r;
}

int voices_in_budget(const Patch &p, int ceiling) {
    int best = 0;
    for (int v = 1; v <= ceiling; v++)
        if (patch_cost(p, v).total <= kBoardCost.budget) best = v;
    return best;
}

}  // namespace mod
