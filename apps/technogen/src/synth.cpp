// TechnoGen's sound (synth.h).
//
//   global:  T1 KICK --L/R--> T2 BASS --> T3 DRUM --> T4 PERC --> T5 PAD --> T6 TEXT --> T7 SAMP --> MasterOut
//
// Each track module adds its sound to what comes in (tg_tracks.h). The pads' sequencer plays the pattern: pad p of a hit is lane p % 8 of
// track p / 8, routed to that track's module (Engine::pseq_route). The engine's voices are not used yet (the pad's paraphonic strings will).
#include "synth.h"
#include <cstring>
#include "engine/core/engine.h"
#include "engine/modules/builtin.h"
#include "tg_tracks.h"

using namespace sc;

namespace {
enum Node : int { N_TRACK0 = 1, N_MASTER = N_TRACK0 + tg::kTracks };   // the tracks are nodes 1..7

Heap fast_heap, bulk_heap;
Engine eng;
q15 block_l[kBlock], block_r[kBlock];
int block_pos = kBlock;                 // frames of the rendered block already handed out
bool ready = false;
int n_hits = 0;

bool build_graph(GraphDesc &g) {
    const Registry &reg = eng.registry();
    for (int t = 0; t < tg::kTracks; t++) {
        NodeDesc *n = g.add_node(reg, N_TRACK0 + t, tg::kTypeTrack);
        if (!n) return false;
        n->param[tg::TP_KIND] = t;
    }
    if (!g.add_node(reg, N_MASTER, T_MASTER_OUT)) return false;
    bool ok = true;
    for (int t = 0; t + 1 < tg::kTracks; t++) {
        ok &= g.connect(N_TRACK0 + t, 0, N_TRACK0 + t + 1, Dst::In, 0);
        ok &= g.connect(N_TRACK0 + t, 1, N_TRACK0 + t + 1, Dst::In, 1);
    }
    ok &= g.connect(N_TRACK0 + tg::kTracks - 1, 0, N_MASTER, Dst::In, 0);
    ok &= g.connect(N_TRACK0 + tg::kTracks - 1, 1, N_MASTER, Dst::In, 1);
    return ok;
}

// The level of a hit (0..15) for a kind of step.
int hit_level(int kind) { return kind == tg::S_ACCENT ? 15 : 10; }
}  // namespace

bool synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes) {
    fast_heap.init(fast, fast_bytes);
    Memory mem{&fast_heap, &fast_heap};
    if (bulk && bulk != fast) {
        bulk_heap.init(bulk, bulk_bytes);
        fast_heap.set_spill(&bulk_heap);            // a full fast heap takes from the bulk one instead of failing
        mem.bulk = &bulk_heap;
    }
    if (!eng.init(mem, 1)) return false;            // (one voice: the graph has no voice-scope module)
    tg::register_track_module(eng.registry());

    static GraphDesc g;                             // large: not on the stack
    g = GraphDesc{};
    if (!build_graph(g)) return false;
    if (eng.load(g, 1) != Err::Ok) return false;
    int8_t route[kPadSeqGroups];
    for (int k = 0; k < kPadSeqGroups; k++) route[k] = static_cast<int8_t>(k < tg::kTracks ? N_TRACK0 + k : -1);
    eng.pseq_route(route);
    block_pos = kBlock;
    ready = true;
    return true;
}

void synth_shutdown() {
    if (!ready) return;
    ready = false;
    eng.shutdown();
}

void synth_render(int16_t *stereo, int frames) {
    if (!ready) { std::memset(stereo, 0, static_cast<size_t>(frames) * 4); return; }
    for (int i = 0; i < frames; i++) {              // the engine renders blocks of kBlock frames: hand them out frame by frame
        if (block_pos >= kBlock) { eng.render(block_l, block_r); block_pos = 0; }
        stereo[2 * i] = block_l[block_pos];
        stereo[2 * i + 1] = block_r[block_pos];
        block_pos++;
    }
}

void synth_update() { if (ready) eng.gc(); }

void synth_set_track(int t, const tg::Track &tr) {
    if (!ready || t < 0 || t >= tg::kTracks) return;
    for (int k = 0; k < tg::kParams; k++) eng.set_param(N_TRACK0 + t, tg::TP_V0 + k, tr.value[k]);
    eng.set_param(N_TRACK0 + t, tg::TP_LEVEL, tr.muted ? 0 : tr.level);
}

void synth_set_steps(const tg::Pattern &p) {
    if (!ready) return;
    static PadHit hits[kPadSeqHits];
    int n = 0;
    for (int t = 0; t < tg::kTracks; t++) {         // track by track: if the pattern is too full, the last tracks lose their hits
        const tg::Track &tr = p.track[t];
        if (tr.muted) continue;
        for (int lane = 0; lane < tg::track_def(t).lanes; lane++)
            for (int s = 0; s < tg::kSteps && n < kPadSeqHits; s++) {
                const int kind = tr.step[lane][s];
                if (kind != tg::S_HIT && kind != tg::S_ACCENT) continue;
                int len = 1;                        // a held chord: the hit lasts over the S_HOLD steps after it
                while (s + len < tg::kSteps && tr.step[lane][s + len] == tg::S_HOLD) len++;
                PadHit h = {};
                h.pad = static_cast<uint32_t>(t * 8 + lane);
                h.step = static_cast<uint32_t>(s);
                h.level = static_cast<uint32_t>(hit_level(kind));
                h.prob = 10;
                h.len = static_cast<uint32_t>(len - 1);
                hits[n++] = h;
            }
    }
    for (int first = 0; first < n || first == 0; first += kPadSeqChunk) {
        const int count = n - first < kPadSeqChunk ? n - first : kPadSeqChunk;
        eng.pseq_hits(first, count, hits + first, n);
        if (n == 0) break;
    }
    n_hits = n;
}

void synth_set_transport(int bpm, bool running) {
    if (!ready) return;
    SeqSetup s;
    s.run = running ? 1 : 0;
    s.steps = tg::kSteps;
    s.rate = SEQ_RATE_16;
    eng.pseq_setup(s, N_TRACK0);                    // (its node is not used: the hits are routed)
    eng.set_transport(bpm, running, false);
}

int synth_step() { return ready ? eng.pseq_step() : -1; }
int synth_hits() { return n_hits; }
int synth_max_hits() { return kPadSeqHits; }
int synth_sample_rate() { return kSampleRate; }
