#pragma once
// Minimal test harness: TEST(name) { CHECK(cond); CHECK_NEAR(a, b, tol); }  run by test_main.cpp.
// Also the measuring helpers shared by DSP tests (SNR / RMS against a float reference).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace tst {

struct Case { const char *name; void (*fn)(); };
inline std::vector<Case> &registry() { static std::vector<Case> r; return r; }
inline int &failures() { static int f = 0; return f; }
struct Reg { Reg(const char *n, void (*f)()) { registry().push_back({n, f}); } };

}  // namespace tst

#define TEST(name) \
    static void name(); \
    static tst::Reg reg_##name(#name, name); \
    static void name()

#define CHECK(cond) do { if (!(cond)) { std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); tst::failures()++; } } while (0)
#define CHECK_EQ(a, b) do { long long va_ = (long long)(a), vb_ = (long long)(b); if (va_ != vb_) { \
    std::printf("    FAIL %s:%d: %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); tst::failures()++; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double va_ = (double)(a), vb_ = (double)(b); if (std::fabs(va_ - vb_) > (tol)) { \
    std::printf("    FAIL %s:%d: %s ~ %s  (%g vs %g, tol %g)\n", __FILE__, __LINE__, #a, #b, va_, vb_, (double)(tol)); tst::failures()++; } } while (0)

namespace tst {

// Signal-to-error ratio in dB of `test` against `ref` (both float, same length).
inline double snr_db(const std::vector<double> &ref, const std::vector<double> &test) {
    double s = 0, e = 0;
    for (size_t i = 0; i < ref.size(); i++) { s += ref[i] * ref[i]; double d = ref[i] - test[i]; e += d * d; }
    if (e <= 0) return 200.0;
    return 10.0 * std::log10(s / e);
}

inline double rms(const std::vector<double> &x) {
    double s = 0;
    for (double v : x) s += v * v;
    return x.empty() ? 0 : std::sqrt(s / (double)x.size());
}

}  // namespace tst
