// SynthLab's sound (synth.h).
//
//   voice:   LabVoice (it reads the voice's note, gate and velocity itself) --> VoiceOut
//   global:  BusIn (the sum of the voices) --> LabFx (the synth's effect, if it has one) --> MasterOut
//
// The graph does not change between the synths written as a Voice: picking one is a parameter of LabVoice (lab_voice.h), live. An
// engine synth (lab.h GraphSynth: SynthLab Studio's Export > Engine synth) has its own graph: picking it loads that one (up to 200 ms of
// silence, the user's OK), picking a Voice synth after it loads this one again. Its values go to its graph's nodes (GraphValue).
#include "synth.h"
#include <cmath>
#include "engine/core/host.h"
#include "engine/modules/builtin.h"
#include "lab.h"
#include "lab_voice.h"

using namespace sc;

namespace {
constexpr int kVoices = 6;

enum Node : int { N_LAB = 1, N_VOICE_OUT, N_BUS, N_FX, N_MASTER };

EngineHost host;                        // the heaps, the engine, the hand-out of its blocks (engine/core/host.h)
Engine &eng = host.engine;
int selected = 0;
bool looping = false;
int loop_bpm = 120;
int loop_note = 60;
float values[lab::kMaxSynths][lab::kMaxParams];   // each synth's values

float clamp_value(const lab::Param &p, float v) { return v < p.min ? p.min : (v > p.max ? p.max : v); }

bool build_graph(GraphDesc &g) {
    const Registry &reg = eng.registry();
    NodeDesc *n;
    if (!(n = g.add_node(reg, N_LAB, lab::kTypeLabVoice))) return false;
    n->param[lab::LV_SYNTH] = selected;             // (its voices start with that synth's defaults: synth_init's values)
    if (!(n = g.add_node(reg, N_VOICE_OUT, T_VOICE_OUT))) return false;
    n->param[VO_LEVEL] = 16384;                     // half scale per voice: a voice at +-0.5 is a quarter, six of them rarely clip
    n->param[VO_TAIL_MS] = 30000;                   // a voice ends when it is silent (LabVoice: when render() says so), at the latest here
    if (!g.add_node(reg, N_BUS, T_BUS_IN)) return false;
    if (!(n = g.add_node(reg, N_FX, lab::kTypeLabFx))) return false;
    n->param[lab::LV_SYNTH] = selected;
    if (!(n = g.add_node(reg, N_MASTER, T_MASTER_OUT))) return false;
    n->param[0] = 26000;                            // its level (q15): about -2 dB; the volume knob moves it

    bool ok = true;
    ok &= g.connect(N_LAB, 0, N_VOICE_OUT, Dst::In, 0);
    ok &= g.connect(N_BUS, 0, N_FX, Dst::In, 0);
    ok &= g.connect(N_BUS, 1, N_FX, Dst::In, 1);
    ok &= g.connect(N_FX, 0, N_MASTER, Dst::In, 0);
    ok &= g.connect(N_FX, 1, N_MASTER, Dst::In, 1);
    return ok;
}

void send_values(int first, int count) {           // (in blobs of lab::kBlobValues: one engine command each)
    while (count > 0) {
        lab::LabValues b;
        const int n = count < lab::kBlobValues ? count : lab::kBlobValues;
        b.first = static_cast<uint8_t>(first);
        b.count = static_cast<uint8_t>(n);
        for (int i = 0; i < n; i++) b.v[i] = values[selected][first + i];
        eng.set_blob(N_LAB, &b, b.bytes());         // the voices
        eng.set_blob(N_FX, &b, b.bytes());          // and the effect
        first += n;
        count -= n;
    }
}

uint8_t midi(int note) { return static_cast<uint8_t>(note < 1 ? 1 : (note > 127 ? 127 : note)); }    // (0 = no note)

// The selected synth's groove (lab::Options::grooves), or nullptr: the loop is then one hit a beat
const lab::Groove *groove() {
    const lab::Options &o = lab::kSynths[selected]->opt;
    if (!o.grooves || o.n_grooves < 1 || o.groove_param < 0) return nullptr;
    int k = static_cast<int>(values[selected][o.groove_param] + 0.5f);
    k = k < 0 ? 0 : (k >= o.n_grooves ? o.n_grooves - 1 : k);
    return &o.grooves[k];
}
int loop_tempo() {
    const lab::Options &o = lab::kSynths[selected]->opt;
    const int bpm = o.tempo_param >= 0 ? static_cast<int>(values[selected][o.tempo_param] + 0.5f) : o.play_bpm;
    return bpm < 20 ? 20 : (bpm > 300 ? 300 : bpm);
}
// The sequencer's setup for the loop: a groove's length, rate and swing; else two eighths
SeqSetup loop_setup(bool run) {
    SeqSetup s;
    s.run = run ? 1 : 0;
    s.steps = 2;
    s.rate = SEQ_RATE_8;
    if (const lab::Groove *g = groove()) {
        const lab::Options &o = lab::kSynths[selected]->opt;
        s.steps = static_cast<uint8_t>(g->steps < 1 ? 1 : (g->steps > kSeqSteps ? kSeqSteps : g->steps));
        s.rate = g->rate == 3 ? SEQ_RATE_8T : g->rate == 4 ? SEQ_RATE_16 : g->rate == 6 ? SEQ_RATE_16T : g->rate == 8 ? SEQ_RATE_32 : SEQ_RATE_8;
        const int sw = o.swing_param >= 0 ? static_cast<int>(values[selected][o.swing_param] + 0.5f) : 0;
        s.swing = static_cast<uint8_t>(sw < 0 ? 0 : (sw > 75 ? 75 : sw));
    }
    return s;
}

// The loop's pattern: the groove's steps on the note; without one, two eighths, the note on the first: one hit per beat.
// queue: for the end of the playing pattern (a groove changed while it plays)
void loop_pattern(int note, bool queue = false) {
    static SeqStep st[kSeqSteps];                   // (512 bytes: not on the stack)
    for (SeqStep &x : st) x = SeqStep{};
    int n = 2;
    if (const lab::Groove *g = groove()) {
        n = g->steps < 1 ? 1 : (g->steps > kSeqSteps ? kSeqSteps : g->steps);
        for (int k = 0; k < n; k++) {
            if (g->vel[k] == 0) continue;           // (a rest)
            st[k].note[0] = midi(note + (g->semi ? g->semi[k] : 0));
            st[k].vel = g->vel[k] > 127 ? 127 : g->vel[k];
        }
    } else {
        st[0].note[0] = midi(note);
        st[0].vel = 100;                            // (st[1]: a rest)
    }
    eng.seq_steps(0, n, st, queue);
}

void send_mode() {
    const lab::Options &o = lab::kSynths[selected]->opt;
    eng.set_voice_mode(o.mono ? VoiceMode::Mono : VoiceMode::Poly, o.legato, o.glide_ms, o.voices);   // (its voice count: under kVoices)
}

// Engine synths (lab.h GraphSynth): their own graph. `loaded` is the synth whose graph is loaded, -1 the LabVoice graph (every Voice
// synth plays in it: picking one of them is a parameter, live), -2 none yet.
int loaded = -2;
float volume = -1.f;                                // the volume knob's (-1: not moved: the graph's own master level)
bool is_graph(int i) { return lab::kSynths[i]->graph != nullptr; }
int master_node() { return loaded >= 0 ? lab::kGraphMaster : N_MASTER; }

bool load_for(int index) {                          // [CONTROL] the graph that plays `index` (a rebuild: up to 200 ms of silence)
    const int want = is_graph(index) ? index : -1;
    if (want == loaded) return true;
    GraphDesc &g = host.new_graph();
    const bool ok = want >= 0 ? lab::kSynths[index]->graph->build(g, eng.registry()) : build_graph(g);
    if (!ok || !host.load(g, kVoices)) return false;
    loaded = want;
    if (volume >= 0.f) eng.set_param(master_node(), 0, static_cast<int32_t>(volume * 32767.f));
    return true;
}

// An engine synth's values: to their nodes. all: only the ones moved from the patch's (its graph is built with the patch's values: few
// commands, the ring holds 64)
void send_graph_values(int first, int count, bool all) {
    const lab::Synth &s = *lab::kSynths[selected];
    for (int i = first; i < first + count && i < s.n_params; i++) {
        const lab::GraphValue &w = s.graph->where[i];
        if (w.kind == lab::GV_NONE || !w.node) continue;
        if (!all && values[selected][i] == s.params[i].def) continue;
        if (w.kind == lab::GV_MASTER && volume >= 0.f) continue;     // (the volume knob has the master)
        eng.set_param(w.node, w.param, lab::graph_raw(w, values[selected][i]));
    }
}
}  // namespace

