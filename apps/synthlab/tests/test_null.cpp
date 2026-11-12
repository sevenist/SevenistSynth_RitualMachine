// The null test of Kick Lite against the Kick (src/synths/kick_lite.h): every preset of Kick Lite rendered by both synths (the Kick with
// the same values on its rows, the rest at its defaults), through their Fx, then one minus the other. What is left is what Kick Lite's
// optimisations changed: its level under the signal is the null depth. Each preset must reach kNullMin dB. The three renders are in
// build/wav/null/ (32-bit float: a residual at -120 dB is still there to look at).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>
#include "lab.h"
#include "lab_dsp.h"
#include "synths/kick_lite.h"
#include "tests/check.h"

int find_row(const lab::Synth &s, const char *name);           // test_main.cpp

namespace {

constexpr float kSr = 48000.f;
constexpr int kBlockFrames = 32;
constexpr double kNullMin = 60.0;                               // dB: what "cannot be heard" is taken to mean here

const lab::Synth *find_synth(const char *name) {
    for (int i = 0; i < lab::kSynthCount; i++)
        if (std::strcmp(lab::kSynths[i]->name, name) == 0) return lab::kSynths[i];
    return nullptr;
}

// A phrase of hits on one voice (as the engine plays a mono synth: each hit restarts it), its Fx after it: the sound the engine makes.
// Hits on the beat at 120 BPM, a soft one, one over the last one's tail (a choke), two other notes, then the tails.
std::vector<float> render(const lab::Synth &s, const float *p) {
    struct Hit { float at, note, vel; };
    const Hit hits[] = {{0.f, 48, 0.9f}, {0.5f, 48, 0.9f}, {1.f, 48, 0.4f}, {1.5f, 48, 0.9f}, {1.75f, 48, 0.7f}, {2.f, 43, 0.9f}, {2.5f, 55, 0.9f}};
    std::vector<float> mix(static_cast<size_t>(4.5f * kSr), 0.f);
    void *voice = ::operator new(s.size, std::align_val_t(16));
    s.construct(voice);
    lab::Note note{};
    note.sr = kSr;
    size_t next = 0;
    bool on = false;
    float buf[kBlockFrames];
    for (size_t frame = 0; frame + kBlockFrames <= mix.size(); frame += kBlockFrames) {
        const float t = static_cast<float>(frame) / kSr;
        if (next < sizeof hits / sizeof hits[0] && t >= hits[next].at) {
            note.hz = lab::mtof(hits[next].note);
            note.velocity = hits[next].vel;
            note.gate = true;
            s.start(voice, note, p);
            next++;
            on = true;
        }
        if (note.gate && t >= hits[next - 1].at + 0.1f) { note.gate = false; s.release(voice, note, p); }
        if (!on) continue;
        for (float &x : buf) x = 0.f;
        on = s.render(voice, buf, kBlockFrames, note, p);
        for (int i = 0; i < kBlockFrames; i++) mix[frame + i] += buf[i] * 0.5f;      // (the engine's voice level: half scale)
    }
    s.destroy(voice);
    ::operator delete(voice, std::align_val_t(16));
    if (s.fx_render) {
        void *fx = ::operator new(s.fx_size, std::align_val_t(16));
        s.fx_construct(fx);
        for (size_t at = 0; at + kBlockFrames <= mix.size(); at += kBlockFrames) s.fx_render(fx, &mix[at], kBlockFrames, kSr, p);
        s.fx_destroy(fx);
        ::operator delete(fx, std::align_val_t(16));
    }
    return mix;
}

void write_float_wav(const char *path, const std::vector<float> &x) {
    std::FILE *f = std::fopen(path, "wb");
    if (!f) return;
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t bytes = static_cast<uint32_t>(x.size() * 4);
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(3); u16(1); u32(static_cast<uint32_t>(kSr)); u32(static_cast<uint32_t>(kSr) * 4); u16(4); u16(32);    // (format 3: float)
    std::fwrite("data", 1, 4, f); u32(bytes);
    std::fwrite(x.data(), 4, x.size(), f);
    std::fclose(f);
}

double db(double v) { return v > 0.0 ? 20.0 * std::log10(v) : -999.0; }

}  // namespace

void test_null() {
    const lab::Synth *kick = find_synth("Kick"), *lite = find_synth("Kick Lite");
    CHECK(kick && lite, "the Kick and Kick Lite are in the list");
    if (!kick || !lite) return;
    int rows[kick_lite::kValues];
    for (int k = 0; k < kick_lite::kValues; k++) {
        rows[k] = find_row(*kick, kick_lite::kKickRows[k]);
        CHECK(rows[k] >= 0, "Kick Lite's value %d is the Kick's %s: no such row", k, kick_lite::kKickRows[k]);
        if (rows[k] < 0) return;
    }
    std::printf("null test, Kick -> Kick Lite (the depth must reach %.0f dB):\n", kNullMin);
    std::printf("  %-16s %9s %9s %11s %13s\n", "preset", "rms", "peak", "null depth", "residual peak");
    for (int i = 0; i < kick_lite::kPresetCount; i++) {
        const kick_lite::Preset &pr = kick_lite::kPresets[i];
        float pk[lab::kMaxParams] = {}, pl[lab::kMaxParams] = {};
        for (int r = 0; r < kick->n_params; r++) pk[r] = kick->params[r].def;
        for (int k = 0; k < kick_lite::kValues; k++) { pk[rows[k]] = pr.v[k]; pl[k] = pr.v[k]; }
        const std::vector<float> a = render(*kick, pk), b = render(*lite, pl);
        std::vector<float> d(a.size());
        double sa = 0.0, sd = 0.0, peak = 0.0, dpeak = 0.0;
        bool nan = false;
        for (size_t n = 0; n < a.size(); n++) {
            d[n] = a[n] - b[n];                                     // (b in reverse phase, summed)
            if (!std::isfinite(d[n])) { nan = true; continue; }
            sa += static_cast<double>(a[n]) * a[n];
            sd += static_cast<double>(d[n]) * d[n];
            peak = std::fabs(a[n]) > peak ? std::fabs(a[n]) : peak;
            dpeak = std::fabs(d[n]) > dpeak ? std::fabs(d[n]) : dpeak;
        }
        const double rms = std::sqrt(sa / a.size()), drms = std::sqrt(sd / a.size());
        const double depth = drms > 0.0 ? db(rms) - db(drms) : 999.0;
        if (drms > 0.0)
            std::printf("  %-16s %6.1f dB %6.1f dB %8.1f dB %10.1f dB\n", pr.name, db(rms), db(peak), depth, db(dpeak) - db(peak));
        else
            std::printf("  %-16s %6.1f dB %6.1f dB   identical (bit for bit)\n", pr.name, db(rms), db(peak));
        CHECK(!nan, "%s: NaN in the renders", pr.name);
        CHECK(depth >= kNullMin, "%s: the null depth is %.1f dB, under %.0f dB", pr.name, depth, kNullMin);
        char path[200], slug[64];
        std::snprintf(slug, sizeof slug, "%s", pr.name);
        for (char *c = slug; *c; c++) if (*c == ' ') *c = '_';
        std::snprintf(path, sizeof path, "build/wav/null/%s_kick.wav", slug);
        write_float_wav(path, a);
        std::snprintf(path, sizeof path, "build/wav/null/%s_lite.wav", slug);
        write_float_wav(path, b);
        std::snprintf(path, sizeof path, "build/wav/null/%s_residual.wav", slug);
        write_float_wav(path, d);
    }
}
