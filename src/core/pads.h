#pragma once
// The Pads section (ENGINE_DESIGN.md ADR-041, decisions 14-26): an SP-404-style sampler beside the synth, a separate workflow. 4 banks of
// 16 pads; each pad holds a sample of the library (by name, so it survives a rescan of the card) and how it plays. Every pad is mono: a
// push restarts it. The engine side is the Pads module (engine/modules/sampler_modules.h, 8 voices).
//
// Saved in pads.cfg on the card (system/config, text, one line per pad that differs from the defaults):
//   pad A1 mode gate loop 1 start 1200 end 90000 lstart 4800 xfade 40 xpos post from loop tail 50 tune -2 ... grid beat file kick 01
// (start / end / lstart in frames)
// The file name comes last (the rest of the line). Unknown words are skipped, so a file from a later version still loads.
#include <stdbool.h>
#include <stdint.h>
#include "hal/hal_storage.h"
#include "core/rack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAD_BANKS      4
#define PADS_PER_BANK  16
#define PAD_COUNT      (PAD_BANKS * PADS_PER_BANK)
#define PAD_NAME_MAX   24                       // = audio_sample_info_t.name
#define PAD_CHOKES     4                        // choke groups 1..4 (0 = none)
// The EDIT page's operations, in the order the Op row shows them; app.c render_start turns each into an engine operation (PROC_*)
// and its amount. Amounts as the page shows them: Normalize / Gain / Drive 0.1 dB, the fades ms, Trim dBFS, Stretch and Speed percent,
// Pitch cents, the two Fit beats, Paulstretch tenths of the length factor, Lo-fi rate and the filters Hz, Bits bits; Crop (the pad's
// Start..End), Reverse and Remove DC none. Stretch, Pitch and Fit beats run by the page's Algo: Beat (WSOLA) or Tonal (phase vocoder).
enum { EDIT_NORMALIZE, EDIT_GAIN, EDIT_FADE_IN, EDIT_FADE_OUT, EDIT_TRIM, EDIT_CROP, EDIT_REVERSE, EDIT_DC,
       EDIT_STRETCH, EDIT_PITCH, EDIT_SPEED, EDIT_FIT, EDIT_FIT_SPEED, EDIT_PAUL,
       EDIT_RATE, EDIT_BITS, EDIT_DRIVE, EDIT_LOWPASS, EDIT_HIGHPASS, PAD_EDIT_OPS };
// engine/offline/process.h ProcOp numbers
enum { PROC_NORMALIZE, PROC_GAIN, PROC_FADE_IN, PROC_FADE_OUT, PROC_TRIM, PROC_REVERSE, PROC_DC, PROC_PAD_TO, PROC_STRETCH, PROC_RESAMPLE,
       PROC_PITCH, PROC_CROP, PROC_DECIMATE, PROC_BITS, PROC_DRIVE, PROC_LOWPASS, PROC_HIGHPASS, PROC_STRETCH_TONAL, PROC_PITCH_TONAL, PROC_PAUL };
#define PAUL_FRAMES_MAX 8000000u                // the longest paulstretch result (2 min 46 s at 48 kHz, 16 MB on the card)
#define PROC_PPM_MIN   250000                   // Stretch / Resample: the new length, x0.25 .. x4 in ppm
#define PROC_PPM_MAX   4000000
#define PADS_FILE      STORAGE_DIR_CONFIG "/pads.cfg"
#define PADS_FX_FILE   STORAGE_DIR_CONFIG "/padsfx.cfg"
#define PAD_BUSES      4                        // where a pad sounds: Dry, FXA, FXB, FXC (decision 23)

