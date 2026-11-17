#include "core/rack.h"
#include "core/synth_params.h"
#include "core/fine_step.h"
#include "control/param.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// A module's own parameter: its descriptor (framework/control/param.h, the Ritual Machine's text style; def: what a new module
// starts with).
typedef param_desc_t mp_t;

// The Wav list, simple to complex (the user's order, 2026-11-16): a Wav value is the position in this list. A generator keeps its own number (OC_FIRST_ENGINE /
// OC_FIRST_MI below: 0..5 the classic waves, 6..24 the OscEngines engines, 25.. the MiModel order of engine/modules/mi_osc.h): wave_id[Wav] is that number,
// rack_wave_id() / rack_wave_pos() convert. wave_class gives each entry its type, for a menu to pick from later:
//   A  one fixed shape               B  blends and simple stacks of basic waves        C  shaped, filtered or table-based, still harmonic
//   D  formant, physical, percussive E  noise with structure, chaos, stochastic
static const char *const wave_names[] = {"Sine", "Tri", "SawDn", "SawUp", "Pulse", "Noise", "Tonew", "SinTri", "SawSq", "Morph", "SqSub", "SawSub", "Buzz", "CSaw", "3Sine", "3Tri", "3Sq", "3Saw", "Pulsar", "Chip", "SqSync", "SwSync", "3Ring", "Strng", "SSaw", "ZLP", "ZHP", "ZBP", "ZPk", "Add", "PhDist", "VA", "VAVcf", "Fold", "FM2", "CA", "WTbl", "WLine", "WMap", "Terra", "Terrn", "Toy", "Vowel", "Vosim", "FOF", "Karp", "Kick", "Modal", "Scan", "Wind", "PBass", "PBassS", "BSnare", "PSnare", "PSnrS", "PHat", "PHat2", "Cymbal", "Dust", "FNoise", "Clock", "Twin", "PNoise", "Foley", "Morse", "DigMod", "FbFM", "Rossl", "Chaos", "Gendy", "Grain", "Cloud"};
static const uint8_t wave_id[] = {0, 4, 2, 3, 1, 5, 19, 28, 27, 26, 30, 31, 29, 25, 51, 50, 49, 48, 18, 60, 32, 33, 52, 14, 10, 35, 38, 37, 36, 12, 61, 62, 63, 9, 8, 17, 53, 55, 54, 21, 65, 34, 11, 39, 20, 6, 42, 7, 22, 23, 66, 67, 58, 68, 69, 70, 71, 57, 13, 43, 45, 44, 59, 24, 47, 46, 40, 16, 41, 15, 64, 56};
static const char wave_class[] = "AAAAAAABBBBBBBBBBBBBBBBBBCCCCCCCCCCCCCCCCCDDDDDDDDDDDDDDDDEEEEEEEEEEEEEE";
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
    [MP_OC_WAVE]   = {"Wav", 0, OC_WAVE_COUNT - 1, 2, "", PARAM_ENUM, wave_names, 1, 0, PARAM_TEXT_FIXED},
    [MP_OC_PW]     = {"PW", 0.05f, 0.95f, 0.5f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_OC_LEVEL]  = {"Lvl", 0, 1, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_OC_COARSE] = {"Crs", -24, 24, 0, "st", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_OC_FINE]   = {"Fine", -50, 50, 0, "ct", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_OC_MUTE]   = {"Out", 0, 1, 0, "", PARAM_ENUM, out_names, 1, 0, PARAM_TEXT_FIXED},       // with a target: Mute silences the chain output, not the modulation
    [MP_OC_DEPTH]  = {"Dpth", 0, 2, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},            // with a target: modulation depth (in the target's unit, see rack_depth_*)
    [MP_OC_MORPH]  = {"Mrph", 0, 1, 0.5f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},         // engines only
    [MP_OC_QUAL]   = {"Q", 0, 2, 0, "", PARAM_ENUM, qual_names, 1, 0, PARAM_TEXT_FIXED},     // Saw / Pulse / Tri and Strng: PolyBLEP, mipmap table, naive
    [MP_OC_HARM]   = {"Harm", 0, 1, 0.5f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},         // MI models: Plaits' harmonics (Braids ignores it)
};
_Static_assert(sizeof wave_names / sizeof wave_names[0] == OC_WAVE_COUNT, "wave_names: one name per Wav value (rack.h OC_WAVE_COUNT)");
_Static_assert(sizeof wave_id / sizeof wave_id[0] == OC_WAVE_COUNT && sizeof wave_class == OC_WAVE_COUNT + 1, "wave_id, wave_class: one each per Wav value");

