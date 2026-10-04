#pragma once
// General synth settings (the "GENERAL" and "FX" tabs of the menu): which engine builds the sound, voice
// count, master volume, and the master effects. Stored inside rack_t so the audio layer receives it with the rack.
// Edited through the same adjust / label / format shape as the other parameter sets.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core/dx7.h"
#include "core/fxrack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYNTH_MODULAR,      // built from the rack (oscillators, filters, ...)
    SYNTH_FM,           // DX7-style 6-operator FM with the patch editor
    SYNTH_TYPE_COUNT
} synth_type_t;

#define FM_PATCH_COUNT 128      // the 128 DX7 factory patches
#define SYNTH_MAX_VOICES 8

typedef enum {
    CFGP_TYPE, CFGP_PATCH, CFGP_VOICES, CFGP_VOLUME,                 // GENERAL tab
    CFGP_GENERAL_COUNT,
    CFGP_COUNT = CFGP_GENERAL_COUNT
} cfg_param_id_t;

typedef struct {
    uint8_t type;        // synth_type_t
    uint8_t fm_patch;    // factory patch the edit copy `fm` was loaded from, 0..127 (shown as 1..128)
    uint8_t voices;      // 1..SYNTH_MAX_VOICES
    float   volume;      // master volume 0..2
    dx7_patch_t fm;      // the FM patch being played / edited (a copy: factory edits do not change the bank)
    fxrack_t fxr;        // the master effects rack (four slots, see core/fxrack.h)
} synth_config_t;

// What a change requires from the audio layer.
typedef enum { CFG_UNCHANGED, CFG_LIVE, CFG_REBUILD } cfg_effect_t;

void synth_config_init(synth_config_t *c);

// dir = +1 / -1. Returns whether anything changed and whether the synth must be rebuilt.
cfg_effect_t synth_config_adjust(synth_config_t *c, cfg_param_id_t id, int dir);
const char  *synth_config_label(cfg_param_id_t id);
void         synth_config_format(const synth_config_t *c, cfg_param_id_t id, char *out, size_t n);
const char  *synth_type_name(synth_type_t t);

#ifdef __cplusplus
}
#endif
