// The engine's modules in the modular catalog (engine_adapter.h).
#include "modular/engine_adapter.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <new>
#include <utility>
#include "engine/core/heap.h"
#include "modular/mdsp.h"
#include "modular/patch.h"

namespace mod {

namespace {

constexpr int kSlots = 48;
constexpr int kB = sc::kBlock;

const char *const kSkip[] = {"NoteIn", "VoiceOut", "BusIn", "MasterOut", "Sampler", "Granular", "Pads", "MotionSeq", "GateIn", "ParaGate",
                             "Convolver", "Dx7", "LabVoice", "LabFx"};

// How a value is shown: raw (the engine's units), a percentage of q15, or semitones of a 1/256-semitone pitch.
enum Show : uint8_t { kRaw, kPercent, kSemis };

struct Slot {
    ModuleDef def{};
    const sc::ModuleInfo *info = nullptr;
    int type_v = -1, type_g = -1;                       // the registry's ids of the Voice and the Global types (-1: none)
    sc::Module *(*create_v)(sc::Memory &) = nullptr;
    sc::Module *(*create_g)(sc::Memory &) = nullptr;
    size_t pool = 0;                                    // the module's own heap
    Show show[sc::kMaxParams] = {};
    bool pitch_in[sc::kMaxIn] = {};
    char name[24] = {}, doc[120] = {};
};
Slot g_slots[kSlots];
int g_n = 0;
const sc::Registry *g_reg = nullptr;

// What an engine module lives in, inside the runner's block: this header, then the module's heap.
struct State {
    int slot = 0, area = 0;
    bool voice_type = false;                            // the Voice type was built (it gets a voice's state, in either area)
    sc::Heap heap;
    sc::Module *m = nullptr;
    sc::VoiceState vs;
    bool started = false;
    uint64_t time = 0;
    int32_t sent[sc::kMaxParams] = {};
    bool any_sent = false;
    sc::q15 in[sc::kMaxIn][kB] = {};
    sc::q15 out[sc::kMaxOut][kB] = {};
};
constexpr size_t kHeader = (sizeof(State) + 15) / 16 * 16;

const sc::Transport &transport() {                      // the machine clock: stopped, 120 BPM (the patch has no transport yet)
    static sc::Transport t;
    return t;
}

void construct_at(void *mem, int k, int area) {
    State *s = new (mem) State();
    Slot &sl = g_slots[k];
    s->slot = k;
    s->area = area;
    s->heap.init(static_cast<unsigned char *>(mem) + kHeader, sl.pool);
    sc::Memory m{&s->heap, &s->heap, nullptr};
    sc::Module *(*create)(sc::Memory &) = area == kFxArea ? (sl.create_g ? sl.create_g : sl.create_v) : (sl.create_v ? sl.create_v : sl.create_g);
    s->m = create ? create(m) : nullptr;
    s->voice_type = create && create == sl.create_v;
}

template <int K>
void construct_k(void *mem, int area) { construct_at(mem, K, area); }
template <int... K>
constexpr std::array<void (*)(void *, int), sizeof...(K)> table(std::integer_sequence<int, K...>) { return {&construct_k<K>...}; }

void construct_plain(void *mem) { construct_at(mem, 0, kVoiceArea); }   // (never used: the runner calls construct_in)

void destroy(void *mem) {
    State *s = static_cast<State *>(mem);
    if (s->m) s->m->~Module();                          // (its heap goes with the block)
    s->~State();
}

void start(void *mem, const Ctx &) { static_cast<State *>(mem)->started = true; }

void process(void *mem, const Ctx &c, const IO &io) {
    State *s = static_cast<State *>(mem);
    const Slot &sl = g_slots[s->slot];
    const ModuleDef &d = sl.def;
    if (!s->m) { for (int o = 0; o < d.n_out; o++) for (int i = 0; i < io.frames; i++) io.out[o][i] = 0.f; return; }
    // the values that changed, in the engine's units
    for (int j = 0; j < d.n_param; j++) {
        const int32_t raw = engine_raw_value(&d, j, io.p[j]);
        if (!s->any_sent || raw != s->sent[j]) { s->m->set_param(j, raw); s->sent[j] = raw; }
    }
    s->any_sent = true;
    const bool voice = s->voice_type;                   // (on the FX area: the last note's state, mod::LastNote)
    for (int off = 0; off < io.frames; off += kB) {
        const int n = io.frames - off < kB ? io.frames - off : kB;     // (a short tail: a whole engine block on zeros past it)
        for (int k = 0; k < d.n_in; k++) {
            const float g = sl.pitch_in[k] ? 32768.f / 6.f : 32768.f;       // (a pitch: octaves -> the engine's CV, 72 semitones a unit)
            for (int i = 0; i < kB; i++) {
                const float x = i < n ? io.in[k][off + i] * g : 0.f;
                s->in[k][i] = static_cast<sc::q15>(x > 32767.f ? 32767.f : (x < -32768.f ? -32768.f : (x == x ? x : 0.f)));
            }
        }
        sc::Ports p{};
        for (int k = 0; k < sc::kMaxIn; k++) p.in[k] = s->in[k];
        for (int k = 0; k < sc::kMaxParams; k++) p.mod[k] = nullptr;
        for (int o = 0; o < sc::kMaxOut; o++) p.out[o] = s->out[o];
        sc::ProcessCtx pc{};
        pc.frames = kB;
        pc.time = s->time++;
        pc.transport = &transport();
        if (voice) {
            sc::VoiceState &v = s->vs;
            const float note = 69.f + 12.f * std::log2(c.hz / 440.f);
            v.note = static_cast<int>(std::lround(note));
            v.pitch = v.target_pitch = static_cast<int32_t>(std::lround(note * 256.f));
            v.velocity = static_cast<sc::q15>(clampf(c.velocity, 0.f, 1.f) * 32767.f);
            v.gate = c.gate;
            v.active = true;
            if (s->started) {                                   // the note's first block: as the engine, reset, then `started`
                s->m->reset();
                v.started = true;
                v.age = 0;
                v.release_age = 0;
                s->started = false;
            }
            pc.voice = &v;
            pc.keys_held = c.gate ? 1 : 0;
        }
        s->m->process(pc, p);
        if (voice) {
            s->vs.started = false;
            s->vs.age++;
            if (!s->vs.gate) s->vs.release_age++;
        }
        for (int o = 0; o < d.n_out; o++)
            for (int i = 0; i < n; i++) io.out[o][off + i] = static_cast<float>(s->out[o][i]) * (1.f / 32768.f);
    }
}

bool contains(const char *s, const char *w) { return std::strstr(s, w) != nullptr; }

// The kind an engine module is filed under: the catalog's own categories (modular.h), so the list keeps one group per kind.
const char *kind_of(const sc::ModuleInfo &info) {
    static const struct { const char *name, *kind; } kKinds[] = {
        {"Osc", "Sources"}, {"OscEngines", "Sources"}, {"MiOsc", "Sources"}, {"Strings", "Sources"},
        {"Env", "Envelopes"}, {"EnvG", "Envelopes"}, {"Eg", "Envelopes"}, {"Lfo", "Envelopes"},
        {"Filter", "Filters"}, {"TunedComb", "Filters"}, {"Eq3", "Filters"}, {"Eq3V", "Filters"},
        {"Shaper", "Shapers"}, {"Compressor", "Shapers"}, {"CompressorV", "Shapers"},
        {"Vca", "Utilities"}, {"Mix4", "Utilities"}, {"Mult", "Utilities"}, {"Const", "Utilities"},
    };
    for (const auto &k : kKinds) if (!std::strcmp(k.name, info.name)) return k.kind;
    return "Effects";                                   // (the rest: delay, reverb, chorus, ensemble, spectral, vocoder, phaser ...)
}

// The memory one module asks for when it starts (its heap's high water), with room to spare.
size_t measure(sc::Module *(*create)(sc::Memory &)) {
    static void *scratch = nullptr;
    constexpr size_t kScratch = 16u << 20;
    if (!scratch) scratch = std::malloc(kScratch);
    if (!scratch || !create) return 0;
    sc::Heap h;
    h.init(scratch, kScratch);
    sc::Memory m{&h, &h, nullptr};
    sc::Module *mod = create(m);
    if (!mod) return 0;
    const size_t used = h.high_water();
    mod->~Module();
    return (used + used / 4 + 4096 + 15) / 16 * 16;
}

}  // namespace

int register_engine_modules(const sc::Registry &reg) {
    static const auto ctors = table(std::make_integer_sequence<int, kSlots>{});
    g_reg = &reg;
    int added = 0;
    for (int t = 0; t < sc::Registry::kMax; t++) {
        const sc::ModuleType *mt = reg.get(t);
        if (!mt || !mt->info) continue;
        const sc::ModuleInfo &info = *mt->info;
        bool skip = false;
        for (const char *s : kSkip) if (!std::strcmp(info.name, s)) skip = true;
        if (skip) continue;
        Slot *sl = nullptr;                                 // the same name in the other scope: the same entry
        for (int k = 0; k < g_n; k++) if (!std::strcmp(g_slots[k].info->name, info.name)) sl = &g_slots[k];
        if (!sl) {
            if (g_n >= kSlots) break;
            sl = &g_slots[g_n++];
            sl->info = &info;
        }
        if (info.scope == sc::Scope::Voice) { sl->type_v = t; sl->create_v = mt->create; }
        else { sl->type_g = t; sl->create_g = mt->create; }
        const size_t need = measure(mt->create);
        if (need > sl->pool) sl->pool = need;
    }
    for (int k = 0; k < g_n; k++) {
        Slot &sl = g_slots[k];
        const sc::ModuleInfo &info = *sl.info;
        ModuleDef &d = sl.def;
        std::snprintf(sl.name, sizeof sl.name, "RM%s", info.name);
        std::snprintf(sl.doc, sizeof sl.doc, "The Ritual Machine's %s (framework/engine/modules), played as the engine plays it", info.name);
        d.name = sl.name;
        d.category = kind_of(info);
        d.doc = sl.doc;
        d.scope = Scope::Both;                              // (the scope the engine lacks: the other type, built there; engine_type_in)
        d.n_in = info.n_in;
        d.n_out = info.n_out;
        d.n_param = info.n_param;
        for (int i = 0; i < info.n_in; i++) {
            d.in_name[i] = info.in_name[i];
            sl.pitch_in[i] = !std::strcmp(info.in_name[i], "pitch");
            d.in_def[i] = sl.pitch_in[i] ? kPitch : (!std::strcmp(info.in_name[i], "gate") ? kGate : kZero);
        }
        for (int o = 0; o < info.n_out; o++) d.out_name[o] = info.out_name[o];
        for (int j = 0; j < info.n_param; j++) {
            const sc::ParamDesc &pd = info.param[j];
            Param &q = d.param[j];
            q.name = pd.name;
            const bool q15 = pd.max == 32767 && (pd.min == 0 || pd.min == -32767 || pd.min == -32768);
            const bool semis = !q15 && pd.max >= 24 * 256 && pd.max % 256 == 0 && pd.min % 256 == 0 &&
                               (contains(pd.name, "pitch") || contains(pd.name, "cut") || contains(pd.name, "damp") || contains(pd.name, "bandwidth"));
            if (q15) {
                sl.show[j] = kPercent;
                q.min = pd.min < 0 ? -100.f : 0.f; q.max = 100.f; q.def = static_cast<float>(pd.def) / 327.67f; q.unit = "%"; q.curve = Lin;
            } else if (semis) {
                sl.show[j] = kSemis;
                q.min = static_cast<float>(pd.min) / 256.f; q.max = static_cast<float>(pd.max) / 256.f; q.def = static_cast<float>(pd.def) / 256.f;
                q.unit = "st"; q.curve = Lin;
            } else {
                sl.show[j] = kRaw;
                q.min = static_cast<float>(pd.min); q.max = static_cast<float>(pd.max); q.def = static_cast<float>(pd.def);
                q.curve = pd.max - pd.min <= 64 ? Int : Lin;
            }
            q.def = q.def < q.min ? q.min : (q.def > q.max ? q.max : q.def);
        }
        d.engine = info.name;
        d.origin = "the Ritual Machine's engine";
        d.cpp = nullptr;                                    // (no struct of modules.h: a generated synth cannot hold it)
        d.size = kHeader + sl.pool;
        d.align = 16;
        d.construct = &construct_plain;
        d.destroy = &destroy;
        d.start = &start;
        d.process = &process;
        d.construct_in = ctors[static_cast<size_t>(k)];
        if (catalog_add(&d)) added++;
    }
    return added;
}

bool is_engine_module(const ModuleDef *d) {
    for (int k = 0; k < g_n; k++) if (d == &g_slots[k].def) return true;
    return false;
}

int32_t engine_raw_value(const ModuleDef *d, int j, float v) {
    for (int k = 0; k < g_n; k++) {
        const Slot &sl = g_slots[k];
        if (d != &sl.def) continue;
        const sc::ParamDesc &pd = sl.info->param[j];
        float raw = v;
        if (sl.show[j] == kPercent) raw = v * 327.67f;
        else if (sl.show[j] == kSemis) raw = v * 256.f;
        long r = std::lround(raw);
        if (r < pd.min) r = pd.min;
        if (r > pd.max) r = pd.max;
        return static_cast<int32_t>(r);
    }
    return static_cast<int32_t>(std::lround(v));
}

const char *engine_param_name(const ModuleDef *d, int j) { return d->param[j].name; }

bool engine_type_in(const char *type, int area) {
    if (!g_reg || !type) return false;
    const sc::Scope want = area == kFxArea ? sc::Scope::Global : sc::Scope::Voice;
    for (int t = 0; t < sc::Registry::kMax; t++) {
        const sc::ModuleType *mt = g_reg->get(t);
        if (mt && mt->info && !std::strcmp(mt->info->name, type) && mt->info->scope == want) return true;
    }
    return false;
}

}  // namespace mod
