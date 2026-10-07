#pragma once
// Response curves and knob mappings (synth_ui.h: mapping_t). A curve turns a knob position 0..1 into 0..1 through a table read with
// linear interpolation (cheap on the board). Built in: Lin, Exp, Log, S (CURVE_LUT points). User curves U1..U8 (ids CURVE_USER_FIRST + k):
// 2..16 points (x, y 0..100), each saying whether the segment after it is linear or stepped (held until the next point); turned into a
// CURVE_USER_LUT table whenever they change. Edited in the CURVES menu tab (scr_curves.c), saved in CURVES_FILE on the card.
// A mapping whose user curve was deleted reads it as linear.
#include <stdbool.h>
#include <stdint.h>
#include "core/synth_ui.h"
#include "hal/hal_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CURVE_LUT 33
enum { CURVE_LIN, CURVE_EXP, CURVE_LOG, CURVE_S, CURVE_BUILTIN, CURVE_USER_FIRST = 16 };

float       curve_eval(int id, float x);            // x 0..1 -> 0..1 (an unknown id or a deleted user curve is linear)
const char *curve_name(int id);                     // "Lin", "Exp", "Log", "S", "U1".."U8"
int         curve_by_name(const char *name);        // case-insensitive; -1 when unknown
// The curves a mapping can choose: the built-ins, then the user curves that exist. step: the next / previous one from `id`.
int         curve_count(void);
int         curve_at(int i);
int         curve_step(int id, int dir);

/* ---- user curves ---- */
#define CURVE_USER_MAX   8
#define CURVE_POINTS_MAX 16
#define CURVE_USER_LUT   129
#define CURVES_FILE      STORAGE_DIR_CONFIG "/curves.cfg"
enum { CURVE_PT_LIN, CURVE_PT_STEP };
typedef struct { uint8_t x, y, mode; } curve_point_t;           // x, y 0..100; mode: the segment from this point to the next
typedef struct { bool used; uint8_t n; curve_point_t pt[CURVE_POINTS_MAX]; uint16_t lut[CURVE_USER_LUT]; } user_curve_t;

void                 curves_init(void);                     // no user curves
const user_curve_t  *curve_user(int k);                     // k 0..CURVE_USER_MAX-1
int   curve_user_new(int k);                                // makes slot k a curve (0,0) -> (100,100) linear; returns k, -1 when it exists
void  curve_user_delete(int k);
int   curve_user_add_point(int k, int after);               // a point halfway to the next one (or after the last); returns its index, -1 when full
void  curve_user_remove_point(int k, int i);                // a curve keeps at least 2 points
void  curve_user_set_point(int k, int i, int x, int y);     // x stays between the neighbours (the points keep their order)
void  curve_user_set_mode(int k, int i, int mode);
unsigned curves_rev(void);                                  // changes on every edit

// curves.cfg: "curve 1" then "point X Y lin|step" lines. from_text replaces every user curve (false: not a curve file, nothing changed).
int  curves_to_text(char *buf, int cap);
bool curves_from_text(const char *txt);
bool curves_load(void);                                     // false: no card / no file (the curves are kept)
bool curves_save(void);
bool curves_changed(void);                                  // edited since the last load / save

// A mapping: knob 0..1 -> the curve -> onto Min..Max (% of the target's range) -> the target's value 0..1.
float mapping_apply(const mapping_t *m, float knob);
// The first knob position 0..MAPPING_POSITIONS-1 whose output is nearest `v` (the catch compares knob positions with it).
#define MAPPING_POSITIONS 256
int   mapping_inverse(const mapping_t *m, float v);
bool  mapping_is_plain(const mapping_t *m);         // full range, linear

#ifdef __cplusplus
}
#endif
