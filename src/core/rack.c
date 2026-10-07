#include "core/rack.h"
#include "core/synth_params.h"
#include "core/fine_step.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef enum { K_ENUM, K_LIN, K_LOG } mp_kind_t;

typedef struct {                // descriptor of one module parameter
    const char *label;
    mp_kind_t   kind;
    float       min, max, step;     // K_LIN: added; K_LOG: multiplied
    float       def;
    const char *unit;               // "Hz" prints as kHz above 1000
    uint8_t     decimals;
    const char *const *names;       // K_ENUM
} mp_t;

static const char *const wave_names[]   = {"Sine", "Pulse", "SawDn", "SawUp", "Tri", "Noise", "Karp", "Modal", "FM2", "Fold", "SSaw", "Vowel", "Add", "Dust", "Strng",
    // the Mutable Instruments models (OC_FIRST_MI..): the order of MiModel in engine/modules/mi_osc.h (a UI test compares them)
    "CSaw", "Morph", "SawSq", "SinTri", "Buzz", "SqSub", "SawSub", "SqSync", "SwSync", "Toy", "ZLP", "ZPk", "ZBP", "ZHP", "Vosim", "FbFM",
    "Chaos", "Kick", "FNoise", "Twin", "Clock", "DigMod", "Morse", "3Saw", "3Sq", "3Tri", "3Sine", "3Ring", "WTbl", "WMap", "WLine", "Cloud",
    "Cymbal", "BSnare", "PNoise", "Chip", "PhDist", "VA", "VAVcf", "Grain", "Terrn", "PBass", "PBassS", "PSnare", "PSnrS", "PHat", "PHat2"};
static const char *const qual_names[]   = {"Blep", "Mip", "Naive"};
static const char *const filter_names[] = {"Off", "LP", "BP", "HP", "LP24", "Notch", "LP6", "Ladr", "ChLP", "AP"};
static const char *const sat_names[]    = {"Tanh", "Clip", "Fold", "Crush", "Tube", "Tape", "Diode", "Cheb", "Rect", "Decim"};
static const char *const loop_names[]   = {"File", "Off", "Fwd", "Ping"};
static const char *const dir_names[]    = {"Fwd", "Rev"};
static const char *const track_names[]  = {"Key", "Drum"};
static const char *const trig_names[]   = {"Gate", "Shot"};
static const char *const smode_names[]  = {"On", "Stop"};
static const char *const out_names[]    = {"On", "Mute"};
static const char *const lfo_names[]    = {"Sine", "Tri", "SawDn", "Pulse"};

