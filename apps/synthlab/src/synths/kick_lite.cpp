// Kick Lite: the Kick (kick.cpp) with 8 values: Tune, the four macros, Click, Rumble (the rumble's level) and Room (the reverb's mix).
// Every other value is fixed at the Kick's default. A clone of the Kick's engine, then made cheaper where the fixed values allow it and
// where it cannot be heard: tests/test_null.cpp renders its presets (kick_lite.h) with both synths and checks the difference.
// docs/SYNTHS.md, "Kick Lite".
#include <cstring>
#include "lab.h"
#include "lab_dsp.h"
#include "synths/kick_lite.h"

namespace {

// The optimisations that may change the sound, kept because the null test against the Kick (tests/test_null.cpp) stays far under
// what can be heard: every preset at least 75 dB under the signal with both on (docs/SYNTHS.md, "Kick Lite": the measures, and the
// ones that were tried and rejected). 1 = on; 0 = the Kick's own code, for an A/B.
#define LITE_O1 1   // the distortions (tube -> fold -> sat) as one table, rebuilt when they change: -6 ns a sample (host)
#define LITE_O4 1   // the triangle skipped once the harmonics envelope has gone (under 1e-4): -8 ns a sample (host)

enum : int {
    H_MACRO, M_PUNCH, M_BODY, M_GRIT, M_TONE,
    H_OSC, P_TUNE, P_SHAPE, P_HARM, P_CLICK,
    H_NOISE, P_NLEVEL, P_NMIX, P_NDECAY, P_NPRE, P_NLP, P_NRES, P_NPOST, P_NDLY, P_NFB, P_NDMIX, P_NDUCK, P_NDREL, P_NSEED,
    H_PITCH, P_PAMT, P_PDECAY, P_PCURVE,
    H_HENV, P_HAMT, P_HDECAY,
    H_AMP, P_ATTACK, P_HOLD, P_DECAY, P_CURVE,
    H_DRIVE, P_DRIVE, P_TUBE, P_FOLD, P_SAT, P_CLIP,
    H_COMP, P_CLO, P_CMID, P_CHI, P_CATK, P_CREL,
    H_EQ, P_EQLO, P_EQMID, P_EQMHZ, P_EQHI,
    H_OUT, P_LEVEL,
    H_VERB, P_RROUTE, P_RMIX, P_RDECAY, P_RSIZE, P_RDAMP,
    H_MASTER, P_MGLUE, P_MATK, P_MREL, P_MSAT, P_MTILT, P_MOUT,
    N_PARAMS
};

const char *const kCurves[] = {"Lin", "Exp"};
const char *const kRoutes[] = {"Output", "Rumble"};

const lab::Param kFull[] = {                        // the Kick's values, as in kick.cpp: their defaults and ranges
    lab::section("MACROS"),                         // 50 % = the values below as they are
    {"Punch",    0,    100,   50,   "%"},           //   pitch drop deeper and faster, more click, the compressor lets the hit through
    {"Body",     0,    100,   50,   "%"},           //   longer, more low end, the low band held up
    {"Grit",     0,    100,   50,   "%"},           //   more drive, shape, tube, saturation, then fold; the rumble dirtier
    {"Tone",     0,    100,   50,   "%"},           //   more harmonics, brighter top, the rumble's cutoff higher
    lab::section("OSC"),
    {"Tune",     30,   150,   50,   "Hz", lab::Exp},    // on C3; the keys transpose it
    {"Shape",    0,    100,   15,   "%"},               // sine -> distorted sine
    {"Harmonic", 0,    100,   0,    "%"},               // sine -> triangle
    {"Click",    0,    100,   25,   "%"},               // a burst of band-passed noise on the hit (seeded: the same every hit)
    lab::section("RUMBLE"),                            // noise + kick -> Pre -> resonant low pass -> Post -> its decay -> echo -> duck,
                                                        //   added under the body
    {"Level",    0,    100,   20,   "%"},
    {"In Mix",   0,    100,   0,    "%"},               // what feeds it, a crossfade: 0 = the noise, 50 = the noise and the kick both full,
                                                        //   100 = the kick's oscillator (before its amp env: a tone that follows the
                                                        //   sweep, as long as the rumble's Decay)
    {"Decay",    5,    4000,  300,  "ms", lab::Exp},    // to 1 %
    {"Pre",      0,    100,   30,   "%"},               // distortion before the filter: the noise thicker, denser
    {"Cutoff",   20,   500,   90,   "Hz", lab::Exp},    // the low pass (12 dB / octave); its level is kept as it moves
    {"Reso",     0,    100,   40,   "%"},               // Q 0.5 x 20^reso (0.5 .. 10): the rumble whistles around the cutoff
    {"Post",     0,    100,   40,   "%"},               // distortion after the filter: the rumble growls (harmonics of the lows)
    {"Delay",    10,   250,   80,   "ms", lab::Exp},    // an echo of the rumble (run at 1/8 of the sample rate: the rumble is low)
    {"Feedbk",   0,    90,    35,   "%"},               // how much of each echo comes back
    {"Dly Mix",  0,    100,   30,   "%"},               // 100 %: only the echoes (the rumble then starts one Delay after the hit)
    {"Duck",     0,    100,   50,   "%"},               // the rumble pulled down while the body sounds (keyed by the amp env, +12 dB:
                                                        //   all the way while the body is within 12 dB of its top): 100 % = gone
    {"Duck Rel", 10,   1000,  80,   "ms", lab::Exp},    // how fast it comes back as the body falls
    {"Seed",     1,    99,    1,    nullptr, lab::Int}, // which noise: the same waveform at every hit; another seed, another one
    lab::section("PITCH ENV"),
    {"Amount",   0,    60,    30,   "st", lab::Int},    // how far above the tune the hit starts
    {"Decay",    2,    1000,  45,   "ms", lab::Exp},    // Lin: back to the tune in that time; Exp: 99 % of the way
    {"Curve",    0,    1,     1,    nullptr, lab::Int, kCurves},
    lab::section("HARM ENV"),
    {"Amount",   0,    100,   40,   "%"},               // added to Harmonic at the hit
    {"Decay",    2,    2000,  25,   "ms", lab::Exp},
    lab::section("AMP ENV"),
    {"Attack",   0.1f, 50,    0.5f, "ms", lab::Exp},
    {"Hold",     0,    500,   30,   "ms"},
    {"Decay",    10,   4000,  350,  "ms", lab::Exp},
    {"Curve",    0,    100,   60,   "%"},               // 0 = a straight line down, 100 = a fast drop and a long tail
    lab::section("DRIVE"),
    {"Drive",    0,    24,    6,    "dB", lab::Int},    // how hard Tube and Sat are hit
    {"Tube",     0,    100,   25,   "%"},               // asymmetric: even harmonics, warmth
    {"Fold",     0,    100,   0,    "%"},               // a sine wavefolder: the peaks fold back, metallic
    {"Sat",      0,    100,   35,   "%"},               // symmetric soft saturation: odd harmonics, loudness
    {"Clip",     0,    100,   0,    "%"},               // hard clip, the threshold lowered as it rises
    lab::section("3-BAND COMP"),                        // split at 120 Hz and 2.5 kHz; each amount lowers the threshold and raises the ratio
    {"Low",      0,    100,   40,   "%"},
    {"Mid",      0,    100,   25,   "%"},
    {"High",     0,    100,   30,   "%"},
    {"Attack",   0.1f, 50,    6,    "ms", lab::Exp},
    {"Release",  10,   1000,  120,  "ms", lab::Exp},
    lab::section("EQ"),
    {"Low",      -12,  12,    0,    "dB", lab::Int},    // a shelf at 90 Hz
    {"Mid",      -12,  12,    0,    "dB", lab::Int},    // a bell, at Mid Hz
    {"Mid Hz",   100,  8000,  600,  "Hz", lab::Exp},
    {"High",     -12,  12,    0,    "dB", lab::Int},    // a shelf at 5 kHz
    lab::section("OUT"),
    {"Level",    -30,  6,     0,    "dB", lab::Int},
    lab::section("REVERB"),
    {"Route",    0,    1,     0,    nullptr, lab::Int, kRoutes},    // Output: on the sum of the voices, after them (its tail outlives the
                                                        //   hit); Rumble: inside the rumble's chain, after its decay (its tail is the
                                                        //   rumble's: run at 1/8 of the sample rate), the output's reverb off
    {"Mix",      0,    100,   15,   "%"},               // Output: the wet added; Rumble: dry -> wet (100 %: only the reverb)
    {"Decay",    100,  4000,  500,  "ms", lab::Exp},    // to -60 dB
    {"Size",     10,   100,   60,   "%"},               // the delay lines' length: 5 to 83 ms
    {"Damp",     0,    100,   50,   "%"},               // a low pass in the loop: the tail darkens
    lab::section("MASTER"),                             // the shaper on the sum, after the reverb: glue -> saturation -> tilt -> output
    {"Glue",     0,    100,   30,   "%"},               // a bus compressor: threshold 0 .. -30 dB, ratio 1 .. 4, 6 dB soft knee, a quarter
                                                        //   of the reduction given back (makeup)
    {"Attack",   0.1f, 30,    3,    "ms", lab::Exp},    // fast: it evens the hit and its body (glue); slow: the hit passes (punch)
    {"Release",  30,   1000,  150,  "ms", lab::Exp},
    {"Sat",      0,    100,   20,   "%"},               // a soft saturation: the body denser and louder, the peaks rounded
    {"Tilt",     -100, 100,   0,    "%"},               // around 800 Hz: - darker, + brighter (the ends: +-6 dB on each side)
    {"Output",   -12,  6,     0,    "dB", lab::Int},
};
static_assert(sizeof kFull / sizeof kFull[0] == N_PARAMS, "the enum follows the list");

// Kick Lite's values, and the Kick's row each one is (the rest stay at kFull's defaults)
const lab::Param kParams[] = {
    {"Tune",   30, 150, 50, "Hz", lab::Exp},        // on C3; the keys transpose it
    {"Punch",  0,  100, 50, "%"},                   // the Kick's macros: 50 % = its defaults
    {"Body",   0,  100, 50, "%"},
    {"Grit",   0,  100, 50, "%"},
    {"Tone",   0,  100, 50, "%"},
    {"Click",  0,  100, 25, "%"},
    {"Rumble", 0,  100, 20, "%"},                   // the rumble's level
    {"Room",   0,  100, 15, "%"},                   // the reverb's mix
};
static_assert(sizeof kParams / sizeof kParams[0] == kick_lite::kValues, "kick_lite.h's count");
constexpr int kMap[kick_lite::kValues] = {P_TUNE, M_PUNCH, M_BODY, M_GRIT, M_TONE, P_CLICK, P_NLEVEL, P_RMIX};

// The full values from Kick Lite's.
void full_values(const float *p, float *f) {
    for (int i = 0; i < N_PARAMS; i++) f[i] = kFull[i].def;
    for (int k = 0; k < kick_lite::kValues; k++) f[kMap[k]] = p[k];
}

inline float cl(int i, float v) { return lab::clampf(v, kFull[i].min, kFull[i].max); }   // a value moved by a macro, kept in range

// The reverb: four delay lines fed back through a 4 x 4 Hadamard matrix (a feedback delay network), a one-pole low pass in each loop,
// 16-bit lines. Lean, not lush. Two copies: the output's (Fx, run at half the sample rate, 12.7 KB) and the rumble's (Voice, run at an
// eighth: the rumble is low, 3.2 KB). L0..L3: the lines' longest lengths in ticks.
template <int L0, int L1, int L2, int L3>
struct Room {
    static constexpr int kMax[4] = {L0, L1, L2, L3};
    int16_t l0[L0] = {}, l1[L1] = {}, l2[L2] = {}, l3[L3] = {};
    int w[4] = {}, len[4] = {L0, L1, L2, L3};
    float lp[4] = {}, fb = 0.f, damp = 1.f;