int32_t lab::graph_raw(const GraphValue &w, float v) {
    switch (w.kind) {
    case GV_X100: return static_cast<int32_t>(std::lround(v * 100.f));
    case GV_X1: return static_cast<int32_t>(std::lround(v));
    case GV_VOICE: return static_cast<int32_t>(std::lround(v * 0.01f * static_cast<float>(kVoiceLevel)));
    case GV_MASTER: return static_cast<int32_t>(std::lround(v * 0.01f * static_cast<float>(kMasterLevel)));
    default: return 0;
    }
}

bool synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes) {
    if (lab::kSynthCount < 1 || lab::kSynthCount > lab::kMaxSynths) return false;      // (lab.h: kMaxSynths)
    for (int s = 0; s < lab::kSynthCount; s++)
        for (int i = 0; i < lab::kSynths[s]->n_params; i++) values[s][i] = lab::kSynths[s]->params[i].def;
    if (!host.init(fast, fast_bytes, bulk, bulk_bytes, kVoices)) return false;
    lab::register_lab_modules(eng.registry());
    for (int s = 0; s < lab::kSynthCount; s++)       // the engine synths' module types: only those (each one's code in internal RAM)
        if (is_graph(s) && !lab::kSynths[s]->graph->types(eng.registry())) return false;
    loaded = -2;                                    // (a new engine: nothing loaded yet, whatever an earlier start had)
    volume = -1.f;
    if (!load_for(selected)) return false;
    send_mode();
    return true;
}

