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
#include "platform/esp32/bench_esp32.h"

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
}
#endif