    // size: the lines' length, 0..1 of their longest; decay: to -60 dB, in ms; damp 0..1; rate: ticks per second.
    LAB_COLD void set(float size, float decay_ms, float damp01, float rate) {
        damp = 1.f - 0.9f * damp01;
        float avg = 0.f;
        for (int k = 0; k < 4; k++) {
            len[k] = static_cast<int>(static_cast<float>(kMax[k]) * size);
            if (len[k] < 4) len[k] = 4;
            avg += 0.25f * static_cast<float>(len[k]);
        }
        fb = lab::fast_exp2(-9.966f * avg / (decay_ms * 0.001f * rate));     // -60 dB (2^-9.966) after decay_ms
    }
    static float rd(const int16_t *line, int w, int len, int max) {   // the sample written `len` ticks ago
        int r = w - len;
        if (r < 0) r += max;
        return static_cast<float>(line[r]) * (1.f / 16384.f);         // (stored at half scale: headroom)
    }
    static void wr(int16_t *line, int &w, int max, float v) {
        const float x = v * 16384.f;
        line[w] = static_cast<int16_t>(x > 32767.f ? 32767.f : (x < -32767.f ? -32767.f : x));
        if (++w >= max) w = 0;
    }
    float tick(float in) {
        const float r0 = rd(l0, w[0], len[0], L0), r1 = rd(l1, w[1], len[1], L1);
        const float r2 = rd(l2, w[2], len[2], L2), r3 = rd(l3, w[3], len[3], L3);
        const float a = r0 + r1, b = r0 - r1, c = r2 + r3, e = r2 - r3;        // Hadamard: every line feeds every other, energy kept
        const float m[4] = {(a + c) * 0.5f, (b + e) * 0.5f, (a - c) * 0.5f, (b - e) * 0.5f};
        for (int k = 0; k < 4; k++) lp[k] += (m[k] - lp[k]) * damp;
        wr(l0, w[0], L0, in + fb * lp[0]);                                  // (the input in alternate signs: less of it on one mode)
        wr(l1, w[1], L1, -in + fb * lp[1]);
        wr(l2, w[2], L2, in + fb * lp[2]);
        wr(l3, w[3], L3, -in + fb * lp[3]);
        return (r0 - r1 + r2 - r3) * 0.35f;
    }
    void flush() { for (float &x : lp) lab::flush(x); }             // (the lines are 16 bits: they reach 0 by themselves)
};

// The distortions as one table (O1): y -> tube -> fold -> sat, over -kTabR..kTabR in kTabN steps, followed in lines between its
// points; beyond, a line with the slope of its ends (tube and sat are flat there: what is left is their dry part, a line). Shared by
// the voices: they have the same values. 8 KB. Built on the audio thread when Grit changes (2049 points of the direct code).
constexpr int kTabN = 2048;                         // (512 steps over +-4: Gritty 40 dB; 1024 over +-2: 64 dB; 2048 over +-2: 75 dB)
constexpr float kTabR = 2.f;                        // (the input stays within about +-1.5)
float g_tab[kTabN + 1];
float g_tab_key[6] = {-1.f};                        // the values it was built for (tube, bias, g, fold, fold_g, sat)

// What the values decide, for the sample loop: Voice::update() computes it when a value (or the sample rate) changes.
struct Derived {
    float inv_sr = 1.f / 48000.f, ms = 48.f;
    float pamt12 = 0.f, pk = 0.f;                       // pitch env: semitones / 12; the exponential's speed (Curve: Exp, fixed)
    float click = 0.f, kclick = 0.f, kres = 0.f;
    float nlevel = 0.f, knoise = 0.f, nmakeup = 1.f;    // the rumble: level, decay, the filter's level compensation
    float npre = 0.f, npre_g = 1.f, npre_n = 1.f, npost = 0.f, npost_g = 1.f, npost_n = 1.f;
    float dmix = 0.f, dfb = 0.f;                        // the rumble's echo: mix, feedback, length (ticks at sr / 8)
    float duck = 0.f, kduck = 0.f;                      // the rumble's ducking: depth, the follower's fall per sample
    int dlen = 1;
    float harm = 0.f, hamt = 0.f, hk = 0.f;
    bool tri_on = false;
    float attack = 1.f, inv_attack = 1.f, hold = 0.f, inv_decay = 1.f, acurve = 1.f, end = 0.f;
    float shape = 0.f, shape_g = 1.f, shape_n = 1.f;
    float g = 1.f, tube = 0.f, bias = 0.f, bias_y = 0.f, tube_n = 1.f, sat = 0.f, sat_n = 1.f, fold = 0.f, fold_g = 1.f;
    // (Clip: 0, fixed. The compressor: always on, its three bands too: Low >= 10 %, Mid 25 %, High 30 %, fixed)
    float thr[3] = {}, slope[3] = {}, makeup[3] = {}, kat = 0.f, krel = 0.f;
    float level = 1.f;
};

struct Voice {
    float pc[kick_lite::kValues] = {};              // the values update() last saw
    float f[N_PARAMS] = {};                         // the full values they stand for (full_values)
    float sr_ = 0.f;
    Derived d;
    float ph = 0.f, t = 0.f, vel = 1.f, base = 50.f;    // the phase (0..1); samples since the hit; the pitch at the end of the sweep
    float dt_ = 0.f, h_ = 0.f, a_ = 0.f;            // the phase step, the harmonics and the level at t (the next block's start)
    float click = 0.f, nenv = 0.f;                  // the click's and the noise's envelopes
    lab::Svf click_bp, noise_lp;
    lab::Noise click_noise, noise;
    // the rumble's echo: a 16-bit line at an eighth of the sample rate (8 samples averaged in, a line between two ticks out): 250 ms in 3 KB
    static constexpr int kDecim = 8, kDlyMax = 1536;
    int16_t dline[kDlyMax] = {};
    int dw = 0;
    float duckf = 0.f;                              // the ducking's follower of the body's level: up at once, down at Duck Rel
    // the rumble runs at sr / kDecim (rumble_tick): its output in a line between ticks
    int rphase = 0;
    float rprev = 0.f, rcur = 0.f;
    float last_src = 0.f, residual = 0.f;           // a hit over a sounding one: the old level fades out in 1 ms instead of a jump
    lab::DcBlock dc;
    lab::Svf split_lo, split_hi;                    // the compressor's bands: low pass, high pass, and the rest is the mid
    float det[3] = {}, gain[3] = {1.f, 1.f, 1.f};
    lab::Biquad eq[3];
    bool eq_on[3] = {};

