#ifdef PLATFORM_SIM
// Desktop sample library: the folder samples/ (found next to the working directory, or one or two levels up) plays the role of
// the TF card. Every *.smp in it joins the library; *.wav and *.mp3 are imported first: decoded, mixed to mono, cooked into a
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
#include "engine/sampler/sim_storage.h"
#include "engine/sampler/smp_format.h"
#include "platform/engine/engine_synth.h"
#include "platform/engine/sample_catalog.h"

#if __has_include("minimp3_ex.h")
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
#define HAVE_MP3 1
#endif

namespace fs = std::filesystem;
using namespace sc;

namespace {

SimStorage g_storage(StorageModel{1000, 3000000, 80, 12000});     // a modest card: 1 ms per request, 3 MB/s, a 12 ms stall every 80 requests
std::mutex g_io_mx;                                                 // storage contents vs the loader thread
std::thread g_io;
std::atomic<bool> g_stop{false};
std::string g_dir;
audio_sample_info_t g_cat[AUDIO_SAMPLES_MAX];
int g_n = 0;

bool find_dir() {
    for (const char *c : {"samples", "../samples", "../../samples"}) {
        std::error_code ec;
        if (fs::is_directory(c, ec)) { g_dir = c; return true; }
    }
    return false;
}

std::string lower(std::string s) { for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

bool read_file(const fs::path &p, std::vector<uint8_t> &out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

/* ---------------- WAV ---------------- */

struct Pcm {
    std::vector<int16_t> mono;
    uint32_t rate = 48000;
    int root = 60;
    uint32_t loop_start = 0, loop_end = 0;
};

uint32_t rd32(const uint8_t *p) { return get32(p); }

bool decode_wav(const std::vector<uint8_t> &d, Pcm &out) {
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return false;
    uint32_t fmt_tag = 0, ch = 0, rate = 0, bits = 0;
    const uint8_t *data = nullptr;
    size_t data_bytes = 0;
    for (size_t pos = 12; pos + 8 <= d.size();) {
        const uint32_t size = rd32(&d[pos + 4]);
        const uint8_t *body = &d[pos + 8];
        const size_t avail = std::min<size_t>(size, d.size() - pos - 8);
        if (!std::memcmp(&d[pos], "fmt ", 4) && avail >= 16) {
            fmt_tag = get16(body); ch = get16(body + 2); rate = rd32(body + 4); bits = get16(body + 14);
            if (fmt_tag == 0xFFFE && avail >= 26) fmt_tag = get16(body + 24);               // WAVE_FORMAT_EXTENSIBLE: the sub-format
        } else if (!std::memcmp(&d[pos], "data", 4)) {
            data = body; data_bytes = avail;
        } else if (!std::memcmp(&d[pos], "smpl", 4) && avail >= 60) {
            const uint32_t unity = rd32(body + 12);
            if (unity >= 1 && unity <= 127) out.root = static_cast<int>(unity);
            if (rd32(body + 28) >= 1) { out.loop_start = rd32(body + 36 + 8); out.loop_end = rd32(body + 36 + 12) + 1; }
        }
        pos += 8 + size + (size & 1u);
    }
    if (!data || ch < 1 || ch > 8 || rate == 0 || !(bits == 8 || bits == 16 || bits == 24 || bits == 32)) return false;
    if (fmt_tag != 1 && !(fmt_tag == 3 && bits == 32)) return false;
    const size_t bps = bits / 8, frames = data_bytes / (bps * ch);
    out.rate = rate;
    out.mono.resize(frames);
    for (size_t i = 0; i < frames; i++) {
        double acc = 0;
        for (uint32_t c = 0; c < ch; c++) {
            const uint8_t *p = data + (i * ch + c) * bps;
            double v;
            if (fmt_tag == 3) { float f; std::memcpy(&f, p, 4); v = static_cast<double>(f); }
            else if (bits == 8) v = (p[0] - 128) / 128.0;
            else if (bits == 16) v = static_cast<int16_t>(get16(p)) / 32768.0;
            else if (bits == 24) v = (static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 8) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 24)) >> 8) / 8388608.0;
            else v = static_cast<int32_t>(rd32(p)) / 2147483648.0;
            acc += v;
        }
        out.mono[i] = static_cast<int16_t>(std::lround(std::fmax(-1.0, std::fmin(1.0, acc / ch)) * 32767.0));
    }
    return frames > 0;
}

bool decode_mp3(const fs::path &p, Pcm &out) {
#ifdef HAVE_MP3
    mp3dec_t dec;
    mp3dec_file_info_t info;
    std::memset(&info, 0, sizeof info);
    const std::string path = p.string();
    if (mp3dec_load(&dec, path.c_str(), &info, nullptr, nullptr) != 0 || !info.buffer || info.channels < 1 || info.hz <= 0) { std::free(info.buffer); return false; }
    const size_t frames = info.samples / static_cast<size_t>(info.channels);
    out.rate = static_cast<uint32_t>(info.hz);
    out.mono.resize(frames);
    for (size_t i = 0; i < frames; i++) {
        int acc = 0;
        for (int c = 0; c < info.channels; c++) acc += info.buffer[i * static_cast<size_t>(info.channels) + static_cast<size_t>(c)];
        out.mono[i] = static_cast<int16_t>(acc / info.channels);
    }
    std::free(info.buffer);
    return frames > 0;
#else
    (void)p; (void)out;
    return false;
#endif
}

// Cooks a decoded sample into <stem>.smp. Long files are cut at 5 minutes.
bool write_smp(const fs::path &dst, Pcm &pcm) {
    const size_t cap = static_cast<size_t>(pcm.rate) * 300;
    if (pcm.mono.size() > cap) pcm.mono.resize(cap);
    SmpHeader h;
    h.sample_rate = pcm.rate;
    h.frames = static_cast<uint32_t>(pcm.mono.size());
    h.root_note = static_cast<uint16_t>(pcm.root);
    if (pcm.loop_end > pcm.loop_start + 1 && pcm.loop_end <= h.frames) { h.loop_start = pcm.loop_start; h.loop_end = pcm.loop_end; h.loop_mode = SMP_LOOP_FWD; }
    const std::vector<uint8_t> file = smp_build(h, pcm.mono.data());
    std::ofstream f(dst, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(f);
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
    for (int b = 0; b < AUDIO_PEAKS; b++) {                                      // amplitude overview
        const uint64_t f0 = static_cast<uint64_t>(h.frames) * static_cast<uint64_t>(b) / AUDIO_PEAKS;
        const uint64_t f1 = std::max<uint64_t>(f0 + 1, static_cast<uint64_t>(h.frames) * static_cast<uint64_t>(b + 1) / AUDIO_PEAKS);
        const uint64_t stride = std::max<uint64_t>(1, (f1 - f0) / 512);
        int peak = 0;
        for (uint64_t f = f0; f < f1 && f < h.frames; f += stride) peak = std::max(peak, std::abs(static_cast<int16_t>(get16(&data[kSmpBlockBytes + 2 * f]))));
        in.peaks[b] = static_cast<uint8_t>(std::min(255, peak >> 7));
    }
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
