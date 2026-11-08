// TechnoGen's track module, first version (tg_tracks.h): a placeholder sound per track, a few lines of float DSP each. Not tuned, not
// measured on the board. What each of the seven values does here is a guess at the final engine's meaning, so the dice already moves
// something audible; values the placeholder has no use for yet do nothing.
#include "tg_tracks.h"
#include <cmath>
#include "engine/dsp/config.h"
#include "engine/dsp/q.h"
#include "tg_model.h"

namespace tg {
namespace {

constexpr float kSr = static_cast<float>(sc::kSampleRate);
constexpr float kOutGain = 0.32f;                       // a track at full level and full scale: about -10 dBFS, seven of them rarely clip

// A sine from a phase 0..1 (a shaped triangle: -1 at 0, 0 at 0.25, 1 at 0.5). Cheap and smooth enough for drums and basses.
inline float fsin(float ph) {
    const float x = ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph;
    return x * (1.5f - 0.5f * x * x);
}
inline float wrap(float ph) { return ph - static_cast<float>(static_cast<int>(ph)); }
inline float soft(float x) {                            // a soft clip (tanh-like), flat beyond +-3
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}
inline float decay_k(float ms) { return expf(-1000.f / (ms * kSr)); }       // the per-sample multiplier that falls by 1/e in `ms`
inline float svf_f(float hz) {
    if (hz > kSr * 0.16f) hz = kSr * 0.16f;
    if (hz < 20.f) hz = 20.f;
    return 2.f * sinf(3.14159265f * hz / kSr);
}
inline float semis(float s) { return exp2f(s / 12.f); }

struct Svf {                                            // Chamberlin state-variable filter: low and band outputs
    float lp = 0, bp = 0;
    float run(float in, float f, float q) {
        lp += f * bp;
        const float hp = in - lp - q * bp;
        bp += f * hp;
        return lp;
    }
    float high(float in, float f, float q) {
        lp += f * bp;
        const float hp = in - lp - q * bp;
        bp += f * hp;
        return hp;
    }
};

struct Voice {
    bool on = false;
    float amp = 0;                                      // the hit's level
    float env = 0, env_k = 0;                           // the amplitude envelope (a decay), and the pad's / text's gate
    float nenv = 0, nenv_k = 0;                         // a second decay: the noise, the click, the bass's filter
    float pe = 0, pe_k = 0, sweep = 1;                  // a pitch envelope: pitch x (1 + (sweep - 1) x pe)
    float ph[3] = {}, inc[3] = {};
    float idx = 0, idx_k = 0;                           // the FM index (CLANK)
    int burst = 0, burst_n = 0;                         // the clap's bursts still to come, samples to the next
    bool gate = false, accent = false;
    Svf svf;
};

class TrackModule : public sc::Module {
public:
    const sc::ModuleInfo &info() const override {
        static const sc::ModuleInfo i = {"TgTrack", sc::Scope::Global, 2, 2, TP_COUNT, false, {"L", "R"}, {"L", "R"},
            {{"v1", 50, 0, 100}, {"v2", 50, 0, 100}, {"v3", 50, 0, 100}, {"v4", 50, 0, 100}, {"v5", 50, 0, 100}, {"v6", 50, 0, 100},
             {"v7", 50, 0, 100}, {"level", 60, 0, 100}, {"kind", 0, 0, kTracks - 1}}};
        return i;
    }
    void set_param(int idx, int32_t v) override {
        const int c = v < 0 ? 0 : (v > 100 ? 100 : static_cast<int>(v));
        if (idx >= TP_V0 && idx < TP_V0 + kParams) val_[idx] = static_cast<float>(c);
        else if (idx == TP_LEVEL) level_ = static_cast<float>(c) * 0.01f;
        else if (idx == TP_KIND) kind_ = c < kTracks ? c : kTracks - 1;
    }
    void hit(int lane, int level, bool release) override {
        if (lane < 0 || lane >= kLanes) return;
        if (release) { v_[lane].gate = false; return; }
        trigger(lane, static_cast<float>(level + 1) / 16.f, level >= 15);
    }
    SC_HOT void process(const sc::ProcessCtx &ctx, const sc::Ports &p) override {
        float mix[sc::kBlock] = {};
        const int n = ctx.frames;
        switch (kind_) {
        case T_KICK: kick(mix, n); break;
        case T_BASS: bass(mix, n); break;
        case T_DRUM: drum(mix, n); break;
        case T_PERC: perc(mix, n); break;
        case T_PAD: pad(mix, n); break;
        case T_TEXT: text(mix, n, ctx.transport && ctx.transport->running); break;
        default: samp(mix, n); break;
        }
        const float g = level_ * kOutGain * 32767.f;
        for (int i = 0; i < n; i++) {
            const int32_t s = static_cast<int32_t>(mix[i] * g);
            p.out[0][i] = sc::sat16(p.in[0][i] + s);
            p.out[1][i] = sc::sat16(p.in[1][i] + s);
        }
    }

private:
    float noise() { rng_ = rng_ * 1664525u + 1013904223u; return static_cast<float>(static_cast<int32_t>(rng_)) * (1.f / 2147483648.f); }
    float v(int k) const { return val_[k]; }