    LAB_COLD void update(const float *p, float sr) {
        Derived &D = d;
        const float ms = sr * 0.001f;
        D.inv_sr = 1.f / sr;
        D.ms = ms;
        // the macros: -1..+1 around their middle, each moving its values (kept in their ranges)
        const float mp = (p[M_PUNCH] - 50.f) * 0.02f, mb = (p[M_BODY] - 50.f) * 0.02f;
        const float mg = (p[M_GRIT] - 50.f) * 0.02f, mt = (p[M_TONE] - 50.f) * 0.02f;

        const float pdec = cl(P_PDECAY, p[P_PDECAY] * lab::fast_exp2(-0.7f * mp)) * ms;
        D.pamt12 = cl(P_PAMT, p[P_PAMT] + 12.f * mp) * (1.f / 12.f);
        D.pk = -6.644f / pdec;                      // 2^(pk t): 1 % at t = pdec
        D.click = cl(P_CLICK, p[P_CLICK] + 40.f * mp) * 0.03f;
        D.kclick = lab::decay_coef(4.f, sr);
        D.kres = lab::decay_coef(1.f, sr);

        const float ndec = p[P_NDECAY] * ms;
        D.nlevel = p[P_NLEVEL] * 0.01f;
        const float tr = sr / kDecim;                                               // the rumble's rate (6 kHz at 48 kHz)
        D.knoise = lab::fast_exp2(-6.644f * kDecim / ndec);                         // (per tick)
        const float ncut = cl(P_NLP, p[P_NLP] * lab::fast_exp2(mt));             // (Tone: +- 1 octave)
        const float nq = 0.5f * lab::fast_exp2(4.3219f * p[P_NRES] * 0.01f);       // Q 0.5 x 20^reso: 0.5 .. 10, audible from the middle
        noise_lp.set(ncut, (2.f - 1.f / nq) / 1.9f, tr);                           // (lab::Svf's res: Q = 1 / (2 - 1.9 res))
        D.nmakeup = sqrtf(0.5f * tr / ncut);                                       // low-passed white noise loses level as sqrt(cutoff)
        D.npre = cl(P_NPRE, p[P_NPRE] + 30.f * mg) * 0.01f;
        D.npre_g = 1.f + 7.f * D.npre;
        D.npre_n = 1.f / lab::soft_clip(D.npre_g);
        D.npost = cl(P_NPOST, p[P_NPOST] + 30.f * mg) * 0.01f;
        D.npost_g = 1.f + 7.f * D.npost;
        D.npost_n = 1.f / lab::soft_clip(D.npost_g);

        D.harm = cl(P_HARM, p[P_HARM] + 30.f * mt) * 0.01f;
        D.hamt = cl(P_HAMT, p[P_HAMT] + 30.f * mt) * 0.01f;
        D.hk = -6.644f / (p[P_HDECAY] * ms);
        D.tri_on = D.harm > 0.f || D.hamt > 0.f;

        const float decay = cl(P_DECAY, p[P_DECAY] * lab::fast_exp2(1.2f * mb)) * ms;
        D.attack = p[P_ATTACK] * ms;
        D.inv_attack = 1.f / D.attack;
        D.hold = cl(P_HOLD, p[P_HOLD] + 60.f * mb) * ms;
        D.inv_decay = 1.f / decay;
        D.acurve = 1.f + 7.f * p[P_CURVE] * 0.01f;
        D.dmix = p[P_NDMIX] * 0.01f;
        D.duck = p[P_NDUCK] * 0.01f;
        D.kduck = lab::decay_coef(p[P_NDREL], tr);
        D.dfb = p[P_NFB] * 0.01f;
        D.dlen = static_cast<int>(p[P_NDLY] * sr * (0.001f / kDecim));
        if (D.dlen < 1) D.dlen = 1;
        if (D.dlen > kDlyMax - 1) D.dlen = kDlyMax - 1;
        float noise_end = D.nlevel > 0.f ? 2.f * ndec : 0.f;                          // (the noise: 1e-4 at 2 x decay)
        if (noise_end > 0.f && D.dmix > 0.f) {                                          // and its echoes, down to -60 dB (5 s at most)
            const float repeats = D.dfb > 0.001f ? -9.966f / lab::fast_log2(D.dfb) : 0.f;
            const float tail = static_cast<float>(D.dlen * kDecim) * (repeats + 1.f);
            noise_end += tail < 5.f * sr ? tail : 5.f * sr;
        }
        const float amp_end = D.attack + D.hold + decay;
        D.end = (amp_end > noise_end ? amp_end : noise_end) + 30.f * ms;                                // (30 ms for the chain's tail)

        D.shape = cl(P_SHAPE, p[P_SHAPE] + 35.f * mg) * 0.01f;
        D.shape_g = 1.f + 6.f * D.shape;
        D.shape_n = 1.f / lab::soft_clip(D.shape_g);
        D.g = lab::fast_db(cl(P_DRIVE, p[P_DRIVE] + 9.f * mg));
        D.tube = cl(P_TUBE, p[P_TUBE] + 30.f * mg) * 0.01f;
        D.bias = 0.3f * D.tube;
        D.bias_y = lab::soft_clip(D.bias);
        D.tube_n = 1.f / (lab::soft_clip(D.g + D.bias) - D.bias_y);
        D.sat = cl(P_SAT, p[P_SAT] + 30.f * mg) * 0.01f;
        D.sat_n = 1.f / lab::soft_clip(D.g);
        D.fold = cl(P_FOLD, p[P_FOLD] + (mg > 0.f ? 30.f * mg : 0.f)) * 0.01f;
        D.fold_g = 1.f + 3.f * D.fold;
#if LITE_O1
        const float key[6] = {D.tube, D.bias, D.g, D.fold, D.fold_g, D.sat};
        if (std::memcmp(key, g_tab_key, sizeof key) != 0) {
            std::memcpy(g_tab_key, key, sizeof key);
            for (int k = 0; k <= kTabN; k++) g_tab[k] = shape_direct(-kTabR + 2.f * kTabR * static_cast<float>(k) / kTabN);
        }
#endif

        // the compressor: each amount sets its band's threshold (0 .. -36 dB) and ratio (1 .. 8); the makeup gives back 40 % of what the
        // threshold takes, so the tail comes up as the hit goes down
        const float amounts[3] = {cl(P_CLO, p[P_CLO] + 30.f * mb) * 0.01f, p[P_CMID] * 0.01f, p[P_CHI] * 0.01f};
        for (int b = 0; b < 3; b++) {
            D.thr[b] = -36.f * amounts[b];
            D.slope[b] = 1.f - 1.f / (1.f + 7.f * amounts[b]);
            D.makeup[b] = -D.thr[b] * D.slope[b] * 0.4f;
        }
        D.kat = 1.f - lab::decay_coef(cl(P_CATK, p[P_CATK] * lab::fast_exp2(1.5f * mp)), sr);
        D.krel = 1.f - lab::decay_coef(p[P_CREL], sr);

        // the EQ: a band at 0 dB is skipped (its state cleared when it comes back)
        const float eq_db[3] = {cl(P_EQLO, p[P_EQLO] + 6.f * mb), cl(P_EQMID, p[P_EQMID] - 2.f * mt), cl(P_EQHI, p[P_EQHI] + 6.f * mt)};
        for (int b = 0; b < 3; b++) {
            const bool on = eq_db[b] > 0.05f || eq_db[b] < -0.05f;
            if (on && !eq_on[b]) eq[b].z1 = eq[b].z2 = 0.f;
            eq_on[b] = on;
        }
        if (eq_on[0]) eq[0].set_low_shelf(90.f, eq_db[0], sr);
        if (eq_on[1]) eq[1].set_peak(p[P_EQMHZ], 0.9f, eq_db[1], sr);
        if (eq_on[2]) eq[2].set_high_shelf(5000.f, eq_db[2], sr);
        D.level = lab::fast_db(p[P_LEVEL]);

        click_bp.set(3200.f, 0.3f, sr);
        split_lo.set(120.f, 0.f, sr);
        split_hi.set(2500.f, 0.f, sr);
    }
    // The distortions, sample by sample: what the table (O1) holds.
    float shape_direct(float y) const {
        const Derived &D = d;
        if (D.tube > 0.f) y += ((lab::soft_clip(y * D.g + D.bias) - D.bias_y) * D.tube_n - y) * D.tube;
        if (D.fold > 0.f) y += (lab::sin01(0.25f * y * D.fold_g) - y) * D.fold;
        if (D.sat > 0.f) y += (lab::soft_clip(y * D.g) * D.sat_n - y) * D.sat;
        return y;
    }
    static float shape_table(float y) {
        const float u = (y + kTabR) * (kTabN / (2.f * kTabR));
        if (u <= 0.f) return g_tab[0] + u * (g_tab[1] - g_tab[0]);
        if (u >= static_cast<float>(kTabN)) return g_tab[kTabN] + (u - kTabN) * (g_tab[kTabN] - g_tab[kTabN - 1]);
        const int i = static_cast<int>(u);
        return g_tab[i] + (g_tab[i + 1] - g_tab[i]) * (u - static_cast<float>(i));
    }

