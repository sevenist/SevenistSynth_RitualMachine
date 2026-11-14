// DrumLab: one drum voice for every kind of percussion, the Kick's and the Snare's modules merged into one long chain. Four sources:
// the BODY (the Kick's oscillator and the Snare's skin: a sine morphing to a triangle and to a distorted sine, with a pitch envelope Lin
// or Exp, a harmonics envelope and an amplitude envelope), the RING (the Snare's steady sine at a ratio of the body), the NOISE (the
// Snare's wires and the Kick's click: noise with a ratchet envelope, a high pass, a bell, a low pass) and the METAL (new: six square
// oscillators at the classic hat's inharmonic ratios, or a two-operator FM, through a band pass of two stages); then the Kick's RUMBLE under them, its
// DRIVE (tube, fold, sat, clip), its 3-BAND COMP, an EQ (the Snare's, with frequencies), the level; on the sum of the voices the REVERB
// (on the output, or inside the rumble), the Snare's GATE, the Kick's MASTER. PLAY loops a GROOVE (lab.h Options::grooves) at its Tempo.
// The defaults are the Kick's sound (the noise as its click). docs/SYNTHS.md, "DrumLab".
//
// Cost, as the Kick's: no division and no libm call in the sample loop; what the values decide is computed when one changes (update());
// the body's envelopes once a block, in lines between; each other envelope a multiply a sample; the parts at zero skipped; the rumble at
// an eighth of the sample rate, the output's reverb at half.
#include <cstring>
#include "lab.h"
#include "lab_dsp.h"

