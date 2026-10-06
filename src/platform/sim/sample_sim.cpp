#ifdef PLATFORM_SIM
// Desktop sample library: the folder system/samples of the simulated card (sdcard/, see storage_sim.c; build.ps1 copies samples/*.smp
// into it) plays the role of the TF card's library. Every *.smp in it joins the library; *.wav and *.mp3 are imported first: decoded, mixed to mono, cooked into a
// .smp next to the source (so the next start skips the conversion) and then treated like any other file. The files live in
// a SimStorage, which models the latency, bandwidth and stalls of a slow card, so the streaming behaves like on the target.
// (wav and mp3 files are listed at once and converted when a sampler uses them.)
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
audio_sample_info_t g_cat[AUDIO_SAMPLES_MAX];
int g_n = 0;

// The library folder of the simulated card (sdcard/system/samples, see storage_sim.c); found again by a rescan once it was made.
bool find_dir() {
    char p[256];
    sim_card_path(STORAGE_DIR_SAMPLES, p, sizeof p);
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return false;
    g_dir = p;
    return true;
}

/* ---------------- catalog ---------------- */

std::string g_src[AUDIO_SAMPLES_MAX];                                   // source file of a pending entry

int find_in_catalog(const std::string &stem) {
    for (int i = 0; i < g_n; i++) if (stem == g_cat[i].name) return i;
    return -1;
}

// Reads a cooked .smp, fills entry `i` (name, length, peaks ...) and puts the file on the simulated card.
bool fill_entry(int i, const fs::path &p) {
    const std::string stem = p.stem().string().substr(0, 23);
    std::vector<uint8_t> data;
    SmpHeader h;
    if (!read_file(p, data) || !smp_parse_header(data.data(), static_cast<uint32_t>(data.size()), h) || data.size() < h.file_bytes()) return false;
    audio_sample_info_t &in = g_cat[i];
    std::memset(&in, 0, sizeof in);
    std::snprintf(in.name, sizeof in.name, "%s", stem.c_str());
    in.frames = h.frames; in.rate = h.sample_rate;
    in.root = static_cast<uint8_t>(h.root_note > 127 ? 127 : h.root_note);
    in.slices = h.slice_count; in.loop_mode = h.loop_mode; in.loop_start = h.loop_start; in.loop_end = h.loop_end;
    for (int k = 0; k < h.slice_count; k++) in.slice[k] = h.slice[k];
    static_assert(AUDIO_PEAKS == kSmpPeaks, "the catalog overview is the one stored in the .smp header");
    if (h.has_peaks()) std::memcpy(in.peaks, h.peaks, sizeof in.peaks);                 // amplitude overview: stored by the converter,
    else smp_compute_peaks(reinterpret_cast<const int16_t *>(&data[kSmpBlockBytes]), h.frames, in.peaks);   // or computed for an older file
    {
        std::lock_guard<std::mutex> lk(g_io_mx);
        g_storage.add_file((stem + ".smp").c_str(), data);
    }
    return true;
}

// A .wav / .mp3 is listed at once but decoded only when it is used (assigned to a sampler): decode, mix to mono, cook <name>.smp next to it.
bool convert(int i) {
    if (i < 0 || i >= g_n || !g_cat[i].pending) return i >= 0 && i < g_n;
    const fs::path src = g_src[i];
    fs::path dst = src;
    dst.replace_extension(".smp");
    Pcm pcm;
    bool ok;
    if (g_cat[i].kind == 1) { std::vector<uint8_t> d; ok = read_file(src, d) && decode_wav(d, pcm); }
    else ok = decode_mp3(src, pcm);
    if (!ok || !write_smp(dst, pcm) || !fill_entry(i, dst)) {
        std::printf("samples: could not convert %s\n", src.filename().string().c_str());
        return false;
    }
    std::printf("samples: converted %s (%zu frames at %u Hz)\n", src.filename().string().c_str(), pcm.mono.size(), pcm.rate);
    engine_synth_catalog_set(g_cat, g_n);
    return true;
}

bool convert_cb(int i) { return convert(i); }

int scan() {
    if (g_dir.empty() && !find_dir()) return g_n;
    std::vector<std::pair<std::string, fs::path>> smp, src;                       // stem -> path
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(g_dir, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = lower(e.path().extension().string());
        const std::string stem = e.path().stem().string().substr(0, 23);
        if (ext == ".smp") smp.push_back({stem, e.path()});
        else if (ext == ".wav" || ext == ".mp3") src.push_back({stem, e.path()});
    }
    std::vector<std::string> stems;
    for (auto &p : smp) stems.push_back(p.first);
    for (auto &p : src) stems.push_back(p.first);
    std::sort(stems.begin(), stems.end());
    stems.erase(std::unique(stems.begin(), stems.end()), stems.end());            // new files are appended after the known ones
    for (const std::string &stem : stems) {
        if (find_in_catalog(stem) >= 0 || g_n >= AUDIO_SAMPLES_MAX) continue;
        const fs::path *sp = nullptr, *rp = nullptr;
        for (auto &p : smp) if (p.first == stem) sp = &p.second;
        for (auto &p : src) if (p.first == stem) rp = &p.second;
        const int i = g_n;
        const bool smp_current = sp && (!rp || fs::last_write_time(*sp, ec) >= fs::last_write_time(*rp, ec));
        if (smp_current) { if (fill_entry(i, *sp)) g_n++; continue; }
        std::memset(&g_cat[i], 0, sizeof g_cat[i]);                               // a source without an up-to-date .smp: pending until it is used
        std::snprintf(g_cat[i].name, sizeof g_cat[i].name, "%s", stem.c_str());
        g_cat[i].pending = 1;
        g_cat[i].kind = lower(rp->extension().string()) == ".mp3" ? 2 : 1;
        g_src[i] = rp->string();
        g_n++;
    }
    engine_synth_catalog_set(g_cat, g_n);
    return g_n;
}

}  // namespace

extern "C" {

int sim_samples_init(void) {
    g_n = 0;
    g_stop = false;
    if (!engine_synth_attach_storage(&g_storage)) return 0;
    engine_synth_set_importer(&convert_cb);
    const int n = scan();
    g_io = std::thread([] {
        while (!g_stop) {
            { std::lock_guard<std::mutex> lk(g_io_mx); engine_synth_io_pump(); }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    std::printf("samples: %d file(s) from %s\n", n, g_dir.empty() ? "(no samples folder)" : g_dir.c_str());
    return n;
}

void sim_samples_shutdown(void) {
    g_stop = true;
    if (g_io.joinable()) g_io.join();
}

int sim_samples_rescan(void) { return scan(); }

int sim_samples_prepare(int index) { return convert(index) ? 1 : 0; }

}  // extern "C"
#endif  // PLATFORM_SIM
