// Dev-only micro benchmark (build flag HWV1_BENCH): measures what the engine's inner loops cost on this chip, in CPU cycles per iteration,
// with the data in internal RAM and in PSRAM. Runs once at boot from audio_init(); the result is printed on the serial console.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1_BENCH)
#include <Arduino.h>
#include <esp_cpu.h>
#include <cstring>
#include <esp_heap_caps.h>
#include "engine/dsp/interp.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/q.h"
#include "engine/dsp/svf.h"
#include "engine/dsp/tables.h"
#include "engine/dsp/fft.h"
#include "engine/dsp/fft_ref.h"
#include "engine/dsp/stft.h"
#include "engine/dsp/cordic.h"
#include "engine/core/heap.h"
#include "platform/esp32/bench_esp32.h"
#include <cmath>
#include "dsps_fft2r.h"

namespace {
constexpr int kIter = 8192;
volatile int32_t sink;

template <typename F>
uint32_t cycles_per_iter(F f) {
    f();                                                   // warm the caches
    const uint32_t t0 = esp_cpu_get_cycle_count();
    f();
    return (esp_cpu_get_cycle_count() - t0) / kIter;
}

// ---- FFT candidates (512 complex). Each one has fft_q15's contract: int16 re / im in place, natural order, returns the
// exponent (true = stored * 2^exp), so "full" cycles include the packing an integration into Stft would need.
constexpr int FN = sc::kFftN;
uint32_t g_kernel;                                         // cycles of the library call alone, summed by the candidates
int16_t *g_s16;                                            // interleaved int16 work buffer (16-byte aligned for the PIE kernels)
float *g_f32;                                              // interleaved float work buffer (16-byte aligned)

void bitrev_pairs(uint32_t *d) {                           // our bit reversal table on interleaved (re, im) pairs
    for (int i = 0; i < FN; i++) { const int j = sc::kFftRev[i]; if (j > i) { const uint32_t t = d[i]; d[i] = d[j]; d[j] = t; } }
}

int cand_sc16(int16_t *re, int16_t *im) {                  // ESP-DSP int16 radix-2 (PIE SIMD); input pre-normalised to full scale
    int32_t mx = 1;
    for (int i = 0; i < FN; i++) { mx = std::max<int32_t>(mx, std::abs(re[i])); mx = std::max<int32_t>(mx, std::abs(im[i])); }
    int s = 0;
    while ((mx << (s + 1)) <= 32767) s++;
    for (int i = 0; i < FN; i++) { g_s16[2 * i] = static_cast<int16_t>(re[i] << s); g_s16[2 * i + 1] = static_cast<int16_t>(im[i] << s); }
    const uint32_t t0 = esp_cpu_get_cycle_count();
    dsps_fft2r_sc16(g_s16, FN);
    bitrev_pairs(reinterpret_cast<uint32_t *>(g_s16));
    g_kernel += esp_cpu_get_cycle_count() - t0;
    for (int i = 0; i < FN; i++) { re[i] = g_s16[2 * i]; im[i] = g_s16[2 * i + 1]; }
    return -s;                                             // plus the library's own fixed scaling (found by the gain fit)
}

int f32_out(int16_t *re, int16_t *im) {                    // float spectrum -> int16 + exponent
    float mx = 1.0f;
    for (int i = 0; i < 2 * FN; i++) mx = std::max(mx, std::fabs(g_f32[i]));
    int e = 0;
    while (mx > 32767.0f) { mx *= 0.5f; e++; }
    const float k = std::ldexp(1.0f, -e);
    for (int i = 0; i < FN; i++) { re[i] = static_cast<int16_t>(g_f32[2 * i] * k); im[i] = static_cast<int16_t>(g_f32[2 * i + 1] * k); }
    return e;
}

int cand_fc32_r2(int16_t *re, int16_t *im) {               // ESP-DSP float radix-2 (aes3)
    for (int i = 0; i < FN; i++) { g_f32[2 * i] = re[i]; g_f32[2 * i + 1] = im[i]; }
    const uint32_t t0 = esp_cpu_get_cycle_count();
    dsps_fft2r_fc32(g_f32, FN);
    dsps_bit_rev2r_fc32(g_f32, FN);
    g_kernel += esp_cpu_get_cycle_count() - t0;
    return f32_out(re, im);
}

// Accuracy: forward DFT in float (cos table) as the reference; SNR after the best real gain, so a candidate's unknown
// fixed scaling does not count as error.
void dft_ref(const int16_t *xr, const int16_t *xi, float *yr, float *yi, const float *ct) {
    for (int k = 0; k < FN; k++) {
        float sr = 0, si = 0;
        for (int n = 0; n < FN; n++) {
            const int idx = (k * n) & (FN - 1);
            const float c = ct[idx], s = ct[(idx - FN / 4) & (FN - 1)];         // sin = cos shifted by a quarter
            sr += xr[n] * c + xi[n] * s;
            si += xi[n] * c - xr[n] * s;
        }
        yr[k] = sr; yi[k] = si;
    }
}

// gain <= 0: the best real gain (fitted); otherwise that gain (2^exp: checks the exponent the candidate returned)
float snr_fit(const float *rr, const float *ri, const int16_t *orr, const int16_t *oi, double gain = 0) {
    double num = 0, den = 0;
    for (int k = 0; k < FN; k++) { num += rr[k] * static_cast<double>(orr[k]) + ri[k] * static_cast<double>(oi[k]); den += static_cast<double>(orr[k]) * orr[k] + static_cast<double>(oi[k]) * oi[k]; }
    if (den <= 0) return -99.0f;
    const double g = gain > 0 ? gain : num / den;
    double sig = 0, err = 0;
    for (int k = 0; k < FN; k++) {
        const double er = rr[k] - g * orr[k], ei = ri[k] - g * oi[k];
        sig += static_cast<double>(rr[k]) * rr[k] + static_cast<double>(ri[k]) * ri[k];
        err += er * er + ei * ei;
    }
    return static_cast<float>(10.0 * std::log10(sig / (err + 1e-30)));
}


void bench_fft() {
    using Fn = int (*)(int16_t *, int16_t *);
    g_s16 = static_cast<int16_t *>(heap_caps_aligned_alloc(16, FN * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    g_f32 = static_cast<float *>(heap_caps_aligned_alloc(16, FN * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    constexpr int kSig = 3;
    int16_t *in = static_cast<int16_t *>(heap_caps_malloc(kSig * 2 * FN * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    float *ref = static_cast<float *>(heap_caps_malloc(kSig * 2 * FN * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    float *ct = static_cast<float *>(heap_caps_malloc(FN * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    int16_t *wr = static_cast<int16_t *>(heap_caps_aligned_alloc(16, FN * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));   // 16-byte aligned like Stft's frame
    if (!g_s16 || !g_f32 || !in || !ref || !ct || !wr) { Serial.println("[BENCH] FFT: allocation failed"); return; }
    int16_t *wi = wr + FN;
    const esp_err_t e1 = dsps_fft2r_init_sc16(nullptr, FN), e2 = dsps_fft2r_init_fc32(nullptr, FN);
    if (e1 || e2) Serial.printf("[BENCH] FFT: ESP-DSP init errors sc16 %d fc32 %d\n", e1, e2);

    // signals: complex noise +-8000, real noise +-300 (quiet), real noise +-20 (very quiet): the tests' cases
    uint32_t seed = 12345;
    auto rnd = [&seed](int range) { seed = seed * 1664525u + 1013904223u; return static_cast<int16_t>(static_cast<int>((seed >> 8) % (2u * range + 1u)) - range); };
    const int amp[kSig] = {8000, 300, 20};
    for (int s = 0; s < kSig; s++)
        for (int i = 0; i < FN; i++) { in[s * 2 * FN + i] = rnd(amp[s]); in[s * 2 * FN + FN + i] = s == 0 ? rnd(amp[s]) : 0; }
    for (int i = 0; i < FN; i++) ct[i] = std::cos(2.0f * 3.14159265f * i / FN);
    for (int s = 0; s < kSig; s++) dft_ref(in + s * 2 * FN, in + s * 2 * FN + FN, ref + s * 2 * FN, ref + s * 2 * FN + FN, ct);

    struct Cand { const char *name; Fn fn; bool lib; };
    const Cand cands[] = {
        {"q15 BFP, first version       ", sc::fft_q15_ref, false},
        {"q15 BFP C, fused scan       ", sc::fft_q15_c, false},
        {"ESP-DSP sc16 aes3 (PIE)      ", cand_sc16, true},
        {"ESP-DSP fc32 radix-2 aes3    ", cand_fc32_r2, true},
        {"own PIE BFP (engine fft_q15)", sc::fft_q15, false},
    };
    Serial.printf("[BENCH] FFT %d complex, cycles per transform: full (int16 in -> int16 + exp out) / library kernel; SNR vs float DFT for noise 8000 cplx / 300 real / 20 real\n", FN);
    for (const Cand &c : cands) {
        constexpr int kReps = 8;
        uint32_t full = UINT32_MAX;                          // the fastest run: an interrupt inside a run only makes that run slower
        g_kernel = 0;
        for (uint32_t &v : sc::g_fft_pie_prof) v = 0;
        for (int r = 0; r < kReps + 1; r++) {               // the first run warms the caches and is not counted
            std::memcpy(wr, in, FN * 4);
            const uint32_t k0 = g_kernel, t0 = esp_cpu_get_cycle_count();
            c.fn(wr, wi);
            const uint32_t t = esp_cpu_get_cycle_count() - t0;
            if (r == 0) g_kernel = k0; else if (t < full) full = t;
        }
        const uint32_t kern = c.lib ? g_kernel / kReps : full;
        if (sc::g_fft_pie_prof[0]) {                        // the PIE FFT's pieces (all kReps + 1 runs, divided by the same)
            Serial.printf("  %s pieces: pack %u, stages", c.name, (unsigned)(sc::g_fft_pie_prof[0] / (kReps + 1)));
            for (int i = 1; i <= 9; i++) Serial.printf(" %u", (unsigned)(sc::g_fft_pie_prof[i] / (kReps + 1)));
            Serial.printf(", unpack %u\n", (unsigned)(sc::g_fft_pie_prof[10] / (kReps + 1)));
        }
        float snr[kSig], sx[kSig];
        for (int s = 0; s < kSig; s++) {
            std::memcpy(wr, in + s * 2 * FN, FN * 4);
            const int e = c.fn(wr, wi);
            snr[s] = snr_fit(ref + s * 2 * FN, ref + s * 2 * FN + FN, wr, wi);
            sx[s] = snr_fit(ref + s * 2 * FN, ref + s * 2 * FN + FN, wr, wi, std::ldexp(1.0, e));
            if (s == 0) Serial.printf("  %s full %6u  kernel %6u  exp %3d", c.name, (unsigned)full, (unsigned)kern, e);
        }
        std::memcpy(wr, in, FN * 4);                        // round trip on the loud complex noise: x -> X -> N x (inverse = swapped re / im)
        const int ea = c.fn(wr, wi), eb = c.fn(wi, wr);
        static float xr[FN], xi[FN];
        for (int i = 0; i < FN; i++) { xr[i] = in[i]; xi[i] = in[FN + i]; }
        const float rt = snr_fit(xr, xi, wr, wi), rtx = snr_fit(xr, xi, wr, wi, std::ldexp(1.0, ea + eb) / FN);
        Serial.printf("  SNR %5.1f / %5.1f / %5.1f dB, at exp %5.1f / %5.1f / %5.1f, round trip %5.1f (at exp %5.1f)\n", snr[0], snr[1], snr[2], sx[0], sx[1], sx[2], rt, rtx);
    }
    dsps_fft2r_deinit_sc16();
    dsps_fft2r_deinit_fc32();
    heap_caps_free(g_s16); heap_caps_free(g_f32); heap_caps_free(in); heap_caps_free(ref); heap_caps_free(ct); heap_caps_free(wr);

    {   // the spectral effects' frame engine (Stft, mono, identity callback) with the engine's FFT: cost per sample, identity SNR
        constexpr size_t kHeap = 16384;
        constexpr int kTotal = 8192, kBlk = 32;
        {   // what ee.vmul.s16 does after its SAR shift: round or truncate; and on overflow: saturate or wrap
            alignas(16) static int16_t pa[256], pb[256], po[256];
            for (int i = 0; i < 256; i++) { pa[i] = rnd(32767); pb[i] = rnd(32767); }
            pie_vmul_s16(pa, pb, po, 256, 15);
            int trunc = 0, round = 0;
            for (int i = 0; i < 256; i++) {
                const int32_t p = static_cast<int32_t>(pa[i]) * pb[i];
                trunc += po[i] == static_cast<int16_t>(p >> 15);
                round += po[i] == static_cast<int16_t>((p + (1 << 14)) >> 15);
            }
            for (int i = 0; i < 8; i++) { pa[i] = 32767; pb[i] = 32767; }
            pa[1] = pb[1] = -32768;
            pie_vmul_s16(pa, pb, po, 8, 0);
            const int16_t big = po[0];
            pie_vmul_s16(pa, pb, po, 8, 15);
            Serial.printf("[BENCH] ee.vmul.s16 >> 15: %d of 256 match truncation, %d match rounding; 32767^2 >> 0 gives %d, (-32768)^2 >> 15 gives %d (saturate: 32767)\n",
                          trunc, round, big, po[1]);
            // the PIE overlap-add against the C formula (stft.h), accumulators near saturation included
            alignas(16) static int32_t acc_p[512], acc_c[512];
            alignas(16) static int16_t sre[512];
            int bad = 0, cases = 0;
            for (int s : {16, 7, 1, 0, -1, -6, -15, -30}) {
                for (int i = 0; i < 512; i++) {
                    sre[i] = rnd(32767);
                    const int32_t a = (i % 7 == 0) ? (i & 8 ? INT32_MAX - rnd(30000) - 30000 : INT32_MIN + rnd(30000) + 30000) : rnd(30000) * 7001;
                    acc_p[i] = acc_c[i] = a;
                }
                if (s >= 0) pie_ola_shl(sre, sc::kStftWin, acc_p, 512, s); else pie_ola_shr(sre, sc::kStftWin, acc_p, 512, -s);
                for (int i = 0; i < 512; i++) {
                    int64_t v = (static_cast<int32_t>(sre[i]) * sc::kStftWin[i] + (1 << 14)) >> 15;
                    v = s >= 0 ? (v << s) : ((v + (1LL << (-s - 1))) >> -s);
                    acc_c[i] = sc::sat32(static_cast<int64_t>(acc_c[i]) + v);
                    bad += acc_p[i] != acc_c[i];
                    cases++;
                }
            }
            Serial.printf("[BENCH] PIE overlap-add vs C: %d of %d accumulators differ (shifts 16 .. -30, saturation included)\n", bad, cases);
            // the PIE CORDIC (cordic_polar_bins) against the scalar cordic_polar, extremes included; and its cost per bin
            alignas(16) static int16_t cre[260], cim[260];
            int cbad = 0;
            uint32_t ct_pie = UINT32_MAX, ct_c = UINT32_MAX;
            const int16_t ext[] = {-32768, -32767, -1, 0, 1, 32767};
            for (int rep = 0; rep < 40; rep++) {
                for (int i = 0; i < 260; i++) {
                    cre[i] = rep == 0 ? ext[i % 6] : rnd(rep & 1 ? 32767 : 300);
                    cim[i] = rep == 0 ? ext[(i / 6) % 6] : rnd(rep & 1 ? 32767 : 300);
                }
                uint32_t t0 = esp_cpu_get_cycle_count();
                sc::cordic_polar_bins(cre, cim, 257, [&](int k, uint32_t ph, uint32_t mg) {
                    uint32_t p2, m2;
                    sc::cordic_polar(cre[k], cim[k], p2, m2);
                    cbad += ph != p2 || mg != m2;
                });
                const uint32_t t_both = esp_cpu_get_cycle_count() - t0;
                t0 = esp_cpu_get_cycle_count();
                uint32_t sink2 = 0;
                for (int k = 0; k < 257; k++) { uint32_t p2, m2; sc::cordic_polar(cre[k], cim[k], p2, m2); sink2 += p2 + m2; }
                const uint32_t t_c = esp_cpu_get_cycle_count() - t0;
                sink = static_cast<int32_t>(sink2);
                if (t_c < ct_c) ct_c = t_c;
                if (t_both - t_c < ct_pie) ct_pie = t_both - t_c;
            }
            Serial.printf("[BENCH] PIE CORDIC vs cordic_polar: %d of %d bins differ; 257 bins: PIE %u cycles, scalar %u\n", cbad, 40 * 257, (unsigned)ct_pie, (unsigned)ct_c);
        }
        uint8_t *hm = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, kHeap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        sc::q15 *x = static_cast<sc::q15 *>(heap_caps_malloc(kTotal * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        sc::q15 *y = static_cast<sc::q15 *>(heap_caps_malloc(kTotal * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        sc::Heap h;
        sc::Stft st;
        if (hm && x && y) {
            h.init(hm, kHeap);
            if (st.init(h, false)) {
                for (int i = 0; i < kTotal; i++) x[i] = rnd(8000);
                for (uint32_t &v : sc::g_stft_prof) v = 0;
                // the fastest block of each phase within a hop (one phase carries the frame): other tasks preempt this one at boot
                constexpr int kPh = sc::Stft::H / kBlk;
                uint32_t best[kPh];
                for (uint32_t &b : best) b = UINT32_MAX;
                for (int i = 0; i < kTotal; i += kBlk) {
                    const uint32_t t0 = esp_cpu_get_cycle_count();
                    st.process(x + i, nullptr, y + i, kBlk, [](sc::StftFrame &) {});
                    const uint32_t t = esp_cpu_get_cycle_count() - t0;
                    uint32_t &b = best[(i / kBlk) % kPh];
                    if (t < b) b = t;
                }
                uint32_t sum = 0, worst = 0;
                for (uint32_t b : best) { sum += b; if (b > worst) worst = b; }
                const unsigned frames = kTotal / sc::Stft::H;
                Serial.printf("[BENCH] Stft frame steps (mean of %u frames): window %u, fft %u, callback %u, mirror %u, ifft %u, overlap-add %u, fifo %u\n", frames,
                              (unsigned)(sc::g_stft_prof[0] / frames), (unsigned)(sc::g_stft_prof[1] / frames), (unsigned)(sc::g_stft_prof[2] / frames),
                              (unsigned)(sc::g_stft_prof[3] / frames), (unsigned)(sc::g_stft_prof[4] / frames), (unsigned)(sc::g_stft_prof[5] / frames),
                              (unsigned)(sc::g_stft_prof[6] / frames));
                const int lag = sc::Stft::latency();
                double sig = 0, err = 0;
                for (int i = 2048; i < kTotal; i++) { const double e = y[i] - x[i - lag]; sig += static_cast<double>(x[i - lag]) * x[i - lag]; err += e * e; }
                Serial.printf("[BENCH] Stft mono, identity frame: %u cycles per sample (the %d-sample block with the frame: %u, without: %u), identity SNR %.1f dB at lag %d\n",
                              (unsigned)(sum / sc::Stft::H), kBlk, (unsigned)worst, (unsigned)((sum - worst) / (kPh - 1)), 10.0 * std::log10(sig / (err + 1e-30)), lag);
                st.release();
            }
        }
        heap_caps_free(hm); heap_caps_free(x); heap_caps_free(y);
    }
}
}  // namespace

void bench_run(void) {
    using namespace sc;
    const size_t n = 16384;                                // 32 KB of q15, bigger than a cache line set but streamed like a delay line
    q15 *ram = static_cast<q15 *>(heap_caps_malloc(n * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    q15 *ps = static_cast<q15 *>(heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!ram || !ps) { Serial.println("[BENCH] allocation failed"); return; }
    for (size_t i = 0; i < n; i++) ram[i] = ps[i] = static_cast<q15>(i * 37);

    Serial.printf("[BENCH] cpu %u MHz, cycles per iteration:\n", (unsigned)ESP.getCpuFreqMHz());
    Serial.printf("  empty loop                       %u\n", (unsigned)cycles_per_iter([] { int32_t a = 1; for (int i = 0; i < kIter; i++) a += i; sink = a; }));
    Serial.printf("  int32 mul + shift                %u\n", (unsigned)cycles_per_iter([] { int32_t a = 12345, b = 321; for (int i = 0; i < kIter; i++) { a = (a * (b + i)) >> 8; } sink = a; }));
    Serial.printf("  int32 x int32 -> int64 >> 16     %u\n", (unsigned)cycles_per_iter([] { int32_t a = 1234567, b = 321; for (int i = 0; i < kIter; i++) { a = static_cast<int32_t>((static_cast<int64_t>(a) * (b + i)) >> 16) + i; } sink = a; }));
    Serial.printf("  int64 x int64 >> 16              %u\n", (unsigned)cycles_per_iter([] { int64_t a = 1234567, b = 321; for (int i = 0; i < kIter; i++) { a = ((a * (b + i)) >> 16) + i; } sink = static_cast<int32_t>(a); }));
    Serial.printf("  hermite15, data in internal RAM  %u\n", (unsigned)cycles_per_iter([ram] { int32_t s = 0; uint32_t f = 0; for (int i = 0; i < kIter; i++) { f += 12345; s += hermite15(ram[i & 8191], ram[(i + 1) & 8191], ram[(i + 2) & 8191], ram[(i + 3) & 8191], f & 0xFFFF); } sink = s; }));
    Serial.printf("  hermite15, data in PSRAM         %u\n", (unsigned)cycles_per_iter([ps] { int32_t s = 0; uint32_t f = 0; for (int i = 0; i < kIter; i++) { f += 12345; s += hermite15(ps[i & 8191], ps[(i + 1) & 8191], ps[(i + 2) & 8191], ps[(i + 3) & 8191], f & 0xFFFF); } sink = s; }));
    Serial.printf("  lerp15, data in internal RAM     %u\n", (unsigned)cycles_per_iter([ram] { int32_t s = 0; uint32_t f = 0; for (int i = 0; i < kIter; i++) { f += 12345; s += lerp15(ram[i & 8191], ram[(i + 1) & 8191], f & 0xFFFF); } sink = s; }));
    Serial.printf("  lerp15, data in PSRAM            %u\n", (unsigned)cycles_per_iter([ps] { int32_t s = 0; uint32_t f = 0; for (int i = 0; i < kIter; i++) { f += 12345; s += lerp15(ps[i & 8191], ps[(i + 1) & 8191], f & 0xFFFF); } sink = s; }));
    Serial.printf("  write+read stream, internal RAM  %u\n", (unsigned)cycles_per_iter([ram] { int32_t s = 0; for (int i = 0; i < kIter; i++) { ram[i & 16383] = static_cast<q15>(i); s += ram[(i + 9000) & 16383]; } sink = s; }));
    Serial.printf("  write+read stream, PSRAM         %u\n", (unsigned)cycles_per_iter([ps] { int32_t s = 0; for (int i = 0; i < kIter; i++) { ps[i & 16383] = static_cast<q15>(i); s += ps[(i + 9000) & 16383]; } sink = s; }));
    Serial.printf("  sine() table lookup (flash)      %u\n", (unsigned)cycles_per_iter([] { int32_t s = 0; uint32_t ph = 0; for (int i = 0; i < kIter; i++) { ph += 987654321u; s += kSineTab[(ph >> 22)]; } sink = s; }));
    {   // the filter's per-sample chain, piece by piece
        const uint32_t a4 = inc_a4();
        Serial.printf("  pitch_to_inc                     %u\n", (unsigned)cycles_per_iter([a4] { uint32_t s = 0; for (int i = 0; i < kIter; i++) s += pitch_to_inc(96 * 256 + (i & 1023), a4); sink = static_cast<int32_t>(s); }));
        Serial.printf("  svf_g                            %u\n", (unsigned)cycles_per_iter([] { uint32_t s = 0; for (int i = 0; i < kIter; i++) s += svf_g(0x01000000u + static_cast<uint32_t>(i) * 4099u); sink = static_cast<int32_t>(s); }));
        Serial.printf("  svf_coef                         %u\n", (unsigned)cycles_per_iter([] { int32_t s = 0; for (int i = 0; i < kIter; i++) s += svf_coef(0x01000000u + static_cast<uint32_t>(i) * 4099u, 1 << 29).a3; sink = s; }));
        Serial.printf("  svf_tick                         %u\n", (unsigned)cycles_per_iter([] {
            SvfCoef c = svf_coef(0x08000000u, 1 << 29); SvfState st; int32_t s = 0;
            for (int i = 0; i < kIter; i++) { int32_t lp, bp, hp; svf_tick(c, st, (i & 64) ? 100000000 : -100000000, lp, bp, hp); s += lp; }
            sink = s; }));
        Serial.printf("  svf_tick, float (FPU)            %u\n", (unsigned)cycles_per_iter([] {
            SvfCoefF c = svf_coef_f(0x08000000u, 1 << 29); SvfStateF st; float s = 0;
            for (int i = 0; i < kIter; i++) { float lp, bp, hp; svf_tick_f(c, st, (i & 64) ? 0.4f : -0.4f, lp, bp, hp); s += lp; }
            sink = static_cast<int32_t>(s * 1000.0f); }));
        Serial.printf("  svf_coef, float (FPU)            %u\n", (unsigned)cycles_per_iter([] { float s = 0; for (int i = 0; i < kIter; i++) s += svf_coef_f(0x01000000u + static_cast<uint32_t>(i) * 4099u, 1 << 29).a3; sink = static_cast<int32_t>(s * 1000.0f); }));
        // single float operations, to see which ones are cheap on this FPU (each loop iteration does one)
        Serial.printf("  float: int->float convert        %u\n", (unsigned)cycles_per_iter([] { float s = 0; for (int i = 0; i < kIter; i++) s += static_cast<float>(i); sink = static_cast<int32_t>(s); }));
        Serial.printf("  float: add only                  %u\n", (unsigned)cycles_per_iter([] { float s = 0, c = 1.0001f; for (int i = 0; i < kIter; i++) s += c; sink = static_cast<int32_t>(s); }));
        Serial.printf("  float: multiply-add              %u\n", (unsigned)cycles_per_iter([] { float s = 0.5f, c = 0.9999f, d = 0.0001f; for (int i = 0; i < kIter; i++) s = s * c + d; sink = static_cast<int32_t>(s * 1000.0f); }));
        Serial.printf("  float: divide                    %u\n", (unsigned)cycles_per_iter([] { float s = 0, d = 1.0f; for (int i = 0; i < kIter; i++) { d += 0.0003f; s += 1.0f / d; } sink = static_cast<int32_t>(s); }));
        Serial.printf("  float: float->int truncate       %u\n", (unsigned)cycles_per_iter([] { int32_t s = 0; float f = 1.5f; for (int i = 0; i < kIter; i++) { f += 1.7f; s += static_cast<int32_t>(f); } sink = s; }));
        Serial.printf("  float: bit cast via memcpy       %u\n", (unsigned)cycles_per_iter([] { uint32_t s = 0; float f = 1.5f; for (int i = 0; i < kIter; i++) { f += 0.37f; uint32_t b; std::memcpy(&b, &f, 4); s += b >> 15; } sink = static_cast<int32_t>(s); }));
        // a whole 2-section filter sample, both ways of getting the coefficients (cutoff moves every sample, as with an LFO / envelope on it)
        Serial.printf("  filter sample, int coef + convert %u\n", (unsigned)cycles_per_iter([a4] {
            SvfStateF st[2]; float s = 0;
            for (int i = 0; i < kIter; i++) {
                const uint32_t g = svf_g(pitch_to_inc(96 * 256 + (i & 1023), a4));
                float x = (i & 64) ? 0.4f : -0.4f;
                for (int k = 0; k < 2; k++) { float lp, bp, hp; svf_tick_f(svf_to_float(svf_coef(g, 1 << 29)), st[k], x, lp, bp, hp); x = lp; }
                s += x;
            }
            sink = static_cast<int32_t>(s * 1000.0f); }));
        Serial.printf("  filter sample, batch float coef  %u\n", (unsigned)cycles_per_iter([a4] {
            SvfStateF st[2]; float s = 0; const float kk[2] = {0.7f, 0.9f}; SvfCoefF c[2];
            for (int i = 0; i < kIter; i++) {
                const float g = static_cast<float>(static_cast<int32_t>(svf_g(pitch_to_inc(96 * 256 + (i & 1023), a4)))) * (1.0f / 268435456.0f);
                svf_coef_batch(g, kk, 2, c);
                float x = (i & 64) ? 0.4f : -0.4f;
                for (int k = 0; k < 2; k++) { float lp, bp, hp; svf_tick_f(c[k], st[k], x, lp, bp, hp); x = lp; }
                s += x;
            }
            sink = static_cast<int32_t>(s * 1000.0f); }));
        Serial.printf("  int32 mulh only                 %u\n", (unsigned)cycles_per_iter([] { int32_t a = 1234567, b = 321; for (int i = 0; i < kIter; i++) { a = mulh(a + i, b << 20) + i; } sink = a; }));
    }
    heap_caps_free(ram);
    heap_caps_free(ps);
    bench_fft();
}
#endif
