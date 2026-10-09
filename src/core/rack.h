#pragma once
// The synth "rack": an ordered list of module slots the user builds on screen.
// Pure data + rules (no drawing, no audio). The audio engine will compile it later.
//
// Signal rules (left to right):
//  - Audio modules (OSC, FILTER, SAT) form ONE serial chain in slot order, skipping
//    modulators. Each audio module's input is the previous audio module's output and its
//    output goes to the next audio module, or to the OUT at the end of the rack.
//  - A source (OSC, SM = sampler) ADDS its signal to the running chain; a processor (FILTER, SAT)
//    processes everything to its left. So "OC OC FL SA" = two oscillators summed, then
//    filtered, then saturated; "OC FL OC" filters only the first oscillator.
//  - Modulators (LFO, MSEQ, ENV) are not in the audio chain; each has a target:
//    a parameter of any other module (follows the module when slots shift).
//    An OSC is always a source in the chain. Given a target, its own output (at its own pitch)
//    also modulates that parameter (FM, AM, ...); its Mute switch silences it in the chain
//    while the modulation stays intact.
//
// Rows (ADR-041): two branches and row M.
//  - Every module sits on one of RACK_ROWS rows (slot.row). Rows 0 and 1 are two parallel branches: the same notes and voices, each a
//    chain as above (a source adds to its row, a processor processes what is to its left in its row). Row M (ROW_M) is a chain on the sum
//    of both branches (the MIX: rack_t.br_lvl / br_pan, level and pan of each branch); it runs once (shared), then the output.
//    The slot array keeps one global order; a row is that order filtered by row. A modulator's row is only where the editor shows it.
//  - Para: a processor of a branch with slot.para on runs once on the sum of the branch's voices, and so does everything after it in
//    that branch (rack_shared_start). The heavy FX (rack_type_shared_only) always run shared, so they start the shared part too.
//    One way only: a module that must run per voice (rack_type_voice_only: sources, the resonator) cannot follow the shared start of its
//    branch, nor sit in row M. The shared start's slot.penv says how the shared envelopes follow the keys (para_env_t).
//  - FM and Strings have no branches: their voices feed row M.
#include <stdbool.h>
#include <stdint.h>
#include "core/synth_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RACK_MAX 16                 // slots, shared by the rows (the rack strip of the UI scrolls sideways, see RACK_VIS in ui_internal.h)
#define RACK_ROWS 3
#define RACK_BRANCHES 2             // rows 0 and 1
#define ROW_M 2                     // the chain on the sum of the branches
#define RACK_OUT (-2)               // returned by rack_audio_next(): goes to the output (a branch: to the MIX of row M)
#define RACK_NONE (-1)

// Order must match MODULES in tools/gen_module_sprites.py. New types are appended (the order is the meaning of slot.type).
typedef enum {
    MOD_OSC, MOD_FILTER, MOD_SAT, MOD_LFO, MOD_MSEQ, MOD_ENV, MOD_SAMPLER, MOD_EG, MOD_COMB,
    MOD_TREM, MOD_EQ, MOD_RING, MOD_PHASER, MOD_FLANGER, MOD_COMP,                            // FX: per voice, or shared after a Para point / in row M
    MOD_DELAY, MOD_REVERB, MOD_CHORUS, MOD_SPECTRAL, MOD_CAB, MOD_ENSEMBLE,                   // FX: always shared (rack_type_shared_only)
    MOD_TYPE_COUNT
} module_type_t;

#define MOD_PARAM_MAX 20    // editable parameters per module (floats; enums store their index)

// Parameter indexes (slot.v[i]) per module type. See the descriptor tables in rack.c.
enum { MP_OC_WAVE, MP_OC_PW, MP_OC_LEVEL, MP_OC_COARSE, MP_OC_FINE, MP_OC_MUTE, MP_OC_DEPTH, MP_OC_MORPH, MP_OC_QUAL, MP_OC_HARM };
#define OC_FIRST_MI 15         // Wav values 15.. are the Mutable Instruments models (engine MiOsc, ADR-039; their order is MiModel in mi_osc.h)
#define OC_WAVE_COUNT 62       // 6 classic waves + 9 engines + 47 models; MP_OC_HARM: the models' third control (Plaits' harmonics)
#define OC_FIRST_ENGINE 6      // Wav values 0..5 are the classic waves, 6.. the engines (Karp Modal FM2 Fold SSaw Vowel Add Dust Strng); PW becomes Timbre, Mrph = morph
                               // MP_OC_QUAL: Blep / Mip / Naive for Saw, Pulse, Tri and the Strng engine (Blep = Mip there)
