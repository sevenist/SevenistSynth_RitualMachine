#pragma once
// The Pads section (ENGINE_DESIGN.md ADR-041, decisions 14-26): an SP-404-style sampler beside the synth, a separate workflow. 4 banks of
// 16 pads; each pad holds a sample of the library (by name, so it survives a rescan of the card) and how it plays. Every pad is mono: a
// push restarts it. The engine side is the Pads module (engine/modules/sampler_modules.h, 8 voices).
//
// Saved in pads.cfg on the card (system/config, text, one line per pad that differs from the defaults):
//   pad A1 mode gate loop 1 start 120 end 900 tail 50 tune -2 fine 10 level 80 pan -10 cut 60 res 20 choke 1 mute 1 file kick 01
// The file name comes last (the rest of the line). Unknown words are skipped, so a file from a later version still loads.
#include <stdbool.h>
#include <stdint.h>
#include "hal/hal_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAD_BANKS      4
#define PADS_PER_BANK  16
#define PAD_COUNT      (PAD_BANKS * PADS_PER_BANK)
#define PAD_NAME_MAX   24                       // = audio_sample_info_t.name
#define PAD_CHOKES     4                        // choke groups 1..4 (0 = none)
#define PADS_FILE      STORAGE_DIR_CONFIG "/pads.cfg"

typedef enum { PAD_ONESHOT, PAD_GATE, PAD_TOGGLE, PAD_MODE_COUNT } pad_mode_t;
// Loop (user, 2026-10-10): Trim = the trimmed region start..end (a sample recorded from the synth has no loop points); File = the loop
// points of the file itself, exact (the pad plays from its start and loops there; a ping-pong file loop stays ping-pong). Assigning a file
// that has loop points sets File; a file without them never offers File.
typedef enum { PAD_LOOP_OFF, PAD_LOOP_TRIM, PAD_LOOP_FILE, PAD_LOOP_COUNT } pad_loop_t;

typedef struct {
    char     file[PAD_NAME_MAX];  // sample name without extension; "" = an empty pad
    uint16_t start, end;          // trim in 1/1000 of the file (start < end)
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
} pad_t;

typedef struct { pad_t pad[PAD_COUNT]; } pads_t;

// The pad settings, in the order the pad editor shows them.
typedef enum { PADP_FILE, PADP_MODE, PADP_START, PADP_END, PADP_LOOP, PADP_TAIL, PADP_TUNE, PADP_FINE, PADP_LEVEL, PADP_PAN,
               PADP_CUT, PADP_RES, PADP_CHOKE, PADP_MUTE, PADP_COUNT } pad_param_t;

void pad_default(pad_t *p);
void pads_init(pads_t *ps);                                // every pad empty, default settings
static inline bool pad_empty(const pad_t *p) { return !p->file[0]; }
void pad_label(int pad, char *out, int n);                 // "A1" .. "D16"

const char *pad_param_label(pad_param_t k);                // "Smp", "Mode", "Strt" ...
// The value as text. frames / rate: the sample's length (0: unknown, the trim is shown in %).
void pad_param_format(const pad_t *p, pad_param_t k, uint32_t frames, uint32_t rate, char *out, int n);
// One step of a setting (dir +1 / -1; core/fine_step.h makes Start / End / Level / Pan / Cut / Res step by 1). PADP_FILE is the UI's (it
// needs the library). Returns true when the value changed.
bool pad_param_adjust(pad_t *p, pad_param_t k, int dir);
// A setting from 0..1 (a knob's travel; Mode, Loop, Choke, Mute go by steps). Returns true when it changed.
bool pad_param_set_norm(pad_t *p, pad_param_t k, float x);
float pad_cut_hz(const pad_t *p);                          // the cutoff in Hz (pad.cut 100: 0 = off)

// pads.cfg. pads_load() replaces the pads with the file (false: no card or no file, the pads are kept); pads_save() writes it.
int  pads_to_text(const pads_t *ps, char *buf, int cap);  // the length written (cut at cap - 1: 64 pads with long names and many changed settings
                                                           // can pass STORAGE_FILE_MAX; the last lines are then lost, known debt)
bool pads_from_text(pads_t *ps, const char *txt);         // false when it is not a pads file (the pads are kept)
bool pads_load(pads_t *ps);
bool pads_save(const pads_t *ps);

#ifdef __cplusplus
}
#endif
