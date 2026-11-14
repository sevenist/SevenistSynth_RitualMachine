#pragma once
// The catalog's modules (modular.h), header only: the runner (patch.h) plays them through their descriptions (catalog.cpp), a synth
// generated from a patch (apps/studio's export) calls them directly. Each comes from a SynthLab synth or from mdsp.h; where it comes from
// is in its description (`origin`). Values arrive in io.p in the units shown (Hz, ms, %, dB), once a block.
#include <cmath>
#include <cstdint>
#include <cstring>
#include "modular/mdsp.h"
#include "modular/modular.h"

namespace mod {

// Values: the index of each in its module's description (catalog.cpp keeps the same order).
namespace P {
enum { OSC_WAVE, OSC_TUNE, OSC_FINE, OSC_PW, OSC_TRACK, OSC_LEVEL, OSC_PMOD, OSC_FM };
enum { BODY_TUNE, BODY_HARM, BODY_SHAPE, BODY_LEVEL, BODY_PMOD, BODY_TRACK, BODY_STEPS };
enum { OP_RATIO, OP_DEPTH, OP_FEEDBACK, OP_LEVEL, OP_TRACK };
enum { NOISE_LEVEL, NOISE_TONE, NOISE_SEED, NOISE_STREAM };
enum { METAL_TYPE, METAL_TUNE, METAL_INDEX, METAL_RATIO, METAL_TONE, METAL_LEVEL, METAL_TRACK };
enum { ADSR_A, ADSR_D, ADSR_S, ADSR_R };
enum { PERC_ATTACK, PERC_HOLD, PERC_DECAY, PERC_CURVE, PERC_AMOUNT, PERC_LAW, PERC_STEPS };
enum { RUM_INMIX, RUM_DECAY, RUM_PRE, RUM_FILTER, RUM_CUTOFF, RUM_RESO, RUM_ENVAMT, RUM_ENVATK, RUM_ENVDEC, RUM_POST, RUM_DELAY, RUM_FEEDBACK,
       RUM_DLYMIX, RUM_DUCK, RUM_DUCKREL, RUM_SEED };
enum { CMP_LOW, CMP_MID, CMP_HIGH, CMP_ATTACK, CMP_RELEASE };
enum { LIM_GAIN, LIM_OUT };
enum { RAT_COUNT, RAT_SPACING, RAT_ACCEL, RAT_BURST, RAT_DECAY };
enum { LFO_WAVE, LFO_RATE, LFO_RETRIG, LFO_UNIPOLAR, LFO_LEVEL };
enum { FLT_TYPE, FLT_CUTOFF, FLT_RESO, FLT_TRACK, FLT_CMOD };
enum { EQ_LOWDB, EQ_LOWHZ, EQ_MIDDB, EQ_MIDHZ, EQ_MIDQ, EQ_HIGHDB, EQ_HIGHHZ, EQ_N };
enum { DRV_DRIVE, DRV_TUBE, DRV_FOLD, DRV_SAT, DRV_CLIP, DRV_OUT };
enum { DLY_TIME, DLY_FEEDBACK, DLY_DAMP, DLY_MIX };
enum { ROOM_MIX, ROOM_DECAY, ROOM_SIZE, ROOM_DAMP };
enum { GLUE_GLUE, GLUE_ATTACK, GLUE_RELEASE, GLUE_SAT, GLUE_TILT, GLUE_OUT };
enum { GATE_DEPTH, GATE_THRESH, GATE_ATTACK, GATE_HOLD, GATE_RELEASE };
}  // namespace P

constexpr float kC4 = 261.6256f;

// A rising edge of a gate / trigger signal (crosses 0.5 upwards), sample by sample.
struct Edge {
    bool high = false;
    bool rise(float x) { const bool h = x > 0.5f; const bool r = h && !high; high = h; return r; }
    bool fall(float x) { const bool h = x > 0.5f; const bool f = !h && high; high = h; return f; }
};

/* ---------------- in / out ---------------- */

// The voice's note: its gate, a pulse when it starts, its velocity, its pitch in octaves over middle C (C4 = 0, C5 = 1).
struct NoteM {
    bool trig = false;
    void start(const Ctx &) { trig = true; }
    void process(const Ctx &c, const IO &io) {
        const float g = c.gate ? 1.f : 0.f, pitch = fast_log2(c.hz * (1.f / kC4));
        for (int i = 0; i < io.frames; i++) {
            io.out[0][i] = g;
            io.out[1][i] = (trig && i == 0) ? 1.f : 0.f;
            io.out[2][i] = c.velocity;
            io.out[3][i] = pitch;
        }
        trig = false;
    }
};

// The end of a chain: what comes in is the voice's sound (VoiceOut) or the machine's (Output). Its out is what the runner takes.
struct OutM {
    void start(const Ctx &) {}
    void process(const Ctx &, const IO &io) {
        const float g = io.p[0] * 0.01f;
        for (int i = 0; i < io.frames; i++) io.out[0][i] = io.in[0][i] * g;
    }
};

// The voices' sum, in the FX area: the runner writes it into the out.
struct VoicesM {
    void start(const Ctx &) {}
    void process(const Ctx &, const IO &) {}
};

/* ---------------- sources ---------------- */

// The classic shapes (mdsp.h Osc's, PolyBLEP), the voice's pitch (Track) or middle C, moved by Tune / Fine, by the Pitch input (octaves x
// PMod / 12: 12 = one octave per unit) and by FM (linear, through zero: the FM input times FM %, x the frequency).
struct OscM {
    float ph = 0.f;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        const int wave = static_cast<int>(p[P::OSC_WAVE] + 0.5f);
        const float base = (p[P::OSC_TRACK] >= 0.5f ? c.hz : kC4) * fast_exp2((p[P::OSC_TUNE] + p[P::OSC_FINE] * 0.01f) * (1.f / 12.f));
        const float pmod = p[P::OSC_PMOD] * (1.f / 12.f), fm = p[P::OSC_FM] * 0.01f, level = p[P::OSC_LEVEL] * 0.01f;
        const float pw = clampf(p[P::OSC_PW] * 0.01f, 0.02f, 0.98f), nyq = 0.45f;
        for (int i = 0; i < io.frames; i++) {
            float dt = base * c.inv_sr;
            if (pmod != 0.f) dt *= fast_exp2(io.in[0][i] * pmod);
            if (fm != 0.f) dt *= 1.f + io.in[1][i] * fm;
            dt = clampf(dt, -nyq, nyq);
            const float adt = dt < 0.f ? -dt : dt;
            const float t = ph;
            float y;
            switch (wave) {
            case 1: y = t < 0.5f ? 4.f * t - 1.f : 3.f - 4.f * t; break;
            case 2: y = 2.f * t - 1.f - poly_blep(t, adt); break;
            case 3: y = (t < pw ? 1.f : -1.f) + poly_blep(t, adt) - poly_blep(wrap01(t - pw + 1.f), adt); break;
            default: y = sin01(t); break;
            }
            ph = wrap01(ph + dt);
            io.out[0][i] = y * level;
        }
    }
};