enum { MP_FL_TYPE, MP_FL_CUT, MP_FL_RES, MP_FL_ENVAMT, MP_FL_A, MP_FL_D, MP_FL_S, MP_FL_R, MP_FL_ACV, MP_FL_DCV, MP_FL_RCV };
enum { MP_SA_MODE, MP_SA_DRIVE, MP_SA_MIX };
enum { MP_LF_SHAPE, MP_LF_RATE, MP_LF_DEPTH };
enum { MP_EN_A, MP_EN_D, MP_EN_S, MP_EN_DEPTH, MP_EN_R, MP_EN_HOLD, MP_EN_START, MP_EN_ACV, MP_EN_DCV, MP_EN_RCV };
// EG (four-point envelope): point i (0..3) = v[3 i] time ms (0 = unused), v[3 i + 1] level %, v[3 i + 2] curve %; then:
enum { MP_EG_SUS = 12, MP_EG_REL, MP_EG_RCV, MP_EG_ONE, MP_EG_DEPTH };
enum { MP_RS_TUNE, MP_RS_FB, MP_RS_DAMP, MP_RS_INT, MP_RS_MIX };    // RS (tuned resonator): Crs st, Fb %, Dmp Hz, Int st, Mix
enum { MP_MS_POOL };    // MS: index into rack_t.ms (not editable)
// FX modules: the parameters of the effect (the former FX rack's), in the rack's units
enum { MP_TR_RATE, MP_TR_DEPTH, MP_TR_SHAPE, MP_TR_MODE };
enum { MP_EQ_LOW, MP_EQ_MID, MP_EQ_MIDF, MP_EQ_HIGH };
enum { MP_RG_MODE, MP_RG_FREQ, MP_RG_MIX };
enum { MP_PH_RATE, MP_PH_DEPTH, MP_PH_FB, MP_PH_MIX };                     // also the flanger (MP_PH_* = MP_FG_*)
enum { MP_CP_THR, MP_CP_RATIO, MP_CP_REL, MP_CP_GAIN };
enum { MP_DL_TIME, MP_DL_FB, MP_DL_MIX, MP_DL_PONG };
enum { MP_RV_MIX, MP_RV_DEC, MP_RV_SIZE, MP_RV_DAMP };
enum { MP_CH_MODE, MP_CH_MIX };
enum { MP_SP_MODE, MP_SP_SHIFT, MP_SP_AMT, MP_SP_MIX, MP_SP_HOLD, MP_SP_LO, MP_SP_HI };
enum { MP_CB_IR, MP_CB_LEN, MP_CB_MIX, MP_CB_LEVEL };
enum { MP_ES_RATE, MP_ES_DEPTH, MP_ES_SHIM, MP_ES_MIX };
// SM (sampler): File = catalog index + 1 (0 = none), Slc 0 = whole sample / n = slice n, Out values are indexes of the enums in rack.c
enum { MP_SM_FILE, MP_SM_LEVEL, MP_SM_COARSE, MP_SM_FINE, MP_SM_LOOP, MP_SM_REV, MP_SM_START, MP_SM_TRACK, MP_SM_SLICE, MP_SM_SMODE };    // MS: index into rack_t.ms (not editable)

/* ---- Motion sequencer data (MS modules). Too big for slot.v[], so it lives in a small pool in rack_t; the MS slot's v[MP_MS_POOL]
 *      holds its pool index. Meaning of the fields: see engine/modules/motion_seq.h. ---- */
#define MS_LANES 4
#define MS_STEPS 16         // = SEQ_MAX_STEPS
#define MS_POOL  2          // MS modules a rack can hold

