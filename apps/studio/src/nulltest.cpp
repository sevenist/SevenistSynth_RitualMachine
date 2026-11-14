#include "nulltest.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
#include "studio.h"

namespace studio {

namespace {
constexpr float kSr = 48000.f;
constexpr int kBlock = 32;
constexpr int kNoteFrames = 48000, kTailFrames = 48000;

// Renders the two notes through one of the two; out gets kNotes x (held + tail) samples.
template <typename Start, typename Render>
void play(std::vector<float> &out, Start start, Render render) {
    const int notes[2] = {48, 60};
    out.assign(static_cast<size_t>(2 * (kNoteFrames + kTailFrames)), 0.f);
    size_t at = 0;
    for (int k = 0; k < 2; k++) {
        lab::Note n;
        n.hz = 440.f * std::pow(2.f, (static_cast<float>(notes[k]) - 69.f) / 12.f);
        n.velocity = 100.f / 127.f;
        n.gate = true;
        n.sr = kSr;
        start(n);
        for (int f = 0; f < kNoteFrames + kTailFrames; f += kBlock) {
            n.gate = f < kNoteFrames;
            render(out.data() + at, kBlock, n);
            at += kBlock;
        }
    }
}

float db(float x) { return x > 1e-10f ? 20.f * std::log10(x) : -200.f; }
float rms(const std::vector<float> &v) {
    double s = 0.0;
    for (float x : v) s += static_cast<double>(x) * x;
    return static_cast<float>(std::sqrt(s / static_cast<double>(v.size())));
}
}  // namespace

NullResult null_test(const lab::Synth &s, const mod::Patch &p) {
    // the synth: a fresh Voice for each note (as a freed voice is), its Fx once
    std::vector<float> a, b;
    float defs[lab::kMaxParams] = {};
    for (int i = 0; i < s.n_params; i++) defs[i] = s.params[i].def;
    void *vmem = ::operator new(s.size, std::align_val_t(16));
    void *fmem = s.fx_size ? ::operator new(s.fx_size, std::align_val_t(16)) : nullptr;
    bool held = false;
    if (fmem) s.fx_construct(fmem);
    bool built = false;
    play(a, [&](const lab::Note &n) {
             if (built) s.destroy(vmem);
             s.construct(vmem);
             built = true;
             s.start(vmem, n, defs);
             held = true;
         },
         [&](float *out, int frames, const lab::Note &n) {
             if (!n.gate && held) { s.release(vmem, n, defs); held = false; }    // (the key let go: once)
             s.render(vmem, out, frames, n, defs);
             if (fmem) s.fx_render(fmem, out, frames, n.sr, defs);
         });
    if (built) s.destroy(vmem);
    if (fmem) s.fx_destroy(fmem);
    ::operator delete(vmem, std::align_val_t(16));
    if (fmem) ::operator delete(fmem, std::align_val_t(16));

    // the patch: the runner, rebuilt for each note (a fresh voice), its FX once
    const size_t vbytes = mod::Runner::bytes_for(p, mod::kVoiceArea) + 64, fbytes = mod::Runner::bytes_for(p, mod::kFxArea) + 64;
    void *pv = ::operator new(vbytes, std::align_val_t(16)), *pf = ::operator new(fbytes, std::align_val_t(16));
    static mod::Runner rv, rf;
    rf.build(p, mod::kFxArea, pf, fbytes);
    bool fx_note = false;                               // (the FX area follows the last note, as in the Modular synth: mod::LastNote)
    play(b, [&](const lab::Note &n) {
             rv.build(p, mod::kVoiceArea, pv, vbytes);
             fx_note = true;
             mod::Ctx c;
             c.sr = n.sr; c.inv_sr = 1.f / n.sr; c.hz = n.hz; c.velocity = n.velocity; c.gate = true;
             rv.start(c);
         },
         [&](float *out, int frames, const lab::Note &n) {
             mod::Ctx c;
             c.sr = n.sr; c.inv_sr = 1.f / n.sr; c.hz = n.hz; c.velocity = n.velocity; c.gate = n.gate;
             float v[kBlock] = {};
             rv.process(c, nullptr, nullptr, v, frames);
             if (rf.empty()) { for (int i = 0; i < frames; i++) out[i] += v[i]; return; }
             mod::Ctx g;
             g.sr = n.sr; g.inv_sr = 1.f / n.sr; g.hz = n.hz; g.velocity = n.velocity; g.gate = n.gate;
             if (fx_note) { rf.start(g); fx_note = false; }
             float w[kBlock] = {};
             rf.process(g, nullptr, v, w, frames);
             for (int i = 0; i < frames; i++) out[i] += w[i];
         });
    rv.clear();
    rf.clear();
    ::operator delete(pv, std::align_val_t(16));
    ::operator delete(pf, std::align_val_t(16));

    NullResult r;
    std::vector<float> d(a.size());
    float peak = 0.f;
    for (size_t i = 0; i < a.size(); i++) { d[i] = a[i] - b[i]; peak = std::fmax(peak, std::fabs(d[i])); }
    r.synth_rms_db = db(rms(a));
    r.patch_rms_db = db(rms(b));
    r.residual_peak_db = db(peak);
    r.depth_db = r.synth_rms_db - db(rms(d));
    if (std::getenv("STUDIO_NULL_TRACE")) {             // where the difference lies: the residual and the synth, 5 ms windows of the first note
        for (int w = 0; w < 60; w++) {
            const size_t a0 = static_cast<size_t>(w) * 240, a1 = a0 + 240;
            double ss = 0.0, sd = 0.0;
            for (size_t i = a0; i < a1; i++) { ss += static_cast<double>(a[i]) * a[i]; sd += static_cast<double>(d[i]) * d[i]; }
            std::printf("  %4d ms  synth %6.1f dB  residual %6.1f dB\n", w * 5, static_cast<double>(db(static_cast<float>(std::sqrt(ss / 240.0)))),
                        static_cast<double>(db(static_cast<float>(std::sqrt(sd / 240.0)))));
        }
    }
    return r;
}

std::string library_path(const lab::Synth &s) {
    std::string id;
    for (const char *q = s.name; *q; q++) id += (*q >= 'A' && *q <= 'Z') ? static_cast<char>(*q - 'A' + 'a') : (*q == ' ' ? '_' : *q);
    return st.preset_dir + "../library/" + id + ".patch";
}

bool has_rebuild(const lab::Synth &s) {
    FILE *f = std::fopen(library_path(s).c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

}  // namespace studio