// An FM operator (fm2.cpp's two operators): a sine at the voice's pitch (Track) times Ratio, its phase moved by its PM input (x Depth
// radians) and by its own last two outputs (Feedback, averaged: no squeal). Its phase starts at 0 at every note: the same attack each time.
struct OpM {
    float ph = 0.f, m1 = 0.f, m2 = 0.f;
    void start(const Ctx &) { ph = m1 = m2 = 0.f; }
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        const float dt = clampf((p[P::OP_TRACK] >= 0.5f ? c.hz : kC4) * p[P::OP_RATIO] * c.inv_sr, 0.f, 0.45f);
        const float depth = p[P::OP_DEPTH] * (1.f / (2.f * kPi)), fb = p[P::OP_FEEDBACK] * 0.004f, level = p[P::OP_LEVEL] * 0.01f;
        for (int i = 0; i < io.frames; i++) {
            const float y = sin01(ph + fb * (m1 + m2) + depth * io.in[0][i]);
            m2 = m1;
            m1 = y;
            ph = wrap01(ph + dt);
            io.out[0][i] = y * level;
        }
    }
};

// The Kick's oscillator (kick.cpp, drumlab.cpp BODY): a sine morphing to a triangle (Harm, plus the Harm input) and to a distorted sine
// (Shape). Tune is its pitch on C3 (the keys transpose it with Track on); Pitch input: octaves x PMod / 12 (a pitch envelope).
struct BodyM {
    float ph = 0.f;
    void start(const Ctx &) { ph = 0.f; }                   // (a drum starts its cycle at 0: every hit the same)
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        const float base = p[P::BODY_TUNE] * (p[P::BODY_TRACK] >= 0.5f ? c.hz * (1.f / 130.8128f) : 1.f);
        const float pmod = p[P::BODY_PMOD] * (1.f / 12.f), level = p[P::BODY_LEVEL] * 0.01f, harm = p[P::BODY_HARM] * 0.01f;
        const float shape = p[P::BODY_SHAPE] * 0.01f, sg = 1.f + 6.f * shape, sn = 1.f / soft_clip(sg);
        // Steps Block (the Kick's): the pitch at the block's two ends (the Pitch input taken as a line: a Perc in Block), its step
        // followed in a line between them
        const bool block = p[P::BODY_STEPS] >= 0.5f && io.frames > 1;
        float bdt = 0.f, bddt = 0.f;
        if (block) {
            const float n = static_cast<float>(io.frames), e0 = io.in[0][0];
            const float e1 = e0 + (io.in[0][io.frames - 1] - e0) * n / (n - 1.f);
            bdt = clampf(base * fast_exp2(pmod * e0), 5.f, c.sr * 0.45f) * c.inv_sr;
            bddt = (clampf(base * fast_exp2(pmod * e1), 5.f, c.sr * 0.45f) * c.inv_sr - bdt) / n;
        }
        for (int i = 0; i < io.frames; i++) {
            float dt;
            if (block) { dt = bdt; bdt += bddt; }
            else {
                dt = base * c.inv_sr;
                if (pmod != 0.f) dt *= fast_exp2(io.in[0][i] * pmod);
            }
            const float s = sin01(ph);
            const float h = clampf(harm + io.in[1][i], 0.f, 1.f);
            float x = s;
            if (h > 0.f) {
                const float tri = ph < 0.25f ? 4.f * ph : (ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f);
                x += (tri - s) * h;
            }
            if (shape > 0.f) x += (soft_clip(x * sg) * sn - x) * shape;
            ph = wrap01(ph + clampf(dt, 0.f, 0.45f));
            io.out[0][i] = x * level;
        }
    }
};

// White noise (mdsp.h Noise) through a one-pole low pass (Tone). Seed 0: free; any other: the same noise at every note (a drum's click
// that does not change from hit to hit).
struct NoiseM {
    Noise n;
    float lp = 0.f;
    bool reseed = false;
    void start(const Ctx &) { reseed = true; }      // (the seed is a value: applied at the note's first block)
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        const int s = static_cast<int>(p[P::NOISE_SEED] + 0.5f);
        uint32_t seed = s > 0 ? static_cast<uint32_t>(s) * 2654435761u + 0x9E3779B9u : 0u;
        if (seed && p[P::NOISE_STREAM] >= 0.5f) seed ^= 0x5BD1E995u;       // stream B: another noise from the same seed (the Kick's click)
        if (reseed && seed) { n.s = seed; lp = 0.f; }
        reseed = false;
        const float a = 1.f - fast_exp2(-9.0647f * p[P::NOISE_TONE] * c.inv_sr), level = p[P::NOISE_LEVEL] * 0.01f;   // (2 pi / ln 2)
        const bool open = p[P::NOISE_TONE] >= 0.45f * c.sr;
        for (int i = 0; i < io.frames; i++) {
            const float x = n.next();
            lp += (x - lp) * a;
            io.out[0][i] = (open ? x : lp) * level;
        }
        flush(lp);
    }
};