    void refresh(const float *p, float sr) {        // update() when a value or the sample rate changed
        if (sr == sr_ && std::memcmp(p, pc, sizeof pc) == 0) return;
        std::memcpy(pc, p, sizeof pc);
        sr_ = sr;
        full_values(p, f);
        update(f, sr);
    }

    // the three envelopes at tt (samples since the hit), as the phase step, the harmonics and the level
    float step_at(float tt) const {
        const float pe = lab::fast_exp2(d.pk * tt);
        return lab::clampf(base * lab::fast_exp2(d.pamt12 * pe), 5.f, sr_ * 0.45f) * d.inv_sr;
    }
    float harm_at(float tt) const { return lab::clampf(d.harm + d.hamt * lab::fast_exp2(d.hk * tt), 0.f, 1.f); }
    float amp_at(float tt) const {
        if (tt < d.attack) return tt * d.inv_attack;
        tt -= d.attack;
        if (tt < d.hold) return 1.f;
        const float u = (tt - d.hold) * d.inv_decay;
        return u >= 1.f ? 0.f : lab::fast_exp2(d.acurve * lab::fast_log2(1.f - u));      // (1 - u)^curve
    }
    float comp_gain(int b) const {
        const float over = lab::fast_to_db(det[b] + 1e-6f) - d.thr[b];
        return lab::fast_db(d.makeup[b] - (over > 0.f ? over * d.slope[b] : 0.f));
    }

