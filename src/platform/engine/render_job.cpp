#if defined(PLATFORM_SIM) || defined(ARDUINO_ARCH_ESP32)
// Sample processing to the card (engine_synth.h: engine_synth_render_*; ADR-041 decisions 36-38). The UI posts one job; the thread that owns
// the card (the board's card task, the desktop's loader thread) runs it a few chunks per call to engine_synth_render_step, between the
// sample loader's reads: it opens the source .smp of the library and a new one next to it, runs engine/offline/process.h, writes the
// header last, renames the file into place. Nothing the size of the sample is in memory. The UI polls the state, then rescans the library.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_heap_caps.h>
#endif
#include "engine/offline/process.h"
#include "hal/hal_storage.h"
#include "platform/engine/engine_synth.h"

using namespace sc;

namespace {

// The source file's frames (the audio of block b is at (b + 1) * 4096: frame k at 4096 + 2k). The time and texture operations ask for
// a few hundred frames at a time, anywhere in a block; read as asked, each request cost the card three commands (a part of a sector,
// the sectors between, a part again) and ~12 ms on the prototype. So one block of the file is kept (`cache`, when the job could get
// the memory): a small request is served from it, and the card is asked for a whole aligned block, one command. A request of a block
// or more (the level operations, a whole window) goes straight to the destination as before.
struct FileReader : FrameReader {
    FILE *f = nullptr;
    uint8_t *cache = nullptr;
    long cache_at = -1;                                                   // the file offset of the block held, -1: none
    bool read(uint32_t frame, uint32_t n, q15 *dst) override {
        if (!f) return false;
        long pos = static_cast<long>(kSmpBlockBytes + 2u * frame);
        size_t left = 2u * static_cast<size_t>(n);
        uint8_t *out = reinterpret_cast<uint8_t *>(dst);
        while (left) {
            const long blk = pos & ~static_cast<long>(kSmpBlockBytes - 1);
            const size_t off = static_cast<size_t>(pos - blk);
            if (!cache || left >= kSmpBlockBytes) {                       // a block or more: straight to the destination, as asked
                const size_t k = left;
                if (std::fseek(f, pos, SEEK_SET) != 0 || std::fread(out, 1, k, f) != k) return false;
                pos += static_cast<long>(k); out += k; left -= k;
                continue;
            }
            if (cache_at != blk) {                                        // (a .smp file is whole blocks: the last one is there in full)
                cache_at = -1;
                if (std::fseek(f, blk, SEEK_SET) != 0 || std::fread(cache, 1, kSmpBlockBytes, f) != kSmpBlockBytes) return false;
                cache_at = blk;
            }
            const size_t k = left < kSmpBlockBytes - off ? left : kSmpBlockBytes - off;
            std::memcpy(out, cache + off, k);
            pos += static_cast<long>(k); out += k; left -= k;
        }
        return true;
    }
};
struct FileWriter : FrameWriter {
    FILE *f = nullptr;
    bool write(const q15 *src, uint32_t n) override { return f && std::fwrite(src, 2, n, f) == n; }
};

struct Job {
    char src[224], dst[224], tmp[232];                                    // paths on the card (root + folder + a 23-character name)
    ProcOp op = ProcOp::Gain;
    int32_t amount = 0, amount2 = 0;
    Processor proc;
    FileReader in;
    FileWriter out;
    void *work = nullptr;                                                 // the operation's working memory (Processor::work_bytes)
    uint8_t *rcache = nullptr;                                            // the reader's block (internal RAM on the board: 4 KB while a job runs)
    void *fast = nullptr;                                                 // a spectral operation's frame (internal RAM on the board: 20.5 KB while it runs)
    uint8_t *hdr() { return reinterpret_cast<uint8_t *>(proc.scratch()); }   // the header block: read before the job, written after it
};

char g_root[160] = "";                                                     // the card's root on this platform ("/sdcard", the desktop folder)
Job *g_job = nullptr;                                                     // posted by the UI, run by the card thread
std::atomic<int> g_state{RENDER_IDLE};                                    // render_state_t
std::atomic<int> g_percent{0};
std::atomic<bool> g_posted{false};                                        // a job waits for the card thread to open its files

// The job (~5 KB) and its operation's working memory (0..43 KB): in PSRAM on the board (internal RAM is the scarce one), only
// while a job exists.
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
// The reader's block and a spectral operation's frame come from internal RAM on the board: memory that is quick at random access (a
// pass over a block in PSRAM costs milliseconds there), and the PSRAM the job borrows is all but used up by the largest operations.
// A job runs without them when the internal RAM has no room (slower, the same result).
void *quick_new(size_t bytes) {
#ifdef ARDUINO_ARCH_ESP32
    return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return ::operator new(bytes, std::nothrow);
#endif
}
Job *job_new() {
    void *m = mem_new(sizeof(Job));
    return m ? new (m) Job : nullptr;
}
void job_free(Job *j) {
    if (!j) return;
    mem_free(j->work);
    mem_free(j->rcache);
    mem_free(j->fast);
    j->~Job();
    mem_free(j);
}

void path_of(const char *name, char *out, size_t n) { std::snprintf(out, n, "%s/%s/%s.smp", g_root, STORAGE_DIR_SAMPLES, name); }

void finish(Job &j, bool ok) {
    if (j.in.f) std::fclose(j.in.f);
    if (j.out.f) std::fclose(j.out.f);
    j.in.f = j.out.f = nullptr;
    if (ok) { std::remove(j.dst); ok = std::rename(j.tmp, j.dst) == 0; }
    if (!ok) std::remove(j.tmp);
    g_percent.store(ok ? 100 : 0);
    g_state.store(ok ? RENDER_DONE : RENDER_FAILED);
}

}  // namespace