// The hat's metal (drumlab.cpp METAL): six squares at the classic analog hat's ratios, crossfaded by Index to the products of their pairs,
// or a two-operator FM (Ratio, Index); then a band pass of two stages at Tone.
struct MetalM {
    static constexpr float kRatios[6] = {1.f, 1.4827f, 1.8003f, 2.5461f, 2.6303f, 3.8967f};
    static constexpr float kPhases[6] = {0.f, 0.618f, 0.236f, 0.854f, 0.472f, 0.09f};
    float ph[6] = {}, pc = 0.f, pm = 0.f;
    Svf bp1, bp2;
    void start(const Ctx &) { for (int k = 0; k < 6; k++) ph[k] = kPhases[k]; pc = pm = 0.f; }
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        const float base = p[P::METAL_TUNE] * (p[P::METAL_TRACK] >= 0.5f ? c.hz * (1.f / kC4) : 1.f);
        const float index = p[P::METAL_INDEX] * 0.01f, level = p[P::METAL_LEVEL] * 0.01f * 3.5f;
        const bool fm = p[P::METAL_TYPE] >= 0.5f;
        bp1.set(p[P::METAL_TONE], 0.45f, c.sr);
        bp2.set(p[P::METAL_TONE], 0.45f, c.sr);
        float dt[6];
        for (int k = 0; k < 6; k++) dt[k] = clampf(base * kRatios[k] * c.inv_sr, 0.f, 0.45f);
        const float dm = clampf(base * p[P::METAL_RATIO] * c.inv_sr, 0.f, 0.45f);
        for (int i = 0; i < io.frames; i++) {
            float m;
            if (!fm) {
                float sq[6];
                for (int k = 0; k < 6; k++) { sq[k] = ph[k] < 0.5f ? 1.f : -1.f; ph[k] += dt[k]; if (ph[k] >= 1.f) ph[k] -= 1.f; }
                const float sum = (sq[0] + sq[1] + sq[2] + sq[3] + sq[4] + sq[5]) * (1.f / 6.f);
                const float ring = (sq[0] * sq[1] + sq[2] * sq[3] + sq[4] * sq[5]) * (1.f / 3.f);
                m = sum + (ring - sum) * index;
            } else {
                m = sin01(pc + 1.2732395f * index * sin01(pm));
                pc = wrap01(pc + dt[0]);
                pm = wrap01(pm + dm);
            }
            bp1.run(m);
            bp2.run(bp1.bp * bp1.k);
            io.out[0][i] = bp2.bp * bp2.k * level;
        }
        bp1.flush();
        bp2.flush();
    }
};

/* ---------------- envelopes and modulators ---------------- */

// ADSR, analog style (mdsp.h Adsr), on its Gate input (by default the voice's gate).
struct AdsrM {
    Adsr env;
    Edge g;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        env.set(io.p[P::ADSR_A], io.p[P::ADSR_D], io.p[P::ADSR_S] * 0.01f, io.p[P::ADSR_R], c.sr);
        for (int i = 0; i < io.frames; i++) {
            const float x = io.in[0][i];
            const bool was = g.high;
            if (g.rise(x)) env.gate_on();
            else if (was && !g.high) env.gate_off();
            io.out[0][i] = env.next();
        }
    }
};

// The percussive envelope (kick.cpp's amp and pitch envelopes): on a Trig (by default the note's start), Attack up (a line), Hold, then
// Decay down; times Amount. Law Blend: from a line (Curve 0 %) to an exponential (100 %, -60 dB at Decay); Law Power: (1 - u)^(1 + 7 x
// Curve), the Kick's amp envelope (0 %: a line; 100 %: a fast drop and a long tail). Steps Sample: computed at every sample; Block:
// computed at the block's two ends and followed in a line between them (the Kick's cheap envelopes; a trigger inside a block counts from
// where it falls, the block's start before it reads 0).
struct PercM {
    float t = 1e9f;                                         // samples since the trigger (far: silent)
    Edge e;
    float att = 0.f, hold = 0.f, inv_att = 0.f, inv_dec = 1.f, curve = 0.f, k = 1.f;
    bool power = false;
    float at(float tt) const {                              // the envelope tt samples after the trigger (0 before it)
        if (tt < 0.f) return 0.f;
        if (tt < att) return tt * inv_att;
        if (tt < att + hold) return 1.f;
        const float u = (tt - att - hold) * inv_dec;
        const float lin = u < 1.f ? 1.f - u : 0.f;
        if (power) return u >= 1.f ? 0.f : fast_exp2(k * fast_log2(1.f - u));
        return curve <= 0.f ? lin : lin + (fast_exp2(-9.966f * u) - lin) * curve;
    }
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float ms = c.sr * 0.001f, dec = io.p[P::PERC_DECAY] * ms;
        att = io.p[P::PERC_ATTACK] * ms;
        hold = io.p[P::PERC_HOLD] * ms;
        inv_att = att > 0.5f ? 1.f / att : 0.f;
        inv_dec = 1.f / (dec > 1.f ? dec : 1.f);
        curve = io.p[P::PERC_CURVE] * 0.01f;
        power = io.p[P::PERC_LAW] >= 0.5f;
        k = 1.f + 7.f * curve;                              // Power: (1 - u)^k, the Kick's amp envelope (kick.cpp amp_at)
        const float amount = io.p[P::PERC_AMOUNT] * 0.01f;
        if (io.p[P::PERC_STEPS] >= 0.5f) {                  // Block
            for (int i = 0; i < io.frames; i++) if (e.rise(io.in[0][i])) t = -static_cast<float>(i);
            const float y0 = at(t), y1 = at(t + static_cast<float>(io.frames)), dy = (y1 - y0) / static_cast<float>(io.frames);
            for (int i = 0; i < io.frames; i++) io.out[0][i] = (y0 + dy * static_cast<float>(i)) * amount;
            if (t < 1e9f) t += static_cast<float>(io.frames);
            return;
        }
        for (int i = 0; i < io.frames; i++) {
            if (e.rise(io.in[0][i])) t = 0.f;
            io.out[0][i] = at(t) * amount;
            if (t < 1e9f) t += 1.f;
        }
    }
};

