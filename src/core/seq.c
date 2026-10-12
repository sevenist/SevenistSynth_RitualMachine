#include "core/seq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *label;
    int min, max, step;
    const char *unit;
    size_t offset;              // into seq_t
} seq_desc_t;

#define F(field) offsetof(seq_t, field)

static const seq_desc_t table[SQP_COUNT] = {
    [SQP_BPM]       = {"BPM",   40, 240, 5, "",   F(bpm)},
    [SQP_STEPS]     = {"Steps",  1, SEQ_MAX_STEPS, 1, "", F(steps)},
    [SQP_TRANSPOSE] = {"Trsp", -24, 24, 1, "st",  F(transpose)},
    [SQP_SWING]     = {"Swing",  0, 50, 5, "%",   F(swing)},
    [SQP_RATE]      = {"Rate",   0, SQR_COUNT - 1, 1, "", F(rate)},
    [SQP_DIR]       = {"Dir",    0, SQD_COUNT - 1, 1, "", F(dir)},
};

void seq_step_clear(seq_step_t *st) {
    for (int k = 0; k < SEQ_CHORD; k++) st->note[k] = 0;
    st->len = 1;
    st->vel = SEQ_VEL_DEFAULT;
    st->prob = 100;
    st->ratchet = 1;
}

bool seq_step_is_rest(const seq_step_t *st) {
    for (int k = 0; k < SEQ_CHORD; k++) if (st->note[k]) return false;
    return true;
}

void seq_init(seq_t *s) {
    static const uint8_t demo[SEQ_PAGE] = {48, 0, 55, 0, 60, 0, 55, 58, 48, 0, 55, 0, 63, 62, 60, 0};
    for (int i = 0; i < SEQ_MAX_STEPS; i++) seq_step_clear(&s->step[i]);
    for (int i = 0; i < SEQ_PAGE; i++) s->step[i].note[0] = demo[i];
    s->step[0].len = 2; s->step[4].len = 2; s->step[8].len = 2; s->step[14].len = 2;
    s->bpm = 110;
    s->steps = SEQ_PAGE;
    s->transpose = 0;
    s->swing = 0;
    s->rate = SQR_16;
    s->dir = SQD_FORWARD;
    s->running = false;
    s->pos = -1;
    s->last_note = 48;
}

/* ---------------- several patterns, the chain ---------------- */

void seq_pattern_clear(seq_pattern_t *p) {
    for (int k = 0; k < SEQ_MAX_STEPS; k++) seq_step_clear(&p->step[k]);
    p->steps = SEQ_PAGE; p->rate = SQR_16; p->dir = SQD_FORWARD; p->swing = 0; p->transpose = 0;
}

void seq_pattern_get(const seq_t *s, seq_pattern_t *p) {
    memcpy(p->step, s->step, sizeof p->step);
    p->steps = (uint8_t)s->steps; p->rate = (uint8_t)s->rate; p->dir = (uint8_t)s->dir; p->swing = (uint8_t)s->swing;
    p->transpose = (int8_t)s->transpose;
}

void seq_pattern_put(seq_t *s, const seq_pattern_t *p) {
    memcpy(s->step, p->step, sizeof s->step);
    s->steps = p->steps; s->rate = p->rate; s->dir = p->dir; s->swing = p->swing; s->transpose = p->transpose;
}

void seq_bank_init(seq_bank_t *b, seq_pattern_t *patterns, const seq_t *s) {
    memset(b, 0, sizeof *b);
    b->pat = patterns;
    b->next = -1;
    b->chain_len = 1;
    if (!patterns) return;
    for (int k = 0; k < SEQ_PATTERNS; k++) seq_pattern_clear(&b->pat[k]);
    seq_pattern_get(s, &b->pat[0]);
}

void seq_bank_select(seq_bank_t *b, seq_t *s, int k) {
    if (k < 0 || k >= SEQ_PATTERNS || !b->pat) return;
    seq_pattern_get(s, &b->pat[b->cur]);
    b->cur = (uint8_t)k;
    seq_pattern_put(s, &b->pat[k]);
}

