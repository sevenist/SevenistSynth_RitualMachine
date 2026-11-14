// Snare: three layers summed, then shaped as the Kick is. The skin: a sine that starts above the tune and sweeps down to it
// (exponentially), with its own decay. The ring: a steady sine at a ratio of the tune (the shell's resonance; 1.59 is a drum head's
// second mode), its own decay. The wires: noise between a low cut and a high cut, with a ratchet envelope: 1 to 8 bursts at the hit, each
// one decaying at Burst until the next, the last one at Decay (the tail). Then the Kick's shaping: Shape (each sine -> a distorted sine),
// Fold (a sine shaper on the sum), Sat (a soft saturation hit at Drive), a DC block, the level and a soft limit; on the sum of the voices
// the Kick's reverb (the Room: four 16-bit lines in a feedback network, at half the sample rate). docs/SYNTHS.md, "Snare".
//
// Cost, as the Kick's: no division and no libm call in the sample loop; what the values decide is computed when one changes (update());
// the sweep once a block (fast_exp2), in a line between; each envelope a multiply a sample; the layers and shapers at zero skipped.
#include <cstring>
#include "lab.h"
#include "lab_dsp.h"

namespace {

enum : int {
    H_SKIN, P_TUNE, P_SLEVEL, P_SWEEP, P_SWTIME, P_SDECAY,
    H_RING, P_RATIO, P_RLEVEL, P_RDECAY,
    H_NOISE, P_NLEVEL, P_RATCH, P_SPACE, P_ACCEL, P_BURST, P_NDECAY, P_SEED,
    H_NEQ, P_NHP, P_NMID, P_NMIDHZ, P_NLP,
    H_SHAPE, P_SHAPE, P_DRIVE, P_FOLD, P_SAT,
    H_EQ, P_EQLO, P_EQLOHZ, P_EQMID, P_EQMIDHZ, P_EQHI, P_EQHIHZ,
    H_OUT, P_LEVEL,
    H_VERB, P_RMIX, P_RVDECAY, P_RSIZE, P_RDAMP,
    H_GATE, P_GDEPTH, P_GKEY, P_GTHR, P_GATK, P_GHOLD, P_GREL,
    H_MASTER, P_MGLUE, P_MATK, P_MREL, P_MSAT, P_MTILT, P_MOUT,
    N_PARAMS
};

constexpr int kMaxRatchets = 8;
constexpr float kNoiseGain = 0.8f;                      // the noise at Level 100 %, over its whole band (white noise: RMS 0.58)
const char *const kKeys[] = {"Dry", "Out"};

const lab::Param kParams[] = {
    lab::section("SKIN"),                               // the drum head: a sine that starts high and sweeps down to the tune
    {"Tune",     100,  400,   180,   "Hz", lab::Exp},   // on C3; the keys transpose it (and the ring with it)
    {"Level",    0,    100,   70,    "%"},
    {"Sweep",    0,    24,    7,     "st", lab::Int},   // how far above the tune the hit starts
    {"Sw Time",  2,    200,   30,    "ms", lab::Exp},   // 99 % of the way down to the tune
    {"Decay",    10,   1000,  110,   "ms", lab::Exp},   // to 1 %
    lab::section("RING"),                               // the shell's resonance: a steady sine (no sweep)
    {"Ratio",    0.5f, 4,     1.59f, "x",  lab::Exp},   // its pitch over the tune (1.59: a drum head's second mode)
    {"Level",    0,    100,   35,    "%"},
    {"Decay",    10,   2000,  220,   "ms", lab::Exp},   // to 1 %
    lab::section("NOISE"),                              // the wires: noise with a ratchet envelope, then its EQ
    {"Level",    0,    100,   55,    "%"},
    {"Ratchets", 1,    kMaxRatchets, 3, nullptr, lab::Int},    // bursts at the hit (1: one burst); the last one is the tail
    {"Spacing",  1,    50,    8,     "ms", lab::Exp},   // from one burst to the next
    {"Accel",    -100, 100,   0,     "%"},              // each gap shorter (-) or longer (+) than the one before: x0.5 .. x2
    {"Burst",    1,    100,   15,    "ms", lab::Exp},   // each burst's decay (to 1 %), until the next one
    {"Decay",    10,   2000,  180,   "ms", lab::Exp},   // the last burst's (to 1 %): the tail
    {"Seed",     0,    99,    0,     nullptr, lab::Int},   // 0: another noise at every hit; 1..99: the same waveform at every hit
    lab::section("NOISE EQ"),                           // on the noise alone: high pass, bell, low pass (its level kept as HP and LP move)
    {"HP",       20,   8000,  1200,  "Hz", lab::Exp},   // a high pass, 12 dB / octave: takes the lows out (20 Hz: off)
    {"Mid",      -24,  12,    0,     "dB", lab::Int},   // a bell (Q 0.9): the wires' sizzle
    {"Mid Hz",   200,  12000, 4000,  "Hz", lab::Exp},
    {"LP",       1000, 20000, 10000, "Hz", lab::Exp},   // a low pass, 12 dB / octave: takes the highs out (20 kHz: off)
    lab::section("SHAPE"),                              // as the Kick's
    {"Shape",    0,    100,   20,    "%"},              // the skin and the ring: sine -> distorted sine
    {"Drive",    0,    24,    6,     "dB", lab::Int},   // how hard Sat is hit
    {"Fold",     0,    100,   10,    "%"},              // a sine shaper on the sum: the peaks fold back, metallic
    {"Sat",      0,    100,   30,    "%"},              // a symmetric soft saturation: odd harmonics, loudness
    lab::section("EQ"),                                 // on the whole voice, after the shapers
    {"Low",      -12,  12,    0,     "dB", lab::Int},   // a low shelf
    {"Low Hz",   40,   1000,  200,   "Hz", lab::Exp},
    {"Mid",      -12,  12,    0,     "dB", lab::Int},   // a bell (Q 0.9)
    {"Mid Hz",   100,  8000,  1000,  "Hz", lab::Exp},
    {"High",     -12,  12,    0,     "dB", lab::Int},   // a high shelf
    {"High Hz",  1000, 16000, 6000,  "Hz", lab::Exp},
    lab::section("OUT"),
    {"Level",    -30,  6,     0,     "dB", lab::Int},   // before the soft limit
    lab::section("REVERB"),                             // the Kick's: on the sum of the voices, its tail outlives the hit
    {"Mix",      0,    100,   20,    "%"},
    {"Decay",    100,  4000,  700,   "ms", lab::Exp},   // to -60 dB
    {"Size",     10,   100,   50,    "%"},              // the lines' length: 5 to 83 ms
    {"Damp",     0,    100,   40,    "%"},              // a low pass in the loop: the tail darkens
    lab::section("GATE"),                               // after the reverb, on the sum of the voices
    {"Depth",    0,    100,   100,   "%"},              // how far it closes (0: off; 100: to silence)
    {"Key",      0,    1,     0,     nullptr, lab::Int, kKeys},  // what opens it: Dry, the voices before the reverb (the tail cut once
                                                        //   the hit has gone: a gated reverb); Out, after it (the tail keeps it open)
    {"Thresh",   -60,  0,     -40,   "dB", lab::Int},   // it opens above this level (and closes 3 dB below it)
    {"Attack",   0.1f, 20,    0.3f,  "ms", lab::Exp},   // how fast it opens
    {"Hold",     0,    1000,  80,    "ms"},             // held open after the key falls under the threshold
    {"Release",  5,    1000,  50,    "ms", lab::Exp},   // then how fast it closes (99 % of the way)
    lab::section("MASTER"),                             // the Kick's shaper, last: glue -> saturation -> tilt -> output
    {"Glue",     0,    100,   30,    "%"},              // a bus compressor: threshold 0 .. -30 dB, ratio 1 .. 4, 6 dB soft knee, a quarter
                                                        //   of the reduction given back (makeup)
    {"Attack",   0.1f, 30,    3,     "ms", lab::Exp},   // fast: it evens the hit and its body (glue); slow: the hit passes (punch)
    {"Release",  30,   1000,  150,   "ms", lab::Exp},
    {"Sat",      0,    100,   20,    "%"},              // a soft saturation: the body denser and louder, the peaks rounded
    {"Tilt",     -100, 100,   0,     "%"},              // around 800 Hz: - darker, + brighter (the ends: +-6 dB on each side)
    {"Output",   -12,  6,     0,     "dB", lab::Int},
};
static_assert(sizeof kParams / sizeof kParams[0] == N_PARAMS, "the enum follows the list");

// The reverb, the Kick's (kick.cpp, Room): four delay lines fed back through a 4 x 4 Hadamard matrix (a feedback delay network), a
// one-pole low pass in each loop, 16-bit lines. L0..L3: the lines' longest lengths in ticks.
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

// The noise's EQ: a high pass, a bell, a low pass (the Biquad's: 12 dB / octave at Q 0.707, the bell at Q 0.9). Each band is skipped
// when it does nothing (HP at its minimum, the bell at 0 dB, LP at its maximum), its memory cleared when it comes back on.
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

// What the values decide, for the sample loop: Voice::update() computes it when a value (or the sample rate) changes.
struct Derived {
    float inv_sr = 1.f / 48000.f;
    float pamt12 = 0.f, pk = 0.f;                       // the sweep: semitones / 12, its speed (2^(pk t): 1 % at Sw Time)
    float slevel = 0.f, ks = 0.f;                       // the skin: level, its decay a sample
    float ratio = 1.f, rlevel = 0.f, kr = 0.f;          // the ring: ratio, level, decay a sample
    float nlevel = 0.f, kburst = 0.f, ktail = 0.f;      // the noise: level, the bursts' and the tail's decay
    int ratchets = 1, gaps[kMaxRatchets] = {};          //   the bursts, the samples from each to the next
    bool seeded = false;
    uint32_t seed = 0;
    float kres = 0.f;                                   // a hit over a sounding one: the old level's fade (1 ms)
    float shape = 0.f, shape_g = 1.f, shape_n = 1.f;
    float g = 1.f, sat = 0.f, sat_n = 1.f, fold = 0.f, fold_g = 1.f;
    float level = 1.f;
    float end = 0.f;                                    // samples from the hit to silence
};

struct Voice {
    float pc[N_PARAMS] = {};                        // the values update() last saw
    float sr_ = 0.f;
    Derived d;
    float ph_s = 0.f, ph_r = 0.f, t = 0.f, vel = 1.f, base = 180.f;    // the skin's and the ring's phases; samples since the hit; the tune
    float dt_ = 0.f;                                // the skin's phase step at t (the next block's start)
    float es = 0.f, er = 0.f, en = 0.f;             // the envelopes: skin, ring, noise
    int burst = 0, left = 0;                        // the noise's burst (0..Ratchets - 1), the samples to the next one (0: the last)
    lab::Noise noise;
    NoiseEq neq;                                    // the noise's EQ: high pass, bell, low pass
    float last_src = 0.f, residual = 0.f;
    lab::DcBlock dc;
    lab::Eq3 eq;                                    // the voice's EQ, after the shapers