// The Snare's wires (snare.cpp, drumlab.cpp NOISE): on a Trig, Count bursts, Spacing apart, each gap Accel times the last; each burst falls
// in Burst, the last one in Decay. An envelope: multiply a noise by it (a VCA).
struct RatchetM {
    static constexpr int kMax = 8;
    float en = 0.f;
    int burst = 0, left = 0;
    int gaps[kMax] = {};
    Edge e;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p, ms = c.sr * 0.001f;
        const int count = static_cast<int>(clampf(p[P::RAT_COUNT] + 0.5f, 1.f, static_cast<float>(kMax)));
        const float acc = fast_exp2(p[P::RAT_ACCEL] * 0.01f);
        float gap = p[P::RAT_SPACING] * ms;
        for (int k = 0; k < kMax; k++) { gaps[k] = gap < 1.f ? 1 : static_cast<int>(gap); gap *= acc; }
        const float kburst = fast_exp2(-6.644f / (p[P::RAT_BURST] * ms)), ktail = fast_exp2(-6.644f / (p[P::RAT_DECAY] * ms));
        for (int i = 0; i < io.frames; i++) {
            if (e.rise(io.in[0][i])) { burst = 0; en = 1.f; left = count > 1 ? gaps[0] : 0; }
            if (left > 0 && --left == 0) { burst++; en = 1.f; left = burst < count - 1 ? gaps[burst] : 0; }
            io.out[0][i] = en;
            en *= left > 0 ? kburst : ktail;
        }
        flush(en);
    }
};

// A low frequency oscillator: sine, triangle, saw up / down, square, sample and hold; bipolar (-1..1) or unipolar (0..1); restarted by
// its Trig input when Retrig is on (by default: the note's start).
struct LfoM {
    float ph = 0.f, sh = 0.f;
    Noise n;
    Edge e;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        const int wave = static_cast<int>(p[P::LFO_WAVE] + 0.5f);
        const float dt = p[P::LFO_RATE] * c.inv_sr, level = p[P::LFO_LEVEL] * 0.01f;
        const bool retrig = p[P::LFO_RETRIG] >= 0.5f, uni = p[P::LFO_UNIPOLAR] >= 0.5f;
        for (int i = 0; i < io.frames; i++) {
            if (e.rise(io.in[0][i]) && retrig) { ph = 0.f; sh = n.next(); }
            float y;
            switch (wave) {
            case 1: y = ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph; break;
            case 2: y = 2.f * ph - 1.f; break;
            case 3: y = 1.f - 2.f * ph; break;
            case 4: y = ph < 0.5f ? 1.f : -1.f; break;
            case 5: y = sh; break;
            default: y = sin01(ph); break;
            }
            ph += dt;
            if (ph >= 1.f) { ph -= 1.f; sh = n.next(); }
            io.out[0][i] = (uni ? 0.5f * y + 0.5f : y) * level;
        }
    }
};

/* ---------------- filters ---------------- */

// The state-variable filter (mdsp.h Svf): low pass 12 / 24 dB, band pass, high pass 12 / 24 dB, notch. Cutoff follows the key by Track
// (100 %: an octave per octave, from middle C) and the Cutoff input by CMod octaves per unit (an envelope's sweep), once a block, at the
// block's first sample.
struct FilterM {
    Svf a, b;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        const int type = static_cast<int>(p[P::FLT_TYPE] + 0.5f);
        // the Cutoff input at the block's first sample, as SynthLab's synths set their filters once a block (basic.cpp: the rebuild nulls
        // at 74 dB this way, 53 dB with the block's mean)
        const float cv = io.in[1][0];
        const float oct = fast_log2(c.hz * (1.f / kC4)) * p[P::FLT_TRACK] * 0.01f + cv * p[P::FLT_CMOD];
        const float hz = clampf(p[P::FLT_CUTOFF] * fast_exp2(oct), 10.f, c.sr * 0.45f);
        const float g = fast_tan(kPi * clampf(hz * c.inv_sr, 0.f, 0.33f));
        const float res = p[P::FLT_RESO] * 0.01f;
        const bool four = type == 1 || type == 4;
        a.set_g(g, four ? res * 0.7f : res);
        if (four) b.set_g(g, res * 0.7f);
        for (int i = 0; i < io.frames; i++) {
            const float x = io.in[0][i];
            float y;
            a.run(x);
            switch (type) {
            case 1: b.run(a.lp); y = b.lp; break;
            case 2: y = a.bp * a.k; break;
            case 3: y = a.hp; break;
            case 4: b.run(a.hp); y = b.hp; break;
            case 5: y = a.lp + a.hp; break;
            default: y = a.lp; break;
            }
            io.out[0][i] = y;
        }
        a.flush();
        b.flush();
    }
};

// A 3-band EQ (mdsp.h Eq3: a low shelf, a bell, a high shelf), each band skipped at 0 dB.
struct Eq3M {
    Eq3 eq;
    float pc[P::EQ_N] = {}, sr_ = 0.f;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        if (sr_ != c.sr || std::memcmp(pc, p, sizeof pc) != 0) {
            eq.set(p[P::EQ_LOWHZ], p[P::EQ_LOWDB], p[P::EQ_MIDHZ], p[P::EQ_MIDDB], p[P::EQ_MIDQ], p[P::EQ_HIGHHZ], p[P::EQ_HIGHDB], c.sr);
            std::memcpy(pc, p, sizeof pc);
            sr_ = c.sr;
        }
        for (int i = 0; i < io.frames; i++) io.out[0][i] = eq.run(io.in[0][i]);
        eq.flush();
    }
};