typedef struct {
    uint8_t  val[MS_STEPS];     // 0..100 (%)
    uint16_t active;            // bit i: step i takes part
    uint8_t  tgt_id;            // module id of the target, 0 = none
    uint8_t  tgt_param;         // parameter index in that module (rack_param_name)
    uint8_t  linear;            // 0 = STEP, 1 = LINEAR (between active steps)
    uint8_t  bipolar;           // 1 = value 50 is "no change", 0 = value 0 is "no change"
    float    depth;             // like a modulator's Dpth
} ms_lane_t;

typedef struct { ms_lane_t lane[MS_LANES]; } ms_pattern_t;

typedef struct {
    uint8_t type;        // module_type_t
    uint8_t id;          // unique, stable while the module exists (slot index changes)
    uint8_t tgt_id;      // modulators: id of the targeted module, 0 = none
    uint8_t tgt_param;   // modulators: parameter index in the target (see rack_param_name)
    uint8_t row;         // 0..RACK_ROWS-1: branch 1, branch 2, ROW_M (a modulator's row is only where the editor shows it)
    uint8_t para;        // a branch processor: 1 = from here on the branch runs once on the sum of its voices (rack_set_para)
    uint8_t penv;        // para_env_t: how the shared envelopes follow the keys, read on the branch's shared start (rack_shared_start)
    float   v[MOD_PARAM_MAX];   // the module's own parameter values (see rack_mparam_*)
} rack_slot_t;

typedef struct {
    rack_slot_t slot[RACK_MAX];
    int         count;
    uint8_t     next_id;   // ids start at 1
    float       br_lvl[RACK_BRANCHES], br_pan[RACK_BRANCHES];   // the MIX at the start of row M: each branch's level (0..1) and pan (-100..100 %)
    synth_config_t cfg;    // general settings (synth type, voices, volume): the GENERAL tab
    ms_pattern_t ms[MS_POOL];   // motion sequencer patterns (see ms_lane_t)
} rack_t;

void rack_init(rack_t *r);    // small demo rack (osc, filter, saturator, LFO; delay and reverb in row M; the tests build on it)
void rack_init_sampler(rack_t *r, int file, int loop);   // dev / measurement patch: one sampler (catalog index `file`, loop mode 0 file / 1 off / 2 fwd / 3 ping-pong) into one filter, no effects
void rack_init_startup(rack_t *r);   // the patch the device starts with: four oscillator engines into a filter, delay and reverb in row M
void rack_clear(rack_t *r);   // no modules, default general settings

// Edits. Return false when impossible (full / bad position / against the row rules).
bool rack_insert(rack_t *r, int pos, module_type_t type);   // shifts slots >= pos to the right; row 0 (branch 1), no rule checks
bool rack_insert_row(rack_t *r, int pos, int row, module_type_t type);    // the same on `row`, with the row rules (rack_can_insert)
bool rack_delete(rack_t *r, int pos);                       // clears modulators that targeted it
bool rack_set_row(rack_t *r, int slot, int row);            // moves a module to another row (same slot order), with the row rules
int  rack_add_m(rack_t *r, module_type_t type);             // appends a module to row M (patch setup); its slot, RACK_NONE when refused

// Rows, Para (ADR-041)
bool rack_is_fx(module_type_t t);                // an FX module
bool rack_type_shared_only(module_type_t t);     // the heavy FX: always shared (they start their branch's shared part)
bool rack_type_voice_only(module_type_t t);      // follows the key, so per voice only: the sources and the resonator
bool rack_is_processor(module_type_t t);         // an audio module that processes the chain (not a source)
bool rack_can_para(const rack_t *r, int slot);   // a branch processor that may carry the Para switch (not a heavy FX: always shared)
bool rack_set_para(rack_t *r, int slot, bool on);   // false when refused (a per-voice module after it in the branch)
int  rack_shared_start(const rack_t *r, int row);   // a branch's first shared slot (Para on, or a heavy FX), RACK_NONE: all per voice
bool rack_slot_is_shared(const rack_t *r, int slot);   // runs once: row M, from its branch's shared start on; a modulator: its target's
bool rack_can_insert(const rack_t *r, int pos, int row, module_type_t t);   // what rack_insert_row checks

