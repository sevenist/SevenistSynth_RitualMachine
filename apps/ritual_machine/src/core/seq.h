#pragma once
// The synth's step sequencer: the pattern and its settings, as the UI edits them. Pure data and its rules: no HAL / engine / u8g2
// dependency, and no timing. It PLAYS in the engine, on the machine clock (core/audio.h audio_seq_set; engine/core/sequencer.h,
// ENGINE_DESIGN.md ADR-042): the application hands this model over at every step and reads back the step that plays.
//
// A step: up to SEQ_CHORD notes (a chord), a gate length in steps, a velocity, a probability and a ratchet (hits inside the step).
// Global settings (BPM, pattern length, transpose, swing, step rate, direction) are edited generically through the seq_param_* API,
// like synth_params.
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEQ_MAX_STEPS   64
#define SEQ_PAGE        16    // steps shown at once; the motion sequencers' lanes are that long
#define SEQ_CHORD       4     // notes of one step
#define SEQ_NOTE_MIN    12    // C0: lowest editable note; 0 = none
#define SEQ_NOTE_MAX    108   // C8
#define SEQ_LEN_MAX     SEQ_MAX_STEPS
#define SEQ_RATCHET_MAX 4
#define SEQ_VEL_DEFAULT 100

typedef struct {
    uint8_t note[SEQ_CHORD];    // MIDI notes, 0 = none (all 0: a rest)
    uint8_t len;                // gate length in steps (1..SEQ_LEN_MAX)
    uint8_t vel;                // 1..127
    uint8_t prob;               // the chance in % that the step plays (0..100)
    uint8_t ratchet;            // 1..SEQ_RATCHET_MAX hits inside the step
} seq_step_t;

// Global settings editable by the UI. The numbers are saved (ui.cfg: a knob's target): append only.
typedef enum { SQP_BPM, SQP_STEPS, SQP_TRANSPOSE, SQP_SWING, SQP_RATE, SQP_DIR, SQP_COUNT } seq_param_id_t;
typedef enum { SQR_8, SQR_8T, SQR_16, SQR_16T, SQR_32, SQR_COUNT } seq_rate_t;       // a step = an 8th note .. a 32nd (T: triplets)
typedef enum { SQD_FORWARD, SQD_REVERSE, SQD_PINGPONG, SQD_RANDOM, SQD_COUNT } seq_dir_t;

typedef struct {
    seq_step_t step[SEQ_MAX_STEPS];
    int      bpm;                   // 40..240
    int      steps;                 // active pattern length 1..SEQ_MAX_STEPS
    int      transpose;             // semitones added at playback, -24..24
    int      swing;                 // % of a step by which every second step is delayed, 0..50
    int      rate;                  // seq_rate_t
    int      dir;                   // seq_dir_t
    bool     running;
    int      pos;                   // the step that plays, as the engine reports it (-1: none)
    uint8_t  last_note;             // the note a rest takes when it is raised: the last one entered
} seq_t;

/* ---- Several patterns and a chain (user, 2026-10-11: kept with the machine, not in the presets). seq_t is the pattern that plays and
 * is edited; the bank holds all SEQ_PATTERNS of them as stored (the current one: as it was when it was taken or last stored). ---- */
#define SEQ_PATTERNS  8
#define SEQ_CHAIN_MAX 16
typedef struct {
    seq_step_t step[SEQ_MAX_STEPS];
    uint8_t    steps, rate, dir, swing;     // (the tempo is the machine's, not a pattern's)
    int8_t     transpose;
} seq_pattern_t;
typedef struct {
    seq_pattern_t *pat;             // SEQ_PATTERNS of them: the application's (bulk memory); NULL = only the pattern in seq_t
    uint8_t cur;                    // the pattern that is in seq_t
    int8_t  next;                   // the pattern picked while it runs: it follows at the end of the playing one (-1: none)
    uint8_t chain_on;               // the chain plays: its entries one after the other, around
    uint8_t chain_len;              // 1..SEQ_CHAIN_MAX
    uint8_t chain_pos;              // the entry that plays
    uint8_t chain[SEQ_CHAIN_MAX];   // pattern numbers 0..SEQ_PATTERNS-1
} seq_bank_t;

void seq_pattern_clear(seq_pattern_t *p);                        // 16 rests, 1/16, forward
void seq_pattern_get(const seq_t *s, seq_pattern_t *p);          // the playing pattern as a stored one
void seq_pattern_put(seq_t *s, const seq_pattern_t *p);          // ... and back (the tempo, running and the playhead stay)
void seq_bank_init(seq_bank_t *b, seq_pattern_t *patterns, const seq_t *s);   // pattern 1 = s, the others empty; a chain of one entry, off
void seq_bank_select(seq_bank_t *b, seq_t *s, int k);            // s is stored in its place, pattern k becomes s
uint32_t seq_pattern_hash(const seq_pattern_t *p);
// Text, for the card. A pattern:  pattern <steps> <transpose> <swing> <rate> <direction>, then one line per step that is not a plain
// rest:  step K <note> <length> <velocity> <probability> <ratchet> <note 2> <note 3> <note 4>  (what is left out at the end is the
// default). The bank's settings:  seq <bpm> <current pattern 1..8> <chain on>  and  chain <its entries, 1..8>.
// to_text: the length, -1 when it does not fit. from_text: false when it is not such a text (nothing is changed).
int  seq_pattern_to_text(const seq_pattern_t *p, char *buf, int cap);
bool seq_pattern_from_text(seq_pattern_t *p, const char *txt);
int  seq_bank_to_text(const seq_bank_t *b, int bpm, char *buf, int cap);
bool seq_bank_from_text(seq_bank_t *b, int *bpm, const char *txt);