/* ---------------- shapers and utilities ---------------- */

// The Kick's drive (kick.cpp, drumlab.cpp DRIVE): Tube (asymmetric: even harmonics), Fold (a sine wavefolder), Sat (soft saturation at
// Drive), Clip, each mixed in by its amount; then the DC taken out and the Out level.
struct DriveM {
    DcBlock dc;
    void start(const Ctx &) {}
    void process(const Ctx &, const IO &io) {
        const float *p = io.p;
        const float g = fast_db(p[P::DRV_DRIVE]), tube = p[P::DRV_TUBE] * 0.01f, bias = 0.3f * tube, bias_y = soft_clip(bias);
        const float tube_n = 1.f / (soft_clip(g + bias) - bias_y), sat = p[P::DRV_SAT] * 0.01f, sat_n = 1.f / soft_clip(g);
        const float fold = p[P::DRV_FOLD] * 0.01f, fold_g = 1.f + 3.f * fold, clip = p[P::DRV_CLIP] * 0.01f;
        const float clip_t = 1.f - 0.75f * clip, inv_clip_t = 1.f / clip_t, out = fast_db(p[P::DRV_OUT]);
        for (int i = 0; i < io.frames; i++) {
            float y = io.in[0][i];
            if (tube > 0.f) y += ((soft_clip(y * g + bias) - bias_y) * tube_n - y) * tube;
            if (fold > 0.f) y += (sin01(0.25f * y * fold_g) - y) * fold;
            if (sat > 0.f) y += (soft_clip(y * g) * sat_n - y) * sat;
            if (clip > 0.f) y += (clampf(y, -clip_t, clip_t) * inv_clip_t - y) * clip;
            io.out[0][i] = dc.run(y) * out;
        }
        dc.flush();
    }
};

// In x CV x Level (CV: 1 when nothing comes in).
struct VcaM {
    void start(const Ctx &) {}
    void process(const Ctx &, const IO &io) {
        const float l = io.p[0] * 0.01f;
        for (int i = 0; i < io.frames; i++) io.out[0][i] = io.in[0][i] * io.in[1][i] * l;
    }
};

// Four inputs, each at its level (-100..100 %: a negative level inverts).
struct MixerM {
    void start(const Ctx &) {}
    void process(const Ctx &, const IO &io) {
        const float l[4] = {io.p[0] * 0.01f, io.p[1] * 0.01f, io.p[2] * 0.01f, io.p[3] * 0.01f};
        for (int i = 0; i < io.frames; i++)
            io.out[0][i] = io.in[0][i] * l[0] + io.in[1][i] * l[1] + io.in[2][i] * l[2] + io.in[3][i] * l[3];
    }
};

// A x B: ring modulation, or one signal scaled by another.
struct RingM {
    void start(const Ctx &) {}
    void process(const Ctx &, const IO &io) { for (int i = 0; i < io.frames; i++) io.out[0][i] = io.in[0][i] * io.in[1][i]; }
};

// A steady value (-1..1): an offset for a modulation, a level to send somewhere.
struct ConstM {
    void start(const Ctx &) {}
    void process(const Ctx &, const IO &io) { for (int i = 0; i < io.frames; i++) io.out[0][i] = io.p[0]; }
};

// The Kick's 3-band compressor (kick.cpp): split at 120 Hz and 2.5 kHz (two SVFs, the mid is the rest); each amount lowers its band's
// threshold (0 .. -36 dB) and raises its ratio (1 .. 8), 40 % of the threshold given back; the gains computed once a block from the
// peak detectors and followed in a line over it.
struct Comp3M {
    Svf lo, hi;
    float det[3] = {}, gain[3] = {1.f, 1.f, 1.f}, sr_ = 0.f;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        if (sr_ != c.sr) { lo.set(120.f, 0.f, c.sr); hi.set(2500.f, 0.f, c.sr); sr_ = c.sr; }
        const float amounts[3] = {p[P::CMP_LOW] * 0.01f, p[P::CMP_MID] * 0.01f, p[P::CMP_HIGH] * 0.01f};
        bool on[3], any = false;
        float thr[3], slope[3], makeup[3];
        for (int b = 0; b < 3; b++) {
            on[b] = amounts[b] >= 0.005f;
            any = any || on[b];
            thr[b] = -36.f * amounts[b];
            slope[b] = 1.f - 1.f / (1.f + 7.f * amounts[b]);
            makeup[b] = -thr[b] * slope[b] * 0.4f;
        }
        if (!any) {
            for (float &g : gain) g = 1.f;
            for (int i = 0; i < io.frames; i++) io.out[0][i] = io.in[0][i];
            return;
        }
        const float kat = 1.f - decay_coef(p[P::CMP_ATTACK], c.sr), krel = 1.f - decay_coef(p[P::CMP_RELEASE], c.sr);
        const float inv_n = 1.f / static_cast<float>(io.frames);
        float dg[3];
        for (int b = 0; b < 3; b++) {
            float target = 1.f;
            if (on[b]) {
                const float over = fast_to_db(det[b] + 1e-6f) - thr[b];
                target = fast_db(makeup[b] - (over > 0.f ? over * slope[b] : 0.f));
            }
            dg[b] = (target - gain[b]) * inv_n;
        }
        for (int i = 0; i < io.frames; i++) {
            const float y = io.in[0][i];
            const float l = lo.run(y);
            hi.run(y);
            const float h = hi.hp, band[3] = {l, y - l - h, h};
            float out = 0.f;
            for (int b = 0; b < 3; b++) {
                const float e = std::fabs(band[b]);
                det[b] += (e - det[b]) * (e > det[b] ? kat : krel);
                out += band[b] * gain[b];
                gain[b] += dg[b];
            }
            io.out[0][i] = out;
        }
        lo.flush();
        hi.flush();
        for (float &d : det) flush(d);
    }
};

