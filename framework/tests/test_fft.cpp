#include <cstdlib>
#include "rig.h"
#include "engine/dsp/cordic.h"
#include "engine/dsp/delay.h"
#include "engine/dsp/fft.h"
#include "engine/dsp/fft_ref.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/stft.h"

using namespace sc;
using namespace tst;

namespace {
const double kPi = 3.14159265358979323846;
const int N = kFftN;

struct Lcg {
    uint32_t s = 12345;
    int next(int range) { s = s * 1664525u + 1013904223u; return static_cast<int>((s >> 8) % (2u * range + 1u)) - range; }
};

void dft(const std::vector<double> &xr, const std::vector<double> &xi, std::vector<double> &yr, std::vector<double> &yi) {
    yr.assign(N, 0); yi.assign(N, 0);
    for (int k = 0; k < N; k++)
        for (int n = 0; n < N; n++) {
            double a = -2 * kPi * static_cast<double>((static_cast<long long>(k) * n) % N) / N;
            yr[k] += xr[n] * std::cos(a) - xi[n] * std::sin(a);
            yi[k] += xr[n] * std::sin(a) + xi[n] * std::cos(a);
        }
}
}  // namespace

TEST(delay_line_reads_and_interpolates) {
    std::vector<uint8_t> mem(1 << 16);
    Heap h;
    h.init(mem.data(), mem.size());
    {
        DelayLine d;
        CHECK(d.init(h, 100));
        CHECK(d.max_delay() >= 100);
        for (int i = 1; i <= 50; i++) d.write(static_cast<q15>(i * 100));
        CHECK_EQ(d.read(1), 5000);                                        // the previous input
        CHECK_EQ(d.read(10), 4100);
        CHECK_EQ(d.read_lerp((10u << 16) | 0x8000u), 4050);               // halfway between delay 10 (4100) and 11 (4000)
        // Hermite reproduces a slowly varying sine far better than the sample grid allows
        DelayLine s;
        CHECK(s.init(h, 64));
        for (int i = 0; i < 64; i++) s.write(static_cast<q15>(std::lround(20000 * std::sin(2 * kPi * i / 16.0))));
        double ref = 20000 * std::sin(2 * kPi * (64 - 10.37) / 16.0);
        double eh = std::fabs(s.read_hermite(static_cast<uint32_t>(10.37 * 65536)) - ref);
        double el = std::fabs(s.read_lerp(static_cast<uint32_t>(10.37 * 65536)) - ref);
        CHECK(eh < 20);
        CHECK(eh < el);
    }
    CHECK_EQ(h.used(), 0);                                                // destructors released the lines
    CHECK(h.check());
}

TEST(comb_decays_by_feedback_and_damping_darkens) {
    std::vector<uint8_t> mem(1 << 16);
    Heap h;
    h.init(mem.data(), mem.size());
    Comb c;
    CHECK(c.init(h, 100));
    c.set_feedback(16384);                                                // 0.5
    c.set_damp(0);
    std::vector<int> out;
    for (int i = 0; i < 500; i++) out.push_back(c.process(i == 0 ? 20000 : 0));
    CHECK_EQ(out[100], 20000);
    CHECK_NEAR(out[200], 10000, 2);
    CHECK_NEAR(out[300], 5000, 2);

    Comb d;                                                               // damped: HF dies faster than LF
    CHECK(d.init(h, 64));
    d.set_feedback(28000);
    d.set_damp(q31_from_float(0.6));
    double e_lf = 0, e_hf = 0;
    for (int pass = 0; pass < 2; pass++) {
        d.clear();
        uint32_t ph = 0, inc = hz_to_inc(pass ? 9000.0f : 300.0f);
        for (int i = 0; i < 2000; i++, ph += inc) d.process(i < 64 ? sine(ph) / 2 : 0);
        double e = 0;
        for (int i = 0; i < 640; i++) { double v = d.process(0); e += v * v; }
        (pass ? e_hf : e_lf) = e;
    }
    CHECK(e_hf < e_lf * 0.1);
}