typedef enum { PAD_ONESHOT, PAD_GATE, PAD_TOGGLE, PAD_MODE_COUNT } pad_mode_t;
// Loop (user, 2026-10-10): Trim = the trimmed region start..end (a sample recorded from the synth has no loop points); File = the loop
// points of the file itself, exact (the pad plays from its start and loops there; a ping-pong file loop stays ping-pong). Assigning a file
// that has loop points sets File; a file without them never offers File.
typedef enum { PAD_LOOP_OFF, PAD_LOOP_TRIM, PAD_LOOP_FILE, PAD_LOOP_COUNT } pad_loop_t;
// Start on grid (decision 24): with the clock running (Play) the pad waits for the next grid line; stopped, it starts at once. The grid
// unit (decision 41): 1 or 2 beats, 1 .. 8 bars of 4/4.
typedef enum { PAD_GRID_OFF, PAD_GRID_BEAT, PAD_GRID_2BEAT, PAD_GRID_BAR, PAD_GRID_2BAR, PAD_GRID_3BAR, PAD_GRID_4BAR, PAD_GRID_5BAR,
               PAD_GRID_6BAR, PAD_GRID_7BAR, PAD_GRID_8BAR, PAD_GRID_COUNT } pad_grid_t;
int pad_grid_beats(int grid);                              // 0 (off), 1, 2, 4, 8 .. 32
// Snap (decision 40): where Start / End / LStr go when they move: Off, a beat at the clock's tempo, 1/16 of the sample, the attacks of the
// onset scan, a zero crossing. Start counts from the file's start, LStr from Start, End from LStr when the pad loops (else from Start).
typedef enum { PAD_SNAP_OFF, PAD_SNAP_BEAT, PAD_SNAP_16TH, PAD_SNAP_ATTACK, PAD_SNAP_ZERO, PAD_SNAP_COUNT } pad_snap_t;
// Next (user, 2026-10-10): the neighbour on the bank's 4 x 4 grid that starts when this pad ends by itself or is stopped by hand (toggle
// off, gate release); not on Stop all or Play / Stop. None past the grid's edge.
typedef enum { PAD_NEXT_OFF, PAD_NEXT_UP, PAD_NEXT_DOWN, PAD_NEXT_LEFT, PAD_NEXT_RIGHT, PAD_NEXT_COUNT } pad_next_t;

typedef struct {
    char     file[PAD_NAME_MAX];  // sample name without extension; "" = an empty pad
    uint32_t start, end;          // the trim in frames of the file; end 0 = the file's end (a frame count needs the file: core keeps
                                  // whatever it is given, the engine and the editor clamp to the file's length)
    uint32_t lstart;              // the Trim loop's start in frames (user, 2026-10-10: apart from Start); below Start it means Start
    uint16_t xfade_ms;            // the loop crossfade (0 = a plain loop), equal-power, at most half the loop (the engine caps it)
    uint8_t  xpost;               // 0 Pre: the fade-in comes from before the loop start (and ends there); 1 Post: it starts at the loop start
    uint8_t  from;                // a grid launch starts at 0 the sample start, 1 the loop start (user, 2026-10-10)
    uint8_t  next;                // pad_next_t
    uint8_t  sync;                // 1 = the loop start is the anchor (decision 41): with the clock running the pad starts early enough for its
                                  // loop start to land on the grid (a stopped clock: at once)
    uint8_t  snap;                // pad_snap_t
    uint8_t  mode;                // pad_mode_t: one-shot (plays to its end), gate (stops on release), toggle (a push starts, the next stops)
    uint8_t  loop;                // pad_loop_t: loops until stopped (Trim: start..end, File: the file's loop points)
    uint16_t tail_ms;             // fade-out after a stop (gate release, toggle off, Stop all)
    int8_t   tune;                // semitones -24..+24
    int8_t   fine;                // cents -50..+50
    uint8_t  level;               // 0..100 %
    int8_t   pan;                 // -50 (left) .. +50 (right)
    uint8_t  cut;                 // low-pass cutoff 0..100 (20 Hz .. 20 kHz on a log scale; 100 = off)
    uint8_t  res;                 // resonance 0..100
    uint8_t  choke;               // 0 = none, 1..PAD_CHOKES: starting a pad stops the other pads of its group
    uint8_t  mute;                // 1 = its voices sound silent (a playing loop keeps running and comes back on unmute)
    uint8_t  grid;                // pad_grid_t
    uint8_t  bus;                 // 0 Dry (straight to the output), 1..3 = into the FX chain A / B / C
} pad_t;

