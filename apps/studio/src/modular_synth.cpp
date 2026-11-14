// The studio's Modular synth (modular_synth.h).
#include "modular_synth.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include "synth.h"

namespace {

lab::Param g_params[lab::kMaxParams];
char g_titles[mod::kMaxNodes][24];
mod::Patch g_pub[2];                                    // the published structures: the voices read g_pub[g_cur]
std::atomic<int> g_cur{0};
std::atomic<uint32_t> g_ver{1};
mod::LastNote g_last;                                   // the FX area's note (the voices write it, the FX reads it: the audio thread)

const mod::Patch &published() { return g_pub[g_cur.load(std::memory_order_acquire)]; }

mod::Ctx make_ctx(const lab::Note &n) {
    mod::Ctx c;
    c.sr = n.sr;
    c.inv_sr = 1.f / n.sr;
    c.hz = n.hz;
    c.velocity = n.velocity;
    c.gate = n.gate;
    return c;
}

struct Voice {
    static constexpr size_t kArena = studio::kModularVoiceBytes;    // a voice's modules (a Delay is 128 KB; the Ritual Machine's: their own heaps)
    alignas(16) unsigned char mem[kArena];
    mod::Runner run;
    uint32_t ver = 0;
    int quiet = 0;
    ~Voice() { g_last.voice_gone(this); }
    void sync() {
        const uint32_t v = g_ver.load(std::memory_order_acquire);
        if (v != ver) { ver = v; run.build(published(), mod::kVoiceArea, mem, kArena); }
    }
    void start(const lab::Note &n, const float *) { sync(); run.start(make_ctx(n)); quiet = 0; g_last.note_start(this, n.hz, n.velocity); }
    void release(const lab::Note &, const float *) {}
    bool render(float *out, int frames, const lab::Note &n, const float *p) {
        sync();
        g_last.voice_block(this, n.gate, n.hz);
        const float peak = run.process(make_ctx(n), p, nullptr, out, frames);
        if (n.gate || peak > 1e-4f) { quiet = 0; return true; }
        return ++quiet < 16;                            // (about 10 ms of silence after the key: free)
    }
};

struct Fx {
    static constexpr size_t kArena = studio::kModularFxBytes;       // (the Ritual Machine's reverb, delay, chorus ... each with its own lines)
    alignas(16) unsigned char mem[kArena];
    mod::Runner run;
    uint32_t ver = 0, seen = 0;
    float tmp[512];
    void render(float *io, int frames, float sr, const float *p) {
        const uint32_t v = g_ver.load(std::memory_order_acquire);
        if (v != ver) { ver = v; run.build(published(), mod::kFxArea, mem, kArena); }
        mod::Ctx c;
        c.sr = sr;
        c.inv_sr = 1.f / sr;
        if (g_last.fx_block(c, seen)) run.start(c);     // (the last note: its pitch, velocity, gate; a new one restarts the FX's envelopes)
        for (int off = 0; off < frames; off += 512) {
            const int m = frames - off < 512 ? frames - off : 512;
            for (int i = 0; i < m; i++) tmp[i] = 0.f;
            if (run.empty()) continue;                  // (no FX area: the voices as they are)
            run.process(c, p, io + off, tmp, m);
            for (int i = 0; i < m; i++) io[off + i] = tmp[i];
        }
    }
};

lab::Synth make() {
    lab::Synth s = lab::make_synth<Voice, Fx>("Modular", g_params);
    s.n_params = 0;                                     // (modular_publish sets the list)
    return s;
}

}  // namespace

lab::Synth synth_modular = make();

namespace studio {

int modular_index() {
    for (int i = 0; i < lab::kSynthCount; i++) if (lab::kSynths[i] == &synth_modular) return i;
    return -1;
}

int modular_slot(const mod::Patch &p, int node_index, int param) {
    int base[mod::kMaxNodes];
    const int n = mod::patch_slots(p, base);
    if (node_index < 0 || node_index >= p.n_nodes || base[node_index] < 0) return -1;
    const int s = base[node_index] + 1 + param;
    return s < n && s < lab::kMaxParams ? s : -1;
}

void modular_publish(const mod::Patch &p) {
    int base[mod::kMaxNodes];
    int n = mod::patch_slots(p, base);
    if (n > lab::kMaxParams) n = lab::kMaxParams;
    for (int i = 0; i < p.n_nodes; i++) {
        if (base[i] < 0 || base[i] >= n) continue;
        const mod::Node &nd = p.node[i];
        std::snprintf(g_titles[i], sizeof g_titles[i], "%s %d", nd.def->name, nd.id);
        g_params[base[i]] = lab::section(g_titles[i]);
        for (int j = 0; j < nd.def->n_param && base[i] + 1 + j < n; j++) {
            const mod::Param &q = nd.def->param[j];
            g_params[base[i] + 1 + j] = lab::Param{q.name, q.min, q.max, nd.v[j], q.unit, static_cast<lab::Curve>(q.curve), q.labels};   // (its default: the patch's value)
        }
    }
    synth_modular.n_params = n;
    const int next = 1 - g_cur.load();
    g_pub[next] = p;
    g_cur.store(next, std::memory_order_release);
    g_ver.fetch_add(1, std::memory_order_acq_rel);
}

void modular_push_values(const mod::Patch &p) {
    if (synth_selected() != modular_index()) return;
    for (int i = 0; i < p.n_nodes; i++)
        for (int j = 0; j < p.node[i].def->n_param; j++) {
            const int s = modular_slot(p, i, j);
            if (s >= 0) synth_set_value(s, p.node[i].v[j]);
        }
}

void modular_pull_values(mod::Patch &p) {
    if (synth_selected() != modular_index()) return;
    for (int i = 0; i < p.n_nodes; i++)
        for (int j = 0; j < p.node[i].def->n_param; j++) {
            const int s = modular_slot(p, i, j);
            if (s >= 0) p.node[i].v[j] = synth_value(s);
        }
}

}  // namespace studio
