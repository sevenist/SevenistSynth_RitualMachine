// Dev-only benchmark (build flag SYNTHLAB_BENCH, the environment hwv2_bench / hwv1_bench of platformio.ini): what each module of the
// modular catalog (framework/modular) costs on this chip, in CPU cycles per 32-frame block, for SynthLab Studio's cycles wallet; then
// what the engine costs around a synth (its fixed cost, a voice's) and each of SynthLab's synths per voice. Printed on the serial console
// as [MBENCH] lines (tools/module_bench.py reads them and writes framework/modular/module_costs.h). Runs 6 s after the start, again at
// each 'b' received on the console. Do not touch the board while it runs (it plays notes from its own task).
//
// A module is measured alone, as a synth generated from a patch calls it (framework/modular/modules.h, process() on a block): built in
// internal RAM, started, 8 blocks to settle, then the median (and the fastest) of 32 blocks, each one timed with this core's
// interrupts off. Three cases: its inputs at their defaults with its default values ("def"); every input that reads silence by default
// fed a sine instead ("driven"); and the driven case with each value in turn at its minimum and its maximum (each choice of a value with
// names): the slowest of those is "max", with the value that gave it. Then the default case again with the module in PSRAM (what a
// voice too big for the fast heap gets), and the cycles of its start().
#if defined(ARDUINO_ARCH_ESP32) && defined(SYNTHLAB_BENCH)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "engine/core/engine.h"
#include "engine/dsp/block.h"
#include "engine/dsp/curve.h"
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
#include "lab.h"                           // (before Arduino.h: its F() macro would hit lab.h's template parameter F)
#include "modular/engine_adapter.h"
#include "modular/engine_modules.h"
#include "modular/modular.h"
#include "modular/patch.h"
#include "synth.h"
#include <Arduino.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp32s3/rom/cache.h>

// synth.cpp, with SYNTHLAB_BENCH: the cycles synth_render spent (the audio task's whole render) and the frames it rendered
extern volatile uint32_t g_bench_render_cycles, g_bench_render_frames;
void bench_heap(size_t *used, size_t *cap, size_t *spilled);   // the engine's fast heap: in use, its size, what spilled into PSRAM

