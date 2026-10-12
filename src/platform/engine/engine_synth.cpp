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
#include <cmath>
#include "engine/modules/mi_osc.h"
#include "engine/modules/osc_engines.h"
#include "engine/modules/sampler_modules.h"
#include "engine/modules/para_modules.h"
#include "engine/modules/strings_modules.h"
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
    bool pads_have_last = false;              // `last` holds the pads graph (the Pads section; have_last stays false: the synth's is gone)
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
    int clk_bpm = 110, clk_run = 0;           // the machine clock as last set
    SeqSetup seq_setup;                       // the step sequencer as the engine has it (seq_sent: all of it was sent once)
    SeqStep seq_step[kSeqSteps];
    bool seq_sent = false;
    uint32_t seq_taken_seen = 0;              // Engine::seq_taken() as last looked at
    bool pseq_sent = false;                   // the pads' sequencer: the pattern (its hash) and the run state the engine has
    uint32_t pseq_hash = 0, pseq_taken_seen = 0;
    int pseq_run = 0;
    bool click = false;                       // the metronome (the transport's click)
    int last_nv = 0, last_glide = 0, last_legato = 0;      // voice count / note handling of the loaded graph (a different count is a rebuild)
    bool last_mono = false;
    bool pads_mode = false;                   // the pads player is loaded instead of the synth (engine_synth_pads_build)
    int32_t pad_events = 0;                   // the counter of the pad events (sampler_modules.h: (n << 8) | pad)
    size_t resident_bytes = 0;                // pad samples kept whole in memory (kPadResidentBudget)
    bool retire[AUDIO_SAMPLES_MAX] = {};      // the entry was removed while its sample was loaded: retire_samples unloads it
    uint32_t retire_blk[AUDIO_SAMPLES_MAX] = {};   // ... the engine block it was removed in
    int retire_n = 0;
    bool gc_due = false;                      // a graph was loaded: free the old one once the audio thread has switched
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
        char path[AUDIO_NAME_MAX + 8];
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

// Note handling of the synth type: Mono (one voice, last-note priority, legato, glide) or Poly. Posted like a note; cheap enough to resend.
void send_voice_mode(const rack_t &rack) {
    Synth &s = s_synth;
    const bool mono = synth_config_is_mono(&rack.cfg);
    const int glide = synth_config_glide_ms(&rack.cfg);
    const int legato = mono && rack.cfg.legato ? 1 : 0;
    s.eng.set_voice_mode(mono ? VoiceMode::Mono : VoiceMode::Poly, legato != 0, glide);
    s.last_mono = mono; s.last_glide = glide; s.last_legato = legato;
}

// The impulse response of Convolver k: computed here (control thread; only when its IR or length changed) and sent in chunks; the module
// switches when the last one arrives.
void send_conv(const RackGraph &r, int k) {
    ConvBlob b[(kConvMaxTaps + kConvChunk - 1) / kConvChunk];
    const int len = r.conv_len[k] < 16 ? 16 : (r.conv_len[k] > kConvMaxTaps ? kConvMaxTaps : r.conv_len[k]);
    int16_t taps[kConvMaxTaps];
    conv_builtin_ir(r.conv_ir[k], len, taps);
    const int n = conv_blobs(taps, len, b);
    for (int i = 0; i < n; i++) s_synth.eng.set_blob(r.conv_node[k], &b[i], sizeof(ConvBlob));
}

void send_blob(const RackGraph &r) {
    if (r.fm) s_synth.eng.set_blob(r.dx7_node, &r.fm_patch, sizeof r.fm_patch);
    for (int i = 0; i < r.ms_count; i++) s_synth.eng.set_blob(r.ms_node[i], &r.ms_blob[i], sizeof(MotionBlob));
    for (int k = 0; k < r.conv_count; k++) send_conv(r, k);
}

/* ---- the Pads section ---- */

constexpr size_t kPadResidentMax = 192 * 1024;         // a pad sample up to this size is kept whole in memory (no card reads while it plays) ...
constexpr size_t kPadResidentBudget = 768 * 1024;      // ... while they stay under this in total; longer ones stream (a 150 ms head)