TEST(allpass_has_flat_magnitude) {
    std::vector<uint8_t> mem(1 << 16);
    Heap h;
    h.init(mem.data(), mem.size());
    Allpass a;
    CHECK(a.init(h, 113));
    a.set_gain(19660);                                                    // 0.6
    for (float f : {200.0f, 1000.0f, 5000.0f, 12000.0f}) {
        a.clear();
        uint32_t ph = 0, inc = hz_to_inc(f);
        double ei = 0, eo = 0;
        for (int i = 0; i < 20000; i++, ph += inc) {
            q15 x = static_cast<q15>(sine(ph) / 4);
            q15 y = a.process(x);
            if (i > 10000) { ei += static_cast<double>(x) * x; eo += static_cast<double>(y) * y; }
        }
        CHECK_NEAR(db(std::sqrt(eo / ei)), 0.0, 0.3);
    }
}

TEST(fft_matches_the_double_dft) {
    Lcg r;
    std::vector<double> xr(N), xi(N), yr, yi;
    int16_t re[N], im[N];
    for (int i = 0; i < N; i++) { re[i] = static_cast<int16_t>(r.next(8000)); im[i] = static_cast<int16_t>(r.next(8000)); xr[i] = re[i]; xi[i] = im[i]; }
    dft(xr, xi, yr, yi);
    int e = fft_q15(re, im);
    std::vector<double> sr(N), si(N), er(N), ei(N);
    for (int k = 0; k < N; k++) { er[k] = yr[k]; ei[k] = yi[k]; sr[k] = re[k] * std::ldexp(1.0, e); si[k] = im[k] * std::ldexp(1.0, e); }
    double snr = (snr_db(er, sr) + snr_db(ei, si)) / 2;
    std::printf("    FFT vs double DFT: exponent %d, SNR %.1f dB\n", e, snr);
    CHECK(snr > 60.0);
}

TEST(fft_is_bit_identical_to_the_reference) {
    Lcg r;
    int cases = 0, bad = 0;
    for (int amp : {0, 1, 3, 20, 300, 4000, 13500, 13501, 20000, 32767}) {
        for (int kind = 0; kind < 5; kind++) {
            int16_t re[N], im[N], rr[N], ri[N];
            for (int i = 0; i < N; i++) {
                int a = 0, b = 0;
                switch (kind) {
                case 0: a = r.next(amp); b = r.next(amp); break;                                  // complex noise
                case 1: a = r.next(amp); break;                                                    // real noise (the mono STFT)
                case 2: a = (i & 16) ? amp : -amp - 1; b = -amp - 1; break;                        // full-scale square, -32768 included
                case 3: a = i == 5 ? -amp - 1 : 0; break;                                          // impulse
                default: a = static_cast<int>(std::lround(amp * std::sin(2 * kPi * 37.3 * i / N))); b = r.next(amp / 2 + 1); break;
                }
                re[i] = rr[i] = static_cast<int16_t>(a < -32768 ? -32768 : a);
                im[i] = ri[i] = static_cast<int16_t>(b < -32768 ? -32768 : b);
            }
            for (int inv = 0; inv < 2; inv++) {
                const int e = inv ? ifft_q15(re, im) : fft_q15(re, im);
                const int er = inv ? ifft_q15_ref(rr, ri) : fft_q15_ref(rr, ri);
                bool same = e == er;
                for (int i = 0; i < N; i++) same = same && re[i] == rr[i] && im[i] == ri[i];
                cases++;
                if (!same) { bad++; std::printf("    differs: amplitude %d kind %d inverse %d\n", amp, kind, inv); }
            }
        }
    }
    std::printf("    %d FFT / IFFT cases, %d differ from the reference\n", cases, bad);
    CHECK_EQ(bad, 0);
}

TEST(fft_block_floating_point_keeps_precision_on_quiet_signals) {
    Lcg r;
    for (int amp : {20, 300, 12000}) {
        std::vector<double> xr(N), xi(N, 0.0), yr, yi;
        int16_t re[N], im[N];
        for (int i = 0; i < N; i++) { re[i] = static_cast<int16_t>(r.next(amp)); im[i] = 0; xr[i] = re[i]; }
        dft(xr, xi, yr, yi);
        int e = fft_q15(re, im);
        std::vector<double> sr(N), si(N);
        for (int k = 0; k < N; k++) { sr[k] = re[k] * std::ldexp(1.0, e); si[k] = im[k] * std::ldexp(1.0, e); }
        double snr = (snr_db(yr, sr) + snr_db(yi, si)) / 2;
        std::printf("    amplitude %5d: exponent %d, SNR %.1f dB\n", amp, e, snr);
        CHECK(snr > (amp < 100 ? 35.0 : 55.0));                       // few LSB signals are limited by the input quantisation itself
    }
}