// The pads, and the section's FX chains (a rack_t with fx set, core/rack.h: rows = FXA / FXB / FXC).
typedef struct { pad_t pad[PAD_COUNT]; rack_t fx; } pads_t;

// The pad settings, in the order the pad editor shows them.
typedef enum { PADP_FILE, PADP_MODE, PADP_START, PADP_END, PADP_LOOP, PADP_TAIL, PADP_TUNE, PADP_FINE, PADP_LEVEL, PADP_PAN,
               PADP_CUT, PADP_RES, PADP_CHOKE, PADP_MUTE, PADP_GRID, PADP_LSTART, PADP_XFADE, PADP_XPOS, PADP_FROM, PADP_NEXT, PADP_SYNC,
               PADP_SNAP, PADP_BUS, PADP_COUNT } pad_param_t;

void pad_default(pad_t *p);
void pads_init(pads_t *ps);                                // every pad empty, default settings
static inline bool pad_empty(const pad_t *p) { return !p->file[0]; }
void pad_label(int pad, char *out, int n);                 // "A1" .. "D16"

const char *pad_param_label(pad_param_t k);                // "Smp", "Mode", "Strt" ...
// The value as text. frames / rate: the sample's length (0: unknown, the trim is shown in %).
void pad_param_format(const pad_t *p, pad_param_t k, uint32_t frames, uint32_t rate, char *out, int n);
// One step of a setting (dir +1 / -1). The trim points step by 1/200 of the file (fine steps, core/fine_step.h: 1/1000) and need its length
// `frames` (0: they do not move); Level / Pan / Cut / Res step by 5 (fine 1). PADP_FILE is the UI's (it needs the library). True when it changed.
bool pad_param_adjust(pad_t *p, pad_param_t k, int dir, uint32_t frames);
// A setting from 0..1 (a knob's travel; Mode, Loop, Choke, Mute go by steps). True when it changed.
bool pad_param_set_norm(pad_t *p, pad_param_t k, float x, uint32_t frames);
// The trim as the engine plays it, for a file of `frames` frames: start < end <= frames, start <= loop start < end - 1.
uint32_t pad_end(const pad_t *p, uint32_t frames);
uint32_t pad_lstart(const pad_t *p, uint32_t frames);
// Fit to beats (EDIT): the part of the pad that must last a number of beats = its loop when it loops (Trim: loop start..End; File: the
// file's loop `file_ls`..`file_le`), else Start..End; and the length factor in ppm that makes `region` frames last `beats` at `bpm`
// (0 = nothing to fit).
uint32_t pad_fit_region(const pad_t *p, uint32_t frames, uint32_t file_ls, uint32_t file_le);
uint32_t pad_fit_ppm(uint32_t region, uint32_t rate, int beats, int bpm);
// A trim point (PADP_START / END / LSTART) set to `v` frames, kept in order (start < loop start < end - 1 <= frames).
void pad_set_trim(pad_t *p, pad_param_t k, int64_t v, uint32_t frames);
// The snap points of Beat / 1/16 (Attack and Zero need the file: the UI): the next point after `v` in the direction `dir` (+1 / -1), or the
// nearest one (dir 0), for the trim point k. -1 when the snap has no points here (Off, Attack, Zero, no tempo).
int64_t pad_snap_grid(const pad_t *p, pad_param_t k, uint32_t v, int dir, uint32_t frames, uint32_t rate, int bpm);
// Where k counts from (the snap's origin): 0 for Start, Start for LStr, LStr (looping) or Start for End.
uint32_t pad_snap_origin(const pad_t *p, pad_param_t k, uint32_t frames);
float pad_cut_hz(const pad_t *p);                          // the cutoff in Hz (pad.cut 100: 0 = off)