namespace {
constexpr int kFrames = 32;                 // the engine's block (SynthLab renders a voice 32 frames at a time)
constexpr int kWarm = 8, kRuns = 32;
constexpr size_t kInternalMax = 48 * 1024;  // bigger modules are measured in PSRAM only (a Delay's 128 KB line)

alignas(16) float g_in[mod::kMaxIn][kFrames];
alignas(16) float g_out[mod::kMaxOut][kFrames];
alignas(16) float g_zero[kFrames], g_one[kFrames], g_sig[kFrames], g_trig[kFrames];
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
volatile float g_sink;
// Cold: the instruction cache emptied before each timed block (the ROM's Cache_Invalidate_ICache_All; the data cache is left alone).
// In the engine a synth's code runs from flash through a 16 KB cache that the screen's code on the other core shares (engine/dsp/config.h
// SC_HOT): a block of a voice starts cold, the next voices of the same block find the code warm.
bool g_cold = false;

struct Stat { uint32_t med = 0, min = 0; };

Stat stat_of(uint32_t *t, int n) {
    std::sort(t, t + n);
    Stat s;
    s.min = t[0];
    s.med = t[n / 2];
    return s;
}

// One case: the module built in `mem`, its values v, its inputs driven or at their defaults.
Stat run_case(const mod::ModuleDef &d, void *mem, const float *v, bool driven, uint32_t *start_cycles = nullptr) {
    mod::Ctx c;
    c.sr = static_cast<float>(synth_sample_rate());
    c.inv_sr = 1.f / c.sr;
    c.gate = true;
    mod::IO io{};
    io.p = v;
    io.frames = kFrames;
    for (int o = 0; o < mod::kMaxOut; o++) io.out[o] = g_out[o];
    for (int k = 0; k < mod::kMaxIn; k++) io.in[k] = g_zero;
    if (d.construct_in)                                     // an engine module (RM): built for the area the engine has its type in
        d.construct_in(mem, d.engine && !mod::engine_type_in(d.engine, mod::kVoiceArea) ? mod::kFxArea : mod::kVoiceArea);
    else d.construct(mem);
    uint32_t t0 = esp_cpu_get_cycle_count();
    d.start(mem, c);
    const uint32_t st = esp_cpu_get_cycle_count() - t0;
    if (start_cycles) *start_cycles = st;
    uint32_t t[kRuns];
    for (int b = 0; b < kWarm + kRuns; b++) {
        for (int k = 0; k < d.n_in; k++) {
            switch (d.in_def[k]) {
            case mod::kOne: io.in[k] = g_one; break;
            case mod::kGate: io.in[k] = g_one; break;               // the key held
            case mod::kTrig: io.in[k] = b == 0 ? g_trig : g_zero; break;
            case mod::kIn0: io.in[k] = io.in[0]; break;
            case mod::kPitch: io.in[k] = g_zero; break;
            default: io.in[k] = driven ? g_sig : g_zero; break;
            }
        }
        portENTER_CRITICAL(&g_mux);
        if (g_cold) Cache_Invalidate_ICache_All();
        t0 = esp_cpu_get_cycle_count();
        d.process(mem, c, io);
        const uint32_t dt = esp_cpu_get_cycle_count() - t0;
        portEXIT_CRITICAL(&g_mux);
        if (b >= kWarm) t[b - kWarm] = dt;
    }
    g_sink = g_out[0][kFrames - 1];
    d.destroy(mem);
    return stat_of(t, kRuns);
}

// The USB console drops and merges bytes when lines come faster than it sends them (the timing runs with this core's interrupts off):
// each result line is followed by a pause that lets it drain (no Serial.flush(): on the USB console it can drop what is waiting)
void drain() {
    vTaskDelay(pdMS_TO_TICKS(60));
}

void *alloc_in(size_t bytes, bool psram) {
    return heap_caps_aligned_alloc(16, bytes, (psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL) | MALLOC_CAP_8BIT);
}

void bench_module(const mod::ModuleDef &d) {
    const bool fits = d.size <= kInternalMax;
    void *fast = fits ? alloc_in(d.size, false) : nullptr;
    void *slow = alloc_in(d.size, true);
    void *mem = fast ? fast : slow;
    if (!mem) {
        Serial.printf("[MBENCH] module %s size %u no-memory\n", d.name, (unsigned)d.size);
        return;
    }
    float v[mod::kMaxParams] = {};
    for (int j = 0; j < d.n_param; j++) v[j] = d.param[j].def;
    uint32_t st = 0;
    const Stat def = run_case(d, mem, v, false, &st);
    const Stat drv = run_case(d, mem, v, true);
    Stat worst = drv;
    int worst_p = -1;
    float worst_v = 0.f;
    for (int j = 0; j < d.n_param; j++) {
        const mod::Param &q = d.param[j];
        float tries[64];
        int n = 0;
        if (q.labels && q.curve == mod::Int) for (float x = q.min; x <= q.max && n < 64; x += 1.f) tries[n++] = x;
        else { tries[n++] = q.min; tries[n++] = q.max; }
        for (int k = 0; k < n; k++) {
            float w[mod::kMaxParams];
            std::memcpy(w, v, sizeof w);
            w[j] = tries[k];
            const Stat s = run_case(d, mem, w, true);
            if (s.med > worst.med) { worst = s; worst_p = j; worst_v = tries[k]; }
        }
        vTaskDelay(1);                                      // (the idle task of this core)
    }
    Stat ps;
    if (fast && slow) ps = run_case(d, slow, v, false);
    else if (!fast) ps = def;
    g_cold = true;
    const Stat cold = run_case(d, mem, v, false);
    g_cold = false;
    Serial.printf("[MBENCH] module %s size %u mem %s def %u %u driven %u %u max %u %u at %s=%g psram %u start %u cold %u\n", d.name, (unsigned)d.size,
                  fast ? "internal" : "psram", (unsigned)def.med, (unsigned)def.min, (unsigned)drv.med, (unsigned)drv.min, (unsigned)worst.med,
                  (unsigned)worst.min, worst_p >= 0 ? d.param[worst_p].name : "-", (double)worst_v, (unsigned)ps.med, (unsigned)st, (unsigned)cold.med);
    drain();
    if (fast) heap_caps_free(fast);
    if (slow) heap_caps_free(slow);
}

// What a generated synth adds around its modules (apps/studio/src/export.cpp synth_source): a modulation cable's value (to_norm,
// from_norm once a block, with its source's mean over the block), the voice's block (the gate and pulse buffers, the VoiceOut sum and
// its peak), a summed input (a cable at a depth other than 1, or two cables on one input: a multiply-add a sample each).
volatile float g_lo = 20.f, g_hi = 20000.f, g_v = 1000.f, g_amt = 0.1f;
template <typename F>
uint32_t time_med(F f) {
    uint32_t t[kRuns];
    for (int b = 0; b < kWarm + kRuns; b++) {
        portENTER_CRITICAL(&g_mux);
        if (g_cold) Cache_Invalidate_ICache_All();
        const uint32_t t0 = esp_cpu_get_cycle_count();
        f();
        const uint32_t dt = esp_cpu_get_cycle_count() - t0;
        portEXIT_CRITICAL(&g_mux);
        if (b >= kWarm) t[b - kWarm] = dt;
    }
    return stat_of(t, kRuns).med;
}
float mean(const float *b, int n) {
    float s = 0.f;
    for (int i = 0; i < n; i++) s += b[i];
    return s / static_cast<float>(n);
}
void bench_glue() {
    for (int curve = 0; curve < 3; curve++) {
        const uint32_t c = time_med([curve] {
            const mod::Param q{"", g_lo, g_hi, g_lo, nullptr, static_cast<mod::Curve>(curve)};
            g_sink = mod::from_norm(q, mod::to_norm(q, g_v) + mean(g_sig, kFrames) * g_amt);
        });
        static const char *const names[] = {"Lin", "Exp", "Int"};
        Serial.printf("[MBENCH] glue modcable-%s %u\n", names[curve], (unsigned)c);
    }
    static float gate[kFrames], trg[kFrames], out[kFrames];
    static bool trig = true;
    const uint32_t vb = time_med([] {
        float peak = 0.f;
        for (int i = 0; i < kFrames; i++) { gate[i] = 1.f; trg[i] = 0.f; }
        if (trig) { trg[0] = 1.f; trig = false; }
        for (int i = 0; i < kFrames; i++) {
            float y = g_out[0][i];
            if (y != y) y = 0.f;
            out[i] += y;
            peak = y > peak ? y : (-y > peak ? -y : peak);
        }
        g_sink = peak + gate[kFrames - 1] + trg[0] + out[kFrames - 1];     // (the buffers used: the compiler keeps their loops)
    });
    Serial.printf("[MBENCH] glue voice-block %u\n", (unsigned)vb);
    static float sum[kFrames];
    const uint32_t si = time_med([] { for (int i = 0; i < kFrames; i++) sum[i] = g_sig[i] * g_amt; g_sink = sum[3]; });
    Serial.printf("[MBENCH] glue summed-input %u\n", (unsigned)si);
}

// The engine around a synth: synth_render's cycles per 32 frames with no note, 1 and 6 notes held, for each of SynthLab's synths; and
// the synth's Voice (and Fx) alone, rendered here as the modules are. Overhead per voice = (6 notes - none) / 6 - the Voice alone.
#ifdef ENGINE_PROFILE
// (with -DENGINE_PROFILE: the engine's cycles per module type over the same window, taken by the audio thread: synth.cpp)
}  // namespace
extern volatile int g_bench_prof_req, g_bench_prof_n;
extern sc::Engine::ProfEntry g_bench_prof[sc::Engine::kProfMax];
extern uint32_t g_bench_prof_blocks;
namespace {
void prof_take() {
    g_bench_prof_req = 1;
    for (int i = 0; i < 100 && g_bench_prof_req; i++) vTaskDelay(1);
}
#endif

uint32_t render_per_block(int ms, const char *what = nullptr) {
#ifdef ENGINE_PROFILE
    prof_take();                                            // (cleared: the window starts)
#endif
    g_bench_render_cycles = 0;
    g_bench_render_frames = 0;
    vTaskDelay(pdMS_TO_TICKS(ms));
    const uint32_t c = g_bench_render_cycles, f = g_bench_render_frames;
#ifdef ENGINE_PROFILE
    prof_take();
    const uint32_t blocks = g_bench_prof_blocks ? g_bench_prof_blocks : 1;
    for (int i = 0; what && i < g_bench_prof_n; i++)
        Serial.printf("[MBENCH] prof %s %s %u calls %u\n", what, g_bench_prof[i].name, (unsigned)(g_bench_prof[i].cycles / blocks),
                      (unsigned)(g_bench_prof[i].calls / blocks));
#else
    (void)what;
#endif
    return f ? static_cast<uint32_t>(static_cast<uint64_t>(c) * kFrames / f) : 0;
}

// The Voice alone: started, `skip` blocks rendered untimed (later in the note: what the engine's measure sees, 100 ms in), then timed.
uint32_t voice_alone(const lab::Synth &s, const float *p, bool psram, int skip) {
    static float buf[kFrames];
    const lab::Note n{261.63f, 100.f / 127.f, true, static_cast<float>(synth_sample_rate())};
    uint32_t med = 0;
    if (void *m = alloc_in(s.size, psram || s.size > kInternalMax)) {
        s.construct(m);
        s.start(m, n, p);
        for (int b = 0; b < skip; b++) { for (float &x : buf) x = 0.f; s.render(m, buf, kFrames, n, p); }
        med = time_med([&] { for (float &x : buf) x = 0.f; s.render(m, buf, kFrames, n, p); });
        s.destroy(m);
        heap_caps_free(m);
    }
    return med;
}

uint32_t synth_voice_alone(const lab::Synth &s, uint32_t *fx_cycles) {
    float p[lab::kMaxParams];
    for (int i = 0; i < s.n_params; i++) p[i] = synth_value(i);
    static float buf[kFrames];
    const lab::Note n{261.63f, 100.f / 127.f, true, static_cast<float>(synth_sample_rate())};
    const int skip = synth_sample_rate() / 10 / kFrames;     // (100 ms)
    const uint32_t med = voice_alone(s, p, false, 0);
    g_cold = true;
    const uint32_t cold = voice_alone(s, p, false, skip);
    g_cold = false;
    Serial.printf("[MBENCH] voice %s start %u later %u psram-later %u cold-later %u\n", s.name, (unsigned)med, (unsigned)voice_alone(s, p, false, skip),
                  (unsigned)voice_alone(s, p, true, skip), (unsigned)cold);
    *fx_cycles = 0;
    if (s.fx_size) {
        if (void *m = alloc_in(s.fx_size, s.fx_size > kInternalMax)) {
            s.fx_construct(m);
            *fx_cycles = time_med([&] { for (int i = 0; i < kFrames; i++) buf[i] = g_sig[i] * 0.3f; s.fx_render(m, buf, kFrames, n.sr, p); });
            s.fx_destroy(m);
            heap_caps_free(m);
        }
    }
    return med;
}

void bench_engine() {
    const int keep = synth_selected();
    static const int kChord[6] = {48, 52, 55, 60, 64, 67};
    size_t used = 0, cap = 0, spilled = 0;
    bench_heap(&used, &cap, &spilled);
    Serial.printf("[MBENCH] heap fast used %u of %u spilled %u\n", (unsigned)used, (unsigned)cap, (unsigned)spilled);
    for (int s = 0; s < lab::kSynthCount; s++) {
        synth_select(s);
        vTaskDelay(pdMS_TO_TICKS(300));
        char what[32];
        std::snprintf(what, sizeof what, "%s idle", lab::kSynths[s]->name);
        const uint32_t idle = render_per_block(800, what);
        synth_note_on(kChord[0], 100);
        vTaskDelay(pdMS_TO_TICKS(100));
        std::snprintf(what, sizeof what, "%s 1-note", lab::kSynths[s]->name);
        const uint32_t one = render_per_block(400, what);
        const int v1 = synth_active_voices();
        for (int k = 1; k < 6; k++) synth_note_on(kChord[k], 100);
        vTaskDelay(pdMS_TO_TICKS(100));
        const uint32_t six = render_per_block(400);
        const int v6 = synth_active_voices();
        synth_all_notes_off();
        vTaskDelay(pdMS_TO_TICKS(300));
        uint32_t fx = 0;
        const uint32_t alone = synth_voice_alone(*lab::kSynths[s], &fx);
        Serial.printf("[MBENCH] synth %s voice %u bytes %u fx %u bytes %u render-idle %u render-1 %u voices %d render-6 %u voices %d\n",
                      lab::kSynths[s]->name, (unsigned)alone, (unsigned)lab::kSynths[s]->size, (unsigned)fx, (unsigned)lab::kSynths[s]->fx_size,
                      (unsigned)idle, (unsigned)one, v1, (unsigned)six, v6);
    }
    synth_select(keep);
}

// The catalog's modules as engine types (framework/modular/engine_modules.h: what an engine synth plays), each one alone: built by the
// engine's own registry in its heaps, process() timed with the engine's ports, a voice's note (60, velocity 100, the key held). An
// input that is audio by default gets a saw; the others are unplugged (the module reads its default: ProcessCtx::fsilence). Since
// ADR-050 step 2 they are float modules (Sig::F32): float ports, no conversion inside; the difference from the module's own cycles is
// the host's (the values, the note, the defaults). Then the engine's own conversion steps (ToF / ToQ, dsp/block.h), a block each.
void bench_ftypes() {
    static sc::Registry reg;
    static bool registered = false;
    if (!registered) { mod::register_modular_types(reg); registered = true; }
    static sc::Heap fast_heap, bulk_heap;
    constexpr size_t kFast = 16 * 1024, kBulk = 256 * 1024;  // (a module's state over 8 KB goes to the bulk heap: engine_modules.cpp)
    static void *fast_mem = alloc_in(kFast, false), *bulk_mem = alloc_in(kBulk, true);
    if (!fast_mem || !bulk_mem) { Serial.printf("[MBENCH] ftype no-memory\n"); return; }
    alignas(16) static float silence[kFrames], saw[kFrames], outs[sc::kMaxOut][kFrames];
    alignas(16) static sc::q15 qsilence[kFrames];
    for (int i = 0; i < kFrames; i++) { silence[i] = 0.f; saw[i] = g_sig[i]; qsilence[i] = 0; }
    for (int id = mod::kModularTypeFirst; id < sc::Registry::kMax; id += 2) {
        const sc::ModuleType *t = reg.get(id);
        if (!t) continue;
        const mod::ModuleDef *d = mod::modular_def_of(t->info);
        if (!d) continue;
        fast_heap.init(fast_mem, kFast);
        bulk_heap.init(bulk_mem, kBulk);
        sc::Memory mem{&fast_heap, &bulk_heap, nullptr};
        sc::Module *m = t->create(mem);
        if (!m) { Serial.printf("[MBENCH] ftype %s no-memory\n", d->name); continue; }
        sc::VoiceState vs;
        vs.note = 60;
        vs.pitch = vs.target_pitch = 60 * 256;
        vs.velocity = static_cast<sc::q15>(100 * 32767 / 127);
        vs.gate = vs.active = true;
        vs.started = true;
        sc::ProcessCtx ctx{kFrames, 0, &vs, nullptr, nullptr, nullptr, nullptr};
        ctx.silence = qsilence;
        ctx.fsilence = silence;
        ctx.keys_held = 1;
        sc::Ports p{};
        for (int k = 0; k < sc::kMaxIn; k++)
            p.fin[k] = k < d->n_in && d->in_def[k] == mod::kZero && std::strcmp(d->in_name[k], "Pitch") != 0 ? saw : silence;
        for (int o = 0; o < sc::kMaxOut; o++) p.fout[o] = outs[o];
        for (int j = 0; j < sc::kMaxParams; j++) p.fmod[j] = nullptr;
        for (int j = 0; j < d->n_param; j++) m->set_param(j, t->info->param[j].def);
        const uint32_t c = time_med([&] { m->process(ctx, p); ctx.time++; vs.started = false; });
        Serial.printf("[MBENCH] ftype %s %u\n", d->name, (unsigned)c);
        m->~Module();
        vTaskDelay(1);
    }
    alignas(16) static sc::q15 q[kFrames];
    alignas(16) static float f[kFrames];
    for (int i = 0; i < kFrames; i++) q[i] = static_cast<sc::q15>(g_sig[i] * 32767.f);
    const uint32_t to_f = time_med([] { sc::block_q15_to_f(f, q, kFrames); g_sink = f[5]; });
    for (int i = 0; i < kFrames; i++) f[i] = g_sig[i] * 1.2f;                // (some past full scale: the clamp)
    const uint32_t to_q = time_med([] { sc::block_f_to_q15(q, f, kFrames); g_sink = q[5]; });
    Serial.printf("[MBENCH] glue engine-ToF %u\n", (unsigned)to_f);
    drain();
    Serial.printf("[MBENCH] glue engine-ToQ %u\n", (unsigned)to_q);
    drain();
    // The FPU's own conversions (FLOAT.S / ROUND.S, dsp/block.h) against the portable ones: every q15 value there and back, a sweep of
    // floats past full scale on a grid of 1/8 LSB (the ties), and what the infinities and NaN give.
    int back = 0, differ = 0;
    for (int v = -32768; v <= 32767; v++) {
        const sc::q15 x = static_cast<sc::q15>(v);
        if (sc::q15_to_f(x) != sc::q15_to_f_c(x) || sc::f_to_q15(sc::q15_to_f(x)) != x) back++;
    }
    for (int k = -40000 * 8; k <= 40000 * 8; k++) {
        const float x = static_cast<float>(k) * (1.f / (8.f * 32768.f));
        if (sc::f_to_q15(x) != sc::f_to_q15_c(x)) differ++;
    }
    volatile float inf = 1e30f;
    inf = inf * inf;
    volatile float nan = inf - inf;
    for (int cv : {20000, 18000, 8000, -20000, 32767}) {     // the envelope curve table (dsp/curve.h, single precision) against the double formula
        static sc::CurveLut lut;
        lut.build(static_cast<sc::q15>(cv));
        const double k = 8.0 * cv / 32767.0;
        int worst = 0;
        int ref[3] = {};
        for (int i = 0; i <= sc::CurveLut::kN; i++) {
            const double pp = static_cast<double>(i) / sc::CurveLut::kN;
            const double s = (1.0 - std::exp(-k * pp)) / (1.0 - std::exp(-k));
            const int r = static_cast<int>(std::lround(std::fmax(0.0, std::fmin(1.0, s)) * 32767.0));
            if (std::abs(r - lut.t[i]) > worst) worst = std::abs(r - lut.t[i]);
            if (i == 32) ref[0] = r;
            if (i == 64) ref[1] = r;
            if (i == 96) ref[2] = r;
        }
        Serial.printf("[MBENCH] curve %d: float %d %d %d %d, double %d %d %d, worst diff %d\n", cv, lut.t[32], lut.t[64], lut.t[96], lut.t[128], ref[0], ref[1], ref[2], worst);
        drain();
    }
    int blocks_differ = 0;                                  // the block loops: the sweep in blocks, NaN and the infinities among them
    for (int k0 = -40000 * 8; k0 <= 40000 * 8; k0 += kFrames) {
        for (int i = 0; i < kFrames; i++) f[i] = static_cast<float>(k0 + i) * (1.f / (8.f * 32768.f));
        if ((k0 / kFrames) % 64 == 0) { f[3] = nan; f[7] = inf; f[9] = -inf; }
        sc::block_f_to_q15(q, f, kFrames);
        for (int i = 0; i < kFrames; i++) if (q[i] != sc::f_to_q15_c(f[i])) blocks_differ++;
    }
    for (int v = -32768; v <= 32767 - kFrames; v += kFrames) {
        for (int i = 0; i < kFrames; i++) q[i] = static_cast<sc::q15>(v + i);
        sc::block_q15_to_f(f, q, kFrames);
        for (int i = 0; i < kFrames; i++) if (f[i] != sc::q15_to_f_c(q[i])) blocks_differ++;
    }
    Serial.printf("[MBENCH] conv check round-trip-wrong %d sweep-differ %d blocks-differ %d inf %d -inf %d nan %d nan-c %d\n", back, differ, blocks_differ,
                  (int)sc::f_to_q15(inf), (int)sc::f_to_q15(-inf), (int)sc::f_to_q15(nan), (int)sc::f_to_q15_c(nan));
    drain();
}

#ifdef SYNTHLAB_BENCH_RM
// The engine's own modules, natively (ADR-050 step 0: the float / q15 comparison): each registered type built by the registry in the
// engine's heaps and its process() timed with q15 ports, as the engine's plan calls it, with no adapter and no conversion around it.
// Its inputs are wired as a Ritual Machine voice wires them: "pitch" / "gate" / "vel" (and the like) from a held note, as NoteIn
// gives them; every other input silence ("def") or a saw ("driven"). Then the driven case with every value's modulation input fed
// at once, read per sample: a constant ("modconst"), a ramp over the block ("modramp": a slow LFO or an envelope), a saw ("modaudio":
// audio-rate modulation); and the ramp marked slow ("modslow": Ports::mod_slow, what the plan gives a module that only slow sources
// modulate, ADR-050: the block-rate reading). "max": the driven case with each value in turn at
// its minimum and maximum (every value of a range of 40 or less), the slowest, and which. "silent": the output was zero in the driven
// case (a module that needs a service the bench has not: samples, an FM patch, an impulse response), its cost not representative.
sc::Registry g_rm_reg;
alignas(16) sc::q15 g_q_zero[kFrames], g_q_saw[kFrames], g_q_pitch[kFrames], g_q_gate[kFrames], g_q_vel[kFrames];
alignas(16) sc::q15 g_q_modc[kFrames], g_q_modr[kFrames], g_q_moda[kFrames], g_q_out[sc::kMaxOut][kFrames], g_q_master[2][kFrames];
alignas(16) int32_t g_bus[4][kFrames];

bool name_is(const char *n, const char *const *list) {
    for (; *list; list++) if (!std::strcmp(n, *list)) return true;
    return false;
}

enum NativeCase { kDef, kDriven, kModConst, kModRamp, kModAudio, kModSlow };

struct NativeRig {
    sc::VoiceState vs;
    sc::Transport tr;
    sc::SeqPos seq;
    sc::ProcessCtx ctx{kFrames, 0, nullptr, nullptr, nullptr, nullptr, nullptr};
    sc::Ports p{};
    void wire(const sc::ModuleInfo &info, NativeCase c) {
        static const char *const kPitch[] = {"pitch", "note", nullptr};
        static const char *const kGate[] = {"gate", "trig", "trigger", "retrig", "clock", nullptr};
        static const char *const kVel[] = {"vel", "velocity", nullptr};
        for (int k = 0; k < sc::kMaxIn; k++) {
            const char *n = k < info.n_in ? info.in_name[k] : "";
            p.in[k] = !n ? g_q_zero : name_is(n, kPitch) ? g_q_pitch : name_is(n, kGate) ? g_q_gate : name_is(n, kVel) ? g_q_vel
                    : c == kDef ? g_q_zero : g_q_saw;
        }
        const sc::q15 *m = c == kModConst ? g_q_modc : c == kModRamp || c == kModSlow ? g_q_modr : c == kModAudio ? g_q_moda : nullptr;
        for (int j = 0; j < sc::kMaxParams; j++) p.mod[j] = j < info.n_param ? m : nullptr;
#ifdef SC_MOD_RATES                                         // (an engine before ADR-050 step 1 has no marks: modslow = modramp)
        p.mod_slow = c == kModSlow ? 0xFFFF : 0;            // (ADR-050: the ramp marked slow, as the plan marks an envelope's: the
        p.in_slow = c == kModSlow ? 0xFF : 0;               //   block-rate reading)
#endif
        for (int o = 0; o < sc::kMaxOut; o++) p.out[o] = g_q_out[o];
    }
    void begin(const sc::ModuleInfo &info) {
        vs = sc::VoiceState{};
        vs.note = 60;
        vs.pitch = vs.target_pitch = 60 * 256;
        vs.velocity = static_cast<sc::q15>(100 * 32767 / 127);
        vs.gate = vs.active = vs.started = true;
        tr.running = true;
        tr.inc_q32 = (static_cast<uint64_t>(120) << 32) / 60u / static_cast<uint64_t>(sc::kSampleRate);
        tr.beat_q32 = 0;
        ctx = sc::ProcessCtx{kFrames, 0, info.scope == sc::Scope::Voice ? &vs : nullptr, g_bus[0], g_bus[1], g_q_master[0], g_q_master[1]};
        ctx.voices = &vs;
        ctx.nvoices = 1;
        ctx.keys_held = 1;
        ctx.bus2_l = g_bus[2];
        ctx.bus2_r = g_bus[3];
        ctx.transport = &tr;
        ctx.silence = g_q_zero;
        ctx.seq = &seq;
    }
};

// One case on a built module: reset (a note's start), 8 blocks to settle, the median of 32. `nonzero`: the output moved.
uint32_t native_case(sc::Module &m, NativeRig &r, NativeCase c, bool *nonzero = nullptr) {
    const sc::ModuleInfo &info = m.info();
    r.begin(info);
    r.wire(info, c);
    m.reset();
    uint32_t t[kRuns];
    bool moved = false;
    for (int b = 0; b < kWarm + kRuns; b++) {
        for (int i = 0; i < kFrames; i++) {                 // (the bus as a voice leaves it: VoiceOut accumulates, the plan clears it)
            g_bus[0][i] = g_bus[1][i] = g_bus[2][i] = g_bus[3][i] = c == kDef ? 0 : g_q_saw[i];
            g_q_master[0][i] = g_q_master[1][i] = 0;      // (cleared: what the module writes is what `nonzero` sees)
            for (int o = 0; o < sc::kMaxOut; o++) g_q_out[o][i] = 0;
        }
        portENTER_CRITICAL(&g_mux);
        if (g_cold) Cache_Invalidate_ICache_All();
        const uint32_t t0 = esp_cpu_get_cycle_count();
        m.process(r.ctx, r.p);
        const uint32_t dt = esp_cpu_get_cycle_count() - t0;
        portEXIT_CRITICAL(&g_mux);
        if (b >= kWarm) t[b - kWarm] = dt;
        r.vs.started = false;
        r.vs.age++;
        r.ctx.time++;
        r.tr.beat_q32 += r.tr.inc_q32 * kFrames;
        for (int o = 0; o < info.n_out; o++) for (int i = 0; i < kFrames; i++) moved |= g_q_out[o][i] != 0;
        for (int i = 0; i < kFrames; i++) moved |= g_q_master[0][i] != 0 || g_q_master[1][i] != 0;
    }
    if (nonzero) *nonzero = moved;
    return stat_of(t, kRuns).med;
}

void bench_native() {
    static sc::Heap fast_heap, bulk_heap;
    static size_t fast_size = 0;
    static void *fast_mem = nullptr, *bulk_mem = nullptr;
    constexpr size_t kBulk = 640 * 1024;                    // (the Delay's line: ENGINE_DELAY_MAX_MS, mono; the Pads' voices)
    if (!fast_mem) {
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        fast_size = std::min<size_t>(96 * 1024, largest > 24 * 1024 ? largest - 16 * 1024 : 0) & ~static_cast<size_t>(15);
        fast_mem = fast_size ? alloc_in(fast_size, false) : nullptr;
        bulk_mem = alloc_in(kBulk, true);
    }
    if (!fast_mem || !bulk_mem) { Serial.printf("[MBENCH] native no-memory\n"); return; }
    Serial.printf("[MBENCH] native-heaps fast %u bulk %u\n", (unsigned)fast_size, (unsigned)kBulk);
    NativeRig rig;
    for (int id = 0; id < mod::kModularTypeFirst; id++) {      // (the engine's own types; 128 and up are the catalog's F types)
        const sc::ModuleType *t = g_rm_reg.get(id);
        if (!t) continue;
        const sc::ModuleInfo &info = *t->info;
        Serial.printf("[MBENCH] native-try %d %s\n", id, info.name);   // (if the board stops here, this module took it down)
        drain();
        fast_heap.init(fast_mem, fast_size);
        bulk_heap.init(bulk_mem, kBulk);
        fast_heap.set_spill(&bulk_heap);
        sc::Memory mem{&fast_heap, &bulk_heap, nullptr};
        const size_t spilled0 = fast_heap.spilled();       // (the heap's count adds up: this module's share)
        sc::Module *m = t->create(mem);
        if (!m) { Serial.printf("[MBENCH] native %d %s no-memory\n", id, info.name); continue; }
        for (int j = 0; j < info.n_param; j++) m->set_param(j, info.param[j].def);
        const uint32_t t0 = esp_cpu_get_cycle_count();
        m->reset();
        const uint32_t start = esp_cpu_get_cycle_count() - t0;
        bool moved = false;
        const uint32_t def = native_case(*m, rig, kDef);
        const uint32_t drv = native_case(*m, rig, kDriven, &moved);
        const uint32_t mc = info.n_param ? native_case(*m, rig, kModConst) : drv;
        const uint32_t mr = info.n_param ? native_case(*m, rig, kModRamp) : drv;
        const uint32_t ma = info.n_param ? native_case(*m, rig, kModAudio) : drv;
        const uint32_t ms = info.n_param ? native_case(*m, rig, kModSlow) : drv;
        uint32_t worst = drv;
        int worst_p = -1;
        int32_t worst_v = 0;
        for (int j = 0; j < info.n_param; j++) {
            const sc::ParamDesc &q = info.param[j];
            int32_t tries[48];
            int n = 0;
            if (static_cast<int64_t>(q.max) - q.min <= 40) for (int32_t x = q.min; x <= q.max && n < 48; x++) tries[n++] = x;
            else { tries[n++] = q.min; tries[n++] = q.max; }
            for (int k = 0; k < n; k++) {
                m->set_param(j, tries[k]);
                const uint32_t c = native_case(*m, rig, kDriven);
                if (c > worst) { worst = c; worst_p = j; worst_v = tries[k]; }
            }
            m->set_param(j, q.def);
            vTaskDelay(1);                                  // (the idle task of this core)
        }
        g_cold = true;
        const uint32_t cold = native_case(*m, rig, kDriven);
        g_cold = false;
        Serial.printf("[MBENCH] native %d %s scope %s fast %u spill %u def %u driven %u modconst %u modramp %u modaudio %u modslow %u max %u at %s=%ld "
                      "cold %u start %u silent %d\n", id, info.name, info.scope == sc::Scope::Voice ? "voice" : "global", (unsigned)fast_heap.used(),
                      (unsigned)(fast_heap.spilled() - spilled0), (unsigned)def, (unsigned)drv, (unsigned)mc, (unsigned)mr, (unsigned)ma, (unsigned)ms, (unsigned)worst,
                      worst_p >= 0 ? info.param[worst_p].name : "-", (long)worst_v, (unsigned)cold, (unsigned)start, moved ? 0 : 1);
        drain();
        sc::destroy_module(fast_heap, m);
        vTaskDelay(1);
    }
    // The catalog's float modules and the engine's module doing the same (ModuleDef::engine): the pairs the report compares
    int n = 0;
    const mod::ModuleDef *const *all = mod::catalog(&n);
    for (int i = 0; i < n; i++)
        if (!mod::is_engine_module(all[i]) && all[i]->engine) Serial.printf("[MBENCH] equiv %s %s\n", all[i]->name, all[i]->engine);
    // What a conversion costs at a module's edge, a 32-frame block: q15 -> float, float -> q15 (rounded, clamped)
    static float f[kFrames];
    static sc::q15 q[kFrames];
    const uint32_t to_f = time_med([] { for (int i = 0; i < kFrames; i++) f[i] = static_cast<float>(g_q_saw[i]) * (1.f / 32768.f); g_sink = f[5]; });
    const uint32_t to_q = time_med([] {
        for (int i = 0; i < kFrames; i++) {
            const float s = g_sig[i] * 32767.f;
            q[i] = static_cast<sc::q15>(s > 32767.f ? 32767.f : (s < -32768.f ? -32768.f : s));
        }
        g_sink = q[5];
    });
    Serial.printf("[MBENCH] glue q15-to-float %u\n", (unsigned)to_f);
    Serial.printf("[MBENCH] glue float-to-q15 %u\n", (unsigned)to_q);
}
#endif

void bench_all() {
    for (int i = 0; i < kFrames; i++) {
        g_zero[i] = 0.f;
        g_one[i] = 1.f;
        g_sig[i] = 0.5f * std::sin(6.2831853f * static_cast<float>(i) / kFrames);
        g_trig[i] = i == 0 ? 1.f : 0.f;
    }
    Serial.printf("[MBENCH] begin cpu %u MHz rate %d block %d budget %u free-internal %u free-psram %u\n", (unsigned)ESP.getCpuFreqMHz(),
                  synth_sample_rate(), kFrames, (unsigned)(static_cast<uint64_t>(ESP.getCpuFreqMHz()) * 1000000u * kFrames / synth_sample_rate()),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#ifdef SYNTHLAB_BENCH_RM
    // The engine's modules ("RM ...", through modular/engine_adapter), alone: linked in, all their code in internal RAM leaves too little
    // of it for SynthLab's own engine to start (its 64 KB fast heap), so this build measures them and nothing else (hwv1_bench_rm);
    // module_bench.py merges its log with hwv1_bench's.
    for (int i = 0; i < kFrames; i++) {
        g_q_zero[i] = 0;
        g_q_saw[i] = static_cast<sc::q15>((i * 2 - kFrames) * (16384 / kFrames));         // (half scale)
        g_q_pitch[i] = sc::pitch_to_cv(60 * 256);
        g_q_gate[i] = sc::kUnity;
        g_q_vel[i] = static_cast<sc::q15>(100 * 32767 / 127);
        g_q_modc[i] = 4096;
        g_q_modr[i] = static_cast<sc::q15>(i * (2048 / kFrames));
        g_q_moda[i] = static_cast<sc::q15>(g_q_saw[i] / 2);
    }
    static bool registered = false;                         // they join the catalog once: the families the Ritual Machine registers,
    if (!registered) {                                      // as SynthLab Studio does (its main.cpp)
        sc::Registry &reg = g_rm_reg;
        sc::register_builtin_modules(reg);
        sc::register_sampler_modules(reg);
        sc::register_synth_modules(reg);
        sc::register_fx_modules(reg);
        sc::register_fx2_modules(reg);
        sc::register_strings_modules(reg);
        sc::register_para_modules(reg);
        sc::register_dx7_module(reg);
        sc::register_motion_module(reg);
        sc::register_osc_engines(reg);
        sc::register_mi_osc(reg);
        Serial.printf("[MBENCH] engine modules %d\n", mod::register_engine_modules(reg));
        registered = true;
    }
    int n = 0;
    const mod::ModuleDef *const *all = mod::catalog(&n);
    for (int i = 0; i < n; i++) if (mod::is_engine_module(all[i])) bench_module(*all[i]);
    bench_native();
#else
    int n = 0;
    const mod::ModuleDef *const *all = mod::catalog(&n);
    for (int i = 0; i < n; i++) bench_module(*all[i]);
    bench_ftypes();
    bench_glue();
    bench_engine();
#endif
    Serial.printf("[MBENCH] done\n");
}

void bench_task(void *) {
    vTaskDelay(pdMS_TO_TICKS(6000));
    for (;;) {
        bench_all();
        while (Serial.read() != 'b') vTaskDelay(pdMS_TO_TICKS(100));
    }
}
}  // namespace

void bench_start() {
    xTaskCreatePinnedToCore(bench_task, "mbench", 8192, nullptr, 1, nullptr, 1);   // core 1, the audio core, under the audio task
}
#endif  // SYNTHLAB_BENCH