extern "C" {

void engine_synth_set_card_root(const char *root) { std::snprintf(g_root, sizeof g_root, "%s", root ? root : ""); }
const char *engine_synth_card_root(void) { return g_root; }

bool engine_synth_render_start(const char *src_name, const char *dst_name, int op, int amount, int amount2) {
    if (g_state.load() == RENDER_RUNNING || !src_name || !dst_name || op < 0 || op >= static_cast<int>(ProcOp::Count)) return false;
    if (!g_job) g_job = job_new();
    if (!g_job) return false;
    Job &j = *g_job;
    mem_free(j.work);
    mem_free(j.fast);
    const size_t quick = Processor::fast_bytes(static_cast<ProcOp>(op));
    j.fast = quick ? quick_new(quick) : nullptr;
    const size_t need = Processor::work_bytes(static_cast<ProcOp>(op), j.fast != nullptr);
    j.work = need ? mem_new(need) : nullptr;
    if (need && !j.work) { mem_free(j.fast); j.fast = nullptr; return false; }   // not enough memory for this operation
    if (!j.rcache && need) j.rcache = static_cast<uint8_t *>(quick_new(kSmpBlockBytes));   // (only the operations that read in small pieces; without it they still run)
    j.in.cache = need ? j.rcache : nullptr;
    j.in.cache_at = -1;
    path_of(src_name, j.src, sizeof j.src);
    path_of(dst_name, j.dst, sizeof j.dst);
    std::snprintf(j.tmp, sizeof j.tmp, "%s.tmp", j.dst);
    j.op = static_cast<ProcOp>(op);
    j.amount = amount;
    j.amount2 = amount2;
    j.in.f = j.out.f = nullptr;
    g_percent.store(0);
    g_state.store(RENDER_RUNNING);
    g_posted.store(true);
    return true;
}

render_state_t engine_synth_render_poll(int *percent) {
    if (percent) *percent = g_percent.load();
    return static_cast<render_state_t>(g_state.load());
}

void engine_synth_render_done(void) {                                     // the UI took the result: the next job may start
    if (g_state.load() == RENDER_RUNNING) return;
    g_state.store(RENDER_IDLE);
    job_free(g_job);                                                       // (the card thread only touches it while RUNNING)
    g_job = nullptr;
}

bool engine_synth_render_step(void) {
    if (g_state.load() != RENDER_RUNNING || !g_job) return false;
    Job &j = *g_job;
    if (g_posted.exchange(false)) {                                        // open the files here: only this thread touches the card
        j.in.f = std::fopen(j.src, "rb");
        SmpHeader h;
        if (!j.in.f || std::fread(j.hdr(), 1, kSmpBlockBytes, j.in.f) != kSmpBlockBytes || !smp_parse_header(j.hdr(), kSmpBlockBytes, h)) { finish(j, false); return false; }
        j.out.f = std::fopen(j.tmp, "wb");
        std::memset(j.hdr(), 0, kSmpBlockBytes);                           // the header is written last: a zero block holds its place
        if (!j.out.f || std::fwrite(j.hdr(), 1, kSmpBlockBytes, j.out.f) != kSmpBlockBytes) { finish(j, false); return false; }
        if (!j.proc.begin(h, j.op, j.amount, j.amount2, j.work, j.fast)) { finish(j, false); return false; }
        return true;
    }
    if (!j.proc.step(j.in, j.out, 1)) { finish(j, false); return false; }   // one chunk a call: the caller decides how many between the streams' reads
    g_percent.store(static_cast<int>(j.proc.progress() * 99.0f));
    if (!j.proc.done()) return true;
    const SmpHeader &oh = j.proc.out_header();                             // pad the last block, then the header in place
    const uint32_t tail = oh.blocks() * kSmpBlockFrames - oh.frames;
    static const q15 zero[64] = {};
    for (uint32_t k = 0; k < tail; k += 64) if (std::fwrite(zero, 2, tail - k < 64 ? tail - k : 64, j.out.f) == 0) { finish(j, false); return false; }
    std::memset(j.hdr(), 0, kSmpBlockBytes);
    smp_write_header(j.hdr(), oh);
    const bool ok = std::fseek(j.out.f, 0, SEEK_SET) == 0 && std::fwrite(j.hdr(), 1, kSmpBlockBytes, j.out.f) == kSmpBlockBytes && std::fflush(j.out.f) == 0;
    finish(j, ok);
    return false;
}

}  // extern "C"
#endif  // PLATFORM_SIM || ARDUINO_ARCH_ESP32
