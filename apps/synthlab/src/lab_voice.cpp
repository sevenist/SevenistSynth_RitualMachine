// SynthLab's two engine modules (lab_voice.h): LabVoice plays the selected synth's Voice on each voice; LabFx runs its Fx once on the sum.
#include "lab_voice.h"
#include <cmath>
#include <cstring>
#include "engine/dsp/config.h"
#include "engine/dsp/q.h"

namespace lab {
namespace {

size_t voice_bytes() {                                  // the largest Voice of the list: one buffer holds any of them
    size_t n = 16;
    for (int i = 0; i < kSynthCount; i++)
        if (kSynths[i]->size > n) n = kSynths[i]->size;
    return n;
}
size_t fx_bytes() {                                     // the largest Fx (0: no synth has one)
    size_t n = 0;
    for (int i = 0; i < kSynthCount; i++)
        if (kSynths[i]->fx_size > n) n = kSynths[i]->fx_size;
    return n;
}

// What both modules share: the synth that plays, its values (set_blob), and the object built for it in the module's own memory.
struct Host {
    const Synth *synth = nullptr;
    void *mem = nullptr;
    float p[kMaxParams] = {};
    void values(const void *data, size_t bytes) {
        LabValues b;
        if (bytes < 4 || bytes > sizeof b) return;
        std::memcpy(&b, data, bytes);
        for (int i = 0; i < b.count && b.first + i < kMaxParams && 4 + 4 * static_cast<size_t>(i + 1) <= bytes; i++) p[b.first + i] = b.v[i];
    }
    void defaults() { for (int k = 0; k < kMaxParams; k++) p[k] = k < synth->n_params ? synth->params[k].def : 0.f; }
};

inline sc::q15 to_q15(float x) {
    const float s = x * 32767.f;                        // (NaN fails both tests: it comes out as 0 rather than a full-scale click)
    return s > 32767.f ? 32767 : (s < -32767.f ? -32767 : (s == s ? static_cast<sc::q15>(s) : 0));
}

class LabVoice : public sc::Module {
public:
    const sc::ModuleInfo &info() const override {
        static const sc::ModuleInfo i = {"LabVoice", sc::Scope::Voice, 0, 1, 1, false, {}, {"out"}, {{"synth", 0, 0, kMaxSynths - 1}}};
        return i;
    }
    bool init(sc::Memory &mem) override {
        heap_ = mem.fast;
        h_.mem = heap_->alloc(voice_bytes());           // (16-byte aligned)
        if (!h_.mem) return false;
        select(0);
        return true;
    }
    ~LabVoice() override {
        if (h_.synth) h_.synth->destroy(h_.mem);
        if (heap_) heap_->free(h_.mem);
    }
    void set_param(int idx, int32_t v) override {
        if (idx == LV_SYNTH && v >= 0 && v < kSynthCount && kSynths[v] != h_.synth) select(static_cast<int>(v));
    }
    void set_blob(const void *data, size_t bytes) override { h_.values(data, bytes); }
    SC_HOT void process(const sc::ProcessCtx &ctx, const sc::Ports &p) override {
        const sc::VoiceState *vs = ctx.voice;
        const int n = ctx.frames;
        sc::q15 *out = p.out[0];
        if (!vs || !h_.synth) { for (int i = 0; i < n; i++) out[i] = 0; return; }
        const Synth &s = *h_.synth;
        Note note;
        note.hz = 440.f * exp2f((static_cast<float>(vs->pitch) * (1.f / 256.f) - 69.f) * (1.f / 12.f));
        note.velocity = static_cast<float>(vs->velocity) * (1.f / 32767.f);
        note.gate = vs->gate;
        note.sr = static_cast<float>(sc::kSampleRate);
        if (vs->started) { s.start(h_.mem, note, h_.p); sounding_ = true; held_ = true; }
        if (held_ && !vs->gate) { held_ = false; s.release(h_.mem, note, h_.p); }
        if (!sounding_) { for (int i = 0; i < n; i++) out[i] = 0; return; }
        float buf[sc::kBlock];
        for (int i = 0; i < n; i++) buf[i] = 0.f;
        sounding_ = s.render(h_.mem, buf, n, note, h_.p);
        for (int i = 0; i < n; i++) out[i] = to_q15(buf[i]);
    }

private:
    void select(int i) {
        if (h_.synth) h_.synth->destroy(h_.mem);
        h_.synth = kSynths[i];
        h_.synth->construct(h_.mem);
        h_.defaults();
        sounding_ = held_ = false;
    }
    sc::Heap *heap_ = nullptr;
    Host h_;
    bool sounding_ = false, held_ = false;
};

class LabFx : public sc::Module {
public:
    const sc::ModuleInfo &info() const override {
        static const sc::ModuleInfo i = {"LabFx", sc::Scope::Global, 2, 2, 1, true, {"L", "R"}, {"L", "R"}, {{"synth", 0, 0, kMaxSynths - 1}}};
        return i;
    }
    bool init(sc::Memory &mem) override {
        heap_ = mem.fast;                               // (a full fast heap spills into the bulk one: synth.cpp)
        const size_t bytes = fx_bytes();
        if (bytes) {
            h_.mem = heap_->alloc(bytes);
            if (!h_.mem) return false;
        }
        select(0);
        return true;
    }
    ~LabFx() override {
        if (h_.synth && h_.synth->fx_destroy) h_.synth->fx_destroy(h_.mem);
        if (heap_ && h_.mem) heap_->free(h_.mem);
    }
    void set_param(int idx, int32_t v) override {
        if (idx == LV_SYNTH && v >= 0 && v < kSynthCount && kSynths[v] != h_.synth) select(static_cast<int>(v));
    }
    void set_blob(const void *data, size_t bytes) override { h_.values(data, bytes); }
    SC_HOT void process(const sc::ProcessCtx &ctx, const sc::Ports &p) override {
        const int n = ctx.frames;
        if (!h_.synth || !h_.synth->fx_render) {        // no effect: the sum goes through
            for (int i = 0; i < n; i++) { p.out[0][i] = p.in[0][i]; p.out[1][i] = p.in[1][i]; }
            return;
        }
        float buf[sc::kBlock];
        for (int i = 0; i < n; i++) buf[i] = static_cast<float>(p.in[0][i] + p.in[1][i]) * (0.5f / 32768.f);   // (the voices are mono)
        h_.synth->fx_render(h_.mem, buf, n, static_cast<float>(sc::kSampleRate), h_.p);
        for (int i = 0; i < n; i++) p.out[0][i] = p.out[1][i] = to_q15(buf[i]);
    }

private:
    void select(int i) {
        if (h_.synth && h_.synth->fx_destroy) h_.synth->fx_destroy(h_.mem);
        h_.synth = kSynths[i];
        if (h_.synth->fx_construct) h_.synth->fx_construct(h_.mem);     // (zeroes its memory on the audio thread: once, at a change of synth)
        h_.defaults();
    }
    sc::Heap *heap_ = nullptr;
    Host h_;
};

}  // namespace

void register_lab_modules(sc::Registry &reg) {
    reg.add(kTypeLabVoice, sc::module_type_of<LabVoice>());
    reg.add(kTypeLabFx, sc::module_type_of<LabFx>());
}

}  // namespace lab