// A soft limit (the Kick's last step): soft_clip(In x Gain) x Out.
struct LimitM {
    void start(const Ctx &) {}
    void process(const Ctx &, const IO &io) {
        const float g = fast_db(io.p[P::LIM_GAIN]), out = io.p[P::LIM_OUT] * 0.01f;
        for (int i = 0; i < io.frames; i++) io.out[0][i] = soft_clip(io.in[0][i] * g) * out;
    }
};

// The Kick's rumble (kick.cpp rumble_tick), whole: noise and the kick (its In: the oscillator before the amp envelope) crossfaded by In Mix,
// Pre (a soft clip), a resonant filter (8 types, its own envelope), Post, its decay, an echo, ducked under the body (its Key: the amp
// envelope). It runs at an eighth of the sample rate (its filter stops at 500 Hz): In averaged over 8 samples, its output a line between
// two ticks. Vel scales the kick and the noise (the Kick's velocity curve). Its level: a VCA after it. The noise is seeded at each note.
struct RumbleM {
    static constexpr int kDecim = 8, kDlyMax = 1536;
    Svf lp, lp2;
    float op_s = 0.f, op_g = 0.f;
    Noise noise;
    int16_t dline[kDlyMax] = {};
    int dw = 0;
    float duckf = 0.f, nenv = 0.f, t = 0.f;
    int rphase = 0;
    float kacc = 0.f, rprev = 0.f, rcur = 0.f;
    // what the values decide (each block)
    int ftype = 1, dlen = 1;
    float nmakeup = 1.f, nin_noise = 1.f, nin_kick = 0.f, npre = 0.f, npre_g = 1.f, npre_n = 1.f, npost = 0.f, npost_g = 1.f, npost_n = 1.f;
    float knoise = 0.f, dmix = 0.f, dfb = 0.f, duck = 0.f, kduck = 0.f;
    bool reseed = false;

    void set_cutoff(float hz, float res, float tr) {
        const float f = clampf(hz, 10.f, 0.33f * tr);
        const float g = fast_tan(kPi * f / tr);
        lp.set_g(g, res);
        if (ftype == 2 || ftype == 5) lp2.set_g(g, 0.30831f);
        if (ftype == 0) op_g = g / (1.f + g);
    }
    float filter(float x) {
        switch (ftype) {
        case 0: { const float v = (x - op_s) * op_g, y = v + op_s; op_s = y + v; return y; }
        case 1: return lp.run(x);
        case 2: return lp2.run(lp.run(x));
        case 3: lp.run(x); return lp.bp * lp.k;
        case 4: lp.run(x); return lp.hp;
        case 5: lp.run(x); lp2.run(lp.hp); return lp2.hp;
        case 6: lp.run(x); return x - lp.k * lp.bp;
        default: lp.run(x); return lp.lp - lp.hp;
        }
    }
    float tick(float kick, bool noise_on, float vel, float a) {
        float r = 0.f;
        if (noise_on) {
            float src = 0.f;
            if (nin_noise > 0.f) {
                float nz = noise.next();
                if (npre > 0.f) nz += (soft_clip(nz * npre_g) * npre_n - nz) * npre;
                src += nz * nin_noise;
            }
            if (nin_kick > 0.f) {
                if (npre > 0.f) kick += (soft_clip(kick * npre_g) * npre_n - kick) * npre;
                src += kick * nin_kick;
            }
            r = filter(src) * nmakeup;
            if (npost > 0.f) r += (soft_clip(r * npost_g) * npost_n - r) * npost;
            r *= nenv * vel;
            nenv *= knoise;
        }
        if (dmix > 0.f) {
            int rd = dw - dlen;
            if (rd < 0) rd += kDlyMax;
            const float back = static_cast<float>(dline[rd]) * (1.f / 8192.f);
            const float v = (r + dfb * back) * 8192.f;
            dline[dw] = static_cast<int16_t>(v > 32767.f ? 32767.f : (v < -32767.f ? -32767.f : v));
            if (++dw >= kDlyMax) dw = 0;
            r += (back - r) * dmix;
        }
        if (duck > 0.f) {
            const float key = a * 4.f < 1.f ? a * 4.f : 1.f;
            duckf = key > duckf ? key : duckf * kduck;
            r *= 1.f - duck * duckf;
        }
        return r;
    }
    void start(const Ctx &) {
        reseed = true;                                  // (the seed is a value: applied at the note's first block)
        lp.ic1 = lp.ic2 = lp2.ic1 = lp2.ic2 = op_s = 0.f;
        nenv = 1.f;
        rphase = 0;
        kacc = 0.f;
        t = 0.f;
    }
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p, ms = c.sr * 0.001f, tr = c.sr / kDecim;
        if (reseed) { noise.s = static_cast<uint32_t>(p[P::RUM_SEED]) * 2654435761u + 0x9E3779B9u; reseed = false; }
        ftype = static_cast<int>(p[P::RUM_FILTER] + 0.5f);
        const float ncut = p[P::RUM_CUTOFF], nq = 0.5f * fast_exp2(4.3219f * p[P::RUM_RESO] * 0.01f), nres = (2.f - 1.f / nq) / 1.9f;
        knoise = fast_exp2(-6.644f * kDecim / (p[P::RUM_DECAY] * ms));
        set_cutoff(ncut, nres, tr);
        const float lp12 = sqrtf(0.5f * tr / ncut);
        nmakeup = ftype == 0 ? lp12 * 0.84f : ftype <= 2 ? lp12 : ftype == 3 ? lp12 / sqrtf(1.571f * lp.k) : 1.f;
        const float nmix = p[P::RUM_INMIX] * 0.01f;
        nin_noise = 2.f * (1.f - nmix) < 1.f ? 2.f * (1.f - nmix) : 1.f;
        nin_kick = (2.f * nmix < 1.f ? 2.f * nmix : 1.f) / nmakeup;
        npre = p[P::RUM_PRE] * 0.01f; npre_g = 1.f + 7.f * npre; npre_n = 1.f / soft_clip(npre_g);
        npost = p[P::RUM_POST] * 0.01f; npost_g = 1.f + 7.f * npost; npost_n = 1.f / soft_clip(npost_g);
        dmix = p[P::RUM_DLYMIX] * 0.01f;
        dfb = p[P::RUM_FEEDBACK] * 0.01f;
        duck = p[P::RUM_DUCK] * 0.01f;
        kduck = decay_coef(p[P::RUM_DUCKREL], tr);
        dlen = static_cast<int>(p[P::RUM_DELAY] * c.sr * (0.001f / kDecim));
        dlen = dlen < 1 ? 1 : (dlen > kDlyMax - 1 ? kDlyMax - 1 : dlen);
        // the filter envelope: the cutoff set once a block, for the block's middle
        const float foct = p[P::RUM_ENVAMT];
        const bool noise_on = nenv > 1e-4f;
        if ((foct > 0.005f || foct < -0.005f) && noise_on) {
            const float fatt = p[P::RUM_ENVATK] * ms, fk = -6.644f / (p[P::RUM_ENVDEC] * ms), tt = t + 0.5f * static_cast<float>(io.frames);
            const float env = tt < fatt ? tt / fatt : fast_exp2(fk * (tt - fatt));
            set_cutoff(ncut * fast_exp2(foct * env), nres, tr);
        }
        const bool on = noise_on || dmix > 0.f || rcur != 0.f || rprev != 0.f;
        for (int i = 0; i < io.frames; i++) {
            float y = 0.f;
            if (on) {
                kacc += io.in[0][i];
                if (++rphase >= kDecim) {
                    rphase = 0;
                    rprev = rcur;
                    rcur = tick(kacc * (io.in[2][i] / kDecim), noise_on, io.in[2][i], io.in[1][i]);
                    kacc = 0.f;
                }
                y = rprev + (rcur - rprev) * static_cast<float>(rphase) * (1.f / kDecim);
            }
            io.out[0][i] = y;
        }
        t += static_cast<float>(io.frames);
        flush(duckf); flush(rprev); flush(rcur); flush(op_s);
        lp.flush();
        lp2.flush();
    }
};