uint32_t seq_pattern_hash(const seq_pattern_t *p) {
    uint32_t h = 2166136261u;
    const uint8_t *d = (const uint8_t *)p;
    for (size_t i = 0; i < sizeof *p; i++) h = (h ^ d[i]) * 16777619u;
    return h;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int seq_pattern_to_text(const seq_pattern_t *p, char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "# Ritual Machine sequencer pattern\npattern %d %d %d %d %d\n", p->steps, p->transpose, p->swing, p->rate, p->dir);
    for (int k = 0; k < SEQ_MAX_STEPS && n < cap; k++) {
        const seq_step_t *st = &p->step[k];
        const int v[8] = {st->note[0], st->len, st->vel, st->prob, st->ratchet, st->note[1], st->note[2], st->note[3]};
        static const int def[8] = {0, 1, SEQ_VEL_DEFAULT, 100, 1, 0, 0, 0};
        int used = 0;
        for (int i = 0; i < 8; i++) if (v[i] != def[i]) used = i + 1;
        if (!used) continue;                                 // a plain rest: no line
        n += snprintf(buf + n, (size_t)(cap - n), "step %d", k);
        for (int i = 0; i < used && n < cap; i++) n += snprintf(buf + n, (size_t)(cap - n), " %d", v[i]);
        if (n < cap) n += snprintf(buf + n, (size_t)(cap - n), "\n");
    }
    return n < cap ? n : -1;
}

#define EACH_LINE(line, txt) for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL)

bool seq_pattern_from_text(seq_pattern_t *p, const char *txt) {
    seq_pattern_t r;
    bool is = false;
    seq_pattern_clear(&r);
    EACH_LINE(line, txt) {
        int a = 0, b = 0, c = 0, d = 0, e = 0;
        int sv[9] = {0, 0, 1, SEQ_VEL_DEFAULT, 100, 1, 0, 0, 0};
        if (sscanf(line, "pattern %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
            r.steps = (uint8_t)clampi(a, 1, SEQ_MAX_STEPS); r.transpose = (int8_t)clampi(b, -24, 24); r.swing = (uint8_t)clampi(c, 0, 50);
            r.rate = (uint8_t)clampi(d, 0, SQR_COUNT - 1); r.dir = (uint8_t)clampi(e, 0, SQD_COUNT - 1);
            is = true;
        } else if (sscanf(line, "step %d %d %d %d %d %d %d %d %d", &sv[0], &sv[1], &sv[2], &sv[3], &sv[4], &sv[5], &sv[6], &sv[7], &sv[8]) >= 2) {
            if (sv[0] < 0 || sv[0] >= SEQ_MAX_STEPS) continue;
            seq_step_t *st = &r.step[sv[0]];
            const int note[SEQ_CHORD] = {sv[1], sv[6], sv[7], sv[8]};
            for (int k = 0; k < SEQ_CHORD; k++) st->note[k] = (uint8_t)(note[k] < 1 ? 0 : clampi(note[k], SEQ_NOTE_MIN, SEQ_NOTE_MAX));
            st->len = (uint8_t)clampi(sv[2], 1, SEQ_LEN_MAX);
            st->vel = (uint8_t)clampi(sv[3], 1, 127);
            st->prob = (uint8_t)clampi(sv[4], 0, 100);
            st->ratchet = (uint8_t)clampi(sv[5], 1, SEQ_RATCHET_MAX);
        }
    }
    if (is) *p = r;
    return is;
}

int seq_bank_to_text(const seq_bank_t *b, int bpm, char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "# Ritual Machine sequencer (its patterns: pattern1.cfg .. pattern%d.cfg)\nseq %d %d %d\nchain", SEQ_PATTERNS, bpm,
                     b->cur + 1, b->chain_on ? 1 : 0);
    for (int i = 0; i < b->chain_len && i < SEQ_CHAIN_MAX && n < cap; i++) n += snprintf(buf + n, (size_t)(cap - n), " %d", b->chain[i] + 1);
    if (n < cap) n += snprintf(buf + n, (size_t)(cap - n), "\n");
    return n < cap ? n : -1;
}