void synth_shutdown() { host.shutdown(); }
#if defined(SYNTHLAB_BENCH) && defined(ARDUINO_ARCH_ESP32)
#include <esp_cpu.h>
volatile uint32_t g_bench_render_cycles = 0, g_bench_render_frames = 0;     // (src/bench_esp32.cpp reads them)
void bench_heap(size_t *used, size_t *cap, size_t *spilled) {
    *used = host.fast_heap().used();
    *cap = host.fast_heap().capacity();
    *spilled = host.fast_heap().spilled();
}
#ifdef ENGINE_PROFILE
volatile int g_bench_prof_req = 0;                  // set by the bench: the audio thread takes the engine's profile (and clears it)
Engine::ProfEntry g_bench_prof[Engine::kProfMax];
volatile int g_bench_prof_n = 0;
uint32_t g_bench_prof_blocks = 0;
#endif
void synth_render(int16_t *stereo, int frames) {
    const uint32_t t0 = esp_cpu_get_cycle_count();
    host.render(stereo, frames);
    g_bench_render_cycles += esp_cpu_get_cycle_count() - t0;
    g_bench_render_frames += static_cast<uint32_t>(frames);
#ifdef ENGINE_PROFILE
    if (g_bench_prof_req) {
        g_bench_prof_n = eng.prof_take(g_bench_prof, Engine::kProfMax, &g_bench_prof_blocks);
        g_bench_prof_req = 0;
    }
#endif
}
#else
void synth_render(int16_t *stereo, int frames) { host.render(stereo, frames); }
#endif
void synth_update() { host.update(); }
void synth_note_on(int midi_note, int velocity127) { if (host.ready()) eng.note_on(midi_note, velocity127); }
void synth_note_off(int midi_note) { if (host.ready()) eng.note_off(midi_note); }
void synth_all_notes_off() { if (host.ready()) eng.all_notes_off(); }

