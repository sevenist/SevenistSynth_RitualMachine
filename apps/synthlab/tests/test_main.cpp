// SynthLab's host tests (tools/build_tests.ps1). Every synth of the list, with no change here when one is added:
//   voice     its Voice alone: a note sounds, no NaN, stays in range, falls silent after the key is let go (render() returns false)
//   extremes  every value at its min and at its max (the others at their default): no NaN, no blow-up
//   fx        a synth's effect (lab.h, Fx) the same way, and its tail dies away
//   engine    through the app's engine (synth.cpp): selected, played, freed
//   app       the screen and the encoders (test_app.cpp): build/screen_*.png
//   null      Kick Lite against the Kick, preset by preset (test_null.cpp): the null depth, the renders in build/wav/null/
//   bench     the host's time per sample of each synth (to compare synths and versions), and a warning when denormals slow it
//   wav       a short phrase of each synth in build/wav/<name>.wav, to listen to (and the host's time per voice, to compare synths)
// Run with a text to run only the tests whose name, or whose synth's name, contains it:  build/tests.exe FM2
// Values after it replace the defaults of the synth whose name is that text, for every test (to render or time a variant):
//   build/tests.exe Kick Click=0 "AMP ENV/Decay=10" #24=10    a name: the first value with that name; SECTION/name: the one in that
//                                            section; #n: the n-th row of its list (titles count). Kept in the rows' ranges.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include <xmmintrin.h>      // the bench: denormals flushed or not (the tests run on an x86 PC)
#include "lab.h"
#include "lab_dsp.h"
#include "synth.h"
#include "tests/check.h"

int failures = 0, checks = 0;
const char *filter = nullptr;
namespace {
struct Override { const char *name; int index; float value; };
constexpr int kMaxOverrides = 64;
Override overrides[kMaxOverrides];
int n_overrides = 0;
}
// The row a command-line name means: "Decay" the first value with that name, "AMP ENV/Decay" the one in that section; -1: none.
int find_row(const lab::Synth &s, const char *name) {
    const char *slash = std::strchr(name, '/');
    const char *section = nullptr;                                  // the title above the row being looked at
    for (int i = 0; i < s.n_params; i++) {
        if (s.params[i].curve == lab::Head) { section = s.params[i].name; continue; }
        if (!slash) { if (std::strcmp(s.params[i].name, name) == 0) return i; continue; }
        const size_t n = static_cast<size_t>(slash - name);
        if (section && std::strlen(section) == n && std::strncmp(section, name, n) == 0 && std::strcmp(s.params[i].name, slash + 1) == 0) return i;
    }
    return -1;
}
// The command line's values, for the synth named exactly like the filter (std::strcmp: "Kick", not "Ki"), kept in their rows' ranges.
void apply_overrides(const lab::Synth &s, float *p) {
    if (!filter || std::strcmp(s.name, filter) != 0) return;
    for (int o = 0; o < n_overrides; o++) {
        const int k = overrides[o].index >= 0 ? overrides[o].index : find_row(s, overrides[o].name);
        if (k < 0 || k >= s.n_params || s.params[k].curve == lab::Head) {
            std::printf("  '%s': no such value in %s\n", overrides[o].name, s.name);
            continue;
        }
        const float v = overrides[o].value;
        p[k] = v < s.params[k].min ? s.params[k].min : (v > s.params[k].max ? s.params[k].max : v);
    }
}
void test_app();                    // test_app.cpp
void test_null();                   // test_null.cpp