    void trigger(int lane, float amp, bool accent) {
        Voice &o = v_[lane];
        o.on = true;
        o.amp = amp;
        o.accent = accent;
        o.gate = true;
        switch (kind_) {
        case T_KICK: {                                  // PITCH DROP PUNCH DECAY DRIVE CLICK RUMBL
            o.env = 1; o.env_k = decay_k(80.f + v(3) * 7.f);
            o.inc[0] = (38.f + v(0) * 0.45f) / kSr;
            o.sweep = 1.f + v(1) * 0.07f; o.pe = 1; o.pe_k = decay_k(4.f + (100.f - v(2)) * 0.5f);
            o.nenv = v(5) * 0.01f; o.nenv_k = decay_k(2.5f);
            o.ph[0] = 0.25f;
            break;
        }
        case T_BASS:                                    // CUTOF RESO ENV DRIVE DECAY GLIDE ALGO
            o.inc[0] = 65.41f / kSr; o.inc[1] = o.inc[0] * 1.006f;
            o.env = 1; o.env_k = decay_k(150.f + v(4) * 8.f);
            o.nenv = accent ? 1.4f : 1.f; o.nenv_k = decay_k(40.f + v(4) * 5.f);
            break;
        case T_DRUM: {                                  // TUNE SNAP DECAY NOISE METAL CRUSH SKEW; lanes SNARE CLAP RIM HAT CYMB TOM
            const float tune = 0.7f + v(0) * 0.006f;
            o.env = 1; o.nenv = 1; o.ph[0] = 0.25f; o.ph[1] = 0.25f; o.pe = 0; o.sweep = 1;
            if (lane == 0) { o.inc[0] = 185.f * tune / kSr; o.env_k = decay_k(60.f); o.nenv = 0.3f + v(1) * 0.007f; o.nenv_k = decay_k(80.f + v(2) * 2.2f); }
            else if (lane == 1) { o.burst = 3; o.burst_n = 0; o.nenv = 0; o.nenv_k = decay_k(60.f + v(2) * 2.f); o.env = 0; }
            else if (lane == 2) { o.inc[0] = 1700.f * tune / kSr; o.inc[1] = 520.f * tune / kSr; o.env_k = decay_k(12.f); o.nenv = 0; }
            else if (lane == 3) { o.env = 0; o.nenv_k = decay_k(accent ? 180.f + v(2) * 2.5f : 25.f + v(2) * 0.4f); }
            else if (lane == 4) { o.env = 0; o.nenv_k = decay_k(900.f + v(2) * 10.f); }
            else { o.inc[0] = 110.f * tune / kSr; o.sweep = 1.6f; o.pe = 1; o.pe_k = decay_k(30.f); o.env_k = decay_k(200.f + v(2) * 3.f); o.nenv = 0; }
            break;
        }
        case T_PERC: {                                  // STRIK BODY RING WARP PITCH DRIVE ALGO; lanes MODAL RUST BYTE CLANK
            static const float ratio[4] = {1.f, 1.5f, 2.1f, 0.75f};
            const float f = 180.f * exp2f(v(4) * 0.03f) * ratio[lane];
            o.inc[0] = f / kSr; o.inc[1] = f * (lane == 0 ? 2.76f : (lane == 2 ? 1.5f : 1.41f)) / kSr;
            o.ph[0] = 0.25f; o.ph[1] = 0.25f;
            o.env = 1; o.env_k = decay_k(30.f + v(2) * 6.f);
            o.nenv = v(0) * 0.01f; o.nenv_k = decay_k(4.f);
            o.idx = 3.f; o.idx_k = decay_k(40.f);
            o.sweep = lane == 3 ? 1.3f : 1.f; o.pe = 1; o.pe_k = decay_k(25.f);
            break;
        }
        case T_PAD: {                                   // SPRED AGE ATTCK FILTR MANGL FDBK FREEZ: a minor chord on C3
            static const float note[3] = {48, 51, 55};
            for (int k = 0; k < 3; k++) o.inc[k] = 440.f * semis(note[k] - 69) / kSr;
            if (o.env < 0.0001f) o.env = 0;             // (a chord held over keeps its level: no click)
            o.env_k = decay_k(5.f + v(2) * 15.f);       // here the attack's speed
            break;
        }
        case T_TEXT:                                    // GRIT DENS TONE DRIFT GATE RESON ALGO: a step opens the gate
            o.env_k = decay_k(3.f);
            break;
        default:                                        // SAMP (no sample yet): START LEN PITCH SCAN FILTR CRUSH RESYN, a stab
            o.inc[0] = 261.6f * semis((v(2) - 50.f) * 0.24f) / kSr; o.inc[1] = o.inc[0] * 1.4983f;
            o.env = 1; o.env_k = decay_k(50.f + v(1) * 8.f);
            break;
        }
    }