/* ---------------- effects ---------------- */

// An echo (the Kick's rumble echo, drumlab.cpp): a 16-bit line of up to 1.3 s at 48 kHz, a one-pole low pass in the loop (Damp), Mix.
struct DelayM {
    static constexpr int kLen = 65536;
    int16_t line[kLen] = {};
    int w = 0;
    float lp = 0.f;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        int d = static_cast<int>(p[P::DLY_TIME] * 0.001f * c.sr);
        d = d < 1 ? 1 : (d > kLen - 1 ? kLen - 1 : d);
        const float fb = p[P::DLY_FEEDBACK] * 0.01f, mix = p[P::DLY_MIX] * 0.01f;
        const float a = 1.f - fast_exp2(-9.0647f * clampf(p[P::DLY_DAMP], 100.f, 0.45f * c.sr) * c.inv_sr);
        for (int i = 0; i < io.frames; i++) {
            int r = w - d;
            if (r < 0) r += kLen;
            const float wet = static_cast<float>(line[r]) * (1.f / 16384.f);
            lp += (wet - lp) * a;
            const float x = io.in[0][i];
            const float v = (x + lp * fb) * 16384.f;
            line[w] = static_cast<int16_t>(v > 32767.f ? 32767.f : (v < -32767.f ? -32767.f : v));
            if (++w >= kLen) w = 0;
            io.out[0][i] = x + (wet - x) * mix;
        }
        flush(lp);
    }
};

// The Kick's reverb (kick.cpp Room): four 16-bit delay lines fed back through a 4 x 4 Hadamard matrix, a one-pole low pass in each loop,
// run at half the sample rate (each pair of samples averaged in, the output drawn in a line between two ticks). Lean, not lush.
struct RoomM {
    static constexpr int L0 = 1201, L1 = 1427, L2 = 1709, L3 = 2003;
    static constexpr int kMaxLen[4] = {L0, L1, L2, L3};
    int16_t l0[L0] = {}, l1[L1] = {}, l2[L2] = {}, l3[L3] = {};
    int w[4] = {}, len[4] = {L0, L1, L2, L3};
    float lp[4] = {}, fb = 0.f, damp = 1.f, prev = 0.f, half = 0.f;
    int odd = 0;
    float pc[4] = {}, sr_ = 0.f;
    static float rd(const int16_t *line, int wr, int n, int max) {
        int r = wr - n;
        if (r < 0) r += max;
        return static_cast<float>(line[r]) * (1.f / 16384.f);
    }
    static void wr(int16_t *line, int &wp, int max, float v) {
        const float x = v * 16384.f;
        line[wp] = static_cast<int16_t>(x > 32767.f ? 32767.f : (x < -32767.f ? -32767.f : x));
        if (++wp >= max) wp = 0;
    }
    void set(float size, float decay_ms, float damp01, float rate) {
        damp = 1.f - 0.9f * damp01;
        float avg = 0.f;
        for (int k = 0; k < 4; k++) {
            len[k] = static_cast<int>(static_cast<float>(kMaxLen[k]) * size);
            if (len[k] < 4) len[k] = 4;
            avg += 0.25f * static_cast<float>(len[k]);
        }
        fb = fast_exp2(-9.966f * avg / (decay_ms * 0.001f * rate));
    }
    float tick(float in) {
        const float r0 = rd(l0, w[0], len[0], L0), r1 = rd(l1, w[1], len[1], L1);
        const float r2 = rd(l2, w[2], len[2], L2), r3 = rd(l3, w[3], len[3], L3);
        const float a = r0 + r1, b = r0 - r1, cc = r2 + r3, e = r2 - r3;
        const float m[4] = {(a + cc) * 0.5f, (b + e) * 0.5f, (a - cc) * 0.5f, (b - e) * 0.5f};
        for (int k = 0; k < 4; k++) lp[k] += (m[k] - lp[k]) * damp;
        wr(l0, w[0], L0, in + fb * lp[0]);
        wr(l1, w[1], L1, -in + fb * lp[1]);
        wr(l2, w[2], L2, in + fb * lp[2]);
        wr(l3, w[3], L3, -in + fb * lp[3]);
        return (r0 - r1 + r2 - r3) * 0.35f;
    }
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        if (sr_ != c.sr || std::memcmp(pc, p, sizeof pc) != 0) {
            set(clampf(p[P::ROOM_SIZE] * 0.01f, 0.05f, 1.f), p[P::ROOM_DECAY], p[P::ROOM_DAMP] * 0.01f, c.sr * 0.5f);
            std::memcpy(pc, p, sizeof pc);
            sr_ = c.sr;
        }
        const float mix = p[P::ROOM_MIX] * 0.01f;
        // by pairs, as the Kick's Fx: a tick on the pair's mean, the first sample gets the line between the last tick and this one
        int i = 0;
        if (odd && io.frames > 0) {                         // (a pair split by the block before: finish it)
            const float x = io.in[0][0];
            const float wet = tick((half + x) * 0.5f);
            io.out[0][0] = x + wet * mix;
            prev = wet;
            odd = 0;
            i = 1;
        }
        for (; i + 1 < io.frames; i += 2) {
            const float x0 = io.in[0][i], x1 = io.in[0][i + 1];
            const float wet = tick((x0 + x1) * 0.5f);
            io.out[0][i] = x0 + (prev + wet) * 0.5f * mix;
            io.out[0][i + 1] = x1 + wet * mix;
            prev = wet;
        }
        if (i < io.frames) {                                // (an odd block: its last sample opens the next pair)
            half = io.in[0][i];
            io.out[0][i] = half + prev * mix;
            odd = 1;
        }
        for (float &x : lp) flush(x);
    }
};