bool seq_bank_from_text(seq_bank_t *b, int *bpm, const char *txt) {
    bool is = false;
    EACH_LINE(line, txt) {
        int a = 0, c = 0, d = 0;
        if (sscanf(line, "seq %d %d %d", &a, &c, &d) == 3) {
            *bpm = clampi(a, 40, 240);
            b->cur = (uint8_t)(clampi(c, 1, SEQ_PATTERNS) - 1);
            b->chain_on = d ? 1 : 0;
            is = true;
        } else if (!strncmp(line, "chain", 5) && (line[5] == ' ' || line[5] == '\n' || line[5] == '\r' || !line[5])) {
            const char *s = line + 5;
            int len = 0;
            while (len < SEQ_CHAIN_MAX) {
                char *end = NULL;
                const long v = strtol(s, &end, 10);
                if (end == s) break;
                b->chain[len++] = (uint8_t)(clampi((int)v, 1, SEQ_PATTERNS) - 1);
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

void seq_set_running(seq_t *s, bool run) {
    s->running = run;
    if (!run) s->pos = -1;
}

bool seq_adjust_note(seq_t *s, int step, int voice, int dir) {
    uint8_t *note = &s->step[step].note[voice];
    int n = *note;
    const int old = n;
    if (n == 0) { if (dir > 0) n = s->last_note >= SEQ_NOTE_MIN && s->last_note <= SEQ_NOTE_MAX ? s->last_note : SEQ_NOTE_MIN; }
    else if (n + dir < SEQ_NOTE_MIN) n = 0;
    else if (n + dir <= SEQ_NOTE_MAX) n += dir;
    *note = (uint8_t)n;
    if (n) s->last_note = (uint8_t)n;
    return n != old;
}

static bool set_u8(uint8_t *v, int nv, int lo, int hi) {
    if (nv < lo) nv = lo;
    if (nv > hi) nv = hi;
    const bool changed = nv != *v;
    *v = (uint8_t)nv;
    return changed;
}

// The next multiple of `unit` above / below v (the ends of the range are not on it).
static int next_multiple(int v, int dir, int unit) { return dir > 0 ? (v / unit + 1) * unit : (v - 1) / unit * unit; }

bool seq_adjust_len(seq_t *s, int step, int dir)     { return set_u8(&s->step[step].len, s->step[step].len + dir, 1, SEQ_LEN_MAX); }
bool seq_adjust_vel(seq_t *s, int step, int dir)     { return set_u8(&s->step[step].vel, next_multiple(s->step[step].vel, dir, 5), 1, 127); }
bool seq_adjust_prob(seq_t *s, int step, int dir)    { return set_u8(&s->step[step].prob, next_multiple(s->step[step].prob, dir, 5), 0, 100); }
bool seq_adjust_ratchet(seq_t *s, int step, int dir) { return set_u8(&s->step[step].ratchet, s->step[step].ratchet + dir, 1, SEQ_RATCHET_MAX); }

bool seq_set_norm(seq_t *s, int step, int voice, seq_knob_t what, float x) {
    seq_step_t *st = &s->step[step];
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    switch (what) {
        case SQK_NOTE: {
            const int k = (int)(x * (float)(SEQ_NOTE_MAX - SEQ_NOTE_MIN + 1) + 0.5f);      // 0 = none, then the notes
            const int n = k ? SEQ_NOTE_MIN + k - 1 : 0;
            const bool changed = n != st->note[voice];
            st->note[voice] = (uint8_t)n;
            if (n) s->last_note = (uint8_t)n;
            return changed;
        }
        case SQK_LEN:  return set_u8(&st->len, 1 + (int)(x * (float)(SEQ_PAGE - 1) + 0.5f), 1, SEQ_LEN_MAX);
        case SQK_VEL:  return set_u8(&st->vel, 1 + (int)(x * 126.0f + 0.5f), 1, 127);
        case SQK_PROB: return set_u8(&st->prob, 5 * (int)(x * 20.0f + 0.5f), 0, 100);
        default:       return false;
    }
}

/* ---------------- the quick edit tools ---------------- */

static struct {
    seq_step_t clip[SEQ_MAX_STEPS];     // what Copy took ...
    uint8_t    clip_n, clip_scope;      // ... how many steps (0: nothing) and of which scope
    seq_step_t undo[SEQ_MAX_STEPS];     // the pattern before the last tool ...
    uint8_t    undo_steps;              // ... and its length (0: nothing to undo)
    uint32_t   rng;
} g_tools = {.rng = 0x9E3779B9u};

void seq_tools_reset(void) { g_tools.clip_n = 0; g_tools.undo_steps = 0; g_tools.rng = 0x9E3779B9u; }

static int rnd(int n) {                                  // 0..n-1
    g_tools.rng = g_tools.rng * 1664525u + 1013904223u;
    return (int)((g_tools.rng >> 8) % (uint32_t)(n < 1 ? 1 : n));
}

const char *seq_tool_name(int tool) {
    static const char *const n[SQT_COUNT] = {"Copy", "Paste", "Clear", "Shift<", "Shift>", "Reverse", "Double", "RndNote", "Human", "RndProb", "Undo"};
    return tool >= 0 && tool < SQT_COUNT ? n[tool] : "?";
}

const char *seq_scope_name(int scope) {
    static const char *const n[SQS_COUNT] = {"Step", "Page", "All"};
    return scope >= 0 && scope < SQS_COUNT ? n[scope] : "?";
}

static void keep_undo(const seq_t *s) {
    memcpy(g_tools.undo, s->step, sizeof g_tools.undo);
    g_tools.undo_steps = (uint8_t)s->steps;
}

bool seq_tool_apply(seq_t *s, int tool, int scope, int cursor, char *msg, int n) {
    if (cursor >= s->steps) cursor = s->steps - 1;
    if (cursor < 0) cursor = 0;
    const int page = cursor / SEQ_PAGE * SEQ_PAGE;
    // the steps the scope names (a page: what the pattern has of it), and the range the moving tools work on (a step alone cannot move)
    int a = cursor, b = cursor + 1;
    if (scope == SQS_PAGE) { a = page; b = page + SEQ_PAGE < s->steps ? page + SEQ_PAGE : s->steps; }
    else if (scope == SQS_ALL) { a = 0; b = s->steps; }
    const int ma = scope == SQS_PAGE ? a : 0, mb = scope == SQS_PAGE ? b : s->steps;
    const char *what = scope == SQS_STEP ? "step" : scope == SQS_PAGE ? "page" : "pattern";
    seq_step_t t;
    switch (tool) {
        case SQT_COPY:
            if (scope == SQS_PAGE) { a = page; b = page + SEQ_PAGE; }                  // (a whole page, also past the pattern's end)
            memcpy(g_tools.clip, &s->step[a], (size_t)(b - a) * sizeof t);
            g_tools.clip_n = (uint8_t)(b - a);
            g_tools.clip_scope = (uint8_t)scope;
            snprintf(msg, (size_t)n, "Copied the %s", what);
            return false;
        case SQT_PASTE:
            if (!g_tools.clip_n) { snprintf(msg, (size_t)n, "Nothing copied"); return false; }
            keep_undo(s);
            if (g_tools.clip_scope == SQS_ALL) {
                for (int k = 0; k < SEQ_MAX_STEPS; k++) seq_step_clear(&s->step[k]);
                memcpy(s->step, g_tools.clip, (size_t)g_tools.clip_n * sizeof t);
                s->steps = g_tools.clip_n;
            } else {
                const int at = g_tools.clip_scope == SQS_PAGE ? page : cursor;
                memcpy(&s->step[at], g_tools.clip, (size_t)g_tools.clip_n * sizeof t);
            }
            snprintf(msg, (size_t)n, "Pasted the %s", g_tools.clip_scope == SQS_STEP ? "step" : g_tools.clip_scope == SQS_PAGE ? "page" : "pattern");
            return true;
        case SQT_CLEAR:
            keep_undo(s);
            for (int k = a; k < b; k++) seq_step_clear(&s->step[k]);
            snprintf(msg, (size_t)n, "Cleared the %s", what);
            return true;
        case SQT_SHIFT_L: case SQT_SHIFT_R:
            if (mb - ma < 2) { snprintf(msg, (size_t)n, "One step only"); return false; }
            keep_undo(s);
            if (tool == SQT_SHIFT_L) { t = s->step[ma]; memmove(&s->step[ma], &s->step[ma + 1], (size_t)(mb - ma - 1) * sizeof t); s->step[mb - 1] = t; }
            else                     { t = s->step[mb - 1]; memmove(&s->step[ma + 1], &s->step[ma], (size_t)(mb - ma - 1) * sizeof t); s->step[ma] = t; }
            snprintf(msg, (size_t)n, "Shifted %s", tool == SQT_SHIFT_L ? "left" : "right");
            return true;
        case SQT_REVERSE:
            if (mb - ma < 2) { snprintf(msg, (size_t)n, "One step only"); return false; }
            keep_undo(s);
            for (int i = ma, j = mb - 1; i < j; i++, j--) { t = s->step[i]; s->step[i] = s->step[j]; s->step[j] = t; }
            snprintf(msg, (size_t)n, "Reversed");
            return true;
        case SQT_DOUBLE: {
            if (s->steps >= SEQ_MAX_STEPS) { snprintf(msg, (size_t)n, "Already %d steps", SEQ_MAX_STEPS); return false; }
            keep_undo(s);
            const int to = 2 * s->steps < SEQ_MAX_STEPS ? 2 * s->steps : SEQ_MAX_STEPS;
            for (int k = s->steps; k < to; k++) s->step[k] = s->step[k - s->steps];
            s->steps = to;
            snprintf(msg, (size_t)n, "%d steps", to);
            return true;
        }
        case SQT_RND_NOTE: {
            int lo = 0, hi = 0;
            for (int k = 0; k < s->steps; k++)
                for (int v = 0; v < SEQ_CHORD; v++) {
                    const int x = s->step[k].note[v];
                    if (!x) continue;
                    if (!lo || x < lo) lo = x;
                    if (x > hi) hi = x;
                }
            if (!lo) { snprintf(msg, (size_t)n, "No notes"); return false; }
            if (lo == hi) { lo = lo - 6 < SEQ_NOTE_MIN ? SEQ_NOTE_MIN : lo - 6; hi = hi + 6 > SEQ_NOTE_MAX ? SEQ_NOTE_MAX : hi + 6; }
            keep_undo(s);
            for (int k = a; k < b; k++)
                for (int v = 0; v < SEQ_CHORD; v++) if (s->step[k].note[v]) s->step[k].note[v] = (uint8_t)(lo + rnd(hi - lo + 1));
            snprintf(msg, (size_t)n, "Random notes");
            return true;
        }
        case SQT_HUMANISE: case SQT_RND_PROB:
            keep_undo(s);
            for (int k = a; k < b; k++) {
                if (seq_step_is_rest(&s->step[k])) continue;
                if (tool == SQT_HUMANISE) set_u8(&s->step[k].vel, s->step[k].vel + rnd(41) - 20, 1, 127);
                else s->step[k].prob = (uint8_t)(50 + 5 * rnd(11));
            }
            snprintf(msg, (size_t)n, tool == SQT_HUMANISE ? "Velocities moved" : "Random chances");
            return true;
        case SQT_UNDO: {
            if (!g_tools.undo_steps) { snprintf(msg, (size_t)n, "Nothing to undo"); return false; }
            const int steps = s->steps;
            for (int k = 0; k < SEQ_MAX_STEPS; k++) { t = s->step[k]; s->step[k] = g_tools.undo[k]; g_tools.undo[k] = t; }
            s->steps = g_tools.undo_steps;
            g_tools.undo_steps = (uint8_t)steps;
            snprintf(msg, (size_t)n, "Undone");
            return true;
        }
        default: break;
    }
    msg[0] = 0;
    return false;
}

bool seq_param_adjust(seq_t *s, seq_param_id_t id, int dir) {
    const seq_desc_t *d = &table[id];
    int *v = (int *)((uint8_t *)s + d->offset);
    int nv = *v + dir * d->step;
    if (nv < d->min) nv = d->min;
    if (nv > d->max) nv = d->max;
    bool changed = nv != *v;
    *v = nv;
    return changed;
}

const char *seq_param_label(seq_param_id_t id) { return table[id].label; }

void seq_param_format(const seq_t *s, seq_param_id_t id, char *out, size_t n) {
    static const char *const rate[SQR_COUNT] = {"1/8", "1/8T", "1/16", "1/16T", "1/32"};
    static const char *const dir[SQD_COUNT] = {"Fwd", "Rev", "Ping", "Rand"};
    const seq_desc_t *d = &table[id];
    int v = *(const int *)((const uint8_t *)s + d->offset);
    if (id == SQP_TRANSPOSE)  snprintf(out, n, "%+d%s", v, d->unit);
    else if (id == SQP_RATE)  snprintf(out, n, "%s", rate[v >= 0 && v < SQR_COUNT ? v : SQR_16]);
    else if (id == SQP_DIR)   snprintf(out, n, "%s", dir[v >= 0 && v < SQD_COUNT ? v : SQD_FORWARD]);
    else                      snprintf(out, n, "%d%s", v, d->unit);
}

void seq_note_name(int note, char *buf, int size) {
    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note <= 0) snprintf(buf, size, "--");
    else snprintf(buf, size, "%s%d", names[note % 12], note / 12 - 1);
}

int seq_tap(seq_tap_t *t, uint32_t now_ms) {
    if (!t->on || now_ms - t->last_ms > SEQ_TAP_RESET_MS) { t->on = true; t->n = 0; t->last_ms = now_ms; return 0; }
    for (int i = 3; i > 0; i--) t->iv[i] = t->iv[i - 1];
    t->iv[0] = now_ms - t->last_ms;
    t->last_ms = now_ms;
    if (t->n < 4) t->n++;
    uint32_t sum = 0;
    for (int i = 0; i < t->n; i++) sum += t->iv[i];
    if (!sum) return 0;
    const int bpm = (int)((60000u * (uint32_t)t->n + sum / 2) / sum);
    return bpm < 40 ? 40 : bpm > 240 ? 240 : bpm;
}
