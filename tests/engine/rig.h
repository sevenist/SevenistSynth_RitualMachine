#pragma once
// Test rig for DSP module tests: an engine with the builtin and synth modules, a heap, and measuring helpers.
#include <cmath>
#include <vector>
#include "engine/core/engine.h"
#include "engine/modules/dx7_voice.h"
#include "engine/modules/fx_modules.h"
#include "engine/modules/fx2_modules.h"
#include "engine/modules/motion_seq.h"
#include "engine/modules/osc_engines.h"
#include "engine/modules/synth_modules.h"
#include "test.h"

namespace tst {

using namespace sc;

struct DspRig {
    std::vector<uint8_t> mem, bulk_mem;
    Heap heap, bulk;
    Engine eng;
    // separate_bulk: long buffers (delay lines) go to their own heap, like PSRAM on the target
    explicit DspRig(int voices = 4, bool separate_bulk = false) : mem(1 << 21), bulk_mem(separate_bulk ? (1 << 22) : 0) {
        heap.init(mem.data(), mem.size());
        if (separate_bulk) bulk.init(bulk_mem.data(), bulk_mem.size());
        eng.init(Memory{&heap, separate_bulk ? &bulk : &heap}, voices);
        register_synth_modules(eng.registry());
        register_fx_modules(eng.registry());
        register_fx2_modules(eng.registry());
        register_dx7_module(eng.registry());
        register_motion_module(eng.registry());
        register_osc_engines(eng.registry());
    }
    ~DspRig() { eng.shutdown(); }
    NodeDesc *add(GraphDesc &g, int id, int type) { return g.add_node(eng.registry(), id, type); }
    void run(int blocks, std::vector<double> *out = nullptr) {
        q15 l[kBlock], r[kBlock];
        for (int b = 0; b < blocks; b++) {
            eng.render(l, r);
            if (out) for (int i = 0; i < kBlock; i++) out->push_back(l[i]);
        }
    }
    void run2(int blocks, std::vector<double> *l, std::vector<double> *r) {
        q15 bl[kBlock], br[kBlock];
        for (int b = 0; b < blocks; b++) {
            eng.render(bl, br);
            for (int i = 0; i < kBlock; i++) { if (l) l->push_back(bl[i]); if (r) r->push_back(br[i]); }
        }
    }
    static int blocks_for(double seconds) { return static_cast<int>(seconds * kSampleRate / kBlock) + 1; }
    double rms_over(double seconds) {
        std::vector<double> x;
        run(blocks_for(seconds), &x);
        return tst::rms(x);
    }
};

inline int32_t hz_to_pitch(double hz) { return static_cast<int32_t>(std::lround(69.0 * 256 + 12.0 * 256 * std::log2(hz / 440.0))); }
inline double db(double ratio) { return 20.0 * std::log10(ratio); }

// Power per bin of a real signal (naive DFT, N small). Returns bins 0..N/2.
inline std::vector<double> dft_power(const std::vector<double> &x) {
    const size_t N = x.size();
    std::vector<double> p(N / 2 + 1);
    for (size_t k = 0; k <= N / 2; k++) {
        double re = 0, im = 0;
        for (size_t n = 0; n < N; n++) {
            double a = 2.0 * 3.14159265358979323846 * static_cast<double>((k * n) % N) / static_cast<double>(N);
            re += x[n] * std::cos(a);
            im -= x[n] * std::sin(a);
        }
        p[k] = (re * re + im * im) / (static_cast<double>(N) * static_cast<double>(N));
    }
    return p;
}

}  // namespace tst