    LAB_COLD void update(const float *p, float sr) {
        Derived &D = d;
        const float ms = sr * 0.001f;
        D.inv_sr = 1.f / sr;
        D.pamt12 = p[P_SWEEP] * (1.f / 12.f);
        D.pk = -6.644f / (p[P_SWTIME] * ms);
        D.slevel = p[P_SLEVEL] * 0.01f;
        D.ks = lab::fast_exp2(-6.644f / (p[P_SDECAY] * ms));            // 1 % at Decay
        D.ratio = p[P_RATIO];
        D.rlevel = p[P_RLEVEL] * 0.01f;
        D.kr = lab::fast_exp2(-6.644f / (p[P_RDECAY] * ms));

        // the noise: its bursts' gaps (each one Accel times the last), its envelope's two speeds, its EQ
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
        // the noise's EQ, its level kept as HP and LP move: white noise's level goes as the square root of its bandwidth (a 12 dB low
        // pass lets through 1.11 x its cutoff of it), at least 300 Hz
        neq.set(p[P_NHP], p[P_NMID], p[P_NMIDHZ], p[P_NLP], sr);
        float bw = (neq.on[2] && 1.11f * p[P_NLP] < 0.5f * sr ? 1.11f * p[P_NLP] : 0.5f * sr) - (neq.on[0] ? p[P_NHP] : 0.f);
        if (bw < 300.f) bw = 300.f;
        D.nlevel = p[P_NLEVEL] * 0.01f * kNoiseGain * sqrtf(0.5f * sr / bw);
        D.seeded = p[P_SEED] >= 0.5f;
        D.seed = static_cast<uint32_t>(p[P_SEED]) * 2654435761u + 0x9E3779B9u;
        D.kres = lab::decay_coef(1.f, sr);

        D.shape = p[P_SHAPE] * 0.01f;
        D.shape_g = 1.f + 6.f * D.shape;
        D.shape_n = 1.f / lab::soft_clip(D.shape_g);
        D.g = lab::fast_db(p[P_DRIVE]);
        D.sat = p[P_SAT] * 0.01f;
        D.sat_n = 1.f / lab::soft_clip(D.g);
        D.fold = p[P_FOLD] * 0.01f;
        D.fold_g = 1.f + 3.f * D.fold;
        eq.set(p[P_EQLOHZ], p[P_EQLO], p[P_EQMIDHZ], p[P_EQMID], 0.9f, p[P_EQHIHZ], p[P_EQHI], sr);
        D.level = lab::fast_db(p[P_LEVEL]);

        // the end: each layer at 1e-4 (twice its time to 1 %), and 30 ms for the chain's tail
        float end = 0.f;
        if (D.slevel > 0.f) end = 2.f * p[P_SDECAY] * ms;
        if (D.rlevel > 0.f && 2.f * p[P_RDECAY] * ms > end) end = 2.f * p[P_RDECAY] * ms;
        if (D.nlevel > 0.f && bursts_len + 2.f * p[P_NDECAY] * ms > end) end = bursts_len + 2.f * p[P_NDECAY] * ms;
        D.end = end + 30.f * ms;
    }
    void refresh(const float *p, float sr) {        // update() when a value or the sample rate changed
        if (sr == sr_ && std::memcmp(p, pc, sizeof pc) == 0) return;
        std::memcpy(pc, p, sizeof pc);
        sr_ = sr;
        update(p, sr);
    }

