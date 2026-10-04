// Dev check of the simulator's sample folder: lists the library, converts every pending .wav / .mp3 and prints what came out.
//   g++ -std=c++17 -O1 -DPLATFORM_SIM -Isrc -Ilib/minimp3 tools/sample_sim_check.cpp src/platform/sim/sample_sim.cpp src/platform/engine/engine_synth.cpp \
//       <engine objects> src/core/{rack,synth_config,synth_params,dx7,dx7_factory,fxrack}.o -pthread -o build/sample_sim_check.exe
#include <cstdio>
#include <cstdlib>
#include "platform/engine/engine_synth.h"
extern "C" { int sim_samples_init(void); void sim_samples_shutdown(void); int sim_samples_prepare(int); }
int main() {
    static unsigned char fast[6 << 20], bulk[6 << 20];
    if (engine_synth_init(fast, sizeof fast, bulk, sizeof bulk) != 0) return 1;
    const int n = sim_samples_init();
    std::printf("%d entries\n", n);
    for (int i = 0; i < n; i++) {
        audio_sample_info_t in;
        engine_synth_sample_info(i, &in);
        std::printf("  %-12s pending %d kind %d frames %u\n", in.name, in.pending, in.kind, in.frames);
        if (in.pending) {
            const int ok = sim_samples_prepare(i);
            engine_synth_sample_info(i, &in);
            std::printf("    -> converted %d: frames %u rate %u root %u peaks[0..3] %u %u %u %u\n", ok, in.frames, in.rate, in.root, in.peaks[0], in.peaks[16], in.peaks[32], in.peaks[48]);
        }
    }
    sim_samples_shutdown();
    engine_synth_shutdown();
    return 0;
}
