#pragma once
// Editable 6-operator FM patch (DX7 style) and the factory bank (originally AMY's DX7 patches, see tools/gen_dx7.py).
// Portable data + editing helpers; platform/engine/dx7_convert.cpp turns a dx7_patch_t into the engine's FM voice.
//
// Envelope: 4 stages. Stage i moves to eg_l[i] (level 0..99) in eg_t[i] milliseconds; stages 1..3
// run on note-on, stage 4 (the release) runs on note-off and normally ends at 0. Levels use the
// DX7 scale: 99 = full, every 8 steps = 6 dB.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DX7_OPS            6
#define DX7_FACTORY_COUNT  128
#define DX7_ALGORITHMS     32
#define DX7_NAME_LEN       12

typedef struct {
    uint8_t  level;        // output level 0..99
    float    coarse;       // frequency ratio = coarse + fine (ignored when fixed_hz > 0)
    float    fine;
    float    fixed_hz;     // 0 = follow the note (ratio mode), > 0 = fixed frequency
    uint8_t  eg_l[4];      // envelope levels 0..99
    uint32_t eg_t[4];      // envelope stage times in ms
} dx7_op_t;

typedef struct {
    char     name[DX7_NAME_LEN];
    uint8_t  algorithm;    // 1..32
    float    feedback;     // 0..1 (operator feedback amount)
    uint32_t rel_ms;       // release tail of the whole voice
    dx7_op_t op[DX7_OPS];  // op[0] = operator 1 ... op[5] = operator 6
} dx7_patch_t;

extern const dx7_patch_t dx7_factory[DX7_FACTORY_COUNT];    // generated: dx7_factory.c

void dx7_load_factory(dx7_patch_t *p, int index);           // copies a factory patch for editing

static inline float dx7_ratio(const dx7_op_t *o) { return o->coarse + o->fine; }

/* ---- editing: same adjust / format shape as the other parameter sets (dir = +1 / -1) ---- */

typedef enum { DXP_LEVEL, DXP_COARSE, DXP_FINE, DXP_FIXED, DXP_COUNT } dx7_op_param_t;
typedef enum { DXE_LEVEL, DXE_TIME } dx7_eg_field_t;

bool        dx7_op_adjust(dx7_patch_t *p, int op, dx7_op_param_t id, int dir);
const char *dx7_op_label(dx7_op_param_t id);
void        dx7_op_format(const dx7_patch_t *p, int op, dx7_op_param_t id, char *out, size_t n);

bool dx7_eg_adjust(dx7_patch_t *p, int op, int point, dx7_eg_field_t field, int dir);
void dx7_eg_format(const dx7_patch_t *p, int op, int point, dx7_eg_field_t field, char *out, size_t n);

bool dx7_algorithm_adjust(dx7_patch_t *p, int dir);   // 1..32
bool dx7_feedback_adjust(dx7_patch_t *p, int dir);    // 0..1 in steps of 0.02
void dx7_feedback_format(const dx7_patch_t *p, char *out, size_t n);

// Level (0..99) -> linear amplitude (2 at 99, halving every 8 steps) and envelope value.
float dx7_amp(int level);
float dx7_env_value(int level);

#ifdef __cplusplus
}
#endif