    // The skin's phase step at tt (samples since the hit): the tune, raised by the sweep's semitones as they fall away
    float step_at(float tt) const {
        return lab::clampf(base * lab::fast_exp2(d.pamt12 * lab::fast_exp2(d.pk * tt)), 5.f, sr_ * 0.45f) * d.inv_sr;
    }
    float shaped(float s) const { return s + (lab::soft_clip(s * d.shape_g) * d.shape_n - s) * d.shape; }

    void start(const lab::Note &n, const float *p) {
        refresh(p, n.sr);
        residual = last_src;
        ph_s = ph_r = 0.f;
        t = 0.f;
        vel = 0.35f + 0.65f * n.velocity;
        base = p[P_TUNE] * n.hz * (1.f / 130.8128f);                // C3 plays the tune
        if (d.seeded) {                                             // the same noise at every hit, its filters from rest
            noise.s = d.seed;
            neq.reset();
        }
        es = er = en = 1.f;
        burst = 0;
        left = d.ratchets > 1 ? d.gaps[0] : 0;
        dt_ = step_at(0.f);
    }
    void release(const lab::Note &, const float *) {}       // a one-shot: the key's length does not matter

    bool render(float *out, int frames, const lab::Note &n, const float *p) {
        refresh(p, n.sr);
        const Derived &D = d;
        base = p[P_TUNE] * n.hz * (1.f / 130.8128f);
        const float t1 = t + static_cast<float>(frames), inv_n = frames == 32 ? 1.f / 32.f : 1.f / static_cast<float>(frames);   // (no division: blocks are 32)
        float dt = dt_;                                             // the block's start: the last one's end
        dt_ = step_at(t1);
        const float ddt = (dt_ - dt) * inv_n;
        const float dr = lab::clampf(base * D.ratio, 5.f, sr_ * 0.45f) * D.inv_sr;
        const bool skin_on = es > 1e-4f && D.slevel > 0.f, ring_on = er > 1e-4f && D.rlevel > 0.f;
        const bool noise_on = (en > 1e-4f || left > 0) && D.nlevel > 0.f;
        const float sl = D.slevel * vel, rl = D.rlevel * vel, nl = D.nlevel * vel;
        const bool shape_on = D.shape > 0.f;

        for (int i = 0; i < frames; i++) {
            float x = 0.f;
            if (skin_on) {                                          // the skin: the swept sine
                const float s = lab::sin01(ph_s);
                ph_s += dt;
                if (ph_s >= 1.f) ph_s -= 1.f;
                dt += ddt;
                x += (shape_on ? shaped(s) : s) * es * sl;
                es *= D.ks;
            }
            if (ring_on) {                                          // the ring: the steady sine
                const float s = lab::sin01(ph_r);
                ph_r += dr;
                if (ph_r >= 1.f) ph_r -= 1.f;
                x += (shape_on ? shaped(s) : s) * er * rl;
                er *= D.kr;
            }
            if (noise_on) {                                         // the wires: the ratchet's next burst, the noise, its EQ
                if (left > 0 && --left == 0) {
                    burst++;
                    en = 1.f;
                    left = burst < D.ratchets - 1 ? D.gaps[burst] : 0;
                }
                x += neq.run(noise.next()) * en * nl;
                en *= left > 0 ? D.kburst : D.ktail;
            }
            float y = x + residual;
            residual *= D.kres;
            last_src = y;

            // the shapers, each mixed in by its amount
            if (D.fold > 0.f) y += (lab::sin01(0.25f * y * D.fold_g) - y) * D.fold;
            if (D.sat > 0.f) y += (lab::soft_clip(y * D.g) * D.sat_n - y) * D.sat;
            y = eq.run(dc.run(y));
            out[i] += lab::soft_clip(y * D.level) * 0.6f;
        }
        t = t1;
        // the states that decay to nothing: kept out of the denormals (lab_dsp.h, flush)
        lab::flush(es);
        lab::flush(er);
        lab::flush(en);
        lab::flush(residual);
        neq.flush();
        dc.flush();
        eq.flush();
        return t < D.end;
    }
};

// What runs once on the sum of the voices (lab.h, Fx): the reverb, then the gate.
//
// The reverb: the Room at half the sample rate (each pair of samples is averaged in, the output is drawn in a line between two ticks),
// as the Kick's. At 0 % it does not run (its lines keep what they hold).
//
// The gate: a peak follower of its key (Dry: the voices before the reverb; Out: after it), falling in 10 ms; open above the threshold,
// closed 3 dB under it (no chatter at the edge), held open Hold after the key falls; its gain goes to 1 at Attack and to 1 - Depth at
// Release (one-poles, 99 % of the way in that time). At Depth 0 it does not run.
//
// The master shaper, last (the Kick's, kick.cpp Fx): a glue compressor (a peak detector every sample, the gain computed every 8 samples
// with a soft knee and followed in a line), a soft saturation, a tilt filter (one one-pole low pass: the lows and the rest weighted
// against each other), the output level. Each part is skipped at zero; at all zeros the shaper does not run.
struct Fx {
    Room<1201, 1427, 1709, 2003> room;                              // at half rate (24 kHz): 50 .. 83 ms; Size shortens them
    float prev = 0.f;
    static constexpr int kWatch = N_PARAMS - P_RMIX;                // the values it uses: the REVERB, GATE and MASTER sections
    float pc[kWatch] = {}, sr_ = 0.f;
    bool fresh = true;
    float mix = 0.f;
    // the gate
    bool gate_on = false, key_dry = true, open = false;
    float thr_open = 0.f, thr_close = 0.f, floor_ = 0.f, kdet = 0.f, katt = 1.f, krel = 1.f;
    float det = 0.f, gain = 1.f;
    int hold = 0, held = 0;
    // the master shaper
    static constexpr int kChunk = 8;                                // the glue's gain is computed every 8 samples
    float mdet = 0.f, mgain = 1.f, tilt_lp = 0.f;
    bool glue_on = false, tilt_on = false, master_on = false;
    float thr = 0.f, slope = 0.f, makeup = 0.f, mkat = 0.f, mkrel = 0.f;
    float msat = 0.f, sat_g = 1.f, sat_n = 1.f, tilt_a = 0.f, tilt_lo = 1.f, tilt_hi = 1.f, mout = 1.f;