TEST(fft_finds_a_bin_centred_sine_and_inverts) {
    int16_t re[N], im[N], orig[N];
    for (int i = 0; i < N; i++) { orig[i] = re[i] = static_cast<int16_t>(std::lround(16000 * std::sin(2 * kPi * 17 * i / N))); im[i] = 0; }
    int e = fft_q15(re, im);
    int peak = 0;
    for (int k = 1; k < N / 2; k++) if (std::abs(re[k]) + std::abs(im[k]) > std::abs(re[peak]) + std::abs(im[peak])) peak = k;
    CHECK_EQ(peak, 17);
    CHECK_NEAR(std::hypot(re[17], im[17]) * std::ldexp(1.0, e), 16000.0 * N / 2, 16000.0 * N / 2 * 0.01);

    int e2 = ifft_q15(re, im);                                           // round trip: x * N
    std::vector<double> ref(N), got(N);
    for (int i = 0; i < N; i++) { ref[i] = orig[i]; got[i] = re[i] * std::ldexp(1.0, e + e2) / N; }
    double snr = snr_db(ref, got);
    std::printf("    FFT + IFFT round trip: SNR %.1f dB\n", snr);
    CHECK(snr > 55.0);
}

TEST(cordic_polar_matches_atan2) {
    Lcg r;
    double worst_deg = 0, worst_mag = 0;
    for (int t = 0; t < 20000; t++) {
        int x = r.next(20000), y = r.next(20000);
        if (std::abs(x) + std::abs(y) < 200) continue;
        uint32_t ph, mag;
        cordic_polar(x, y, ph, mag);
        double want = std::atan2(static_cast<double>(y), static_cast<double>(x)) / (2 * kPi);
        if (want < 0) want += 1.0;
        double got = ph / 4294967296.0;
        double d = std::fabs(got - want);
        if (d > 0.5) d = 1.0 - d;
        worst_deg = std::fmax(worst_deg, d * 360.0);
        double hyp = std::hypot(static_cast<double>(x), static_cast<double>(y));
        worst_mag = std::fmax(worst_mag, std::fabs(mag - hyp) - 0.0005 * hyp);          // allowed: 0.05 % plus output rounding
        int16_t rr, ii;
        polar_to_rect(mag, ph, rr, ii);
        CHECK(std::abs(rr - x) <= 2 + std::abs(x) / 800 && std::abs(ii - y) <= 2 + std::abs(y) / 800);
    }
    std::printf("    CORDIC worst phase error %.4f deg, magnitude error beyond 0.05 %% : %.3f LSB\n", worst_deg, worst_mag);
    CHECK(worst_deg < 0.02);
    CHECK(worst_mag < 0.6);                                              // i.e. within rounding of the output
}

TEST(stft_reconstructs_perfectly_with_a_fixed_latency) {
    std::vector<uint8_t> mem(1 << 16);
    Heap h;
    h.init(mem.data(), mem.size());
    Stft st;
    CHECK(st.init(h, false));
    Lcg r;
    const int total = 20480;                                          // a multiple of every supported block size
    std::vector<q15> in(total), out(total);
    for (int i = 0; i < total; i++) in[i] = static_cast<q15>(r.next(8000));
    for (int i = 0; i < total; i += kBlock) st.process(&in[i], nullptr, &out[i], kBlock, [](StftFrame &) {});
    // find the delay that best aligns the output with the input
    int best = 0;
    double best_snr = -1e9;
    for (int lag = 0; lag < 1200; lag++) {
        std::vector<double> a, b;
        for (int i = 3000; i < 6000; i++) { a.push_back(in[i - lag]); b.push_back(out[i]); }
        double s = snr_db(a, b);
        if (s > best_snr) { best_snr = s; best = lag; }
    }
    std::printf("    STFT identity: best lag %d samples (declared %d), SNR %.1f dB\n", best, Stft::latency(), best_snr);
    CHECK_EQ(best, Stft::latency());
    CHECK(best_snr > 45.0);
}
