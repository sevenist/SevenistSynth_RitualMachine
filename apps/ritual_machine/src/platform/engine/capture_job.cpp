#if defined(PLATFORM_SIM) || defined(ARDUINO_ARCH_ESP32)
// Recording what plays into a new sample on the card (engine_synth.h: engine_synth_capture_*; ADR-041 capture / resampling). The audio
// thread fills the tap's ring (engine/sampler/capture.h); the thread that owns the card empties it here, a chunk per call to
// engine_synth_capture_step, into `<name>.smp.tmp` of the samples folder; when the recording has stopped and the ring is empty it
// writes the header and renames the file into place. The UI polls the state, then rescans the library.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_heap_caps.h>
#endif
#include "engine/sampler/capture.h"
#include "engine/sampler/smp_format.h"
#include "core/card_layout.h"
#include "platform/engine/engine_synth.h"

using namespace sc;

namespace {

constexpr uint32_t kRing = 16384;                                         // frames between the audio and the card: 0.37 s at 44.1 kHz
constexpr uint32_t kChunkFrames = 2 * kSmpBlockFrames;                    // written at a time: 8 KB = 16 sectors, one card command

struct Cap {
    char dst[224], tmp[232];
    FILE *f = nullptr;
    q15 *ring = nullptr;
    uint32_t frames = 0;                                                  // written to the file
    q15 arm_threshold = 0;                                                // how the tap is armed once the file is open
    bool arm_bar = false;
    uint32_t arm_limit = 0;
    // the overview while the length is unknown: the peak of every `win` frames; when the 64 are used, pairs merge and win doubles
    uint8_t pk[kSmpPeaks] = {};
    uint32_t pk_n = 0, pk_win = 512, pk_fill = 0, pk_cur = 0;
    q15 chunk[kChunkFrames];
};

Cap *g_cap = nullptr;
std::atomic<int> g_state{CAPTURE_IDLE};
std::atomic<bool> g_posted{false};
std::atomic<bool> g_stop{false};                                          // stop asked (it may come before the tap is armed)

void *mem_new(size_t bytes) {
#ifdef ARDUINO_ARCH_ESP32
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return ::operator new(bytes, std::nothrow);
#endif
}
void mem_free(void *m) {
    if (!m) return;
#ifdef ARDUINO_ARCH_ESP32
    heap_caps_free(m);
#else
    ::operator delete(m);
#endif
}

void peaks_add(Cap &c, const q15 *x, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t a = static_cast<uint32_t>(x[i] < 0 ? -x[i] : x[i]);
        if (a > c.pk_cur) c.pk_cur = a;
        if (++c.pk_fill < c.pk_win) continue;
        if (c.pk_n == kSmpPeaks) {                                        // full: halve the resolution
            for (int k = 0; k < kSmpPeaks / 2; k++) c.pk[k] = c.pk[2 * k] > c.pk[2 * k + 1] ? c.pk[2 * k] : c.pk[2 * k + 1];
            c.pk_n = kSmpPeaks / 2;
            c.pk_win *= 2;
            // (the window just finished is half of a new one: it goes on filling)
            c.pk_fill = c.pk_win / 2;
            continue;
        }
        c.pk[c.pk_n++] = static_cast<uint8_t>(c.pk_cur >> 7 > 255 ? 255 : c.pk_cur >> 7);
        c.pk_fill = 0; c.pk_cur = 0;
    }
}

void finish(Cap &c, bool ok) {
    if (c.f) std::fclose(c.f);
    c.f = nullptr;
    if (ok) { std::remove(c.dst); ok = std::rename(c.tmp, c.dst) == 0; }
    if (!ok) std::remove(c.tmp);
    capture_tap().reset();
    g_state.store(ok ? CAPTURE_DONE : CAPTURE_FAILED);
}

}  // namespace

