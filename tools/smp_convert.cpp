// Offline sample converter: every .wav / .mp3 in <src_dir> becomes <dst_dir>/<name>.smp (the cooked format the engine streams from the TF card).
// build.ps1 runs it on samples_src/ -> samples/, so putting a file in samples_src/ and building is all it takes. Only new or changed files are
// converted (the .smp is older than its source), unless --force.
//
//   build\smp_convert.exe samples_src samples [--force]
//
// Stereo is mixed to mono, files longer than 5 minutes are cut. Loop points and the root note come from the WAV 'smpl' chunk when it has one.
// Without it, the root note is read from a name that ends in a note: pad_c4.wav = C4, bass_fs2.wav / bass_f#2.wav = F#2, lead_bb3.wav = Bb3
// (C4 = MIDI 60); otherwise it is C4. Names are cut to 23 characters (the catalog's limit).
#include <cstdio>
#include <cstring>
#include "platform/sim/sample_convert.h"

using namespace smpconv;

namespace {

// "_c4", "_F#3", "_fs3", "_bb2", "_a-1" at the end of a stem -> MIDI note, or -1
int note_from_name(const std::string &stem) {
    const size_t us = stem.rfind('_');
    if (us == std::string::npos) return -1;
    const std::string t = lower(stem.substr(us + 1));
    if (t.size() < 2 || t[0] < 'a' || t[0] > 'g') return -1;
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};                       // a b c d e f g
    int semi = base[t[0] - 'a'];
    size_t i = 1;
    if (t[i] == '#' || t[i] == 's') { semi++; i++; }
    else if (t[i] == 'b' && i + 1 < t.size() && (std::isdigit(static_cast<unsigned char>(t[i + 1])) || t[i + 1] == '-')) { semi--; i++; }
    if (i >= t.size()) return -1;
    char *end = nullptr;
    const long oct = std::strtol(t.c_str() + i, &end, 10);
    if (end == t.c_str() + i || *end) return -1;
    const long midi = 12 * (oct + 1) + semi;
    return midi >= 0 && midi <= 127 ? static_cast<int>(midi) : -1;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: smp_convert <src_dir> <dst_dir> [--force]\n"); return 2; }
    const fs::path src = argv[1], dst = argv[2];
    const bool force = argc > 3 && !std::strcmp(argv[3], "--force");
    std::error_code ec;
    if (!fs::is_directory(src, ec)) { std::fprintf(stderr, "smp_convert: no folder %s\n", src.string().c_str()); return 2; }
    fs::create_directories(dst, ec);

    std::vector<fs::path> files;
    for (const auto &e : fs::directory_iterator(src, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = lower(e.path().extension().string());
        if (ext == ".wav" || ext == ".mp3") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());

    int converted = 0, skipped = 0, failed = 0;
    for (const fs::path &p : files) {
        std::string stem = p.stem().string();
        if (stem.size() > 23) { stem.resize(23); std::printf("  note: the name of %s is cut to %s (23 characters at most)\n", p.filename().string().c_str(), stem.c_str()); }
        const fs::path out = dst / (stem + ".smp");
        if (!force && fs::exists(out, ec) && fs::last_write_time(out, ec) >= fs::last_write_time(p, ec)) { skipped++; continue; }

        Pcm pcm;
        bool ok;
        if (lower(p.extension().string()) == ".wav") { std::vector<uint8_t> d; ok = read_file(p, d) && decode_wav(d, pcm); }
        else ok = decode_mp3(p, pcm);
        if (ok && !pcm.has_root) { const int n = note_from_name(stem); if (n >= 0) pcm.root = n; }
        if (!ok || !write_smp(out, pcm)) {
            std::printf("  FAILED %s (%s)\n", p.filename().string().c_str(), ok ? "could not write the .smp" : "not a PCM / float WAV or an MP3 this decoder reads");
            failed++;
            continue;
        }
        char loop[48] = "no loop";
        if (pcm.loop_end > pcm.loop_start + 1) std::snprintf(loop, sizeof loop, "loop %u..%u", pcm.loop_start, pcm.loop_end);
        std::printf("  %s -> %s  %u Hz, %zu frames (%.2f s), root %d, %s\n", p.filename().string().c_str(), out.filename().string().c_str(), pcm.rate,
                    pcm.mono.size(), static_cast<double>(pcm.mono.size()) / pcm.rate, pcm.root, loop);
        converted++;
    }
    std::printf("samples: %d converted, %d up to date%s\n", converted, skipped, failed ? ", FAILED: see above" : "");
    return failed ? 1 : 0;
}
