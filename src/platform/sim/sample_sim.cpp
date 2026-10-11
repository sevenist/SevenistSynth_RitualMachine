#ifdef PLATFORM_SIM
// Desktop sample library: the folder system/samples of the simulated card (sdcard/, see storage_sim.c; build.ps1 copies samples/*.smp
// into it) plays the role of the TF card's library, with its sub-folders. Nothing is scanned at start: a file enters the catalog when it
// is asked for by name (its path below that folder), and the browser reads one folder at a time. A *.smp is read as it is; a *.wav or
// *.mp3 is imported first: decoded, mixed to mono, cooked into a .smp next to the source (so the next start skips the conversion) and
// then treated like any other file. The files live in a SimStorage, which models the latency, bandwidth and stalls of a slow card, so
// the streaming behaves like on the target.
// (wav and mp3 files are listed at once and converted when a pad or a sampler uses them.)
// A loader thread runs the sample bank (engine_synth_io_pump); on the ESP32 that is the I/O task.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "hal/hal_storage.h"
#include "platform/sim/sim_card.h"
#include "platform/sim/sim_storage.h"
#include "platform/sim/sample_convert.h"
#include "platform/engine/engine_synth.h"
#include "platform/engine/sample_catalog.h"

namespace fs = std::filesystem;
using namespace sc;
using namespace smpconv;

namespace {

SimStorage g_storage(StorageModel{1000, 3000000, 80, 12000});     // a modest card: 1 ms per request, 3 MB/s, a 12 ms stall every 80 requests
std::mutex g_io_mx;                                                 // storage contents vs the loader thread
std::thread g_io;
std::atomic<bool> g_stop{false};
std::string g_dir;

// The library folder of the simulated card (sdcard/system/samples, see storage_sim.c); found again later once it was made.
bool find_dir() {
    char p[256];
    sim_card_path(STORAGE_DIR_SAMPLES, p, sizeof p);
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return false;
    g_dir = p;
    return true;
}

/* ---------------- the catalog: the files in use, each read when it is asked for ---------------- */

std::string g_src[AUDIO_SAMPLES_MAX];                                   // source file of a pending entry, by catalog index

// The facts of a cooked .smp (length, peaks ...) into `in` under the library name `name`, and the file onto the simulated card.
bool fill_info(audio_sample_info_t &in, const std::string &name, const fs::path &p) {
    std::vector<uint8_t> data;
    SmpHeader h;
    if (!read_file(p, data) || !smp_parse_header(data.data(), static_cast<uint32_t>(data.size()), h) || data.size() < h.file_bytes()) return false;
    std::memset(&in, 0, sizeof in);
    std::snprintf(in.name, sizeof in.name, "%s", name.c_str());
    in.frames = h.frames; in.rate = h.sample_rate;
    in.root = static_cast<uint8_t>(h.root_note > 127 ? 127 : h.root_note);
    in.slices = h.slice_count; in.loop_mode = h.loop_mode; in.loop_start = h.loop_start; in.loop_end = h.loop_end;
    for (int k = 0; k < h.slice_count; k++) in.slice[k] = h.slice[k];
    static_assert(AUDIO_PEAKS == kSmpPeaks, "the catalog overview is the one stored in the .smp header");
    if (h.has_peaks()) std::memcpy(in.peaks, h.peaks, sizeof in.peaks);                 // amplitude overview: stored by the converter,
    else smp_compute_peaks(reinterpret_cast<const int16_t *>(&data[kSmpBlockBytes]), h.frames, in.peaks);   // or computed for an older file
    {
        std::lock_guard<std::mutex> lk(g_io_mx);
        g_storage.set_file((name + ".smp").c_str(), data);
    }
    return true;
}

int have_room(int index) { return index >= 0 ? index : -2; }             // engine_synth_catalog_add's -1: the catalog is full

// The file `name` (its path below the library folder, no extension) into the catalog: its index, -1 when there is no such file, -2
// when the catalog is full. An up-to-date .smp is read; a .wav / .mp3 without one is listed as pending and converted when it is used (convert).
int use(const std::string &name) {
    const int have = engine_synth_catalog_find(name.c_str());
    if (have >= 0) return have;
    if (name.empty() || name.size() >= AUDIO_NAME_MAX || (g_dir.empty() && !find_dir())) return -1;
    const fs::path base = fs::path(g_dir) / fs::path(name);
    fs::path smp = base, src;
    smp += ".smp";
    std::error_code ec;
    for (const char *ext : {".wav", ".mp3"}) {
        fs::path c = base;
        c += ext;
        if (src.empty() && fs::is_regular_file(c, ec)) src = c;
    }
    audio_sample_info_t in;
    if (fs::is_regular_file(smp, ec) && (src.empty() || fs::last_write_time(smp, ec) >= fs::last_write_time(src, ec)))
        return !fill_info(in, name, smp) ? -1 : (have_room(engine_synth_catalog_add(&in)));
    if (src.empty()) return -1;
    std::memset(&in, 0, sizeof in);                                               // a source without an up-to-date .smp: pending until it is used
    std::snprintf(in.name, sizeof in.name, "%s", name.c_str());
    in.pending = 1;
    in.kind = lower(src.extension().string()) == ".mp3" ? 2 : 1;
    const int i = engine_synth_catalog_add(&in);
    if (i >= 0) g_src[i] = src.string();
    return have_room(i);
}

// A pending .wav / .mp3 is decoded when it is used (assigned to a pad or a sampler): decode, mix to mono, cook <name>.smp next to it.
bool convert(int i) {
    audio_sample_info_t in;
    if (i < 0 || i >= AUDIO_SAMPLES_MAX || !engine_synth_sample_info(i, &in) || !in.name[0]) return false;
    if (!in.pending) return true;
    const std::string name = in.name;
    const fs::path src = g_src[i];
    fs::path dst = src;
    dst.replace_extension(".smp");
    Pcm pcm;
    bool ok;
    if (in.kind == 1) { std::vector<uint8_t> d; ok = read_file(src, d) && decode_wav(d, pcm); }
    else ok = decode_mp3(src, pcm);
    if (!ok || !write_smp(dst, pcm) || !fill_info(in, name, dst)) {
        std::printf("samples: could not convert %s\n", src.filename().string().c_str());
        return false;
    }
    std::printf("samples: converted %s (%zu frames at %u Hz)\n", src.filename().string().c_str(), pcm.mono.size(), pcm.rate);
    engine_synth_catalog_add(&in);                                                // the same name: the entry's facts
    return true;
}

bool convert_cb(int i) { return convert(i); }

/* ---------------- a folder of the library, for the browser ---------------- */

struct DirEnt { std::string name, key; bool dir; };
std::vector<DirEnt> g_list;

int dir_open(const char *dir) {
    g_list.clear();
    if (g_dir.empty() && !find_dir()) return -1;
    std::error_code ec;
    const fs::path p = dir && dir[0] ? fs::path(g_dir) / fs::path(dir) : fs::path(g_dir);
    if (!fs::is_directory(p, ec)) return -1;
    for (const auto &e : fs::directory_iterator(p, ec)) {
        const std::string fn = e.path().filename().string();
        if (fn.empty() || fn[0] == '.') continue;
        if (e.is_directory(ec)) { if (fn.size() < AUDIO_NAME_MAX) g_list.push_back({fn, lower(fn), true}); continue; }
        if (!e.is_regular_file(ec) || fn.size() < 5) continue;
        const std::string ext = lower(fn.substr(fn.size() - 4)), stem = fn.substr(0, fn.size() - 4);
        if ((ext != ".smp" && ext != ".wav" && ext != ".mp3") || stem.size() >= AUDIO_NAME_MAX || stem.find('~') != std::string::npos) continue;
        g_list.push_back({stem, lower(stem), false});
    }
    std::sort(g_list.begin(), g_list.end(), [](const DirEnt &a, const DirEnt &b) { return a.dir != b.dir ? a.dir : a.key < b.key; });
    g_list.erase(std::unique(g_list.begin(), g_list.end(), [](const DirEnt &a, const DirEnt &b) { return a.dir == b.dir && a.key == b.key; }),
                 g_list.end());                                                   // (a .wav next to its .smp: once)
    return static_cast<int>(g_list.size());
}

}  // namespace