    // The rumble, one tick (kDecim samples, 6 kHz at 48 kHz): noise + kick -> Pre -> low pass -> Post -> decay -> reverb -> echo -> duck.
    // The whole chain runs at that rate: its low pass stops at 500 Hz, far under the tick rate's 3 kHz (and a noisy rumble is no loss).
    // kick: the oscillator averaged over the tick, times the velocity; a: the body's level (the duck's key).
    float rumble_tick(bool noise_on, float nlevel, float a) {
        const Derived &D = d;
        float r = 0.f;
        if (noise_on) {
            float src = noise.next();
            if (D.npre > 0.f) src += (lab::soft_clip(src * D.npre_g) * D.npre_n - src) * D.npre;
            r = noise_lp.run(src) * D.nmakeup;
            if (D.npost > 0.f) r += (lab::soft_clip(r * D.npost_g) * D.npost_n - r) * D.npost;
            r *= nenv * nlevel;
            nenv *= D.knoise;
        }
        {                                                                   // the echo: a 16-bit line, one tick a step
            int rd = dw - D.dlen;
            if (rd < 0) rd += kDlyMax;
            const float back = static_cast<float>(dline[rd]) * (1.f / 8192.f);     // (stored at a quarter scale: headroom)
            const float v = (r + D.dfb * back) * 8192.f;
            dline[dw] = static_cast<int16_t>(v > 32767.f ? 32767.f : (v < -32767.f ? -32767.f : v));
            if (++dw >= kDlyMax) dw = 0;
            r += (back - r) * D.dmix;
        }
        {                                                                   // ducked under the body: its level (+12 dB), followed
            const float key = a * 4.f < 1.f ? a * 4.f : 1.f;
            duckf = key > duckf ? key : duckf * D.kduck;
            r *= 1.f - D.duck * duckf;
        }
        return r;
    }

