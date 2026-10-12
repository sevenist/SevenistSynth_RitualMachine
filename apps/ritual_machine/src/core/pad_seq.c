#include "core/pad_seq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void pseq_clear(pseq_pattern_t *p) {
    memset(p, 0, sizeof *p);
    p->steps = SEQ_PAGE; p->rate = SQR_16; p->dir = SQD_FORWARD; p->swing = 0;
}

int pseq_find(const pseq_pattern_t *p, int pad, int step) {
    for (int i = 0; i < p->n; i++) if (p->hit[i].pad == (uint32_t)pad && p->hit[i].step == (uint32_t)step) return i;
    return -1;
}

void pseq_remove(pseq_pattern_t *p, int index) {
    if (index < 0 || index >= p->n) return;
    p->hit[index] = p->hit[p->n - 1];                       // the last one takes its place
    memset(&p->hit[p->n - 1], 0, sizeof p->hit[0]);
    p->n--;
}

bool pseq_put(pseq_pattern_t *p, int pad, int step, int level, int prob, int ratchet, int len) {
    if (pad < 0 || pad > 63 || step < 0 || step > 63) return false;
    int i = pseq_find(p, pad, step);
    if (i < 0) {
        if (p->n >= PSEQ_HITS) return false;
        i = p->n++;
    }
    pseq_hit_t h;
    memset(&h, 0, sizeof h);
    h.pad = (uint32_t)pad; h.step = (uint32_t)step;
    h.level = (uint32_t)(clampi(level, 1, PSEQ_LEVEL_MAX + 1) - 1);
    h.prob = (uint32_t)clampi((prob + 5) / 10, 0, PSEQ_PROB_MAX);
    h.ratchet = (uint32_t)(clampi(ratchet, 1, 4) - 1);
    h.len = (uint32_t)(clampi(len, 1, 64) - 1);
    p->hit[i] = h;
    return true;
}

bool pseq_toggle(pseq_pattern_t *p, int pad, int step) {
    const int i = pseq_find(p, pad, step);
    if (i >= 0) { pseq_remove(p, i); return true; }
    return pseq_put(p, pad, step, PSEQ_LEVEL_MAX + 1, 100, 1, 1);
}

int pseq_clear_pad(pseq_pattern_t *p, int pad) {
    int n = 0;
    for (int i = p->n - 1; i >= 0; i--) if (p->hit[i].pad == (uint32_t)pad) { pseq_remove(p, i); n++; }
    return n;
}

uint64_t pseq_track(const pseq_pattern_t *p, int pad) {
    uint64_t m = 0;
    for (int i = 0; i < p->n; i++) if (p->hit[i].pad == (uint32_t)pad) m |= 1ull << p->hit[i].step;
    return m;
}

uint64_t pseq_used(const pseq_pattern_t *p) {
    uint64_t m = 0;
    for (int i = 0; i < p->n; i++) if (p->hit[i].step < p->steps) m |= 1ull << p->hit[i].pad;
    return m;
}

bool pseq_hit_adjust(pseq_hit_t *h, pseq_value_t what, int dir) {
    static const int max[PSH_COUNT] = {PSEQ_LEVEL_MAX, PSEQ_PROB_MAX, 3, 63};
    const int old = what == PSH_LEVEL ? (int)h->level : what == PSH_PROB ? (int)h->prob : what == PSH_RATCHET ? (int)h->ratchet : (int)h->len;
    const int v = clampi(old + dir, 0, max[what]);
    switch (what) {
        case PSH_LEVEL:   h->level = (uint32_t)v; break;
        case PSH_PROB:    h->prob = (uint32_t)v; break;
        case PSH_RATCHET: h->ratchet = (uint32_t)v; break;
        default:          h->len = (uint32_t)v; break;
    }
    return v != old;
}

void pseq_hit_format(const pseq_hit_t *h, pseq_value_t what, char *out, int n) {
    switch (what) {
        case PSH_LEVEL:   snprintf(out, (size_t)n, "%d/16", (int)h->level + 1); break;
        case PSH_PROB:    snprintf(out, (size_t)n, "%d%%", (int)h->prob * 10); break;
        case PSH_RATCHET: snprintf(out, (size_t)n, "x%d", (int)h->ratchet + 1); break;
        default:          snprintf(out, (size_t)n, "%d", (int)h->len + 1); break;
    }
}

const char *pseq_value_label(pseq_value_t what) {
    static const char *const l[PSH_COUNT] = {"Lvl", "Prob", "Rtch", "Len"};
    return what < PSH_COUNT ? l[what] : "?";
}

bool pseq_param_adjust(pseq_pattern_t *p, pseq_param_t k, int dir) {
    uint8_t *v = k == PSP_STEPS ? &p->steps : k == PSP_SWING ? &p->swing : k == PSP_RATE ? &p->rate : &p->dir;
    static const int lo[PSP_COUNT] = {1, 0, 0, 0}, hi[PSP_COUNT] = {SEQ_MAX_STEPS, 50, SQR_COUNT - 1, SQD_COUNT - 1}, st[PSP_COUNT] = {1, 5, 1, 1};
    const int nv = clampi(*v + dir * st[k], lo[k], hi[k]);
    const bool changed = nv != *v;
    *v = (uint8_t)nv;
    return changed;
}

const char *pseq_param_label(pseq_param_t k) {
    static const char *const l[PSP_COUNT] = {"Steps", "Swing", "Rate", "Dir"};
    return k < PSP_COUNT ? l[k] : "?";
}