int rack_wave_id(int wav) { return wave_id[wav < 0 ? 0 : (wav >= OC_WAVE_COUNT ? OC_WAVE_COUNT - 1 : wav)]; }
int rack_wave_pos(int id) { for (int i = 0; i < OC_WAVE_COUNT; i++) if (wave_id[i] == id) return i; return 0; }
char rack_wave_class(int wav) { return wave_class[wav < 0 ? 0 : (wav >= OC_WAVE_COUNT ? OC_WAVE_COUNT - 1 : wav)]; }
static const mp_t flt_mp[] = {
    [MP_FL_TYPE]   = {"Typ", 0, FILT_COUNT - 1, 1, "", PARAM_ENUM, filter_names, 1, 0, PARAM_TEXT_FIXED},
    [MP_FL_CUT]    = {"Cut", 20, 18000, 4000, "Hz", PARAM_LOG, NULL, 1.12f, 0, PARAM_TEXT_FIXED},
    [MP_FL_RES]    = {"Res", 0.5f, 10, 0.7f, "", PARAM_LIN, NULL, 0.1f, 1, PARAM_TEXT_FIXED},
    [MP_FL_ENVAMT] = {"Env", 0, 8, 0, "", PARAM_LIN, NULL, 0.25f, 2, PARAM_TEXT_FIXED},
    [MP_FL_A]      = {"Atk", 1, 5000, 5, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED},
    [MP_FL_D]      = {"Dec", 1, 5000, 300, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED},
    [MP_FL_S]      = {"Sus", 0, 1, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_FL_R]      = {"Rel", 1, 5000, 200, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED},
    [MP_FL_ACV]    = {"ACv", -100, 100, 55, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_FL_DCV]    = {"DCv", -100, 100, 60, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_FL_RCV]    = {"RCv", -100, 100, 60, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
};
static const mp_t sat_mp[] = {
    [MP_SA_MODE]  = {"Mod", 0, 9, 1, "", PARAM_ENUM, sat_names, 1, 0, PARAM_TEXT_FIXED},
    [MP_SA_DRIVE] = {"Drv", 1, 16, 4, "", PARAM_LOG, NULL, 1.25f, 1, PARAM_TEXT_FIXED},
    [MP_SA_MIX]   = {"Mix", 0, 1, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t lfo_mp[] = {
    [MP_LF_SHAPE] = {"Shp", 0, 3, 0, "", PARAM_ENUM, lfo_names, 1, 0, PARAM_TEXT_FIXED},
    [MP_LF_RATE]  = {"Rate", 0.05f, 30, 4, "Hz", PARAM_LOG, NULL, 1.2f, 2, PARAM_TEXT_FIXED},
    [MP_LF_DEPTH] = {"Dpth", 0, 2, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t rs_mp[] = {
    [MP_RS_TUNE] = {"Crs", -24, 24, 0, "st", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_RS_FB]   = {"Fb", -95, 95, 80, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_RS_DAMP] = {"Dmp", 200, 18000, 8000, "Hz", PARAM_LOG, NULL, 1.25f, 0, PARAM_TEXT_FIXED},
    [MP_RS_INT]  = {"Int", 0, 24, 0, "st", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_RS_MIX]  = {"Mix", 0, 1, 0.5f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t env_mp[] = {
    [MP_EN_A]     = {"Atk", 1, 5000, 5, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED},
    [MP_EN_D]     = {"Dec", 1, 5000, 300, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED},
    [MP_EN_S]     = {"Sus", 0, 1, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_EN_DEPTH] = {"Dpth", 0, 2, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_EN_R]     = {"Rel", 1, 5000, 200, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED},
    [MP_EN_HOLD]  = {"Hld", 0, 2000, 0, "ms", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_EN_START] = {"Str", 0, 1, 0, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_EN_ACV]   = {"ACv", -100, 100, 55, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_EN_DCV]   = {"DCv", -100, 100, 60, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_EN_RCV]   = {"RCv", -100, 100, 60, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
};
static const mp_t eg_mp[] = {            // labels are generic: the EG pages show the selected point (see ui_pages.c / ui_input.c)
    [0]  = {"T1", 0, 2000, 1, "ms", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},   [1]  = {"L1", 0, 100, 100, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED}, [2]  = {"C1", -100, 100, 0, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [3]  = {"T2", 0, 2000, 100, "ms", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED}, [4]  = {"L2", 0, 100, 20, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},  [5]  = {"C2", -100, 100, 60, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [6]  = {"T3", 0, 2000, 0, "ms", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},   [7]  = {"L3", 0, 100, 0, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},   [8]  = {"C3", -100, 100, 60, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [9]  = {"T4", 0, 2000, 0, "ms", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},   [10] = {"L4", 0, 100, 0, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},   [11] = {"C4", -100, 100, 60, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_EG_SUS]   = {"Sus", 1, 4, 2, "", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_EG_REL]   = {"Rel", 1, 5000, 200, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED},
    [MP_EG_RCV]   = {"RCv", -100, 100, 60, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_EG_ONE]   = {"Trig", 0, 1, 0, "", PARAM_ENUM, trig_names, 1, 0, PARAM_TEXT_FIXED},
    [MP_EG_DEPTH] = {"Dpth", 0, 2, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};

static const mp_t sm_mp[] = {
    [MP_SM_FILE]   = {"File", 0, 32, 0, "", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},                  // shown by name (ui_draw.c); 0 = none
    [MP_SM_LEVEL]  = {"Lvl", 0, 1, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_SM_COARSE] = {"Crs", -24, 24, 0, "st", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_SM_FINE]   = {"Fine", -50, 50, 0, "ct", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_SM_LOOP]   = {"Loop", 0, 3, 0, "", PARAM_ENUM, loop_names, 1, 0, PARAM_TEXT_FIXED},
    [MP_SM_REV]    = {"Dir", 0, 1, 0, "", PARAM_ENUM, dir_names, 1, 0, PARAM_TEXT_FIXED},
    [MP_SM_START]  = {"Start", 0, 5000, 0, "ms", PARAM_LIN, NULL, 10, 0, PARAM_TEXT_FIXED},
    [MP_SM_TRACK]  = {"Trk", 0, 1, 0, "", PARAM_ENUM, track_names, 1, 0, PARAM_TEXT_FIXED},
    [MP_SM_SLICE]  = {"Slc", 0, 16, 0, "", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},                  // 0 = all
    [MP_SM_SMODE]  = {"Mode", 0, 1, 0, "", PARAM_ENUM, smode_names, 1, 0, PARAM_TEXT_FIXED},
};

// FX modules (ADR-040 / 041): the ranges of the former FX rack, in the rack's units (0..1 for %, Hz, ms, dB).
static const char *const tr_shapes[]  = {"Sine", "Tri", "Square"};
static const char *const tr_modes[]   = {"Trem", "Pan"};
static const char *const rg_modes[]   = {"Ring", "Up", "Down"};
static const char *const off_on[]     = {"Off", "On"};
static const char *const ch_modes[]   = {"Off", "I", "II", "I+II"};
static const char *const sp_modes[]   = {"Thru", "Freeze", "Gate", "Robot", "Whisp", "Pitch"};
static const char *const cb_names[]   = {"1x12", "4x12", "Bright", "Dark", "Acoust", "Violin", "Drum", "Phone"};

static const mp_t tr_mp[] = {
    [MP_TR_RATE]  = {"Rate", 0.05f, 20, 4, "Hz", PARAM_LOG, NULL, 1.2f, 2, PARAM_TEXT_FIXED},
    [MP_TR_DEPTH] = {"Dpth", 0, 1, 0.6f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_TR_SHAPE] = {"Shp", 0, 2, 0, "", PARAM_ENUM, tr_shapes, 1, 0, PARAM_TEXT_FIXED},
    [MP_TR_MODE]  = {"Mode", 0, 1, 0, "", PARAM_ENUM, tr_modes, 1, 0, PARAM_TEXT_FIXED},
};
static const mp_t eq_mp[] = {
    [MP_EQ_LOW]  = {"Low", -15, 15, 0, "dB", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_EQ_MID]  = {"Mid", -15, 15, 0, "dB", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_EQ_MIDF] = {"MidF", 65, 8400, 1000, "Hz", PARAM_LOG, NULL, 1.12f, 0, PARAM_TEXT_FIXED},
    [MP_EQ_HIGH] = {"High", -15, 15, 0, "dB", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
};
static const mp_t rg_mp[] = {
    [MP_RG_MODE] = {"Mode", 0, 2, 0, "", PARAM_ENUM, rg_modes, 1, 0, PARAM_TEXT_FIXED},
    [MP_RG_FREQ] = {"Freq", 1, 5000, 440, "Hz", PARAM_LOG, NULL, 1.12f, 0, PARAM_TEXT_FIXED},
    [MP_RG_MIX]  = {"Mix", 0, 1, 0.5f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t ph_mp[] = {
    [MP_PH_RATE]  = {"Rate", 0.05f, 20, 0.4f, "Hz", PARAM_LOG, NULL, 1.2f, 2, PARAM_TEXT_FIXED},
    [MP_PH_DEPTH] = {"Dpth", 0, 1, 0.6f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_PH_FB]    = {"Fb", -95, 95, 40, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_PH_MIX]   = {"Mix", 0, 1, 0.5f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t fg_mp[] = {
    [MP_PH_RATE]  = {"Rate", 0.05f, 20, 0.3f, "Hz", PARAM_LOG, NULL, 1.2f, 2, PARAM_TEXT_FIXED},
    [MP_PH_DEPTH] = {"Dpth", 0, 1, 0.6f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_PH_FB]    = {"Fb", -95, 95, 40, "%", PARAM_LIN, NULL, 5, 0, PARAM_TEXT_FIXED},
    [MP_PH_MIX]   = {"Mix", 0, 1, 0.6f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t cp_mp[] = {
    [MP_CP_THR]   = {"Thr", -60, 0, -18, "dB", PARAM_LIN, NULL, 2, 0, PARAM_TEXT_FIXED},
    [MP_CP_RATIO] = {"Rat", 1, 20, 4, "", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_CP_REL]   = {"Rel", 10, 1000, 150, "ms", PARAM_LOG, NULL, 1.2f, 0, PARAM_TEXT_FIXED},
    [MP_CP_GAIN]  = {"Gain", 0, 24, 0, "dB", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
};
static const mp_t dl_mp[] = {
    [MP_DL_TIME] = {"Time", 20, 1000, 350, "ms", PARAM_LIN, NULL, 10, 0, PARAM_TEXT_FIXED},
    [MP_DL_FB]   = {"Fb", 0, 0.95f, 0.4f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_DL_MIX]  = {"Mix", 0, 1, 0.3f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_DL_PONG] = {"Pong", 0, 1, 0, "", PARAM_ENUM, off_on, 1, 0, PARAM_TEXT_FIXED},
};
static const mp_t rv_mp[] = {
    [MP_RV_MIX]  = {"Mix", 0, 1, 0.3f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_RV_DEC]  = {"Dec", 0, 0.98f, 0.6f, "", PARAM_LIN, NULL, 0.02f, 2, PARAM_TEXT_FIXED},
    [MP_RV_SIZE] = {"Size", 0, 1, 0.6f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_RV_DAMP] = {"Damp", 0, 1, 0.5f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t ch_mp[] = {
    [MP_CH_MODE] = {"Mode", 0, 3, 1, "", PARAM_ENUM, ch_modes, 1, 0, PARAM_TEXT_FIXED},
    [MP_CH_MIX]  = {"Mix", 0, 1, 0.75f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t sp_mp[] = {
    [MP_SP_MODE]  = {"Mode", 0, 5, 0, "", PARAM_ENUM, sp_modes, 1, 0, PARAM_TEXT_FIXED},
    [MP_SP_SHIFT] = {"Shft", -24, 24, 0, "st", PARAM_LIN, NULL, 1, 0, PARAM_TEXT_FIXED},
    [MP_SP_AMT]   = {"Amt", 0, 1, 0.12f, "", PARAM_LIN, NULL, 0.01f, 2, PARAM_TEXT_FIXED},
    [MP_SP_MIX]   = {"Mix", 0, 1, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_SP_HOLD]  = {"Hold", 0, 1, 0, "", PARAM_ENUM, off_on, 1, 0, PARAM_TEXT_FIXED},
    [MP_SP_LO]    = {"Lo", 20, 20000, 20, "Hz", PARAM_LOG, NULL, 1.12f, 0, PARAM_TEXT_FIXED},
    [MP_SP_HI]    = {"Hi", 20, 20000, 20000, "Hz", PARAM_LOG, NULL, 1.12f, 0, PARAM_TEXT_FIXED},
};
static const mp_t cb_mp[] = {
    [MP_CB_IR]    = {"IR", 0, 7, 0, "", PARAM_ENUM, cb_names, 1, 0, PARAM_TEXT_FIXED},
    [MP_CB_LEN]   = {"Len", 64, 512, 256, "", PARAM_LIN, NULL, 32, 0, PARAM_TEXT_FIXED},
    [MP_CB_MIX]   = {"Mix", 0, 1, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_CB_LEVEL] = {"Lvl", 0, 1, 0.5f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t es_mp[] = {
    [MP_ES_RATE]  = {"Rate", 0.05f, 5, 0.6f, "Hz", PARAM_LOG, NULL, 1.2f, 2, PARAM_TEXT_FIXED},
    [MP_ES_DEPTH] = {"Dpth", 0, 1, 0.7f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_ES_SHIM]  = {"Shim", 0, 1, 0.35f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
    [MP_ES_MIX]   = {"Mix", 0, 1, 0.7f, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
};
static const mp_t vel_mp[] = {
    [MP_VL_DEPTH] = {"Dpth", 0, 2, 1, "", PARAM_LIN, NULL, 0.05f, 2, PARAM_TEXT_FIXED},
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
    // ADR-040 / 041. Codes differ from the old FX rack's where those mean another rack module (FL, RS, EN): FG, RG, ES.
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
    // The note's velocity as a source (0 .. 1 = velocity 0 .. 127; the keys play 100, the sequencer its steps' Vel): Tgt / Prm / Dpth
    // like an LFO or an envelope. A target after a Para point or in row M follows the newest key.
    [MOD_VEL]      = {"VL", "Velocity",    false, true, 1, {"Dpth"},        {"Depth"}, vel_mp, NMP(vel_mp)},
};

static void depth_reset(rack_t *r, int slot);

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
    for (int b = 0; b < RACK_BRANCHES; b++) r->br_lvl[b] = 1.0f;   // the MIX: both branches at full level, centre
    synth_config_init(&r->cfg);
}

void rack_init_fx(rack_t *r) {
    rack_clear(r);
    r->fx = 1;
    r->fx_route = RACK_ROUTE_A_B_C;
}

const char *rack_route_name(int route) {
    static const char *const n[RACK_ROUTE_COUNT] = {"A/B/C", "A+B/C", "A+B+C", "A+C/A+B"};
    return n[route >= 0 && route < RACK_ROUTE_COUNT ? route : 0];
}

// Cycles per 32-frame engine block of each module in a chain (stereo), measured on the prototype 2026-10-11 (tools/padsfx_test.py,
// ESP32-S3 at 240 MHz, 48 kHz); the 8 pad voices and the output; the block's budget as the firmware reports it.
int rack_fx_load_pct(const rack_t *fx) {
    static const uint16_t cost[MOD_TYPE_COUNT] = {
        [MOD_FILTER] = 8300, [MOD_SAT] = 3700, [MOD_TREM] = 4500, [MOD_EQ] = 5900, [MOD_RING] = 5000, [MOD_PHASER] = 19500,
        [MOD_FLANGER] = 15900, [MOD_COMP] = 8700, [MOD_DELAY] = 8800, [MOD_REVERB] = 26000, [MOD_CHORUS] = 12900, [MOD_SPECTRAL] = 19300,
        [MOD_CAB] = 15600, [MOD_ENSEMBLE] = 17800,
    };
    uint32_t sum = 45000 + 1400;
    for (int i = 0; i < fx->count; i++) sum += cost[fx->slot[i].type < MOD_TYPE_COUNT ? fx->slot[i].type : 0];
    return (int)((sum * 100 + 87000) / 174000);
}

int rack_fx_param_count(module_type_t t) { return t == MOD_FILTER ? MP_FL_RES + 1 : rack_mparam_count(t); }

// Row M of the demo and startup patches: delay 1000 ms at 40 % mix, reverb at 40 % mix.
static void add_delay_reverb(rack_t *r) {
    int k = rack_add_m(r, MOD_DELAY);
    if (k != RACK_NONE) { r->slot[k].v[MP_DL_TIME] = 1000; r->slot[k].v[MP_DL_MIX] = 0.4f; }
    k = rack_add_m(r, MOD_REVERB);
    if (k != RACK_NONE) r->slot[k].v[MP_RV_MIX] = 0.4f;
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
    add_delay_reverb(r);
}

// The startup patch: four oscillators, each a different engine (Karplus string, Modal, Supersaw, Additive), into one filter; the delay and the
// reverb are on. Eight voices of this is the heaviest thing the default synth does, which is the point: it is the load the engines are tuned for.
void rack_init_startup(rack_t *r) {
    rack_clear(r);
    static const int engine[4] = {0, 1, 4, 6};                       // OSCX_KARP, OSCX_MODAL, OSCX_SSAW, OSCX_ADD
    static const int coarse[4] = {0, 0, 0, 12};
    for (int i = 0; i < 4; i++) {
        rack_insert(r, i, MOD_OSC);
        r->slot[i].v[MP_OC_WAVE] = (float)rack_wave_pos(OC_FIRST_ENGINE + engine[i]);
        r->slot[i].v[MP_OC_COARSE] = (float)coarse[i];
    }
    rack_insert(r, 4, MOD_FILTER);
    add_delay_reverb(r);
}

// Measurement patch for the sampler: nothing but a sampler and one filter (wide open), no effects, so what is measured or heard is the sampler path.
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

// The chain helpers stay inside the slot's row.
int rack_audio_prev(const rack_t *r, int slot) {
    for (int i = slot - 1; i >= 0; i--) if (rack_slot_is_audio(r, i) && r->slot[i].row == r->slot[slot].row) return i;
    return RACK_NONE;
}

int rack_audio_next(const rack_t *r, int slot) {
    for (int i = slot + 1; i < r->count; i++) if (rack_slot_is_audio(r, i) && r->slot[i].row == r->slot[slot].row) return i;
    return RACK_OUT;
}

/* ---------------- rows and Para (ADR-041) ---------------- */

static bool is_source(module_type_t t)        { return t == MOD_OSC || t == MOD_SAMPLER; }
bool rack_is_fx(module_type_t t)              { return t >= MOD_TREM && t <= MOD_ENSEMBLE; }
bool rack_type_shared_only(module_type_t t)   { return t >= MOD_DELAY && t <= MOD_ENSEMBLE; }
bool rack_type_voice_only(module_type_t t)    { return is_source(t) || t == MOD_COMB; }
bool rack_is_processor(module_type_t t)       { return t < MOD_TYPE_COUNT && info[t].audio && !is_source(t); }

// A slot that starts its branch's shared part: Para on (a processor that may carry it), or a heavy FX.
static bool starts_shared(const rack_slot_t *s) {
    const module_type_t t = (module_type_t)s->type;
    return rack_type_shared_only(t) || (s->para && rack_is_processor(t) && !rack_type_voice_only(t));
}

int rack_shared_start(const rack_t *r, int row) {
    if (row < 0 || row >= RACK_BRANCHES || r->fx) return RACK_NONE;
    for (int i = 0; i < r->count; i++) if (r->slot[i].row == row && starts_shared(&r->slot[i])) return i;
    return RACK_NONE;
}

bool rack_slot_is_shared(const rack_t *r, int slot) {
    const rack_slot_t *s = &r->slot[slot];
    if (r->fx) return true;
    if (!info[s->type].audio) {                                                 // a modulator runs where its target runs
        const int ti = s->tgt_id ? rack_find(r, s->tgt_id) : RACK_NONE;
        return ti != RACK_NONE && ti != slot && info[r->slot[ti].type].audio && rack_slot_is_shared(r, ti);
    }
    if (s->row >= ROW_M) return true;
    const int ss = rack_shared_start(r, s->row);
    return ss != RACK_NONE && slot >= ss;
}

bool rack_can_para(const rack_t *r, int slot) {
    if (slot < 0 || slot >= r->count || r->fx) return false;
    const module_type_t t = (module_type_t)r->slot[slot].type;
    return r->slot[slot].row < RACK_BRANCHES && rack_is_processor(t) && !rack_type_voice_only(t) && !rack_type_shared_only(t);
}

// The one-way rule: no per-voice-only module in row M, nor after its branch's shared start.
static bool rows_ok(const rack_t *r) {
    for (int i = 0; i < r->count; i++) {
        const rack_slot_t *s = &r->slot[i];
        if (!rack_type_voice_only((module_type_t)s->type)) continue;
        if (s->row >= ROW_M) return false;
        const int ss = rack_shared_start(r, s->row);
        if (ss != RACK_NONE && ss < i) return false;
    }
    return true;
}

bool rack_set_para(rack_t *r, int slot, bool on) {
    if (!rack_can_para(r, slot)) return false;
    const uint8_t old = r->slot[slot].para;
    r->slot[slot].para = on ? 1 : 0;
    if (rows_ok(r)) return true;
    r->slot[slot].para = old;                                                // a per-voice module follows it in the branch
    return false;
}

bool rack_can_insert(const rack_t *r, int pos, int row, module_type_t t) {
    if (r->count >= RACK_MAX || pos < 0 || pos > r->count || t >= MOD_TYPE_COUNT || row < 0 || row >= RACK_ROWS) return false;
    if (t == MOD_MSEQ && ms_pool_free(r) < 0) return false;
    if (r->fx) {                                                             // a chain of the pads: effects only, RACK_FX_SLOTS of them
        int n = 0;
        for (int i = 0; i < r->count; i++) n += r->slot[i].row == row;
        return rack_is_processor(t) && !rack_type_voice_only(t) && n < RACK_FX_SLOTS;
    }
    if (!synth_type_is_rack(r->cfg.type) && (row != ROW_M || !rack_is_processor(t))) return false;   // FM / Strings: effects in row M only
    if (rack_type_voice_only(t)) {
        if (row >= ROW_M) return false;
        for (int i = 0; i < pos; i++) if (r->slot[i].row == row && starts_shared(&r->slot[i])) return false;   // after the shared start
    } else if (rack_type_shared_only(t) && row < ROW_M) {
        for (int i = pos; i < r->count; i++) if (r->slot[i].row == row && rack_type_voice_only((module_type_t)r->slot[i].type)) return false;
    }
    return true;
}

static bool insert_raw(rack_t *r, int pos, int row, module_type_t type) {
    if (r->count >= RACK_MAX || pos < 0 || pos > r->count || type >= MOD_TYPE_COUNT) return false;
    int pool = -1;
    if (type == MOD_MSEQ && (pool = ms_pool_free(r)) < 0) return false;      // both motion sequencers are in use
    for (int i = r->count; i > pos; i--) r->slot[i] = r->slot[i - 1];
    r->slot[pos] = (rack_slot_t){.type = (uint8_t)type, .id = r->next_id++, .row = (uint8_t)row, .penv = PARA_ENV_LEGATO};
    for (int i = 0; i < info[type].nmp; i++) r->slot[pos].v[i] = info[type].mp[i].def;
    if (pool >= 0) { r->slot[pos].v[MP_MS_POOL] = (float)pool; ms_pattern_default(&r->ms[pool]); }
    if (r->next_id == 0) r->next_id = 1;
    r->count++;
    return true;
}

bool rack_insert(rack_t *r, int pos, module_type_t type) { return insert_raw(r, pos, 0, type); }

bool rack_insert_row(rack_t *r, int pos, int row, module_type_t type) {
    return rack_can_insert(r, pos, row, type) && insert_raw(r, pos, row, type);
}

int rack_add_m(rack_t *r, module_type_t type) { return rack_insert_row(r, r->count, ROW_M, type) ? r->count - 1 : RACK_NONE; }

bool rack_set_row(rack_t *r, int slot, int row) {
    if (slot < 0 || slot >= r->count || row < 0 || row >= RACK_ROWS) return false;
    rack_slot_t *s = &r->slot[slot];
    const uint8_t from = s->row;
    if (from == row) return true;
    if (r->fx) {                                                             // to another chain, while it has room
        int n = 0;
        for (int i = 0; i < r->count; i++) n += r->slot[i].row == row;
        if (n >= RACK_FX_SLOTS) return false;
        s->row = (uint8_t)row;
        return true;
    }
    if (!synth_type_is_rack(r->cfg.type)) return false;                      // FM / Strings: row M only
    s->row = (uint8_t)row;
    if (rows_ok(r)) return true;
    s->row = from;
    return false;
}

bool rack_has_signal(const rack_t *r, int slot) {
    const int row = r->slot[slot].row;
    if (r->fx) return true;                                                  // the pads feed every chain
    if (row >= ROW_M && !synth_type_is_rack(r->cfg.type)) return true;      // FM / Strings voices feed row M
    for (int i = 0; i < r->count; i++) {
        if (row < ROW_M && (i > slot || r->slot[i].row != row)) continue;   // a branch: its own sources up to the slot; row M: any branch's
        if (row >= ROW_M && r->slot[i].row >= ROW_M) continue;
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

bool rack_set_target(rack_t *r, int slot, int target, int param) {
    if (slot < 0 || slot >= r->count || target < 0 || target >= r->count || slot == target) return false;
    rack_slot_t *s = &r->slot[slot];
    if (!rack_can_target((module_type_t)s->type) || param < 0 || param >= info[r->slot[target].type].nparams) return false;
    s->tgt_id = r->slot[target].id;
    s->tgt_param = (uint8_t)param;
    depth_reset(r, slot);
    return true;
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
        case MOD_VEL: return MP_VL_DEPTH;
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
const char *rack_mparam_label(module_type_t t, int i) { return info[t].mp[i].name; }

bool rack_mparam_adjust(rack_slot_t *s, int i, int dir) {
    const float nv = pdesc_step(&info[s->type].mp[i], s->v[i], dir, g_fine_step);    // (an enumeration keeps its steps)
    if (nv == s->v[i]) return false;
    s->v[i] = nv;
    return true;
}

bool rack_mparam_is_continuous(module_type_t t, int i) { return i >= 0 && i < info[t].nmp && pdesc_is_continuous(&info[t].mp[i]); }

float rack_mparam_norm(const rack_slot_t *s, int i) {
    const mp_t *d = &info[s->type].mp[i];
    return pdesc_is_continuous(d) ? pdesc_to_norm(d, s->v[i]) : 0.0f;
}

// Back to the value a new module starts with (insert_raw). Not the sampler's File nor the motion sequencer's pool: they are not values.
static bool keeps_value(const rack_slot_t *s, int i) { return (s->type == MOD_SAMPLER && i == MP_SM_FILE) || (s->type == MOD_MSEQ && i == MP_MS_POOL); }

bool rack_mparam_reset(rack_slot_t *s, int i) {
    if (s->type >= MOD_TYPE_COUNT || i < 0 || i >= info[s->type].nmp || keeps_value(s, i)) return false;
    const float d = info[s->type].mp[i].def;
    if (s->v[i] == d) return false;
    s->v[i] = d;
    return true;
}

bool rack_slot_reset(rack_t *r, int slot) {
    if (slot < 0 || slot >= r->count) return false;
    bool any = false;
    for (int i = 0; i < MOD_PARAM_MAX; i++) any |= rack_mparam_reset(&r->slot[slot], i);
    return any;
}

bool rack_mparam_set_norm(rack_slot_t *s, int i, float n) {
    const mp_t *d = &info[s->type].mp[i];
    if (!pdesc_is_continuous(d)) return false;
    const float nv = pdesc_from_norm(d, n);                    // to the precision shown: semitones, cents, counts stay whole
    if (nv == s->v[i]) return false;
    s->v[i] = nv;
    return true;
}

void rack_mparam_format(const rack_slot_t *s, int i, char *out, int n) {
    const mp_t *d = &info[s->type].mp[i];
    const float v = s->v[i];
    if (d->curve != PARAM_ENUM && s->type == MOD_SAMPLER && i == MP_SM_SLICE && v < 0.5f) { snprintf(out, n, "All"); return; }
    pdesc_format(d, v, out, (size_t)n);
}