void seq_init(seq_t *s);
void seq_step_clear(seq_step_t *st);                 // a rest with the default length, velocity, probability and ratchet
void seq_set_running(seq_t *s, bool run);
bool seq_step_is_rest(const seq_step_t *st);
static inline int seq_motion_steps(const seq_t *s) { return s->steps < SEQ_PAGE ? s->steps : SEQ_PAGE; }   // the steps a motion lane plays

// Per-step edits. dir = +1 / -1. Return true if the value changed.
bool seq_adjust_note(seq_t *s, int step, int voice, int dir);   // none <-> SEQ_NOTE_MIN..MAX (raised from none: the last note entered)
bool seq_adjust_len(seq_t *s, int step, int dir);               // 1..SEQ_LEN_MAX steps
bool seq_adjust_vel(seq_t *s, int step, int dir);               // 1..127, in fives
bool seq_adjust_prob(seq_t *s, int step, int dir);              // 0..100 %, in fives
bool seq_adjust_ratchet(seq_t *s, int step, int dir);           // 1..SEQ_RATCHET_MAX

// A step's values from a knob's travel (x = 0..1), for the SEQUENCER page's column knobs: the note (the bottom of the travel = none,
// then SEQ_NOTE_MIN..MAX), the length 1..SEQ_PAGE steps, the velocity 1..127, the probability 0..100 % in fives. True if it changed.
typedef enum { SQK_NOTE, SQK_LEN, SQK_VEL, SQK_PROB, SQK_COUNT } seq_knob_t;
bool seq_set_norm(seq_t *s, int step, int voice, seq_knob_t what, float x);

// The quick edit tools (the SEQUENCER page's Tool row; user, 2026-10-11), applied to the cursor's step, its page of SEQ_PAGE steps or
// the whole pattern (the scope):
//   Copy / Paste   the clipboard holds a step, a page or a pattern; Paste puts it at the cursor (its step, its page) or replaces the
//                  pattern with it, whatever the scope says
//   Clear          rests with the default values
//   Shift < / >    the steps move one place, around (the page's, or the pattern's: also with the scope Step)
//   Reverse        the same range, back to front
//   Double         the pattern twice as long (SEQ_MAX_STEPS at most), its steps copied into the new half
//   RndNote        every note becomes a random one between the pattern's lowest and highest (an octave around a single pitch)
//   Human          every velocity moves by up to +-20          RndProb   every probability becomes 50..100 %
//   Undo           the pattern as it was before the last tool (again: the tool's result back)
// Rests stay rests. The clipboard and the undo copy are the module's own (one sequencer).
typedef enum { SQT_COPY, SQT_PASTE, SQT_CLEAR, SQT_SHIFT_L, SQT_SHIFT_R, SQT_REVERSE, SQT_DOUBLE, SQT_RND_NOTE, SQT_HUMANISE, SQT_RND_PROB,
               SQT_UNDO, SQT_COUNT } seq_tool_t;
typedef enum { SQS_STEP, SQS_PAGE, SQS_ALL, SQS_COUNT } seq_scope_t;
const char *seq_tool_name(int tool);
const char *seq_scope_name(int scope);
// Runs a tool at step `cursor`. `msg` (n characters) says what was done, or why nothing was. True when the pattern changed.
bool seq_tool_apply(seq_t *s, int tool, int scope, int cursor, char *msg, int n);
void seq_tools_reset(void);                          // an empty clipboard, nothing to undo, the random numbers from their start

// Global settings, same shape as param_adjust()/param_label()/param_format().
bool        seq_param_adjust(seq_t *s, seq_param_id_t id, int dir);
const char *seq_param_label(seq_param_id_t id);
void        seq_param_format(const seq_t *s, seq_param_id_t id, char *out, size_t n);

// Writes e.g. "C#4" or "--" (none).
void seq_note_name(int note, char *buf, int size);

// Tap tempo: call at every tap; returns the BPM of the last taps (up to 4 intervals averaged, 40..240), 0 after the first tap. A pause
// of more than SEQ_TAP_RESET_MS starts again.
#define SEQ_TAP_RESET_MS 2000
typedef struct { uint32_t last_ms; uint32_t iv[4]; int n; bool on; } seq_tap_t;
int seq_tap(seq_tap_t *t, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
