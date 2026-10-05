#if defined(PLATFORM_SIM) || defined(ARDUINO_ARCH_ESP32)
#include "platform/engine/engine_synth.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include "engine/core/engine.h"
#include "engine/modules/builtin.h"
#include "engine/modules/dx7_voice.h"
#include "engine/modules/fx_modules.h"
#include "engine/modules/fx2_modules.h"
#include "engine/modules/motion_seq.h"
#include "engine/modules/osc_engines.h"
#include "engine/modules/sampler_modules.h"
#include "engine/sampler/sample_bank.h"
#include "engine/modules/synth_modules.h"
#include "platform/engine/rack_graph.h"
#include "platform/engine/sample_catalog.h"

using namespace sc;

namespace {

struct Synth {
    Heap fast, bulk;
    Engine eng;
    RackGraph last, cur;
    bool have_last = false;
    q15 bl[kBlock], br[kBlock];
    int bpos = kBlock;                 // frames of the current block already handed out (kBlock = need a new block)
    Memory mem;
    bool (*importer)(int) = nullptr;
    SampleBank bank;
    bool bank_on = false;
    audio_sample_info_t cat[AUDIO_SAMPLES_MAX];
    int cat_n = 0;
    int16_t slot_of[AUDIO_SAMPLES_MAX];       // catalog index -> bank slot (-1 = not loaded; samples stay loaded once used)
    std::mutex mx;                            // control calls (build, params, catalog) vs the loader thread; never taken by the audio thread
    int clk_bpm = 110, clk_steps = 16, clk_swing = 0, clk_run = 0, clk_restart = 0;   // the note sequencer's clock, for the motion sequencers
};

Synth s_synth;
bool s_ready = false;

// Converts the pending .wav / .mp3 files the rack uses (outside the lock: the importer updates the catalog through engine_synth_catalog_set).
void prepare_samples(const rack_t &rack) {
    Synth &s = s_synth;
    if (!s.importer) return;
    for (int i = 0; i < rack.count; i++) {
        if (rack.slot[i].type != MOD_SAMPLER) continue;
        const int f = static_cast<int>(rack.slot[i].v[MP_SM_FILE]) - 1;
        bool pending = false;
        {
            std::lock_guard<std::mutex> lk(s.mx);
            pending = f >= 0 && f < s.cat_n && s.cat[f].pending;
        }
        if (pending) s.importer(f);
    }
}

// Makes sure every sample the rack uses is being loaded; fills slot_of for the mapper.
void resolve_samples(const rack_t &rack) {
    Synth &s = s_synth;
    if (!s.bank_on) return;
    for (int i = 0; i < rack.count; i++) {
        if (rack.slot[i].type != MOD_SAMPLER) continue;
        const int f = static_cast<int>(rack.slot[i].v[MP_SM_FILE]) - 1;
        if (f < 0 || f >= s.cat_n || s.slot_of[f] >= 0 || s.cat[f].pending) continue;
        char path[40];
        std::snprintf(path, sizeof path, "%s.smp", s.cat[f].name);
        s.slot_of[f] = static_cast<int16_t>(s.bank.load(path, 150));
    }
}

// Same nodes and cables; cable depths may differ (they are applied live, see engine_synth_set_params).
bool same_structure(const GraphDesc &a, const GraphDesc &b) {
    if (a.n_nodes != b.n_nodes || a.n_edges != b.n_edges) return false;
    for (int i = 0; i < a.n_nodes; i++) if (a.node[i].id != b.node[i].id || a.node[i].type != b.node[i].type) return false;
    for (int i = 0; i < a.n_edges; i++) {
        const EdgeDesc &x = a.edge[i], &y = b.edge[i];
        if (x.src_id != y.src_id || x.src_port != y.src_port || x.dst_id != y.dst_id || x.dst_port != y.dst_port ||
            x.dst_kind != y.dst_kind || x.delayed != y.delayed) return false;
    }
    return true;
}

void send_blob(const RackGraph &r) {
    if (r.fm) s_synth.eng.set_blob(r.dx7_node, &r.fm_patch, sizeof r.fm_patch);
    for (int i = 0; i < r.ms_count; i++) s_synth.eng.set_blob(r.ms_node[i], &r.ms_blob[i], sizeof(MotionBlob));
}

void send_clock(const RackGraph &r) {                 // the motion sequencers' timing (not part of the rack: it follows the note sequencer)
    Synth &s = s_synth;
    for (int i = 0; i < r.ms_count; i++) {
        const int id = r.ms_node[i];
        s.eng.set_param(id, MSP_BPM, s.clk_bpm);
        s.eng.set_param(id, MSP_STEPS, s.clk_steps);
        s.eng.set_param(id, MSP_SWING, s.clk_swing);
        s.eng.set_param(id, MSP_RESTART, s.clk_restart);
        s.eng.set_param(id, MSP_RUN, s.clk_run);
    }
}

}  // namespace