// The Kick's master shaper (kick.cpp Fx): a glue compressor (a peak detector, the gain every 8 samples with a soft knee, a quarter of
// makeup), a soft saturation, a tilt (lows against highs around 800 Hz), the output level. Each part skipped at zero.
struct GlueM {
    float det = 0.f, gain = 1.f, tilt_lp = 0.f;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p;
        const float glue = p[P::GLUE_GLUE] * 0.01f, tilt = p[P::GLUE_TILT] * 0.01f, sat = p[P::GLUE_SAT] * 0.01f;
        const bool glue_on = glue >= 0.005f, tilt_on = tilt >= 0.005f || tilt <= -0.005f;
        const float thr = -30.f * glue, slope = 1.f - 1.f / (1.f + 3.f * glue), makeup = -thr * slope * 0.25f;
        const float kat = 1.f - decay_coef(p[P::GLUE_ATTACK], c.sr), krel = 1.f - decay_coef(p[P::GLUE_RELEASE], c.sr);
        const float sat_g = 1.f + 7.f * sat, sat_n = 1.f / sqrtf(sat_g);
        const float tilt_a = 1.f - fast_exp2(-9.0647f * 800.f * c.inv_sr), lo = fast_db(-6.f * tilt), hi = fast_db(6.f * tilt);
        const float out = fast_db(p[P::GLUE_OUT]);
        if (!glue_on) gain = 1.f;
        for (int k = 0; k < io.frames; k += 8) {
            const int m = io.frames - k < 8 ? io.frames - k : 8;
            float g = gain, dg = 0.f;
            if (glue_on) {
                for (int i = k; i < k + m; i++) { const float e = std::fabs(io.in[0][i]); det += (e - det) * (e > det ? kat : krel); }
                const float over = fast_to_db(det + 1e-6f) - thr;
                const float gr = over <= -3.f ? 0.f : (over >= 3.f ? over * slope : slope * (over + 3.f) * (over + 3.f) * (1.f / 12.f));
                gain = fast_db(makeup - gr);
                dg = (gain - g) / static_cast<float>(m);
            }
            for (int i = k; i < k + m; i++) {
                float x = io.in[0][i];
                if (glue_on) { g += dg; x *= g; }
                if (sat > 0.f) x += (soft_clip(x * sat_g) * sat_n - x) * sat;
                if (tilt_on) { tilt_lp += (x - tilt_lp) * tilt_a; x = tilt_lp * lo + (x - tilt_lp) * hi; }
                io.out[0][i] = x * out;
            }
        }
        flush(det);
        flush(tilt_lp);
    }
};

// The Snare's gate (snare.cpp Fx): a peak follower of its Key (by default its In), open above Threshold, closed 3 dB under it, held open
// Hold after the key falls; its gain goes to 1 in Attack and to 1 - Depth in Release.
struct GateM {
    float det = 0.f, gain = 1.f;
    bool open = false;
    int held = 0;
    void start(const Ctx &) {}
    void process(const Ctx &c, const IO &io) {
        const float *p = io.p, ms = c.sr * 0.001f;
        const float depth = p[P::GATE_DEPTH] * 0.01f, floor_ = 1.f - depth;
        const float thr_open = fast_db(p[P::GATE_THRESH]), thr_close = thr_open * 0.70795f, kdet = decay_coef(10.f, c.sr);
        const float katt = 1.f - fast_exp2(-6.644f / (p[P::GATE_ATTACK] * ms)), krel = 1.f - fast_exp2(-6.644f / (p[P::GATE_RELEASE] * ms));
        const int hold = static_cast<int>(p[P::GATE_HOLD] * ms);
        for (int i = 0; i < io.frames; i++) {
            const float x = io.in[0][i];
            if (depth < 0.005f) { io.out[0][i] = x; continue; }
            const float e = std::fabs(io.in[1][i]);
            det = e > det ? e : det * kdet;
            if (det > (open ? thr_close : thr_open)) { open = true; held = hold; }
            else if (held > 0) held--;
            else open = false;
            const float target = open ? 1.f : floor_;
            gain += (target - gain) * (target > gain ? katt : krel);
            io.out[0][i] = x * gain;
        }
        flush(det);
    }
};

}  // namespace mod