/* ---- chopping (ADR-041 decisions 34-35): cuts inside a pad's trim, then the slices onto consecutive pads (same file) ---- */
#define PAD_CHOP_MAX 16                                    // slices
typedef enum { CHOP_EQUAL, CHOP_TRANSIENT, CHOP_GRID, CHOP_MANUAL, CHOP_MODE_COUNT } chop_mode_t;
#define CHOP_DIVS 5                                        // Grid: a slice every 1/16, 1/8, 1/4, 1/2 note or a bar (4/4)
typedef struct {
    uint8_t  mode;                                         // chop_mode_t
    uint8_t  n;                                            // Equal: slices 2..PAD_CHOP_MAX
    uint8_t  sens;                                         // Transients: 0..100 (100 = the faintest attacks too)
    uint8_t  div;                                          // Grid: 0..CHOP_DIVS-1, at the clock's tempo
    uint8_t  to;                                           // the first pad the slices go to (0..PAD_COUNT-1)
    uint8_t  sel;                                          // Manual: the selected cut
    uint8_t  n_cuts;
    bool     pending;                                      // Transients: the file's scan is not done yet
    uint32_t cut[PAD_CHOP_MAX - 1];                        // the cuts (frames), ascending, strictly inside start..end
} pad_chop_t;

void pad_chop_init(pad_chop_t *c, int pad);                // Equal 4, sensitivity 50, a 1/4 note grid, to the next pad
const char *pad_chop_mode_name(int mode);                  // "Equal", "Trans", "Grid", "Manual"
const char *pad_chop_div_name(int div);                    // "1/16" .. "Bar"
// The cuts of an automatic mode for the trim start..end of a file at `rate`: Equal (n), Grid (div at bpm), Transients (the onsets of the
// file's scan, frame + strength, filtered by the sensitivity; the strongest when there are too many). Manual keeps its cuts.
void pad_chop_compute(pad_chop_t *c, uint32_t start, uint32_t end, uint32_t rate, int bpm, const uint32_t *on, const uint16_t *str, int n_on);
// Manual: add a cut at `at` (selected), remove the selected one, move it by `delta` frames (between its neighbours). True when it changed.
bool pad_chop_add(pad_chop_t *c, uint32_t at, uint32_t start, uint32_t end);
bool pad_chop_del(pad_chop_t *c);
bool pad_chop_move(pad_chop_t *c, int64_t delta, uint32_t start, uint32_t end);
// Writes the slices onto pads c->to, c->to + 1 ...: each a copy of the source pad (the same file) with its start / end, no loop. Stops at
// the last pad. Returns how many pads were written.
int pad_chop_apply(pads_t *ps, int src, const pad_chop_t *c, uint32_t frames);
// The Grid chop's slice length in frames (div at bpm, a file at rate); 0 without a tempo.
uint32_t pad_chop_unit(int div, uint32_t rate, int bpm);

// pads.cfg. pads_load() replaces the pads with the file (false: no card or no file, the pads are kept); pads_save() writes it.
int  pads_to_text(const pads_t *ps, char *buf, int cap);  // the length written (cut at cap - 1: 64 pads with long names and many changed settings
                                                           // can pass STORAGE_FILE_MAX; the last lines are then lost, known debt)
bool pads_from_text(pads_t *ps, const char *txt);         // false when it is not a pads file (the pads are kept)
void pad_parse(pad_t *p, const char *words);              // one pad's settings as its pads.cfg line gives them ("loop 1 bus a file kick")
bool pads_load(pads_t *ps);
bool pads_save(const pads_t *ps);
// padsfx.cfg: the FX chains ("route N", then "fx <A|B|C> <module code> <its values>" per module, in order). from_text replaces the rack
// (false: not that file, the rack is kept).
int  pads_fx_to_text(const rack_t *fx, char *buf, int cap);
bool pads_fx_from_text(rack_t *fx, const char *txt);
bool pads_fx_load(rack_t *fx);
bool pads_fx_save(const rack_t *fx);

#ifdef __cplusplus
}
#endif
