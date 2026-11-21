// Dev only: the live performance stream for tools/perf_view (see perf_stream_esp32.h; the packet format is in tools/perf_view/README.md).
//
// The audio task (core 1) adds every render to one of two accumulators: its cycles (the timeline), the engine's per-module rows, the
// module costs of the slowest render, and with ENGINE_PROFILE_PMU what the core's two performance counters counted. The sender task
// (core 0, lowest priority) asks for the accumulator 20 times a second; the audio task hands it over at the end of its next render and
// goes on in the other one, so neither side waits for the other. A packet is a text line, "@PF " + base64 + "\n": the boot log and the
// command replies stay readable around it, and a line cut by another print fails its checksum and is dropped by the viewer.
#if defined(ARDUINO_ARCH_ESP32) && defined(DEV_PERF_STREAM)
#ifndef ENGINE_PROFILE
#error "DEV_PERF_STREAM needs ENGINE_PROFILE (the engine's per-module profile)"
#endif
#include <Arduino.h>
#include <atomic>
#include <string.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include "platform/esp32/perf_stream_esp32.h"
#include "platform/engine/engine_synth.h"
#include "engine/modules/fx_modules.h"
#include "engine/modules/mi_osc.h"
#include "engine/modules/osc_engines.h"
#include "engine/dsp/config.h"
#include "boards/esp32/board_esp32.h"
#ifdef ENGINE_PROFILE_PMU
#include <xtensa_perfmon_access.h>
#include <xtensa/xt_perf_consts.h>
#endif