void synth_select(int index) {
    if (index < 0 || index >= lab::kSynthCount) return;
    selected = index;
    if (!host.ready()) return;
    if (looping) synth_loop_stop();
    eng.all_notes_off();
    if (!load_for(index)) return;                   // (an engine synth, or back from one: its graph)
    if (is_graph(index)) {
        send_graph_values(0, lab::kSynths[index]->n_params, false);
    } else {
        eng.set_param(N_LAB, lab::LV_SYNTH, index); // first: the voices build the new synth's Voice (and the effect its Fx), then take its values
        eng.set_param(N_FX, lab::LV_SYNTH, index);
        send_values(0, lab::kSynths[index]->n_params);
    }
    send_mode();
}
int synth_selected() { return selected; }
void synth_mode_changed() { if (host.ready()) send_mode(); }

void synth_set_value(int param, float value) {
    const lab::Synth &s = *lab::kSynths[selected];
    if (param < 0 || param >= s.n_params) return;
    values[selected][param] = clamp_value(s.params[param], value);
    if (!host.ready()) return;
    if (is_graph(selected)) send_graph_values(param, 1, true);
    else send_values(param, 1);
    if (looping && groove()) {                      // the loop's values, live
        const lab::Options &o = s.opt;
        if (param == o.groove_param) {              // the new groove from the end of the playing one
            loop_pattern(loop_note, true);
            SeqSetup q = loop_setup(true);
            q.queue = SEQ_QUEUE;
            eng.seq_setup(q);
        } else if (param == o.tempo_param) {
            loop_bpm = loop_tempo();
            eng.set_transport(loop_bpm, true, false);
        } else if (param == o.swing_param) {
            eng.seq_setup(loop_setup(true));
        }
    }
}
float synth_value(int param) { return param >= 0 && param < lab::kMaxParams ? values[selected][param] : 0.f; }

void synth_set_volume(float level) {
    const float v = level < 0.f ? 0.f : (level > 1.f ? 1.f : level);
    volume = v;                                     // (kept: an engine synth's graph gets it when it loads)
    if (host.ready()) eng.set_param(master_node(), 0, static_cast<int32_t>(v * 32767.f));
}
void synth_loop_start(int midi_note, int bpm) {
    if (!host.ready()) return;
    const lab::Options &o = lab::kSynths[selected]->opt;
    loop_bpm = groove() && o.tempo_param >= 0 ? loop_tempo() : bpm;
    loop_note = midi_note;
    loop_pattern(midi_note);
    eng.seq_setup(loop_setup(true));                // the setup first, then the clock: the run starts with it, on beat 0
    eng.set_transport(loop_bpm, true, false);
    looping = true;
}
void synth_loop_note(int midi_note) {
    loop_note = midi_note;
    if (host.ready() && looping) loop_pattern(midi_note);
}
void synth_loop_stop() {
    if (!host.ready()) return;
    SeqSetup s;
    s.run = 0;
    eng.seq_setup(s);
    SeqSetup u;                                     // (a groove queued and not yet taken: dropped)
    u.queue = SEQ_UNQUEUE;
    eng.seq_setup(u);
    eng.set_transport(loop_bpm, false, false);
    looping = false;
}
bool synth_looping() { return looping; }
int synth_loop_bpm() { return loop_bpm; }

int synth_sample_rate() { return kSampleRate; }
int synth_active_voices() { return host.ready() ? eng.active_voices() : 0; }