namespace {

enum : int {
    H_GROOVE, P_GROOVE, P_TEMPO, P_SWING,
    H_BODY, P_TUNE, P_BLEVEL, P_SHAPE, P_HARM,
    H_PITCH, P_PAMT, P_PDECAY, P_PCURVE,
    H_HENV, P_HAMT, P_HDECAY,
    H_AMP, P_ATTACK, P_HOLD, P_DECAY, P_CURVE,
    H_RING, P_RRATIO, P_RLEVEL, P_RDECAY,
    H_NOISE, P_NLEVEL, P_RATCH, P_SPACE, P_ACCEL, P_BURST, P_NDECAY, P_SEED,
    H_NEQ, P_NHP, P_NMID, P_NMIDHZ, P_NLP,
    H_METAL, P_XTYPE, P_XLEVEL, P_XTUNE, P_XRATIO, P_XINDEX, P_XTONE, P_XDECAY,
    H_RUMBLE, P_ULEVEL, P_UMIX, P_UDECAY, P_UPRE, P_UTYPE, P_UCUT, P_URES, P_UFENV, P_UFATK, P_UFDEC, P_UPOST, P_UDLY, P_UFB, P_UDMIX,
    P_UDUCK, P_UDREL,
    H_DRIVE, P_DRIVE, P_TUBE, P_FOLD, P_SAT, P_CLIP,
    H_COMP, P_CLO, P_CMID, P_CHI, P_CATK, P_CREL,
    H_EQ, P_EQLO, P_EQLOHZ, P_EQMID, P_EQMIDHZ, P_EQHI, P_EQHIHZ,
    H_OUT, P_LEVEL,
    H_VERB, P_VROUTE, P_VMIX, P_VDECAY, P_VSIZE, P_VDAMP,
    H_GATE, P_GDEPTH, P_GKEY, P_GTHR, P_GATK, P_GHOLD, P_GREL,
    H_MASTER, P_MGLUE, P_MATK, P_MREL, P_MSAT, P_MTILT, P_MOUT,
    N_PARAMS
};

constexpr int kMaxRatchets = 8;
constexpr float kNoiseGain = 0.8f;                      // the noise at Level 100 %, over its whole band (white noise: RMS 0.58)

const char *const kGrooveNames[] = {"Four", "Backbeat", "Offbeat", "16ths", "Broken", "Roll", "Fill"};
const char *const kCurves[] = {"Lin", "Exp"};
const char *const kMetals[] = {"Squares", "FM"};
const char *const kFilters[] = {"LP 6", "LP 12", "LP 24", "BP", "HP 12", "HP 24", "Notch", "Peak"};
const char *const kRoutes[] = {"Output", "Rumble"};
const char *const kKeys[] = {"Dry", "Out"};

const lab::Param kParams[] = {
    lab::section("GROOVE"),                             // what PLAY loops, on the note last played (a key changes it)
    {"Groove",   0,    6,     0,     nullptr, lab::Int, kGrooveNames},
    {"Tempo",    60,   200,   130,   "BPM", lab::Int},
    {"Swing",    0,    75,    0,     "%"},              // the off steps late by this much of a step
    lab::section("BODY"),                               // the Kick's oscillator, the Snare's skin
    {"Tune",     20,   1000,  50,    "Hz", lab::Exp},   // the end of the sweep, on C3; the keys transpose it (and the ring, the metal)
    {"Level",    0,    100,   100,   "%"},
    {"Shape",    0,    100,   15,    "%"},              // sine -> distorted sine (the body and the ring)
    {"Harmonic", 0,    100,   0,     "%"},              // sine -> triangle
    lab::section("PITCH ENV"),
    {"Amount",   0,    60,    30,    "st", lab::Int},   // how far above the tune the hit starts
    {"Decay",    2,    1000,  45,    "ms", lab::Exp},   // Lin: back to the tune in that time; Exp: 99 % of the way
    {"Curve",    0,    1,     1,     nullptr, lab::Int, kCurves},
    lab::section("HARM ENV"),
    {"Amount",   0,    100,   40,    "%"},              // added to Harmonic at the hit
    {"Decay",    2,    2000,  25,    "ms", lab::Exp},
    lab::section("AMP ENV"),                            // the body's
    {"Attack",   0.1f, 50,    0.5f,  "ms", lab::Exp},
    {"Hold",     0,    500,   30,    "ms"},
    {"Decay",    10,   4000,  350,   "ms", lab::Exp},
    {"Curve",    0,    100,   60,    "%"},              // 0 = a straight line down, 100 = a fast drop and a long tail
    lab::section("RING"),                               // a steady sine (no sweep): a drum's resonance, a tom's second mode
    {"Ratio",    0.5f, 4,     1.59f, "x",  lab::Exp},   // its pitch over the tune (1.59: a drum head's second mode)
    {"Level",    0,    100,   0,     "%"},
    {"Decay",    10,   2000,  220,   "ms", lab::Exp},   // to 1 %
    lab::section("NOISE"),                              // the wires, the click: noise with a ratchet envelope (by default: the Kick's click)
    {"Level",    0,    100,   25,    "%"},
    {"Ratchets", 1,    kMaxRatchets, 1, nullptr, lab::Int},    // bursts at the hit (1: one burst); the last one is the tail
    {"Spacing",  1,    50,    8,     "ms", lab::Exp},   // from one burst to the next
    {"Accel",    -100, 100,   0,     "%"},              // each gap shorter (-) or longer (+) than the one before: x0.5 .. x2
    {"Burst",    1,    100,   15,    "ms", lab::Exp},   // each burst's decay (to 1 %), until the next one
    {"Decay",    1,    2000,  18,    "ms", lab::Exp},   // the last burst's (to 1 %): the tail
    {"Seed",     0,    99,    1,     nullptr, lab::Int},   // the noise and the rumble's: 0, another at every hit; 1..99, the same every hit
    lab::section("NOISE EQ"),                           // on the noise alone; its level kept as HP and LP move
    {"HP",       20,   8000,  1500,  "Hz", lab::Exp},   // a high pass, 12 dB / octave (20 Hz: off)
    {"Mid",      -24,  12,    6,     "dB", lab::Int},   // a bell (Q 0.9)
    {"Mid Hz",   200,  12000, 3200,  "Hz", lab::Exp},
    {"LP",       1000, 20000, 6000,  "Hz", lab::Exp},   // a low pass, 12 dB / octave (20 kHz: off)
    lab::section("METAL"),                              // hats, cymbals, cowbells, bells: through a band pass
    {"Type",     0,    1,     0,     nullptr, lab::Int, kMetals},  // Squares: six square waves at the classic hat's ratios; FM: a sine
                                                        //   phase-modulated by another
    {"Level",    0,    100,   0,     "%"},
    {"Tune",     50,   4000,  205,   "Hz", lab::Exp},   // on C3: Squares, the lowest of the six; FM, the carrier
    {"Ratio",    0.25f, 4,    1,     "x",  lab::Exp},   // Squares: their spread (1: the classic ratios); FM: the modulator over the carrier
    {"Index",    0,    100,   30,    "%"},              // Squares: the pairs ring-modulated (sum -> products); FM: the depth (0..8 rad),
                                                        //   falling with the decay
    {"Tone",     500,  16000, 8000,  "Hz", lab::Exp},   // the band pass's centre
    {"Decay",    5,    2000,  80,    "ms", lab::Exp},   // to 1 %
    lab::section("RUMBLE"),                             // the Kick's: noise + body -> Pre -> filter -> Post -> decay -> echo -> duck
    {"Level",    0,    100,   20,    "%"},
    {"In Mix",   0,    100,   0,     "%"},              // what feeds it: 0 = the noise, 50 = both full, 100 = the body's oscillator
    {"Decay",    5,    4000,  300,   "ms", lab::Exp},   // to 1 %
    {"Pre",      0,    100,   30,    "%"},              // distortion before the filter
    {"Filter",   0,    7,     1,     nullptr, lab::Int, kFilters},
    {"Cutoff",   20,   500,   90,    "Hz", lab::Exp},   // its level kept as it moves
    {"Reso",     0,    100,   40,    "%"},              // Q 0.5 .. 10 (not in LP 6)
    {"Env Amt",  -5,   5,     0,     "oct"},            // the filter envelope: the cutoff moved this far at its top
    {"Env Atk",  0,    500,   0,     "ms"},
    {"Env Dec",  5,    4000,  150,   "ms", lab::Exp},
    {"Post",     0,    100,   40,    "%"},              // distortion after the filter
    {"Delay",    10,   250,   80,    "ms", lab::Exp},   // an echo of the rumble
    {"Feedbk",   0,    90,    35,    "%"},
    {"Dly Mix",  0,    100,   30,    "%"},
    {"Duck",     0,    100,   50,    "%"},              // the rumble under the body (keyed by the amp env, +12 dB)
    {"Duck Rel", 10,   1000,  80,    "ms", lab::Exp},
    lab::section("DRIVE"),                              // on the sum of the sources
    {"Drive",    0,    24,    6,     "dB", lab::Int},   // how hard Tube and Sat are hit
    {"Tube",     0,    100,   25,    "%"},              // asymmetric: even harmonics, warmth
    {"Fold",     0,    100,   0,     "%"},              // a sine wavefolder: the peaks fold back, metallic
    {"Sat",      0,    100,   35,    "%"},              // symmetric soft saturation: odd harmonics, loudness
    {"Clip",     0,    100,   0,     "%"},              // hard clip, the threshold lowered as it rises
    lab::section("3-BAND COMP"),                        // split at 120 Hz and 2.5 kHz; each amount lowers the threshold and raises the ratio
    {"Low",      0,    100,   40,    "%"},
    {"Mid",      0,    100,   25,    "%"},
    {"High",     0,    100,   30,    "%"},
    {"Attack",   0.1f, 50,    6,     "ms", lab::Exp},
    {"Release",  10,   1000,  120,   "ms", lab::Exp},
    lab::section("EQ"),
    {"Low",      -12,  12,    0,     "dB", lab::Int},   // a low shelf
    {"Low Hz",   40,   1000,  90,    "Hz", lab::Exp},
    {"Mid",      -12,  12,    0,     "dB", lab::Int},   // a bell (Q 0.9)
    {"Mid Hz",   100,  8000,  600,   "Hz", lab::Exp},
    {"High",     -12,  12,    0,     "dB", lab::Int},   // a high shelf
    {"High Hz",  1000, 16000, 5000,  "Hz", lab::Exp},
    lab::section("OUT"),
    {"Level",    -30,  6,     0,     "dB", lab::Int},   // before the soft limit
    lab::section("REVERB"),
    {"Route",    0,    1,     0,     nullptr, lab::Int, kRoutes},  // Output: on the sum of the voices; Rumble: inside the rumble's chain
    {"Mix",      0,    100,   15,    "%"},              // Output: the wet added; Rumble: dry -> wet
    {"Decay",    100,  4000,  500,   "ms", lab::Exp},   // to -60 dB
    {"Size",     10,   100,   60,    "%"},              // the lines' length: 5 to 83 ms
    {"Damp",     0,    100,   50,    "%"},              // a low pass in the loop
    lab::section("GATE"),                               // after the reverb (0 %: off)
    {"Depth",    0,    100,   0,     "%"},              // how far it closes (100: to silence)
    {"Key",      0,    1,     0,     nullptr, lab::Int, kKeys},  // Dry: the voices before the reverb (a gated reverb); Out: after it
    {"Thresh",   -60,  0,     -40,   "dB", lab::Int},   // it opens above (closes 3 dB under)
    {"Attack",   0.1f, 20,    0.3f,  "ms", lab::Exp},
    {"Hold",     0,    1000,  80,    "ms"},
    {"Release",  5,    1000,  50,    "ms", lab::Exp},   // 99 % of the way
    lab::section("MASTER"),                             // the Kick's, last: glue -> saturation -> tilt -> output
    {"Glue",     0,    100,   30,    "%"},              // a bus compressor: threshold 0 .. -30 dB, ratio 1 .. 4, soft knee, a quarter back
    {"Attack",   0.1f, 30,    3,     "ms", lab::Exp},   // fast: glue; slow: punch (the hit passes)
    {"Release",  30,   1000,  150,   "ms", lab::Exp},
    {"Sat",      0,    100,   20,    "%"},              // a soft saturation: denser and louder, the peaks rounded
    {"Tilt",     -100, 100,   0,     "%"},              // around 800 Hz: - darker, + brighter
    {"Output",   -12,  6,     0,     "dB", lab::Int},
};
static_assert(sizeof kParams / sizeof kParams[0] == N_PARAMS, "the enum follows the list");
static_assert(N_PARAMS <= lab::kMaxParams, "within the list's rows");

// The grooves (GROOVE / Groove): velocities a step (0: a rest), sixteenths unless said
constexpr uint8_t kFour[16] = {120, 0, 0, 0, 100, 0, 0, 0, 110, 0, 0, 0, 100, 0, 0, 0};          // every beat, the one accented
constexpr uint8_t kBack[16] = {0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 115, 0, 40, 0};            // 2 and 4, a ghost before the one
constexpr uint8_t kOff[16] = {0, 0, 110, 0, 0, 0, 100, 0, 0, 0, 110, 0, 0, 0, 100, 0};          // the offbeats
constexpr uint8_t k16th[16] = {110, 45, 70, 45, 95, 45, 70, 45, 105, 45, 70, 45, 95, 45, 70, 60};   // every 16th, accents on the beats
constexpr uint8_t kBroken[16] = {120, 0, 0, 85, 0, 0, 100, 0, 0, 70, 0, 110, 0, 0, 90, 0};      // a broken beat
struct RollSteps {                                      // 2 bars of 32nds: quarters, eighths, 16ths, 32nds, the velocity rising
    uint8_t v[64] = {};
    constexpr RollSteps() {
        for (int k = 0; k < 64; k++) {
            const int every = k < 16 ? 8 : (k < 32 ? 4 : (k < 48 ? 2 : 1));
            if (k % every == 0) v[k] = static_cast<uint8_t>(40 + 87 * k / 63);
        }
    }
};
constexpr RollSteps kRoll{};
constexpr uint8_t kFill[16] = {95, 70, 85, 70, 100, 75, 90, 75, 105, 80, 95, 80, 115, 90, 105, 127};   // a tom fill: 16ths falling
constexpr int8_t kFillSemi[16] = {12, 12, 10, 10, 7, 7, 5, 5, 3, 3, 0, 0, -2, -2, -5, -5};
constexpr lab::Groove kGrooves[] = {
    {16, 4, kFour}, {16, 4, kBack}, {16, 4, kOff}, {16, 4, k16th}, {16, 4, kBroken}, {64, 8, kRoll.v}, {16, 4, kFill, kFillSemi},
};
static_assert(sizeof kGrooves / sizeof kGrooves[0] == sizeof kGrooveNames / sizeof kGrooveNames[0], "a name for each groove");

constexpr lab::Options drum_options() {
    lab::Options o;
    o.mono = true;                                      // a new hit chokes the last (the old level fades in 1 ms)
    o.play_bpm = 130;                                   // (PLAY loops: the tempo is the Tempo row's)
    o.grooves = kGrooves;
    o.n_grooves = static_cast<int>(sizeof kGrooves / sizeof kGrooves[0]);
    o.groove_param = P_GROOVE;
    o.tempo_param = P_TEMPO;
    o.swing_param = P_SWING;
    return o;
}

// The reverb, the Kick's (kick.cpp, Room): four delay lines fed back through a 4 x 4 Hadamard matrix (a feedback delay network), a
// one-pole low pass in each loop, 16-bit lines. Two copies: the output's (Fx, half rate) and the rumble's (Voice, an eighth).
template <int L0, int L1, int L2, int L3>
struct Room {
    static constexpr int kMax[4] = {L0, L1, L2, L3};
    int16_t l0[L0] = {}, l1[L1] = {}, l2[L2] = {}, l3[L3] = {};
    int w[4] = {}, len[4] = {L0, L1, L2, L3};
    float lp[4] = {}, fb = 0.f, damp = 1.f;

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
    static float rd(const int16_t *line, int w, int len, int max) {
        int r = w - len;
        if (r < 0) r += max;
        return static_cast<float>(line[r]) * (1.f / 16384.f);
    }
    static void wr(int16_t *line, int &w, int max, float v) {
        const float x = v * 16384.f;
        line[w] = static_cast<int16_t>(x > 32767.f ? 32767.f : (x < -32767.f ? -32767.f : x));
        if (++w >= max) w = 0;
    }
    float tick(float in) {
        const float r0 = rd(l0, w[0], len[0], L0), r1 = rd(l1, w[1], len[1], L1);
        const float r2 = rd(l2, w[2], len[2], L2), r3 = rd(l3, w[3], len[3], L3);
        const float a = r0 + r1, b = r0 - r1, c = r2 + r3, e = r2 - r3;
        const float m[4] = {(a + c) * 0.5f, (b + e) * 0.5f, (a - c) * 0.5f, (b - e) * 0.5f};
        for (int k = 0; k < 4; k++) lp[k] += (m[k] - lp[k]) * damp;
        wr(l0, w[0], L0, in + fb * lp[0]);
        wr(l1, w[1], L1, -in + fb * lp[1]);
        wr(l2, w[2], L2, in + fb * lp[2]);
        wr(l3, w[3], L3, -in + fb * lp[3]);
        return (r0 - r1 + r2 - r3) * 0.35f;
    }
    void flush() { for (float &x : lp) lab::flush(x); }
};

// The noise's EQ (the Snare's): a high pass, a bell, a low pass; each skipped when it does nothing, its memory cleared when it comes back.
struct NoiseEq {
    lab::Biquad b[3];
    bool on[3] = {};
    void set(float hp_hz, float mid_db, float mid_hz, float lp_hz, float sr) {
        const bool o[3] = {hp_hz > kParams[P_NHP].min + 0.5f, mid_db > 0.05f || mid_db < -0.05f, lp_hz < kParams[P_NLP].max - 0.5f};
        for (int k = 0; k < 3; k++) {
            if (o[k] && !on[k]) b[k].z1 = b[k].z2 = 0.f;
            on[k] = o[k];
        }
        if (on[0]) b[0].set_highpass(hp_hz, 0.70711f, sr);
        if (on[1]) b[1].set_peak(mid_hz, 0.9f, mid_db, sr);
        if (on[2]) b[2].set_lowpass(lp_hz, 0.70711f, sr);
    }
    float run(float x) {
        if (on[0]) x = b[0].run(x);
        if (on[1]) x = b[1].run(x);
        if (on[2]) x = b[2].run(x);
        return x;
    }
    void reset() { for (lab::Biquad &q : b) q.z1 = q.z2 = 0.f; }
    void flush() { for (lab::Biquad &q : b) q.flush(); }
};

// The six squares' ratios: the classic analog hat's oscillators (205.3, 304.4, 369.6, 522.7, 540, 800 Hz) over the lowest
constexpr float kMetalGain = 3.5f;                      // the metal at Level 100 %, after its band pass (a hat about as loud as the noise
                                                        //   at 70 %: the band pass keeps a small part of the squares' energy)
constexpr float kMetalPhases[6] = {0.f, 0.618f, 0.236f, 0.854f, 0.472f, 0.09f};    // where they start (k x the golden ratio)
constexpr float kMetalRatios[6] = {1.f, 1.4827f, 1.8003f, 2.5461f, 2.6303f, 3.8967f};

// What the values decide, for the sample loop: Voice::update() computes it when a value (or the sample rate) changes.
struct Derived {
    float inv_sr = 1.f / 48000.f, ms = 48.f;
    // the body
    float blevel = 1.f;
    float pamt12 = 0.f, pk = 0.f, pdec = 1.f, inv_pdec = 1.f;
    bool plin = false;
    float harm = 0.f, hamt = 0.f, hk = 0.f;
    bool tri_on = false;
    float attack = 1.f, inv_attack = 1.f, hold = 0.f, inv_decay = 1.f, acurve = 1.f;
    float shape = 0.f, shape_g = 1.f, shape_n = 1.f;
    // the ring
    float rratio = 1.f, rlevel = 0.f, kr = 0.f;
    // the noise
    float nlevel = 0.f, kburst = 0.f, ktail = 0.f;
    int ratchets = 1, gaps[kMaxRatchets] = {};
    bool seeded = false;
    uint32_t seed = 0;
    // the metal
    bool fm = false;
    float xlevel = 0.f, xratio = 1.f, xindex = 0.f, kx = 0.f;
    // the rumble (the Kick's)
    float ulevel = 0.f, kuenv = 0.f, umakeup = 1.f;
    int utype = 1;
    float ucut = 90.f, ures = 0.f, tr = 6000.f, inv_tr = 1.f;
    bool ufenv_on = false;
    float ufoct = 0.f, ufatt = 0.f, inv_ufatt = 0.f, ufk = 0.f;
    float uin_noise = 1.f, uin_body = 0.f;
    float upre = 0.f, upre_g = 1.f, upre_n = 1.f, upost = 0.f, upost_g = 1.f, upost_n = 1.f;
    float udmix = 0.f, udfb = 0.f;
    float uduck = 0.f, kuduck = 0.f;
    bool urverb = false;
    float urmix = 0.f;
    int udlen = 1;
    // the chain
    float kres = 0.f;
    float g = 1.f, tube = 0.f, bias = 0.f, bias_y = 0.f, tube_n = 1.f, sat = 0.f, sat_n = 1.f, fold = 0.f, fold_g = 1.f;
    float clip = 0.f, clip_t = 1.f, inv_clip_t = 1.f;
    bool comp_on = false, band_on[3] = {};
    float thr[3] = {}, slope[3] = {}, makeup[3] = {}, kat = 0.f, krel = 0.f;
    float level = 1.f;
    float end = 0.f;                                    // samples from the hit to silence
};

struct Voice {
    float pc[N_PARAMS] = {};                        // the values update() last saw
    float sr_ = 0.f;
    Derived d;
    float t = 0.f, vel = 1.f, note_ratio = 1.f;     // samples since the hit; the velocity; the note over C3
    // the body
    float ph = 0.f, base = 50.f, dt_ = 0.f, h_ = 0.f, a_ = 0.f;
    // the ring
    float ph_r = 0.f, er = 0.f;
    // the noise
    lab::Noise noise;
    NoiseEq neq;
    float en = 0.f;
    int burst = 0, left = 0;
    // the metal
    float mph[6] = {}, fph_c = 0.f, fph_m = 0.f, em = 0.f;
    lab::Svf mbp, mbp2;                             // the metal's band pass: two stages (12 dB / octave each side)
    // the rumble (the Kick's): its noise, filter, echo line, reverb, ticks at sr / 8
    lab::Noise rnoise;
    lab::Svf ulp, ulp2;
    float op_s = 0.f, op_g = 0.f, uenv = 0.f;
    static constexpr int kDecim = 8, kDlyMax = 1536;
    int16_t dline[kDlyMax] = {};
    int dw = 0;
    float duckf = 0.f;
    Room<301, 357, 427, 501> rroom;
    int rphase = 0;
    float kacc = 0.f, rprev = 0.f, rcur = 0.f;
    // the chain
    float last_src = 0.f, residual = 0.f;
    lab::DcBlock dc;
    lab::Svf split_lo, split_hi;
    float det[3] = {}, gain[3] = {1.f, 1.f, 1.f};
    lab::Eq3 eq;

