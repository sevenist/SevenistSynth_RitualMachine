// The modular catalog's modules as engine modules (framework/modular/engine_modules.h): each one, played by the engine through its
// wrapper, against the same float module played by the modular runner (patch.h). The same chain both ways: a saw (Osc) into the
// module's first input when that input is audio (its default silence), the module, the voice's output; one voice, note 60, velocity
// 100. The engine's side goes through q15 at each module's ports and through its voice output and master: the difference left after
// the best gain (the voice output's pan law) is the rounding.
#include <cstring>
#include "modular/engine_modules.h"
#include "modular/modules.h"
#include "modular/patch.h"
#include "rig.h"

using namespace sc;
using namespace tst;

namespace {

bool io_module(const mod::ModuleDef *d) {
    return !std::strcmp(d->name, "Note") || !std::strcmp(d->name, "VoiceOut") || !std::strcmp(d->name, "Voices") || !std::strcmp(d->name, "Output");
}

// The module's output through the runner, `blocks` blocks from the note's start.
std::vector<double> float_run(const mod::ModuleDef *d, bool feed, int blocks) {
    static mod::Patch p;
    p.clear();
    const int out = p.add(mod::find_module("VoiceOut"), mod::kVoiceArea, 400, 0)->id;
    const int m = p.add(d, mod::kVoiceArea, 200, 0)->id;
    p.connect(m, 0, out, 0, false, 1.f);
    if (feed) {
        const int osc = p.add(mod::find_module("Osc"), mod::kVoiceArea, 0, 0)->id;
        p.connect(osc, 0, m, 0, false, 1.f);
    }
    std::vector<unsigned char> mem(mod::Runner::bytes_for(p, mod::kVoiceArea) + 64);
    void *aligned = reinterpret_cast<void *>((reinterpret_cast<uintptr_t>(mem.data()) + 15) & ~static_cast<uintptr_t>(15));
    static mod::Runner r;
    std::vector<double> y;
    if (!r.build(p, mod::kVoiceArea, aligned, mem.size() - 16)) return y;
    mod::Ctx c;
    c.sr = static_cast<float>(kSampleRate);
    c.inv_sr = 1.f / c.sr;
    c.hz = 440.f * std::pow(2.f, (60.f - 69.f) / 12.f);
    c.velocity = static_cast<float>(100 * 32767 / 127) / 32767.f;
    c.gate = true;
    r.start(c);
    float buf[kBlock];
    for (int b = 0; b < blocks; b++) {
        for (float &x : buf) x = 0.f;
        r.process(c, nullptr, nullptr, buf, kBlock);
        for (float x : buf) y.push_back(x);
    }
    r.clear();
    return y;
}

// The same chain in the engine, through the wrapped types.
std::vector<double> engine_run(const mod::ModuleDef *d, bool feed, int blocks) {
    DspRig rig(1);
    mod::register_modular_types(rig.eng.registry());
    GraphDesc g;
    rig.add(g, 1, T_NOTE_IN);
    rig.add(g, 3, mod::modular_type_id(d->name, Scope::Voice));
    rig.add(g, 4, T_VOICE_OUT);
    rig.add(g, 5, T_BUS_IN);
    rig.add(g, 6, T_MASTER_OUT);
    g.connect(3, 0, 4, Dst::In, 0);
    g.connect(5, 0, 6, Dst::In, 0);
    g.connect(5, 1, 6, Dst::In, 1);
    if (feed) {
        rig.add(g, 2, mod::modular_type_id("Osc", Scope::Voice));
        g.connect(2, 0, 3, Dst::In, 0);
    }
    std::vector<double> y;
    if (rig.eng.load(g, 1) != Err::Ok) return y;
    rig.run(2);
    rig.eng.note_on(60, 100);
    rig.run(blocks, &y);
    for (double &x : y) x /= 32768.0;
    return y;
}

}  // namespace

TEST(every_catalog_module_plays_in_the_engine_as_its_float_original) {
    int n = 0;
    const mod::ModuleDef *const *all = mod::catalog(&n);
    int checked = 0, silent = 0;
    double worst = -1000.0;
    const char *worst_name = "";
    for (int i = 0; i < n; i++) {
        const mod::ModuleDef *d = all[i];
        if (!d->cpp || d->construct_in || io_module(d)) continue;
        CHECK(mod::modular_type_id(d->name, Scope::Voice) >= mod::kModularTypeFirst);
        // (a Pitch input is a CV in the engine, 72 semitones a unit, and octaves in the catalog: a saw into it is not the same note)
        const bool feed = d->n_in > 0 && d->in_def[0] == mod::kZero && std::strcmp(d->in_name[0], "Pitch") != 0;
        const int blocks = 60;
        const std::vector<double> ref = float_run(d, feed, blocks), got = engine_run(d, feed, blocks);
        CHECK(ref.size() == got.size() && !ref.empty());
        if (ref.size() != got.size() || ref.empty()) continue;
        double rr = 0.0, rg = 0.0, gg = 0.0;
        bool finite = true;
        for (size_t k = 0; k < ref.size(); k++) {
            rr += ref[k] * ref[k];
            rg += ref[k] * got[k];
            gg += got[k] * got[k];
            if (!std::isfinite(got[k])) finite = false;
        }
        CHECK(finite);
        if (rr < 1e-8) { silent++; CHECK(gg < 1e-6); continue; }    // (a module that sends nothing here: Macro / Const at 0)
        const double gain = rg / rr;
        double e = 0.0;
        for (size_t k = 0; k < ref.size(); k++) { const double x = got[k] - gain * ref[k]; e += x * x; }
        const double null_db = 10.0 * std::log10((e + 1e-30) / rr);
        std::printf("    %-8s null %6.1f dB (gain %.3f)\n", d->name, null_db, gain);
        CHECK(gain > 0.4 && gain < 1.6);
        CHECK(null_db < -50.0);
        if (null_db > worst) { worst = null_db; worst_name = d->name; }
        checked++;
    }
    std::printf("    %d modules nulled (worst %s %.1f dB), %d silent here\n", checked, worst_name, worst, silent);
    CHECK(checked >= 25);
}

TEST(a_catalog_module_registers_alone) {
    Registry reg;
    CHECK(mod::register_modular_type(reg, "Body"));
    const int id = mod::modular_type_id("Body", Scope::Voice);
    CHECK(reg.get(id) != nullptr && !std::strcmp(reg.get(id)->info->name, "FBody"));
    CHECK(reg.get(mod::modular_type_id("Osc", Scope::Voice)) == nullptr);          // (only what was asked for)
    CHECK(!mod::register_modular_type(reg, "Note"));                               // (the In / Out modules: the engine's own)
}