namespace {

bool wanted(const char *test, const char *synth) {
    return !filter || std::strstr(test, filter) || (synth && std::strstr(synth, filter));
}

constexpr float kSr = 48000.f;
constexpr int kBlockFrames = 32;

// A Voice of a synth in its own memory: what LabVoice does, without the engine.
struct TestVoice {
    const lab::Synth &s;
    float p[lab::kMaxParams] = {};
    alignas(16) unsigned char mem[16384];
    lab::Note note{};
    explicit TestVoice(const lab::Synth &synth) : s(synth) {
        for (int i = 0; i < s.n_params; i++) p[i] = s.params[i].def;
        apply_overrides(s, p);
        s.construct(mem);
    }
    ~TestVoice() { s.destroy(mem); }
    void start(float hz, float vel) {
        note = lab::Note{hz, vel, true, kSr};
        s.start(mem, note, p);
    }
    void release() { note.gate = false; s.release(mem, note, p); }
    // One block; false once the voice said it is silent. Peak and NaN tracked.
    bool block(float *out, float &peak, bool &nan) {
        for (int i = 0; i < kBlockFrames; i++) out[i] = 0.f;
        const bool on = s.render(mem, out, kBlockFrames, note, p);
        for (int i = 0; i < kBlockFrames; i++) {
            if (!std::isfinite(out[i])) nan = true;
            else if (std::fabs(out[i]) > peak) peak = std::fabs(out[i]);
        }
        return on;
    }
};

// A synth's Fx in its own memory (aligned), with its default values: what LabFx does, without the engine.
struct TestFx {
    const lab::Synth &s;
    float p[lab::kMaxParams] = {};
    void *mem = nullptr;
    explicit TestFx(const lab::Synth &synth) : s(synth) {
        for (int i = 0; i < s.n_params; i++) p[i] = s.params[i].def;
        apply_overrides(s, p);
        mem = ::operator new(s.fx_size, std::align_val_t(16));
        s.fx_construct(mem);
    }
    ~TestFx() { s.fx_destroy(mem); ::operator delete(mem, std::align_val_t(16)); }
    void block(float *io) { s.fx_render(mem, io, kBlockFrames, kSr, p); }
};

// The Fx with each value at its min and at its max: 0.5 s of a decaying noise burst in, then 12 s of silence: no NaN, no blow-up,
// and the tail dies away.
void test_fx(const lab::Synth &s) {
    for (int k = -1; k < s.n_params; k++) {
        if (k >= 0 && s.params[k].curve == lab::Head) continue;
        for (int end = 0; end < (k < 0 ? 1 : 2); end++) {
            TestFx fx(s);
            if (k >= 0) fx.p[k] = end ? s.params[k].max : s.params[k].min;
            lab::Noise nz;
            float env = 0.5f, peak = 0.f, tail = 0.f;
            bool nan = false;
            const int blocks = static_cast<int>(12.5f * kSr) / kBlockFrames, in_blocks = static_cast<int>(0.5f * kSr) / kBlockFrames;
            for (int b = 0; b < blocks; b++) {
                float io[kBlockFrames];
                for (int i = 0; i < kBlockFrames; i++) { io[i] = b < in_blocks ? nz.next() * env : 0.f; env *= 0.9999f; }
                fx.block(io);
                for (int i = 0; i < kBlockFrames; i++) {
                    if (!std::isfinite(io[i])) nan = true;
                    else if (std::fabs(io[i]) > peak) peak = std::fabs(io[i]);
                    if (b >= blocks - 100 && std::fabs(io[i]) > tail) tail = std::fabs(io[i]);
                }
            }
            const char *what = k < 0 ? "its defaults" : s.params[k].name;
            CHECK(!nan, "%s fx: NaN with %s at its %s", s.name, what, end ? "max" : "min");
            CHECK(peak < 4.f, "%s fx: peak %.1f with %s at its %s", s.name, peak, what, end ? "max" : "min");
            CHECK(tail < 1e-3f, "%s fx: still %.4f after 12 s with %s at its %s: it should die away", s.name, tail, what, end ? "max" : "min");
        }
    }
}

void test_voice(const lab::Synth &s) {
    CHECK(s.size <= sizeof(TestVoice::mem), "%s: its Voice is %zu bytes, the test holds %zu", s.name, s.size, sizeof(TestVoice::mem));
    if (s.size > sizeof(TestVoice::mem)) return;
    TestVoice v(s);
    float buf[kBlockFrames], peak = 0.f;
    bool nan = false;
    v.start(220.f, 0.8f);
    const int held = static_cast<int>(kSr * 0.5f) / kBlockFrames;                      // 0.5 s held
    for (int b = 0; b < held; b++) v.block(buf, peak, nan);
    CHECK(!nan, "%s: NaN or infinity while held", s.name);
    CHECK(peak > 0.01f, "%s: silent while held (peak %.4f)", s.name, peak);
    CHECK(peak < 1.5f, "%s: peak %.2f while held: a voice should stay around 0.5", s.name, peak);
    v.release();
    const int limit = static_cast<int>(kSr * 30.f) / kBlockFrames;                     // the engine's tail: 30 s
    int b = 0;
    float tail_peak = 0.f;
    while (b < limit && v.block(buf, tail_peak, nan)) b++;
    CHECK(b < limit, "%s: still sounding 30 s after release: render() should return false once silent", s.name);
    CHECK(!nan, "%s: NaN or infinity after release", s.name);
}

void test_extremes(const lab::Synth &s) {
    if (s.size > sizeof(TestVoice::mem)) return;
    for (int k = 0; k < s.n_params; k++) {
        if (s.params[k].curve == lab::Head) continue;                                  // (a section's title)
        for (int end = 0; end < 2; end++) {
            TestVoice v(s);
            v.p[k] = end ? s.params[k].max : s.params[k].min;
            float buf[kBlockFrames], peak = 0.f;
            bool nan = false;
            for (const float hz : {27.5f, 4186.f}) {                                   // the piano's lowest and highest notes
                v.start(hz, 1.f);
                for (int b = 0; b < 4000; b++) v.block(buf, peak, nan);                // 2.7 s
                v.release();
                for (int b = 0; b < 1000; b++) v.block(buf, peak, nan);
            }
            CHECK(!nan, "%s: NaN with %s at its %s (%g)", s.name, s.params[k].name, end ? "max" : "min", v.p[k]);
            CHECK(peak < 4.f, "%s: peak %.1f with %s at its %s (%g)", s.name, peak, s.params[k].name, end ? "max" : "min", v.p[k]);
        }
    }
}

void test_engine() {
    static std::vector<unsigned char> fast(1u << 20), bulk(1u << 20);
    CHECK(synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), "the engine did not start");
    int16_t buf[2 * 256];
    for (int s = 0; s < lab::kSynthCount; s++) {
        if (filter && !wanted("engine", lab::kSynths[s]->name)) continue;
        synth_select(s);
        CHECK(synth_selected() == s, "select %d", s);
        synth_note_on(57);
        int peak = 0;
        for (int b = 0; b < 100; b++) {                                                 // 0.5 s
            synth_render(buf, 256);
            for (int i = 0; i < 512; i++) peak = std::abs(buf[i]) > peak ? std::abs(buf[i]) : peak;
            synth_update();
        }
        CHECK(peak > 300, "%s through the engine: peak %d", lab::kSynths[s]->name, peak);
        CHECK(synth_active_voices() == 1, "%s: %d voices for one key", lab::kSynths[s]->name, synth_active_voices());
        synth_note_off(57);
        for (int b = 0; b < 6000 && synth_active_voices() > 0; b++) { synth_render(buf, 256); synth_update(); }   // up to 32 s
        CHECK(synth_active_voices() == 0, "%s: the voice is not freed after release", lab::kSynths[s]->name);
    }
    synth_select(0);                                                                    // values: clamped to the range
    synth_set_value(1, 1e9f);
    CHECK(synth_value(1) == lab::kSynths[0]->params[1].max, "a value above its max is clamped (%g)", synth_value(1));
    synth_shutdown();
}