    void start(const lab::Note &n, const float *p) {
        refresh(p, n.sr);
        residual = last_src;
        ph = 0.f;
        t = 0.f;
        vel = 0.35f + 0.65f * n.velocity;
        base = f[P_TUNE] * n.hz * (1.f / 130.8128f);                // C3 plays the tune
        // the noise and the click start from the same seed at every hit, their filters from rest: the same waveform each time
        const uint32_t seed = static_cast<uint32_t>(f[P_NSEED]) * 2654435761u + 0x9E3779B9u;
        noise.s = seed;
        click_noise.s = seed ^ 0x5BD1E995u;
        noise_lp.ic1 = noise_lp.ic2 = click_bp.ic1 = click_bp.ic2 = 0.f;
        click = 1.f;
        nenv = 1.f;
        rphase = 0;
        dt_ = step_at(0.f);
        h_ = harm_at(0.f);
        a_ = amp_at(0.f);
    }
    void release(const lab::Note &, const float *) {}       // a one-shot: the key's length does not matter

    bool render(float *out, int frames, const lab::Note &n, const float *p) {
        refresh(p, n.sr);
        const Derived &D = d;
        base = f[P_TUNE] * n.hz * (1.f / 130.8128f);
        const float t1 = t + static_cast<float>(frames), inv_n = frames == 32 ? 1.f / 32.f : 1.f / static_cast<float>(frames);   // (no division: blocks are 32)
        float dt = dt_, h = h_, a = a_;                             // the block's start: the last one's end
        dt_ = step_at(t1);
        h_ = harm_at(t1);
        a_ = amp_at(t1);
        const float ddt = (dt_ - dt) * inv_n, dh = (h_ - h) * inv_n, da = (a_ - a) * inv_n;
        float dg[3];
        for (int b = 0; b < 3; b++) dg[b] = (comp_gain(b) - gain[b]) * inv_n;
        const bool click_on = click > 1e-4f && D.click > 0.f, noise_on = nenv > 1e-4f && D.nlevel > 0.f;
        // (the rumble's echo runs on: the rumble ticks as long as the voice lives)
        const float nlevel = D.nlevel * vel;
#if LITE_O4
        const bool tri_on = D.tri_on && (h > 1e-4f || h_ > 1e-4f);         // (gone: the harmonics' envelope has died, no Harmonic)
#else
        const bool tri_on = D.tri_on;
#endif

        for (int i = 0; i < frames; i++) {
            // the oscillator: sine -> triangle (harmonics), then -> distorted sine (shape)
            const float s = lab::sin01(ph);
            float x = s;
            if (tri_on) {
                const float tri = ph < 0.25f ? 4.f * ph : (ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f);
                x += (tri - s) * h;
                h += dh;
            }
            ph += dt;
            if (ph >= 1.f) ph -= 1.f;
            dt += ddt;
            if (D.shape > 0.f) x += (lab::soft_clip(x * D.shape_g) * D.shape_n - x) * D.shape;
            if (click_on) {
                click_bp.run(click_noise.next());
                x += click_bp.bp * click * D.click;
                click *= D.kclick;
            }
            float y = x * a * vel + residual;
            a += da;
            if (++rphase >= kDecim) {                                       // the rumble, at 1 / kDecim of the rate (rumble_tick)
                rphase = 0;
                rprev = rcur;
                rcur = rumble_tick(noise_on, nlevel, a);
            }
            y += rprev + (rcur - rprev) * static_cast<float>(rphase) * (1.f / kDecim);
            residual *= D.kres;
            last_src = y;

            // the light distortions, each mixed in by its amount
#if LITE_O1
            y = shape_table(y);
#else
            y = shape_direct(y);
#endif
            y = dc.run(y);

            // the 3-band compressor
            {
                const float lo = split_lo.run(y);
                split_hi.run(y);
                const float hi = split_hi.hp, band[3] = {lo, y - lo - hi, hi};
                y = 0.f;
                for (int b = 0; b < 3; b++) {
                    const float e = std::fabs(band[b]);
                    det[b] += (e - det[b]) * (e > det[b] ? D.kat : D.krel);
                    y += band[b] * gain[b];
                    gain[b] += dg[b];
                }
            }

            // the EQ, the level, and a soft limit
            if (eq_on[0]) y = eq[0].run(y);
            if (eq_on[1]) y = eq[1].run(y);
            if (eq_on[2]) y = eq[2].run(y);
            out[i] += lab::soft_clip(y * D.level) * 0.6f;
        }
        t = t1;
        // the states that decay to nothing: kept out of the denormals (lab_dsp.h, flush)
        lab::flush(residual);
        lab::flush(duckf);
        lab::flush(rprev);
        lab::flush(rcur);
        dc.flush();
        click_bp.flush();
        noise_lp.flush();
        split_lo.flush();
        split_hi.flush();
        for (int b = 0; b < 3; b++) { eq[b].flush(); lab::flush(det[b]); }
        return t < D.end;
    }
};

// What runs once on the sum of the voices (lab.h, Fx): the reverb, then the master shaper (in Kick Lite: Route Output, Glue 30 %, Sat 20 %,
// no Tilt, Output 0 dB, fixed).
//
// The reverb (Route = Output): the Room, at half the sample rate (each pair of samples is averaged in, the output is drawn in a line
// between two ticks). Route = Rumble: it does not run (the voice has its own, in the rumble's chain).
//
// The master shaper: a glue compressor (a peak detector every sample, the gain computed every 8 samples with a soft knee and followed
// in a line) and a soft saturation.
struct Fx {
    Room<1201, 1427, 1709, 2003> room;                              // at half rate (24 kHz): 50 .. 83 ms; Size shortens them
    float prev = 0.f;
    float pc[kick_lite::kValues] = {}, sr_ = 0.f;                   // the values the coefficients were computed for
    float f[N_PARAMS] = {};
    bool fresh = true;
    float mix = 0.f;
    // the master shaper
    static constexpr int kChunk = 8;                                // the glue's gain is computed every 8 samples
    float det = 0.f, gain = 1.f;
    float thr = 0.f, slope = 0.f, makeup = 0.f, kat = 0.f, krel = 0.f;
    float sat = 0.f, sat_g = 1.f, sat_n = 1.f;

