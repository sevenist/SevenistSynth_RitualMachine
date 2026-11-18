// The Hadamard reverb (MOD_HADAMARD, "HD") as a row M module of the rack: its knobs are live values (no graph rebuild), and it plays.
#include <cmath>
#include <vector>
#include "rig.h"
#include "rack_m.h"
#include "platform/engine/engine_synth.h"
#include "core/rack.h"
#include "core/synth_params.h"

using namespace tst;

TEST(the_hadamard_reverbs_knobs_never_rebuild_the_graph) {
    rack_t r; synth_params_t p;
    synth_params_default(&p);
    rack_init(&r);
    rack_m_clear(r);
    const int k = rack_add_m(&r, MOD_HADAMARD);
    CHECK(k != RACK_NONE);
    CHECK(rack_is_fx(MOD_HADAMARD) && rack_type_shared_only(MOD_HADAMARD));
    std::vector<uint8_t> fast(6u << 20), bulk(6u << 20);
    CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
    engine_synth_build(&r, &p);
    int16_t buf[2 * 256];
    engine_synth_note_on(57);
    engine_synth_note_on(64);
    engine_synth_render(buf, 256);
    const unsigned before = engine_synth_build_count();
    double peak = 0, ss = 0;
    long n = 0;
    CHECK_EQ(rack_mparam_count(MOD_HADAMARD), 6);
    for (int i = 0; i < rack_mparam_count(MOD_HADAMARD); i++) {                 // every knob from its minimum to its maximum, as a knob turns it
        CHECK(rack_mparam_is_continuous(MOD_HADAMARD, i));
        for (int s = 0; s <= 20; s++) {
            rack_mparam_set_norm(&r.slot[k], i, static_cast<float>(s) / 20.0f);
            engine_synth_set_params(&r, &p);
            engine_synth_render(buf, 256);
            for (int j = 0; j < 512; j++) { peak = std::fmax(peak, std::fabs(static_cast<double>(buf[j]))); ss += static_cast<double>(buf[j]) * buf[j]; n++; }
        }
        rack_mparam_reset(&r.slot[k], i);
    }
    std::printf("    %u rebuilds over the six knobs (last: %s); rms %.0f, peak %.0f\n", engine_synth_build_count() - before, engine_synth_build_reason(),
                std::sqrt(ss / static_cast<double>(n)), peak);
    CHECK_EQ(engine_synth_build_count(), before);
    CHECK(std::sqrt(ss / static_cast<double>(n)) > 30.0);
    engine_synth_shutdown();
}
