#pragma once
// General synth settings (the GENERAL tab of the menu): which engine builds the sound, voice count, master volume.
// Stored inside rack_t so the audio layer receives it with the rack (the effects are row M of the rack, ADR-041).
// Edited through the same adjust / label / format shape as the other parameter sets.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core/dx7.h"

#ifdef __cplusplus
extern "C" {
#endif

// The engine that builds the sound (ADR-041). Every voice module is copied `voices` times; Voices = 1 is mono (Modular and FM): last-note
// priority, optional legato and glide, the whole budget for one patch. Paraphonic is the Para switch on a rack module (rack.h).
typedef enum {
    SYNTH_MODULAR,      // built from the rack: two branches and row M
    SYNTH_FM,           // DX7-style 6-operator FM with the patch editor; its voices feed row M
    SYNTH_STRINGS,      // thin voices for pads and big chords, up to 32 (ADR-037); its pages are in synth_params (P_STR_*); its voices feed row M
    SYNTH_TYPE_COUNT
} synth_type_t;

#define FM_PATCH_COUNT 128      // the 128 DX7 factory patches
#define SYNTH_MAX_VOICES 8      // Modular / FM
#define SYNTH_STR_MAX_VOICES 32 // Strings (the engine's ceiling, ENGINE_MAX_VOICES)

typedef enum {
    CFGP_TYPE, CFGP_PATCH, CFGP_VOICES, CFGP_GLIDE, CFGP_LEGATO, CFGP_VOLUME, CFGP_OUTPUT, CFGP_SPEAKER,    // GENERAL tab
    CFGP_KNOB_MODE,                                                                                        // GENERAL tab: column knobs catch the value or set it at once
    CFGP_GENERAL_COUNT,
    CFGP_COUNT = CFGP_GENERAL_COUNT
} cfg_param_id_t;

// Para envelope policies (ADR-036 decision 3; set per Para point, rack_slot_t.penv, ADR-041): LEGATO = the shared filter and amp envelopes
// start with the first key and keep running while keys are added; RETRIG = they restart their attack on every new key; VOICE = every voice
// has its own amp envelope in front of the shared part, whose filter envelope restarts on every key.
typedef enum { PARA_ENV_LEGATO, PARA_ENV_RETRIG, PARA_ENV_VOICE, PARA_ENV_COUNT } para_env_t;

typedef struct {
    uint8_t type;        // synth_type_t
    uint8_t fm_patch;    // factory patch the edit copy `fm` was loaded from, 0..127 (shown as 1..128)
    uint8_t voices;      // 1..SYNTH_MAX_VOICES (Modular, FM; 1 = mono)
    uint8_t str_voices;  // 1..SYNTH_STR_MAX_VOICES (Strings; kept apart so switching types keeps both counts)
    uint8_t glide;       // Mono: index into the glide times (0 = off)
    uint8_t legato;      // Mono: 1 = a new key while one is held changes the pitch only (envelopes keep running)
    uint8_t knob_mode;   // column knobs: 0 = Catch (ignored until they cross the value, no jumps), 1 = Direct (set the value at once)
    float   volume;      // master volume 0..2 (applied at the very end of the chain, after the master effects)
    uint8_t mono;        // 0 = stereo output, 1 = (L + R) / 2 on both channels
    uint8_t speaker;     // built-in loudspeaker level in 5 % steps: 0 = off (amplifier shut down), 1..20 = 5..100 % (the board scales the channel that feeds it)
    dx7_patch_t fm;      // the FM patch being played / edited (a copy: factory edits do not change the bank)
} synth_config_t;

// What a change requires from the audio layer.
typedef enum { CFG_UNCHANGED, CFG_LIVE, CFG_REBUILD } cfg_effect_t;

void synth_config_init(synth_config_t *c);

// dir = +1 / -1. Returns whether anything changed and whether the synth must be rebuilt.
cfg_effect_t synth_config_adjust(synth_config_t *c, cfg_param_id_t id, int dir);
const char  *synth_config_label(cfg_param_id_t id);
void         synth_config_format(const synth_config_t *c, cfg_param_id_t id, char *out, size_t n);
const char  *synth_type_name(synth_type_t t);
static inline bool synth_type_is_fm(uint8_t t)   { return t == SYNTH_FM; }
static inline bool synth_type_is_strings(uint8_t t) { return t == SYNTH_STRINGS; }
static inline bool synth_type_is_rack(uint8_t t) { return t == SYNTH_MODULAR; }   // built from the module rack (branches; every type has row M)

// What the audio layer needs: how many voices the engine builds, whether they play mono (Voices = 1, Modular / FM), the glide time in ms (0 = off).
int          synth_config_voices(const synth_config_t *c);
bool         synth_config_is_mono(const synth_config_t *c);
int          synth_config_glide_ms(const synth_config_t *c);
int          synth_config_speaker_pct(const synth_config_t *c);   // 0 (off) .. 100

#ifdef __cplusplus
}
#endif
