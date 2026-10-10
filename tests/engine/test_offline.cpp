// The offline sample processing (engine/offline/process.h, ADR-041 decisions 36-38): every Level & cleanup operation on an in-memory
// sample, in small steps like the card task runs it, checked against the expected result.
#include <algorithm>
#include <cmath>
#include <vector>
#include "rig.h"
#include "engine/offline/process.h"

using namespace tst;

namespace {
struct MemReader : sc::FrameReader {
    const std::vector<int16_t> &x;
    explicit MemReader(const std::vector<int16_t> &v) : x(v) {}
    bool read(uint32_t frame, uint32_t n, sc::q15 *dst) override {
        if (frame + n > x.size()) return false;
        for (uint32_t i = 0; i < n; i++) dst[i] = x[frame + i];
        return true;
    }
};
struct MemWriter : sc::FrameWriter {
    std::vector<int16_t> y;
    bool write(const sc::q15 *src, uint32_t n) override { y.insert(y.end(), src, src + n); return true; }
};

std::vector<int16_t> run(const std::vector<int16_t> &x, sc::ProcOp op, int32_t amount, sc::SmpHeader *out = nullptr, sc::SmpHeader in = {}) {
    sc::Processor p;
    in.sample_rate = 48000;
    in.frames = static_cast<uint32_t>(x.size());
    MemReader r(x);
    MemWriter w;
    p.begin(in, op, amount);
    for (int guard = 0; guard < 100000 && !p.done(); guard++) CHECK(p.step(r, w, 3));
    CHECK(p.done());
    CHECK_EQ(w.y.size(), p.out_header().frames);
    if (out) *out = p.out_header();
    return w.y;
}

std::vector<int16_t> tone(size_t n, double amp, double hz = 440.0) {
    std::vector<int16_t> x(n);
    for (size_t i = 0; i < n; i++) x[i] = static_cast<int16_t>(amp * 32767.0 * std::sin(6.283185307 * hz * static_cast<double>(i) / 48000.0));
    return x;
}
int peak(const std::vector<int16_t> &x) { int p = 0; for (int16_t v : x) p = std::max(p, std::abs(static_cast<int>(v))); return p; }
}  // namespace

TEST(offline_normalize_and_gain) {
    const std::vector<int16_t> x = tone(30000, 0.1);                        // a peak of -20 dB, 30000 frames (not a whole number of chunks)
    const std::vector<int16_t> n = run(x, sc::ProcOp::Normalize, -3);       // to -0.3 dBFS
    std::printf("    normalize: peak %d -> %d (target %d)\n", peak(x), peak(n), static_cast<int>(32767 * std::pow(10.0, -0.3 / 20)));
    CHECK(std::abs(peak(n) - static_cast<int>(32767 * std::pow(10.0, -0.3 / 20))) <= 2);
    const std::vector<int16_t> g = run(x, sc::ProcOp::Gain, 60);           // +6 dB
    CHECK(std::abs(peak(g) - static_cast<int>(peak(x) * std::pow(10.0, 0.3))) <= 2);
    const std::vector<int16_t> s = run(tone(5000, 0.9), sc::ProcOp::Gain, 120);   // +12 dB on a loud tone: saturates, no wrap
    CHECK(peak(s) >= 32767);                                                // (saturated: 32767 or -32768)
    for (size_t i = 1; i < s.size(); i++) CHECK(!(s[i - 1] > 30000 && s[i] < -30000));
}

TEST(offline_fades_reverse_dc) {
    const std::vector<int16_t> x(48000, 10000);                             // a constant: easy to check
    const std::vector<int16_t> fi = run(x, sc::ProcOp::FadeIn, 100);        // 100 ms = 4800 frames
    CHECK_EQ(fi[0], 0);
    CHECK(std::abs(fi[2400] - 5000) <= 2);
    CHECK_EQ(fi[4800], 10000);
    const std::vector<int16_t> fo = run(x, sc::ProcOp::FadeOut, 100);
    CHECK_EQ(fo[47999], 0);
    CHECK_EQ(fo[40000], 10000);
    std::vector<int16_t> ramp(10000);
    for (size_t i = 0; i < ramp.size(); i++) ramp[i] = static_cast<int16_t>(i);
    sc::SmpHeader in, out;
    in.loop_start = 1000; in.loop_end = 3000; in.loop_mode = sc::SMP_LOOP_FWD;
    in.slice_count = 2; in.slice[0] = 0; in.slice[1] = 5000;
    const std::vector<int16_t> rv = run(ramp, sc::ProcOp::Reverse, 0, &out, in);
    bool exact = true;
    for (size_t i = 0; i < rv.size(); i++) exact &= rv[i] == ramp[ramp.size() - 1 - i];
    CHECK(exact);
    CHECK(out.loop_start == 7000 && out.loop_end == 9000 && out.slice_count == 0);
    std::vector<int16_t> dc = tone(96000, 0.2, 200.0);
    for (auto &v : dc) v = static_cast<int16_t>(v + 6000);                  // a DC offset of 6000
    const std::vector<int16_t> d = run(dc, sc::ProcOp::RemoveDc, 0);
    double mean = 0;
    for (size_t i = 48000; i < d.size(); i++) mean += d[i];
    mean /= 48000.0;
    std::printf("    remove DC: mean of the second half %.1f (was 6000)\n", mean);
    CHECK(std::fabs(mean) < 60.0);
}