    LAB_COLD void update(const float *p, float sr) {
        mix = p[P_RMIX] * 0.01f;                                       // (Route: Output, fixed)
        room.set(p[P_RSIZE] * 0.01f, p[P_RDECAY], p[P_RDAMP] * 0.01f, sr * 0.5f);

        const float glue = p[P_MGLUE] * 0.01f;                         // (Glue 30 %, Sat 20 %, no Tilt, Output 0 dB: fixed)
        thr = -30.f * glue;
        slope = 1.f - 1.f / (1.f + 3.f * glue);
        makeup = -thr * slope * 0.25f;              // (a quarter: the hit passes the 10 ms attack with it, more would clip)
        kat = 1.f - lab::decay_coef(p[P_MATK], sr);
        krel = 1.f - lab::decay_coef(p[P_MREL], sr);
        sat = p[P_MSAT] * 0.01f;
        sat_g = 1.f + 7.f * sat;                    // soft_clip(x g) / sqrt(g): the body up, the peaks held near the bus's level
        sat_n = 1.f / sqrtf(sat_g);

        sr_ = sr;
        fresh = false;
    }
    // The glue compressor's gain for the level detected: a 6 dB soft knee around the threshold, then the ratio; plus the makeup.
    float glue_gain() const {
        const float over = lab::fast_to_db(det + 1e-6f) - thr;
        const float gr = over <= -3.f ? 0.f : (over >= 3.f ? over * slope : slope * (over + 3.f) * (over + 3.f) * (1.f / 12.f));
        return lab::fast_db(makeup - gr);
    }
    void render(float *io, int frames, float sr, const float *p) {
        if (fresh || sr != sr_ || std::memcmp(p, pc, sizeof pc) != 0) {
            std::memcpy(pc, p, sizeof pc);
            full_values(p, f);
            update(f, sr);
        }
        if (mix > 0.f) {                            // the reverb (at 0 % its lines keep what they hold)
            for (int i = 0; i + 1 < frames; i += 2) {
                const float wet = room.tick((io[i] + io[i + 1]) * 0.5f);
                io[i] += (prev + wet) * 0.5f * mix;
                io[i + 1] += wet * mix;
                prev = wet;
            }
            room.flush();
            lab::flush(prev);
        }
        // the master shaper: glue -> saturation, by chunks of 8 samples: the glue's detector runs over the chunk first,
        // then its gain goes in a line to what the chunk's level asks (a lag of 8 samples, 0.17 ms: a fast attack catches the hit; the
        // gain computed once a block came a block late, and the hit passed with the makeup)
        for (int c = 0; c < frames; c += kChunk) {
            const int m = frames - c < kChunk ? frames - c : kChunk;
            float g = gain;
            for (int i = c; i < c + m; i++) {
                const float e = std::fabs(io[i]);
                det += (e - det) * (e > det ? kat : krel);
            }
            gain = glue_gain();
            const float dg = (gain - g) * (m == kChunk ? 1.f / kChunk : 1.f / static_cast<float>(m));
            for (int i = c; i < c + m; i++) {
                g += dg;
                const float x = io[i] * g;
                io[i] = x + (lab::soft_clip(x * sat_g) * sat_n - x) * sat;
            }
        }
        lab::flush(det);
    }
};

}  // namespace

namespace kick_lite {
const char *const kKickRows[kValues] = {"OSC/Tune", "MACROS/Punch", "MACROS/Body", "MACROS/Grit", "MACROS/Tone", "OSC/Click",
                                        "RUMBLE/Level", "REVERB/Mix"};
//                                Tune Punch Body Grit Tone Click Rumble Room
const Preset kPresets[] = {
    {"Default",                  {50,  50,   50,  50,  50,  25,   20,    15}},
    {"Techno Rumble",            {48,  60,   65,  60,  40,  30,   70,    30}},
    {"Hard Punch",               {55,  85,   35,  80,  70,  60,   10,    10}},
    {"Deep Sub",                 {40,  30,   90,  20,  20,  10,   30,    5}},
    {"Gritty",                   {52,  70,   50,  100, 60,  45,   40,    20}},
    {"Clean Short",              {60,  50,   10,  0,   50,  15,   0,     0}},
};
const int kPresetCount = static_cast<int>(sizeof kPresets / sizeof kPresets[0]);
}  // namespace kick_lite

extern const lab::Synth synth_kick_lite = lab::make_synth<Voice, Fx>("Kick Lite", kParams, {true, false, 0, 135});