static const mp_t osc_mp[] = {
    [MP_OC_WAVE]   = {"Wav",  K_ENUM, 0, OC_WAVE_COUNT - 1, 1, 2, "", 0, wave_names},
    [MP_OC_PW]     = {"PW",   K_LIN, 0.05f, 0.95f, 0.05f, 0.5f, "", 2, 0},
    [MP_OC_LEVEL]  = {"Lvl",  K_LIN, 0, 1, 0.05f, 1, "", 2, 0},
    [MP_OC_COARSE] = {"Crs",  K_LIN, -24, 24, 1, 0, "st", 0, 0},
    [MP_OC_FINE]   = {"Fine", K_LIN, -50, 50, 1, 0, "ct", 0, 0},
    [MP_OC_MUTE]   = {"Out",  K_ENUM, 0, 1, 1, 0, "", 0, out_names},       // with a target: Mute silences the chain output, not the modulation
    [MP_OC_DEPTH]  = {"Dpth", K_LIN, 0, 2, 0.05f, 1, "", 2, 0},            // with a target: modulation depth (in the target's unit, see rack_depth_*)
    [MP_OC_MORPH]  = {"Mrph", K_LIN, 0, 1, 0.05f, 0.5f, "", 2, 0},         // engines only
    [MP_OC_QUAL]   = {"Q",    K_ENUM, 0, 2, 1, 0, "", 0, qual_names},     // Saw / Pulse / Tri and Strng: PolyBLEP, mipmap table, naive
    [MP_OC_HARM]   = {"Harm", K_LIN, 0, 1, 0.05f, 0.5f, "", 2, 0},         // MI models: Plaits' harmonics (Braids ignores it)
};
_Static_assert(sizeof wave_names / sizeof wave_names[0] == OC_WAVE_COUNT, "wave_names: one name per Wav value (rack.h OC_WAVE_COUNT)");
static const mp_t flt_mp[] = {
    [MP_FL_TYPE]   = {"Typ", K_ENUM, 0, FILT_COUNT - 1, 1, 1, "", 0, filter_names},
    [MP_FL_CUT]    = {"Cut", K_LOG, 20, 18000, 1.12f, 4000, "Hz", 0, 0},
    [MP_FL_RES]    = {"Res", K_LIN, 0.5f, 10, 0.1f, 0.7f, "", 1, 0},
    [MP_FL_ENVAMT] = {"Env", K_LIN, 0, 8, 0.25f, 0, "", 2, 0},
    [MP_FL_A]      = {"Atk", K_LOG, 1, 5000, 1.2f, 5, "ms", 0, 0},
    [MP_FL_D]      = {"Dec", K_LOG, 1, 5000, 1.2f, 300, "ms", 0, 0},
    [MP_FL_S]      = {"Sus", K_LIN, 0, 1, 0.05f, 0, "", 2, 0},
    [MP_FL_R]      = {"Rel", K_LOG, 1, 5000, 1.2f, 200, "ms", 0, 0},
    [MP_FL_ACV]    = {"ACv", K_LIN, -100, 100, 5, 55, "%", 0, 0},
    [MP_FL_DCV]    = {"DCv", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [MP_FL_RCV]    = {"RCv", K_LIN, -100, 100, 5, 60, "%", 0, 0},
};
static const mp_t sat_mp[] = {
    [MP_SA_MODE]  = {"Mod", K_ENUM, 0, 9, 1, 1, "", 0, sat_names},
    [MP_SA_DRIVE] = {"Drv", K_LOG, 1, 16, 1.25f, 4, "", 1, 0},
    [MP_SA_MIX]   = {"Mix", K_LIN, 0, 1, 0.05f, 1, "", 2, 0},
};
static const mp_t lfo_mp[] = {
    [MP_LF_SHAPE] = {"Shp", K_ENUM, 0, 3, 1, 0, "", 0, lfo_names},
    [MP_LF_RATE]  = {"Rate", K_LOG, 0.05f, 30, 1.2f, 4, "Hz", 2, 0},
    [MP_LF_DEPTH] = {"Dpth", K_LIN, 0, 2, 0.05f, 1, "", 2, 0},
};
static const mp_t rs_mp[] = {
    [MP_RS_TUNE] = {"Crs",  K_LIN, -24, 24, 1, 0, "st", 0, 0},
    [MP_RS_FB]   = {"Fb",   K_LIN, -95, 95, 5, 80, "%", 0, 0},
    [MP_RS_DAMP] = {"Dmp",  K_LOG, 200, 18000, 1.25f, 8000, "Hz", 0, 0},
    [MP_RS_INT]  = {"Int",  K_LIN, 0, 24, 1, 0, "st", 0, 0},
    [MP_RS_MIX]  = {"Mix",  K_LIN, 0, 1, 0.05f, 0.5f, "", 2, 0},
};
static const mp_t env_mp[] = {
    [MP_EN_A]     = {"Atk", K_LOG, 1, 5000, 1.2f, 5, "ms", 0, 0},
    [MP_EN_D]     = {"Dec", K_LOG, 1, 5000, 1.2f, 300, "ms", 0, 0},
    [MP_EN_S]     = {"Sus", K_LIN, 0, 1, 0.05f, 0, "", 2, 0},
    [MP_EN_DEPTH] = {"Dpth", K_LIN, 0, 2, 0.05f, 1, "", 2, 0},
    [MP_EN_R]     = {"Rel", K_LOG, 1, 5000, 1.2f, 200, "ms", 0, 0},
    [MP_EN_HOLD]  = {"Hld", K_LIN, 0, 2000, 5, 0, "ms", 0, 0},
    [MP_EN_START] = {"Str", K_LIN, 0, 1, 0.05f, 0, "", 2, 0},
    [MP_EN_ACV]   = {"ACv", K_LIN, -100, 100, 5, 55, "%", 0, 0},
    [MP_EN_DCV]   = {"DCv", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [MP_EN_RCV]   = {"RCv", K_LIN, -100, 100, 5, 60, "%", 0, 0},
};
static const mp_t eg_mp[] = {            // labels are generic: the EG pages show the selected point (see ui_pages.c / ui_input.c)
    [0]  = {"T1", K_LIN, 0, 2000, 1, 1, "ms", 0, 0},   [1]  = {"L1", K_LIN, 0, 100, 5, 100, "%", 0, 0}, [2]  = {"C1", K_LIN, -100, 100, 5, 0, "%", 0, 0},
    [3]  = {"T2", K_LIN, 0, 2000, 1, 100, "ms", 0, 0}, [4]  = {"L2", K_LIN, 0, 100, 5, 20, "%", 0, 0},  [5]  = {"C2", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [6]  = {"T3", K_LIN, 0, 2000, 1, 0, "ms", 0, 0},   [7]  = {"L3", K_LIN, 0, 100, 5, 0, "%", 0, 0},   [8]  = {"C3", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [9]  = {"T4", K_LIN, 0, 2000, 1, 0, "ms", 0, 0},   [10] = {"L4", K_LIN, 0, 100, 5, 0, "%", 0, 0},   [11] = {"C4", K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [MP_EG_SUS]   = {"Sus",  K_LIN, 1, 4, 1, 2, "", 0, 0},
    [MP_EG_REL]   = {"Rel",  K_LOG, 1, 5000, 1.2f, 200, "ms", 0, 0},
    [MP_EG_RCV]   = {"RCv",  K_LIN, -100, 100, 5, 60, "%", 0, 0},
    [MP_EG_ONE]   = {"Trig", K_ENUM, 0, 1, 1, 0, "", 0, trig_names},
    [MP_EG_DEPTH] = {"Dpth", K_LIN, 0, 2, 0.05f, 1, "", 2, 0},
};

static const mp_t sm_mp[] = {
    [MP_SM_FILE]   = {"File",  K_LIN, 0, 32, 1, 0, "", 0, 0},                  // shown by name (ui_draw.c); 0 = none
    [MP_SM_LEVEL]  = {"Lvl",   K_LIN, 0, 1, 0.05f, 1, "", 2, 0},
    [MP_SM_COARSE] = {"Crs",   K_LIN, -24, 24, 1, 0, "st", 0, 0},
    [MP_SM_FINE]   = {"Fine",  K_LIN, -50, 50, 1, 0, "ct", 0, 0},
    [MP_SM_LOOP]   = {"Loop",  K_ENUM, 0, 3, 1, 0, "", 0, loop_names},
    [MP_SM_REV]    = {"Dir",   K_ENUM, 0, 1, 1, 0, "", 0, dir_names},
    [MP_SM_START]  = {"Start", K_LIN, 0, 5000, 10, 0, "ms", 0, 0},
    [MP_SM_TRACK]  = {"Trk",   K_ENUM, 0, 1, 1, 0, "", 0, track_names},
    [MP_SM_SLICE]  = {"Slc",   K_LIN, 0, 16, 1, 0, "", 0, 0},                  // 0 = all
    [MP_SM_SMODE]  = {"Mode",  K_ENUM, 0, 1, 1, 0, "", 0, smode_names},
};

// Sum and FX modules (ADR-040): the FX use the ranges of the FX rack (fxrack.c) in the rack's units (0..1 for %, Hz, ms, dB).
static const char *const tr_shapes[]  = {"Sine", "Tri", "Square"};
static const char *const tr_modes[]   = {"Trem", "Pan"};
static const char *const rg_modes[]   = {"Ring", "Up", "Down"};
static const char *const off_on[]     = {"Off", "On"};
static const char *const ch_modes[]   = {"Off", "I", "II", "I+II"};
static const char *const sp_modes[]   = {"Thru", "Freeze", "Gate", "Robot", "Whisp", "Pitch"};
static const char *const cb_names[]   = {"1x12", "4x12", "Bright", "Dark", "Acoust", "Violin", "Drum", "Phone"};

static const mp_t su_mp[] = {
    [MP_SU_LEVEL] = {"Lvl", K_LIN, 0, 1, 0.05f, 1, "", 2, 0},
    [MP_SU_PAN]   = {"Pan", K_LIN, -100, 100, 5, 0, "%", 0, 0},
};
static const mp_t tr_mp[] = {
    [MP_TR_RATE]  = {"Rate", K_LOG, 0.05f, 20, 1.2f, 4, "Hz", 2, 0},
    [MP_TR_DEPTH] = {"Dpth", K_LIN, 0, 1, 0.05f, 0.6f, "", 2, 0},
    [MP_TR_SHAPE] = {"Shp",  K_ENUM, 0, 2, 1, 0, "", 0, tr_shapes},
    [MP_TR_MODE]  = {"Mode", K_ENUM, 0, 1, 1, 0, "", 0, tr_modes},
};
static const mp_t eq_mp[] = {
    [MP_EQ_LOW]  = {"Low",  K_LIN, -15, 15, 1, 0, "dB", 0, 0},
    [MP_EQ_MID]  = {"Mid",  K_LIN, -15, 15, 1, 0, "dB", 0, 0},
    [MP_EQ_MIDF] = {"MidF", K_LOG, 65, 8400, 1.12f, 1000, "Hz", 0, 0},
    [MP_EQ_HIGH] = {"High", K_LIN, -15, 15, 1, 0, "dB", 0, 0},
};
static const mp_t rg_mp[] = {
    [MP_RG_MODE] = {"Mode", K_ENUM, 0, 2, 1, 0, "", 0, rg_modes},
    [MP_RG_FREQ] = {"Freq", K_LOG, 1, 5000, 1.12f, 440, "Hz", 0, 0},
    [MP_RG_MIX]  = {"Mix",  K_LIN, 0, 1, 0.05f, 0.5f, "", 2, 0},
};
static const mp_t ph_mp[] = {
    [MP_PH_RATE]  = {"Rate", K_LOG, 0.05f, 20, 1.2f, 0.4f, "Hz", 2, 0},
    [MP_PH_DEPTH] = {"Dpth", K_LIN, 0, 1, 0.05f, 0.6f, "", 2, 0},
    [MP_PH_FB]    = {"Fb",   K_LIN, -95, 95, 5, 40, "%", 0, 0},
    [MP_PH_MIX]   = {"Mix",  K_LIN, 0, 1, 0.05f, 0.5f, "", 2, 0},
};
static const mp_t fg_mp[] = {
    [MP_PH_RATE]  = {"Rate", K_LOG, 0.05f, 20, 1.2f, 0.3f, "Hz", 2, 0},
    [MP_PH_DEPTH] = {"Dpth", K_LIN, 0, 1, 0.05f, 0.6f, "", 2, 0},
    [MP_PH_FB]    = {"Fb",   K_LIN, -95, 95, 5, 40, "%", 0, 0},
    [MP_PH_MIX]   = {"Mix",  K_LIN, 0, 1, 0.05f, 0.6f, "", 2, 0},
};
static const mp_t cp_mp[] = {
    [MP_CP_THR]   = {"Thr",  K_LIN, -60, 0, 2, -18, "dB", 0, 0},
    [MP_CP_RATIO] = {"Rat",  K_LIN, 1, 20, 1, 4, "", 0, 0},
    [MP_CP_REL]   = {"Rel",  K_LOG, 10, 1000, 1.2f, 150, "ms", 0, 0},
    [MP_CP_GAIN]  = {"Gain", K_LIN, 0, 24, 1, 0, "dB", 0, 0},
};
static const mp_t dl_mp[] = {
    [MP_DL_TIME] = {"Time", K_LIN, 20, 1000, 10, 350, "ms", 0, 0},
    [MP_DL_FB]   = {"Fb",   K_LIN, 0, 0.95f, 0.05f, 0.4f, "", 2, 0},
    [MP_DL_MIX]  = {"Mix",  K_LIN, 0, 1, 0.05f, 0.3f, "", 2, 0},
    [MP_DL_PONG] = {"Pong", K_ENUM, 0, 1, 1, 0, "", 0, off_on},
};
static const mp_t rv_mp[] = {
    [MP_RV_MIX]  = {"Mix",  K_LIN, 0, 1, 0.05f, 0.3f, "", 2, 0},
    [MP_RV_DEC]  = {"Dec",  K_LIN, 0, 0.98f, 0.02f, 0.6f, "", 2, 0},
    [MP_RV_SIZE] = {"Size", K_LIN, 0, 1, 0.05f, 0.6f, "", 2, 0},
    [MP_RV_DAMP] = {"Damp", K_LIN, 0, 1, 0.05f, 0.5f, "", 2, 0},
};
static const mp_t ch_mp[] = {
    [MP_CH_MODE] = {"Mode", K_ENUM, 0, 3, 1, 1, "", 0, ch_modes},
    [MP_CH_MIX]  = {"Mix",  K_LIN, 0, 1, 0.05f, 0.75f, "", 2, 0},
};
static const mp_t sp_mp[] = {
    [MP_SP_MODE]  = {"Mode", K_ENUM, 0, 5, 1, 0, "", 0, sp_modes},
    [MP_SP_SHIFT] = {"Shft", K_LIN, -24, 24, 1, 0, "st", 0, 0},
    [MP_SP_AMT]   = {"Amt",  K_LIN, 0, 1, 0.01f, 0.12f, "", 2, 0},
    [MP_SP_MIX]   = {"Mix",  K_LIN, 0, 1, 0.05f, 1, "", 2, 0},
    [MP_SP_HOLD]  = {"Hold", K_ENUM, 0, 1, 1, 0, "", 0, off_on},
    [MP_SP_LO]    = {"Lo",   K_LOG, 20, 20000, 1.12f, 20, "Hz", 0, 0},
    [MP_SP_HI]    = {"Hi",   K_LOG, 20, 20000, 1.12f, 20000, "Hz", 0, 0},
};
static const mp_t cb_mp[] = {
    [MP_CB_IR]    = {"IR",   K_ENUM, 0, 7, 1, 0, "", 0, cb_names},
    [MP_CB_LEN]   = {"Len",  K_LIN, 64, 512, 32, 256, "", 0, 0},
    [MP_CB_MIX]   = {"Mix",  K_LIN, 0, 1, 0.05f, 1, "", 2, 0},
    [MP_CB_LEVEL] = {"Lvl",  K_LIN, 0, 1, 0.05f, 0.5f, "", 2, 0},
};
static const mp_t es_mp[] = {
    [MP_ES_RATE]  = {"Rate", K_LOG, 0.05f, 5, 1.2f, 0.6f, "Hz", 2, 0},
    [MP_ES_DEPTH] = {"Dpth", K_LIN, 0, 1, 0.05f, 0.7f, "", 2, 0},
    [MP_ES_SHIM]  = {"Shim", K_LIN, 0, 1, 0.05f, 0.35f, "", 2, 0},
    [MP_ES_MIX]   = {"Mix",  K_LIN, 0, 1, 0.05f, 0.7f, "", 2, 0},
};
#define NMP(a) ((int)(sizeof a / sizeof a[0]))

typedef struct {
    const char *code, *name;
    bool audio, modulator;
    int nparams;
    const char *pshort[4], *plong[4];   // modulation targets (see rack_param_name)
    const mp_t *mp; int nmp;            // module's own parameters
} type_info_t;

static const type_info_t info[MOD_TYPE_COUNT] = {
    [MOD_OSC]    = {"OC", "Oscillator", true,  false, 4, {"Pit", "Lvl", "PW", "Mrph"},   {"Pitch", "Level", "Pulse width", "Morph"}, osc_mp, (int)(sizeof osc_mp / sizeof osc_mp[0])},
    [MOD_FILTER] = {"FL", "Filter",     true,  false, 2, {"Cut", "Res"},         {"Cutoff", "Resonance"}, flt_mp, 11},
    [MOD_SAT]    = {"SA", "Saturation", true,  false, 2, {"Drv", "Mix"},         {"Drive", "Mix"}, sat_mp, 3},
    [MOD_LFO]    = {"LF", "LFO",        false, true,  2, {"Rate", "Dpth"},       {"Rate", "Depth"}, lfo_mp, 3},
    [MOD_MSEQ]   = {"MS", "Motion Seq", false, true,  1, {"Dpth"},              {"Depth"}, NULL, 0},
    [MOD_ENV]    = {"EN", "Envelope",   false, true,  1, {"Dpth"},              {"Depth"}, env_mp, 10},
    [MOD_SAMPLER]= {"SM", "Sampler",    true,  false, 1, {"Pit"},               {"Pitch"}, sm_mp, 10},
    [MOD_COMB]   = {"RS", "Resonator",  true,  false, 1, {"Mix"},               {"Mix"}, rs_mp, 5},
    [MOD_EG]     = {"EG", "Multi Env",  false, true,  1, {"Dpth"},              {"Depth"}, eg_mp, 17},
    // ADR-040. Codes differ from the FX rack's where those mean another rack module (FL, RS, EN): FG, RG, ES.
    [MOD_SUM]      = {"SU", "Sum",         true, false, 2, {"Lvl", "Pan"},  {"Level", "Pan"}, su_mp, NMP(su_mp)},
    [MOD_TREM]     = {"TR", "Trem / Pan",  true, false, 1, {"Dpth"},        {"Depth"}, tr_mp, NMP(tr_mp)},
    [MOD_EQ]       = {"EQ", "EQ 3-band",   true, false, 1, {"Mid"},         {"Mid gain"}, eq_mp, NMP(eq_mp)},
    [MOD_RING]     = {"RG", "Ring / Shift", true, false, 2, {"Freq", "Mix"}, {"Frequency", "Mix"}, rg_mp, NMP(rg_mp)},
    [MOD_PHASER]   = {"PH", "Phaser",      true, false, 1, {"Mix"},         {"Mix"}, ph_mp, NMP(ph_mp)},
    [MOD_FLANGER]  = {"FG", "Flanger",     true, false, 1, {"Mix"},         {"Mix"}, fg_mp, NMP(fg_mp)},
    [MOD_COMP]     = {"CP", "Compressor",  true, false, 1, {"Thr"},         {"Threshold"}, cp_mp, NMP(cp_mp)},
    [MOD_DELAY]    = {"DL", "Delay",       true, false, 1, {"Mix"},         {"Mix"}, dl_mp, NMP(dl_mp)},
    [MOD_REVERB]   = {"RV", "Reverb",      true, false, 1, {"Mix"},         {"Mix"}, rv_mp, NMP(rv_mp)},
    [MOD_CHORUS]   = {"CH", "Chorus",      true, false, 1, {"Mix"},         {"Mix"}, ch_mp, NMP(ch_mp)},
    [MOD_SPECTRAL] = {"SP", "Spectral",    true, false, 1, {"Mix"},         {"Mix"}, sp_mp, NMP(sp_mp)},
    [MOD_CAB]      = {"CB", "Cab / Body",  true, false, 1, {"Mix"},         {"Mix"}, cb_mp, NMP(cb_mp)},
    [MOD_ENSEMBLE] = {"ES", "Ensemble",    true, false, 1, {"Mix"},         {"Mix"}, es_mp, NMP(es_mp)},
};

static void depth_reset(rack_t *r, int slot);
static bool global_fx_after_sum(const rack_t *r, int lane, int sum_at, int skip);

const char *rack_type_code(module_type_t t) { return info[t].code; }
const char *rack_type_name(module_type_t t) { return info[t].name; }
bool rack_is_audio(module_type_t t)         { return info[t].audio; }
bool rack_is_modulator(module_type_t t)     { return info[t].modulator; }
bool rack_can_target(module_type_t t)       { return (info[t].modulator && t != MOD_MSEQ) || t == MOD_OSC; }
int  rack_param_count(module_type_t t)      { return info[t].nparams; }
const char *rack_param_name(module_type_t t, int p) { return info[t].pshort[p]; }
const char *rack_param_long(module_type_t t, int p) { return info[t].plong[p]; }

void rack_clear(rack_t *r) {
    memset(r, 0, sizeof *r);
    r->next_id = 1;
    for (int l = 0; l < RACK_LANES; l++) r->lane_lvl[l] = 1.0f;     // implicit Sums: full level, centre
    synth_config_init(&r->cfg);
}

void rack_init(rack_t *r) {
    rack_clear(r);
    rack_insert(r, 0, MOD_OSC);
    rack_insert(r, 1, MOD_FILTER);
    rack_insert(r, 2, MOD_SAT);
    rack_insert(r, 3, MOD_LFO);
    r->slot[3].tgt_id = r->slot[1].id;      // LFO -> filter cutoff
    r->slot[3].tgt_param = 0;
    r->slot[3].v[MP_LF_DEPTH] = 1.5f;       // octaves of cutoff
    r->cfg.fxr.slot[1].v[0] = 1000;         // startup patch: delay 1000 ms, 40 % mix; reverb 40 % mix (the FX type defaults stay dry)
    r->cfg.fxr.slot[1].v[2] = 40;
    r->cfg.fxr.slot[2].v[0] = 40;
}

// The startup patch: four oscillators, each a different engine (Karplus string, Modal, Supersaw, Additive), into one filter; the delay and the
// reverb are on. Eight voices of this is the heaviest thing the default synth does, which is the point: it is the load the engines are tuned for.
void rack_init_startup(rack_t *r) {
    rack_clear(r);
    static const int engine[4] = {0, 1, 4, 6};                       // OSCX_KARP, OSCX_MODAL, OSCX_SSAW, OSCX_ADD
    static const int coarse[4] = {0, 0, 0, 12};
    for (int i = 0; i < 4; i++) {
        rack_insert(r, i, MOD_OSC);
        r->slot[i].v[MP_OC_WAVE] = (float)(OC_FIRST_ENGINE + engine[i]);
        r->slot[i].v[MP_OC_COARSE] = (float)coarse[i];
    }
    rack_insert(r, 4, MOD_FILTER);
    r->cfg.fxr.slot[1].v[0] = 1000;         // delay 1000 ms, 40 % mix; reverb 40 % mix (the FX type defaults stay dry)
    r->cfg.fxr.slot[1].v[2] = 40;
    r->cfg.fxr.slot[2].v[0] = 40;
}

// Measurement patch for the sampler: nothing but a sampler and one filter (wide open), every master effect dry, so what is measured or heard is the sampler path.
void rack_init_sampler(rack_t *r, int file, int loop) {
    rack_clear(r);
    rack_insert(r, 0, MOD_SAMPLER);
    r->slot[0].v[MP_SM_FILE] = (float)(file + 1);
    r->slot[0].v[MP_SM_LOOP] = (float)loop;
    rack_insert(r, 1, MOD_FILTER);
}

static void ms_pattern_default(ms_pattern_t *p) {
    memset(p, 0, sizeof *p);
    for (int l = 0; l < MS_LANES; l++) {
        for (int i = 0; i < MS_STEPS; i++) p->lane[l].val[i] = 50;
        p->lane[l].bipolar = 1;
        p->lane[l].depth = 1.0f;                                 // (replaced by the unit's default once a target is chosen)
    }
    static const uint8_t demo[4] = {100, 30, 70, 0};            // lane 1: a rising and falling shape on every 4th step
    for (int i = 0; i < 4; i++) { p->lane[0].val[i * 4] = demo[i]; p->lane[0].active |= (uint16_t)(1u << (i * 4)); }
}

static int ms_pool_free(const rack_t *r) {
    for (int p = 0; p < MS_POOL; p++) {
        bool used = false;
        for (int i = 0; i < r->count; i++) if (r->slot[i].type == MOD_MSEQ && (int)r->slot[i].v[MP_MS_POOL] == p) used = true;
        if (!used) return p;
    }
    return -1;
}

bool rack_delete(rack_t *r, int pos) {
    if (pos < 0 || pos >= r->count) return false;
    if (r->slot[pos].type == MOD_SUM) {
        const int lane = r->slot[pos].lane;
        if (!global_fx_after_sum(r, lane, RACK_NONE, RACK_NONE)) return false;  // global-only FX still need it
        r->lane_lvl[lane] = r->slot[pos].v[MP_SU_LEVEL];                         // the implicit Sum keeps its level and pan
        r->lane_pan[lane] = r->slot[pos].v[MP_SU_PAN];
    }
    uint8_t id = r->slot[pos].id;
    for (int i = pos; i < r->count - 1; i++) r->slot[i] = r->slot[i + 1];
    r->count--;
    for (int i = 0; i < r->count; i++)
        if (r->slot[i].tgt_id == id) { r->slot[i].tgt_id = 0; r->slot[i].tgt_param = 0; }
    for (int p = 0; p < MS_POOL; p++)
        for (int l = 0; l < MS_LANES; l++)
            if (r->ms[p].lane[l].tgt_id == id) { r->ms[p].lane[l].tgt_id = 0; r->ms[p].lane[l].tgt_param = 0; }
    return true;
}

int rack_find(const rack_t *r, int id) {
    for (int i = 0; i < r->count; i++) if (r->slot[i].id == id) return i;
    return RACK_NONE;
}

int rack_instance(const rack_t *r, int slot) {
    int n = 0;
    for (int i = 0; i <= slot; i++) if (r->slot[i].type == r->slot[slot].type) n++;
    return n;
}

bool rack_slot_is_mod(const rack_t *r, int slot) {
    const rack_slot_t *s = &r->slot[slot];
    return info[s->type].modulator || (s->type == MOD_OSC && s->tgt_id != 0);
}

bool rack_slot_is_audio(const rack_t *r, int slot) {
    return info[r->slot[slot].type].audio;                       // an OSC with a target stays in the chain (it can be muted there)
}

// The chain helpers stay inside the slot's lane.
int rack_audio_prev(const rack_t *r, int slot) {
    for (int i = slot - 1; i >= 0; i--) if (rack_slot_is_audio(r, i) && r->slot[i].lane == r->slot[slot].lane) return i;
    return RACK_NONE;
}

int rack_audio_next(const rack_t *r, int slot) {
    for (int i = slot + 1; i < r->count; i++) if (rack_slot_is_audio(r, i) && r->slot[i].lane == r->slot[slot].lane) return i;
    return RACK_OUT;
}

/* ---------------- lanes and Sum points (ADR-040) ---------------- */

bool rack_type_global_only(module_type_t t) { return t >= MOD_DELAY && t <= MOD_ENSEMBLE; }
bool rack_is_fx(module_type_t t)            { return t >= MOD_TREM && t <= MOD_ENSEMBLE; }

int rack_lane_sum(const rack_t *r, int lane) {
    for (int i = 0; i < r->count; i++) if (r->slot[i].type == MOD_SUM && r->slot[i].lane == lane) return i;
    return RACK_NONE;
}

bool rack_slot_is_global(const rack_t *r, int slot) {
    const int s = rack_lane_sum(r, r->slot[slot].lane);
    return s != RACK_NONE && s < slot;
}

float *rack_lane_level(rack_t *r, int lane) { const int s = rack_lane_sum(r, lane); return s != RACK_NONE ? &r->slot[s].v[MP_SU_LEVEL] : &r->lane_lvl[lane]; }
float *rack_lane_pan(rack_t *r, int lane)   { const int s = rack_lane_sum(r, lane); return s != RACK_NONE ? &r->slot[s].v[MP_SU_PAN] : &r->lane_pan[lane]; }

// A global-only FX of `lane` placed at slot index `at` (in the order after the edit) needs the lane's Sum before it.
static bool global_fx_after_sum(const rack_t *r, int lane, int sum_at, int skip) {
    for (int i = 0; i < r->count; i++) {
        if (i == skip || r->slot[i].lane != lane || !rack_type_global_only((module_type_t)r->slot[i].type)) continue;
        if (sum_at == RACK_NONE || i < sum_at) return false;
    }
    return true;
}

bool rack_can_insert(const rack_t *r, int pos, int lane, module_type_t t) {
    if (r->count >= RACK_MAX || pos < 0 || pos > r->count || t >= MOD_TYPE_COUNT || lane < 0 || lane >= RACK_LANES) return false;
    if (t == MOD_MSEQ && ms_pool_free(r) < 0) return false;
    const int s = rack_lane_sum(r, lane);
    if (t == MOD_SUM) return s == RACK_NONE;                                     // one Sum per lane (no global-only FX can precede it: they need one)
    if (rack_type_global_only(t)) return s != RACK_NONE && s < pos;              // the heavy FX only after the lane's Sum
    return true;
}

static bool insert_raw(rack_t *r, int pos, int lane, module_type_t type) {
    if (r->count >= RACK_MAX || pos < 0 || pos > r->count || type >= MOD_TYPE_COUNT) return false;
    int pool = -1;
    if (type == MOD_MSEQ && (pool = ms_pool_free(r)) < 0) return false;      // both motion sequencers are in use
    for (int i = r->count; i > pos; i--) r->slot[i] = r->slot[i - 1];
    r->slot[pos] = (rack_slot_t){.type = (uint8_t)type, .id = r->next_id++, .lane = (uint8_t)lane};
    for (int i = 0; i < info[type].nmp; i++) r->slot[pos].v[i] = info[type].mp[i].def;
    if (pool >= 0) { r->slot[pos].v[MP_MS_POOL] = (float)pool; ms_pattern_default(&r->ms[pool]); }
    if (type == MOD_SUM) { r->slot[pos].v[MP_SU_LEVEL] = r->lane_lvl[lane]; r->slot[pos].v[MP_SU_PAN] = r->lane_pan[lane]; }   // takes over the implicit Sum
    if (r->next_id == 0) r->next_id = 1;
    r->count++;
    return true;
}

bool rack_insert(rack_t *r, int pos, module_type_t type) { return insert_raw(r, pos, 0, type); }

bool rack_insert_lane(rack_t *r, int pos, int lane, module_type_t type) {
    return rack_can_insert(r, pos, lane, type) && insert_raw(r, pos, lane, type);
}

bool rack_set_lane(rack_t *r, int slot, int lane) {
    if (slot < 0 || slot >= r->count || lane < 0 || lane >= RACK_LANES) return false;
    rack_slot_t *s = &r->slot[slot];
    const int from = s->lane;
    if (from == lane) return true;
    const module_type_t t = (module_type_t)s->type;
    if (t == MOD_SUM) {
        if (rack_lane_sum(r, lane) != RACK_NONE) return false;                   // the other lane has its own
        if (!global_fx_after_sum(r, from, RACK_NONE, RACK_NONE)) return false;   // global-only FX still need it here
        r->lane_lvl[from] = s->v[MP_SU_LEVEL]; r->lane_pan[from] = s->v[MP_SU_PAN];
    } else if (rack_type_global_only(t)) {
        const int sum = rack_lane_sum(r, lane);
        if (sum == RACK_NONE || sum > slot) return false;
    }
    s->lane = (uint8_t)lane;
    return true;
}

bool rack_has_signal(const rack_t *r, int slot) {
    for (int i = 0; i <= slot; i++) {
        if (r->slot[i].lane != r->slot[slot].lane) continue;
        if (r->slot[i].type == MOD_OSC && !(r->slot[i].tgt_id && r->slot[i].v[MP_OC_MUTE] > 0.5f)) return true;
        if (r->slot[i].type == MOD_SAMPLER && r->slot[i].v[MP_SM_FILE] > 0.5f) return true;
    }
    return false;
}

void rack_cycle_target(rack_t *r, int slot, int dir) {
    rack_slot_t *s = &r->slot[slot];
    if (!rack_can_target((module_type_t)s->type)) return;
    // candidates: 0 = none, then every other module in slot order
    int cur = s->tgt_id ? rack_find(r, s->tgt_id) : RACK_NONE;   // slot index or none
    int n = r->count;
    int pos = cur + 1;                     // 0 = none, 1..n = slot+1
    for (int k = 0; k <= n; k++) {
        pos = (pos + dir + n + 1) % (n + 1);
        if (pos == 0) { s->tgt_id = 0; s->tgt_param = 0; depth_reset(r, slot); return; }
        if (pos - 1 != slot) { s->tgt_id = r->slot[pos - 1].id; s->tgt_param = 0; depth_reset(r, slot); return; }
    }
}

void rack_cycle_param(rack_t *r, int slot, int dir) {
    rack_slot_t *s = &r->slot[slot];
    int ti = s->tgt_id ? rack_find(r, s->tgt_id) : RACK_NONE;
    if (!rack_can_target((module_type_t)s->type) || ti == RACK_NONE) return;
    int n = info[r->slot[ti].type].nparams;
    s->tgt_param = (uint8_t)((s->tgt_param + dir + n) % n);
    depth_reset(r, slot);
}

void rack_slot_name(const rack_t *r, int slot, char *buf, int n) {
    snprintf(buf, n, "%s%d", info[r->slot[slot].type].code, rack_instance(r, slot));
}

void rack_describe(const rack_t *r, int slot, char *buf, int n) {
    const rack_slot_t *s = &r->slot[slot];
    char me[8];
    rack_slot_name(r, slot, me, sizeof me);
    if (!rack_can_target((module_type_t)s->type)) { snprintf(buf, n, "%s %s", me, info[s->type].name); return; }
    if (s->type == MOD_OSC && !s->tgt_id) { snprintf(buf, n, "%s %s", me, info[s->type].name); return; }
    int ti = s->tgt_id ? rack_find(r, s->tgt_id) : RACK_NONE;
    if (ti == RACK_NONE) { snprintf(buf, n, "%s > (no target)", me); return; }
    char tg[8];
    rack_slot_name(r, ti, tg, sizeof tg);
    snprintf(buf, n, "%s > %s %s", me, tg, info[r->slot[ti].type].plong[s->tgt_param]);
}

/* ---------------- modulation depth units ---------------- */

static const depth_unit_t du_pitch = {"st",  -96, 96, 1,    12, 0};
static const depth_unit_t du_pct   = {"%",  -100, 100, 5,   50, 0};
static const depth_unit_t du_oct   = {"oct",  -8, 8, 0.25f, 2,  2};
static const depth_unit_t du_drive = {"oct",  -4, 4, 0.25f, 1,  2};
static const depth_unit_t du_plain = {"",     -2, 2, 0.05f, 1,  2};      // a target the mapper does not realise

const depth_unit_t *rack_depth_unit(module_type_t t, int param) {
    switch (t) {
        case MOD_OSC:     return param == 0 ? &du_pitch : &du_pct;     // (Mrph only exists on the engines: the mapper checks)
        case MOD_SAMPLER: return param == 0 ? &du_pitch : NULL;
        case MOD_FILTER:  return param == 0 ? &du_oct : NULL;
        case MOD_SAT:     return param == 0 ? &du_drive : NULL;
        default:          return NULL;
    }
}

int rack_depth_index(module_type_t t) {
    switch (t) {
        case MOD_LFO: return MP_LF_DEPTH;
        case MOD_ENV: return MP_EN_DEPTH;
        case MOD_OSC: return MP_OC_DEPTH;
        case MOD_EG:  return MP_EG_DEPTH;
        default:      return -1;
    }
}

static const depth_unit_t *unit_of(const rack_t *r, int slot) {
    const rack_slot_t *s = &r->slot[slot];
    int ti = s->tgt_id ? rack_find(r, s->tgt_id) : RACK_NONE;
    const depth_unit_t *u = ti == RACK_NONE ? NULL : rack_depth_unit((module_type_t)r->slot[ti].type, s->tgt_param);
    return u ? u : &du_plain;
}

static bool adjust_in(float *v, const depth_unit_t *u, int dir) {
    float nv = *v + (float)dir * fine_lin_step(u->step, u->decimals);
    if (nv < u->min) nv = u->min;
    if (nv > u->max) nv = u->max;
    if (nv > -u->step * 0.5f && nv < u->step * 0.5f) nv = 0;           // no float drift around zero
    if (nv == *v) return false;
    *v = nv;
    return true;
}

static void format_in(float v, const depth_unit_t *u, char *out, int n) { snprintf(out, n, "%+.*f%s", u->decimals, (double)v, u->unit); }

bool rack_depth_adjust(rack_t *r, int slot, int dir) {
    const int i = rack_depth_index((module_type_t)r->slot[slot].type);
    return i >= 0 && adjust_in(&r->slot[slot].v[i], unit_of(r, slot), dir);
}

void rack_depth_format(const rack_t *r, int slot, char *out, int n) {
    const int i = rack_depth_index((module_type_t)r->slot[slot].type);
    format_in(i >= 0 ? r->slot[slot].v[i] : 0, unit_of(r, slot), out, n);
}

static void depth_reset(rack_t *r, int slot) {
    const int i = rack_depth_index((module_type_t)r->slot[slot].type);
    if (i >= 0) r->slot[slot].v[i] = unit_of(r, slot)->def;
}

bool rack_ms_depth_adjust(const rack_t *r, int slot, ms_lane_t *l, int dir) {
    (void)slot;
    int ti = l->tgt_id ? rack_find(r, l->tgt_id) : RACK_NONE;
    const depth_unit_t *u = ti == RACK_NONE ? NULL : rack_depth_unit((module_type_t)r->slot[ti].type, l->tgt_param);
    return adjust_in(&l->depth, u ? u : &du_plain, dir);
}

void rack_ms_depth_format(const rack_t *r, ms_lane_t *l, char *out, int n) {
    int ti = l->tgt_id ? rack_find(r, l->tgt_id) : RACK_NONE;
    const depth_unit_t *u = ti == RACK_NONE ? NULL : rack_depth_unit((module_type_t)r->slot[ti].type, l->tgt_param);
    format_in(l->depth, u ? u : &du_plain, out, n);
}

/* ---------------- motion sequencer lanes ---------------- */

ms_pattern_t *rack_ms(rack_t *r, int slot) { return &r->ms[(int)r->slot[slot].v[MP_MS_POOL] % MS_POOL]; }
const ms_pattern_t *rack_ms_const(const rack_t *r, int slot) { return &r->ms[(int)r->slot[slot].v[MP_MS_POOL] % MS_POOL]; }

// What the engine mapper can realise (keep in step with rack_graph.cpp): oscillator pitch / level / PW, filter cutoff, saturation drive.
bool rack_ms_target_ok(module_type_t t, int param) {
    switch (t) {
        case MOD_OSC:    return param < 4;
        case MOD_SAMPLER: return param == 0;
        case MOD_FILTER: return param == 0;
        case MOD_SAT:    return param == 0;
        default:         return false;
    }
}

void rack_ms_cycle_target(rack_t *r, int slot, int lane, int dir) {
    ms_lane_t *l = &rack_ms(r, slot)->lane[lane];
    // candidates: 0 = none, then (module, parameter) pairs in slot order
    int cand[1 + RACK_MAX * 3][2], n = 1, cur = 0;
    cand[0][0] = 0; cand[0][1] = 0;
    for (int i = 0; i < r->count; i++) {
        if (i == slot) continue;
        for (int p = 0; p < info[r->slot[i].type].nparams; p++) {
            if (!rack_ms_target_ok((module_type_t)r->slot[i].type, p)) continue;
            cand[n][0] = r->slot[i].id; cand[n][1] = p;
            if (l->tgt_id == r->slot[i].id && l->tgt_param == p) cur = n;
            n++;
        }
    }
    cur = (cur + dir + n) % n;
    l->tgt_id = (uint8_t)cand[cur][0];
    l->tgt_param = (uint8_t)cand[cur][1];
    {
        const int ti = l->tgt_id ? rack_find(r, l->tgt_id) : RACK_NONE;
        const depth_unit_t *u = ti == RACK_NONE ? NULL : rack_depth_unit((module_type_t)r->slot[ti].type, l->tgt_param);
        l->depth = (u ? u : &du_plain)->def;
    }
}

void rack_ms_target_name(const rack_t *r, const ms_lane_t *l, char *buf, int n) {
    int ti = l->tgt_id ? rack_find(r, l->tgt_id) : RACK_NONE;
    if (ti == RACK_NONE) { snprintf(buf, n, "--"); return; }
    char nm[8];
    rack_slot_name(r, ti, nm, sizeof nm);
    snprintf(buf, n, "%s %s", nm, info[r->slot[ti].type].pshort[l->tgt_param]);
}

bool rack_ms_adjust_step(ms_lane_t *l, int step, int dir) {
    int v = l->val[step] + dir * 5;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    bool changed = v != l->val[step];
    l->val[step] = (uint8_t)v;
    return changed;
}

void rack_ms_toggle_step(ms_lane_t *l, int step) { l->active ^= (uint16_t)(1u << step); }

/* ---------------- module parameters ---------------- */

int rack_mparam_count(module_type_t t) { return info[t].nmp; }
const char *rack_mparam_label(module_type_t t, int i) { return info[t].mp[i].label; }

bool rack_mparam_adjust(rack_slot_t *s, int i, int dir) {
    const mp_t *d = &info[s->type].mp[i];
    float v = s->v[i], nv;
    if (d->kind == K_ENUM)     nv = v + (float)dir;
    else if (d->kind == K_LOG) nv = dir > 0 ? v * fine_log_step(d->step) : v / fine_log_step(d->step);
    else                       nv = v + (float)dir * fine_lin_step(d->step, d->decimals);
    if (nv < d->min) nv = d->min;
    if (nv > d->max) nv = d->max;
    if (nv == v) return false;
    s->v[i] = nv;
    return true;
}

bool rack_mparam_is_continuous(module_type_t t, int i) { return i >= 0 && i < info[t].nmp && info[t].mp[i].kind != K_ENUM; }

float rack_mparam_norm(const rack_slot_t *s, int i) {
    const mp_t *d = &info[s->type].mp[i];
    if (d->kind == K_ENUM) return 0.0f;
    const float v = s->v[i];
    const float n = d->kind == K_LOG ? logf(v / d->min) / logf(d->max / d->min) : (v - d->min) / (d->max - d->min);
    return n < 0.0f ? 0.0f : n > 1.0f ? 1.0f : n;
}

bool rack_mparam_set_norm(rack_slot_t *s, int i, float n) {
    const mp_t *d = &info[s->type].mp[i];
    if (d->kind == K_ENUM) return false;
    n = n < 0.0f ? 0.0f : n > 1.0f ? 1.0f : n;
    const float q = powf(10.0f, (float)d->decimals);           // to the precision shown: semitones, cents, counts stay whole
    float nv = roundf((d->kind == K_LOG ? d->min * powf(d->max / d->min, n) : d->min + n * (d->max - d->min)) * q) / q;
    nv = nv < d->min ? d->min : nv > d->max ? d->max : nv;
    if (nv == s->v[i]) return false;
    s->v[i] = nv;
    return true;
}

void rack_mparam_format(const rack_slot_t *s, int i, char *out, int n) {
    const mp_t *d = &info[s->type].mp[i];
    float v = s->v[i];
    if (d->kind == K_ENUM) { snprintf(out, n, "%s", d->names[(int)v]); return; }
    if (s->type == MOD_SAMPLER && i == MP_SM_SLICE && v < 0.5f) { snprintf(out, n, "All"); return; }
    if (d->unit[0] == 'H' && v >= 1000) snprintf(out, n, "%.1fkHz", v / 1000.0f);
    else snprintf(out, n, "%.*f%s", d->decimals, v, d->unit);
}
