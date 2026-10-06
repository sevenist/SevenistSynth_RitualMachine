// Dev: ESP32Synth as a second engine on the board, to compare CPU cost with ours and to try its ideas (per-block linear envelopes, silent-voice skip,
// GCC vector types on the S3, control-rate modulation). Source: github.com/danilogcrf2-oss/ESP32Synth, MIT, v2.4.7 at commit 75ddb19 (2026-09-16),
// copied unchanged into esp32synth/ except the #ifndef around its voice / stream limits in ESP32Synth_Config.hpp. platformio.ini keeps that folder
// out of the source build (build_src_filter): the library is compiled here, in one translation unit, only with -DALT_ESP32SYNTH.
//
// What it is: an oscillator bank. A voice = one oscillator (sine, triangle, saw, pulse, noise; also wavetables, RAM samples, SD streams, custom waves)
// x a linear ADSR x a volume, mixed into buses with user FX callbacks. No filter, no built-in effects. So it compares with our osc -> Env -> Vca rack,
// not with the startup patch. It runs in "pull" mode (beginCustom without a callback: no task, no I2S); our audio task calls generateSamples().
// Its SD streaming, recording and wavetables are not wired: the card belongs to our own driver (sd_card_spi.cpp).
// Threads: note on / off and settings come from core 0 (UI loop, serial), rendering runs on core 1, as in the library's own examples (no lock).
//
// Memory: the library object (voices, buses, the stream ring) is allocated at "alt on" and freed at "alt off", in internal RAM when it fits:
// our engine's fast heap is sized from the free internal RAM at boot, so a static object would shrink it and could push the startup patch into PSRAM
// (slower), which would bias the comparison. Only the library's 8 KB sine table (a global of the library) stays allocated.
// Note names: the library defines lower-case macros for notes (c0, d1, ...): avoid such names in this file.
#if defined(ARDUINO_ARCH_ESP32) && defined(ALT_ESP32SYNTH)

// Limits of the library (its defaults: 80 voices, 20 wavetables, 20 samples, 4 streams of 2048 samples).
#ifndef MAX_VOICES
#define MAX_VOICES 48
#endif
#define MAX_WAVETABLES 1
#define MAX_SAMPLES 1
#define MAX_STREAMS 1
#define STREAM_BUF_SAMPLES 256                          // streams are not wired (a power of two, as the library requires)
#include "esp32synth/ESP32Synth.cpp"                    // the whole library (it sets "#pragma GCC optimize O3" for the rest of this file)

#include <atomic>
#include <math.h>
#include <new>
#include "platform/esp32/alt_esp32synth.h"

namespace {
ESP32Synth *g_syn = nullptr;                             // allocated between "alt on" and "alt off"
bool g_in_psram = false;
int g_rate = 48000;
std::atomic<bool> g_on{false};

// Voice allocation is ours: the library addresses voices by index. Free voice first (oldest), else steal (released first, then the oldest).
int8_t g_note[MAX_VOICES];                              // midi note of the voice, -1 = released
uint32_t g_age[MAX_VOICES];
uint32_t g_clock = 0;
int g_poly = 16;                                         // voices the allocator may use (alt poly N)

// Settings applied to every voice (alt wave / env / vol / vib); kept across alt off / on
WaveType g_wave = WAVE_SAW;
uint16_t g_a = 5, g_d = 200, g_r = 300;                  // ms
uint8_t g_s = 180;                                       // 0..255
uint16_t g_vol = 64;                                     // 0..255 per voice; the mix clips past about 255 in total (4 voices at 64)
uint8_t g_crush = 0;
uint32_t g_vib_rate = 0, g_vib_depth = 0;                // centi-Hz

// Measurement: CPU cycles spent in generateSamples, reported per 32 frames (the engine block, as in [PROF])
uint32_t g_cycles = 0, g_frames = 0, g_worst = 0;

uint32_t centi_hz(int note) { return (uint32_t)(44000.0 * pow(2.0, (note - 69) / 12.0) + 0.5); }

void apply_voice(int v) {
    g_syn->setWave(v, g_wave);
    g_syn->setEnv(v, g_a, g_d, g_s, g_r);
    g_syn->setVibrato(v, g_vib_rate, g_vib_depth);
}

void apply_all() {
    for (int v = 0; v < MAX_VOICES; v++) apply_voice(v);
    g_syn->setMasterBitcrush(g_crush);
}

void release_all() {
    for (int v = 0; v < MAX_VOICES; v++) { g_syn->noteOff(v); g_note[v] = -1; }
}

const struct { const char *name; WaveType w; } kWaves[] = {
    {"sine", WAVE_SINE}, {"tri", WAVE_TRIANGLE}, {"saw", WAVE_SAW}, {"pulse", WAVE_PULSE}, {"noise", WAVE_NOISE},
};

const char *wave_name(WaveType w) {
    for (const auto &k : kWaves) if (k.w == w) return k.name;
    return "?";
}

int sounding() {
    int n = 0;
    for (int v = 0; v < MAX_VOICES; v++) n += g_syn->isVoiceActive(v) ? 1 : 0;
    return n;
}

void print_status() {
    Serial.printf("[ALT] %s, wave %s, env a %u d %u s %u r %u ms, vol %u, poly %d of %d, vib %u/%u cHz, crush %u, %d sounding\n",
                  g_on.load() ? "ON" : "off", wave_name(g_wave), g_a, g_d, g_s, g_r, g_vol, g_poly, MAX_VOICES, (unsigned)g_vib_rate, (unsigned)g_vib_depth,
                  g_crush, sounding());
}
}  // namespace

