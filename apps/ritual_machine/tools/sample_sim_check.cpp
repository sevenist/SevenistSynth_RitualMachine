// Dev check of the simulator's sample library (sample_sim.cpp) without the window: lists the library's root folder and its first
// sub-folder, takes samples into the catalog until it is full, converts a pending .wav / .mp3, then takes an entry out and checks
// that the next file gets its place. Run from apps/ritual_machine (the card is the folder sdcard/):
//   gcc -c -O1 -DPLATFORM_SIM -Isrc framework/boards/desktop/storage_sim.c -o build/storage_sim_check.o
//   g++ -std=c++17 -O1 -DPLATFORM_SIM -Isrc -I../../framework -I../../lib/minimp3 tools/sample_sim_check.cpp src/platform/sim/sample_sim.cpp build/storage_sim_check.o \
//       ../../framework/engine/*/*.cpp ../../framework/engine/mi/*/*.cpp ../../framework/engine/mi/*/*/*/*.cpp src/platform/engine/*.cpp build/tobj/*.o -pthread -o build/sample_sim_check.exe
//   (build/tobj: the C core objects tools/build_engine_tests.ps1 makes)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "platform/engine/engine_synth.h"
extern "C" {
int sim_samples_init(void);
void sim_samples_shutdown(void);
int sim_samples_prepare(int);
int sim_samples_use(const char *name);
int sim_samples_find(const char *name);
void sim_samples_hide(int index);
int sim_samples_dir_open(const char *dir);
int sim_samples_dir_read(int first, audio_dir_entry_t *out, int max);
}

static int list(const char *dir, std::string *first_dir) {
    const int total = sim_samples_dir_open(dir);
    std::printf("folder \"%s\": %d entries\n", dir, total);
    for (int k = 0; k < total; k++) {
        audio_dir_entry_t e;
        if (sim_samples_dir_read(k, &e, 1) != 1) break;
        if (k < 8 || k == total - 1) std::printf("  %3d %s%s\n", k, e.name, e.dir ? "/" : "");
        if (e.dir && first_dir && first_dir->empty()) *first_dir = e.name;
    }
    return total;
}

int main() {
    static unsigned char fast[6 << 20], bulk[6 << 20];
    if (engine_synth_init(fast, sizeof fast, bulk, sizeof bulk) != 0) return 1;
    sim_samples_init();
    std::string sub;
    const int total = list("", &sub);
    if (!sub.empty()) list(sub.c_str(), nullptr);
    std::printf("no such folder: %d, no such sample: %d\n", sim_samples_dir_open("no/such/folder"), sim_samples_use("no such sample"));
    sim_samples_dir_open("");
    int used = 0, full_at = -1, first_index = -1;
    for (int k = 0; k < total && full_at < 0; k++) {
        audio_dir_entry_t e;
        if (sim_samples_dir_read(k, &e, 1) != 1 || e.dir) continue;
        const int i = sim_samples_use(e.name);
        if (i == -2) { full_at = k; break; }
        if (i < 0) { std::printf("  %s: not a sample (%d)\n", e.name, i); continue; }
        if (first_index < 0) first_index = i;
        audio_sample_info_t in;
        engine_synth_sample_info(i, &in);
        if (in.pending) { const int ok = sim_samples_prepare(i); engine_synth_sample_info(i, &in); std::printf("  converted %s: %d\n", in.name, ok); }
        if (used < 4) std::printf("  [%d] %-28s frames %u rate %u root %u loop %d peaks %u %u %u %u\n", i, in.name, in.frames, in.rate, in.root, in.loop_mode, in.peaks[0], in.peaks[16], in.peaks[32], in.peaks[48]);
        used++;
    }
    std::printf("%d samples in the catalog (%d entries)%s\n", used, engine_synth_sample_count(), full_at >= 0 ? ", then full (-2)" : "");
    if (full_at >= 0 && first_index >= 0) {                                 // an entry leaves (its sample was never loaded): the next file takes its place
        audio_dir_entry_t e;
        sim_samples_dir_read(full_at, &e, 1);
        sim_samples_hide(first_index);
        const int i = sim_samples_use(e.name);
        std::printf("entry %d taken out, \"%s\" gets index %d (find: %d)\n", first_index, e.name, i, sim_samples_find(e.name));
    }
    sim_samples_shutdown();
    engine_synth_shutdown();
    return 0;
}
