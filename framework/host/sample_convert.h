#pragma once
// WAV / MP3 -> cooked .smp (smp_format.h): shared by the simulator's importer (sample_sim.cpp) and the offline converter (tools/smp_convert.cpp,
// run by build.ps1 on the samples_src/ folder). Stereo is mixed down to mono, long files are cut at 5 minutes, loop points and the root note
// come from the WAV 'smpl' chunk, and the amplitude overview is stored in the header. Desktop only: it uses <filesystem> and, when
// lib/minimp3 is present, decodes MP3.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "engine/sampler/smp_format.h"

#if __has_include("minimp3_ex.h")
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
#define HAVE_MP3 1
#endif

namespace smpconv {
namespace fs = std::filesystem;
using namespace sc;

inline std::string lower(std::string s) { for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

inline bool read_file(const fs::path &p, std::vector<uint8_t> &out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

/* ---------------- WAV ---------------- */

struct Pcm {
    std::vector<int16_t> mono;
    uint32_t rate = 48000;
    bool has_root = false;                                      // the WAV said what its root note is
    int root = 60;
    uint32_t loop_start = 0, loop_end = 0;
};

inline uint32_t rd32(const uint8_t *p) { return get32(p); }

inline bool decode_wav(const std::vector<uint8_t> &d, Pcm &out) {
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
            if (unity >= 1 && unity <= 127) { out.root = static_cast<int>(unity); out.has_root = true; }
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

inline bool decode_mp3(const fs::path &p, Pcm &out) {
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
inline bool write_smp(const fs::path &dst, Pcm &pcm) {
    const size_t cap = static_cast<size_t>(pcm.rate) * 300;
    if (pcm.mono.size() > cap) pcm.mono.resize(cap);
    SmpHeader h;
    h.sample_rate = pcm.rate;
    h.frames = static_cast<uint32_t>(pcm.mono.size());
    h.root_note = static_cast<uint16_t>(pcm.root);
    if (pcm.loop_end > pcm.loop_start + 1 && pcm.loop_end <= h.frames) { h.loop_start = pcm.loop_start; h.loop_end = pcm.loop_end; h.loop_mode = SMP_LOOP_FWD; }
    smp_compute_peaks(pcm.mono.data(), h.frames, h.peaks);
    const std::vector<uint8_t> file = smp_build(h, pcm.mono.data());
    std::ofstream f(dst, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(f);
}

}  // namespace smpconv