    void kick(float *out, int n) {
        Voice &o = v_[0];
        if (!o.on) return;
        const float drive = 1.f + v(4) * 0.06f;
        for (int i = 0; i < n; i++) {
            o.ph[0] = wrap(o.ph[0] + o.inc[0] * (1.f + (o.sweep - 1.f) * o.pe));
            o.pe *= o.pe_k;
            const float x = fsin(o.ph[0]) * o.env + noise() * o.nenv * 0.5f;
            out[i] += soft(x * drive) * o.amp;
            o.env *= o.env_k;
            o.nenv *= o.nenv_k;
        }
        if (o.env < 0.0005f) o.on = false;
    }

    void bass(float *out, int n) {
        Voice &o = v_[0];
        if (!o.on) return;
        const int algo = static_cast<int>(v(6)) / 26;                  // 0..3: saw, square, reese (two saws), fold
        const float cut = 60.f * exp2f(v(0) * 0.06f) * (1.f + o.nenv * v(2) * 0.08f);
        const float f = svf_f(cut), q = 1.f - v(1) * 0.0092f, drive = 1.f + v(3) * 0.05f;
        for (int i = 0; i < n; i++) {
            o.ph[0] = wrap(o.ph[0] + o.inc[0]);
            o.ph[1] = wrap(o.ph[1] + o.inc[1]);
            float s;
            if (algo == 0) s = 2.f * o.ph[0] - 1.f;
            else if (algo == 1) s = o.ph[0] < 0.5f ? 0.8f : -0.8f;
            else if (algo == 2) s = o.ph[0] + o.ph[1] - 1.f;
            else { s = fsin(o.ph[0]) * (1.f + v(3) * 0.04f); s = s > 1.f ? 2.f - s : (s < -1.f ? -2.f - s : s); }
            out[i] += soft(o.svf.run(s, f, q) * drive) * o.env * o.amp;
            o.env *= o.env_k;
        }
        o.nenv *= powf(o.nenv_k, static_cast<float>(n));               // the filter's envelope moves once a block
        if (o.env < 0.0005f) o.on = false;
    }