// Denormals flushed to zero by the PC's FPU (or not): the bench times each synth both ways. A synth much slower without the flush has
// states that decay into the tiniest floats (lab_dsp.h, flush).
void set_ftz(bool on) {
    const unsigned csr = _mm_getcsr();
    _mm_setcsr(on ? (csr | 0x8040u) : (csr & ~0x8040u));     // FTZ | DAZ
}

double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// One sounding voice: a note every 0.5 s, held 0.1 s, 30 s of sound; only the blocks where the voice sounds are counted (the engine stops
// calling a voice once it is silent). The best of 5 runs, in ns per sample.
double time_voice(const lab::Synth &s) {
    double best = 1e9;
    for (int run = 0; run < 5; run++) {
        TestVoice v(s);
        float buf[kBlockFrames], peak = 0.f;
        bool nan = false, on = false;
        long active = 0;
        const int every = static_cast<int>(0.5f * kSr) / kBlockFrames, held = static_cast<int>(0.1f * kSr) / kBlockFrames;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < static_cast<int>(30.f * kSr) / kBlockFrames; b++) {
            if (b % every == 0) { v.start(110.f, 0.8f); on = true; }
            if (b % every == held && v.note.gate) v.release();
            if (!on) continue;
            on = v.block(buf, peak, nan);
            active++;
        }
        const double ns = seconds_since(t0) * 1e9 / (static_cast<double>(active) * kBlockFrames);
        if (ns < best) best = ns;
    }
    return best;
}

