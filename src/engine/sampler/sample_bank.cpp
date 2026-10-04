#include "engine/sampler/sample_bank.h"
#include <cstring>

namespace sc {

namespace {
constexpr int64_t kFar = 1ll << 50;

// Moves along the playback path by `delta` frames (>= 0), following the loop rules. `wrapped` is set when the
// path jumped (loop wrap) or turned around (ping-pong) during the step.
void path_step(int64_t &f, int &dir, int64_t delta, const StreamSnap &s, bool &wrapped) {
    const int64_t frames = s.frames;
    const bool loop = s.loop.active();
    for (int guard = 0; delta > 0 && guard < 8; guard++) {
        if (dir > 0) {
            const int64_t limit = (loop && f < s.loop.end) ? s.loop.end : frames;
            const int64_t room = limit - f;
            if (delta < room) { f += delta; return; }
            delta -= room;
            if (loop && limit == s.loop.end) {
                wrapped = true;
                if (s.loop.mode == SMP_LOOP_FWD) f = s.loop.start;
                else { f = limit - 1; dir = -1; }
            } else { f = frames > 0 ? frames - 1 : 0; return; }
        } else {
            const int64_t limit = (loop && s.loop.mode == SMP_LOOP_PINGPONG && f > s.loop.start) ? s.loop.start :
                                  (loop && s.loop.mode == SMP_LOOP_FWD && f > s.loop.start) ? s.loop.start : 0;
            const int64_t room = f - limit;
            if (delta <= room) { f -= delta; return; }
            delta -= room;
            if (loop && limit == s.loop.start) {
                wrapped = true;
                if (s.loop.mode == SMP_LOOP_PINGPONG) { f = limit; dir = 1; }
                else f = s.loop.end > 0 ? s.loop.end - 1 : 0;
            } else { f = 0; return; }
        }
    }
}
}  // namespace

bool SampleBank::init(StorageDevice &dev, Memory &mem) {
    dev_ = &dev;
    mem_ = mem;
    return true;
}

void SampleBank::shutdown() {
    for (auto &s : slot_) { free_heads(s); s = SampleSlot{}; }
}

void SampleBank::free_heads(SampleSlot &s) {
    for (int i = 0; i < s.n_heads; i++) { mem_.bulk->free(s.head[i].data); s.head[i].data = nullptr; }
    s.n_heads = 0;
    if (s.hdr_tmp) { mem_.fast->free(s.hdr_tmp); s.hdr_tmp = nullptr; }
}

int SampleBank::alloc_job() {
    for (int i = 0; i < kMaxJobs; i++) if (jobs_[i].kind == J_FREE) return i;
    return -1;
}

int SampleBank::load(const char *name, int head_ms) {
    int id = -1;
    for (int i = 0; i < kMaxSamples; i++) if (slot_[i].state == SLOT_FREE) { id = i; break; }
    if (id < 0) return -1;
    SampleSlot &s = slot_[id];
    s = SampleSlot{};
    s.file = dev_->open(name);
    if (s.file < 0) return -1;
    s.hdr_tmp = static_cast<uint8_t *>(mem_.fast->alloc(kSmpBlockBytes));
    if (!s.hdr_tmp) return -1;
    s.state = SLOT_HEADER;
    s.head_ms = static_cast<uint32_t>(head_ms);             // converted to blocks when the header is known
    s.next_issue = 0;
    return id;
}

void SampleBank::unload(int id) {
    if (id < 0 || id >= kMaxSamples) return;
    free_heads(slot_[id]);
    slot_[id] = SampleSlot{};
}

/* ---------------- instruments ---------------- */

int SampleBank::add_instrument() {
    for (int i = 0; i < kMaxInstruments; i++) if (!inst_[i].used) { inst_[i] = Instrument{}; inst_[i].used = true; return i; }
    return -1;
}
bool SampleBank::add_zone(int inst, const Zone &z) {
    if (inst < 0 || inst >= kMaxInstruments || !inst_[inst].used || inst_[inst].n >= kMaxZones) return false;
    inst_[inst].zone[inst_[inst].n++] = z;
    return true;
}
const Zone *SampleBank::pick(int inst, int note, int vel) const {
    if (inst < 0 || inst >= kMaxInstruments || !inst_[inst].used) return nullptr;
    for (int i = 0; i < inst_[inst].n; i++) {
        const Zone &z = inst_[inst].zone[i];
        if (note >= z.lo_note && note <= z.hi_note && vel >= z.lo_vel && vel <= z.hi_vel) return &z;
    }
    return nullptr;
}

/* ---------------- streams ---------------- */

void SampleBank::add_stream(Stream *s) {
    for (auto &p : streams_) if (!p) { p = s; return; }
}
void SampleBank::remove_stream(Stream *s) {
    for (auto &p : streams_) if (p == s) p = nullptr;
}

/* ---------------- loader ---------------- */

void SampleBank::finish_slot_heads(SampleSlot &s) {
    if (s.hdr_tmp) { mem_.fast->free(s.hdr_tmp); s.hdr_tmp = nullptr; }
    s.state = SLOT_READY;
}

void SampleBank::complete(const IoDone &d) {
    if (d.tag >= static_cast<uint32_t>(kMaxJobs) || jobs_[d.tag].kind == J_FREE) return;
    Job j = jobs_[d.tag];
    jobs_[d.tag].kind = J_FREE;
    jobs_in_flight_--;
    switch (j.kind) {
    case J_HEADER: {
        SampleSlot &s = slot_[j.slot];
        if (s.state != SLOT_HEADER) break;
        SmpHeader h;
        if (!d.ok || !smp_parse_header(s.hdr_tmp, kSmpBlockBytes, h)) { s.state = SLOT_FAILED; break; }
        s.h = h;
        const uint32_t head_ms = s.head_ms;
        const uint32_t nblocks = h.blocks();
        s.resident = head_ms == 0;
        auto blocks_for = [&](uint32_t ms) { return (static_cast<uint64_t>(ms) * h.sample_rate / 1000 + kSmpBlockFrames - 1) / kSmpBlockFrames; };
        uint32_t main_blocks = s.resident ? nblocks : static_cast<uint32_t>(blocks_for(head_ms));
        if (main_blocks < 1) main_blocks = 1;
        if (main_blocks >= nblocks) { main_blocks = nblocks; s.resident = true; }
        s.n_heads = 0;
        auto add_head = [&](uint32_t first_block, uint32_t count) {
            if (first_block >= nblocks) return;
            if (first_block + count > nblocks) count = nblocks - first_block;
            Head &hd = s.head[s.n_heads];
            hd.start_frame = first_block * kSmpBlockFrames;
            hd.frames = count * kSmpBlockFrames;
            hd.data = static_cast<q15 *>(mem_.bulk->alloc(static_cast<size_t>(count) * kSmpBlockBytes));
            if (hd.data) s.n_heads++;
        };
        add_head(0, main_blocks);
        if (!s.resident) {
            const uint32_t sl_blocks = static_cast<uint32_t>(blocks_for(head_ms / 3 + 1)) + 1;
            for (int i = 0; i < h.slice_count; i++) add_head(h.slice[i] / kSmpBlockFrames, sl_blocks);
            if (nblocks > sl_blocks) add_head(nblocks - sl_blocks, sl_blocks);
        }
        if (s.n_heads == 0) { s.state = SLOT_FAILED; break; }
        s.pending = s.n_heads;
        s.next_issue = 0;
        s.state = SLOT_HEADS;
        break;
    }
    case J_HEAD: {
        SampleSlot &s = slot_[j.slot];
        if (s.state != SLOT_HEADS) break;
        if (!d.ok) { s.state = SLOT_FAILED; break; }
        if (--s.pending == 0) finish_slot_heads(s);
        break;
    }
    case J_BLOCK: {
        Stream *st = streams_[j.stream];
        if (!st) break;
        st->busy[j.ring_slot] = false;
        StreamSnap sn;
        if (d.ok && st->snapshot(sn) && sn.gen == j.gen && sn.sample == j.sample) {
            st->publish_slot(j.ring_slot, j.gen, j.blk);
            stats.block_reads.fetch_add(1, std::memory_order_relaxed);
        }
        break;
    }
    default: break;
    }
}

void SampleBank::issue_admin(uint64_t now_us) {
    for (int i = 0; i < kMaxSamples; i++) {
        SampleSlot &s = slot_[i];
        if (s.state == SLOT_HEADER && s.next_issue == 0) {
            const int jid = alloc_job();
            if (jid < 0 || jobs_in_flight_ >= kQueueDepth + 2) return;
            IoRead r{s.file, 0, kSmpBlockBytes, s.hdr_tmp, static_cast<uint32_t>(jid)};
            if (!dev_->submit(r, now_us)) return;
            jobs_[jid] = Job{J_HEADER, static_cast<int16_t>(i), 0, 0, 0, 0, 0, 0};
            jobs_in_flight_++;
            s.next_issue = 1;
        } else if (s.state == SLOT_HEADS) {
            while (s.next_issue < s.n_heads) {
                const int jid = alloc_job();
                if (jid < 0 || jobs_in_flight_ >= kQueueDepth + 2) return;
                const Head &hd = s.head[s.next_issue];
                IoRead r{s.file, (hd.start_frame / kSmpBlockFrames + 1) * kSmpBlockBytes, hd.frames / kSmpBlockFrames * kSmpBlockBytes, hd.data, static_cast<uint32_t>(jid)};
                if (!dev_->submit(r, now_us)) return;
                jobs_[jid] = Job{J_HEAD, static_cast<int16_t>(i), 0, 0, static_cast<int16_t>(s.next_issue), 0, 0, 0};
                jobs_in_flight_++;
                s.next_issue++;
            }
        }
    }
}

// The blocks a stream will need, in path order, with the time (in output samples) until each is needed.
int SampleBank::desired_blocks(const StreamSnap &sn, int64_t blk[], int64_t until[]) const {
    int n = 0;
    auto add = [&](int64_t b, int64_t u) {
        if (b < 0) return;
        for (int i = 0; i < n; i++) if (blk[i] == b) { if (u < until[i]) until[i] = u; return; }
        blk[n] = b; until[n] = u; n++;
    };
    int64_t f = sn.play_frame;
    int dir = sn.dir;
    const int64_t rate = sn.rate_q16 > 64 ? sn.rate_q16 : 64;
    const int64_t cur = f / kSmpBlockFrames, off = f % kSmpBlockFrames;
    add(cur, 0);
    if (dir > 0 && off < 4) add(cur - 1, 0);                                         // the interpolator looks 1 frame back (forward) or ahead (reverse)
    if (dir < 0 && off > static_cast<int64_t>(kSmpBlockFrames) - 4) add(cur + 1, 0);
    // walk the path in half-block steps so that no block it touches is skipped; remember where it wrapped
    const int64_t step = kSmpBlockFrames / 2;
    for (int j = 1; j <= kLookaheadBlocks * 2; j++) {
        bool wrapped = false;
        path_step(f, dir, step, sn, wrapped);
        const int64_t u = (static_cast<int64_t>(j) * step << 16) / rate;
        add(f / kSmpBlockFrames, u);
        if (wrapped) {                                                               // both ends of the loop are about to be needed
            add(static_cast<int64_t>(sn.loop.start) / kSmpBlockFrames, u);
            add((static_cast<int64_t>(sn.loop.end) - 1) / kSmpBlockFrames, u);
        }
    }
    return n;
}

void SampleBank::issue_streams(uint64_t now_us) {
    while (jobs_in_flight_ < kQueueDepth) {
        // best candidate over all streams: the block with the smallest deadline that is neither present nor on its way
        int best_stream = -1, best_ring = 0;
        int64_t best_blk = 0, best_until = kFar;
        StreamSnap best_snap;
        for (int si = 0; si < kMaxStreams; si++) {
            Stream *st = streams_[si];
            if (!st || !st->attached()) continue;
            StreamSnap sn;
            if (!st->snapshot(sn) || sn.sample < 0 || sn.sample >= kMaxSamples) continue;
            const SampleSlot &sl = slot_[sn.sample];
            if (sl.state != SLOT_READY || sl.resident) continue;
            int64_t blk[kRingBlocks + 4], until[kRingBlocks + 4];
            const int n = desired_blocks(sn, blk, until);
            const int64_t nblocks = sl.h.blocks();
            for (int i = 0; i < n; i++) {
                const int64_t b = blk[i];
                if (b >= nblocks) continue;
                bool in_head = false;                                                 // heads are block aligned
                for (int h = 0; h < sl.n_heads && !in_head; h++) {
                    const int64_t lo = sl.head[h].start_frame / kSmpBlockFrames, cnt = sl.head[h].frames / kSmpBlockFrames;
                    in_head = b >= lo && b < lo + cnt;
                }
                if (in_head) continue;
                const int rs = static_cast<int>(b % kRingBlocks);
                const uint64_t want = (static_cast<uint64_t>(sn.gen) << 32) | static_cast<uint32_t>(b) | 0x8000000000000000ull;
                if (st->slot_tag(rs) == want) continue;                              // already there
                if (st->busy[rs]) continue;                                           // being read (this or an older generation)
                // the slot must not hold a block that is still wanted
                bool wanted = false;
                const uint64_t cur = st->slot_tag(rs);
                if (cur) { const uint32_t cb = static_cast<uint32_t>(cur & 0xFFFFFFFFu); for (int k = 0; k < n; k++) if (blk[k] == cb && static_cast<uint32_t>(cur >> 32) == sn.gen) wanted = true; }
                if (wanted) continue;
                if (until[i] < best_until) { best_until = until[i]; best_stream = si; best_ring = rs; best_blk = b; best_snap = sn; }
            }
        }
        if (best_stream < 0) return;
        Stream *st = streams_[best_stream];
        const int jid = alloc_job();
        if (jid < 0) return;
        const SampleSlot &sl = slot_[best_snap.sample];
        st->invalidate_slot(best_ring);
        IoRead r{sl.file, static_cast<uint32_t>((best_blk + 1) * kSmpBlockBytes), kSmpBlockBytes, st->slot_memory(best_ring), static_cast<uint32_t>(jid)};
        if (!dev_->submit(r, now_us)) return;
        st->busy[best_ring] = true;
        st->fetching[best_ring] = best_blk;
        jobs_[jid] = Job{J_BLOCK, 0, static_cast<int16_t>(best_stream), static_cast<int16_t>(best_ring), 0, best_snap.gen, static_cast<uint32_t>(best_blk), best_snap.sample};
        jobs_in_flight_++;
    }
}

void SampleBank::pump(uint64_t now_us) {
    IoDone d;
    while (dev_->poll(d, now_us)) complete(d);
    issue_admin(now_us);
    issue_streams(now_us);
}

}  // namespace sc
