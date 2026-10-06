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

// The engine that builds the sound and how it plays (ADR-036). The polyphonic types copy every voice module `voices` times; the Mono types
// have one voice, last-note priority, optional legato and glide, and give the whole budget to one patch. (Paraphonic is stage 2.)
typedef enum {
    SYNTH_MODULAR,      // built from the rack (oscillators, filters, ...), polyphonic
    SYNTH_MOD_MONO,     // the rack synth, monophonic
    SYNTH_MOD_PARA,     // the rack synth, paraphonic: the voices play up to the first filter, one shared filter / amp chain after their sum (ADR-036)
    SYNTH_FM,           // DX7-style 6-operator FM with the patch editor, polyphonic
    SYNTH_FM_MONO,      // the FM synth, monophonic
    SYNTH_STRINGS,      // thin voices for pads and big chords, up to 32 (ADR-037); its pages are in synth_params (P_STR_*)
    SYNTH_TYPE_COUNT
} synth_type_t;

#define FM_PATCH_COUNT 128      // the 128 DX7 factory patches
#define SYNTH_MAX_VOICES 8      // Modular / FM
#define SYNTH_STR_MAX_VOICES 32 // Strings (the engine's ceiling, ENGINE_MAX_VOICES)

typedef enum {
    CFGP_TYPE, CFGP_PATCH, CFGP_VOICES, CFGP_GLIDE, CFGP_LEGATO, CFGP_VOLUME, CFGP_OUTPUT, CFGP_SPEAKER,    // GENERAL tab
    CFGP_PARA_ENV,                                                                                         // GENERAL tab, Mod Para only
    CFGP_KNOB_MODE,                                                                                        // GENERAL tab: column knobs catch the value or set it at once
    CFGP_GENERAL_COUNT,
    CFGP_COUNT = CFGP_GENERAL_COUNT
} cfg_param_id_t;

// Mod Para envelope policies (ADR-036 decision 3): LEGATO = the shared filter and amp envelopes start with the first key and keep running while
// keys are added; RETRIG = they restart their attack on every new key; VOICE = every voice has its own amp envelope in front of the shared
// filter, whose envelope restarts on every key.
typedef enum { PARA_ENV_LEGATO, PARA_ENV_RETRIG, PARA_ENV_VOICE, PARA_ENV_COUNT } para_env_t;

typedef struct {
    uint8_t type;        // synth_type_t
    uint8_t fm_patch;    // factory patch the edit copy `fm` was loaded from, 0..127 (shown as 1..128)
    uint8_t voices;      // 1..SYNTH_MAX_VOICES (the polyphonic types)
    uint8_t str_voices;  // 1..SYNTH_STR_MAX_VOICES (Strings; kept apart so switching types keeps both counts)
    uint8_t glide;       // Mono: index into the glide times (0 = off)
    uint8_t legato;      // Mono: 1 = a new key while one is held changes the pitch only (envelopes keep running)
    uint8_t para_env;    // Mod Para: how the shared envelopes follow the keys, para_env_t
    uint8_t knob_mode;   // column knobs: 0 = Catch (ignored until they cross the value, no jumps), 1 = Direct (set the value at once)
    float   volume;      // master volume 0..2 (applied at the very end of the chain, after the master effects)
    uint8_t mono;        // 0 = stereo output, 1 = (L + R) / 2 on both channels
    uint8_t speaker;     // built-in loudspeaker level in 5 % steps: 0 = off (amplifier shut down), 1..20 = 5..100 % (the board scales the channel that feeds it)
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
static inline bool synth_type_is_fm(uint8_t t)   { return t == SYNTH_FM || t == SYNTH_FM_MONO; }
static inline bool synth_type_is_mono(uint8_t t) { return t == SYNTH_MOD_MONO || t == SYNTH_FM_MONO; }
static inline bool synth_type_is_strings(uint8_t t) { return t == SYNTH_STRINGS; }
static inline bool synth_type_is_rack(uint8_t t) { return t == SYNTH_MODULAR || t == SYNTH_MOD_MONO || t == SYNTH_MOD_PARA; }   // built from the module rack
static inline bool synth_type_is_para(uint8_t t) { return t == SYNTH_MOD_PARA; }

// What the audio layer needs: how many voices the engine builds (1 for Mono) and the glide time in ms (0 = off).
int          synth_config_voices(const synth_config_t *c);
int          synth_config_glide_ms(const synth_config_t *c);
int          synth_config_speaker_pct(const synth_config_t *c);   // 0 (off) .. 100

#ifdef __cplusplus
}
#endif
