#pragma once
// Step sequencer (monophonic, 16th-note steps). Pure logic: time comes from the caller,
// notes are returned to the caller. No HAL/engine/u8g2 dependency.
//
// Per-step data: note + length. Global settings (BPM, pattern length, transpose, swing)
// are edited generically through the seq_param_* API, like synth_params.
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEQ_MAX_STEPS 16
#define SEQ_NOTE_MIN  48    // C3: lowest editable note; 0 = rest
#define SEQ_NOTE_MAX  71    // B4
#define SEQ_LEN_MAX   16
#define SEQ_POLY      8     // simultaneously sounding sequencer notes

// Global settings editable by the UI.
typedef enum { SQP_BPM, SQP_STEPS, SQP_TRANSPOSE, SQP_SWING, SQP_COUNT } seq_param_id_t;

typedef struct {
    uint8_t  note[SEQ_MAX_STEPS];   // MIDI note, 0 = rest
    uint8_t  len[SEQ_MAX_STEPS];    // gate length in steps (1..SEQ_LEN_MAX)
    int      bpm;                   // 40..240
    int      steps;                 // active pattern length 1..SEQ_MAX_STEPS
    int      transpose;             // semitones added at playback, -24..24
    int      swing;                 // % of a step by which odd steps are delayed, 0..50
    bool     running;
    int      held[SEQ_POLY];        // notes currently sounding (0 = free slot); overlapping notes ring together
    int      gate[SEQ_POLY];        // steps until the matching held note is released
    int      pos;                   // step played last (valid while running)
    bool     fresh;                 // just started: first tick plays step 0 immediately
    uint32_t next_at;               // time (ms) of the next step
} seq_t;

void seq_init(seq_t *s);
void seq_set_running(seq_t *s, bool run);

// Per-step edits. dir = +1 / -1. Return true if the value changed.
bool seq_adjust_note(seq_t *s, int step, int dir);   // rest <-> SEQ_NOTE_MIN..MAX
bool seq_adjust_len(seq_t *s, int step, int dir);    // 1..SEQ_LEN_MAX steps

// Global settings, same shape as param_adjust()/param_label()/param_format().
bool        seq_param_adjust(seq_t *s, seq_param_id_t id, int dir);
const char *seq_param_label(seq_param_id_t id);
void        seq_param_format(const seq_t *s, seq_param_id_t id, char *out, size_t n);

// What to do after a tick: release off[0..n_off-1] THEN start `on` (0 = nothing).
typedef struct { int off[SEQ_POLY]; int n_off; int on; } seq_event_t;

// Call often with a monotonic ms clock. *ev is always filled. Returns true when a new
// step starts. After seq_set_running(false) the next tick releases every held note.
bool seq_tick(seq_t *s, uint32_t now_ms, seq_event_t *ev);

// Writes e.g. "C#4" or "--" (rest).
void seq_note_name(int note, char *buf, int size);

#ifdef __cplusplus
}
#endif