extern "C" {

bool engine_synth_capture_start(const char *name, int threshold, int at_bar, uint32_t limit_frames) {
    if (g_state.load() != CAPTURE_IDLE || !name || !name[0] || engine_synth_render_poll(nullptr) == RENDER_RUNNING) return false;
    if (!g_cap) { void *m = mem_new(sizeof(Cap)); g_cap = m ? new (m) Cap : nullptr; }
    if (!g_cap) return false;
    Cap &c = *g_cap;
    if (!c.ring) c.ring = static_cast<q15 *>(mem_new(sizeof(q15) * kRing));
    if (!c.ring) { mem_free(g_cap); g_cap = nullptr; return false; }
    std::snprintf(c.dst, sizeof c.dst, "%s/%s/%s.smp", engine_synth_card_root(), STORAGE_DIR_SAMPLES, name);
    std::snprintf(c.tmp, sizeof c.tmp, "%s.tmp", c.dst);
    c.f = nullptr;
    c.frames = 0;
    c.pk_n = 0; c.pk_win = 512; c.pk_fill = 0; c.pk_cur = 0;
    capture_tap().set_buffer(c.ring, kRing);
    c.arm_threshold = static_cast<q15>(threshold < 0 ? 0 : threshold > 32767 ? 32767 : threshold);
    c.arm_bar = at_bar != 0;
    c.arm_limit = limit_frames;
    g_stop.store(false);
    g_state.store(CAPTURE_ARMED);
    g_posted.store(true);                                                  // the card thread opens the file, then arms the tap
    return true;
}

void engine_synth_capture_stop(void) { g_stop.store(true); capture_tap().stop(); }

capture_state_t engine_synth_capture_poll(uint32_t *frames, uint32_t *dropped) {
    int s = g_state.load();
    if (s == CAPTURE_ARMED && capture_tap().state() == CaptureTap::Recording) s = CAPTURE_RECORDING;
    if ((s == CAPTURE_ARMED || s == CAPTURE_RECORDING) && capture_tap().state() == CaptureTap::Done) s = CAPTURE_FINISHING;
    if (frames) *frames = capture_tap().frames();
    if (dropped) *dropped = capture_tap().dropped();
    return static_cast<capture_state_t>(s);
}

void engine_synth_capture_done(void) {
    const int s = g_state.load();
    if (s != CAPTURE_DONE && s != CAPTURE_FAILED && s != CAPTURE_EMPTY) return;
    g_state.store(CAPTURE_IDLE);
    if (g_cap) { mem_free(g_cap->ring); g_cap->~Cap(); mem_free(g_cap); g_cap = nullptr; }
}

bool engine_synth_capture_step(void) {
    if (!g_cap) return false;
    const int s = g_state.load();
    if (s != CAPTURE_ARMED) return false;                                  // (ARMED covers armed, recording and finishing: the tap knows which)
    Cap &c = *g_cap;
    CaptureTap &tap = capture_tap();
    if (g_posted.exchange(false)) {                                        // open the file here: only this thread touches the card
        c.f = std::fopen(c.tmp, "wb");
        std::memset(c.chunk, 0, kSmpBlockBytes);                           // the header is written last: a zero block holds its place
        if (!c.f || std::fwrite(c.chunk, 1, kSmpBlockBytes, c.f) != kSmpBlockBytes) { finish(c, false); return false; }
        tap.arm(c.arm_threshold, c.arm_bar, c.arm_limit);
        if (g_stop.load()) tap.stop();
        return true;
    }
    const bool ended = tap.state() == CaptureTap::Done;
    if (tap.waiting() < kChunkFrames && !ended) return false;              // not a chunk's worth yet
    const uint32_t n = tap.take(c.chunk, kChunkFrames);
    if (n) {
        if (std::fwrite(c.chunk, sizeof(q15), n, c.f) != n) { tap.stop(); finish(c, false); return false; }
        peaks_add(c, c.chunk, n);
        c.frames += n;
    }
    if (!ended || tap.waiting()) return true;
    if (!c.frames) {                                                       // stopped before any sound: no file
        std::fclose(c.f); c.f = nullptr;
        std::remove(c.tmp);
        tap.reset();
        g_state.store(CAPTURE_EMPTY);
        return false;
    }
    SmpHeader h;                                                           // the last block padded, then the header in its place
    h.sample_rate = static_cast<uint32_t>(engine_synth_sample_rate());
    h.frames = c.frames;
    h.root_note = 60;
    if (c.pk_fill && c.pk_n < kSmpPeaks) c.pk[c.pk_n++] = static_cast<uint8_t>(c.pk_cur >> 7 > 255 ? 255 : c.pk_cur >> 7);
    for (int b = 0; b < kSmpPeaks; b++) h.peaks[b] = c.pk_n ? c.pk[static_cast<uint32_t>(b) * c.pk_n / kSmpPeaks] : 0;
    if (!h.peaks[0]) h.peaks[0] = 1;                                       // (all zero would read as "not stored")
    const uint32_t tail = h.blocks() * kSmpBlockFrames - h.frames;
    std::memset(c.chunk, 0, sizeof c.chunk);
    bool ok = !tail || std::fwrite(c.chunk, sizeof(q15), tail, c.f) == tail;
    smp_write_header(reinterpret_cast<uint8_t *>(c.chunk), h);
    ok = ok && std::fseek(c.f, 0, SEEK_SET) == 0 && std::fwrite(c.chunk, 1, kSmpBlockBytes, c.f) == kSmpBlockBytes && std::fflush(c.f) == 0;
    finish(c, ok);
    return false;
}

}  // extern "C"
#endif  // PLATFORM_SIM || ARDUINO_ARCH_ESP32