void pseq_param_format(const pseq_pattern_t *p, pseq_param_t k, char *out, int n) {
    seq_t s;                                                 // (the rate's and the direction's names are the synth sequencer's)
    s.rate = p->rate; s.dir = p->dir;
    switch (k) {
        case PSP_STEPS: snprintf(out, (size_t)n, "%d", p->steps); break;
        case PSP_SWING: snprintf(out, (size_t)n, "%d%%", p->swing); break;
        case PSP_RATE:  seq_param_format(&s, SQP_RATE, out, (size_t)n); break;
        default:        seq_param_format(&s, SQP_DIR, out, (size_t)n); break;
    }
}

uint32_t pseq_hash(const pseq_pattern_t *p) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < p->n; i++) {
        const pseq_hit_t *x = &p->hit[i];
        const uint32_t v = (uint32_t)x->pad | (uint32_t)x->step << 6 | (uint32_t)x->level << 12 | (uint32_t)x->prob << 16 | (uint32_t)x->ratchet << 20 | (uint32_t)x->len << 22;
        h = (h ^ v) * 16777619u;
    }
    h = (h ^ p->n) * 16777619u;
    h = (h ^ (uint32_t)(p->steps | p->rate << 8 | p->dir << 16 | p->swing << 24)) * 16777619u;
    return h;
}

int pseq_to_text(const pseq_pattern_t *p, char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "# Ritual Machine pads pattern\npadpattern %d %d %d %d\n", p->steps, p->swing, p->rate, p->dir);
    for (int i = 0; i < p->n && n < cap; i++) {
        const pseq_hit_t *h = &p->hit[i];
        const int v[4] = {(int)h->level + 1, (int)h->prob * 10, (int)h->ratchet + 1, (int)h->len + 1};
        static const int def[4] = {PSEQ_LEVEL_MAX + 1, 100, 1, 1};
        int used = 0;
        for (int k = 0; k < 4; k++) if (v[k] != def[k]) used = k + 1;
        n += snprintf(buf + n, (size_t)(cap - n), "hit %d %d", (int)h->pad, (int)h->step);
        for (int k = 0; k < used && n < cap; k++) n += snprintf(buf + n, (size_t)(cap - n), " %d", v[k]);
        if (n < cap) n += snprintf(buf + n, (size_t)(cap - n), "\n");
    }
    return n < cap ? n : -1;
}

#define EACH_LINE(line, txt) for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL)

bool pseq_from_text(pseq_pattern_t *p, const char *txt) {
    pseq_pattern_t r;
    bool is = false;
    pseq_clear(&r);
    EACH_LINE(line, txt) {
        int a = 0, b = 0, c = 0, d = 0;
        int v[6] = {0, 0, PSEQ_LEVEL_MAX + 1, 100, 1, 1};
        if (sscanf(line, "padpattern %d %d %d %d", &a, &b, &c, &d) == 4) {
            r.steps = (uint8_t)clampi(a, 1, SEQ_MAX_STEPS); r.swing = (uint8_t)clampi(b, 0, 50);
            r.rate = (uint8_t)clampi(c, 0, SQR_COUNT - 1); r.dir = (uint8_t)clampi(d, 0, SQD_COUNT - 1);
            is = true;
        } else if (sscanf(line, "hit %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) >= 2) {
            pseq_put(&r, v[0], v[1], v[2], v[3], v[4], v[5]);
        }
    }
    if (is) *p = r;
    return is;
}

int pseq_bank_to_text(const pseq_bank_t *b, char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "# Ritual Machine pads sequencer (its patterns: padpat1.cfg .. padpat%d.cfg)\npadseq %d %d %d\nchain", PSEQ_PATTERNS,
                     b->on ? 1 : 0, b->cur + 1, b->chain_on ? 1 : 0);
    for (int i = 0; i < b->chain_len && i < SEQ_CHAIN_MAX && n < cap; i++) n += snprintf(buf + n, (size_t)(cap - n), " %d", b->chain[i] + 1);
    if (n < cap) n += snprintf(buf + n, (size_t)(cap - n), "\n");
    return n < cap ? n : -1;
}

bool pseq_bank_from_text(pseq_bank_t *b, const char *txt) {
    bool is = false;
    EACH_LINE(line, txt) {
        int a = 0, c = 0, d = 0;
        if (sscanf(line, "padseq %d %d %d", &a, &c, &d) == 3) {
            b->on = a ? 1 : 0;
            b->cur = (uint8_t)(clampi(c, 1, PSEQ_PATTERNS) - 1);
            b->chain_on = d ? 1 : 0;
            is = true;
        } else if (!strncmp(line, "chain", 5) && (line[5] == ' ' || line[5] == '\n' || line[5] == '\r' || !line[5])) {
            const char *s = line + 5;
            int len = 0;
            while (len < SEQ_CHAIN_MAX) {
                char *end = NULL;
                const long v = strtol(s, &end, 10);
                if (end == s) break;
                b->chain[len++] = (uint8_t)(clampi((int)v, 1, PSEQ_PATTERNS) - 1);
                s = end;
                while (*s == ' ') s++;
                if (*s == '\n' || *s == '\r' || !*s) break;
            }
            b->chain_len = (uint8_t)(len < 1 ? 1 : len);
            if (len < 1) b->chain[0] = 0;
        }
    }
    b->chain_pos = 0;
    b->next = -1;
    return is;
}

void pseq_bank_init(pseq_bank_t *b, pseq_pattern_t *patterns) {
    memset(b, 0, sizeof *b);
    b->pat = patterns;
    for (int k = 0; k < PSEQ_PATTERNS; k++) pseq_clear(&patterns[k]);
    b->on = 1;
    b->next = -1;
    b->chain_len = 1;
}
