// The module bench (tools/module_bench, README.md there): every engine module type, the board's own (q15) and the catalog's float ones
// (the "F" types: SynthLab Studio's modules, framework/modular/engine_modules.h), built alone in the engine's heaps and timed per 32-frame
// block, on the board (env hwv2: CPU cycles) or in Espressif's QEMU (env qemu: run with -icount, the counter then counts instructions,
// the same number on every run). Each module also gets a checksum of everything it wrote over its 40 blocks of the driven case: an
// optimization that keeps it is exact (bit for bit), one that changes it changed the sound.
//
// Lines on the console (UART0 in QEMU, the native USB port on the board), read by tools/module_bench/module_bench.py:
//   [MB] begin <board|qemu> rate R block B budget C
//   [MB] calib nop1024 <counts>                          1024 NOPs: counts per instruction (QEMU's scale; ~1024 on the board)
//   [MB] mod <id> <name> <voice|global> <q15|f32> def D driven V modramp R modslow S start T fast F spill P silent Z sum <hex>
//   [MB] max <id> <name> <cycles> <param>=<value>       (only with "max": each value at its extremes, the slowest)
//   [MB] done <modules>
// Commands (a line on the console): "run" every module again, "run NAME [NAME ...]" only those (an engine type's name: "Filter",
// "FFilter"), "max NAME ..." the same with the extremes, "list" the types. Among the names, "param=value" sets that value after the
// defaults (every module that has it: "run Filter fm_q=2"), and "audiomod" makes the modulated cases an audio-rate sine (1.5 kHz,
// not a straight line over the block: the Filter's per-sample path) instead of the slow ramp.
#include <Arduino.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <esp32s3/rom/cache.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "engine/core/engine.h"
#include "engine/dsp/phase.h"
#include "engine/modules/builtin.h"
#include "engine/modules/dx7_voice.h"
#include "engine/modules/fx2_modules.h"
#include "engine/modules/fx_modules.h"
#include "engine/modules/mi_osc.h"
#include "engine/modules/motion_seq.h"
#include "engine/modules/osc_engines.h"
#include "engine/modules/para_modules.h"
#include "engine/modules/sampler_modules.h"
#include "engine/modules/strings_modules.h"
#include "engine/modules/synth_modules.h"
#include "modular/engine_modules.h"
#include "modular/modular.h"