extern "C" {

int sim_samples_init(void) {
    g_stop = false;
    if (!engine_synth_attach_storage(&g_storage)) return 0;
    engine_synth_set_importer(&convert_cb);
    char root[256];
    sim_card_path("", root, sizeof root);
    const size_t rl = std::strlen(root);
    if (rl && (root[rl - 1] == '/' || root[rl - 1] == 92)) root[rl - 1] = 0;
    engine_synth_set_card_root(root);                                      // the processing jobs write next to the library
    find_dir();
    g_io = std::thread([] {
        while (!g_stop) {
            { std::lock_guard<std::mutex> lk(g_io_mx); engine_synth_io_pump(); }
            for (int k = 0; k < 8 && engine_synth_capture_step(); k++) {}  // a recording: what waits in its ring
            for (int k = 0; k < 8 && engine_synth_render_step(); k++) {}   // a processing job, a few chunks between the loader's reads
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    std::printf("samples: the library is %s\n", g_dir.empty() ? "(no samples folder)" : g_dir.c_str());
    return 1;
}

void sim_samples_shutdown(void) {
    g_stop = true;
    if (g_io.joinable()) g_io.join();
}

int sim_samples_rescan(void) { if (g_dir.empty()) find_dir(); return engine_synth_sample_count(); }   // (the card has its folders now)

void sim_samples_hide(int index) { engine_synth_catalog_remove(index); }

int sim_samples_prepare(int index) { return convert(index) ? 1 : 0; }

int sim_samples_find(const char *name) { return engine_synth_catalog_find(name); }
int sim_samples_use(const char *name) { return name ? use(name) : -1; }
int sim_samples_dir_open(const char *dir) { return dir_open(dir); }
void sim_samples_dir_close(void) { g_list.clear(); }
int sim_samples_dir_read(int first, audio_dir_entry_t *out, int max) {
    int n = 0;
    for (; n < max && first >= 0 && first + n < static_cast<int>(g_list.size()); n++) {
        const DirEnt &e = g_list[static_cast<size_t>(first + n)];
        std::snprintf(out[n].name, sizeof out[n].name, "%s", e.name.c_str());
        out[n].dir = e.dir ? 1 : 0;
    }
    return n;
}

}  // extern "C"
#endif  // PLATFORM_SIM
