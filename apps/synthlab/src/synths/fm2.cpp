// FM2: two operators, a modulator with feedback into a carrier. The modulation fades with its own decay, so the attack is bright and the
// tail pure: bells, electric pianos, plucked metal. docs/SYNTHS.md, "FM2".
#include "lab.h"
#include "lab_dsp.h"

namespace {

enum { P_RATIO, P_INDEX, P_IDECAY, P_FEEDBACK, P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE };

const lab::Param kParams[] = {
    {"Ratio",   0.25f, 16,    2,    nullptr, lab::Exp},   // the modulator's frequency, times the carrier's
    {"Index",   0,     12,    4},                         // how far the modulator moves the carrier's phase, in radians
    {"I.Decay", 5,     8000,  300,  "ms", lab::Exp},      // how fast the index falls to nothing
    {"Feedbk",  0,     100,   0,    "%"},                 // the modulator on itself: towards a saw, then noise
    {"Attack",  1,     4000,  2,    "ms", lab::Exp},
    {"Decay",   5,     8000,  1200, "ms", lab::Exp},
    {"Sustain", 0,     100,   0,    "%"},
    {"Release", 5,     8000,  400,  "ms", lab::Exp},
};

struct Voice {
    float pc = 0.f, pm = 0.f;               // the phases, 0..1
    float m1 = 0.f, m2 = 0.f;               // the modulator's last two outputs (the feedback averages them: no squeal)
    lab::Adsr amp;
    lab::Decay index;
    float vel = 1.f;

    void start(const lab::Note &n, const float *) {
        pc = pm = m1 = m2 = 0.f;            // the same attack at every note
        index.trigger();
        amp.gate_on();
        vel = 0.3f + 0.7f * n.velocity;
    }
    void release(const lab::Note &, const float *) { amp.gate_off(); }
    bool render(float *out, int frames, const lab::Note &n, const float *p) {
        amp.set(p[P_ATTACK], p[P_DECAY], p[P_SUSTAIN] * 0.01f, p[P_RELEASE], n.sr);
        index.set(p[P_IDECAY], n.sr);
        const float dtc = n.hz / n.sr, dtm = dtc * p[P_RATIO];
        const float depth = p[P_INDEX] * (1.f / (2.f * lab::kPi)) * vel;    // radians -> cycles; softer notes are darker
        const float fb = p[P_FEEDBACK] * 0.004f;
        for (int i = 0; i < frames; i++) {
            const float m = lab::sin01(pm + fb * (m1 + m2));
            m2 = m1; m1 = m;
            const float c = lab::sin01(pc + depth * index.next() * m);
            pm = lab::wrap01(pm + dtm);
            pc = lab::wrap01(pc + dtc);
            out[i] += c * amp.next() * 0.5f * vel;
        }
        index.flush();
        return !amp.idle();
    }
};

}  // namespace

extern const lab::Synth synth_fm2 = lab::make_synth<Voice>("FM2", kParams);
