// SynthLab's sound (synth.h).
//
//   voice:   LabVoice (it reads the voice's note, gate and velocity itself) --> VoiceOut
//   global:  BusIn (the sum of the voices) --> LabFx (the synth's effect, if it has one) --> MasterOut
//
// The graph never changes: picking a synth is a parameter of LabVoice (lab_voice.h), so it is live, without a reload.
#include "synth.h"
#include <cstring>
#include "engine/core/engine.h"
#include "engine/modules/builtin.h"
#include "lab.h"
#include "lab_voice.h"

using namespace sc;

namespace {
constexpr int kVoices = 6;

enum Node : int { N_LAB = 1, N_VOICE_OUT, N_BUS, N_FX, N_MASTER };

Heap fast_heap, bulk_heap;
Engine eng;
q15 block_l[kBlock], block_r[kBlock];
int block_pos = kBlock;                 // frames of the rendered block already handed out
bool ready = false;
int selected = 0;
bool looping = false;
int loop_bpm = 120;
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

// The loop's pattern: two steps of an eighth, the note on the first: one hit per beat.
void loop_pattern(int note) {
    SeqStep st[2];
    st[0].note[0] = static_cast<uint8_t>(note < 1 ? 1 : (note > 127 ? 127 : note));    // (0 = no note)
    st[0].len = 1;
    st[0].vel = 100;
    eng.seq_steps(0, 2, st);                        // (st[1]: a rest)
}

void send_mode() {
    const lab::Options &o = lab::kSynths[selected]->opt;
    eng.set_voice_mode(o.mono ? VoiceMode::Mono : VoiceMode::Poly, o.legato, o.glide_ms);
}
}  // namespace

bool synth_init(void *fast, size_t fast_bytes, void *bulk, size_t bulk_bytes) {
    if (lab::kSynthCount < 1 || lab::kSynthCount > lab::kMaxSynths) return false;      // (lab.h: kMaxSynths)
    for (int s = 0; s < lab::kSynthCount; s++)
        for (int i = 0; i < lab::kSynths[s]->n_params; i++) values[s][i] = lab::kSynths[s]->params[i].def;
    fast_heap.init(fast, fast_bytes);
    Memory mem{&fast_heap, &fast_heap};
    if (bulk && bulk != fast) {
        bulk_heap.init(bulk, bulk_bytes);
        fast_heap.set_spill(&bulk_heap);
        mem.bulk = &bulk_heap;
    }
    if (!eng.init(mem, kVoices)) return false;
    lab::register_lab_modules(eng.registry());

    static GraphDesc g;                             // large: not on the stack
    g = GraphDesc{};
    if (!build_graph(g)) return false;
    if (eng.load(g, kVoices) != Err::Ok) return false;
    send_mode();
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
void synth_note_on(int midi_note, int velocity127) { if (ready) eng.note_on(midi_note, velocity127); }
void synth_note_off(int midi_note) { if (ready) eng.note_off(midi_note); }
void synth_all_notes_off() { if (ready) eng.all_notes_off(); }

void synth_select(int index) {
    if (index < 0 || index >= lab::kSynthCount) return;
    selected = index;
    if (!ready) return;
    if (looping) synth_loop_stop();
    eng.all_notes_off();
    eng.set_param(N_LAB, lab::LV_SYNTH, index);     // first: the voices build the new synth's Voice (and the effect its Fx), then take its values
    eng.set_param(N_FX, lab::LV_SYNTH, index);
    send_values(0, lab::kSynths[index]->n_params);
    send_mode();
}
int synth_selected() { return selected; }

void synth_set_value(int param, float value) {
    const lab::Synth &s = *lab::kSynths[selected];
    if (param < 0 || param >= s.n_params) return;
    values[selected][param] = clamp_value(s.params[param], value);
    if (ready) send_values(param, 1);
}
float synth_value(int param) { return param >= 0 && param < lab::kMaxParams ? values[selected][param] : 0.f; }

void synth_set_volume(float volume) {
    const float v = volume < 0.f ? 0.f : (volume > 1.f ? 1.f : volume);
    if (ready) eng.set_param(N_MASTER, 0, static_cast<int32_t>(v * 32767.f));
}
void synth_loop_start(int midi_note, int bpm) {
    if (!ready) return;
    loop_bpm = bpm;
    loop_pattern(midi_note);
    SeqSetup s;
    s.run = 1;
    s.steps = 2;
    s.rate = SEQ_RATE_8;
    eng.seq_setup(s);                               // the setup first, then the clock: the run starts with it, on beat 0
    eng.set_transport(bpm, true, false);
    looping = true;
}
void synth_loop_note(int midi_note) { if (ready && looping) loop_pattern(midi_note); }
void synth_loop_stop() {
    if (!ready) return;
    SeqSetup s;
    s.run = 0;
    eng.seq_setup(s);
    eng.set_transport(loop_bpm, false, false);
    looping = false;
}
bool synth_looping() { return looping; }

int synth_sample_rate() { return kSampleRate; }
int synth_active_voices() { return ready ? eng.active_voices() : 0; }
