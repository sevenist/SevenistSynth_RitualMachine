#include "engine/core/engine.h"
#include "engine/dsp/block.h"
#include "engine/modules/builtin.h"

namespace sc {

bool Engine::init(Memory mem, int nvoices) {
    mem_ = mem;
    nvoices_ = nvoices < 1 ? 1 : (nvoices > kMaxVoices ? kMaxVoices : nvoices);
    register_builtin_modules(reg_);
    return true;
}

void Engine::destroy_rec(Rec &r) {
    for (int v = 0; v < kMaxVoices; v++) { destroy_module(*mem_.fast, r.inst[v]); r.inst[v] = nullptr; }
    r.alive = false;
}

int Engine::find_rec(int id, int type) const {
    for (int i = 0; i < kMaxRecs; i++) if (rec_[i].alive && rec_[i].id == id && rec_[i].type == type) return i;
    return -1;
}

Err Engine::load(const GraphDesc &g) {
    Plan *pl = nullptr;
    Err e = compile_plan(g, reg_, *mem_.fast, nvoices_, &pl);
    if (e != Err::Ok) return e;

    bool created[kMaxRecs] = {};
    auto rollback = [&]() {
        for (int i = 0; i < kMaxRecs; i++) if (created[i]) destroy_rec(rec_[i]);
        free_plan(pl);
    };

    // instances that already exist keep running: their new parameter values are applied by the audio thread at the swap
    int n_updates = 0;
    for (int i = 0; i < g.n_nodes; i++) {
        if (find_rec(g.node[i].id, g.node[i].type) < 0) continue;
        const ModuleInfo &info = *reg_.get(g.node[i].type)->info;
        n_updates += info.n_param * (info.scope == Scope::Voice ? nvoices_ : 1);
    }
    if (n_updates > 0) {
        pl->updates = pl->heap->alloc_array<ParamUpdate>(static_cast<size_t>(n_updates));
        if (!pl->updates) { rollback(); return Err::NoMem; }
    }

    for (int i = 0; i < g.n_nodes; i++) {
        const NodeDesc &nd = g.node[i];
        const ModuleType *t = reg_.get(nd.type);
        const ModuleInfo &info = *t->info;
        int ri = find_rec(nd.id, nd.type);
        bool fresh = ri < 0;
        const int count = info.scope == Scope::Voice ? nvoices_ : 1;
        if (fresh) {
            for (int k = 0; k < kMaxRecs && ri < 0; k++) if (!rec_[k].alive) ri = k;
            if (ri < 0) { rollback(); return Err::TooBig; }
            Rec &r = rec_[ri];
            r = Rec{};
            r.alive = true; r.id = nd.id; r.type = nd.type;
            created[ri] = true;
            for (int v = 0; v < count; v++) {
                r.inst[v] = t->create(mem_);
                if (!r.inst[v]) { rollback(); return Err::NoMem; }
            }
        }
        Rec &r = rec_[ri];
        for (int v = 0; v < count; v++) {
            if (fresh) {                                     // not visible to the audio thread yet: configure directly
                for (int p = 0; p < info.n_param; p++) r.inst[v]->set_param(p, nd.param[p]);
                r.inst[v]->reset();
            } else {
                for (int p = 0; p < info.n_param; p++) pl->updates[pl->n_updates++] = ParamUpdate{r.inst[v], static_cast<uint8_t>(p), nd.param[p]};
            }
            pl->inst[i][v] = r.inst[v];
        }
        pl->rec[i] = ri;
        if (nd.type == T_MASTER_OUT) pl->has_master = true;
    }

    Plan *old = pending_.exchange(pl, std::memory_order_acq_rel);
    free_plan(old);             // a plan that was published but never rendered (its fresh instances stay recorded: gc() or the next load reuse them)
    return Err::Ok;
}

void Engine::free_retired() {
    Plan *p = retired_.exchange(nullptr, std::memory_order_acq_rel);             // take the whole stack; the audio thread keeps pushing onto a fresh one
    while (p) {
        Plan *next = p->next_retired;
        free_plan(p);
        p = next;
    }
}

void Engine::gc() {
    // The audio thread switches plans between swap_seq_ increments (odd = in progress): read both plans consistently or try later.
    const uint32_t s1 = swap_seq_.load(std::memory_order_acquire);
    if (s1 & 1u) return;
    const Plan *plans[2] = {active_.load(std::memory_order_acquire), pending_.load(std::memory_order_acquire)};
    if (swap_seq_.load(std::memory_order_acquire) != s1) return;

    free_retired();
    bool used[kMaxRecs] = {};
    for (const Plan *pl : plans)
        if (pl) for (int i = 0; i < pl->n_nodes; i++) used[pl->rec[i]] = true;
    for (int i = 0; i < kMaxRecs; i++) if (rec_[i].alive && !used[i]) destroy_rec(rec_[i]);
}

void Engine::shutdown() {
    free_plan(pending_.exchange(nullptr));
    free_plan(active_.exchange(nullptr));
    free_retired();
    for (auto &r : rec_) if (r.alive) destroy_rec(r);
    for (auto &v : voices_) v = VoiceState{};
    Command c;
    while (cmd_.pop(c)) {}
}

/* ---------------- control side: post commands ---------------- */

bool Engine::note_on(int note, int velocity127) {
    Command c;
    c.type = Cmd::NoteOn; c.note = static_cast<uint8_t>(note); c.value = velocity127;
    return cmd_.push(c);
}
bool Engine::note_off(int note) {
    Command c;
    c.type = Cmd::NoteOff; c.note = static_cast<uint8_t>(note);
    return cmd_.push(c);
}
bool Engine::all_notes_off() {
    Command c;
    c.type = Cmd::AllNotesOff;
    return cmd_.push(c);
}
bool Engine::set_param(int node_id, int idx, int32_t value) {
    Command c;
    c.type = Cmd::SetParam; c.node = static_cast<uint8_t>(node_id); c.idx = static_cast<uint8_t>(idx); c.value = value;
    return cmd_.push(c);
}
bool Engine::set_blob(int node_id, const void *data, size_t bytes) {
    if (bytes > static_cast<size_t>(kCmdBlobMax)) return false;
    Command c;
    c.type = Cmd::SetBlob; c.node = static_cast<uint8_t>(node_id); c.size = static_cast<uint16_t>(bytes);
    std::memcpy(c.blob, data, bytes);
    return cmd_.push(c);
}

/* ---------------- voices (audio thread) ---------------- */

int Engine::alloc_voice(int note) {
    int best = -1;
    for (int v = 0; v < nvoices_; v++)                                  // same note: retrigger that voice
        if (voices_[v].active && voices_[v].note == note) return v;
    for (int v = 0; v < nvoices_; v++) if (!voices_[v].active) return v;
    for (int v = 0; v < nvoices_; v++)                                  // steal: longest-released, else oldest
        if (!voices_[v].gate && (best < 0 || voices_[v].release_age > voices_[best].release_age)) best = v;
    if (best >= 0) return best;
    best = 0;
    for (int v = 1; v < nvoices_; v++) if (voices_[v].age > voices_[best].age) best = v;
    return best;
}

void Engine::do_note_on(int note, int velocity127) {
    int v = alloc_voice(note);
    VoiceState &s = voices_[v];
    s = VoiceState{};
    s.note = note;
    s.pitch = note * 256;
    s.velocity = static_cast<q15>(clamp_i32(velocity127, 1, 127) * 32767 / 127);
    s.gate = true;
    s.started = true;
    s.active = true;
}

void Engine::do_note_off(int note) {
    for (int v = 0; v < nvoices_; v++)
        if (voices_[v].active && voices_[v].gate && voices_[v].note == note) voices_[v].gate = false;
}

void Engine::apply(const Command &c) {
    switch (c.type) {
    case Cmd::NoteOn: do_note_on(c.note, c.value); break;
    case Cmd::NoteOff: do_note_off(c.note); break;
    case Cmd::AllNotesOff: for (auto &v : voices_) v = VoiceState{}; break;
    case Cmd::SetParam:
    case Cmd::SetBlob: {
        const Plan *pl = active_.load(std::memory_order_acquire);
        if (!pl) break;
        for (int n = 0; n < pl->n_nodes; n++) {
            if (pl->node_id[n] != c.node) continue;
            const int count = pl->node_scope[n] == Scope::Voice ? nvoices_ : 1;
            for (int v = 0; v < count; v++) {
                Module *m = pl->inst[n][v];
                if (!m) continue;
                if (c.type == Cmd::SetParam) m->set_param(c.idx, c.value);
                else m->set_blob(c.blob, c.size);
            }
            break;
        }
        break;
    }
    default: break;
    }
}

int Engine::active_voices() const {
    int n = 0;
    for (int v = 0; v < nvoices_; v++) n += voices_[v].active;
    return n;
}

/* ---------------- audio ---------------- */

void Engine::run(const Plan *pl, int first, int count, int voice, ProcessCtx &ctx) {
    const int stride = pl->fbv_stride;
    const q15 *fb0 = pl->fbv, *fb1 = pl->fbv + (stride ? stride * pl->nvoices : 0);
    const ptrdiff_t shift = static_cast<ptrdiff_t>(voice) * stride;
    auto rel = [&](const q15 *p) -> const q15 * { return (p >= fb0 && p < fb1) ? p + shift : p; };

    for (int i = first; i < first + count; i++) {
        const Step &s = pl->steps[i];
        const bool move = s.fb && voice > 0;
        switch (s.kind) {
        case StepKind::Module: {
            Module *m = pl->inst[s.node][voice > 0 ? voice : 0];
            if (!move) { m->process(ctx, s.p); break; }
            Ports p = s.p;
            for (auto &x : p.in) x = rel(x);
            for (auto &x : p.mod) x = rel(x);
            m->process(ctx, p);
            break;
        }
        case StepKind::Mix: {
            q15 *dst = move ? const_cast<q15 *>(rel(s.dst)) : s.dst;
            int32_t acc[kBlock] = {};
            for (int k = 0; k < s.n; k++) {
                const q15 *src = move ? rel(s.src[k]) : s.src[k];
                if (s.gain[k] == kUnity) for (int j = 0; j < kBlock; j++) acc[j] += src[j];
                else for (int j = 0; j < kBlock; j++) acc[j] += (src[j] * s.gain[k] + (1 << 14)) >> 15;
            }
            for (int j = 0; j < kBlock; j++) dst[j] = sat16(acc[j]);
            break;
        }
        case StepKind::Copy: {
            q15 *dst = move ? const_cast<q15 *>(rel(s.dst)) : s.dst;
            block_copy(dst, s.src[0]);
            break;
        }
        }
    }
}

void Engine::render(q15 *l, q15 *r) {
    if (pending_.load(std::memory_order_acquire)) {
        swap_seq_.fetch_add(1, std::memory_order_acq_rel);                       // odd: gc() must not look at the plans now
        if (Plan *np = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
            for (int k = 0; k < np->n_updates; k++) np->updates[k].m->set_param(np->updates[k].idx, np->updates[k].value);
            Plan *prev = active_.exchange(np, std::memory_order_acq_rel);
            if (prev) {                                                          // hand the old plan to gc(): push on the retired stack (only this thread pushes)
                prev->next_retired = retired_.load(std::memory_order_relaxed);
                while (!retired_.compare_exchange_weak(prev->next_retired, prev, std::memory_order_release, std::memory_order_relaxed)) {}
            }
        }
        swap_seq_.fetch_add(1, std::memory_order_acq_rel);
    }
    Command c;
    while (cmd_.pop(c)) apply(c);

    block_clear(l);
    block_clear(r);
    block_clear(bus_l_);
    block_clear(bus_r_);
    const uint64_t t = time_.fetch_add(1, std::memory_order_relaxed) + 1;
    const Plan *pl = active_.load(std::memory_order_acquire);
    if (!pl) return;

    ProcessCtx ctx{kBlock, t, nullptr, bus_l_, bus_r_, l, r};
    run(pl, 0, pl->n_pre, -1, ctx);
    for (int v = 0; v < nvoices_; v++) {
        VoiceState &vs = voices_[v];
        if (!vs.active) continue;
        if (vs.started) {
            for (int n = 0; n < pl->n_nodes; n++)
                if (pl->node_scope[n] == Scope::Voice && pl->inst[n][v]) pl->inst[n][v]->reset();
        }
        ctx.voice = &vs;
        run(pl, pl->n_pre, pl->n_voice, v, ctx);
        vs.started = false;
        vs.age++;
        if (!vs.gate) vs.release_age++;
        if (vs.done) vs = VoiceState{};
    }
    ctx.voice = nullptr;
    run(pl, pl->n_pre + pl->n_voice, pl->n_post, -1, ctx);
    if (!pl->has_master) { block_copy(l, bus_l_); block_copy(r, bus_r_); }
}

}  // namespace sc
