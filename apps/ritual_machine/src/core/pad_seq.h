#pragma once
// The Pads section's sequencer (ENGINE_DESIGN.md ADR-042, stage 3; user, 2026-10-12): the pattern and the bank of patterns, as the UI
// edits them. Pure data and its rules: no HAL / engine dependency, no timing. It PLAYS in the engine, on the machine clock (core/audio.h
// audio_pseq_set), like the synth's sequencer (core/seq.h).
//
// A pattern covers all 64 pads. It is kept as a list of HITS (a pad on a step), not as a 64 x 64 grid: a hit is one 32-bit bit-field
// (the user: "compact ram/rom usage with bit-field struct") and a pattern holds PSEQ_HITS of them at most, so a pattern is 0.65 KB.
// A hit: the pad, the step, a level (16 steps of the pad's own level), a chance, a ratchet (hits inside the step) and a length in
// steps (how long a Gate or Toggle pad is held; a one-shot pad plays to its end whatever it says).
#include <stdbool.h>
#include <stdint.h>
#include "core/seq.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PSEQ_HITS      160
#define PSEQ_PATTERNS  SEQ_PATTERNS
#define PSEQ_LEVEL_MAX 15                   // level 0..15: (level + 1) / 16 of the pad's level
#define PSEQ_PROB_MAX  10                   // chance 0..10: tens of %

typedef struct {
    uint32_t pad     : 6;                   // 0..63
    uint32_t step    : 6;                   // 0..63
    uint32_t level   : 4;                   // 0..PSEQ_LEVEL_MAX
    uint32_t prob    : 4;                   // 0..PSEQ_PROB_MAX
    uint32_t ratchet : 2;                   // 0..3: 1..4 hits
    uint32_t len     : 6;                   // 0..63: 1..64 steps
} pseq_hit_t;

typedef struct {
    pseq_hit_t hit[PSEQ_HITS];              // in no order; the unused ones are zero
    uint8_t    n;                           // hits in use
    uint8_t    steps, rate, dir, swing;     // 1..SEQ_MAX_STEPS, seq_rate_t, seq_dir_t, 0..50 %
} pseq_pattern_t;

// The settings of a pattern the UI edits generically (the PAD SEQ page).
typedef enum { PSP_STEPS, PSP_SWING, PSP_RATE, PSP_DIR, PSP_COUNT } pseq_param_t;

// The patterns and their chain. The patterns themselves are the application's (bulk memory); the one that plays is edited in place.
typedef struct {
    pseq_pattern_t *pat;                    // PSEQ_PATTERNS of them
    uint8_t on;                             // the sequencer plays with the Pads clock (off: the clock runs without it)
    uint8_t cur;                            // the pattern that plays and is edited
    int8_t  next;                           // the pattern picked while it runs: it follows at the end of the playing one (-1: none)
    uint8_t chain_on, chain_len, chain_pos; // as seq_bank_t
    uint8_t chain[SEQ_CHAIN_MAX];
} pseq_bank_t;

void pseq_clear(pseq_pattern_t *p);                              // no hits, 16 steps, 1/16, forward
int  pseq_find(const pseq_pattern_t *p, int pad, int step);      // the hit's index, -1 when the pad has none on that step
// A hit added with the default values (full level, always, one hit, one step) or taken away. False: the pattern is full.
bool pseq_toggle(pseq_pattern_t *p, int pad, int step);
// A hit set (added, or its values replaced). The values as the page shows them: level 1..16, chance 0..100 %, ratchet 1..4, length
// 1..64. False: the pattern is full.
bool pseq_put(pseq_pattern_t *p, int pad, int step, int level, int prob, int ratchet, int len);
void pseq_remove(pseq_pattern_t *p, int index);
int  pseq_clear_pad(pseq_pattern_t *p, int pad);                 // every hit of a pad goes; how many there were
uint64_t pseq_track(const pseq_pattern_t *p, int pad);           // bit s = the pad has a hit on step s
uint64_t pseq_used(const pseq_pattern_t *p);                     // bit k = pad k has hits (inside the pattern's steps)
// A hit's values, one step up / down (dir +1 / -1). True if it changed.
typedef enum { PSH_LEVEL, PSH_PROB, PSH_RATCHET, PSH_LEN, PSH_COUNT } pseq_value_t;
bool pseq_hit_adjust(pseq_hit_t *h, pseq_value_t what, int dir);
void pseq_hit_format(const pseq_hit_t *h, pseq_value_t what, char *out, int n);
const char *pseq_value_label(pseq_value_t what);                 // "Lvl", "Prob", "Rtch", "Len"

bool        pseq_param_adjust(pseq_pattern_t *p, pseq_param_t k, int dir);
const char *pseq_param_label(pseq_param_t k);
void        pseq_param_format(const pseq_pattern_t *p, pseq_param_t k, char *out, int n);

uint32_t pseq_hash(const pseq_pattern_t *p);
// Text, for the card. A pattern:  padpattern <steps> <swing> <rate> <direction>, then one line per hit:
//   hit <pad 0..63> <step 0..63> <level 1..16> <chance %> <ratchet> <length>     (what is left out at the end is the default)
// The bank:  padseq <on> <current pattern 1..8> <chain on>  and  chain <its entries, 1..8>.
// to_text: the length, -1 when it does not fit. from_text: false when it is not such a text (nothing is changed).
int  pseq_to_text(const pseq_pattern_t *p, char *buf, int cap);
bool pseq_from_text(pseq_pattern_t *p, const char *txt);
int  pseq_bank_to_text(const pseq_bank_t *b, char *buf, int cap);
bool pseq_bank_from_text(pseq_bank_t *b, const char *txt);
void pseq_bank_init(pseq_bank_t *b, pseq_pattern_t *patterns);   // every pattern empty; on; a chain of one entry, off

#ifdef __cplusplus
}
#endif
