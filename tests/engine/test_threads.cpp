#include <atomic>
#include <chrono>
#include <thread>
#include "rig.h"
#include "engine/modules/sampler_modules.h"

using namespace sc;
using namespace tst;

namespace {
// voice chain + global FX; the variant changes ids, parameters and structure so rebuilds really add / remove / keep modules
void build_variant(DspRig &rig, GraphDesc &g, int variant) {
    rig.add(g, 1, T_NOTE_IN);
    NodeDesc *o = rig.add(g, 2, T_OSC);
    o->param[OSC_WAVE] = variant % 2 ? WAVE_SAW_ : WAVE_PULSE_;
    NodeDesc *f = rig.add(g, 3, T_FILTER_V);
    f->param[FLT_CUTOFF] = (60 + 10 * (variant % 4)) * 256;
    rig.add(g, 4, T_ENV);
    rig.add(g, 5, T_VCA_V)->param[VCA_LEVEL] = 0;
    rig.add(g, 6, T_VOICE_OUT)->param[VO_TAIL_MS] = 300;
    rig.add(g, 7, T_BUS_IN);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(1, 1, 4, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
    g.connect(3, 0, 5, Dst::In, 0);
    g.connect(4, 0, 5, Dst::Param, VCA_LEVEL);
    g.connect(5, 0, 6, Dst::In, 0);
    int last = 7;
    if (variant % 3 != 0) { rig.add(g, 8, T_CHORUS); g.connect(7, 0, 8, Dst::In, 0); g.connect(7, 1, 8, Dst::In, 1); last = 8; }
    if (variant % 2 == 0) {
        rig.add(g, 9, T_DELAY)->param[DLY_TIME] = (100 + 20 * variant) * 16;
        g.connect(last, 0, 9, Dst::In, 0);
        g.connect(last, 1 % (last == 7 ? 2 : 2), 9, Dst::In, 1);
        last = 9;
    }
    rig.add(g, 10, T_REVERB)->param[RVB_MIX] = 8000;
    g.connect(last, 0, 10, Dst::In, 0);
    g.connect(last, 1, 10, Dst::In, 1);
    rig.add(g, 11, T_MASTER_OUT);
    g.connect(10, 0, 11, Dst::In, 0);
    g.connect(10, 1, 11, Dst::In, 1);
}
}  // namespace

TEST(audio_thread_survives_rebuilds_notes_and_parameter_traffic) {
    DspRig rig(6);
    GraphDesc g;
    build_variant(rig, g, 0);
    CHECK(rig.eng.load(g) == Err::Ok);

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> blocks{0};
    std::atomic<int> peak{0};
    std::thread audio([&] {
        q15 l[kBlock], r[kBlock];
        while (!stop.load()) {
            rig.eng.render(l, r);
            int p = 0;
            for (int i = 0; i < kBlock; i++) { int a = l[i] < 0 ? -l[i] : l[i]; if (a > p) p = a; }
            if (p > peak.load()) peak.store(p);
            blocks.fetch_add(1);
            if ((blocks.load() & 7) == 0) std::this_thread::sleep_for(std::chrono::microseconds(100));   // roughly real time on a fast PC
        }
    });

    uint32_t rng = 12345;
    auto next = [&] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
    int loads = 0, posted = 0, dropped = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(2500)) {
        switch (next() % 6) {
        case 0: case 1: {
            GraphDesc v;
            build_variant(rig, v, static_cast<int>(next() % 12));
            if (rig.eng.load(v) == Err::Ok) loads++;
            break;
        }
        case 2: (rig.eng.note_on(40 + static_cast<int>(next() % 40), 60 + static_cast<int>(next() % 60)) ? posted : dropped)++; break;
        case 3: (rig.eng.note_off(40 + static_cast<int>(next() % 40)) ? posted : dropped)++; break;
        case 4: (rig.eng.set_param(3, FLT_CUTOFF, static_cast<int32_t>(next() % (110 * 256))) ? posted : dropped)++; break;
        default: rig.eng.gc(); break;
        }
        if (next() % 4 == 0) std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    stop.store(true);
    audio.join();
    rig.eng.gc();
    std::printf("    %llu blocks rendered while: %d rebuilds, %d commands posted (%d dropped), peak %d\n",
                static_cast<unsigned long long>(blocks.load()), loads, posted, dropped, peak.load());
    CHECK(blocks.load() > 1500);
    CHECK(loads > 50);
    CHECK(peak.load() <= 32767);
    CHECK(rig.heap.check());
    rig.eng.shutdown();
    CHECK_EQ(rig.heap.used(), 0);
}

TEST(command_ring_is_ordered_and_reports_overflow) {
    CommandRing<8> ring;
    Command c;
    for (int i = 0; i < 8; i++) { c.value = i; CHECK(ring.push(c)); }
    c.value = 99;
    CHECK(!ring.push(c));                                              // full: rejected, not overwritten
    for (int i = 0; i < 8; i++) { Command o; CHECK(ring.pop(o)); CHECK_EQ(o.value, i); }
    Command o;
    CHECK(!ring.pop(o));
    c.value = 5;
    CHECK(ring.push(c));
    CHECK(ring.pop(o));
    CHECK_EQ(o.value, 5);
}