// The synth's Fx, once for all the voices: 1 s of noise then 1 s of silence (its tail), 20 s. The best of 5 runs, in ns per sample.
double time_fx(const lab::Synth &s) {
    double best = 1e9;
    for (int run = 0; run < 5; run++) {
        TestFx fx(s);
        lab::Noise nz;
        const int blocks = static_cast<int>(20.f * kSr) / kBlockFrames, second = static_cast<int>(kSr) / kBlockFrames;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; b++) {
            float io[kBlockFrames];
            for (int i = 0; i < kBlockFrames; i++) io[i] = (b / second) % 2 == 0 ? nz.next() * 0.3f : 0.f;
            fx.block(io);
        }
        const double ns = seconds_since(t0) * 1e9 / (static_cast<double>(blocks) * kBlockFrames);
        if (ns < best) best = ns;
    }
    return best;
}

// The host's time per sample of each synth (and its Fx), to compare synths and versions of a synth on this PC (the board's cycles are
// measured on the board), and a warning when denormals slow it down.
void bench(const lab::Synth &s) {
    if (s.size > sizeof(TestVoice::mem)) return;
    const double ns = time_voice(s);
    set_ftz(true);
    const double ns_ftz = time_voice(s), fx_ftz = s.fx_render ? time_fx(s) : 0.0;
    set_ftz(false);
    std::printf("  %-12s %6.1f ns per sample of a sounding voice (host)\n", s.name, ns);
    if (ns > 1.25 * ns_ftz)                                                     // (below 25 %: the PC's noise)
        std::printf("  %-12s WARNING: %.0f %% slower than with denormals flushed (%.1f ns): flush the states that decay (lab_dsp.h)\n", "",
                    100.0 * (ns / ns_ftz - 1.0), ns_ftz);
    if (!s.fx_render) return;
    const double fx = time_fx(s);
    std::printf("  %-12s %6.1f ns per sample of its fx, once for every voice (host)\n", "", fx);
    if (fx > 1.25 * fx_ftz)
        std::printf("  %-12s WARNING: its fx is %.0f %% slower than with denormals flushed (%.1f ns)\n", "", 100.0 * (fx / fx_ftz - 1.0), fx_ftz);
}

void put32(std::FILE *f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
void put16(std::FILE *f, uint16_t v) { std::fwrite(&v, 2, 1, f); }

// A mono synth's phrase on one voice, as the engine plays it: each note restarts it (the last one choked). Hits on the beat at 120 BPM,
// some of them before the last has ended, a soft one, two other notes, then the tail.
void mono_phrase(const lab::Synth &s, std::vector<float> &mix, double &rendered, double &spent) {
    struct Ev { float at, note, vel; };
    const Ev hits[] = {{0.f, 48, 0.9f}, {0.5f, 48, 0.9f}, {1.f, 48, 0.4f}, {1.5f, 48, 0.9f}, {1.75f, 48, 0.7f}, {2.f, 43, 0.9f}, {2.5f, 55, 0.9f}};
    TestVoice v(s);
    float buf[kBlockFrames], peak = 0.f;
    bool nan = false, on = false;
    size_t next = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int frame = 0; frame + kBlockFrames <= static_cast<int>(mix.size()); frame += kBlockFrames) {
        if (next < sizeof hits / sizeof hits[0] && frame >= static_cast<int>(hits[next].at * kSr)) {
            v.start(lab::mtof(hits[next].note), hits[next].vel);
            next++;
            on = true;
        }
        if (!on) continue;
        if (v.note.gate && frame >= static_cast<int>((hits[next - 1].at + 0.1f) * kSr)) v.release();
        on = v.block(buf, peak, nan);
        for (int i = 0; i < kBlockFrames; i++) mix[static_cast<size_t>(frame + i)] += buf[i];
        rendered += kBlockFrames / kSr;
    }
    spent += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(!nan, "%s: NaN in the phrase", s.name);
}