int catalog_find(const char *name) {                    // [lock held]
    if (!name || !name[0]) return -1;
    for (int i = 0; i < s_synth.cat_n; i++) if (!std::strcmp(s_synth.cat[i].name, name)) return i;
    return -1;
}

// Converts a pending .wav / .mp3 a pad uses (outside the lock, like prepare_samples).
void pad_prepare(const pad_t &p) {
    Synth &s = s_synth;
    if (!s.importer || !p.file[0]) return;
    int f;
    bool pending;
    {
        std::lock_guard<std::mutex> lk(s.mx);
        f = catalog_find(p.file);
        pending = f >= 0 && s.cat[f].pending;
    }
    if (pending) s.importer(f);
}

// The engine's form of a pad [lock held]: its sample loaded (or loading), the trim in frames, the gains and the filter.
PadBlob pad_blob(int idx, const pad_t &p) {
    Synth &s = s_synth;
    PadBlob b;
    b.pad = static_cast<uint8_t>(idx);
    const int f = catalog_find(p.file);
    if (f >= 0 && s.bank_on && !s.cat[f].pending) {
        if (s.slot_of[f] < 0) {
            const size_t bytes = static_cast<size_t>(s.cat[f].frames) * sizeof(q15);
            const bool whole = bytes <= kPadResidentMax && s.resident_bytes + bytes <= kPadResidentBudget;
            char path[AUDIO_NAME_MAX + 8];
            std::snprintf(path, sizeof path, "%s.smp", s.cat[f].name);
            s.slot_of[f] = static_cast<int16_t>(s.bank.load(path, whole ? 0 : 150));
            if (whole && s.slot_of[f] >= 0) s.resident_bytes += bytes;
        }
        b.slot = s.slot_of[f];
        const audio_sample_info_t &in = s.cat[f];
        const uint32_t frames = in.frames, end = pad_end(&p, frames);
        b.start = static_cast<int32_t>(p.start + 2 <= frames ? p.start : (frames > 2 ? frames - 2 : 0));
        b.end = end >= frames ? 0 : static_cast<int32_t>(end);
        const bool file_loop = in.loop_mode != 0 && in.loop_end > in.loop_start + 1;
        b.lstart = static_cast<int32_t>(p.loop == PAD_LOOP_FILE && file_loop ? in.loop_start : pad_lstart(&p, frames));
        b.xfade = static_cast<int32_t>(static_cast<uint64_t>(p.xfade_ms) * in.rate / 1000);
    }
    b.bus = static_cast<uint8_t>(p.bus < PAD_BUSES ? p.bus : 0);
    b.mode = p.mode == PAD_GATE ? PADM_GATE : p.mode == PAD_TOGGLE ? PADM_TOGGLE : PADM_ONESHOT;
    b.loop = p.loop == PAD_LOOP_FILE ? 2 : p.loop == PAD_LOOP_TRIM ? 1 : 0;
    b.choke = p.choke;
    b.grid = static_cast<uint8_t>(pad_grid_beats(p.grid));
    if (f >= 0 && s.bank_on && !s.cat[f].pending && p.sync && p.loop != PAD_LOOP_OFF && b.lstart > b.start) {
        b.sync = 1;                                                             // the anchor: the loop start, reached after the pre-roll
        const double ratio = std::pow(2.0, (p.tune * 100 + p.fine) / 1200.0) * s.cat[f].rate / kSampleRate;
        b.preroll = p.from ? 0 : static_cast<int32_t>((b.lstart - b.start) / ratio);
    }
    if (p.next) {                                                               // a neighbour on the bank's 4 x 4 grid (none past an edge)
        const int bank = idx / PADS_PER_BANK * PADS_PER_BANK, k = idx % PADS_PER_BANK, r = k / 4, c = k % 4;
        const int nr = r + (p.next == PAD_NEXT_DOWN) - (p.next == PAD_NEXT_UP), nc = c + (p.next == PAD_NEXT_RIGHT) - (p.next == PAD_NEXT_LEFT);
        if (nr >= 0 && nr < 4 && nc >= 0 && nc < 4) b.next = static_cast<int8_t>(bank + nr * 4 + nc);
    }
    b.from = p.from;
    b.xpost = p.xpost;
    b.mute = p.mute ? 1 : 0;
    b.tune = (static_cast<int32_t>(p.tune) * 100 + p.fine) * 256 / 100;
    b.tail = static_cast<int32_t>(p.tail_ms) * kSampleRate / 1000;
    const float lvl = p.level / 100.0f, pan = p.pan / 50.0f;                   // a balance: the far side goes down, the near side stays
    b.gain_l = static_cast<q15>(std::lround(32767.0f * lvl * (pan > 0.0f ? 1.0f - pan : 1.0f)));
    b.gain_r = static_cast<q15>(std::lround(32767.0f * lvl * (pan < 0.0f ? 1.0f + pan : 1.0f)));
    const float hz = pad_cut_hz(&p);
    if (hz > 0.0f) {                                                            // a 12 dB low-pass (TPT SVF), resonance 0..100 -> Q 0.7..12
        const float fc = std::fmin(hz, 0.45f * kSampleRate);
        const float g = std::tan(3.14159265f * fc / kSampleRate), k = 1.0f / (0.7071f + p.res * 0.113f);
        b.filter = 1;
        b.a1 = 1.0f / (1.0f + g * (g + k)); b.a2 = g * b.a1; b.a3 = g * b.a2; b.k = k;
    }
    return b;
}