    LAB_COLD void update(const float *p, float sr) {
        const float ms = sr * 0.001f;
        mix = p[P_RMIX] * 0.01f;
        room.set(p[P_RSIZE] * 0.01f, p[P_RVDECAY], p[P_RDAMP] * 0.01f, sr * 0.5f);
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
        makeup = -thr * slope * 0.25f;              // (a quarter: the hit passes the attack with it, more would clip)
        mkat = 1.f - lab::decay_coef(p[P_MATK], sr);
        mkrel = 1.f - lab::decay_coef(p[P_MREL], sr);
        if (!glue_on) mgain = 1.f;
        msat = p[P_MSAT] * 0.01f;
        sat_g = 1.f + 7.f * msat;                   // soft_clip(x g) / sqrt(g): the body up, the peaks held near the bus's level
        sat_n = 1.f / sqrtf(sat_g);
        tilt_on = tilt >= 0.005f || tilt <= -0.005f;
        tilt_a = 1.f - expf(-2.f * lab::kPi * 800.f / sr);
        tilt_lo = lab::fast_db(-6.f * tilt);
        tilt_hi = lab::fast_db(6.f * tilt);
        mout = lab::fast_db(p[P_MOUT]);
        master_on = glue_on || msat > 0.f || tilt_on || p[P_MOUT] != 0.f;

        std::memcpy(pc, p + P_RMIX, sizeof pc);
        sr_ = sr;
        fresh = false;
    }
    // The gate's gain for one sample of its key
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
    // The glue compressor's gain for the level detected: a 6 dB soft knee around the threshold, then the ratio; plus the makeup.
    float glue_gain() const {
        const float over = lab::fast_to_db(mdet + 1e-6f) - thr;
        const float gr = over <= -3.f ? 0.f : (over >= 3.f ? over * slope : slope * (over + 3.f) * (over + 3.f) * (1.f / 12.f));
        return lab::fast_db(makeup - gr);
    }
    void render(float *io, int frames, float sr, const float *p) {
        if (fresh || sr != sr_ || std::memcmp(p + P_RMIX, pc, sizeof pc) != 0) update(p, sr);
        reverb_gate(io, frames);
        if (master_on) master(io, frames);
    }
    // The reverb and the gate, a pair of samples at a time (the reverb runs at half the rate)
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
    // The master shaper: glue -> saturation -> tilt -> output, by chunks of 8 samples: the glue's detector runs over the chunk first,
    // then its gain goes in a line to what the chunk's level asks (a lag of 8 samples, 0.17 ms: a fast attack catches the hit)
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

extern const lab::Synth synth_snare = lab::make_synth<Voice, Fx>("Snare", kParams, {true, false, 0, 135});   // mono: a new hit chokes the
                                                                                                           // last; PLAY: a hit every beat