extern "C" {

int engine_synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes) {
    Synth &s = s_synth;
    s.fast.init(fast, fast_bytes);
    s.bulk.init(bulk, bulk_bytes);
    if (fast != bulk) s.fast.set_spill(&s.bulk);              // a full fast heap falls back to the big one instead of failing to create a module
    s.mem = Memory{&s.fast, &s.bulk, &s.bank};
    s.eng.init(s.mem, SYNTH_MAX_VOICES);
    s.bank_on = false; s.cat_n = 0;
    for (auto &v : s.slot_of) v = -1;
    register_sampler_modules(s.eng.registry());
    register_synth_modules(s.eng.registry());
    register_fx_modules(s.eng.registry());
    register_fx2_modules(s.eng.registry());
    register_dx7_module(s.eng.registry());
    register_motion_module(s.eng.registry());
    register_osc_engines(s.eng.registry());
    s.have_last = false;
    s.bpos = kBlock;
    s_ready = true;
    return 0;
}

void engine_synth_shutdown(void) {
    if (!s_ready) return;
    s_ready = false;                  // the platform stops its audio callback before calling this
    std::lock_guard<std::mutex> lk(s_synth.mx);
    s_synth.eng.shutdown();
    if (s_synth.bank_on) s_synth.bank.shutdown();
    s_synth.bank_on = false;
}

unsigned s_builds = 0;
char s_reason[48] = "first";

const char *engine_synth_build_reason(void) { return s_reason; }

unsigned engine_synth_build_count(void) { return s_builds; }

void engine_synth_heap_stats(size_t *fast_used, size_t *fast_cap, size_t *fast_high, size_t *bulk_used) {
    if (fast_used) *fast_used = s_synth.fast.used();
    if (fast_cap) *fast_cap = s_synth.fast.capacity();
    if (fast_high) *fast_high = s_synth.fast.high_water();
    if (bulk_used) *bulk_used = s_synth.bulk.used();
}

void engine_synth_set_osc_engine(int index, int engine) {
    if (!s_ready) return;
    Synth &s = s_synth;
    if (!s.have_last) return;
    int k = 0;
    for (int i = 0; i < s.last.g.n_nodes; i++) {
        if (s.last.g.node[i].type != T_OSCX) continue;
        if (k++ == index) { s.eng.set_param(s.last.g.node[i].id, OSCX_ENGINE, engine); return; }
    }
}

void engine_synth_build(const rack_t *rack, const synth_params_t *params) {
    if (!s_ready) return;
    s_builds++;
    Synth &s = s_synth;
    prepare_samples(*rack);
    std::lock_guard<std::mutex> lk(s.mx);
    resolve_samples(*rack);
    if (!rack_graph_build(*rack, *params, s.eng.registry(), s.cur, s.slot_of, s.cat_n)) return;
    if (s.eng.load(s.cur.g) != Err::Ok) return;
    send_blob(s.cur);
    send_clock(s.cur);
    s.last = s.cur;
    s.have_last = true;
    s.eng.gc();
}

void engine_synth_set_params(const rack_t *rack, const synth_params_t *params) {
    if (!s_ready) return;
    Synth &s = s_synth;
    if (!s.have_last) { engine_synth_build(rack, params); return; }
    prepare_samples(*rack);
    {
        std::lock_guard<std::mutex> lk(s.mx);
        resolve_samples(*rack);
        if (!rack_graph_build(*rack, *params, s.eng.registry(), s.cur, s.slot_of, s.cat_n)) return;
    }
    if (!same_structure(s.last.g, s.cur.g) || s.last.fm != s.cur.fm) {
        std::snprintf(s_reason, sizeof s_reason, "structure n%d/%d e%d/%d fm%d/%d", s.last.g.n_nodes, s.cur.g.n_nodes, s.last.g.n_edges, s.cur.g.n_edges, s.last.fm, s.cur.fm);
        engine_synth_build(rack, params); return;
    }
    for (int k = 0; k < s.cur.g.n_edges; k++)                         // modulation amounts: a gain write, not a rebuild (unless a cable is, or becomes, exactly unity)
        if (s.cur.g.edge[k].depth != s.last.g.edge[k].depth && !s.eng.set_edge_depth(k, s.cur.g.edge[k].depth)) {
            std::snprintf(s_reason, sizeof s_reason, "edge %d depth %d -> %d", k, s.last.g.edge[k].depth, s.cur.g.edge[k].depth);
            engine_synth_build(rack, params); return;
        }
    for (int i = 0; i < s.cur.g.n_nodes; i++) {                       // same graph: only values moved
        const NodeDesc &n = s.cur.g.node[i], &o = s.last.g.node[i];
        for (int p = 0; p < kMaxParams; p++)
            if (n.param[p] != o.param[p]) s.eng.set_param(n.id, p, n.param[p]);
    }
    if (s.cur.fm && std::memcmp(&s.cur.fm_patch, &s.last.fm_patch, sizeof(Dx7Patch)) != 0) send_blob(s.cur);
    for (int i = 0; i < s.cur.ms_count; i++)
        if (std::memcmp(&s.cur.ms_blob[i], &s.last.ms_blob[i], sizeof(MotionBlob)) != 0) s.eng.set_blob(s.cur.ms_node[i], &s.cur.ms_blob[i], sizeof(MotionBlob));
    s.last = s.cur;
}

