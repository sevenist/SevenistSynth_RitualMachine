#pragma once
// The master effects rack: four slots in series after the voices, each holding one effect with four parameters (any order, any type,
// duplicates allowed). Pure data and editing rules (no drawing, no audio); the engine mapper (platform/engine/rack_graph.cpp) turns it
// into nodes. Values are small integers in the unit shown on screen (%, ms, dB, steps of an enum ...), see the table in fxrack.c.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FXR_SLOTS 4
#define FXR_PARAMS 4

typedef enum {
    FX_NONE, FX_DRIVE, FX_CHORUS, FX_PHASER, FX_FLANGER, FX_TREMOLO, FX_COMP, FX_EQ, FX_SHIFT, FX_DELAY, FX_REVERB, FX_CAB,
    FX_ENSEMBLE,                  // string ensemble (Solina style, ADR-037)
    FX_TYPE_COUNT
} fx_type_t;

typedef struct {
    uint8_t type;                 // fx_type_t
    int16_t v[FXR_PARAMS];        // parameter values in screen units
} fx_slot_t;

typedef struct { fx_slot_t slot[FXR_SLOTS]; } fxrack_t;

void        fxr_init(fxrack_t *r);                                   // chorus (off), delay (dry), reverb (dry), empty: audibly neutral
void        fxr_set_type(fx_slot_t *s, int type);                    // new type with the defaults of that type
bool        fxr_cycle_type(fx_slot_t *s, int dir);                   // true if the type changed
const char *fxr_type_name(int type);                                 // "Reverb"
const char *fxr_type_code(int type);                                 // "RV" (two letters, for the chain overview)
int         fxr_param_count(int type);
const char *fxr_label(int type, int i);                              // "Mix"
bool        fxr_adjust(fx_slot_t *s, int i, int dir);                // true if the value changed
void        fxr_format(const fx_slot_t *s, int i, char *out, size_t n);

#ifdef __cplusplus
}
#endif
