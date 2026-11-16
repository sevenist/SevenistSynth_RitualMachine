// The catalog's modules as engine modules (engine_modules.h): one engine module type per catalog module and scope, all of them one host
// (Host) around the module's description (its construct / start / process, its size). On the board the host's process() sits in internal
// RAM once for every type; a type's description (its ModuleInfo) is made only when it is registered: an app pays for what it plays.
#include "modular/engine_modules.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include "modular/mdsp.h"

namespace mod {

namespace {
constexpr int kMaxWrapped = 32;                         // the catalog's own modules, the In / Out ones aside (ids up to 128 + 63)

struct Wrapped {
    const ModuleDef *def = nullptr;
    sc::ModuleInfo *info[2] = {};                       // Voice, Global: made at the type's registration
    char name[20] = {};
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
    }
}

// The engine's description of a wrapped module in one scope (made once, kept: the registry points at it)
const sc::ModuleInfo *info_of(int k, bool global) {
    Wrapped &w = g_w[k];
    sc::ModuleInfo *&slot = w.info[global ? 1 : 0];
    if (slot) return slot;
    slot = static_cast<sc::ModuleInfo *>(std::malloc(sizeof(sc::ModuleInfo)));
    if (!slot) return nullptr;
    new (slot) sc::ModuleInfo{};
    const ModuleDef *d = w.def;
    slot->name = w.name;
    slot->scope = global ? sc::Scope::Global : sc::Scope::Voice;
    slot->n_in = d->n_in;
    slot->n_out = d->n_out;
    slot->n_param = d->n_param;
    slot->reads_bus = false;
    // What its outputs carry, for the engine's modules it modulates (ADR-050), from its kind: envelopes (the LFO's 50 Hz at most), macros
    // and the note are slow; processors follow what feeds them; sources and effects are audio
    const char *kind = d->category ? d->category : "";
    slot->rate = !std::strcmp(kind, "Envelopes") || !std::strcmp(kind, "Macros") || !std::strcmp(kind, "In / Out") ? sc::Rate::Control
               : !std::strcmp(kind, "Math") || !std::strcmp(kind, "Filters") || !std::strcmp(kind, "Shapers") || !std::strcmp(kind, "Utilities")
                     ? sc::Rate::Follow : sc::Rate::Audio;
    slot->sig = sc::Sig::F32;                           // (ADR-050 step 2: float blocks, the engine converts where a q15 module joins)
    for (int i = 0; i < d->n_in; i++) slot->in_name[i] = d->in_name[i];
    for (int o = 0; o < d->n_out; o++) slot->out_name[o] = d->out_name[o];
    for (int j = 0; j < d->n_param; j++) {
        const Param &q = d->param[j];
        slot->param[j] = sc::ParamDesc{q.name, modular_raw(q, q.def), modular_raw(q, q.min), modular_raw(q, q.max)};
    }
    return slot;
}

// The host: the module's state in the engine's heap, its values in units, the conversions each block.
class Host final : public sc::Module {
public:
    Host(int k, bool global) : k_(k), global_(global) {}
    const sc::ModuleInfo &info() const override { return *g_w[k_].info[global_ ? 1 : 0]; }
    bool init(sc::Memory &mem) override {
        const ModuleDef &d = *g_w[k_].def;
        heap_ = d.size > 8192 && mem.bulk ? mem.bulk : mem.fast;
        state_ = heap_->alloc(d.size);
        if (!state_) return false;
        d.construct(state_);
        for (int j = 0; j < d.n_param; j++) v_[j] = d.param[j].def;
        for (int k = 0; k < d.n_in; k++) if (!std::strcmp(d.in_name[k], "Pitch")) pitch_in_ |= static_cast<uint8_t>(1u << k);   // (once, not a block)
        return true;
    }
    ~Host() override {
        if (!state_) return;
        g_w[k_].def->destroy(state_);
        heap_->free(state_);
    }
    // (reset(), at a voice's start: nothing. The module's own start() runs in the block, as the runner calls it: an ADSR starts again
    // from where it is, a stolen voice does not click to zero.)
    void set_param(int idx, int32_t raw) override {
        const ModuleDef &d = *g_w[k_].def;
        if (idx >= 0 && idx < d.n_param) v_[idx] = modular_value(d.param[idx], raw);
    }
    SC_HOT void process(const sc::ProcessCtx &ctx, const sc::Ports &p) override;

private:
    int k_;
    bool global_;
    sc::Heap *heap_ = nullptr;
    void *state_ = nullptr;
    float v_[kMaxParams] = {};
    uint8_t pitch_in_ = 0;                              // the inputs named Pitch (a CV: scaled to octaves)
};

// A float module of the engine (ADR-050 step 2: Sig::F32): its blocks are the engine's, read and written in place, the pitch inputs
// scaled (the engine's pitch CV is 72 semitones to full scale, the catalog's octaves). A block longer than the catalog's kMaxFrames runs
// in pieces, as the runner does (the values' modulation: the mean over each piece).
SC_HOT void Host::process(const sc::ProcessCtx &ctx, const sc::Ports &p) {
    const ModuleDef &d = *g_w[k_].def;
    Ctx c;
    c.sr = static_cast<float>(sc::kSampleRate);
    c.inv_sr = 1.f / c.sr;
    bool started = false;
    if (!global_ && ctx.voice) {                            // a voice's note
        const sc::VoiceState &vs = *ctx.voice;
        c.hz = 440.f * fast_exp2((static_cast<float>(vs.pitch) * (1.f / 256.f) - 69.f) * (1.f / 12.f));
        c.velocity = static_cast<float>(vs.velocity) * (1.f / 32767.f);
        c.gate = vs.gate;
        started = vs.started;
    } else {                                                // the global section: the keys held, a start when any voice starts
        c.gate = ctx.keys_held > 0;
        for (int v = 0; v < ctx.nvoices && ctx.voices; v++) if (ctx.voices[v].started) started = true;
    }
    if (started) d.start(state_, c);
    for (int off = 0; off < ctx.frames; off += kMaxFrames) {
        const int n = ctx.frames - off < kMaxFrames ? ctx.frames - off : kMaxFrames;
        // the values, with their modulations (the runner's rule: the mean over the piece moves the value along its range)
        float pv[kMaxParams];
        for (int j = 0; j < d.n_param; j++) {
            pv[j] = v_[j];
            if (const float *m = p.fmod[j]) {
                float s = 0.f;
                for (int i = 0; i < n; i++) s += m[off + i];
                const float amount = s / static_cast<float>(n);
                if (amount != 0.f) pv[j] = from_norm(d.param[j], to_norm(d.param[j], pv[j]) + amount);
            }
        }
        // the inputs: the engine's block itself, scaled (pitch), or what the catalog's module reads with nothing plugged in
        float in[kMaxIn][kMaxFrames];
        IO io{};
        for (int k = 0; k < d.n_in; k++) {
            const float *src = p.fin[k];
            if (src && src != ctx.fsilence) {
                if (!((pitch_in_ >> k) & 1u)) { io.in[k] = src + off; continue; }
                for (int i = 0; i < n; i++) in[k][i] = src[off + i] * 6.f;     // (pitch CV: 72 semitones a unit)
            } else {
                float *x = in[k];
                switch (d.in_def[k]) {
                case kOne: for (int i = 0; i < n; i++) x[i] = 1.f; break;
                case kGate: { const float gv = c.gate ? 1.f : 0.f; for (int i = 0; i < n; i++) x[i] = gv; break; }
                case kTrig: for (int i = 0; i < n; i++) x[i] = 0.f; if (started && off == 0) x[0] = 1.f; break;
                case kIn0: for (int i = 0; i < n; i++) x[i] = k > 0 ? io.in[0][i] : 0.f; break;
                case kPitch: { const float oct = fast_log2(c.hz * (1.f / 261.6256f)); for (int i = 0; i < n; i++) x[i] = oct; break; }
                default: for (int i = 0; i < n; i++) x[i] = 0.f; break;
                }
            }
            io.in[k] = in[k];
        }
        for (int k = d.n_in; k < kMaxIn; k++) io.in[k] = d.n_in > 0 ? io.in[0] : in[0];   // (never read)
        for (int o = 0; o < kMaxOut; o++) io.out[o] = o < d.n_out ? p.fout[o] + off : nullptr;
        io.p = pv;
        io.frames = n;
        d.process(state_, c, io);
    }
}

// A type's creator: it only knows which module it builds (one small function per module and scope; the host's code is shared)
template <int K, bool G>
sc::Module *create(sc::Memory &m) {
    Host *h = m.fast->make<Host>(K, G);
    if (!h) return nullptr;
    if (!h->init(m)) { h->~Host(); m.fast->free(h); return nullptr; }
    return h;
}

using Create = sc::Module *(*)(sc::Memory &);
template <bool G, int... K>
constexpr std::array<Create, sizeof...(K)> creators(std::integer_sequence<int, K...>) { return {&create<K, G>...}; }

bool add(sc::Registry &reg, int k) {
    static const auto cv = creators<false>(std::make_integer_sequence<int, kMaxWrapped>{});
    static const auto cg = creators<true>(std::make_integer_sequence<int, kMaxWrapped>{});
    const Wrapped &w = g_w[k];
    auto put = [&](int id, bool global, Create c) {      // (registered already, as this type: fine)
        const sc::ModuleInfo *info = info_of(k, global);
        if (!info) return false;
        if (const sc::ModuleType *t = reg.get(id)) return t->info == info;
        return reg.add(id, sc::ModuleType{info, c});
    };
    bool any = false;
    if (w.def->scope != Scope::Global) any |= put(kModularTypeFirst + 2 * k, false, cv[static_cast<size_t>(k)]);
    if (w.def->scope != Scope::Voice) any |= put(kModularTypeFirst + 2 * k + 1, true, cg[static_cast<size_t>(k)]);
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

const char *modular_type_name(const char *name) {
    fill();
    for (int k = 0; k < g_nw; k++) if (!std::strcmp(g_w[k].def->name, name)) return g_w[k].name;
    return nullptr;
}

const ModuleDef *modular_def_of(const sc::ModuleInfo *info) {
    for (int k = 0; k < g_nw; k++) if (info && (info == g_w[k].info[0] || info == g_w[k].info[1])) return g_w[k].def;
    return nullptr;
}

}  // namespace mod