    LAB_COLD void update(const float *p, float sr) {
        Derived &D = d;
        const float ms = sr * 0.001f;
        D.inv_sr = 1.f / sr;
        D.ms = ms;

        // the body
        D.blevel = p[P_BLEVEL] * 0.01f;
        const float pdec = p[P_PDECAY] * ms;
        D.pamt12 = p[P_PAMT] * (1.f / 12.f);
        D.plin = p[P_PCURVE] < 0.5f;
        D.pdec = pdec;
        D.inv_pdec = 1.f / pdec;
        D.pk = -6.644f / pdec;                      // 2^(pk t): 1 % at t = pdec
        D.harm = p[P_HARM] * 0.01f;
        D.hamt = p[P_HAMT] * 0.01f;
        D.hk = -6.644f / (p[P_HDECAY] * ms);
        D.tri_on = D.harm > 0.f || D.hamt > 0.f;
        const float decay = p[P_DECAY] * ms;
        D.attack = p[P_ATTACK] * ms;
        D.inv_attack = 1.f / D.attack;
        D.hold = p[P_HOLD] * ms;
        D.inv_decay = 1.f / decay;
        D.acurve = 1.f + 7.f * p[P_CURVE] * 0.01f;
        D.shape = p[P_SHAPE] * 0.01f;
        D.shape_g = 1.f + 6.f * D.shape;
        D.shape_n = 1.f / lab::soft_clip(D.shape_g);

        // the ring
        D.rratio = p[P_RRATIO];
        D.rlevel = p[P_RLEVEL] * 0.01f;
        D.kr = lab::fast_exp2(-6.644f / (p[P_RDECAY] * ms));

        // the noise: its bursts' gaps (each one Accel times the last), its envelope's two speeds, its EQ (its level kept as HP and LP
        // move: white noise's level goes as the square root of its bandwidth; a 12 dB low pass lets 1.11 x its cutoff through)
        D.ratchets = static_cast<int>(p[P_RATCH] + 0.5f);
        if (D.ratchets < 1) D.ratchets = 1;
        if (D.ratchets > kMaxRatchets) D.ratchets = kMaxRatchets;
        const float acc = lab::fast_exp2(p[P_ACCEL] * 0.01f);
        float gap = p[P_SPACE] * ms, bursts_len = 0.f;
        for (int k = 0; k < kMaxRatchets; k++) {
            D.gaps[k] = gap < 1.f ? 1 : static_cast<int>(gap);
            if (k < D.ratchets - 1) bursts_len += static_cast<float>(D.gaps[k]);
            gap *= acc;
        }
        D.kburst = lab::fast_exp2(-6.644f / (p[P_BURST] * ms));
        D.ktail = lab::fast_exp2(-6.644f / (p[P_NDECAY] * ms));
        neq.set(p[P_NHP], p[P_NMID], p[P_NMIDHZ], p[P_NLP], sr);
        float bw = (neq.on[2] && 1.11f * p[P_NLP] < 0.5f * sr ? 1.11f * p[P_NLP] : 0.5f * sr) - (neq.on[0] ? p[P_NHP] : 0.f);
        if (bw < 300.f) bw = 300.f;
        D.nlevel = p[P_NLEVEL] * 0.01f * kNoiseGain * sqrtf(0.5f * sr / bw);
        D.seeded = p[P_SEED] >= 0.5f;
        D.seed = static_cast<uint32_t>(p[P_SEED]) * 2654435761u + 0x9E3779B9u;

        // the metal: its band pass (normalised: 1 at its peak), its decay
        D.fm = p[P_XTYPE] >= 0.5f;
        D.xlevel = p[P_XLEVEL] * 0.01f;
        D.xratio = p[P_XRATIO];
        D.xindex = p[P_XINDEX] * 0.01f;
        D.kx = lab::fast_exp2(-6.644f / (p[P_XDECAY] * ms));
        mbp.set(p[P_XTONE], 0.45f, sr);
        mbp2.set(p[P_XTONE], 0.45f, sr);

        // the rumble (the Kick's, without its macros)
        const float udec = p[P_UDECAY] * ms;
        D.ulevel = p[P_ULEVEL] * 0.01f;
        const float tr = sr / kDecim;
        D.kuenv = lab::fast_exp2(-6.644f * kDecim / udec);
        const float ucut = p[P_UCUT];
        const float uq = 0.5f * lab::fast_exp2(4.3219f * p[P_URES] * 0.01f);
        D.utype = static_cast<int>(p[P_UTYPE] + 0.5f);
        D.ucut = ucut;
        D.ures = (2.f - 1.f / uq) / 1.9f;
        D.tr = tr;
        D.inv_tr = 1.f / tr;
        set_cutoff(ucut, false);
        const float lp12 = sqrtf(0.5f * tr / ucut);
        D.umakeup = D.utype == 0 ? lp12 * 0.84f : D.utype <= 2 ? lp12 : D.utype == 3 ? lp12 / sqrtf(1.571f * ulp.k) : 1.f;
        D.ufoct = p[P_UFENV];
        D.ufenv_on = D.ufoct > 0.005f || D.ufoct < -0.005f;
        D.ufatt = p[P_UFATK] * ms;
        D.inv_ufatt = D.ufatt > 0.f ? 1.f / D.ufatt : 0.f;
        D.ufk = -6.644f / (p[P_UFDEC] * ms);
        const float umix = p[P_UMIX] * 0.01f;
        D.uin_noise = 2.f * (1.f - umix) < 1.f ? 2.f * (1.f - umix) : 1.f;
        D.uin_body = (2.f * umix < 1.f ? 2.f * umix : 1.f) / D.umakeup;
        D.upre = p[P_UPRE] * 0.01f;
        D.upre_g = 1.f + 7.f * D.upre;
        D.upre_n = 1.f / lab::soft_clip(D.upre_g);
        D.upost = p[P_UPOST] * 0.01f;
        D.upost_g = 1.f + 7.f * D.upost;
        D.upost_n = 1.f / lab::soft_clip(D.upost_g);
        D.udmix = p[P_UDMIX] * 0.01f;
        D.uduck = p[P_UDUCK] * 0.01f;
        D.kuduck = lab::decay_coef(p[P_UDREL], tr);
        D.udfb = p[P_UFB] * 0.01f;
        D.udlen = static_cast<int>(p[P_UDLY] * sr * (0.001f / kDecim));
        if (D.udlen < 1) D.udlen = 1;
        if (D.udlen > kDlyMax - 1) D.udlen = kDlyMax - 1;
        D.urverb = p[P_VROUTE] >= 0.5f && p[P_VMIX] > 0.f;
        D.urmix = p[P_VMIX] * 0.01f;
        if (D.urverb) rroom.set(p[P_VSIZE] * 0.01f, p[P_VDECAY], p[P_VDAMP] * 0.01f, tr);

        // the end: each source at 1e-4 (twice its time to 1 %), the rumble's tails, and 30 ms for the chain's
        float end = 0.f;
        const auto at_least = [&end](float x) { if (x > end) end = x; };
        if (D.blevel > 0.f || D.uduck > 0.f) at_least(D.attack + D.hold + decay);
        if (D.rlevel > 0.f) at_least(2.f * p[P_RDECAY] * ms);
        if (D.nlevel > 0.f) at_least(bursts_len + 2.f * p[P_NDECAY] * ms);
        if (D.xlevel > 0.f) at_least(2.f * p[P_XDECAY] * ms);
        float rumble_end = D.ulevel > 0.f ? 2.f * udec : 0.f;
        if (rumble_end > 0.f && D.urverb) rumble_end += p[P_VDECAY] * ms;
        if (rumble_end > 0.f && D.udmix > 0.f) {
            const float repeats = D.udfb > 0.001f ? -9.966f / lab::fast_log2(D.udfb) : 0.f;
            const float tail = static_cast<float>(D.udlen * kDecim) * (repeats + 1.f);
            rumble_end += tail < 5.f * sr ? tail : 5.f * sr;
        }
        at_least(rumble_end);
        D.end = end + 30.f * ms;
        D.kres = lab::decay_coef(1.f, sr);

        // the drive (the Kick's)
        D.g = lab::fast_db(p[P_DRIVE]);
        D.tube = p[P_TUBE] * 0.01f;
        D.bias = 0.3f * D.tube;
        D.bias_y = lab::soft_clip(D.bias);
        D.tube_n = 1.f / (lab::soft_clip(D.g + D.bias) - D.bias_y);
        D.sat = p[P_SAT] * 0.01f;
        D.sat_n = 1.f / lab::soft_clip(D.g);
        D.fold = p[P_FOLD] * 0.01f;
        D.fold_g = 1.f + 3.f * D.fold;
        D.clip = p[P_CLIP] * 0.01f;
        D.clip_t = 1.f - 0.75f * D.clip;
        D.inv_clip_t = 1.f / D.clip_t;

        // the compressor (the Kick's)
        const float amounts[3] = {p[P_CLO] * 0.01f, p[P_CMID] * 0.01f, p[P_CHI] * 0.01f};
        D.comp_on = false;
        for (int b = 0; b < 3; b++) {
            D.band_on[b] = amounts[b] >= 0.005f;
            D.comp_on = D.comp_on || D.band_on[b];
            D.thr[b] = -36.f * amounts[b];
            D.slope[b] = 1.f - 1.f / (1.f + 7.f * amounts[b]);
            D.makeup[b] = -D.thr[b] * D.slope[b] * 0.4f;
        }
        D.kat = 1.f - lab::decay_coef(p[P_CATK], sr);
        D.krel = 1.f - lab::decay_coef(p[P_CREL], sr);
        if (!D.comp_on) for (float &x : gain) x = 1.f;
        split_lo.set(120.f, 0.f, sr);
        split_hi.set(2500.f, 0.f, sr);

        eq.set(p[P_EQLOHZ], p[P_EQLO], p[P_EQMIDHZ], p[P_EQMID], 0.9f, p[P_EQHIHZ], p[P_EQHI], sr);
        D.level = lab::fast_db(p[P_LEVEL]);
    }
    void refresh(const float *p, float sr) {        // update() when a value or the sample rate changed
        if (sr == sr_ && std::memcmp(p, pc, sizeof pc) == 0) return;
        std::memcpy(pc, p, sizeof pc);
        sr_ = sr;
        update(p, sr);
    }