bool alt_init(int sample_rate) {
    g_rate = sample_rate;
    for (auto &n : g_note) n = -1;
    Serial.printf("[ALT] ESP32Synth available (serial: alt on): %d voices max, %u bytes allocated at alt on\n", MAX_VOICES, (unsigned)sizeof(ESP32Synth));
    return true;
}

bool alt_active(void) { return g_on.load(std::memory_order_relaxed); }

void alt_set_active(bool on) {
    if (on && !g_syn) {
        const size_t free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        void *mem = heap_caps_malloc(sizeof(ESP32Synth), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        g_in_psram = mem == nullptr;
        if (!mem) mem = heap_caps_malloc(sizeof(ESP32Synth), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem) { Serial.println("[ALT] out of memory"); return; }
        g_syn = new (mem) ESP32Synth();
        if (!g_syn->beginCustom((uint32_t)g_rate, nullptr)) {            // pull mode: no task, no I2S
            g_syn->~ESP32Synth();
            heap_caps_free(mem);
            g_syn = nullptr;
            Serial.println("[ALT] beginCustom failed");
            return;
        }
        for (auto &n : g_note) n = -1;
        apply_all();
        Serial.printf("[ALT] %u bytes in %s, internal RAM free before: %u\n", (unsigned)sizeof(ESP32Synth),
                      g_in_psram ? "PSRAM (internal RAM was full: slower, the numbers are not comparable)" : "internal RAM", (unsigned)free_before);
    }
    if (!g_syn) return;
    release_all();
    g_cycles = g_frames = g_worst = 0;
    g_on.store(on, std::memory_order_relaxed);
    print_status();
    if (!on) {                                           // free it; the audio task may still be inside alt_render for this block (64 frames = 1.5 ms)
        vTaskDelay(pdMS_TO_TICKS(20));
        g_syn->end();
        g_syn->~ESP32Synth();
        heap_caps_free(g_syn);
        g_syn = nullptr;
        Serial.println("[ALT] freed");
    }
}

void alt_note_on(int note) {
    if (!g_syn || note < 0 || note > 127) return;
    int pick = -1;
    for (int v = 0; v < g_poly; v++) if (g_note[v] == note) { pick = v; break; }                      // the same key again: retrigger its voice
    if (pick < 0) {
        uint32_t best = UINT32_MAX;
        for (int v = 0; v < g_poly; v++)                                                              // the oldest silent voice
            if (!g_syn->isVoiceActive(v) && g_age[v] <= best) { best = g_age[v]; pick = v; }
    }
    if (pick < 0) {
        uint64_t best = UINT64_MAX;
        for (int v = 0; v < g_poly; v++) {                                                            // steal: released voices first, then the oldest
            const uint64_t key = (g_note[v] < 0 ? 0 : (1ull << 32)) + g_age[v];
            if (key <= best) { best = key; pick = v; }
        }
    }
    g_note[pick] = (int8_t)note;
    g_age[pick] = ++g_clock;
    g_syn->noteOn(pick, centi_hz(note), g_vol);
}

void alt_note_off(int note) {
    if (!g_syn) return;
    for (int v = 0; v < MAX_VOICES; v++)
        if (g_note[v] == note) { g_syn->noteOff(v); g_note[v] = -1; }
}

void alt_render(int16_t *stereo, int frames) {
    static int16_t mono[256];
    if (frames > 256) frames = 256;
    if (!g_syn) { memset(stereo, 0, frames * 2 * sizeof(int16_t)); return; }
    const uint32_t t_start = ESP.getCycleCount();
    g_syn->generateSamples(mono, frames);
    const uint32_t dc = ESP.getCycleCount() - t_start;
    g_cycles += dc;
    g_frames += frames;
    const uint32_t per32 = dc * 32 / frames;
    if (per32 > g_worst) g_worst = per32;
    for (int i = 0; i < frames; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
}

void alt_report(void) {
    if (!alt_active() || !g_syn || !g_frames) return;
    const uint32_t budget = (uint32_t)(ESP.getCpuFreqMHz() * 1000000ull * 32 / g_rate);
    const uint32_t avg = (uint32_t)((uint64_t)g_cycles * 32 / g_frames);
    const int n = sounding();
    Serial.printf("[ALT] cycles per 32 frames %u avg, %u worst (budget %u, %u %%), %d voices sounding, %u per voice, wave %s%s\n", (unsigned)avg, (unsigned)g_worst,
                  (unsigned)budget, (unsigned)(avg * 100ull / budget), n, n ? (unsigned)(avg / n) : 0u, wave_name(g_wave), g_in_psram ? ", object in PSRAM" : "");
    g_cycles = g_frames = g_worst = 0;
}

void alt_command(const char *args) {
    char what[8] = {};
    int a = -1, b = -1, c = -1, d = -1;
    sscanf(args ? args : "", "%7s %d %d %d %d", what, &a, &b, &c, &d);
    if (!strcmp(what, "wave")) {
        char name[8] = {};
        sscanf(args, "%*s %7s", name);
        bool found = false;
        for (const auto &k : kWaves) if (!strcmp(k.name, name)) { g_wave = k.w; found = true; }
        if (!found) { Serial.println("[ALT] wave sine|tri|saw|pulse|noise"); return; }
    } else if (!strcmp(what, "env")) {                  // env a d s r: ms, ms, 0..255, ms
        if (d < 0) { Serial.println("[ALT] env <attack ms> <decay ms> <sustain 0-255> <release ms>"); return; }
        g_a = (uint16_t)a; g_d = (uint16_t)b; g_s = (uint8_t)(c > 255 ? 255 : c); g_r = (uint16_t)d;
    } else if (!strcmp(what, "vol")) {
        g_vol = (uint16_t)(a < 0 ? 0 : (a > 255 ? 255 : a));
    } else if (!strcmp(what, "poly")) {
        g_poly = a < 1 ? 1 : (a > MAX_VOICES ? MAX_VOICES : a);
    } else if (!strcmp(what, "vib")) {                  // vib <rate cHz> <depth cHz>: 500 300 = 5 Hz, +-3 Hz
        g_vib_rate = a < 0 ? 0 : (uint32_t)a;
        g_vib_depth = b < 0 ? 0 : (uint32_t)b;
    } else if (!strcmp(what, "crush")) {                // crush <bits>: 0 = off
        g_crush = (uint8_t)(a < 0 ? 0 : a);
    } else if (strcmp(what, "status") != 0) {
        Serial.println("[ALT] alt on|off|status | wave <name> | env a d s r | vol 0-255 | poly N | vib rate depth | crush bits");
        return;
    }
    if (!g_syn) {                                        // remembered, applied at alt on
        Serial.println("[ALT] off (settings kept for alt on)");
        return;
    }
    if (!strcmp(what, "poly")) release_all();
    apply_all();                                         // takes effect at the next note (and the envelope of the sounding ones)
    print_status();
}
#endif
