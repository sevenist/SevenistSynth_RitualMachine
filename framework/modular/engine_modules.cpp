// The catalog's modules as engine modules (engine_modules.h): one engine module type per catalog module and scope, all of them the same
// host (Host<K, G>) around the module's description (its construct / start / process, its size).
#include "modular/engine_modules.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include "modular/mdsp.h"

namespace mod {

namespace {
constexpr int kMaxWrapped = 32;                         // the catalog's own modules, the In / Out ones aside (ids up to 128 + 63)

struct Wrapped {
    const ModuleDef *def = nullptr;
    sc::ModuleInfo info_v{}, info_g{};
    char name[24] = {};
};
Wrapped g_w[kMaxWrapped];
int g_nw = 0;

bool is_io(const ModuleDef *d) {
    return !std::strcmp(d->name, "Note") || !std::strcmp(d->name, "VoiceOut") || !std::strcmp(d->name, "Voices") || !std::strcmp(d->name, "Output");
}
bool own(const ModuleDef *d) { return d->cpp != nullptr && d->construct_in == nullptr; }   // (not an engine module the adapter added)

// The catalog's own modules, in its order: their rank is their type id's (stable while the catalog's list is).
void fill() {
    if (g_nw) return;
    int n = 0;
    const ModuleDef *const *all = catalog(&n);
    for (int i = 0; i < n && g_nw < kMaxWrapped; i++) {
        const ModuleDef *d = all[i];
        if (!own(d) || is_io(d)) continue;
        Wrapped &w = g_w[g_nw++];
        w.def = d;
        std::snprintf(w.name, sizeof w.name, "F%s", d->name);
        sc::ModuleInfo &v = w.info_v;
        v.name = w.name;
        v.scope = sc::Scope::Voice;
        v.n_in = d->n_in;
        v.n_out = d->n_out;
        v.n_param = d->n_param;
        v.reads_bus = false;
        for (int k = 0; k < d->n_in; k++) v.in_name[k] = d->in_name[k];
        for (int o = 0; o < d->n_out; o++) v.out_name[o] = d->out_name[o];
        for (int j = 0; j < d->n_param; j++) {
            const Param &q = d->param[j];
            v.param[j] = sc::ParamDesc{q.name, modular_raw(q, q.def), modular_raw(q, q.min), modular_raw(q, q.max)};
        }
        w.info_g = v;
        w.info_g.scope = sc::Scope::Global;
    }
}

// The host: the module's state in the engine's heap, its values in units, the conversions each block.
template <int K, bool G>
class Host final : public sc::Module {
public:
    const sc::ModuleInfo &info() const override { return G ? g_w[K].info_g : g_w[K].info_v; }
    bool init(sc::Memory &mem) override {
        const ModuleDef &d = *g_w[K].def;
        heap_ = d.size > 8192 && mem.bulk ? mem.bulk : mem.fast;
        state_ = heap_->alloc(d.size);
        if (!state_) return false;
        d.construct(state_);
        for (int j = 0; j < d.n_param; j++) v_[j] = d.param[j].def;
        return true;
    }
    ~Host() override {
        if (!state_) return;
        g_w[K].def->destroy(state_);
        heap_->free(state_);
    }
    // (reset(), at a voice's start: nothing. The module's own start() runs in the block, as the runner calls it: an ADSR starts again
    // from where it is, a stolen voice does not click to zero.)
    void set_param(int idx, int32_t raw) override {
        const ModuleDef &d = *g_w[K].def;
        if (idx >= 0 && idx < d.n_param) v_[idx] = modular_value(d.param[idx], raw);
    }
    SC_HOT void process(const sc::ProcessCtx &ctx, const sc::Ports &p) override {
        const ModuleDef &d = *g_w[K].def;
        const int n = ctx.frames < kMaxFrames ? ctx.frames : kMaxFrames;
        Ctx c;
        c.sr = static_cast<float>(sc::kSampleRate);
        c.inv_sr = 1.f / c.sr;
        bool started = false;
        if (!G && ctx.voice) {                              // a voice's note
            const sc::VoiceState &vs = *ctx.voice;
            c.hz = 440.f * fast_exp2((static_cast<float>(vs.pitch) * (1.f / 256.f) - 69.f) * (1.f / 12.f));
            c.velocity = static_cast<float>(vs.velocity) * (1.f / 32767.f);
            c.gate = vs.gate;
            started = vs.started;
        } else {                                            // the global section: the keys held, a start when any voice starts
            c.gate = ctx.keys_held > 0;
            for (int v = 0; v < ctx.nvoices && ctx.voices; v++) if (ctx.voices[v].started) started = true;
        }
        if (started) d.start(state_, c);
        // the values, with their modulations (the runner's rule: the mean over the block moves the value along its range)
        float pv[kMaxParams];
        for (int j = 0; j < d.n_param; j++) {
            pv[j] = v_[j];
            if (const sc::q15 *m = p.mod[j]) {
                int32_t s = 0;
                for (int i = 0; i < ctx.frames; i++) s += m[i];
                const float amount = static_cast<float>(s) * (1.f / 32768.f) / static_cast<float>(ctx.frames);
                if (amount != 0.f) pv[j] = from_norm(d.param[j], to_norm(d.param[j], pv[j]) + amount);
            }
        }
        // the inputs: converted, or what the catalog's module reads with nothing plugged in
        float in[kMaxIn][kMaxFrames], out[kMaxOut][kMaxFrames];
        IO io{};
        for (int k = 0; k < d.n_in; k++) {
            float *x = in[k];
            const sc::q15 *src = p.in[k];
            if (src && src != ctx.silence) {
                const float g = !std::strcmp(d.in_name[k], "Pitch") ? 6.f / 32768.f : 1.f / 32768.f;    // (pitch CV: 72 semitones a unit)
                for (int i = 0; i < n; i++) x[i] = static_cast<float>(src[i]) * g;
            } else {
                switch (d.in_def[k]) {
                case kOne: for (int i = 0; i < n; i++) x[i] = 1.f; break;
                case kGate: { const float gv = c.gate ? 1.f : 0.f; for (int i = 0; i < n; i++) x[i] = gv; break; }
                case kTrig: for (int i = 0; i < n; i++) x[i] = 0.f; if (started && n > 0) x[0] = 1.f; break;
                case kIn0: for (int i = 0; i < n; i++) x[i] = k > 0 ? in[0][i] : 0.f; break;
                case kPitch: { const float oct = fast_log2(c.hz * (1.f / 261.6256f)); for (int i = 0; i < n; i++) x[i] = oct; break; }
                default: for (int i = 0; i < n; i++) x[i] = 0.f; break;
                }
            }
            io.in[k] = x;
        }
        for (int k = d.n_in; k < kMaxIn; k++) io.in[k] = in[0];         // (never read)
        for (int o = 0; o < kMaxOut; o++) io.out[o] = o < d.n_out ? out[o] : nullptr;
        io.p = pv;
        io.frames = n;
        d.process(state_, c, io);
        for (int o = 0; o < d.n_out; o++) {
            sc::q15 *dst = p.out[o];
            const float *y = out[o];
            for (int i = 0; i < n; i++) {
                const float s = y[i] * 32767.f;
                dst[i] = static_cast<sc::q15>(s > 32767.f ? 32767.f : (s < -32768.f ? -32768.f : (s == s ? s : 0.f)));
            }
            for (int i = n; i < ctx.frames; i++) dst[i] = 0;
        }
    }

private:
    sc::Heap *heap_ = nullptr;
    void *state_ = nullptr;
    float v_[kMaxParams] = {};
};

template <int K, bool G>
sc::Module *create(sc::Memory &m) { return sc::create_module<Host<K, G>>(m); }

using Create = sc::Module *(*)(sc::Memory &);
template <int... K>
constexpr std::array<Create, sizeof...(K)> creators(bool global, std::integer_sequence<int, K...>) {
    return global ? std::array<Create, sizeof...(K)>{&create<K, true>...} : std::array<Create, sizeof...(K)>{&create<K, false>...};
}

bool add(sc::Registry &reg, int k) {
    static const auto cv = creators(false, std::make_integer_sequence<int, kMaxWrapped>{});
    static const auto cg = creators(true, std::make_integer_sequence<int, kMaxWrapped>{});
    const Wrapped &w = g_w[k];
    bool any = false;
    if (w.def->scope != Scope::Global) any |= reg.add(kModularTypeFirst + 2 * k, sc::ModuleType{&w.info_v, cv[static_cast<size_t>(k)]});
    if (w.def->scope != Scope::Voice) any |= reg.add(kModularTypeFirst + 2 * k + 1, sc::ModuleType{&w.info_g, cg[static_cast<size_t>(k)]});
    return any;
}
}  // namespace

int32_t modular_raw(const Param &q, float v) {
    const float s = q.curve == Int ? 1.f : 100.f;
    return static_cast<int32_t>(std::lround(v * s));
}
float modular_value(const Param &q, int32_t raw) {
    const float v = static_cast<float>(raw) * (q.curve == Int ? 1.f : 0.01f);
    return v < q.min ? q.min : (v > q.max ? q.max : v);
}

int register_modular_types(sc::Registry &reg) {
    fill();
    int added = 0;
    for (int k = 0; k < g_nw; k++) if (add(reg, k)) added++;
    return added;
}

bool register_modular_type(sc::Registry &reg, const char *name) {
    fill();
    for (int k = 0; k < g_nw; k++) if (!std::strcmp(g_w[k].def->name, name)) return add(reg, k);
    return false;
}

int modular_type_id(const char *name, sc::Scope scope) {
    fill();
    for (int k = 0; k < g_nw; k++) {
        const ModuleDef *d = g_w[k].def;
        if (std::strcmp(d->name, name) != 0) continue;
        if (scope == sc::Scope::Voice) return d->scope != Scope::Global ? kModularTypeFirst + 2 * k : -1;
        return d->scope != Scope::Voice ? kModularTypeFirst + 2 * k + 1 : -1;
    }
    return -1;
}

const ModuleDef *modular_def_of(const sc::ModuleInfo *info) {
    for (int k = 0; k < g_nw; k++) if (info == &g_w[k].info_v || info == &g_w[k].info_g) return g_w[k].def;
    return nullptr;
}

}  // namespace mod