namespace {
constexpr int kMaxNames = 64;          // module type names seen since the start (a name keeps its id)
constexpr int kMaxRows = 40;           // rows of one engine profile take (Engine::kProfMax)
constexpr int kTimeline = 128;         // renders per packet kept one by one (20 packets a second: ~35 at 44.1 kHz / 64 frames)
constexpr int kSec = 20;               // g_sec_prof
constexpr uint8_t kVersion = 1;

// What the two counters count (perf pmu MODE). The viewer knows the same table by index.
struct PmuMode { const char *name; uint16_t sel0, mask0, sel1, mask1; };
#ifdef ENGINE_PROFILE_PMU
const PmuMode kPmu[] = {
    {"off", 0, 0, 0, 0},
    {"stalls", XTPERF_CNT_I_STALL, XTPERF_MASK_I_STALL_ALL, XTPERF_CNT_D_STALL, XTPERF_MASK_D_STALL_ALL},              // waiting on code / on data
    {"cache", XTPERF_CNT_I_STALL, XTPERF_MASK_I_STALL_CACHE_MISS, XTPERF_CNT_D_STALL, XTPERF_MASK_D_STALL_CACHE_MISS}, // flash / PSRAM cache misses only
    {"insn", XTPERF_CNT_INSN, XTPERF_MASK_INSN_ALL, XTPERF_CNT_BUBBLES, XTPERF_MASK_BUBBLES_ALL},                      // instructions, pipeline bubbles
};
#else
const PmuMode kPmu[] = {{"off", 0, 0, 0, 0}};
#endif
constexpr int kPmuModes = sizeof kPmu / sizeof kPmu[0];

struct Acc {
    uint32_t renders, blocks, sum, max, over, ev[2];
    uint16_t ntl;
    uint32_t tl[kTimeline];
    uint32_t cyc[kMaxNames], calls[kMaxNames], ev0[kMaxNames], ev1[kMaxNames], worst[kMaxNames];
    uint32_t osc[sc::OSCX_ENGINES], mi[sc::MI_MODELS], sec[kSec];
};
Acc g_acc[2];                          // internal RAM: the audio task writes into it every render
int g_cur = 0;                         // [AUDIO] the one being filled
std::atomic<bool> g_want{false};       // the sender asks for the accumulator ...
std::atomic<bool> g_ready{false};      // ... the audio task has handed g_acc[g_ready_idx] over
int g_ready_idx = 0;

const char *g_names[kMaxNames];        // written by the audio task only, published by g_names_n
std::atomic<int> g_names_n{0};
const char *g_row_name[kMaxRows];      // [AUDIO] the engine's rows keep their order: row -> id without a search
int8_t g_row_id[kMaxRows];

std::atomic<bool> g_on{true};
std::atomic<int> g_rate{20};           // packets a second
std::atomic<int> g_pmu_req{kPmuModes > 1 ? 1 : 0};
int g_pmu_cur = 0;                     // [AUDIO] the mode the counters are set to (they are per core: only this task may set them)
std::atomic<int> g_pmu_shown{0};       // the mode of the data being sent

uint32_t g_budget = 0;                 // cycles a render may take (frames / sample rate at the CPU clock)
uint32_t g_t0, g_e0, g_e1;             // [AUDIO] at the start of the render

inline uint32_t pmu_read(int i) {
#ifdef ENGINE_PROFILE_PMU
    return xtensa_perfmon_value(i);    // (once a render: the call into flash does not matter here; the engine reads them inline)
#else
    (void)i;
    return 0;
#endif
}

void pmu_apply(int m) {
#ifdef ENGINE_PROFILE_PMU
    xtensa_perfmon_stop();
    if (m > 0) {
        xtensa_perfmon_init(0, kPmu[m].sel0, kPmu[m].mask0, 0, -1);     // (as ESP-IDF's xtensa_perfmon_exec: task level)
        xtensa_perfmon_init(1, kPmu[m].sel1, kPmu[m].mask1, 0, -1);
        xtensa_perfmon_start();
    }
#else
    (void)m;
#endif
}

int name_id(int row, const char *name) {
    if (row < kMaxRows && g_row_name[row] == name) return g_row_id[row];
    int n = g_names_n.load(std::memory_order_relaxed), id = -1;
    for (int i = 0; i < n; i++) if (g_names[i] == name) { id = i; break; }
    if (id < 0) {
        if (n >= kMaxNames) return -1;
        g_names[n] = name;
        id = n;
        g_names_n.store(n + 1, std::memory_order_release);
    }
    if (row < kMaxRows) { g_row_name[row] = name; g_row_id[row] = static_cast<int8_t>(id); }
    return id;
}

// ---- the sender's side ----
uint8_t g_pkt[3072];
char g_line[4200];
int g_len = 0;
uint32_t g_drops = 0;                  // packets not sent: the port's buffer was full (nobody reading, or too slow)
uint16_t g_seq = 0;

void put8(uint32_t v) { if (g_len < (int)sizeof g_pkt) g_pkt[g_len++] = static_cast<uint8_t>(v); }
void put16(uint32_t v) { put8(v); put8(v >> 8); }
void put32(uint32_t v) { put16(v); put16(v >> 16); }
void put_str(const char *s) {
    const size_t n = s ? strnlen(s, 40) : 0;
    put8(n);
    for (size_t i = 0; i < n; i++) put8(s[i]);
}

void send_packet() {
    {   // a checksum over the packet (Fletcher-16): the viewer drops a line that another print cut into
        uint32_t a = 0, b = 0;
        for (int i = 0; i < g_len; i++) { a = (a + g_pkt[i]) % 255; b = (b + a) % 255; }
        put16(b << 8 | a);
    }
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    g_line[o++] = '@'; g_line[o++] = 'P'; g_line[o++] = 'F'; g_line[o++] = ' ';
    for (int i = 0; i < g_len; i += 3) {
        const uint32_t v = static_cast<uint32_t>(g_pkt[i]) << 16 | (i + 1 < g_len ? g_pkt[i + 1] << 8 : 0) | (i + 2 < g_len ? g_pkt[i + 2] : 0);
        g_line[o++] = kB64[v >> 18 & 63];
        g_line[o++] = kB64[v >> 12 & 63];
        g_line[o++] = i + 1 < g_len ? kB64[v >> 6 & 63] : '=';
        g_line[o++] = i + 2 < g_len ? kB64[v & 63] : '=';
    }
    g_line[o++] = '\n';
    g_len = 0;
    if (Serial.availableForWrite() >= o) Serial.write(reinterpret_cast<const uint8_t *>(g_line), o);   // one write: whole, or not at all
    else g_drops++;
}

// The names: the module types (kind 0) as they appear, the oscillator engines (1), the MI models (2), the section counters (3).
void send_names() {
    static const char *const osc[sc::OSCX_ENGINES] = {"karp", "modal", "fm2", "fold", "ssaw", "vowel", "add", "dust", "str", "gendy",
                                                      "rossl", "ca", "pulsar", "tonew", "fof", "terra", "scan", "wind", "foley"};
    static const char *const sec[kSec] = {"reverb in", "reverb mod", "reverb left", "reverb right", "reverb taps", "reverb mix", nullptr, nullptr,
                                          "delay time", "delay reads", "delay lp+write", "delay mix", "spectral analysis", "spectral resynthesis",
                                          "spectral stft+frame", nullptr, "pads grid waits", "pads samplers", "pads filter+mix", "pads block end"};
    const int n = g_names_n.load(std::memory_order_acquire);
    for (int kind = 0; kind < 4; kind++) {
        const int count = kind == 0 ? n : kind == 1 ? sc::OSCX_ENGINES : kind == 2 ? sc::MI_MODELS : kSec;
        for (int first = 0; first < count; first += 32) {
            const int k = count - first < 32 ? count - first : 32;
            put8('N'); put8(kVersion); put8(kind); put8(first); put8(k);
            for (int i = first; i < first + k; i++)
                put_str(kind == 0 ? g_names[i] : kind == 1 ? osc[i] : kind == 2 ? sc::mi_model_name(i) : sec[i]);
            send_packet();
        }
    }
}

void send_stats(const Acc &a, uint32_t t_ms) {
    size_t fu = 0, fc = 0, fh = 0, bu = 0, sp = 0;
    engine_synth_heap_stats(&fu, &fc, &fh, &bu, &sp);
    put8('S'); put8(kVersion); put16(g_seq++); put32(t_ms);
    put32(g_budget); put16(ENGINE_BLOCK); put32(static_cast<uint32_t>(engine_synth_sample_rate())); put32(ESP.getCpuFreqMHz());
    put8(g_pmu_shown.load(std::memory_order_relaxed));
    put32(a.renders); put32(a.blocks); put32(a.sum); put32(a.max); put32(a.over); put32(a.ev[0]); put32(a.ev[1]);
    put32(board_audio_stalls()); put32(engine_synth_build_count()); put32(g_drops);
    put32(fu); put32(fc); put32(fh); put32(bu); put32(sp);
    put32(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)); put32(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    put32(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    put16(a.ntl);
    for (int i = 0; i < a.ntl; i++) put32(a.tl[i]);
    const int n = g_names_n.load(std::memory_order_acquire);
    int nm = 0;
    for (int i = 0; i < n; i++) nm += a.calls[i] != 0;
    put8(nm);
    for (int i = 0; i < n; i++)
        if (a.calls[i]) { put8(i); put32(a.calls[i]); put32(a.cyc[i]); put32(a.ev0[i]); put32(a.ev1[i]); put32(a.worst[i]); }
    int ns = 0;
    for (uint32_t v : a.osc) ns += v != 0;
    for (uint32_t v : a.mi) ns += v != 0;
    for (uint32_t v : a.sec) ns += v != 0;
    put8(ns);
    for (int i = 0; i < sc::OSCX_ENGINES; i++) if (a.osc[i]) { put8(1); put8(i); put32(a.osc[i]); }
    for (int i = 0; i < sc::MI_MODELS; i++) if (a.mi[i]) { put8(2); put8(i); put32(a.mi[i]); }
    for (int i = 0; i < kSec; i++) if (a.sec[i]) { put8(3); put8(i); put32(a.sec[i]); }
    send_packet();
}

void sender_main(void *) {
    int names_sent = -1;
    uint32_t names_ms = 0;
    for (;;) {
        const int rate = g_rate.load(std::memory_order_relaxed);
        vTaskDelay(pdMS_TO_TICKS(1000 / (rate > 0 ? rate : 20)));
        const bool on = g_on.load(std::memory_order_relaxed);      // (off: the accumulators are still taken, so their sums never overflow)
        const uint32_t now = millis();
        if (on && (g_names_n.load(std::memory_order_acquire) != names_sent || now - names_ms > 2000)) {   // again every 2 s: a viewer that connects late
            names_sent = g_names_n.load(std::memory_order_acquire);
            names_ms = now;
            send_names();
        }
        if (!g_ready.load(std::memory_order_acquire)) {
            g_want.store(true, std::memory_order_release);
            for (int i = 0; i < 50 && !g_ready.load(std::memory_order_acquire); i++) vTaskDelay(1);
            if (!g_ready.load(std::memory_order_acquire)) continue;                 // no render meanwhile (the audio task is not running)
        }
        if (on) send_stats(g_acc[g_ready_idx], now);
        g_ready.store(false, std::memory_order_release);
    }
}
}  // namespace

void perf_render_begin(void) {
    const int want = g_pmu_req.load(std::memory_order_relaxed);
    if (want != g_pmu_cur) { pmu_apply(want); g_pmu_cur = want; }
    g_e0 = pmu_read(0);
    g_e1 = pmu_read(1);
    g_t0 = esp_cpu_get_cycle_count();
}

void perf_render_end(int frames) {
    const uint32_t dt = esp_cpu_get_cycle_count() - g_t0;
    const uint32_t d0 = pmu_read(0) - g_e0, d1 = pmu_read(1) - g_e1;
    if (!g_budget) g_budget = static_cast<uint32_t>(static_cast<uint64_t>(ESP.getCpuFreqMHz()) * 1000000u * frames / engine_synth_sample_rate());
    static engine_prof_row_t rows[kMaxRows];
    uint32_t blocks = 0;
    const int n = engine_synth_profile_take(rows, kMaxRows, &blocks);
    Acc &a = g_acc[g_cur];
    const bool worst = dt > a.max || !a.renders;
    a.renders++; a.blocks += blocks; a.sum += dt; a.ev[0] += d0; a.ev[1] += d1;
    if (dt > g_budget) a.over++;
    if (a.ntl < kTimeline) a.tl[a.ntl++] = dt;
    if (worst) { a.max = dt; memset(a.worst, 0, sizeof a.worst); }
    for (int i = 0; i < n; i++) {
        if (!rows[i].calls) continue;
        const int id = name_id(i, rows[i].name);
        if (id < 0) continue;
        a.cyc[id] += rows[i].cycles; a.calls[id] += rows[i].calls; a.ev0[id] += rows[i].ev0; a.ev1[id] += rows[i].ev1;
        if (worst) a.worst[id] = rows[i].cycles;
    }
    if (g_want.load(std::memory_order_acquire) && !g_ready.load(std::memory_order_relaxed)) {   // the sender asks: hand this one over
        for (int i = 0; i < sc::OSCX_ENGINES; i++) { a.osc[i] = sc::g_osc_prof[i]; sc::g_osc_prof[i] = 0; }
        for (int i = 0; i < sc::MI_MODELS; i++) { a.mi[i] = sc::g_mi_prof[i]; sc::g_mi_prof[i] = 0; }
        for (int i = 0; i < kSec; i++) { a.sec[i] = g_sec_prof[i]; g_sec_prof[i] = 0; }
        g_pmu_shown.store(g_pmu_cur, std::memory_order_relaxed);
        g_ready_idx = g_cur;
        g_cur ^= 1;
        memset(&g_acc[g_cur], 0, sizeof(Acc));
        g_want.store(false, std::memory_order_relaxed);
        g_ready.store(true, std::memory_order_release);
    }
}

void perf_stream_init(void) {
    xTaskCreatePinnedToCore(sender_main, "perf", 4096, nullptr, 1, nullptr, 0);
    Serial.printf("[PERF] stream on, %d packets a second, counters %s (perf off | on | rate N | pmu MODE)\n", g_rate.load(), kPmu[g_pmu_req.load()].name);
}

void perf_stream_cmd(const char *arg) {
    if (arg && *arg) {
        if (!strcmp(arg, "on")) g_on.store(true);
        else if (!strcmp(arg, "off")) g_on.store(false);
        else if (!strncmp(arg, "rate ", 5)) { const int r = atoi(arg + 5); g_rate.store(r < 1 ? 1 : r > 50 ? 50 : r); }
        else if (!strncmp(arg, "pmu ", 4)) {
            int m = -1;
            for (int i = 0; i < kPmuModes; i++) if (!strcmp(arg + 4, kPmu[i].name)) m = i;
            if (m < 0) { Serial.printf("[PERF] no counter mode '%s'%s\n", arg + 4, kPmuModes > 1 ? " (off stalls cache insn)" : " (built without ENGINE_PROFILE_PMU)"); return; }
            g_pmu_req.store(m);
        } else { Serial.println("[PERF] perf [on | off | rate N | pmu MODE]"); return; }
    }
    Serial.printf("[PERF] %s, %d packets a second, counters %s, %u packets dropped\n", g_on.load() ? "on" : "off", g_rate.load(), kPmu[g_pmu_req.load()].name, (unsigned)g_drops);
}
#endif