void pads_master(const rack_t &rack) {                  // the master output of the pads graph follows the rack's volume (as rack_graph does)
    const synth_config_t &cfg = rack.cfg;
    const bool boost = cfg.volume > 1.0f;
    s_synth.eng.set_param(RP_MASTER, 0, static_cast<int32_t>(std::lround(32767.0f * (boost ? cfg.volume * 0.5f : cfg.volume))));
    s_synth.eng.set_param(RP_MASTER, 1, boost ? 1 : 0);
    s_synth.eng.set_param(RP_MASTER, 2, cfg.mono);
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
    register_strings_modules(s.eng.registry());
    register_para_modules(s.eng.registry());
    register_dx7_module(s.eng.registry());
    register_motion_module(s.eng.registry());
    register_osc_engines(s.eng.registry());
    register_mi_osc(s.eng.registry());
    s.have_last = false;
    s.bpos = kBlock;
    s.click = false;
    s.resident_bytes = 0;
    s.clk_bpm = 110; s.clk_run = 0;                         // a new engine: set_clock compares with these
    s.seq_sent = false;                                     // ... and has no pattern yet
    s.seq_taken_seen = 0;
    s.pseq_sent = false; s.pseq_taken_seen = 0;
    s.eng.set_transport(s.clk_bpm, false, false);          // the machine clock: stopped, at the default tempo
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

// The samples whose catalog entry was removed (engine_synth_catalog_remove) are unloaded once nothing can read them: every command
// sent before the removal has reached the audio thread (4 blocks later: a pad pushed on its old settings has started its voice by
// then, and its stream shows it), and the loader and the streams are off the sample (SampleBank::can_unload).
static void retire_samples() {
    Synth &s = s_synth;
    std::lock_guard<std::mutex> lk(s.mx);
    const uint32_t now = static_cast<uint32_t>(s.eng.blocks());
    for (int i = 0; i < AUDIO_SAMPLES_MAX; i++) {
        if (!s.retire[i] || now - s.retire_blk[i] < 4) continue;
        const int slot = s.slot_of[i];
        if (slot >= 0 && !s.bank.can_unload(slot)) continue;
        if (slot >= 0) {
            const SampleSlot *sl = s.bank.slot(slot);
            if (sl && sl->state != SLOT_FREE && sl->head_ms == 0) {             // it was counted as a whole pad sample (pad_blob)
                const size_t bytes = static_cast<size_t>(s.cat[i].frames) * sizeof(q15);
                s.resident_bytes -= bytes < s.resident_bytes ? bytes : s.resident_bytes;
            }
            s.bank.unload(slot);
            s.slot_of[i] = -1;
        }
        s.retire[i] = false;
        s.retire_n--;
    }
}

void engine_synth_housekeeping(void) {
    Synth &s = s_synth;
    if (!s_ready) return;
    if (s.retire_n > 0) retire_samples();
    if (!s.gc_due || s.eng.switching()) return;
    std::lock_guard<std::mutex> lk(s.mx);                                       // (a freed Sampler leaves the loader's stream list)
    s.eng.gc();
    s.gc_due = s.eng.switching();
}

void engine_synth_heap_stats(size_t *fast_used, size_t *fast_cap, size_t *fast_high, size_t *bulk_used, size_t *spilled) {
    if (spilled) *spilled = s_synth.fast.spilled();
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
    s.pads_mode = false;                                    // an explicit build brings the synth back (from the Pads section)
    s.pads_have_last = false;
    prepare_samples(*rack);
    std::lock_guard<std::mutex> lk(s.mx);
    resolve_samples(*rack);
    if (!rack_graph_build(*rack, *params, s.eng.registry(), s.cur, s.slot_of, s.cat_n)) return;
    const int nv = synth_config_voices(&rack->cfg);
    if (s.eng.load(s.cur.g, nv) != Err::Ok) return;
    s.gc_due = true;
    s.last_nv = nv;
    send_voice_mode(*rack);
    send_blob(s.cur);
    s.last = s.cur;
    s.have_last = true;
    s.eng.gc();
}

void engine_synth_set_params(const rack_t *rack, const synth_params_t *params) {
    if (!s_ready) return;
    Synth &s = s_synth;
    if (s.pads_mode) { pads_master(*rack); return; }       // the Pads section: only the master volume is the synth's
    if (!s.have_last) { engine_synth_build(rack, params); return; }
    prepare_samples(*rack);
    {
        std::lock_guard<std::mutex> lk(s.mx);
        resolve_samples(*rack);
        if (!rack_graph_build(*rack, *params, s.eng.registry(), s.cur, s.slot_of, s.cat_n)) return;
    }
    if (synth_config_voices(&rack->cfg) != s.last_nv) {                  // another voice count: the voice modules are created anew
        std::snprintf(s_reason, sizeof s_reason, "voices %d -> %d", s.last_nv, synth_config_voices(&rack->cfg));
        engine_synth_build(rack, params); return;
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
    if (synth_config_is_mono(&rack->cfg) != s.last_mono || synth_config_glide_ms(&rack->cfg) != s.last_glide ||
        (s.last_mono && (rack->cfg.legato ? 1 : 0) != s.last_legato)) send_voice_mode(*rack);
    for (int i = 0; i < s.cur.ms_count; i++)
        if (std::memcmp(&s.cur.ms_blob[i], &s.last.ms_blob[i], sizeof(MotionBlob)) != 0) s.eng.set_blob(s.cur.ms_node[i], &s.cur.ms_blob[i], sizeof(MotionBlob));
    for (int k = 0; k < s.cur.conv_count; k++)                         // (same graph: the same Convolver nodes in the same order)
        if (s.cur.conv_ir[k] != s.last.conv_ir[k] || s.cur.conv_len[k] != s.last.conv_len[k]) send_conv(s.cur, k);
    s.last = s.cur;
}

void engine_synth_set_clock(int bpm, int running) {
    if (!s_ready) return;
    Synth &s = s_synth;
    const int run = running ? 1 : 0;
    if (bpm == s.clk_bpm && run == s.clk_run) return;                           // called every UI step
    s.clk_bpm = bpm; s.clk_run = run;
    s.eng.set_transport(bpm, running != 0, s.click);
}

namespace {
uint8_t seq_clamp(int v, int lo, int hi) { return static_cast<uint8_t>(v < lo ? lo : (v > hi ? hi : v)); }

void seq_steps_of(const seq_step_t *from, SeqStep *to, int n) {              // the application's steps as the engine's
    for (int i = 0; i < n; i++) {
        const seq_step_t &u = from[i];
        for (int k = 0; k < kSeqChord; k++) to[i].note[k] = u.note[k] > 127 ? 0 : u.note[k];
        to[i].len = seq_clamp(u.len, 1, kSeqSteps);
        to[i].vel = seq_clamp(u.vel, 1, 127);
        to[i].prob = seq_clamp(u.prob, 0, 100);
        to[i].ratchet = seq_clamp(u.ratchet, 1, kSeqRatchet);
    }
}

SeqSetup seq_setup_of(int run, int steps, int rate, int dir, int swing, int transpose) {
    SeqSetup su;
    su.run = run ? 1 : 0;
    su.steps = seq_clamp(steps, 1, kSeqSteps);
    su.rate = seq_clamp(rate, 0, SEQ_RATES - 1);
    su.dir = seq_clamp(dir, 0, SEQ_DIRS - 1);
    su.swing = seq_clamp(swing, 0, 75);
    su.transpose = static_cast<int8_t>(transpose < -48 ? -48 : (transpose > 48 ? 48 : transpose));
    return su;
}
}  // namespace

// The pattern goes over in chunks of kSeqChunk steps, the settings after it (a run then starts on the steps it was given). What the
// command queue did not take is sent again at the next call.
void engine_synth_seq_set(const seq_t *q) {
    if (!s_ready) return;
    Synth &s = s_synth;
    bool all = true;
    for (int first = 0; first < kSeqSteps; first += kSeqChunk) {
        SeqStep st[kSeqChunk];
        seq_steps_of(&q->step[first], st, kSeqChunk);
        if (s.seq_sent && !std::memcmp(st, &s.seq_step[first], sizeof st)) continue;
        if (s.eng.seq_steps(first, kSeqChunk, st)) std::memcpy(&s.seq_step[first], st, sizeof st); else all = false;
    }
    const SeqSetup su = seq_setup_of(q->running, q->steps, q->rate, q->dir, q->swing, q->transpose);
    if (!s.seq_sent || std::memcmp(&su, &s.seq_setup, sizeof su)) {
        if (all && s.eng.seq_setup(su)) s.seq_setup = su; else all = false;
    }
    if (all) s.seq_sent = true;
}

int engine_synth_seq_queue(const seq_pattern_t *p, int id) {
    if (!s_ready) return 0;
    Synth &s = s_synth;
    SeqSetup su;
    if (!p) { su.queue = SEQ_UNQUEUE; return s.eng.seq_setup(su) ? 1 : 0; }
    for (int first = 0; first < kSeqSteps; first += kSeqChunk) {
        SeqStep st[kSeqChunk];
        seq_steps_of(&p->step[first], st, kSeqChunk);
        if (!s.eng.seq_steps(first, kSeqChunk, st, true)) return 0;
    }
    su = seq_setup_of(1, p->steps, p->rate, p->dir, p->swing, p->transpose);
    su.queue = SEQ_QUEUE;
    su.id = static_cast<uint8_t>(id);
    return s.eng.seq_setup(su) ? 1 : 0;
}

// The audio thread went on to the queued pattern: what seq_set believes the engine holds is no longer true, so it sends everything
// again (the application hands it that pattern from now on: the engine ends up with exactly what the application shows).
int engine_synth_seq_taken(void) {
    if (!s_ready) return -1;
    Synth &s = s_synth;
    const uint32_t t = s.eng.seq_taken();
    if (t == s.seq_taken_seen) return -1;
    s.seq_taken_seen = t;
    s.seq_sent = false;
    return static_cast<int>(t & 0xFF);
}

/* ---- the Pads section's sequencer ---- */

namespace {
bool send_pad_pattern(const pseq_pattern_t *p, bool queue) {                 // its hits, kPadSeqChunk per command
    const int n = p->n > kPadSeqHits ? kPadSeqHits : p->n;
    int first = 0;
    do {
        PadHit h[kPadSeqChunk];
        const int count = n - first < kPadSeqChunk ? n - first : kPadSeqChunk;
        for (int i = 0; i < count; i++) {
            const pseq_hit_t &u = p->hit[first + i];
            h[i].pad = u.pad; h[i].step = u.step; h[i].level = u.level; h[i].prob = u.prob; h[i].ratchet = u.ratchet; h[i].len = u.len;
        }
        if (!s_synth.eng.pseq_hits(first, count, h, n, queue)) return false;
        first += count;
    } while (first < n);
    return true;
}
}  // namespace

void engine_synth_pseq_set(const pseq_pattern_t *p, int run) {
    if (!s_ready) return;
    Synth &s = s_synth;
    const uint32_t h = pseq_hash(p);
    run = run ? 1 : 0;
    if (s.pseq_sent && h == s.pseq_hash && run == s.pseq_run) return;         // called every UI step
    if ((!s.pseq_sent || h != s.pseq_hash) && !send_pad_pattern(p, false)) { s.pseq_sent = false; return; }
    if (!s.eng.pseq_setup(seq_setup_of(run, p->steps, p->rate, p->dir, p->swing, 0), RP_PADS)) { s.pseq_sent = false; return; }
    s.pseq_sent = true; s.pseq_hash = h; s.pseq_run = run;
}

int engine_synth_pseq_queue(const pseq_pattern_t *p, int id) {
    if (!s_ready) return 0;
    Synth &s = s_synth;
    SeqSetup su;
    if (!p) { su.queue = SEQ_UNQUEUE; return s.eng.pseq_setup(su, RP_PADS) ? 1 : 0; }
    if (!send_pad_pattern(p, true)) return 0;
    su = seq_setup_of(1, p->steps, p->rate, p->dir, p->swing, 0);
    su.queue = SEQ_QUEUE;
    su.id = static_cast<uint8_t>(id);
    return s.eng.pseq_setup(su, RP_PADS) ? 1 : 0;
}

int engine_synth_pseq_taken(void) {
    if (!s_ready) return -1;
    Synth &s = s_synth;
    const uint32_t t = s.eng.pseq_taken();
    if (t == s.pseq_taken_seen) return -1;
    s.pseq_taken_seen = t;
    s.pseq_sent = false;                                                      // (what the engine holds is the queued pattern now: the next set sends again)
    return static_cast<int>(t & 0xFF);
}

int engine_synth_pseq_pos(void) { return s_ready ? s_synth.eng.pseq_step() : -1; }
int engine_synth_pseq_where(int *step, int *frac, uint32_t *count) { return s_ready && s_synth.eng.pseq_where(step, frac, count) ? 1 : 0; }

int engine_synth_seq_pos(void) { return s_ready ? s_synth.eng.seq_step() : -1; }
int engine_synth_seq_where(int *step, int *frac, uint32_t *count) { return s_ready && s_synth.eng.seq_where(step, frac, count) ? 1 : 0; }

void engine_synth_set_metronome(int on) {
    if (!s_ready || (on != 0) == s_synth.click) return;
    s_synth.click = on != 0;
    s_synth.eng.set_transport(s_synth.clk_bpm, s_synth.clk_run != 0, s_synth.click);
}

int engine_synth_transport(int *bar, int *beat, int *frac) {
    const uint64_t b = s_ready ? s_synth.eng.transport_beat() : 0;
    const uint64_t whole = b >> 32;
    if (bar) *bar = static_cast<int>(whole / 4) + 1;
    if (beat) *beat = static_cast<int>(whole % 4) + 1;
    if (frac) *frac = static_cast<int>((b >> 24) & 0xFF);
    return s_ready && s_synth.eng.transport_running() ? 1 : 0;
}

void engine_synth_note_on(int note)  { if (s_ready) s_synth.eng.note_on(note, 100); }
void engine_synth_note_off(int note) { if (s_ready) s_synth.eng.note_off(note); }

void engine_synth_pads_build(const pads_t *pads, const rack_t *rack) {
    if (!s_ready) return;
    Synth &s = s_synth;
    s_builds++;
    std::snprintf(s_reason, sizeof s_reason, "pads");
    for (int i = 0; i < PAD_COUNT; i++) pad_prepare(pads->pad[i]);
    std::lock_guard<std::mutex> lk(s.mx);
    if (!pads_graph_build(pads->fx, rack->cfg, s.eng.registry(), s.cur)) return;   // the player, its FX chains, the output
    if (s.eng.load(s.cur.g, 1) != Err::Ok) return;
    s.gc_due = true;
    s.pads_mode = true;
    s.have_last = false;                                    // the synth's graph is gone: the next set_params is a build
    s.last_nv = 1;
    for (int k = 0; k < s.cur.conv_count; k++) send_conv(s.cur, k);
    s.last = s.cur;
    s.pads_have_last = true;
    for (int i = 0; i < PAD_COUNT; i++) {
        const PadBlob b = pad_blob(i, pads->pad[i]);
        s.eng.set_blob(RP_PADS, &b, sizeof b);
    }
    pads_master(*rack);
    s.eng.gc();
}

// The FX chains changed (a module added, removed or moved, the routing, a value). The same shape: only the values that moved are sent
// (no stall); another shape: the graph is loaded again (the player and the untouched effects keep their state, engine.h).
void engine_synth_pads_fx(const pads_t *pads, const rack_t *rack) {
    if (!s_ready || !s_synth.pads_mode) return;
    Synth &s = s_synth;
    std::lock_guard<std::mutex> lk(s.mx);
    if (!pads_graph_build(pads->fx, rack->cfg, s.eng.registry(), s.cur)) return;
    if (!s.pads_have_last || !same_structure(s.last.g, s.cur.g)) {
        s_builds++;
        std::snprintf(s_reason, sizeof s_reason, "pads fx n%d/%d e%d/%d", s.last.g.n_nodes, s.cur.g.n_nodes, s.last.g.n_edges, s.cur.g.n_edges);
        if (s.eng.load(s.cur.g, 1) != Err::Ok) return;
        s.gc_due = true;
        for (int k = 0; k < s.cur.conv_count; k++) send_conv(s.cur, k);
        s.last = s.cur;
        s.pads_have_last = true;
        s.eng.gc();
        return;
    }
    for (int i = 0; i < s.cur.g.n_nodes; i++) {
        const NodeDesc &n = s.cur.g.node[i], &o = s.last.g.node[i];
        if (n.id == RP_PADS) continue;                                          // (its parameters are the pads' events)
        for (int p = 0; p < kMaxParams; p++)
            if (n.param[p] != o.param[p]) s.eng.set_param(n.id, p, n.param[p]);
    }
    for (int k = 0; k < s.cur.conv_count; k++)
        if (s.cur.conv_ir[k] != s.last.conv_ir[k] || s.cur.conv_len[k] != s.last.conv_len[k]) send_conv(s.cur, k);
    s.last = s.cur;
}

void engine_synth_pad_set(int pad, const pad_t *p) {
    if (!s_ready || !s_synth.pads_mode || pad < 0 || pad >= PAD_COUNT) return;
    pad_prepare(*p);
    std::lock_guard<std::mutex> lk(s_synth.mx);
    const PadBlob b = pad_blob(pad, *p);
    s_synth.eng.set_blob(RP_PADS, &b, sizeof b);
}

static void pad_event(int idx, int pad) {
    if (!s_ready || !s_synth.pads_mode) return;
    s_synth.pad_events = (s_synth.pad_events + 1) & 0x7FFFFF;
    if (!s_synth.pad_events) s_synth.pad_events = 1;
    s_synth.eng.set_param(RP_PADS, idx, (s_synth.pad_events << 8) | (pad & 0xFF));
}

void engine_synth_pad_push(int pad)    { if (pad >= 0 && pad < PAD_COUNT) pad_event(PADS_PUSH, pad); }
void engine_synth_pad_release(int pad) { if (pad >= 0 && pad < PAD_COUNT) pad_event(PADS_RELEASE, pad); }
void engine_synth_pads_stop(void)      { pad_event(PADS_STOP, 0); }
uint64_t engine_synth_pads_playing(void) { return s_ready && s_synth.pads_mode ? pads_playing() : 0; }
int32_t engine_synth_pad_position(int pad) { return s_ready && s_synth.pads_mode ? pads_position(pad) : -1; }

int64_t engine_synth_sample_zero(int index, uint32_t frame, int dir) {
    if (!s_ready) return -1;
    std::lock_guard<std::mutex> lk(s_synth.mx);
    if (index < 0 || index >= s_synth.cat_n || s_synth.slot_of[index] < 0) return -1;
    return s_synth.bank.zero_near(s_synth.slot_of[index], frame, dir);
}

int engine_synth_sample_onsets(int index, uint32_t *frames, uint16_t *strength, int max) {
    if (!s_ready) return 0;
    Synth &s = s_synth;
    std::lock_guard<std::mutex> lk(s.mx);
    if (!s.bank_on || index < 0 || index >= s.cat_n || s.cat[index].pending) return 0;
    if (s.slot_of[index] < 0) {                                                 // not loaded yet: load it (a head) to scan it
        char path[AUDIO_NAME_MAX + 8];
        std::snprintf(path, sizeof path, "%s.smp", s.cat[index].name);
        s.slot_of[index] = static_cast<int16_t>(s.bank.load(path, 150));
        if (s.slot_of[index] < 0) return 0;
    }
    const int slot = s.slot_of[index];
    Onset o[kMaxOnsets];
    const int n = s.bank.onsets(slot, o, max < kMaxOnsets ? max : kMaxOnsets);
    if (n < 0) { s.bank.scan(slot); return -1; }
    for (int i = 0; i < n; i++) { frames[i] = o[i].frame; strength[i] = o[i].strength; }
    return n;
}

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

int engine_synth_osc_preview(int model, float timbre, float morph, float harm, int period, int16_t *out, int n) {
    if (model < 0 || model >= MI_MODELS || period < 2 || n <= 0) return 0;
    auto q = [](float v) { return static_cast<q15>(std::lround(std::fmax(0.0f, std::fmin(1.0f, v)) * 32767.0f)); };
    const double hz = static_cast<double>(kSampleRate) / period;
    const int32_t pitch = static_cast<int32_t>(std::lround((69.0 + 12.0 * std::log2(hz / 440.0)) * 256.0));
    const bool struck = mi_model_percussive(model);
    mi_render_preview(model, q(timbre), q(morph), q(harm), pitch, 8 * period, struck ? 4 : 1, out, n);   // a decay: 4x longer, peaks kept
    return struck ? 2 : 1;
}

bool engine_synth_sample_info(int index, audio_sample_info_t *out) {
    if (!s_ready) return false;
    std::lock_guard<std::mutex> lk(s_synth.mx);
    if (index < 0 || index >= s_synth.cat_n) return false;
    *out = s_synth.cat[index];
    return true;
}

bool engine_synth_io_pump(void) {
    if (!s_ready) return false;
    Synth &s = s_synth;
    std::lock_guard<std::mutex> lk(s.mx);
    if (!s.bank_on) return false;
    s.bank.pump(s.eng.blocks() * static_cast<uint64_t>(kBlock) * 1000000ull / static_cast<uint64_t>(kSampleRate));
    return !s.bank.idle();
}

void engine_synth_catalog_reset(void) {
    if (!s_ready) return;
    std::lock_guard<std::mutex> lk(s_synth.mx);
    s_synth.cat_n = 0;
    for (auto &v : s_synth.slot_of) v = -1;
    for (auto &v : s_synth.retire) v = false;
    s_synth.retire_n = 0;
}

void engine_synth_sampler_stats(uint32_t *underruns, uint32_t *block_reads) {
    const bool on = s_ready && s_synth.bank_on;
    const SamplerStats &st = s_synth.bank.stats;
    if (underruns) *underruns = on ? st.underruns.load(std::memory_order_relaxed) : 0;
    if (block_reads) *block_reads = on ? st.block_reads.load(std::memory_order_relaxed) : 0;
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

int engine_synth_catalog_find(const char *name) {
    if (!s_ready) return -1;
    std::lock_guard<std::mutex> lk(s_synth.mx);
    return catalog_find(name);
}

int engine_synth_catalog_add(const audio_sample_info_t *info) {
    if (!s_ready || !info || !info->name[0]) return -1;
    Synth &s = s_synth;
    std::lock_guard<std::mutex> lk(s.mx);
    int i = catalog_find(info->name);
    for (int k = 0; k < s.cat_n && i < 0; k++) if (!s.cat[k].name[0] && s.slot_of[k] < 0 && !s.retire[k]) i = k;   // a removed entry, unloaded
    if (i < 0 && s.cat_n < AUDIO_SAMPLES_MAX) i = s.cat_n++;
    if (i < 0) return -1;
    s.cat[i] = *info;
    return i;
}

void engine_synth_catalog_remove(int index) {
    if (!s_ready) return;
    Synth &s = s_synth;
    std::lock_guard<std::mutex> lk(s.mx);
    if (index < 0 || index >= s.cat_n) return;
    s.cat[index].name[0] = 0;
    if (s.slot_of[index] >= 0 && !s.retire[index]) {
        s.retire[index] = true;
        s.retire_blk[index] = static_cast<uint32_t>(s.eng.blocks());
        s.retire_n++;
    }
}
#endif  // PLATFORM_SIM || ARDUINO_ARCH_ESP32
