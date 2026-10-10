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

std::vector<int16_t> run(const std::vector<int16_t> &x, sc::ProcOp op, int32_t amount, sc::SmpHeader *out = nullptr, sc::SmpHeader in = {}, uint32_t rate = 48000,
                         int32_t amount2 = 0) {
    static sc::Processor p;                                               // (20 KB: not on the stack)
    in.sample_rate = rate;
    in.frames = static_cast<uint32_t>(x.size());
    MemReader r(x);
    MemWriter w;
    std::vector<uint64_t> work((sc::Processor::work_bytes(op) + 7) / 8 + 1);  // the operation's working memory
    p.begin(in, op, amount, amount2, work.data());
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
    CHECK(engine_synth_render_start("src", "src~1", 0, -3, 0));                // Normalize to -0.3 dBFS
    CHECK(!engine_synth_render_start("src", "x", 0, 0, 0));                     // one job at a time
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
    CHECK(engine_synth_render_start("src", "src~2", 8, 1500000, 0));           // Stretch to 150 %: reads all over the source file
    steps = 0;
    while (engine_synth_render_step() && steps < 10000) steps++;
    CHECK_EQ(engine_synth_render_poll(nullptr), RENDER_DONE);
    engine_synth_render_done();
    std::ifstream in2(root / STORAGE_DIR_SAMPLES / "src~2.smp", std::ios::binary);
    std::vector<uint8_t> out2((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
    in2.close();
    CHECK(out2.size() >= sc::kSmpBlockBytes && sc::smp_parse_header(out2.data(), static_cast<uint32_t>(out2.size()), oh));
    CHECK_EQ(oh.frames, 45000u);
    CHECK_EQ(out2.size(), oh.file_bytes());
    CHECK(oh.loop_start == 1500 && oh.loop_end == 30000);
    p = 0;
    for (uint32_t i = 20000; i < 30000; i++) p = std::max(p, std::abs(static_cast<int>(static_cast<int16_t>(sc::get16(&out2[sc::kSmpBlockBytes + 2 * i])))));
    std::printf("    card job, stretch: %d steps, %u frames, peak %d (the tone's 3276)\n", steps, oh.frames, p);
    CHECK(std::abs(p - 3276) <= 40);
    CHECK(engine_synth_render_start("src", "src~3", 19, 2000000, 0));       // Paulstretch x2: a job with working memory of its own
    while (engine_synth_render_step()) {}
    CHECK_EQ(engine_synth_render_poll(nullptr), RENDER_DONE);
    engine_synth_render_done();
    std::ifstream in3(root / STORAGE_DIR_SAMPLES / "src~3.smp", std::ios::binary);
    std::vector<uint8_t> out3((std::istreambuf_iterator<char>(in3)), std::istreambuf_iterator<char>());
    in3.close();
    CHECK(out3.size() >= sc::kSmpBlockBytes && sc::smp_parse_header(out3.data(), static_cast<uint32_t>(out3.size()), oh));
    CHECK_EQ(oh.frames, 60000u);
    CHECK(oh.loop_start == 2000 && oh.loop_end == 40000 && oh.has_peaks());
    CHECK(!engine_synth_render_start("src", "x", 20, 0, 0));                    // not an operation
    CHECK(engine_synth_render_start("missing", "m~1", 0, 0, 0));                // a source that is not there: FAILED, no file left
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

/* ---------------- time & pitch: Stretch (WSOLA), Resample, Pitch ---------------- */

namespace {
// The `hz` component of x[a, b), looked at in windows of 4096 frames (a seam of the stretch is 1024 long): its amplitude in the
// weakest window (`amp`), in the strongest (`top`), and the largest RMS of everything else in a window (`rest`). Per window, so a
// slow drift of the phase (inaudible) does not count as an error, and a seam cannot hide in the average.
struct Part { double amp, top, rest; };
Part measure(const std::vector<int16_t> &x, size_t a, size_t b, double hz, double rate = 48000.0) {
    Part out{1e9, 0.0, 0.0};
    const size_t W = 4096;
    for (size_t s0 = a; s0 + W <= b; s0 += W / 2) {
        double re = 0, im = 0, e = 0, ws = 0;
        for (size_t i = 0; i < W; i++) {
            const double w = 0.5 - 0.5 * std::cos(6.283185307 * static_cast<double>(i) / static_cast<double>(W));
            const double ph = 6.283185307 * hz * static_cast<double>(s0 + i) / rate, v = x[s0 + i];
            re += w * v * std::cos(ph);
            im += w * v * std::sin(ph);
            e += w * v * v;
            ws += w;
        }
        const double amp = 2.0 * std::sqrt(re * re + im * im) / ws;
        out.amp = std::min(out.amp, amp);
        out.top = std::max(out.top, amp);
        out.rest = std::max(out.rest, std::sqrt(std::max(0.0, e / ws - amp * amp / 2.0)));
    }
    return out;
}
// The level of the tone against the 0.5 it had (dB, the window furthest off), and how far under it everything else is (dB).
double level(const Part &p) { const double lo = tst::db(p.amp / 16383.5), hi = tst::db(p.top / 16383.5); return std::fabs(lo) > std::fabs(hi) ? lo : hi; }
double under(const Part &p) { return -tst::db(std::max(p.rest, 1e-3) / (p.amp / 1.41421356)); }
}  // namespace

TEST(offline_stretch_by_1_is_the_sample_itself) {
    std::vector<int16_t> x(50000);
    for (size_t i = 0; i < x.size(); i++) {                                   // two tones and a slow swell: nothing repeats
        const double t = static_cast<double>(i) / 48000.0;
        x[i] = static_cast<int16_t>(9000.0 * (0.4 + 0.6 * t) * (std::sin(6.283185307 * 440.0 * t) + 0.7 * std::sin(6.283185307 * 617.3 * t)));
    }
    const std::vector<int16_t> y = run(x, sc::ProcOp::Stretch, 1000000);
    CHECK_EQ(y.size(), x.size());
    int worst = 0;
    for (size_t i = 0; i < y.size() && i < x.size(); i++) worst = std::max(worst, std::abs(y[i] - x[i]));
    std::printf("    stretch x1: largest difference %d\n", worst);
    CHECK(worst <= 1);
}

TEST(offline_stretch_changes_the_length_and_keeps_the_pitch) {
    struct Case { double hz; int32_t ppm; };
    const Case cases[] = {{440.0, 1500000}, {440.0, 500000}, {440.0, 730000}, {55.0, 2000000}, {3000.0, 1250000}, {110.0, 4000000}, {220.0, 250000}};
    for (const Case &c : cases) {
        const std::vector<int16_t> x = tone(96000, 0.5, c.hz);
        const std::vector<int16_t> y = run(x, sc::ProcOp::Stretch, c.ppm);
        const size_t want = static_cast<size_t>(96000.0 * c.ppm / 1e6 + 0.5);
        CHECK_EQ(y.size(), want);
        const Part p = measure(y, y.size() / 8, y.size() * 7 / 8, c.hz);
        std::printf("    stretch x%.2f of %.0f Hz: level %+.2f dB, everything else %.1f dB under it\n", c.ppm / 1e6, c.hz, level(p), under(p));
        CHECK(std::fabs(level(p)) < 0.3);                          // the tone, at its level
        CHECK(under(p) > 30.0);                     // the seams: 30 dB under it or better
    }
}

TEST(offline_stretch_keeps_the_attack_and_scales_loop_and_slices) {
    std::vector<int16_t> x(48000);
    for (size_t i = 0; i < x.size(); i++) x[i] = static_cast<int16_t>(28000.0 * std::exp(-static_cast<double>(i) / 2500.0) * std::sin(6.283185307 * 180.0 * static_cast<double>(i) / 48000.0 + 1.2));
    sc::SmpHeader in, out;
    in.loop_start = 1000; in.loop_end = 3000; in.loop_mode = sc::SMP_LOOP_FWD;
    in.slice_count = 2; in.slice[0] = 0; in.slice[1] = 24000;
    const std::vector<int16_t> y = run(x, sc::ProcOp::Stretch, 2000000, &out, in);
    CHECK_EQ(y.size(), 96000u);
    bool same = true;
    for (int i = 0; i < 1024; i++) same &= y[i] == x[i];                      // the first hop is the file's own start
    CHECK(same);
    CHECK(out.loop_start == 2000 && out.loop_end == 6000 && out.slice_count == 2 && out.slice[1] == 48000);
    CHECK(out.has_peaks());
    // a 44.1 kHz file: the window follows the rate
    const std::vector<int16_t> t = tone(60000, 0.5, 441.0);                   // (441 Hz when read at 44.1 kHz: "hz" is per 48000 frames here)
    const std::vector<int16_t> z = run(t, sc::ProcOp::Stretch, 1300000, nullptr, {}, 44100);
    CHECK_EQ(z.size(), 78000u);
    const Part p = measure(z, 10000, 68000, 441.0);
    CHECK(std::fabs(level(p)) < 0.3 && under(p) > 30.0);
}

TEST(offline_resample_changes_speed_and_pitch_together) {
    const std::vector<int16_t> x = tone(48000, 0.5, 1000.0);
    const std::vector<int16_t> lo = run(x, sc::ProcOp::Resample, 2000000);    // twice as long: an octave down
    CHECK_EQ(lo.size(), 96000u);
    const Part a = measure(lo, 4800, 91200, 500.0);
    const std::vector<int16_t> hi = run(x, sc::ProcOp::Resample, 500000);     // half as long: an octave up
    CHECK_EQ(hi.size(), 24000u);
    const Part b = measure(hi, 2400, 21600, 2000.0);
    std::printf("    resample: x2 %+.3f dB, rest %.1f dB under; x0.5 %+.3f dB, rest %.1f dB under\n", level(a), under(a),
                level(b), under(b));
    CHECK(std::fabs(level(a)) < 0.05 && under(a) > 60.0);
    CHECK(std::fabs(level(b)) < 0.05 && under(b) > 60.0);
    // the top of the band survives a slow-down, and what would fold over on a speed-up is filtered first
    const std::vector<int16_t> top = run(tone(48000, 0.5, 15000.0), sc::ProcOp::Resample, 1500000);
    const Part c = measure(top, 7200, 64800, 10000.0);
    const std::vector<int16_t> fold = run(tone(48000, 0.5, 15000.0), sc::ProcOp::Resample, 500000);   // 15 kHz would land on 30 kHz = 18 kHz folded
    const Part d = measure(fold, 2400, 21600, 18000.0);
    std::printf("    resample: 15 kHz slowed to 10 kHz %+.2f dB; 15 kHz sped up x2 leaves %.1f dB at the folded 18 kHz\n", level(c), tst::db(d.top / 16383.5));
    CHECK(std::fabs(level(c)) < 0.5);
    CHECK(tst::db(d.top / 16383.5) < -25.0);
    sc::SmpHeader in, out;
    in.loop_start = 1000; in.loop_end = 3000; in.loop_mode = sc::SMP_LOOP_FWD;
    run(x, sc::ProcOp::Resample, 500000, &out, in);
    CHECK(out.loop_start == 500 && out.loop_end == 1500);
}

TEST(offline_pitch_shifts_and_keeps_the_length) {
    struct Case { int32_t cents; double hz; };
    const Case cases[] = {{1200, 440.0}, {-1200, 440.0}, {-700, 440.0}, {300, 110.0}, {0, 440.0}, {2400, 220.0}, {-2400, 880.0}};
    for (const Case &c : cases) {
        const std::vector<int16_t> x = tone(96000, 0.5, c.hz);
        sc::SmpHeader in, out;
        in.loop_start = 1000; in.loop_end = 3000; in.loop_mode = sc::SMP_LOOP_FWD;
        const std::vector<int16_t> y = run(x, sc::ProcOp::Pitch, c.cents, &out, in);
        CHECK_EQ(y.size(), x.size());
        CHECK(out.loop_start == 1000 && out.loop_end == 3000);
        const double want = c.hz * std::pow(2.0, c.cents / 1200.0);
        const Part p = measure(y, 12000, 84000, want);
        std::printf("    pitch %+d cents of %.0f Hz -> %.1f Hz: level %+.2f dB, everything else %.1f dB under it\n", c.cents, c.hz, want, level(p),
                    under(p));
        CHECK(std::fabs(level(p)) < 0.3);
        CHECK(under(p) > 30.0);
    }
}

/* ---------------- Crop, and color / lo-fi: Decimate, Bits, Drive, the filters ---------------- */

TEST(offline_crop_keeps_a_region_and_moves_loop_and_slices) {
    std::vector<int16_t> ramp(30000);
    for (size_t i = 0; i < ramp.size(); i++) ramp[i] = static_cast<int16_t>(i);
    sc::SmpHeader in, out;
    in.loop_start = 12000; in.loop_end = 18000; in.loop_mode = sc::SMP_LOOP_FWD;
    in.slice_count = 3; in.slice[0] = 0; in.slice[1] = 15000; in.slice[2] = 25000;
    const std::vector<int16_t> y = run(ramp, sc::ProcOp::Crop, 10000, &out, in, 48000, 20000);
    CHECK_EQ(y.size(), 10000u);
    CHECK(y[0] == 10000 && y[9999] == 19999);
    CHECK(out.loop_start == 2000 && out.loop_end == 8000);
    CHECK(out.slice_count == 1 && out.slice[0] == 5000);
    CHECK_EQ(run(ramp, sc::ProcOp::Crop, 25000).size(), 5000u);               // no end given: to the file's end
    CHECK_EQ(run(ramp, sc::ProcOp::Crop, 5000, &out, in, 48000, 14000).size(), 9000u);
    CHECK_EQ(out.loop_mode, sc::SMP_LOOP_OFF);                                // the loop is cut through: it goes
}

TEST(offline_lofi_rate_bits_and_drive) {
    const std::vector<int16_t> x = tone(48000, 0.5, 300.0);
    const std::vector<int16_t> d = run(x, sc::ProcOp::Decimate, 6000);       // held 8 frames at a time
    bool held = true;
    for (size_t i = 0; i < 4000; i += 8) for (size_t k = 1; k < 8; k++) held &= d[i + k] == d[i];
    CHECK(held);
    CHECK_EQ(d[800], x[800]);                                               // and each held value is the sample's own
    const std::vector<int16_t> b = run(x, sc::ProcOp::Bits, 6);              // 6 bits: steps of 1024
    bool stepped = true;
    int worst = 0;
    for (size_t i = 0; i < b.size(); i++) { stepped &= b[i] % 1024 == 0; worst = std::max(worst, std::abs(b[i] - x[i])); }
    CHECK(stepped);
    CHECK(worst <= 512);
    CHECK(run(x, sc::ProcOp::Bits, 16) == x);                                 // 16 bits: unchanged
    // Drive: full scale stays full scale, what is under it comes up, and nothing wraps
    const std::vector<int16_t> full = tone(48000, 1.0, 300.0), quiet = tone(48000, 0.1, 300.0);
    const std::vector<int16_t> df = run(full, sc::ProcOp::Drive, 240), dq = run(quiet, sc::ProcOp::Drive, 240);
    std::printf("    drive +24 dB: full scale %d -> %d, a -20 dB tone %d -> %d\n", peak(full), peak(df), peak(quiet), peak(dq));
    CHECK(peak(df) >= 32700);
    CHECK(peak(dq) > 8 * peak(quiet) && peak(dq) < 32767);                   // (+24 dB is x15.8: most of it arrives, bent)
    for (size_t i = 1; i < df.size(); i++) CHECK(!(df[i - 1] > 30000 && df[i] < -30000));
    const std::vector<int16_t> d0 = run(quiet, sc::ProcOp::Drive, 0);        // no drive: a quiet tone passes almost as it is
    CHECK(std::abs(peak(d0) - static_cast<int>(peak(quiet) / 0.7778)) <= 40);   // (the curve's own slope: 1 / soft(1))
}

TEST(offline_filters_are_second_order_butterworth) {
    struct Case { sc::ProcOp op; double hz, want; };
    const Case cases[] = {{sc::ProcOp::LowPass, 250.0, -0.017}, {sc::ProcOp::LowPass, 1000.0, -3.01}, {sc::ProcOp::LowPass, 2000.0, -12.3}, {sc::ProcOp::LowPass, 4000.0, -24.1},
                          {sc::ProcOp::HighPass, 4000.0, -0.017}, {sc::ProcOp::HighPass, 1000.0, -3.01}, {sc::ProcOp::HighPass, 500.0, -12.3}, {sc::ProcOp::HighPass, 250.0, -24.1}};
    for (const Case &c : cases) {
        const std::vector<int16_t> y = run(tone(48000, 0.5, c.hz), c.op, 1000);   // the cutoff at 1 kHz
        const Part p = measure(y, 12000, 44000, c.hz);
        std::printf("    %s 1 kHz at %.0f Hz: %+.2f dB (ideal %+.2f)\n", c.op == sc::ProcOp::LowPass ? "low-pass" : "high-pass", c.hz, level(p), c.want);
        CHECK(std::fabs(level(p) - c.want) < 0.5);
    }
    std::vector<int16_t> dc(48000, 8000);                                    // a high-pass removes a constant
    const std::vector<int16_t> h = run(dc, sc::ProcOp::HighPass, 100);
    CHECK(std::abs(h[47000]) <= 1);
}

/* ---------------- the spectral methods: the 4096-point FFT, the Tonal stretch (phase vocoder), paulstretch ---------------- */
#include "engine/offline/rfft.h"

TEST(offline_rfft_matches_a_direct_transform_and_inverts) {
    const int N = sc::kRfftN;
    std::vector<float> t(N / 4 + 1), x(N + 2);
    std::vector<double> ref(N);
    sc::rfft_table(t.data());
    uint32_t rng = 12345;
    for (int n = 0; n < N; n++) {                                             // two tones (one between bins) and noise
        rng = rng * 1664525u + 1013904223u;
        ref[n] = std::sin(6.283185307179586 * 37.3 * n / N) + 0.5 * std::cos(6.283185307179586 * 801.0 * n / N) + ((rng >> 8) / 16777216.0 - 0.5);
        x[n] = static_cast<float>(ref[n]);
    }
    sc::rfft_forward(x.data(), t.data());
    double worst = 0, big = 0;
    for (int k : {0, 1, 2, 37, 38, 500, 801, 1024, 1025, 2047, 2048}) {
        double re = 0, im = 0;
        for (int n = 0; n < N; n++) { re += ref[n] * std::cos(6.283185307179586 * n * k / N); im -= ref[n] * std::sin(6.283185307179586 * n * k / N); }
        worst = std::max(worst, std::max(std::fabs(x[2 * k] - re), std::fabs(x[2 * k + 1] - im)));
        big = std::max(big, std::hypot(re, im));
    }
    sc::rfft_inverse(x.data(), t.data());
    double back = 0;
    for (int n = 0; n < N; n++) back = std::max(back, std::fabs(x[n] / (N / 2) - ref[n]));
    std::printf("    rfft 4096: worst bin error %.2e (largest bin %.0f), round trip error %.2e\n", worst, big, back);
    CHECK(worst < 2e-3 * big / 1000.0 + 5e-3);
    CHECK(back < 2e-5);
}

TEST(offline_tonal_stretch_keeps_tones_and_a_chord) {
    std::vector<int16_t> x(60000);
    for (size_t i = 0; i < x.size(); i++) {
        const double t = static_cast<double>(i) / 48000.0;
        x[i] = static_cast<int16_t>(9000.0 * (0.4 + 0.6 * t) * (std::sin(6.283185307 * 440.0 * t) + 0.7 * std::sin(6.283185307 * 617.3 * t)));
    }
    const std::vector<int16_t> same = run(x, sc::ProcOp::StretchTonal, 1000000);   // x1: the windows add up to the sample again
    CHECK_EQ(same.size(), x.size());
    int worst = 0;
    for (size_t i = 0; i < same.size() && i < x.size(); i++) worst = std::max(worst, std::abs(same[i] - x[i]));
    std::printf("    tonal stretch x1: largest difference %d\n", worst);
    CHECK(worst <= 3);
    struct Case { double hz; int32_t ppm; };
    const Case cases[] = {{440.0, 1500000}, {440.0, 500000}, {440.0, 730000}, {55.0, 2000000}, {3000.0, 1250000}, {110.0, 4000000}};
    for (const Case &c : cases) {
        const std::vector<int16_t> y = run(tone(96000, 0.5, c.hz), sc::ProcOp::StretchTonal, c.ppm);
        CHECK_EQ(y.size(), static_cast<size_t>(96000.0 * c.ppm / 1e6 + 0.5));
        const Part p = measure(y, y.size() / 8, y.size() * 7 / 8, c.hz);
        std::printf("    tonal stretch x%.2f of %.0f Hz: level %+.2f dB, everything else %.1f dB under it\n", c.ppm / 1e6, c.hz, level(p), under(p));
        CHECK(std::fabs(level(p)) < 0.3);
        CHECK(under(p) > (c.hz < 200.0 ? 30.0 : 40.0));                       // (under 200 Hz the 4096-frame measure itself stops near 35 dB)
    }
    // a chord: every note keeps its level (the weakest window of each, against the 0.2 it had)
    std::vector<int16_t> chord(96000);
    const double notes[3] = {220.0, 277.18, 329.63};
    for (size_t i = 0; i < chord.size(); i++) {
        double v = 0;
        for (double hz : notes) v += 0.2 * std::sin(6.283185307 * hz * static_cast<double>(i) / 48000.0);
        chord[i] = static_cast<int16_t>(32767.0 * v);
    }
    const std::vector<int16_t> c15 = run(chord, sc::ProcOp::StretchTonal, 1500000);
    for (double hz : notes) {
        const Part p = measure(c15, 18000, 126000, hz);
        std::printf("    tonal stretch x1.5 of a chord, %.0f Hz: %+.2f .. %+.2f dB\n", hz, tst::db(p.amp / 6553.4), tst::db(p.top / 6553.4));
        CHECK(std::fabs(tst::db(p.amp / 6553.4)) < 0.5 && std::fabs(tst::db(p.top / 6553.4)) < 0.5);
    }
    sc::SmpHeader in, out;
    in.loop_start = 1000; in.loop_end = 3000; in.loop_mode = sc::SMP_LOOP_FWD;
    run(chord, sc::ProcOp::StretchTonal, 2000000, &out, in);
    CHECK(out.loop_start == 2000 && out.loop_end == 6000);
}

TEST(offline_tonal_pitch_shifts_and_keeps_the_length) {
    struct Case { int32_t cents; double hz; };
    const Case cases[] = {{1200, 440.0}, {-1200, 440.0}, {-700, 440.0}, {300, 110.0}, {0, 440.0}};
    for (const Case &c : cases) {
        const std::vector<int16_t> x = tone(96000, 0.5, c.hz);
        const std::vector<int16_t> y = run(x, sc::ProcOp::PitchTonal, c.cents);
        CHECK_EQ(y.size(), x.size());
        const double want = c.hz * std::pow(2.0, c.cents / 1200.0);
        const Part p = measure(y, 12000, 84000, want);
        std::printf("    tonal pitch %+d cents of %.0f Hz -> %.1f Hz: level %+.2f dB, everything else %.1f dB under it\n", c.cents, c.hz, want, level(p), under(p));
        CHECK(std::fabs(level(p)) < 0.3);
        CHECK(under(p) > 40.0);
    }
}

TEST(offline_paulstretch_smears_a_tone_into_a_band_at_the_same_pitch) {
    const std::vector<int16_t> x = tone(48000, 0.5, 440.0);
    sc::SmpHeader in, out;
    in.loop_start = 1000; in.loop_end = 3000; in.loop_mode = sc::SMP_LOOP_FWD;
    const std::vector<int16_t> y = run(x, sc::ProcOp::Paul, 4000000, &out, in);   // 4 times as long
    CHECK_EQ(y.size(), 192000u);
    CHECK(out.loop_start == 4000 && out.loop_end == 12000);
    double e = 0;
    for (size_t i = 24000; i < 168000; i++) e += static_cast<double>(y[i]) * y[i];
    const double rms = std::sqrt(e / 144000.0), in_rms = 16383.5 / 1.41421356;
    // where the energy lies: the spectrum of 4096 frames from the middle, the bins around 440 Hz (bin 37.5) against all
    const int N = sc::kRfftN;
    std::vector<float> t(N / 4 + 1), f(N + 2);
    sc::rfft_table(t.data());
    for (int n = 0; n < N; n++) f[n] = static_cast<float>(y[90000 + n]) * (0.5f - 0.5f * static_cast<float>(std::cos(6.283185307 * n / N)));
    sc::rfft_forward(f.data(), t.data());
    double near = 0, all = 0;
    for (int k = 0; k <= N / 2; k++) { const double pw = static_cast<double>(f[2 * k]) * f[2 * k] + static_cast<double>(f[2 * k + 1]) * f[2 * k + 1]; all += pw; if (k >= 33 && k <= 42) near += pw; }
    const Part p = measure(y, 24000, 168000, 440.0);                        // a steady tone would give the same amplitude in every window
    std::printf("    paulstretch x4 of 440 Hz: level %+.2f dB against the tone, %.1f %% of the energy within 60 Hz of it, peak %d, windows %.2f .. %.2f of the tone\n",
                tst::db(rms / in_rms), 100.0 * near / all, peak(y), p.amp / 16383.5, p.top / 16383.5);
    CHECK(std::fabs(tst::db(rms / in_rms) + 6.0) < 1.5);                     // 6 dB under the source, by design
    CHECK(near / all > 0.97);
    CHECK(peak(y) < 32767);                                                  // and the peaks had the room
    CHECK(p.top / p.amp > 1.5);                                              // not a steady tone any more
    std::printf("    working memory: stretch %zu, pitch %zu, resample %zu, tonal stretch %zu, tonal pitch %zu, paulstretch %zu bytes; the processor %zu\n",
                sc::Processor::work_bytes(sc::ProcOp::Stretch), sc::Processor::work_bytes(sc::ProcOp::Pitch), sc::Processor::work_bytes(sc::ProcOp::Resample),
                sc::Processor::work_bytes(sc::ProcOp::StretchTonal), sc::Processor::work_bytes(sc::ProcOp::PitchTonal), sc::Processor::work_bytes(sc::ProcOp::Paul),
                sizeof(sc::Processor));
    CHECK_EQ(sc::Processor::work_bytes(sc::ProcOp::Gain), 0u);
}