void engine_synth_set_clock(int bpm, int steps, int swing, int running) {
    if (!s_ready) return;
    Synth &s = s_synth;
    const int run = running ? 1 : 0;
    if (bpm == s.clk_bpm && steps == s.clk_steps && swing == s.clk_swing && run == s.clk_run) return;     // called every UI step
    s.clk_bpm = bpm; s.clk_steps = steps; s.clk_swing = swing; s.clk_run = running ? 1 : 0;
    if (s.have_last) send_clock(s.last);
}

void engine_synth_motion_restart(void) {
    if (!s_ready) return;
    Synth &s = s_synth;
    s.clk_restart = (s.clk_restart + 1) & 0x7FFFFFFF;
    if (s.have_last) for (int i = 0; i < s.last.ms_count; i++) s.eng.set_param(s.last.ms_node[i], MSP_RESTART, s.clk_restart);
}

void engine_synth_note_on(int note)  { if (s_ready) s_synth.eng.note_on(note, 100); }
void engine_synth_note_off(int note) { if (s_ready) s_synth.eng.note_off(note); }

void engine_synth_render(int16_t *stereo, int frames) {
    if (!s_ready) { std::memset(stereo, 0, static_cast<size_t>(frames) * 4); return; }
    Synth &s = s_synth;
    for (int i = 0; i < frames; i++) {
        if (s.bpos >= kBlock) { s.eng.render(s.bl, s.br); s.bpos = 0; }
        stereo[2 * i] = s.bl[s.bpos];
        stereo[2 * i + 1] = s.br[s.bpos];
        s.bpos++;
    }
}

int engine_synth_sample_count(void) { return s_ready ? s_synth.cat_n : 0; }

bool engine_synth_sample_info(int index, audio_sample_info_t *out) {
    if (!s_ready) return false;
    std::lock_guard<std::mutex> lk(s_synth.mx);
    if (index < 0 || index >= s_synth.cat_n) return false;
    *out = s_synth.cat[index];
    return true;
}

void engine_synth_io_pump(void) {
    if (!s_ready) return;
    Synth &s = s_synth;
    std::lock_guard<std::mutex> lk(s.mx);
    if (s.bank_on) s.bank.pump(s.eng.blocks() * static_cast<uint64_t>(kBlock) * 1000000ull / static_cast<uint64_t>(kSampleRate));
}

#ifdef ENGINE_PROFILE
void engine_synth_profile(void (*cb)(const char *, uint32_t, uint32_t, void *), void *user, uint32_t *blocks) {
    Engine::ProfEntry e[Engine::kProfMax];
    const int n = s_ready ? s_synth.eng.prof_take(e, Engine::kProfMax, blocks) : 0;
    if (!n) { *blocks = 0; return; }
    const uint32_t b = *blocks ? *blocks : 1;
    for (int i = 0; i < n; i++) cb(e[i].name, static_cast<uint32_t>(e[i].cycles / b), e[i].calls / b, user);
}
#endif

int engine_synth_sample_rate(void) { return kSampleRate; }

uint32_t engine_synth_millis(void) {
    return static_cast<uint32_t>(s_synth.eng.blocks() * static_cast<uint64_t>(kBlock) * 1000u / static_cast<uint64_t>(kSampleRate));
}

}  // extern "C"

bool engine_synth_attach_storage(StorageDevice *dev) {
    if (!s_ready || !dev) return false;
    std::lock_guard<std::mutex> lk(s_synth.mx);
    s_synth.bank_on = s_synth.bank.init(*dev, s_synth.mem);
    return s_synth.bank_on;
}

void engine_synth_set_importer(bool (*convert)(int index)) {
    std::lock_guard<std::mutex> lk(s_synth.mx);
    s_synth.importer = convert;
}

void engine_synth_catalog_set(const audio_sample_info_t *infos, int n) {
    if (!s_ready) return;
    std::lock_guard<std::mutex> lk(s_synth.mx);
    if (n > AUDIO_SAMPLES_MAX) n = AUDIO_SAMPLES_MAX;
    for (int i = 0; i < n; i++) s_synth.cat[i] = infos[i];
    if (n > s_synth.cat_n) s_synth.cat_n = n;
}
#endif  // PLATFORM_SIM || ARDUINO_ARCH_ESP32