namespace {
constexpr int kFrames = sc::kBlock;
constexpr int kWarm = 8, kRuns = 32;

sc::Registry g_reg;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
sc::Heap g_fast, g_bulk;
void *g_fast_mem = nullptr, *g_bulk_mem = nullptr;
size_t g_fast_size = 0, g_bulk_size = 0;

// The signals: q15 for the board's modules, the same as floats (q15 / 32768) for the float ones
alignas(16) sc::q15 q_zero[kFrames], q_saw[kFrames], q_pitch[kFrames], q_gate[kFrames], q_vel[kFrames], q_ramp[kFrames], q_audio[kFrames];
alignas(16) sc::q15 q_out[sc::kMaxOut][kFrames], q_master[2][kFrames];
alignas(16) float f_zero[kFrames], f_saw[kFrames], f_pitch[kFrames], f_gate[kFrames], f_vel[kFrames], f_ramp[kFrames];
alignas(16) float f_out[sc::kMaxOut][kFrames];
alignas(16) int32_t g_bus[4][kFrames], g_send[2 * sc::kSendBuses][kFrames];
bool g_audiomod = false;                        // "audiomod": the modulated cases get q_audio
struct ParamSet { const char *name; int32_t value; };
ParamSet g_sets[8];
int g_nsets = 0;
uint8_t g_mono = 0;                             // the engine's ProcessCtx::bus_mono (MB_NO_MONO: none, both halves of a bus always written)

void console_pause() {
#ifndef MB_QEMU
    vTaskDelay(pdMS_TO_TICKS(40));          // the USB console drops lines that come faster than it sends them
#endif
}

void *alloc_in(size_t bytes, uint32_t caps) { return heap_caps_aligned_alloc(16, bytes, caps | MALLOC_CAP_8BIT); }

void setup_memory() {
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    g_fast_size = std::min<size_t>(96 * 1024, largest > 40 * 1024 ? largest - 24 * 1024 : 0) & ~static_cast<size_t>(15);
    g_fast_mem = g_fast_size ? alloc_in(g_fast_size, MALLOC_CAP_INTERNAL) : nullptr;
    g_bulk_size = 640 * 1024;                                   // (the Delay's line, the Pads' voices)
    g_bulk_mem = alloc_in(g_bulk_size, MALLOC_CAP_SPIRAM);
    if (!g_bulk_mem) {                                          // (no PSRAM: QEMU without it) what internal RAM is left
        const size_t left = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        g_bulk_size = (left > 40 * 1024 ? left - 24 * 1024 : 0) & ~static_cast<size_t>(15);
        g_bulk_mem = g_bulk_size ? alloc_in(g_bulk_size, MALLOC_CAP_INTERNAL) : nullptr;
    }
}

void setup_signals() {
    for (int i = 0; i < kFrames; i++) {
        q_zero[i] = 0;
        q_saw[i] = static_cast<sc::q15>((i * 2 - kFrames) * (16384 / kFrames));           // half scale
        q_pitch[i] = sc::pitch_to_cv(60 * 256);
        q_gate[i] = sc::kUnity;
        q_vel[i] = static_cast<sc::q15>(100 * 32767 / 127);
        q_ramp[i] = static_cast<sc::q15>(i * (2048 / kFrames));                           // a slow modulation (an LFO, an envelope)
        q_audio[i] = static_cast<sc::q15>(9000.0f * std::sin(6.2831853f * 1500.0f * static_cast<float>(i) / static_cast<float>(sc::kSampleRate)));
    }
    for (int i = 0; i < kFrames; i++) {
        f_zero[i] = 0.f;
        f_saw[i] = q_saw[i] * (1.f / 32768.f);
        f_pitch[i] = q_pitch[i] * (1.f / 32768.f);
        f_gate[i] = 1.f;
        f_vel[i] = q_vel[i] * (1.f / 32768.f);
        f_ramp[i] = q_ramp[i] * (1.f / 32768.f);
    }
}

void register_all() {
    sc::register_builtin_modules(g_reg);
    sc::register_sampler_modules(g_reg);
    sc::register_synth_modules(g_reg);
    sc::register_fx_modules(g_reg);
    sc::register_fx2_modules(g_reg);
    sc::register_strings_modules(g_reg);
    sc::register_para_modules(g_reg);
    sc::register_dx7_module(g_reg);
    sc::register_motion_module(g_reg);
    sc::register_osc_engines(g_reg);
    sc::register_mi_osc(g_reg);
#ifndef MB_NO_FLOAT
    mod::register_modular_types(g_reg);                         // SynthLab Studio's modules, as engine types 128..191
#endif
}

uint32_t calib() {                                              // 1024 single-cycle instructions: the counter's unit
    uint32_t best = 0xFFFFFFFFu;
    for (int r = 0; r < 8; r++) {
        portENTER_CRITICAL(&g_mux);
        const uint32_t t0 = esp_cpu_get_cycle_count();
        __asm__ __volatile__(".rept 1024\n nop\n .endr\n");
        const uint32_t dt = esp_cpu_get_cycle_count() - t0;
        portEXIT_CRITICAL(&g_mux);
        best = std::min(best, dt);
    }
    return best;
}

bool name_is(const char *n, const char *const *list) {
    for (; *list; list++) if (!strcasecmp(n, *list)) return true;
    return false;
}

enum Case { kDef, kDriven, kModRamp, kModSlow };

struct Rig {
    sc::VoiceState vs;
    sc::Transport tr;
    sc::SeqPos seq;
    sc::ProcessCtx ctx{kFrames, 0, nullptr, nullptr, nullptr, nullptr, nullptr};
    sc::Ports p{};
    bool f32 = false;
    // Inputs as a voice wires them: pitch / gate / velocity from a held note, every other one silence (def) or a saw (driven). A float
    // module's unplugged input reads ctx.fsilence (its catalog default: 1, the gate, the pitch ...); driven, its audio inputs get the saw.
    void wire(const sc::ModuleInfo &info, Case c) {
        static const char *const kPitch[] = {"pitch", "note", nullptr};
        static const char *const kGate[] = {"gate", "trig", "trigger", "retrig", "clock", nullptr};
        static const char *const kVel[] = {"vel", "velocity", nullptr};
        f32 = info.sig == sc::Sig::F32;
        const mod::ModuleDef *d = f32 ? mod::modular_def_of(&info) : nullptr;
        for (int k = 0; k < sc::kMaxIn; k++) {
            const char *n = k < info.n_in && info.in_name[k] ? info.in_name[k] : "";
            if (f32) {
                const bool audio = d && k < d->n_in && d->in_def[k] == mod::kZero && strcmp(n, "Pitch") != 0;
                p.fin[k] = c != kDef && audio ? f_saw : ctx.fsilence;       // (fsilence: unplugged, the catalog's default)
            } else {
                p.in[k] = name_is(n, kPitch) ? q_pitch : name_is(n, kGate) ? q_gate : name_is(n, kVel) ? q_vel : c == kDef ? q_zero : q_saw;
            }
        }
        const bool modded = c == kModRamp || c == kModSlow;
        for (int j = 0; j < sc::kMaxParams; j++) {
            if (f32) p.fmod[j] = modded && j < info.n_param ? f_ramp : nullptr;
            else p.mod[j] = modded && j < info.n_param ? (g_audiomod ? q_audio : q_ramp) : nullptr;
        }
        p.mod_slow = c == kModSlow && !g_audiomod ? 0xFFFF : 0;
        p.in_slow = c == kModSlow ? 0xFF : 0;
        for (int o = 0; o < sc::kMaxOut; o++) {
            if (f32) p.fout[o] = f_out[o];
            else p.out[o] = q_out[o];
        }
    }
    void begin(const sc::ModuleInfo &info) {
        vs = sc::VoiceState{};
        vs.note = 60;
        vs.pitch = vs.target_pitch = 60 * 256;
        vs.velocity = static_cast<sc::q15>(100 * 32767 / 127);
        vs.gate = vs.active = vs.started = true;
        tr = sc::Transport{};
        tr.running = true;
        tr.inc_q32 = (static_cast<uint64_t>(120) << 32) / 60u / static_cast<uint64_t>(sc::kSampleRate);
        ctx = sc::ProcessCtx{kFrames, 0, info.scope == sc::Scope::Voice ? &vs : nullptr, g_bus[0], g_bus[1], q_master[0], q_master[1]};
        ctx.voices = &vs;
        ctx.nvoices = 1;
        ctx.keys_held = 1;
        ctx.bus2_l = g_bus[2];
        ctx.bus2_r = g_bus[3];
        for (int k = 0; k < sc::kSendBuses; k++) { ctx.send_l[k] = g_send[2 * k]; ctx.send_r[k] = g_send[2 * k + 1]; }
#ifndef MB_NO_MONO
        ctx.bus_mono = &g_mono;
#endif
        ctx.transport = &tr;
        ctx.silence = q_zero;
        ctx.fsilence = f_zero;
        ctx.seq = &seq;
    }
};

struct Result { uint32_t med = 0; uint32_t sum = 0; bool moved = false; };

inline uint32_t fnv(uint32_t h, const void *data, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

// One case on a built module: reset (a note's start), 8 blocks to settle, the median of 32; the checksum of every block it wrote.
Result run_case(sc::Module &m, Rig &r, Case c) {
    const sc::ModuleInfo &info = m.info();
    r.begin(info);
    r.wire(info, c);
    m.reset();
    uint32_t t[kRuns];
    Result res;
    res.sum = 2166136261u;
    for (int b = 0; b < kWarm + kRuns; b++) {
        for (int i = 0; i < kFrames; i++) {                     // (the bus as a voice leaves it; the outputs cleared: `moved` sees writes)
            g_bus[0][i] = g_bus[1][i] = g_bus[2][i] = g_bus[3][i] = c == kDef ? 0 : q_saw[i];
            for (int k = 0; k < 2 * sc::kSendBuses; k++) g_send[k][i] = c == kDef ? 0 : q_saw[i];
            q_master[0][i] = q_master[1][i] = 0;
            for (int o = 0; o < sc::kMaxOut; o++) { q_out[o][i] = 0; f_out[o][i] = 0.f; }
        }
        g_mono = 0x0F;
        portENTER_CRITICAL(&g_mux);
        const uint32_t t0 = esp_cpu_get_cycle_count();
        m.process(r.ctx, r.p);
        const uint32_t dt = esp_cpu_get_cycle_count() - t0;
        portEXIT_CRITICAL(&g_mux);
        if (b >= kWarm) t[b - kWarm] = dt;
        r.vs.started = false;
        r.vs.age++;
        r.ctx.time++;
        r.tr.beat_q32 += r.tr.inc_q32 * kFrames;
        for (int o = 0; o < info.n_out; o++) {
            if (r.f32) { res.sum = fnv(res.sum, f_out[o], sizeof f_out[o]); for (int i = 0; i < kFrames; i++) res.moved |= f_out[o][i] != 0.f; }
            else { res.sum = fnv(res.sum, q_out[o], sizeof q_out[o]); for (int i = 0; i < kFrames; i++) res.moved |= q_out[o][i] != 0; }
        }
        res.sum = fnv(res.sum, q_master, sizeof q_master);
        if (info.n_out == 0) {                                   // (VoiceOut: the buses' left halves, what it adds to)
            res.sum = fnv(fnv(res.sum, g_bus[0], sizeof g_bus[0]), g_bus[2], sizeof g_bus[2]);
            for (int k = 0; k < sc::kSendBuses; k++) res.sum = fnv(res.sum, g_send[2 * k], sizeof g_send[0]);
        }
        for (int i = 0; i < kFrames; i++) res.moved |= q_master[0][i] != 0 || q_master[1][i] != 0;
    }
    std::sort(t, t + kRuns);
    res.med = t[kRuns / 2];
    return res;
}

bool wanted(const char *name, char **names, int n) {
    if (n == 0) return true;
    for (int i = 0; i < n; i++) if (!strcasecmp(name, names[i])) return true;
    return false;
}

int bench(char **names, int n_names, bool extremes) {
    Serial.printf("[MB] begin %s rate %d block %d budget %u\n",
#ifdef MB_QEMU
                  "qemu",
#else
                  "hwv2",
#endif
                  sc::kSampleRate, kFrames, static_cast<unsigned>(240000000ull * kFrames / sc::kSampleRate));
    Serial.printf("[MB] calib nop1024 %u\n", static_cast<unsigned>(calib()));
    Serial.printf("[MB] heaps fast %u bulk %u\n", static_cast<unsigned>(g_fast_size), static_cast<unsigned>(g_bulk_size));
    console_pause();
    if (!g_fast_mem || !g_bulk_mem) { Serial.printf("[MB] no-memory\n"); return 0; }
    Rig rig;
    int done = 0;
    for (int id = 0; id < sc::Registry::kMax; id++) {
        const sc::ModuleType *t = g_reg.get(id);
        if (!t || !wanted(t->info->name, names, n_names)) continue;
        const sc::ModuleInfo &info = *t->info;
        g_fast.init(g_fast_mem, g_fast_size);
        g_bulk.init(g_bulk_mem, g_bulk_size);
        g_fast.set_spill(&g_bulk);
        sc::Memory mem{&g_fast, &g_bulk, nullptr};
        sc::Module *m = t->create(mem);
        if (!m) { Serial.printf("[MB] nomem %d %s\n", id, info.name); console_pause(); continue; }
        for (int j = 0; j < info.n_param; j++) m->set_param(j, info.param[j].def);
        for (int k = 0; k < g_nsets; k++)
            for (int j = 0; j < info.n_param; j++) if (!strcmp(info.param[j].name, g_sets[k].name)) m->set_param(j, g_sets[k].value);
        const uint32_t t0 = esp_cpu_get_cycle_count();
        m->reset();
        const uint32_t start = esp_cpu_get_cycle_count() - t0;
        const Result def = run_case(*m, rig, kDef);
        const Result drv = run_case(*m, rig, kDriven);
        const Result ramp = info.n_param ? run_case(*m, rig, kModRamp) : drv;
        const Result slow = info.n_param ? run_case(*m, rig, kModSlow) : drv;
        // (the checksum: the driven case and both modulated ones, so a change of the modulation's path shows too)
        const uint32_t sum = fnv(fnv(drv.sum, &ramp.sum, 4), &slow.sum, 4);
        Serial.printf("[MB] mod %d %s %s %s def %u driven %u modramp %u modslow %u start %u fast %u spill %u silent %d sum %08x\n", id, info.name,
                      info.scope == sc::Scope::Voice ? "voice" : "global", info.sig == sc::Sig::F32 ? "f32" : "q15", (unsigned)def.med,
                      (unsigned)drv.med, (unsigned)ramp.med, (unsigned)slow.med, (unsigned)start, (unsigned)g_fast.used(),
                      (unsigned)g_fast.spilled(), drv.moved ? 0 : 1, (unsigned)sum);
        console_pause();
        if (extremes) {
            uint32_t worst = drv.med;
            int wp = -1;
            int32_t wv = 0;
            for (int j = 0; j < info.n_param; j++) {
                const sc::ParamDesc &q = info.param[j];
                int32_t tries[48];
                int n = 0;
                if (static_cast<int64_t>(q.max) - q.min <= 40) for (int32_t x = q.min; x <= q.max && n < 48; x++) tries[n++] = x;
                else { tries[n++] = q.min; tries[n++] = q.max; }
                for (int k = 0; k < n; k++) {
                    m->set_param(j, tries[k]);
                    const Result r = run_case(*m, rig, kDriven);
                    if (r.med > worst) { worst = r.med; wp = j; wv = tries[k]; }
                }
                m->set_param(j, q.def);
                for (int k = 0; k < g_nsets; k++) if (!strcmp(q.name, g_sets[k].name)) m->set_param(j, g_sets[k].value);
                vTaskDelay(1);
            }
            Serial.printf("[MB] max %d %s %u %s=%ld\n", id, info.name, (unsigned)worst, wp >= 0 ? info.param[wp].name : "-", (long)wv);
            console_pause();
        }
        sc::destroy_module(g_fast, m);
        done++;
        vTaskDelay(1);
    }
    Serial.printf("[MB] done %d\n", done);
    return done;
}

void command(char *line) {
    char *names[24];
    int n = 0;
    char *w = strtok(line, " ");
    if (!w) return;
    if (!strcmp(w, "list")) {
        for (int id = 0; id < sc::Registry::kMax; id++)
            if (const sc::ModuleType *t = g_reg.get(id)) Serial.printf("[MB] type %d %s\n", id, t->info->name);
        Serial.printf("[MB] done 0\n");
        return;
    }
    const bool extremes = !strcmp(w, "max");
    if (!extremes && strcmp(w, "run") != 0) { Serial.printf("[MB] unknown %s\n", w); return; }
    g_audiomod = false;
    g_nsets = 0;
    while (n < 24 && (w = strtok(nullptr, " "))) {
        if (!strcmp(w, "audiomod")) { g_audiomod = true; continue; }
        if (char *eq = strchr(w, '=')) {
            if (g_nsets < 8) { *eq = 0; g_sets[g_nsets++] = ParamSet{w, static_cast<int32_t>(strtol(eq + 1, nullptr, 10))}; }
            continue;
        }
        names[n++] = w;
    }
    bench(names, n, extremes);
}
}  // namespace

void setup() {
    Serial.begin(115200);
#ifndef MB_QEMU
    delay(1500);                                                // (the host opens the USB console)
#endif
    setup_memory();
    setup_signals();
    register_all();
    bench(nullptr, 0, false);
}

void loop() {
    static char buf[200];
    static int n = 0;
    while (Serial.available() > 0) {
        const int ch = Serial.read();
        if (ch == '\n' || ch == '\r') { buf[n] = 0; if (n) command(buf); n = 0; }
        else if (n < static_cast<int>(sizeof buf) - 1) buf[n++] = static_cast<char>(ch);
    }
    delay(5);
}
