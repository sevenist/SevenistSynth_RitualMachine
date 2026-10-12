// The saturation modes and the newer effects (modulation, dynamics, EQ, convolution, resonators).
#include <algorithm>
#include <vector>
#include "rig.h"

using namespace tst;

namespace {
const double kTwoPi = 6.28318530717958647692;

double power_at(const std::vector<double> &x, size_t from, size_t to, double hz) {
    double re = 0, im = 0;
    for (size_t i = from; i < to && i < x.size(); i++) { const double ph = kTwoPi * hz * static_cast<double>(i) / kSampleRate; re += x[i] * std::cos(ph); im += x[i] * std::sin(ph); }
    const double n = static_cast<double>(to - from);
    return (re * re + im * im) / (n * n);
}
double peak_of(const std::vector<double> &x) { double p = 0; for (double v : x) p = std::fmax(p, std::fabs(v)); return p; }

// global sine (1 kHz, level) -> shaper -> master
std::vector<double> shape(int mode, int drive_q8, double level = 0.5, int bits = 8) {
    DspRig rig;
    GraphDesc g;
    NodeDesc *o = rig.add(g, 1, T_OSC_G);
    o->param[OSC_WAVE] = WAVE_SINE_; o->param[OSC_PITCH] = hz_to_pitch(1000.0); o->param[OSC_LEVEL] = static_cast<int32_t>(level * 32767);
    NodeDesc *s = rig.add(g, 2, T_SHAPER_G);
    s->param[SHP_MODE] = mode; s->param[SHP_DRIVE] = drive_q8; s->param[SHP_BITS] = bits;
    rig.add(g, 3, T_MASTER_OUT);
    g.connect(1, 0, 2, Dst::In, 0);
    g.connect(2, 0, 3, Dst::In, 0);
    CHECK(rig.eng.load(g) == Err::Ok);
    std::vector<double> y;
    rig.run(DspRig::blocks_for(0.3), &y);
    return y;
}
}  // namespace

TEST(saturation_modes_have_their_harmonic_signatures) {
    const size_t a = static_cast<size_t>(kSampleRate / 10), z = static_cast<size_t>(kSampleRate / 5);                                           // whole cycles of 1 kHz
    for (int m = 0; m < SHPM_N; m++) {
        std::vector<double> y = shape(m, 2 * 256);                              // 2 octaves of drive (4x)
        CHECK(peak_of(y) <= 32767.0);                                           // (a hard clipper reaches full scale)
        CHECK(peak_of(y) > 2000.0);
    }
    auto h = [&](int mode, double hz) { return power_at(shape(mode, 2 * 256), a, z, hz); };
    const double clip2 = h(SHPM_CLIP, 2000), clip3 = h(SHPM_CLIP, 3000);
    const double tube2 = h(SHPM_TUBE, 2000), tube1 = h(SHPM_TUBE, 1000);
    const double dio2 = h(SHPM_DIODE, 2000);
    const double ch2 = h(SHPM_CHEB, 2000), ch3 = h(SHPM_CHEB, 3000), ch5 = h(SHPM_CHEB, 5000);
    const double rc1 = h(SHPM_RECT, 1000), rc2 = h(SHPM_RECT, 2000);
    std::printf("    clip: 2f %.2e, 3f %.2e | tube: f %.2e, 2f %.2e | diode 2f %.2e | cheb: 2f %.2e 3f %.2e 5f %.2e | rect: f %.2e, 2f %.2e\n",
                clip2, clip3, tube1, tube2, dio2, ch2, ch3, ch5, rc1, rc2);
    CHECK(clip3 > 1000.0 * (clip2 + 1.0));                                      // a symmetric clipper has odd harmonics only
    CHECK(tube2 > 1000.0 * (clip2 + 1.0));                                      // the biased tube adds even ones
    CHECK(dio2 > 1000.0 * (clip2 + 1.0));
    CHECK(ch3 > 100.0 * (ch2 + 1.0) && ch5 > 100.0 * (ch2 + 1.0));              // Chebyshev T3 + T5: odd only
    CHECK(rc2 > 30.0 * (rc1 + 1.0));                                            // full-wave rectifier: the octave up, no fundamental
}

TEST(tape_rolls_off_the_top_and_decimate_holds_samples) {
    std::vector<double> tanh_y = shape(SHPM_TANH, 2 * 256), tape = shape(SHPM_TAPE, 2 * 256);
    const size_t a = static_cast<size_t>(kSampleRate / 10), z = static_cast<size_t>(kSampleRate / 5);
    const double r_tanh = power_at(tanh_y, a, z, 7000) + 1.0, r_tape = power_at(tape, a, z, 7000) + 1.0;
    std::printf("    tape vs tanh at 7 kHz (harmonic of a 1 kHz sine): %.1f dB\n", 10.0 * std::log10(r_tape / r_tanh));
    CHECK(r_tape < 0.7 * r_tanh);
    // decimate by 8x: the held steps repeat 8 samples at a time
    std::vector<double> d = shape(SHPM_DECIM, 3 * 256, 0.5, 15);
    int equal = 0;
    for (size_t i = 8001; i < 9000; i++) equal += d[i] == d[i - 1];
    CHECK(equal > 800);                                                         // 7 of every 8 samples repeat the previous one
}
