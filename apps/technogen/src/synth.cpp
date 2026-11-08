// The project's synth: a saw oscillator into a low-pass filter and an amplifier, both driven by one envelope, 4 voices.
//
//   voice:   NoteIn --pitch--> Osc --> Filter --> Vca --> VoiceOut
//                   --gate---> Env --(depth)--> Filter cutoff
//                              Env -----------> Vca level
//   global:  BusIn (the sum of the voices) --> MasterOut
//
// To make it yours: add nodes and cables in build_graph(), register the module families you use in synth_init(),
// and add a setter like synth_set_cutoff() for every value a control should move.
#include "synth.h"
#include <cstring>
#include "engine/core/engine.h"
#include "engine/modules/synth_modules.h"

using namespace sc;

namespace {
constexpr int kVoices = 4;

// Node ids: any numbers, unique and stable. The engine keeps a module's state across a reload while its id and type stay the same.
enum Node : int { N_NOTE = 1, N_OSC, N_FILTER, N_ENV, N_VCA, N_VOICE_OUT, N_BUS, N_MASTER };

Heap fast_heap, bulk_heap;
Engine eng;
q15 block_l[kBlock], block_r[kBlock];
int block_pos = kBlock;                 // frames of the rendered block already handed out
bool ready = false;
int cutoff = 84;

bool build_graph(GraphDesc &g) {
    const Registry &reg = eng.registry();
    NodeDesc *n;
    if (!g.add_node(reg, N_NOTE, T_NOTE_IN)) return false;
    if (!(n = g.add_node(reg, N_OSC, T_OSC))) return false;
    n->param[OSC_WAVE] = WAVE_SAW_;
    if (!(n = g.add_node(reg, N_FILTER, T_FILTER_V))) return false;
    n->param[FLT_MODE] = FLTM_LP;
    n->param[FLT_CUTOFF] = cutoff * 256;            // pitch units: semitones x 256
    n->param[FLT_RES] = 12000;                      // q15: 0..32767
    if (!(n = g.add_node(reg, N_ENV, T_ENV))) return false;
    n->param[ENV_ATTACK] = 4;                       // ms
    n->param[ENV_DECAY] = 350;
    n->param[ENV_SUSTAIN] = 14000;                  // q15
    n->param[ENV_RELEASE] = 250;
    if (!(n = g.add_node(reg, N_VCA, T_VCA_V))) return false;
    n->param[VCA_LEVEL] = 0;                        // closed: the envelope opens it
    if (!(n = g.add_node(reg, N_VOICE_OUT, T_VOICE_OUT))) return false;
    n->param[VO_LEVEL] = 8192;                      // a quarter of full scale per voice: four voices cannot clip
    n->param[VO_TAIL_MS] = 400;
    if (!g.add_node(reg, N_BUS, T_BUS_IN)) return false;
    if (!g.add_node(reg, N_MASTER, T_MASTER_OUT)) return false;

    // connect(from node, from output, to node, input or parameter, its index, depth = 1.0)
    bool ok = true;
    ok &= g.connect(N_NOTE, 0, N_OSC, Dst::In, 0);                          // pitch
    ok &= g.connect(N_NOTE, 1, N_ENV, Dst::In, 0);                          // gate
    ok &= g.connect(N_OSC, 0, N_FILTER, Dst::In, 0);
    ok &= g.connect(N_ENV, 0, N_FILTER, Dst::Param, FLT_CUTOFF, 8192);      // a quarter of the filter's cut_mod range
    ok &= g.connect(N_FILTER, 0, N_VCA, Dst::In, 0);
    ok &= g.connect(N_ENV, 0, N_VCA, Dst::Param, VCA_LEVEL);
    ok &= g.connect(N_VCA, 0, N_VOICE_OUT, Dst::In, 0);
    ok &= g.connect(N_BUS, 0, N_MASTER, Dst::In, 0);                        // left
    ok &= g.connect(N_BUS, 1, N_MASTER, Dst::In, 1);                        // right
    return ok;
}
}  // namespace

bool synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes) {
    fast_heap.init(fast, fast_bytes);
    Memory mem{&fast_heap, &fast_heap};
    if (bulk && bulk != fast) {
        bulk_heap.init(bulk, bulk_bytes);
        fast_heap.set_spill(&bulk_heap);            // a full fast heap takes from the bulk one instead of failing
        mem.bulk = &bulk_heap;
    }
    if (!eng.init(mem, kVoices)) return false;      // registers the structure modules (NoteIn, VoiceOut, BusIn, MasterOut)
    register_synth_modules(eng.registry());         // Osc, Env, Lfo, Filter, Vca, Mix4, Shaper ... (add the families you use)

    static GraphDesc g;                             // large: not on the stack
    g = GraphDesc{};
    if (!build_graph(g)) return false;
    if (eng.load(g, kVoices) != Err::Ok) return false;
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
void synth_note_on(int midi_note) { if (ready) eng.note_on(midi_note); }
void synth_note_off(int midi_note) { if (ready) eng.note_off(midi_note); }

void synth_set_cutoff(int semitones) {
    cutoff = semitones < 24 ? 24 : (semitones > 120 ? 120 : semitones);
    if (ready) eng.set_param(N_FILTER, FLT_CUTOFF, cutoff * 256);           // a live write: no reload, every voice follows
}
int synth_cutoff() { return cutoff; }
int synth_sample_rate() { return kSampleRate; }