// Module type info
const char *rack_type_code(module_type_t t);     // "OC"
const char *rack_type_name(module_type_t t);     // "Oscillator"
bool        rack_is_audio(module_type_t t);      // part of the audio chain
bool        rack_is_modulator(module_type_t t);  // pure modulator type (LFO, MSEQ, ENV)
bool        rack_can_target(module_type_t t);    // has a single target setting (LFO, ENV and OSC; MS has one per lane)
int         rack_param_count(module_type_t t);
const char *rack_param_name(module_type_t t, int p);       // short (<= 4 chars): "Cut"
const char *rack_param_long(module_type_t t, int p);       // "Cutoff"

// The module's own editable parameters (what its pages show), stored in slot.v[].
int         rack_mparam_count(module_type_t t);
const char *rack_mparam_label(module_type_t t, int i);
bool        rack_mparam_adjust(rack_slot_t *s, int i, int dir);                   // true if changed
// Continuous access for knobs (as param_norm in synth_params.h): 0..1 of the range, of the log range for a logarithmic one; not enumerations.
// set_norm rounds to the decimals the parameter shows.
bool        rack_mparam_is_continuous(module_type_t t, int i);
float       rack_mparam_norm(const rack_slot_t *s, int i);
bool        rack_mparam_set_norm(rack_slot_t *s, int i, float n);
void        rack_mparam_format(const rack_slot_t *s, int i, char *out, int n);    // "4.0kHz", "Saw"

// Lookup / connections
int  rack_find(const rack_t *r, int id);                   // slot index or RACK_NONE
int  rack_instance(const rack_t *r, int slot);             // 1-based count of this type so far
int  rack_audio_prev(const rack_t *r, int slot);           // previous audio slot or RACK_NONE
int  rack_audio_next(const rack_t *r, int slot);           // next audio slot or RACK_OUT
bool rack_has_signal(const rack_t *r, int slot);           // a source exists at or before slot (row M: a branch has one, or FM / Strings)
bool rack_slot_is_mod(const rack_t *r, int slot);          // modulator type, or an OSC that has a target (it is also in the audio chain)
bool rack_slot_is_audio(const rack_t *r, int slot);        // in the audio chain (OSC, FILTER, SAT)

// Modulator target editing. dir = +1 / -1. Target cycles: none, each other module, none...
void rack_cycle_target(rack_t *r, int slot, int dir);
void rack_cycle_param(rack_t *r, int slot, int dir);

// Motion sequencer lanes (slot must be an MS). A lane's target is a (module, parameter) pair the engine mapper can realise.
ms_pattern_t *rack_ms(rack_t *r, int slot);
const ms_pattern_t *rack_ms_const(const rack_t *r, int slot);
void rack_ms_cycle_target(rack_t *r, int slot, int lane, int dir);                  // none, then every realisable (module, parameter) pair
bool rack_ms_target_ok(module_type_t t, int param);                                 // can a lane drive this parameter?
void rack_ms_target_name(const rack_t *r, const ms_lane_t *l, char *buf, int n);    // "FL1 Cut" or "--"
bool rack_ms_adjust_step(ms_lane_t *l, int step, int dir);                          // value 0..100 in steps of 5 (true = changed)
void rack_ms_toggle_step(ms_lane_t *l, int step);

// Modulation depth in the target's own unit. A modulator's Dpth (and an MS lane's) is a signed value in st (pitch), oct (cutoff, drive) or %
// (level, PW), with a range that suits the target; changing the target resets it to a sensible default of the new unit.
typedef struct { const char *unit; float min, max, step, def; uint8_t decimals; } depth_unit_t;
const depth_unit_t *rack_depth_unit(module_type_t target, int param);      // NULL when the mapper cannot realise that target
int  rack_depth_index(module_type_t modulator);                            // slot.v[] index of the modulator's depth, -1 when it has none
bool rack_depth_adjust(rack_t *r, int slot, int dir);                      // true if changed
void rack_depth_format(const rack_t *r, int slot, char *out, int n);       // "+12st", "1.50oct", "40%"
bool rack_ms_depth_adjust(const rack_t *r, int slot, ms_lane_t *l, int dir);
void rack_ms_depth_format(const rack_t *r, ms_lane_t *l, char *out, int n);

// "OC2" style short name, and "LF1 > FL1 Cutoff" description (modulators) / full name.
void rack_slot_name(const rack_t *r, int slot, char *buf, int n);
void rack_describe(const rack_t *r, int slot, char *buf, int n);

#ifdef __cplusplus
}
#endif
