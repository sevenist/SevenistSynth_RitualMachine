#include "core/seq.h"
#include <stdio.h>

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
};

void seq_init(seq_t *s) {
    static const uint8_t demo[SEQ_MAX_STEPS] = {48, 0, 55, 0, 60, 0, 55, 58, 48, 0, 55, 0, 63, 62, 60, 0};
    for (int i = 0; i < SEQ_MAX_STEPS; i++) { s->note[i] = demo[i]; s->len[i] = 1; }
    s->len[0] = 2; s->len[4] = 2; s->len[8] = 2; s->len[14] = 2;
    s->bpm = 110;
    s->steps = SEQ_MAX_STEPS;
    s->transpose = 0;
    s->swing = 0;
    s->running = false;
    for (int i = 0; i < SEQ_POLY; i++) { s->held[i] = 0; s->gate[i] = 0; }
    s->pos = 0;
    s->fresh = false;
    s->next_at = 0;
}

void seq_set_running(seq_t *s, bool run) {
    if (run && !s->running) { s->pos = s->steps - 1; s->fresh = true; }
    s->running = run;
}

bool seq_adjust_note(seq_t *s, int step, int dir) {
    int n = s->note[step], old = n;
    if (n == 0) { if (dir > 0) n = SEQ_NOTE_MIN; }
    else if (n + dir < SEQ_NOTE_MIN) n = 0;
    else if (n + dir <= SEQ_NOTE_MAX) n += dir;
    s->note[step] = (uint8_t)n;
    return n != old;
}

bool seq_adjust_len(seq_t *s, int step, int dir) {
    int l = s->len[step] + dir;
    if (l < 1) l = 1;
    if (l > SEQ_LEN_MAX) l = SEQ_LEN_MAX;
    bool changed = l != s->len[step];
    s->len[step] = (uint8_t)l;
    return changed;
}

bool seq_param_adjust(seq_t *s, seq_param_id_t id, int dir) {
    const seq_desc_t *d = &table[id];
    int *v = (int *)((uint8_t *)s + d->offset);
    int nv = *v + dir * d->step;
    if (nv < d->min) nv = d->min;
    if (nv > d->max) nv = d->max;
    bool changed = nv != *v;
    *v = nv;
    if (s->pos >= s->steps) s->pos = 0;     // pattern got shorter than the playhead
    return changed;
}

const char *seq_param_label(seq_param_id_t id) { return table[id].label; }

void seq_param_format(const seq_t *s, seq_param_id_t id, char *out, size_t n) {
    const seq_desc_t *d = &table[id];
    int v = *(const int *)((const uint8_t *)s + d->offset);
    if (id == SQP_TRANSPOSE) snprintf(out, n, "%+d%s", v, d->unit);
    else                     snprintf(out, n, "%d%s", v, d->unit);
}

static void release_slot(seq_t *s, seq_event_t *ev, int i) {
    ev->off[ev->n_off++] = s->held[i];
    s->held[i] = 0;
}

bool seq_tick(seq_t *s, uint32_t now_ms, seq_event_t *ev) {
    ev->n_off = 0;
    ev->on = 0;
    if (!s->running) {
        for (int i = 0; i < SEQ_POLY; i++) if (s->held[i]) release_slot(s, ev, i);   // stopped: release everything
        return false;
    }
    if (s->fresh) { s->fresh = false; s->next_at = now_ms; }
    if ((int32_t)(now_ms - s->next_at) < 0) return false;

    s->pos = (s->pos + 1) % s->steps;

    // Swing: the gap before an odd step is longer, the gap after it shorter.
    uint32_t step_ms = 15000u / (uint32_t)s->bpm;      // 60000 / bpm / 4
    uint32_t gap = step_ms * (uint32_t)(100 + ((s->pos & 1) ? -s->swing : s->swing)) / 100;
    s->next_at += gap;
    if ((int32_t)(now_ms - s->next_at) >= 0) s->next_at = now_ms + gap;   // fell behind: resync

    // Age the held notes: those whose gate is over are released (notes longer than one step
    // keep ringing over the following steps = polyphony).
    for (int i = 0; i < SEQ_POLY; i++)
        if (s->held[i] && --s->gate[i] <= 0) release_slot(s, ev, i);

    if (s->note[s->pos]) {
        int n = s->note[s->pos] + s->transpose;
        if (n < 1) n = 1;
        if (n > 127) n = 127;
        int free_slot = -1, oldest = 0;
        for (int i = 0; i < SEQ_POLY; i++) {
            if (s->held[i] == n) release_slot(s, ev, i);          // same pitch is retriggered (notes are keyed by number)
            if (s->held[i] && s->gate[i] < s->gate[oldest]) oldest = i;
        }
        for (int i = 0; i < SEQ_POLY; i++) if (!s->held[i]) { free_slot = i; break; }
        if (free_slot < 0) { free_slot = oldest; release_slot(s, ev, free_slot); }   // all busy: steal the oldest
        s->held[free_slot] = n;
        s->gate[free_slot] = s->len[s->pos];
        ev->on = n;
    }
    return true;
}

void seq_note_name(int note, char *buf, int size) {
    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note <= 0) snprintf(buf, size, "--");
    else snprintf(buf, size, "%s%d", names[note % 12], note / 12 - 1);
}
