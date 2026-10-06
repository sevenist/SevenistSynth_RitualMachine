#include "core/synth_config.h"
#include <stdio.h>

static const char *const type_names[SYNTH_TYPE_COUNT] = {"Modular", "Mod Mono", "Mod Para", "FM", "FM Mono", "Strings"};
static const char *const para_env_names[PARA_ENV_COUNT] = {"Legato", "Retrig", "Voice"};

static const int glide_ms[] = {0, 25, 50, 100, 200, 400, 800};
#define GLIDE_STEPS ((int)(sizeof glide_ms / sizeof glide_ms[0]))
#define SPEAKER_STEPS 20                   // 5 % each
#define VOLUME_STEPS_PER_UNIT 20           // 0.05 each

static const char *const labels[CFGP_GENERAL_COUNT] = {"Type", "Patch", "Voices", "Glide", "Legato", "Vol", "Out", "Spk", "PEnv", "Knob"};

void synth_config_init(synth_config_t *c) {
    c->type = SYNTH_MOD_MONO;              // one real voice: the full budget for one patch (ADR-036)
    c->fm_patch = 0;
    c->voices = SYNTH_MAX_VOICES;
    c->str_voices = SYNTH_STR_MAX_VOICES;
    c->glide = 0;
    c->legato = 0;
    c->para_env = PARA_ENV_LEGATO;
    c->volume = 1.0f;
    c->knob_mode = 0;
    c->mono = 1;
#ifdef DEV_SPEAKER_DEFAULT
    c->speaker = DEV_SPEAKER_DEFAULT;      // dev only (platformio.ini): 0 = the speaker starts off while testing with headphones
#else
    c->speaker = SPEAKER_STEPS;            // full: the level the board had before this setting existed
#endif
    dx7_load_factory(&c->fm, 0);
    fxr_init(&c->fxr);
}

const char *synth_type_name(synth_type_t t) { return type_names[t]; }
const char *synth_config_label(cfg_param_id_t id) { return labels[id]; }

int synth_config_voices(const synth_config_t *c) {
    if (synth_type_is_mono(c->type)) return 1;
    return synth_type_is_strings(c->type) ? c->str_voices : c->voices;
}
int synth_config_speaker_pct(const synth_config_t *c) { return (c->speaker > SPEAKER_STEPS ? SPEAKER_STEPS : c->speaker) * 5; }
int synth_config_glide_ms(const synth_config_t *c) { return synth_type_is_mono(c->type) ? glide_ms[c->glide < GLIDE_STEPS ? c->glide : 0] : 0; }

cfg_effect_t synth_config_adjust(synth_config_t *c, cfg_param_id_t id, int dir) {
    int v;
    switch (id) {
    case CFGP_TYPE:
        v = c->type + dir;
        if (v < 0 || v >= SYNTH_TYPE_COUNT) return CFG_UNCHANGED;
        c->type = (uint8_t)v;
        return CFG_REBUILD;
    case CFGP_PATCH:
        v = c->fm_patch + dir;
        if (v < 0 || v >= FM_PATCH_COUNT) return CFG_UNCHANGED;
        c->fm_patch = (uint8_t)v;
        dx7_load_factory(&c->fm, v);                             // a new patch discards the edits
        return CFG_LIVE;                                         // the FM voices just get the new parameters
    case CFGP_GLIDE:
        v = c->glide + dir;
        if (v < 0 || v >= GLIDE_STEPS) return CFG_UNCHANGED;
        c->glide = (uint8_t)v;
        return CFG_LIVE;
    case CFGP_LEGATO:
        v = c->legato + dir;
        if (v < 0 || v > 1) return CFG_UNCHANGED;
        c->legato = (uint8_t)v;
        return CFG_LIVE;
    case CFGP_VOICES: {
        uint8_t *nv = synth_type_is_strings(c->type) ? &c->str_voices : &c->voices;
        v = *nv + dir;
        if (v < 1 || v > (synth_type_is_strings(c->type) ? SYNTH_STR_MAX_VOICES : SYNTH_MAX_VOICES)) return CFG_UNCHANGED;
        *nv = (uint8_t)v;
        return CFG_REBUILD;
    }
    case CFGP_VOLUME: {                                          // 0..2 in steps of 0.05, counted as an integer: adding 0.05 drifted, so the knob walk saw 41 steps
        v = (int)(c->volume * VOLUME_STEPS_PER_UNIT + 0.5f) + dir;
        if (v < 0 || v > 2 * VOLUME_STEPS_PER_UNIT) return CFG_UNCHANGED;
        c->volume = (float)v / VOLUME_STEPS_PER_UNIT;
        return CFG_LIVE;
    }
    case CFGP_OUTPUT:
        v = c->mono + dir;
        if (v < 0 || v > 1) return CFG_UNCHANGED;
        c->mono = (uint8_t)v;
        return CFG_LIVE;
    case CFGP_PARA_ENV:
        v = c->para_env + dir;
        if (v < 0 || v >= PARA_ENV_COUNT) return CFG_UNCHANGED;
        c->para_env = (uint8_t)v;
        return CFG_REBUILD;                                      // the graph's shape changes (per-voice or shared amp envelope)
    case CFGP_KNOB_MODE:
        v = c->knob_mode + dir;
        if (v < 0 || v > 1) return CFG_UNCHANGED;
        c->knob_mode = (uint8_t)v;
        return CFG_LIVE;
    case CFGP_SPEAKER:
        v = c->speaker + dir;
        if (v < 0 || v > SPEAKER_STEPS) return CFG_UNCHANGED;
        c->speaker = (uint8_t)v;
        return CFG_LIVE;
    default: return CFG_UNCHANGED;
    }
}

void synth_config_format(const synth_config_t *c, cfg_param_id_t id, char *out, size_t n) {
    switch (id) {
    case CFGP_TYPE:   snprintf(out, n, "%s", type_names[c->type < SYNTH_TYPE_COUNT ? c->type : 0]); break;
    case CFGP_PATCH:  snprintf(out, n, "DX7 %03d", c->fm_patch + 1); break;
    case CFGP_VOICES: snprintf(out, n, "%d", synth_config_voices(c)); break;
    case CFGP_GLIDE:  if (c->glide == 0) snprintf(out, n, "Off"); else snprintf(out, n, "%d ms", glide_ms[c->glide < GLIDE_STEPS ? c->glide : 0]); break;
    case CFGP_LEGATO: snprintf(out, n, "%s", c->legato ? "On" : "Off"); break;
    case CFGP_VOLUME: snprintf(out, n, "%.2f", c->volume); break;
    case CFGP_OUTPUT: snprintf(out, n, "%s", c->mono ? "Mono" : "Stereo"); break;
    case CFGP_SPEAKER: if (c->speaker == 0) snprintf(out, n, "Off"); else snprintf(out, n, "%d%%", synth_config_speaker_pct(c)); break;
    case CFGP_PARA_ENV: snprintf(out, n, "%s", para_env_names[c->para_env < PARA_ENV_COUNT ? c->para_env : 0]); break;
    case CFGP_KNOB_MODE: snprintf(out, n, "%s", c->knob_mode ? "Direct" : "Catch"); break;
    default: out[0] = 0;
    }
}