    // The body's three envelopes at tt (samples since the hit), as the phase step, the harmonics and the level
    float step_at(float tt) const {
        const float pe = d.plin ? (tt < d.pdec ? 1.f - tt * d.inv_pdec : 0.f) : lab::fast_exp2(d.pk * tt);
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
    float shaped(float s) const { return s + (lab::soft_clip(s * d.shape_g) * d.shape_n - s) * d.shape; }

    // The rumble's filter at hz (the Kick's): fast, g by fast_tan (each block, under its envelope), else tanf (update())
    void set_cutoff(float hz, bool fast) {
        const Derived &D = d;
        const float f = lab::clampf(hz, 10.f, 0.33f * D.tr);
        const float g = fast ? lab::fast_tan(lab::kPi * f * D.inv_tr) : tanf(lab::kPi * f / D.tr);
        ulp.set_g(g, D.ures);
        if (D.utype == 2 || D.utype == 5) ulp2.set_g(g, 0.30831f);
        if (D.utype == 0) op_g = g / (1.f + g);
    }
    float ufenv_at(float tt) const { return tt < d.ufatt ? tt * d.inv_ufatt : lab::fast_exp2(d.ufk * (tt - d.ufatt)); }
    float ufilter(float x) {
        switch (d.utype) {
        case 0: { const float v = (x - op_s) * op_g, y = v + op_s; op_s = y + v; return y; }
        case 1: return ulp.run(x);
        case 2: return ulp2.run(ulp.run(x));
        case 3: ulp.run(x); return ulp.bp * ulp.k;
        case 4: ulp.run(x); return ulp.hp;
        case 5: ulp.run(x); ulp2.run(ulp.hp); return ulp2.hp;
        case 6: ulp.run(x); return x - ulp.k * ulp.bp;
        default: ulp.run(x); return ulp.lp - ulp.hp;
        }
    }
    float comp_gain(int b) const {
        if (!d.band_on[b]) return 1.f;
        const float over = lab::fast_to_db(det[b] + 1e-6f) - d.thr[b];
        return lab::fast_db(d.makeup[b] - (over > 0.f ? over * d.slope[b] : 0.f));
    }

    // The rumble, one tick (kDecim samples; the Kick's rumble_tick): noise + body -> Pre -> filter -> Post -> decay -> reverb -> echo -> duck
    float rumble_tick(float body, bool noise_on, float ulevel, float a) {
        const Derived &D = d;
        float r = 0.f;
        if (noise_on) {
            float src = 0.f;
            if (D.uin_noise > 0.f) {
                float nz = rnoise.next();
                if (D.upre > 0.f) nz += (lab::soft_clip(nz * D.upre_g) * D.upre_n - nz) * D.upre;
                src += nz * D.uin_noise;
            }
            if (D.uin_body > 0.f) {
                if (D.upre > 0.f) body += (lab::soft_clip(body * D.upre_g) * D.upre_n - body) * D.upre;
                src += body * D.uin_body;
            }
            r = ufilter(src) * D.umakeup;
            if (D.upost > 0.f) r += (lab::soft_clip(r * D.upost_g) * D.upost_n - r) * D.upost;
            r *= uenv * ulevel;
            uenv *= D.kuenv;
        }
        if (D.urverb) r += (rroom.tick(r) - r) * D.urmix;
        if (D.udmix > 0.f) {
            int rd = dw - D.udlen;
            if (rd < 0) rd += kDlyMax;
            const float back = static_cast<float>(dline[rd]) * (1.f / 8192.f);
            const float v = (r + D.udfb * back) * 8192.f;
            dline[dw] = static_cast<int16_t>(v > 32767.f ? 32767.f : (v < -32767.f ? -32767.f : v));
            if (++dw >= kDlyMax) dw = 0;
            r += (back - r) * D.udmix;
        }
        if (D.uduck > 0.f) {
            const float key = a * 4.f < 1.f ? a * 4.f : 1.f;
            duckf = key > duckf ? key : duckf * D.kuduck;
            r *= 1.f - D.uduck * duckf;
        }
        return r;
    }

    // The metal, one sample: Squares (six naive squares: their sum, crossfaded by Index to the products of the pairs) or FM (a sine
    // phase-modulated by another, the depth falling with the envelope), then the band pass (two stages: the squares' strong fundamentals,
    // far under a hat's Tone, are held down 12 dB an octave)
    float metal(const float *mdt, float fdt_c, float fdt_m) {
        const Derived &D = d;
        float m;
        if (!D.fm) {
            float sq[6];
            for (int k = 0; k < 6; k++) {
                sq[k] = mph[k] < 0.5f ? 1.f : -1.f;
                mph[k] += mdt[k];
                if (mph[k] >= 1.f) mph[k] -= 1.f;
            }
            const float sum = (sq[0] + sq[1] + sq[2] + sq[3] + sq[4] + sq[5]) * (1.f / 6.f);
            const float ring = (sq[0] * sq[1] + sq[2] * sq[3] + sq[4] * sq[5]) * (1.f / 3.f);
            m = sum + (ring - sum) * D.xindex;
        } else {
            m = lab::sin01(fph_c + 1.2732395f * D.xindex * em * lab::sin01(fph_m));   // (8 rad = 1.27 cycles at 100 %)
            fph_c += fdt_c;
            if (fph_c >= 1.f) fph_c -= 1.f;
            fph_m += fdt_m;
            if (fph_m >= 1.f) fph_m -= 1.f;
        }
        mbp.run(m);
        mbp2.run(mbp.bp * mbp.k);                                   // (each stage times k: 1 at its peak)
        return mbp2.bp * mbp2.k * kMetalGain;
    }

    void start(const lab::Note &n, const float *p) {
        refresh(p, n.sr);
        residual = last_src;
        t = 0.f;
        vel = 0.35f + 0.65f * n.velocity;
        note_ratio = n.hz * (1.f / 130.8128f);                      // C3 plays the tunes
        base = p[P_TUNE] * note_ratio;
        ph = ph_r = fph_c = fph_m = 0.f;
        for (int k = 0; k < 6; k++) mph[k] = kMetalPhases[k];      // (spread: all six at 0 would jump together, a click at the hit)
        if (d.seeded) {                                             // the same noises at every hit, their filters from rest
            noise.s = d.seed;
            rnoise.s = d.seed ^ 0x5BD1E995u;
            neq.reset();
            ulp.ic1 = ulp.ic2 = ulp2.ic1 = ulp2.ic2 = op_s = 0.f;
        }
        mbp.ic1 = mbp.ic2 = mbp2.ic1 = mbp2.ic2 = 0.f;
        er = en = em = uenv = 1.f;
        burst = 0;
        left = d.ratchets > 1 ? d.gaps[0] : 0;
        rphase = 0;
        kacc = 0.f;
        dt_ = step_at(0.f);
        h_ = harm_at(0.f);
        a_ = amp_at(0.f);
    }
    void release(const lab::Note &, const float *) {}       // a one-shot: the key's length does not matter

    bool render(float *out, int frames, const lab::Note &n, const float *p) {
        refresh(p, n.sr);
        const Derived &D = d;
        note_ratio = n.hz * (1.f / 130.8128f);
        base = p[P_TUNE] * note_ratio;
        const float t1 = t + static_cast<float>(frames), inv_n = frames == 32 ? 1.f / 32.f : 1.f / static_cast<float>(frames);   // (no division: blocks are 32)
        float dt = dt_, h = h_, a = a_;                             // the block's start: the last one's end
        dt_ = step_at(t1);
        h_ = harm_at(t1);
        a_ = amp_at(t1);
        const float ddt = (dt_ - dt) * inv_n, dh = (h_ - h) * inv_n, da = (a_ - a) * inv_n;
        float dg[3] = {};
        if (D.comp_on) for (int b = 0; b < 3; b++) dg[b] = (comp_gain(b) - gain[b]) * inv_n;

        const bool ring_on = er > 1e-4f && D.rlevel > 0.f;
        const bool noise_on = (en > 1e-4f || left > 0) && D.nlevel > 0.f;
        const bool metal_on = em > 1e-4f && D.xlevel > 0.f;
        const bool unoise_on = uenv > 1e-4f && D.ulevel > 0.f;
        const bool rumble_on = unoise_on || D.urverb || D.udmix > 0.f || rcur != 0.f || rprev != 0.f;
        const bool shape_on = D.shape > 0.f;
        const float bl = D.blevel * vel, rl = D.rlevel * vel, nl = D.nlevel * vel, xl = D.xlevel * vel, ul = D.ulevel * vel;
        const float dr = lab::clampf(base * D.rratio, 5.f, sr_ * 0.45f) * D.inv_sr;
        float mdt[6] = {}, fdt_c = 0.f, fdt_m = 0.f;
        if (metal_on) {
            const float xb = p[P_XTUNE] * note_ratio;
            for (int k = 0; k < 6; k++) mdt[k] = lab::clampf(xb * (1.f + (kMetalRatios[k] - 1.f) * D.xratio), 5.f, sr_ * 0.45f) * D.inv_sr;
            fdt_c = lab::clampf(xb, 5.f, sr_ * 0.45f) * D.inv_sr;
            fdt_m = lab::clampf(xb * D.xratio, 5.f, sr_ * 0.45f) * D.inv_sr;
        }
        if (D.ufenv_on && unoise_on) set_cutoff(D.ucut * lab::fast_exp2(D.ufoct * ufenv_at(t + 0.5f * static_cast<float>(frames))), true);

        for (int i = 0; i < frames; i++) {
            // the body: sine -> triangle (harmonics), then -> distorted sine (shape)
            const float s = lab::sin01(ph);
            float x = s;
            if (D.tri_on) {
                const float tri = ph < 0.25f ? 4.f * ph : (ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f);
                x += (tri - s) * h;
                h += dh;
            }
            ph += dt;
            if (ph >= 1.f) ph -= 1.f;
            dt += ddt;
            if (shape_on) x = shaped(x);
            float y = x * a * bl + residual;
            a += da;
            if (ring_on) {                                          // the ring: the steady sine
                const float r = lab::sin01(ph_r);
                ph_r += dr;
                if (ph_r >= 1.f) ph_r -= 1.f;
                y += (shape_on ? shaped(r) : r) * er * rl;
                er *= D.kr;
            }
            if (noise_on) {                                         // the noise: the ratchet's next burst, its EQ
                if (left > 0 && --left == 0) {
                    burst++;
                    en = 1.f;
                    left = burst < D.ratchets - 1 ? D.gaps[burst] : 0;
                }
                y += neq.run(noise.next()) * en * nl;
                en *= left > 0 ? D.kburst : D.ktail;
            }
            if (metal_on) {
                y += metal(mdt, fdt_c, fdt_m) * em * xl;
                em *= D.kx;
            }
            if (rumble_on) {                                        // the rumble, at 1 / kDecim of the rate
                kacc += x;
                if (++rphase >= kDecim) {
                    rphase = 0;
                    rprev = rcur;
                    rcur = rumble_tick(kacc * (vel / kDecim), unoise_on, ul, a);
                    kacc = 0.f;
                }
                y += rprev + (rcur - rprev) * static_cast<float>(rphase) * (1.f / kDecim);
            }
            residual *= D.kres;
            last_src = y;

            // the drive, each part mixed in by its amount
            if (D.tube > 0.f) y += ((lab::soft_clip(y * D.g + D.bias) - D.bias_y) * D.tube_n - y) * D.tube;
            if (D.fold > 0.f) y += (lab::sin01(0.25f * y * D.fold_g) - y) * D.fold;
            if (D.sat > 0.f) y += (lab::soft_clip(y * D.g) * D.sat_n - y) * D.sat;
            if (D.clip > 0.f) y += (lab::clampf(y, -D.clip_t, D.clip_t) * D.inv_clip_t - y) * D.clip;
            y = dc.run(y);

            // the 3-band compressor
            if (D.comp_on) {
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
            out[i] += lab::soft_clip(eq.run(y) * D.level) * 0.6f;
        }
        t = t1;
        // the states that decay to nothing: kept out of the denormals (lab_dsp.h, flush)
        lab::flush(er);
        lab::flush(en);
        lab::flush(em);
        lab::flush(residual);
        lab::flush(duckf);
        lab::flush(rprev);
        lab::flush(rcur);
        lab::flush(op_s);
        neq.flush();
        mbp.flush();
        mbp2.flush();
        rroom.flush();
        ulp.flush();
        ulp2.flush();
        dc.flush();
        split_lo.flush();
        split_hi.flush();
        eq.flush();
        for (float &x : det) lab::flush(x);
        return t < D.end;
    }
};

// What runs once on the sum of the voices (lab.h, Fx): the reverb (Route = Output), the gate (the Snare's), the master (the Kick's).
struct Fx {
    Room<1201, 1427, 1709, 2003> room;                              // at half rate (24 kHz): 50 .. 83 ms; Size shortens them
    float prev = 0.f;
    static constexpr int kWatch = N_PARAMS - P_VROUTE;              // the values it uses: the REVERB, GATE and MASTER sections
    float pc[kWatch] = {}, sr_ = 0.f;
    bool fresh = true;
    float mix = 0.f;
    // the gate
    bool gate_on = false, key_dry = true, open = false;
    float thr_open = 0.f, thr_close = 0.f, floor_ = 0.f, kdet = 0.f, katt = 1.f, krel = 1.f;
    float det = 0.f, gain = 1.f;
    int hold = 0, held = 0;
    // the master
    static constexpr int kChunk = 8;
    float mdet = 0.f, mgain = 1.f, tilt_lp = 0.f;
    bool glue_on = false, tilt_on = false, master_on = false;
    float thr = 0.f, slope = 0.f, makeup = 0.f, mkat = 0.f, mkrel = 0.f;
    float msat = 0.f, sat_g = 1.f, sat_n = 1.f, tilt_a = 0.f, tilt_lo = 1.f, tilt_hi = 1.f, mout = 1.f;

    LAB_COLD void update(const float *p, float sr) {
        const float ms = sr * 0.001f;
        mix = p[P_VROUTE] >= 0.5f ? 0.f : p[P_VMIX] * 0.01f;        // (routed to the rumble: off here)
        room.set(p[P_VSIZE] * 0.01f, p[P_VDECAY], p[P_VDAMP] * 0.01f, sr * 0.5f);

        const float depth = p[P_GDEPTH] * 0.01f;
        gate_on = depth >= 0.005f;
        if (!gate_on) gain = 1.f;
        floor_ = 1.f - depth;
        key_dry = p[P_GKEY] < 0.5f;
        thr_open = lab::fast_db(p[P_GTHR]);
        thr_close = thr_open * 0.70795f;                            // (-3 dB)
        kdet = lab::decay_coef(10.f, sr);
        katt = 1.f - lab::fast_exp2(-6.644f / (p[P_GATK] * ms));
        krel = 1.f - lab::fast_exp2(-6.644f / (p[P_GREL] * ms));
        hold = static_cast<int>(p[P_GHOLD] * ms);

        const float glue = p[P_MGLUE] * 0.01f, tilt = p[P_MTILT] * 0.01f;
        glue_on = glue >= 0.005f;
        thr = -30.f * glue;
        slope = 1.f - 1.f / (1.f + 3.f * glue);
        makeup = -thr * slope * 0.25f;
        mkat = 1.f - lab::decay_coef(p[P_MATK], sr);
        mkrel = 1.f - lab::decay_coef(p[P_MREL], sr);
        if (!glue_on) mgain = 1.f;
        msat = p[P_MSAT] * 0.01f;
        sat_g = 1.f + 7.f * msat;
        sat_n = 1.f / sqrtf(sat_g);
        tilt_on = tilt >= 0.005f || tilt <= -0.005f;
        tilt_a = 1.f - expf(-2.f * lab::kPi * 800.f / sr);
        tilt_lo = lab::fast_db(-6.f * tilt);
        tilt_hi = lab::fast_db(6.f * tilt);
        mout = lab::fast_db(p[P_MOUT]);
        master_on = glue_on || msat > 0.f || tilt_on || p[P_MOUT] != 0.f;

        std::memcpy(pc, p + P_VROUTE, sizeof pc);
        sr_ = sr;
        fresh = false;
    }
    float gate(float key) {
        const float e = std::fabs(key);
        det = e > det ? e : det * kdet;
        if (det > (open ? thr_close : thr_open)) { open = true; held = hold; }
        else if (held > 0) held--;
        else open = false;
        const float target = open ? 1.f : floor_;
        gain += (target - gain) * (target > gain ? katt : krel);
        return gain;
    }
    float glue_gain() const {
        const float over = lab::fast_to_db(mdet + 1e-6f) - thr;
        const float gr = over <= -3.f ? 0.f : (over >= 3.f ? over * slope : slope * (over + 3.f) * (over + 3.f) * (1.f / 12.f));
        return lab::fast_db(makeup - gr);
    }
    void render(float *io, int frames, float sr, const float *p) {
        if (fresh || sr != sr_ || std::memcmp(p + P_VROUTE, pc, sizeof pc) != 0) update(p, sr);
        reverb_gate(io, frames);
        if (master_on) master(io, frames);
    }
    void reverb_gate(float *io, int frames) {
        const bool verb_on = mix > 0.f;
        if (!verb_on && !gate_on) return;
        for (int i = 0; i + 1 < frames; i += 2) {
            const float d0 = io[i], d1 = io[i + 1];                 // the dry voices: the gate's key (Dry)
            if (verb_on) {
                const float wet = room.tick((d0 + d1) * 0.5f);
                io[i] += (prev + wet) * 0.5f * mix;
                io[i + 1] += wet * mix;
                prev = wet;
            }
            if (gate_on) {
                io[i] *= gate(key_dry ? d0 : io[i]);
                io[i + 1] *= gate(key_dry ? d1 : io[i + 1]);
            }
        }
        if (verb_on) {
            room.flush();
            lab::flush(prev);
        }
        lab::flush(det);
        lab::flush(gain);
    }
    void master(float *io, int frames) {
        for (int c = 0; c < frames; c += kChunk) {
            const int m = frames - c < kChunk ? frames - c : kChunk;
            float g = mgain, dg = 0.f;
            if (glue_on) {
                for (int i = c; i < c + m; i++) {
                    const float e = std::fabs(io[i]);
                    mdet += (e - mdet) * (e > mdet ? mkat : mkrel);
                }
                mgain = glue_gain();
                dg = (mgain - g) * (m == kChunk ? 1.f / kChunk : 1.f / static_cast<float>(m));
            }
            for (int i = c; i < c + m; i++) {
                float x = io[i];
                if (glue_on) {
                    g += dg;
                    x *= g;
                }
                if (msat > 0.f) x += (lab::soft_clip(x * sat_g) * sat_n - x) * msat;
                if (tilt_on) {
                    tilt_lp += (x - tilt_lp) * tilt_a;
                    x = tilt_lp * tilt_lo + (x - tilt_lp) * tilt_hi;
                }
                io[i] = x * mout;
            }
        }
        lab::flush(mdet);
        lab::flush(tilt_lp);
    }
};

}  // namespace

extern const lab::Synth synth_drumlab = lab::make_synth<Voice, Fx>("DrumLab", kParams, drum_options());