// A phrase (a held chord, then four short notes; a mono synth: hits on one voice) in a mono 16-bit WAV, and the host's time per voice
// per second of sound.
void write_wav(const lab::Synth &s) {
    std::vector<int16_t> pcm;
    double seconds_rendered = 0, seconds_spent = 0;
    struct Ev { float at, len, note; };
    const Ev phrase[] = {{0.f, 1.2f, 48}, {0.f, 1.2f, 55}, {0.f, 1.2f, 63}, {1.6f, 0.2f, 60}, {1.9f, 0.2f, 63}, {2.2f, 0.2f, 67}, {2.5f, 0.6f, 72}};
    const float total = 4.5f;
    std::vector<float> mix(static_cast<size_t>(total * kSr), 0.f);
    if (s.opt.mono) mono_phrase(s, mix, seconds_rendered, seconds_spent);
    else for (const Ev &e : phrase) {
        TestVoice v(s);
        v.start(lab::mtof(e.note), 0.8f);
        const auto t0 = std::chrono::steady_clock::now();
        int frame = static_cast<int>(e.at * kSr);
        const int off = frame + static_cast<int>(e.len * kSr);
        float buf[kBlockFrames], peak = 0.f;
        bool nan = false, on = true;
        while (on && frame + kBlockFrames <= static_cast<int>(mix.size())) {
            if (frame >= off && v.note.gate) v.release();
            on = v.block(buf, peak, nan);
            for (int i = 0; i < kBlockFrames; i++) mix[static_cast<size_t>(frame + i)] += buf[i];
            frame += kBlockFrames;
            seconds_rendered += kBlockFrames / kSr;
        }
        seconds_spent += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    for (float &x : mix) x *= 0.5f;                                                // the engine's voice level: half scale
    if (s.fx_render) {                                                              // the Fx on the sum, as LabFx runs it
        TestFx fx(s);
        for (size_t at = 0; at + kBlockFrames <= mix.size(); at += kBlockFrames) fx.block(&mix[at]);
    }
    for (const float x : mix) {
        const float y = x * 32767.f;
        pcm.push_back(static_cast<int16_t>(y > 32767.f ? 32767.f : (y < -32767.f ? -32767.f : y)));
    }
    char path[160];
    std::snprintf(path, sizeof path, "build/wav/%s.wav", s.name);
    for (char *c = path + 10; *c; c++) if (*c == ' ' || *c == '/') *c = '_';
    std::FILE *f = std::fopen(path, "wb");
    CHECK(f != nullptr, "could not write %s (does build/wav exist?)", path);
    if (!f) return;
    const uint32_t bytes = static_cast<uint32_t>(pcm.size() * 2);
    std::fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, static_cast<uint32_t>(kSr)); put32(f, static_cast<uint32_t>(kSr) * 2); put16(f, 2); put16(f, 16);
    std::fwrite("data", 1, 4, f); put32(f, bytes);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    std::printf("  %-12s %s   host: %.1f us per voice per ms of sound\n", s.name, path,
                seconds_rendered > 0 ? 1000.0 * seconds_spent / seconds_rendered : 0.0);
}

}  // namespace

int main(int argc, char **argv) {
    if (argc > 1) filter = argv[1];
    if (argc - 2 > kMaxOverrides) { std::printf("at most %d values (%d given)\n", kMaxOverrides, argc - 2); return 2; }
    for (int a = 2; a < argc; a++) {
        char *eq = std::strchr(argv[a], '=');
        if (!eq) { std::printf("'%s': give values as name=value or #index=value\n", argv[a]); return 2; }
        *eq = 0;
        Override &o = overrides[n_overrides++];
        o.index = argv[a][0] == '#' ? std::atoi(argv[a] + 1) : -1;
        o.name = argv[a];
        o.value = static_cast<float>(std::atof(eq + 1));
        std::printf("%s = %g\n", argv[a], o.value);
    }
    std::printf("SynthLab: %d synth(s)\n", lab::kSynthCount);
    for (int s = 0; s < lab::kSynthCount; s++) {
        const lab::Synth &syn = *lab::kSynths[s];
        if (wanted("voice", syn.name)) test_voice(syn);
        if (wanted("extremes", syn.name)) test_extremes(syn);
        if (syn.fx_render && wanted("fx", syn.name)) test_fx(syn);
    }
    if (wanted("engine", nullptr) || filter) test_engine();
    if (wanted("app", nullptr)) test_app();
    if (wanted("null", nullptr)) test_null();
    if (wanted("bench", nullptr) || filter) {
        std::printf("bench:\n");
        for (int s = 0; s < lab::kSynthCount; s++)
            if (wanted("bench", lab::kSynths[s]->name)) bench(*lab::kSynths[s]);
    }
    if (wanted("wav", nullptr) || filter) {
        std::printf("wav:\n");
        for (int s = 0; s < lab::kSynthCount; s++)
            if (wanted("wav", lab::kSynths[s]->name)) write_wav(*lab::kSynths[s]);
    }
    std::printf("%s: %d checks, %d failed\n", failures ? "FAILED" : "passed", checks, failures);
    return failures ? 1 : 0;
}