TEST(offline_trim_silence_keeps_5_ms_and_moves_the_loop) {
    std::vector<int16_t> x(48000, 0);
    const std::vector<int16_t> t = tone(12000, 0.5);
    std::copy(t.begin(), t.end(), x.begin() + 20000);                        // sound in 20000..32000, silence around it
    sc::SmpHeader in, out;
    in.loop_start = 22000; in.loop_end = 30000; in.loop_mode = sc::SMP_LOOP_FWD;
    in.slice_count = 2; in.slice[0] = 0; in.slice[1] = 25000;
    const std::vector<int16_t> y = run(x, sc::ProcOp::TrimSilence, -40, &out, in);
    std::printf("    trim: %zu -> %zu frames, loop %u..%u, %d slice(s)\n", x.size(), y.size(), out.loop_start, out.loop_end, out.slice_count);
    CHECK(y.size() > 12000 && y.size() < 12000 + 2 * 240 + 10);              // the sound + 5 ms (240 frames) each side
    CHECK(out.loop_end - out.loop_start == 8000 && out.loop_start > 1700 && out.loop_start < 2300);
    CHECK_EQ(out.slice_count, 1);                                           // the slice at 0 was in the silence
    CHECK(out.has_peaks());
}

// The card job (platform/engine/render_job.cpp): a .smp in a folder that plays the card, normalized into a new file, read back.
#include <filesystem>
#include <fstream>
#include "platform/engine/engine_synth.h"

TEST(offline_render_job_writes_a_new_smp_on_the_card) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "sc_render_test";
    fs::remove_all(root);
    fs::create_directories(root / STORAGE_DIR_SAMPLES);
    const std::vector<int16_t> x = tone(30000, 0.1);
    sc::SmpHeader h;
    h.sample_rate = 48000; h.frames = static_cast<uint32_t>(x.size()); h.loop_start = 1000; h.loop_end = 20000; h.loop_mode = sc::SMP_LOOP_FWD;
    const std::vector<uint8_t> file = sc::smp_build(h, x.data());
    std::ofstream(root / STORAGE_DIR_SAMPLES / "src.smp", std::ios::binary).write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
    engine_synth_set_card_root(root.string().c_str());
    CHECK(engine_synth_render_start("src", "src~1", 0, -3));                // Normalize to -0.3 dBFS
    CHECK(!engine_synth_render_start("src", "x", 0, 0));                     // one job at a time
    int steps = 0, pct = 0;
    while (engine_synth_render_step() && steps < 10000) steps++;
    CHECK_EQ(engine_synth_render_poll(&pct), RENDER_DONE);
    CHECK_EQ(pct, 100);
    engine_synth_render_done();
    CHECK_EQ(engine_synth_render_poll(nullptr), RENDER_IDLE);
    std::ifstream in(root / STORAGE_DIR_SAMPLES / "src~1.smp", std::ios::binary);
    std::vector<uint8_t> out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();                                                             // (Windows: an open file cannot be removed)
    sc::SmpHeader oh;
    CHECK(out.size() >= sc::kSmpBlockBytes && sc::smp_parse_header(out.data(), static_cast<uint32_t>(out.size()), oh));
    CHECK_EQ(oh.frames, x.size());
    CHECK_EQ(out.size(), oh.file_bytes());                                  // whole blocks
    CHECK(oh.loop_start == 1000 && oh.loop_end == 20000 && oh.has_peaks());
    int p = 0;
    for (uint32_t i = 0; i < oh.frames; i++) p = std::max(p, std::abs(static_cast<int>(static_cast<int16_t>(sc::get16(&out[sc::kSmpBlockBytes + 2 * i])))));
    std::printf("    card job: %d steps, peak %d (target %d), %zu bytes\n", steps, p, static_cast<int>(32767 * std::pow(10.0, -0.3 / 20)), out.size());
    CHECK(std::abs(p - static_cast<int>(32767 * std::pow(10.0, -0.3 / 20))) <= 2);
    CHECK(!fs::exists(root / STORAGE_DIR_SAMPLES / "src~1.smp.tmp"));
    CHECK(engine_synth_render_start("missing", "m~1", 0, 0));                // a source that is not there: FAILED, no file left
    while (engine_synth_render_step()) {}
    CHECK_EQ(engine_synth_render_poll(nullptr), RENDER_FAILED);
    engine_synth_render_done();
    CHECK(!fs::exists(root / STORAGE_DIR_SAMPLES / "m~1.smp") && !fs::exists(root / STORAGE_DIR_SAMPLES / "m~1.smp.tmp"));
    fs::remove_all(root);
}

TEST(offline_pad_to_appends_silence) {
    const std::vector<int16_t> x(30000, 1234);
    const std::vector<int16_t> y = run(x, sc::ProcOp::PadTo, 48000);
    CHECK_EQ(y.size(), 48000u);
    CHECK(y[29999] == 1234 && y[30000] == 0 && y[47999] == 0);
    CHECK_EQ(run(x, sc::ProcOp::PadTo, 1000).size(), 30000u);               // shorter: unchanged
}
