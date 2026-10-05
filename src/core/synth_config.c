#include "core/synth_config.h"
#include <stdio.h>

static const char *const type_names[SYNTH_TYPE_COUNT] = {"Modular", "Mod Mono", "FM", "FM Mono"};

static const int glide_ms[] = {0, 25, 50, 100, 200, 400, 800};
#define GLIDE_STEPS ((int)(sizeof glide_ms / sizeof glide_ms[0]))
#define SPEAKER_STEPS 20                   // 5 % each

static const char *const labels[CFGP_GENERAL_COUNT] = {"Type", "Patch", "Voices", "Glide", "Legato", "Vol", "Out", "Spk"};

void synth_config_init(synth_config_t *c) {
    c->type = SYNTH_MOD_MONO;              // one real voice: the full budget for one patch (ADR-036)
    c->fm_patch = 0;
    c->voices = SYNTH_MAX_VOICES;
    c->glide = 0;
    c->legato = 0;
    c->volume = 1.0f;
    c->mono = 1;
    c->speaker = SPEAKER_STEPS;            // full: the level the board had before this setting existed
    dx7_load_factory(&c->fm, 0);
    fxr_init(&c->fxr);
}

const char *synth_type_name(synth_type_t t) { return type_names[t]; }
const char *synth_config_label(cfg_param_id_t id) { return labels[id]; }

int synth_config_voices(const synth_config_t *c) { return synth_type_is_mono(c->type) ? 1 : c->voices; }
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
    case CFGP_VOICES:
        v = c->voices + dir;
        if (v < 1 || v > SYNTH_MAX_VOICES) return CFG_UNCHANGED;
        c->voices = (uint8_t)v;
        return CFG_REBUILD;
    case CFGP_VOLUME: {
        float f = c->volume + 0.05f * (float)dir;
        if (f < 0.0f) f = 0.0f;
        if (f > 2.0f) f = 2.0f;
        if (f == c->volume) return CFG_UNCHANGED;
        c->volume = f;
        return CFG_LIVE;
    }
    case CFGP_OUTPUT:
        v = c->mono + dir;
        if (v < 0 || v > 1) return CFG_UNCHANGED;
        c->mono = (uint8_t)v;
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
    case CFGP_VOICES: snprintf(out, n, "%d", c->voices); break;
    case CFGP_GLIDE:  if (c->glide == 0) snprintf(out, n, "Off"); else snprintf(out, n, "%d ms", glide_ms[c->glide < GLIDE_STEPS ? c->glide : 0]); break;
    case CFGP_LEGATO: snprintf(out, n, "%s", c->legato ? "On" : "Off"); break;
    case CFGP_VOLUME: snprintf(out, n, "%.2f", c->volume); break;
    case CFGP_OUTPUT: snprintf(out, n, "%s", c->mono ? "Mono" : "Stereo"); break;
    case CFGP_SPEAKER: if (c->speaker == 0) snprintf(out, n, "Off"); else snprintf(out, n, "%d%%", synth_config_speaker_pct(c)); break;
    default: out[0] = 0;
    }
}
