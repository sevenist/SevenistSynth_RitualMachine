// Exactness of the optimizations (the module bench's checksums on the host): a module driven alone through many of its settings and
// kinds of modulation, a checksum of everything it wrote. An optimization that must not change the sound keeps the pinned checksum
// (bit for bit the same output); one that changes it fails here and must be judged by ear and by a null test instead. The checksums are
// pinned for the default configuration (48 kHz, blocks of 32); the other configurations of the matrix only print theirs.
#include <cstdio>
#include <cstring>
#include <vector>
#include "rig.h"

namespace {
using namespace tst;

uint32_t fnv(uint32_t h, const void *data, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

bool pinned_config() { return kSampleRate == 48000 && kBlock == 32; }

// What drives a module: its inputs a saw plus noise at about -6 dBFS (a different phase per input); the modulation of `mod_param`
// (-1: none) one of these kinds, fed per sample or marked slow (Ports::mod_slow) as the plan marks an envelope's.
enum ModKind { kNone, kRamp, kLfo, kAudio, kSteps };

struct Drive {
    int blocks = 48;
    int mod_param = -1;
    ModKind kind = kNone;
    bool slow = false;
    int knob_param = -1;            // at block knob_block this value jumps to knob_value (a knob's move: the module's own glide)
    int knob_block = 12;
    int32_t knob_value = 0;
    int gate_len = 0;               // > 0: input 0 is a gate, on for gate_len samples then off as long (its edges fall inside blocks)
    int in0 = 0;                    // input 0: 0 the saw and noise, 1 held (a note's pitch), 2 a slow glide marked slow (Ports::in_slow)
};

uint32_t module_sum(uint32_t h, int type, const std::vector<std::pair<int, int32_t>> &params, const Drive &d) {
    DspRig rig;
    const ModuleType *t = rig.eng.registry().get(type);
    CHECK(t != nullptr);
    if (!t) return h;
    Memory mem{&rig.heap, &rig.heap, nullptr};
    Module *m = t->create(mem);
    CHECK(m != nullptr);
    if (!m) return h;
    const ModuleInfo &info = m->info();
    for (int j = 0; j < info.n_param; j++) m->set_param(j, info.param[j].def);
    for (const auto &p : params) m->set_param(p.first, p.second);
    m->reset();
    q15 in[kMaxIn][kBlock], md[kBlock], out[kMaxOut][kBlock], zero[kBlock] = {};
    VoiceState vs;
    vs.note = 60; vs.pitch = vs.target_pitch = 60 * 256; vs.gate = vs.active = vs.started = true;
    // (the buses and the final output: VoiceOut, BusIn and MasterOut write there; a module with no output is checked by them)
    int32_t bus[4][kBlock];
    q15 master[2][kBlock];
    ProcessCtx ctx{kBlock, 0, info.scope == Scope::Voice ? &vs : nullptr, bus[0], bus[1], master[0], master[1]};
    ctx.bus2_l = bus[2]; ctx.bus2_r = bus[3];
    ctx.silence = zero;
    uint32_t noise = 12345u;
    int64_t n = 0;
    for (int b = 0; b < d.blocks; b++) {
        if (b == d.knob_block && d.knob_param >= 0) m->set_param(d.knob_param, d.knob_value);
        for (int i = 0; i < kBlock; i++, n++) {
            for (int k = 0; k < kMaxIn; k++) {
                noise = noise * 1664525u + 1013904223u;
                const int saw = static_cast<int>(((n * (37 + 11 * k)) % 400) * 82 - 16400);
                in[k][i] = static_cast<q15>(saw / 2 + (static_cast<int32_t>(noise >> 16) - 32768) / 4);
                if (k == 0 && d.gate_len > 0) in[k][i] = (n / d.gate_len) % 2 == 0 ? kUnity : 0;
                if (k == 0 && d.in0 == 1) in[k][i] = 2400;
                if (k == 0 && d.in0 == 2) in[k][i] = static_cast<q15>((n % 9600) - 4800);
            }
            const double ph = static_cast<double>(n);
            switch (d.kind) {
                case kRamp: md[i] = static_cast<q15>((n % 4800) * 6 - 14400); break;                         // a slow saw (10 Hz)
                case kLfo: md[i] = static_cast<q15>(std::lround(12000.0 * std::sin(ph * 6.2831853 * 3.0 / 48000.0))); break;
                case kAudio: md[i] = static_cast<q15>(std::lround(9000.0 * std::sin(ph * 6.2831853 * 440.0 / 48000.0))); break;
                case kSteps: md[i] = static_cast<q15>(((n / 300) % 7) * 3000 - 9000); break;              // sample & hold
                default: md[i] = 0; break;
            }
        }
        Ports p{};
        for (int k = 0; k < kMaxIn; k++) p.in[k] = in[k];
        for (int j = 0; j < kMaxParams; j++) p.mod[j] = j == d.mod_param && d.kind != kNone ? md : nullptr;
        p.mod_slow = d.slow ? 0xFFFF : 0;
        p.in_slow = d.in0 == 2 ? 1u : 0u;
        for (int o = 0; o < kMaxOut; o++) { p.out[o] = out[o]; std::memset(out[o], 0, sizeof out[o]); }
        for (int k = 0; k < 4; k++) for (int i = 0; i < kBlock; i++) bus[k][i] = in[k][i] * (k + 1);   // (what voices left there)
        std::memset(master, 0, sizeof master);
        m->process(ctx, p);
        ctx.time++;
        vs.started = false;
        for (int o = 0; o < info.n_out; o++) h = fnv(h, out[o], sizeof out[o]);
        if (info.n_out == 0) { h = fnv(h, master, sizeof master); h = fnv(h, bus, sizeof bus); }
    }
    destroy_module(rig.heap, m);
    return h;
}

// The ways a value is moved: still, a knob's glide, modulated slowly (per sample, and marked slow), an LFO, audio rate, sample & hold
std::vector<Drive> drives(int param, int32_t knob_to) {
    std::vector<Drive> v;
    Drive d;
    v.push_back(d);
    d.knob_param = param; d.knob_value = knob_to; v.push_back(d);
    d = Drive{}; d.mod_param = param;
    for (ModKind k : {kRamp, kLfo, kAudio, kSteps}) { d.kind = k; d.slow = false; v.push_back(d); }
    for (ModKind k : {kRamp, kLfo}) { d.kind = k; d.slow = true; v.push_back(d); }
    return v;
}

}  // namespace

// The Filter (synth_modules.cpp): every mode, 1 to 4 sections, three resonances, the four algorithms, every way the cutoff moves.
TEST(filter_output_is_bit_for_bit_the_pinned_one) {
    uint32_t h = 2166136261u;
    for (int algo = FLTA_SVF; algo <= FLTA_CHAM; algo++)
        for (int mode = FLTM_LP; mode <= FLTM_AP; mode++)
            for (int sec = 1; sec <= 4; sec++) {
                if (algo != FLTA_SVF && (mode != FLTM_LP || sec != 1)) continue;      // (the light ones have no mode or sections)
                for (int32_t res : {0, 16000, 32767})
                    for (const Drive &d : drives(FLT_CUTOFF, 70 * 256)) {
                        for (int type : {T_FILTER_V, T_FILTER_G})
                            h = module_sum(h, type, {{FLT_ALGO, algo}, {FLT_MODE, mode}, {FLT_SECTIONS, sec}, {FLT_RES, res}, {FLT_CUTOFF, 90 * 256}}, d);
                    }
            }
    std::printf("    Filter checksum %08x\n", static_cast<unsigned>(h));
    // (re-pinned 2026-11-20: the per-sample sets four per divide, equal to the former 0xefd45209 to float rounding, the output -118 dB from
    // it: tests/test_filter_fmq.cpp)
    if (pinned_config()) CHECK_EQ(h, 0x6125d371u);
}

// Osc: every wave and quality, the pitch held, gliding slowly (marked slow) or at audio rate, the pitch, PW and level modulated.
TEST(osc_output_is_bit_for_bit_the_pinned_one) {
    uint32_t h = 2166136261u;
    for (int wave = 0; wave <= 5; wave++)
        for (int qual = 0; qual <= 2; qual++)
            for (int in0 : {0, 1, 2})
                for (int param : {OSC_PITCH, OSC_PW, OSC_LEVEL})
                    for (Drive d : drives(param, param == OSC_PITCH ? 72 * 256 : 9000)) {
                        d.in0 = in0;
                        for (int type : {static_cast<int>(T_OSC), static_cast<int>(T_OSC_G)})
                            h = module_sum(h, type, {{OSC_WAVE, wave}, {OSC_QUAL, qual}, {OSC_PW, 12000}, {OSC_LEVEL, 30000}}, d);
                    }
    std::printf("    Osc checksum %08x\n", static_cast<unsigned>(h));
    if (pinned_config()) CHECK_EQ(h, 0x120facfdu);               // (the loop of 2026-11-19 with its wave switch, before the per-wave loops)
}

// Vca: levels 0 (the rack's amp VCAs), positive, negative and full, the level still, a knob's move and every way it is modulated.
TEST(vca_output_is_bit_for_bit_the_pinned_one) {
    uint32_t h = 2166136261u;
    for (int32_t lvl : {0, 20000, -10000, 32767, -32768})
        for (const Drive &d : drives(VCA_LEVEL, 9000))
            for (int type : {static_cast<int>(T_VCA_V), static_cast<int>(T_VCA_G)}) h = module_sum(h, type, {{VCA_LEVEL, lvl}}, d);
    std::printf("    Vca checksum %08x\n", static_cast<unsigned>(h));
    if (pinned_config()) CHECK_EQ(h, 0x094657c1u);               // (the loop of 2026-11-20, before the level-0 path)
}

// Mix4: 0 to 4 inputs in use (a level of 0 leaves an input out), positive and negative levels, levels modulated (per sample).
TEST(mix4_output_is_bit_for_bit_the_pinned_one) {
    uint32_t h = 2166136261u;
    const int32_t levels[][4] = {{0, 0, 0, 0}, {8192, 0, 0, 0}, {0, 0, 32767, 0}, {8192, 8192, 0, 0}, {32767, 0, -32768, 0},
                                 {8192, 8192, 8192, 0}, {0, 20000, -9000, 32767}, {8192, 8192, 8192, 8192}, {32767, 32767, 32767, 32767}};
    for (const auto &lv : levels)
        for (int mp = -1; mp < 4; mp++) {
            Drive d;
            if (mp >= 0) { d.mod_param = mp; d.kind = kLfo; }
            for (int type : {T_MIX4_V, T_MIX4_G})
                h = module_sum(h, type, {{0, lv[0]}, {1, lv[1]}, {2, lv[2]}, {3, lv[3]}}, d);
        }
    std::printf("    Mix4 checksum %08x\n", static_cast<unsigned>(h));
    if (pinned_config()) CHECK_EQ(h, 0x97b5844du);              // (the loops of 2026-11-18, before their rewrite)
}

// MasterOut: the volume still and moving (its one-block ramp), the boost, mono, a recording tap idle (the capture is not armed).
TEST(master_out_output_is_bit_for_bit_the_pinned_one) {
    uint32_t h = 2166136261u;
    for (int32_t level : {0, 9000, 32767})
        for (int boost = 0; boost < 2; boost++)
            for (int mono = 0; mono < 2; mono++)
                for (int32_t knob : {-1, 20000, 32767}) {
                    Drive d;
                    if (knob >= 0) { d.knob_param = 0; d.knob_value = knob; }
                    h = module_sum(h, T_MASTER_OUT, {{0, level}, {1, boost}, {2, mono}}, d);
                }
    std::printf("    MasterOut checksum %08x\n", static_cast<unsigned>(h));
    if (pinned_config()) CHECK_EQ(h, 0x6960ef16u);              // (its loop of 2026-11-18, before its rewrite)
}

// Env (the ADSR): gates of different lengths (edges inside blocks, releases cut by a new attack), short and long stages, a hold, a start
// level, the curves; and the noisy input of the other tests (a gate that flickers).
TEST(env_output_is_bit_for_bit_the_pinned_one) {
    uint32_t h = 2166136261u;
    const int32_t sets[][9] = {{2, 30, 20000, 40, 0, 0, 18000, 20000, 20000}, {1, 1, 32767, 1, 0, 0, 0, 0, 0},
                               {5, 200, 0, 300, 10, 8000, -20000, 30000, -30000}, {20, 10, 12000, 15, 0, 20000, 18000, 20000, 20000}};
    for (const auto &v : sets)
        for (int gl : {0, 700, 2000, 5000}) {
            Drive d;
            d.blocks = 600;
            d.gate_len = gl;
            if (gl == 5000) { d.knob_param = ENV_SUSTAIN; d.knob_block = 100; d.knob_value = 6000; }   // (a live edit of the sustain, held)
            for (int type : {T_ENV, T_ENV_G}) {
                std::vector<std::pair<int, int32_t>> ps;
                for (int j = 0; j < 9; j++) ps.push_back({j, v[j]});
                h = module_sum(h, type, ps, d);
            }
        }
    std::printf("    Env checksum %08x\n", static_cast<unsigned>(h));
    if (pinned_config()) CHECK_EQ(h, 0x57972bc1u);              // (its per-sample loop of 2026-11-18, before the steady path)
}

// Phaser (global stereo and per-voice mono): every stage count, feedback both ways, mixes, its rate and centre moving.
TEST(phaser_output_is_bit_for_bit_the_pinned_one) {
    uint32_t h = 2166136261u;
    for (int stages = 1; stages <= 4; stages++)
        for (int32_t fb : {-31000, 0, 16000, 31000})
            for (int32_t mix : {0, 16384, 32767}) {
                Drive d;
                d.blocks = 200;
                d.knob_param = PHS_CENTER; d.knob_value = 100 * 256; d.knob_block = 90;
                for (int type : {T_PHASER, T_PHASER_V})
                    h = module_sum(h, type, {{PHS_STAGES, stages}, {PHS_FEEDBACK, fb}, {PHS_MIX, mix}, {PHS_RATE, 3 * 12 * 256}, {PHS_DEPTH, 32767}}, d);
            }
    std::printf("    Phaser checksum %08x\n", static_cast<unsigned>(h));
    if (pinned_config()) CHECK_EQ(h, 0xef2db7d2u);              // (its int64 coefficient of 2026-11-19, before int32)
}

// Reverb (the plate): sizes (a size change glides its taps), decays, damping, modulation depths, the pre-delay, mixes.
TEST(reverb_output_is_bit_for_bit_the_pinned_one) {
    uint32_t h = 2166136261u;
    for (int32_t size : {0, 21845, 32767})
        for (int32_t mod : {0, 12000, 32767})
            for (int32_t decay : {8000, 32400}) {
                Drive d;
                d.blocks = 300;
                d.knob_param = RVB_SIZE; d.knob_value = size > 16000 ? 4000 : 30000; d.knob_block = 100;
                h = module_sum(h, T_REVERB, {{RVB_SIZE, size}, {RVB_MOD, mod}, {RVB_DECAY, decay}, {RVB_PREDELAY, size == 0 ? 0 : 35},
                                              {RVB_DAMP, 90 * 256}, {RVB_MIX, 20000}}, d);
            }
    std::printf("    Reverb checksum %08x\n", static_cast<unsigned>(h));
    if (pinned_config() && !ENGINE_REVERB_HALF) CHECK_EQ(h, 0xffd826bcu);              // (as committed on 2026-11-19)
}

// The engine's Mix steps (engine.cpp): a graph whose plan has one scaled source, two, three with one muted (gain 0), unity ones, and
// modulation cables at depths, rendered with 3 voices; the checksum of the output.
TEST(mix_steps_render_bit_for_bit_the_pinned_one) {
    DspRig rig(4);
    GraphDesc g;
    rig.add(g, 1, T_NOTE_IN);
    rig.add(g, 2, T_OSC)->param[OSC_WAVE] = WAVE_SAW_;
    rig.add(g, 3, T_OSC)->param[OSC_WAVE] = WAVE_SINE_;
    rig.add(g, 4, T_OSC)->param[OSC_WAVE] = WAVE_PULSE_;
    NodeDesc *env = rig.add(g, 5, T_ENV);
    env->param[ENV_ATTACK] = 3; env->param[ENV_DECAY] = 120; env->param[ENV_SUSTAIN] = 12000; env->param[ENV_RELEASE] = 80;
    rig.add(g, 6, T_FILTER_V)->param[FLT_CUTOFF] = 70 * 256;
    rig.add(g, 7, T_VCA_V)->param[VCA_LEVEL] = 0;
    rig.add(g, 8, T_VOICE_OUT);
    rig.add(g, 9, T_BUS_IN);
    rig.add(g, 10, T_MASTER_OUT);
    NodeDesc *lfo = rig.add(g, 11, T_LFO_V);
    lfo->param[LFO_RATE] = static_cast<int32_t>(std::lround(12 * 256 * std::log2(6.0)));
    for (int o : {2, 3, 4}) g.connect(1, 0, o, Dst::In, 0);                 // pitch into the three oscillators (unity)
    g.connect(1, 1, 5, Dst::In, 0);                                         // gate
    g.connect(2, 0, 6, Dst::In, 0, 20000);                                  // three into the filter: two scaled, one muted
    g.connect(3, 0, 6, Dst::In, 0, -9000);
    g.connect(4, 0, 6, Dst::In, 0, 0);
    g.connect(5, 0, 6, Dst::Param, FLT_CUTOFF, 26000);                      // one scaled modulation
    g.connect(11, 0, 6, Dst::Param, FLT_CUTOFF, 3000);                      // ... and a second onto the same value
    g.connect(11, 0, 2, Dst::Param, OSC_PW, 12000);                         // a single scaled one
    g.connect(6, 0, 7, Dst::In, 0);
    g.connect(5, 0, 7, Dst::Param, VCA_LEVEL);
    g.connect(7, 0, 8, Dst::In, 0);
    g.connect(9, 0, 10, Dst::In, 0);
    g.connect(9, 1, 10, Dst::In, 1);
    CHECK(rig.eng.load(g) == Err::Ok);
    uint32_t h = 2166136261u;
    for (int n : {48, 55, 62}) rig.eng.note_on(n);
    q15 l[kBlock], r[kBlock];
    for (int b = 0; b < 600; b++) {
        if (b == 300) rig.eng.note_off(55);
        rig.eng.render(l, r);
        h = fnv(h, l, sizeof l); h = fnv(h, r, sizeof r);
    }
    std::printf("    Mix steps checksum %08x\n", static_cast<unsigned>(h));
    if (pinned_config()) CHECK_EQ(h, 0x507be601u);              // (the Mix step of 2026-11-19, before its rewrite)
}

// The voice buses with ProcessCtx::bus_mono (the engine's: the right half of a bus is its left one while every voice is centred) against
// both halves always written: three voices into the four buses, centred or panned in every order, each bus read back by BusIn.
TEST(voice_buses_read_the_same_with_and_without_the_mono_bit) {
    DspRig rig;
    Memory mem{&rig.heap, &rig.heap, nullptr};
    Module *vo[3], *bi[4];
    for (Module *&m : vo) m = rig.eng.registry().get(T_VOICE_OUT)->create(mem);
    for (Module *&m : bi) m = rig.eng.registry().get(T_BUS_IN)->create(mem);
    for (int k = 0; k < 4; k++) bi[k]->set_param(BUS_SEL, k);
    for (Module *m : vo) for (int j = VO_IN2; j <= VO_IN4; j++) m->set_param(j, 1);
    q15 in[3][kMaxIn][kBlock], out[2][4][2][kBlock];
    int32_t bus[2][8][kBlock];
    for (int v = 0; v < 3; v++)
        for (int k = 0; k < kMaxIn; k++)
            for (int i = 0; i < kBlock; i++) in[v][k][i] = static_cast<q15>(((i * (7 + v + 3 * k)) % 64) * 900 - 28000);
    int differ = 0;
    for (int order = 0; order < 8; order++) {               // bit v: voice v panned
        for (int v = 0; v < 3; v++) vo[v]->set_param(VO_PAN, (order >> v) & 1 ? (v == 1 ? -12000 : 9000) : 0);
        for (int pass = 0; pass < 2; pass++) {
            VoiceState vs;
            vs.gate = vs.active = true;
            uint8_t mono = 0x0F;
            for (int i = 0; i < kBlock; i++)
                for (int b = 0; b < 8; b++) bus[pass][b][i] = pass == 0 && (b & 1) ? 0x5A5A5A5A : 0;   // (pass 0: the right halves stale)
            ProcessCtx ctx{kBlock, 0, &vs, bus[pass][0], bus[pass][1], nullptr, nullptr};
            ctx.bus2_l = bus[pass][2]; ctx.bus2_r = bus[pass][3];
            for (int k = 0; k < kSendBuses; k++) { ctx.send_l[k] = bus[pass][4 + 2 * k]; ctx.send_r[k] = bus[pass][5 + 2 * k]; }
            ctx.bus_mono = pass == 0 ? &mono : nullptr;
            for (int v = 0; v < 3; v++) {
                Ports p{};
                for (int k = 0; k < kMaxIn; k++) p.in[k] = in[v][k];
                vo[v]->process(ctx, p);
            }
            ctx.voice = nullptr;
            for (int k = 0; k < 4; k++) {
                Ports p{};
                p.out[0] = out[pass][k][0]; p.out[1] = out[pass][k][1];
                bi[k]->process(ctx, p);
            }
        }
        differ += std::memcmp(out[0], out[1], sizeof out[0]) != 0;
    }
    CHECK_EQ(differ, 0);
    for (Module *m : vo) destroy_module(rig.heap, m);
    for (Module *m : bi) destroy_module(rig.heap, m);
}
