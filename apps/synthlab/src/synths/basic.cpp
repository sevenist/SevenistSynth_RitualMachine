// Basic: the reference synth, and the one to copy to start a new one. One oscillator (four shapes), a resonant low pass with its own
// envelope, an amplitude envelope. docs/SYNTHS.md, "Basic".
#include "lab.h"
#include "lab_dsp.h"

namespace {

enum { P_WAVE, P_CUTOFF, P_RESO, P_FENV, P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE };

const char *const kWaves[] = {"Saw", "Square", "Tri", "Sine"};

const lab::Param kParams[] = {
    {"Wave",    0,   3,     0,    nullptr, lab::Int, kWaves},
    {"Cutoff",  40,  16000, 900,  "Hz", lab::Exp},
    {"Reso",    0,   100,   30,   "%"},
    {"F.Env",   0,   72,    30,   "st"},          // how far the filter envelope opens the cutoff, in semitones
    {"Attack",  1,   4000,  4,    "ms", lab::Exp},
    {"Decay",   5,   8000,  400,  "ms", lab::Exp},
    {"Sustain", 0,   100,   50,   "%"},
    {"Release", 5,   8000,  300,  "ms", lab::Exp},
};

struct Voice {
    lab::Osc osc;
    lab::Svf svf;
    lab::Adsr amp, fenv;
    float vel = 1.f;

    void start(const lab::Note &n, const float *) {
        amp.gate_on();
        fenv.gate_on();
        vel = 0.3f + 0.7f * n.velocity;
    }
    void release(const lab::Note &, const float *) {
        amp.gate_off();
        fenv.gate_off();
    }
    bool render(float *out, int frames, const lab::Note &n, const float *p) {
        amp.set(p[P_ATTACK], p[P_DECAY], p[P_SUSTAIN] * 0.01f, p[P_RELEASE], n.sr);
        fenv.set(p[P_ATTACK], p[P_DECAY], 0.f, p[P_RELEASE], n.sr);                 // the filter's envelope falls to nothing
        svf.set(p[P_CUTOFF] * lab::semis(p[P_FENV] * fenv.level), p[P_RESO] * 0.01f, n.sr);   // once a block: the cutoff
        const float dt = n.hz / n.sr;
        const int wave = static_cast<int>(p[P_WAVE]);
        for (int i = 0; i < frames; i++) {
            float x;
            switch (wave) {
            case 0: x = osc.saw(dt); break;
            case 1: x = osc.square(dt); break;
            case 2: x = osc.tri(dt); break;
            default: x = osc.sine(dt); break;
            }
            fenv.next();
            out[i] += svf.run(x) * amp.next() * 0.5f * vel;
        }
        svf.flush();
        return !amp.idle();
    }
};

}  // namespace

extern const lab::Synth synth_basic = lab::make_synth<Voice>("Basic", kParams);