    void drum(float *out, int n) {
        const float noise_amt = 0.4f + v(3) * 0.006f, metal = v(4) * 0.01f, tune = 0.7f + v(0) * 0.006f;
        const bool metal_on = v_[3].on || v_[4].on;
        for (int i = 0; i < n; i++) {
            float x = 0;
            const float nz = noise();
            float bank = 0;
            if (metal_on) {                                            // the shared metal bank: six squares at the 808's ratios
                static const float hz[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
                for (int k = 0; k < 6; k++) {
                    metal_ph_[k] = wrap(metal_ph_[k] + hz[k] * tune / kSr);
                    bank += metal_ph_[k] < 0.5f ? 1.f : -1.f;
                }
                bank *= 0.16f;
            }
            Voice &sn = v_[0];
            if (sn.on) { sn.ph[0] = wrap(sn.ph[0] + sn.inc[0]); x += (fsin(sn.ph[0]) * sn.env * 0.6f + nz * sn.nenv * noise_amt) * sn.amp; sn.env *= sn.env_k; sn.nenv *= sn.nenv_k; }
            Voice &cl = v_[1];
            if (cl.on) {
                if (cl.burst > 0 && --cl.burst_n <= 0) { cl.nenv = 1; cl.burst--; cl.burst_n = static_cast<int>(kSr * 0.011f); }
                x += cl.svf.high(nz, svf_f(1200.f), 0.5f) * cl.nenv * cl.amp;
                cl.nenv *= cl.burst > 0 ? decay_k(6.f) : cl.nenv_k;
            }
            Voice &rm = v_[2];
            if (rm.on) { rm.ph[0] = wrap(rm.ph[0] + rm.inc[0]); rm.ph[1] = wrap(rm.ph[1] + rm.inc[1]); x += (fsin(rm.ph[0]) + fsin(rm.ph[1])) * 0.5f * rm.env * rm.amp; rm.env *= rm.env_k; }
            for (int lane = 3; lane <= 4; lane++) {
                Voice &h = v_[lane];
                if (!h.on) continue;
                const float src = nz * (1.f - metal) + bank * metal;
                x += h.svf.high(src, svf_f(lane == 3 ? 7000.f * tune : 5000.f * tune), 0.7f) * h.nenv * h.amp * 0.8f;
                h.nenv *= h.nenv_k;
            }
            Voice &tm = v_[5];
            if (tm.on) { tm.ph[0] = wrap(tm.ph[0] + tm.inc[0] * (1.f + (tm.sweep - 1.f) * tm.pe)); tm.pe *= tm.pe_k; x += fsin(tm.ph[0]) * tm.env * tm.amp; tm.env *= tm.env_k; }
            out[i] += crush(x);
        }
        for (int lane = 0; lane < 6; lane++) {
            Voice &o = v_[lane];
            if (o.on && o.env < 0.0005f && o.nenv < 0.0005f && o.burst == 0) o.on = false;
        }
    }

    float crush(float x) const {                                       // CRUSH: fewer bits, 16 down to 4
        const float c = v(5);
        if (c < 1.f) return x;
        const float steps = exp2f(15.f - c * 0.11f);
        return static_cast<float>(static_cast<int>(x * steps)) / steps;
    }

    void perc(float *out, int n) {
        const float body = v(1) * 0.01f, drive = 1.f + v(5) * 0.05f;
        for (int lane = 0; lane < 4; lane++) {
            Voice &o = v_[lane];
            if (!o.on) continue;
            for (int i = 0; i < n; i++) {
                const float pitch = 1.f + (o.sweep - 1.f) * o.pe;
                o.ph[0] = wrap(o.ph[0] + o.inc[0] * pitch);
                o.ph[1] = wrap(o.ph[1] + o.inc[1] * pitch);
                o.pe *= o.pe_k;
                float s;
                if (lane == 0) s = fsin(o.ph[0]) + body * 0.6f * fsin(o.ph[1]);                         // MODAL
                else if (lane == 1) s = o.svf.run(noise(), svf_f(o.inc[0] * kSr), 0.04f) * 0.4f;         // RUST (a ringing band of noise)
                else if (lane == 2) s = ((o.ph[0] < 0.5f) != (o.ph[1] < 0.5f)) ? 0.5f : -0.5f;          // BYTE
                else { s = fsin(wrap(o.ph[0] + o.idx * 0.15f * fsin(o.ph[1]))); o.idx *= o.idx_k; }    // CLANK
                out[i] += soft((s * o.env + noise() * o.nenv) * drive) * o.amp * 0.7f;
                o.env *= o.env_k;
                o.nenv *= o.nenv_k;
            }
            if (o.env < 0.0005f) o.on = false;
        }
    }

    void pad(float *out, int n) {
        Voice &o = v_[0];
        if (!o.on) return;
        const float spread = v(0) * 0.0002f, f = svf_f(200.f * exp2f(v(3) * 0.05f));
        const float rel = decay_k(400.f);
        for (int i = 0; i < n; i++) {
            float s = 0;
            for (int k = 0; k < 3; k++) {                               // a saw per note, and its detuned twin in ph of the next voice slot
                o.ph[k] = wrap(o.ph[k] + o.inc[k] * (1.f + spread));
                Voice &tw = v_[1];
                tw.ph[k] = wrap(tw.ph[k] + o.inc[k] * (1.f - spread));
                s += o.ph[k] + tw.ph[k] - 1.f;
            }
            o.env = o.gate ? o.env + (1.f - o.env) * (1.f - o.env_k) : o.env * rel;
            out[i] += o.svf.run(s * 0.33f, f, 0.7f) * o.env * o.amp;
        }
        if (!o.gate && o.env < 0.0005f) o.on = false;
    }

    void text(float *out, int n, bool running) {                       // it runs free while the clock runs, silent when it stops
        Voice &o = v_[0];
        const float floor = running ? v(4) * 0.005f : 0.f;              // GATE: what is left while the gate is closed
        drift_ += (noise() * 0.02f - drift_ * 0.001f) * v(3) * 0.01f;   // DRIFT: a slow random walk of the tone
        const float f = svf_f(150.f * exp2f(v(2) * 0.06f + drift_ * 2.f)), q = 1.f - v(5) * 0.017f;
        const float dust = v(1) * v(1) * 0.000004f, grit = v(0) * 0.01f;
        const float target = o.gate && running ? 1.f : floor, k = o.gate ? 0.995f : 0.9993f;
        for (int i = 0; i < n; i++) {
            float s = noise() * (1.f - grit);
            if (static_cast<float>(rng_ >> 8) * (1.f / 16777216.f) < dust) s += (noise() > 0 ? 1.f : -1.f) * grit * 4.f;   // GRIT: crackle
            o.env = target + (o.env - target) * k;
            out[i] += o.svf.run(s, f, q) * o.env * 0.6f;
        }
    }

    void samp(float *out, int n) {
        Voice &o = v_[0];
        if (!o.on) return;
        const float f = svf_f(300.f * exp2f(v(4) * 0.05f));
        for (int i = 0; i < n; i++) {
            o.ph[0] = wrap(o.ph[0] + o.inc[0]);
            o.ph[1] = wrap(o.ph[1] + o.inc[1]);
            out[i] += crush(o.svf.run(o.ph[0] + o.ph[1] - 1.f, f, 0.5f) * o.env * o.amp);
            o.env *= o.env_k;
        }
        if (o.env < 0.0005f) o.on = false;
    }

    float val_[kParams] = {50, 50, 50, 50, 50, 50, 50};
    float level_ = 0.6f;
    int kind_ = 0;
    Voice v_[kLanes];
    float metal_ph_[6] = {};
    float drift_ = 0;
    uint32_t rng_ = 0x1234567u;
};

}  // namespace

void register_track_module(sc::Registry &reg) { reg.add(kTypeTrack, sc::module_type_of<TrackModule>()); }

}  // namespace tg
