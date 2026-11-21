#include "engine/modules/osc_engines.h"
#include <cmath>
#if defined(ENGINE_PROFILE) && defined(ARDUINO_ARCH_ESP32)
#include <esp_cpu.h>
#endif
#include <cstring>
#include "engine/dsp/block.h"
#include "engine/dsp/interp.h"
#include "engine/dsp/osc.h"
#include "engine/dsp/phase.h"
#include "engine/dsp/svf.h"
#include "engine/dsp/util.h"
#include "engine/dsp/wavetables.h"
#include "engine/modules/builtin.h"

namespace sc {
namespace {

constexpr int32_t kSemi = 256;
constexpr int kKsLen = 2048;                         // string buffer: the lowest pitch is sample rate / 2046 (23 Hz at 48 kHz)
constexpr int kModes = 8, kHarm = 12, kSaws = 5;
constexpr int kWgLen = 1024, kTerN = 32, kScN = 32;     // the float scratch (the Karplus buffer, 4 KB): waveguide, terrain, chain
constexpr int kGenN = 12, kCaN = 64;                   // GENDY breakpoints; the automaton's cells

inline int32_t scaled(q15 m, int32_t range) { return static_cast<int32_t>((static_cast<int64_t>(m) * range) >> 15); }
inline q15 clamp_unit(int32_t v) { return static_cast<q15>(v < 0 ? 0 : (v > 32767 ? 32767 : v)); }

// Constant tables of the engines, built once and shared by every instance (there are 32 instances in a 4-oscillator, 8-voice patch: per-instance copies
// were 1.3 KB each of the scarce internal RAM).
struct Tabs {
    int32_t g_tab[33] = {}, harm[kModes] = {}, bell[kModes] = {}, amp[kModes] = {}, dtab[17][kModes] = {}, lg[kHarm + 1] = {};
    uint32_t vinc[5][3] = {};
    Tabs() {
        // (single precision: the tables are built at the first note, on the audio thread)
        for (int i = 0; i <= 32; i++) g_tab[i] = static_cast<int32_t>(std::lroundf((1.0f - 0.1f * std::exp2f(-10.0f * static_cast<float>(i) / 32.0f)) * 32767.0f));
        static const float bell_ratio[kModes] = {1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f, 25.0f, 32.0f};
        for (int k = 0; k < kModes; k++) {
            harm[k] = static_cast<int32_t>((k + 1) * 65536);
            bell[k] = static_cast<int32_t>(std::lroundf(bell_ratio[k] * 65536.0f));
            amp[k] = static_cast<int32_t>(std::lroundf(32767.0f * std::pow(static_cast<float>(k + 1), -0.8f) * 0.2f));
        }
        for (int mi = 0; mi <= 16; mi++) {
            const float t0 = 40.0f * std::exp2f(6.64f * static_cast<float>(mi) / 16.0f);   // ms: 40 ms .. 4 s
            for (int k = 0; k < kModes; k++) {
                const float tk = t0 / (1.0f + 0.7f * static_cast<float>(k));
                // (a decay just under 1, Q31: its distance from 1 by expm1, so single precision keeps the long decays' length)
                dtab[mi][k] = 0x7FFFFFFF - q31_from_f(-std::expm1f(-1.0f / (tk * 0.001f * static_cast<float>(kSampleRate))));
            }
        }
        static const float vow[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {270, 2300, 3000}, {450, 800, 2830}, {325, 700, 2700}};
        for (int v = 0; v < 5; v++) for (int f = 0; f < 3; f++) vinc[v][f] = hz_to_inc(vow[v][f]);
        for (int k = 1; k <= kHarm; k++) lg[k] = static_cast<int32_t>(std::lroundf(std::log2(static_cast<float>(k)) * 65536.0f));
    }
};
const Tabs &tabs() { static const Tabs t; return t; }

class OscEngines : public Module {
public:
    const ModuleInfo &info() const override {
        static const ModuleInfo i = {"OscEngines", Scope::Voice, 2, 1, OSCX_N, false, {"pitch", "gate"}, {"out"},
            {{"engine", OSCX_KARP, 0, OSCX_ENGINES - 1}, {"pitch", 60 * kSemi, 0, 127 * kSemi}, {"timbre", 16384, 0, kUnity},
             {"morph", 16384, 0, kUnity}, {"level", kUnity, 0, kUnity}, {"pitch_mod", 12 * kSemi, 0, 96 * kSemi}, {"quality", 0, 0, 2}}};
        return i;
    }

    bool init(Memory &m) override {
        a4_ = inc_a4();
        heap_ = m.bulk;                                   // the string buffer is read and written sequentially: PSRAM is fine, internal RAM is not to be spent on 32 of them
        ks_ = static_cast<int16_t *>(heap_->alloc(sizeof(int16_t) * kKsLen));
        if (!ks_) return false;
        std::memset(ks_, 0, sizeof(int16_t) * kKsLen);
        t_ = &tabs();
        one_hz_ = hz_to_inc(1.0f);
        return true;
    }
    ~OscEngines() override { if (heap_) heap_->free(ks_); }

    void reset() override {
        std::memset(ph_, 0, sizeof ph_);
        std::memset(mx_, 0, sizeof mx_);
        std::memset(my_, 0, sizeof my_);
        // (the Karplus string buffer is not cleared here: trigger() clears it when the string is struck, and no other engine reads it; clearing
        // 4 KB of PSRAM per oscillator at every voice start cost about 1.5 ms of the audio block on the board)
        lp_ = 0; w_ = 0; gate_ = false; fm_ph_ = 0;
        live_engine_ = -1;                                    // GENDY / CHAOS / CA start from their seeds again
        for (auto &s : sv_) s = SvfStateF{};
        ds_ = SvfStateF{};
    }
    void set_param(int idx, int32_t v) override {
        switch (idx) {
            case OSCX_ENGINE: engine_ = v; break;
            case OSCX_PITCH: pitch_ = v; break;
            case OSCX_TIMBRE: timbre_ = static_cast<q15>(v); break;
            case OSCX_MORPH: morph_ = static_cast<q15>(v); break;
            case OSCX_LEVEL: level_ = static_cast<q15>(v); break;
            case OSCX_PITCH_MOD: pmod_ = v; break;
            case OSCX_QUAL: qual_ = v; prep_ok_ = false; break;
        }
    }

    SC_HOT void process(const ProcessCtx &ctx, const Ports &p) override {
#if defined(ENGINE_PROFILE) && defined(ARDUINO_ARCH_ESP32)
        const uint32_t prof_t0 = esp_cpu_get_cycle_count();
#endif
        const int n = ctx.frames;
        const q15 *cv = p.in[0], *gate = p.in[1], *mp = p.mod[OSCX_PITCH];
        const q15 *mt = p.mod[OSCX_TIMBRE], *mm = p.mod[OSCX_MORPH];
        const q15 timbre = clamp_unit(timbre_ + (mt ? mt[0] : 0)), morph = clamp_unit(morph_ + (mm ? mm[0] : 0));
        // ADR-050: a pitch that only slow sources move (the note and its glide, envelopes, LFOs under 100 Hz) is one increment a block,
        // the mean of its line over the block (dsp/phase.h pitch_line: the phase lands where the per-sample reading puts it), and the block
        // takes the fast path below
        const bool slow = n > 1 && slow_in(p, 0) && (!mp || slow_mod(p, OSCX_PITCH));
        const bool varying = !slow && (mp || cv[0] != cv[n - 1]);
        uint32_t inc;
        if (slow) {
            auto at = [&](int i) { return pitch_ + scaled(cv[i], kPitchCvSpan) + (mp ? scaled(mp[i], pmod_) : 0); };
            int32_t d;
            pitch_line(at(0), (at(n / 2 - 1) + at(n / 2)) / 2, at(n - 1), n, a4_, &inc, &d);
            inc += static_cast<uint32_t>(d * ((n - 1) / 2) + (((n - 1) & 1) ? d / 2 : 0));     // (the line's middle: its mean)
        } else {
            inc = pitch_to_inc(pitch_ + scaled(cv[0], kPitchCvSpan) + (mp ? scaled(mp[0], pmod_) : 0), a4_);
        }
        if (live_engine_ != engine_) { live_engine_ = engine_; init_engine(inc); }       // (a switch of engine: the new one's state starts from its seed)
        // The per-block setup is a pure function of these four values: with a held note and no modulation it runs once, not every block
        if (!(prep_ok_ && prep_engine_ == engine_ && prep_inc_ == inc && prep_timbre_ == timbre && prep_morph_ == morph)) {
            prepare(inc, timbre, morph);
            prep_ok_ = true; prep_engine_ = engine_; prep_inc_ = inc; prep_timbre_ = timbre; prep_morph_ = morph;
        }
        if (engine_ == OSCX_CA) ca_block(n);
        else if (engine_ == OSCX_SCAN) scan_block(n);
        // Fast path: a steady pitch and a gate that does not change inside the block (a held note, the usual case) need no per-sample gate test,
        // trigger check or engine dispatch: one tight loop per engine. The loop around the engines was about 50 cycles per sample.
        bool edge = false;
        if (!varying) for (int i = 0; i < n; i++) if ((gate[i] > 16384) != gate_) { edge = true; break; }
        if (!varying && !edge) {
            q15 *out = p.out[0];
            const q15 lv = level_;
            switch (engine_) {
                case OSCX_KARP: for (int i = 0; i < n; i++) out[i] = mul15(sat16(karp()), lv); break;
                case OSCX_MODAL: for (int i = 0; i < n; i++) out[i] = mul15(sat16(modal(inc, false, i)), lv); break;
                case OSCX_FM2: for (int i = 0; i < n; i++) out[i] = mul15(sat16(fm2(inc, timbre)), lv); break;
                case OSCX_FOLD: for (int i = 0; i < n; i++) out[i] = mul15(sat16(fold(inc, timbre, morph)), lv); break;
                case OSCX_SSAW: for (int i = 0; i < n; i++) out[i] = mul15(sat16(ssaw(inc, timbre, morph)), lv); break;
                case OSCX_VOWEL: for (int i = 0; i < n; i++) out[i] = mul15(sat16(vowel(inc)), lv); break;
                case OSCX_ADD: for (int i = 0; i < n; i++) out[i] = mul15(sat16(additive(inc)), lv); break;
                case OSCX_STR: str_block(out, n, lv); break;
                case OSCX_GENDY: for (int i = 0; i < n; i++) out[i] = mul15(sat16(gendy(inc)), lv); break;
                case OSCX_ROSSL: for (int i = 0; i < n; i++) out[i] = mul15(sat16(chaos(inc)), lv); break;
                case OSCX_CA: for (int i = 0; i < n; i++) out[i] = mul15(sat16(ca(inc)), lv); break;
                case OSCX_PULSAR: for (int i = 0; i < n; i++) out[i] = mul15(sat16(pulsar(inc)), lv); break;
                case OSCX_TONEW: for (int i = 0; i < n; i++) out[i] = mul15(sat16(tonew(inc)), lv); break;
                case OSCX_FOF: for (int i = 0; i < n; i++) out[i] = mul15(sat16(fof(inc)), lv); break;
                case OSCX_TERRA: for (int i = 0; i < n; i++) out[i] = mul15(sat16(terra(inc)), lv); break;
                case OSCX_SCAN: for (int i = 0; i < n; i++) out[i] = mul15(sat16(scan(inc)), lv); break;
                case OSCX_WIND: for (int i = 0; i < n; i++) out[i] = mul15(sat16(wind()), lv); break;
                case OSCX_FOLEY: for (int i = 0; i < n; i++) out[i] = mul15(sat16(foley(inc)), lv); break;
                default: for (int i = 0; i < n; i++) out[i] = mul15(sat16(dust()), lv); break;
            }
#if defined(ENGINE_PROFILE) && defined(ARDUINO_ARCH_ESP32)
            g_osc_prof[engine_ < OSCX_ENGINES ? engine_ : 0] += esp_cpu_get_cycle_count() - prof_t0;
#endif
            return;
        }
        for (int i = 0; i < n; i++) {
            if (varying) inc = pitch_to_inc(pitch_ + scaled(cv[i], kPitchCvSpan) + (mp ? scaled(mp[i], pmod_) : 0), a4_);
            const bool g = gate[i] > 16384;
            if (g && !gate_) trigger(inc, timbre);
            gate_ = g;
            int32_t y;
            switch (engine_) {
                case OSCX_KARP: y = karp(); break;
                case OSCX_MODAL: y = modal(inc, varying, i); break;
                case OSCX_FM2: y = fm2(inc, timbre); break;
                case OSCX_FOLD: y = fold(inc, timbre, morph); break;
                case OSCX_SSAW: y = ssaw(inc, timbre, morph); break;
                case OSCX_VOWEL: y = vowel(inc); break;
                case OSCX_ADD: y = additive(inc); break;
                case OSCX_STR: y = str(); break;
                case OSCX_GENDY: y = gendy(inc); break;
                case OSCX_ROSSL: y = chaos(inc); break;
                case OSCX_CA: y = ca(inc); break;
                case OSCX_PULSAR: y = pulsar(inc); break;
                case OSCX_TONEW: y = tonew(inc); break;
                case OSCX_FOF: y = fof(inc); break;
                case OSCX_TERRA: y = terra(inc); break;
                case OSCX_SCAN: y = scan(inc); break;
                case OSCX_WIND: y = wind(); break;
                case OSCX_FOLEY: y = foley(inc); break;
                default: y = dust(); break;
            }
            p.out[0][i] = mul15(sat16(y), level_);
        }
#if defined(ENGINE_PROFILE) && defined(ARDUINO_ARCH_ESP32)
        g_osc_prof[engine_ < OSCX_ENGINES ? engine_ : 0] += esp_cpu_get_cycle_count() - prof_t0;
#endif
    }

private:
    /* ---- per block: derived values ---- */
    void prepare(uint32_t inc, q15 timbre, q15 morph) {
        switch (engine_) {
            case OSCX_KARP: {
                const uint64_t d = inc ? (1ull << 48) / inc : (static_cast<uint64_t>(kKsLen - 2) << 16);
                const int64_t dq = static_cast<int64_t>(d) - 32768;                      // the loop filter delays by half a sample
                ks_d_ = static_cast<int32_t>(dq < (2 << 16) ? (2 << 16) : (dq > (static_cast<int64_t>(kKsLen - 2) << 16) ? (static_cast<int64_t>(kKsLen - 2) << 16) : dq));
                ks_a_ = 3000 + ((timbre * 29000) >> 15);
                const int gi = morph >> 10;
                ks_g_ = t_->g_tab[gi] + (((t_->g_tab[gi + 1 > 32 ? 32 : gi + 1] - t_->g_tab[gi]) * (morph & 1023)) >> 10);
                break;
            }
            case OSCX_MODAL: {
                const int mi = morph >> 11;                                              // 0..15
                const int fr = morph & 2047;
                for (int k = 0; k < kModes; k++) {
                    dec_[k] = t_->dtab[mi][k] + static_cast<int32_t>((static_cast<int64_t>(t_->dtab[mi + 1][k] - t_->dtab[mi][k]) * fr) >> 11);
                    ratio_[k] = t_->harm[k] + static_cast<int32_t>((static_cast<int64_t>(t_->bell[k] - t_->harm[k]) * timbre) >> 15);
                }
                modal_coefs(inc);
                break;
            }
            case OSCX_FM2: {
                static const int32_t ratios[16] = {32768, 49152, 65536, 98304, 131072, 163840, 196608, 229376, 262144, 327680, 393216, 458752, 524288, 589824, 655360, 786432};
                fm_ratio_ = ratios[morph >> 11];
                break;
            }
            case OSCX_SSAW: {
                static const int32_t off[kSaws] = {0, -737, 737, -1475, 1475};                  // relative increment at spread 1 (Q15): +-4.5 % at the edge, evenly spaced
                for (int k = 0; k < kSaws; k++) {
                    const int32_t rel = static_cast<int32_t>((static_cast<int64_t>(off[k]) * timbre) >> 15);
                    saw_inc_[k] = static_cast<uint32_t>(static_cast<int64_t>(inc) + ((static_cast<int64_t>(inc) * rel) >> 15));
                }
                break;
            }
            case OSCX_VOWEL: {
                const int32_t pos = timbre * 4;                                          // 0 .. 4 vowels apart, Q15
                const int vi = pos >> 15;                                                // 0..3 (timbre < 32768)
                const int32_t fr = pos & 32767;
                static const int32_t wgt[3] = {32767, 22000, 14000};
                for (int f = 0; f < 3; f++) {
                    const uint32_t a = t_->vinc[vi][f], b = t_->vinc[vi + 1][f];
                    const uint32_t fi = a + static_cast<uint32_t>((static_cast<int64_t>(static_cast<int64_t>(b) - a) * fr) >> 15);
                    const int32_t k = (1 << 29) / 3 - static_cast<int32_t>((static_cast<int64_t>(morph) * ((1 << 29) / 3 - (1 << 29) / 30)) >> 15);   // Q 3 .. 30
                    vc_[f] = svf_to_float(svf_coef(svf_g(fi), k));
                    vw_[f] = wgt[f];
                    vwf_[f] = static_cast<float>(wgt[f]) * 3.0f;                              // weight x the make-up gain of 3
                }
                break;
            }
            case OSCX_ADD: {
                const int32_t slope = 2 * 65536 - static_cast<int32_t>((static_cast<int64_t>(timbre) * 115000) >> 15);       // 2.0 .. 0.25 octaves of level per octave of pitch
                int32_t sum = 0;
                for (int k = 1; k <= kHarm; k++) {
                    const bool even = (k & 1) == 0;
                    int32_t a = static_cast<int32_t>(exp2_scale(32767u, -static_cast<int32_t>((static_cast<int64_t>(slope) * t_->lg[k]) >> 16)));
                    if (even) a = static_cast<int32_t>((static_cast<int64_t>(a) * (32767 - morph)) >> 15);
                    if (static_cast<uint64_t>(inc) * static_cast<uint64_t>(k) >= 2147483648ull) a = 0;
                    harm_amp_[k - 1] = a; sum += a;
                }
                const int32_t norm = sum > 0 ? static_cast<int32_t>((32767ll * 24000) / (sum < 1 ? 1 : sum)) : 0;          // total about 0.73 of full scale
                add_top_ = 0;
                for (int k = 0; k < kHarm; k++) {
                    harm_amp_[k] = static_cast<int32_t>((static_cast<int64_t>(harm_amp_[k]) * norm) >> 15);
                    if (harm_amp_[k]) add_top_ = k + 1;
                }
                break;
            }
            case OSCX_STR: {
                // detune: up to 50 cents between the pair, half each side (Q16 octaves: cents / 1200 / 2)
                const int32_t x = static_cast<int32_t>((static_cast<int64_t>(timbre) * 50 * 65536) / (1200LL * 2 * 32767));
                str_inc_[0] = exp2_scale(inc, -x);
                str_inc_[1] = exp2_scale(inc, x);
                str_wave_ = morph < 10923 ? WT_SAW : (morph < 21846 ? WT_PULSE : WT_TRI);
                str_mip_ = qual_ != 2;
                str_tab_[0] = str_mip_ ? wt_table(str_wave_, str_inc_[0]) : nullptr;
                str_tab_[1] = str_mip_ ? wt_table(str_wave_, str_inc_[1]) : nullptr;
                break;
            }
            case OSCX_DUST: {
                dust_thr_ = exp2_scale(one_hz_ * 2u, static_cast<int32_t>((static_cast<int64_t>(timbre) * 11 * 65536) >> 15));          // impulses per sample (as a phase increment)
                const int32_t k = (1 << 29) / 2 - static_cast<int32_t>((static_cast<int64_t>(morph) * ((1 << 29) / 2 - (1 << 29) / 60)) >> 15);   // Q 2 .. 60
                ds_c_ = svf_to_float(svf_coef(svf_g(inc), k));
                // A unity-peak band-pass rings with an amplitude ~ 1/Q after an impulse, so the loudness fell by ~sqrt(Q) towards Morph = 1 (Q = 60).
                // Make-up gain of sqrt(Q / 2) (1.0 at the lowest Q of 2) keeps the level about constant. [control rate: float is fine]
                ds_gain_ = 32768.0f * 14.0f * std::sqrt(static_cast<float>(1 << 28) / static_cast<float>(k));
                break;
            }
            case OSCX_GENDY:
                gd_astep_ = static_cast<float>(timbre) * (0.5f / 32768.0f);                 // the walk's step per cycle: 0 = a clean 12-point wave
                gd_dstep_ = static_cast<float>(morph) * (0.4f / 32768.0f);
                break;
            case OSCX_ROSSL:
                ch_mix_ = static_cast<float>(timbre) * (1.0f / 32768.0f);
                ch_c_ = 3.0f + 6.0f * static_cast<float>(morph) * (1.0f / 32768.0f);       // Roessler c: period 1 at 3.5, doubling, chaos from about 5.7
                break;
            case OSCX_CA: {
                static const uint8_t rules[8] = {30, 90, 110, 150, 45, 22, 126, 54};
                ca_rule_ = rules[(timbre >> 12) & 7];
                // generations a second: 0.5 .. 64 (exponential); morph 0 freezes the wave
                ca_period_ = morph < 512 ? 0.0f : static_cast<float>(kSampleRate) / (0.5f * std::exp2f(static_cast<float>(morph) * (7.0f / 32768.0f)));
                break;
            }
            case OSCX_PULSAR:
                pu_inv_ = 1.0f / (0.04f + 0.96f * static_cast<float>(timbre) * (1.0f / 32768.0f));      // 1 / duty: the pulsaret fills 4 % .. 100 % of the period
                pu_n_ = 0.5f * std::exp2f(static_cast<float>(morph) * (5.0f / 32768.0f));               // its cycles: 0.5 .. 16
                break;
            case OSCX_TONEW: {
                static const uint8_t rank[9] = {1, 3, 0, 2, 5, 4, 7, 6, 8};                              // the order the drawbars come in (16', 5 1/3, 8', 4' ...)
                float lv[9], sum = 0.0f;
                for (int j = 0; j < 9; j++) {
                    const float l = static_cast<float>(timbre) * (8.0f / 32768.0f) - static_cast<float>(rank[j]) + 1.0f;
                    lv[j] = l < 0.0f ? 0.0f : (l > 1.0f ? 1.0f : l);
                    if (static_cast<uint64_t>(inc >> 1) * kTwMul[j] >= 2147483648ull) lv[j] = 0.0f;     // above Nyquist
                    sum += lv[j];
                }
                const float g = sum > 0.0f ? 23000.0f / sum : 0.0f;
                for (int j = 0; j < 9; j++) tw_a_[j] = static_cast<int32_t>(lv[j] * g);
                const float m = static_cast<float>(morph) * (1.0f / 32768.0f);
                tw_click_ = static_cast<int32_t>(m * 14000.0f);                                         // the key click's start level
                tw_leak_ = static_cast<int32_t>(m * m * 900.0f);                                        // the leakage bed
                break;
            }
            case OSCX_FOF: {
                static const float fr[5][4] = {{800, 1150, 2900, 3500}, {400, 1600, 2700, 3300}, {270, 2300, 3000, 3500}, {450, 800, 2830, 3500}, {325, 700, 2700, 3500}};
                static const float bw[4] = {80, 90, 120, 130}, am[4] = {1.0f, 0.5f, 0.25f, 0.12f};
                const float pos = static_cast<float>(timbre) * (4.0f / 32768.0f);
                const int vi = static_cast<int>(pos) > 3 ? 3 : static_cast<int>(pos);
                const float fx = pos - static_cast<float>(vi), bs = 0.5f * std::exp2f(static_cast<float>(morph) * (3.0f / 32768.0f));    // bandwidth x0.5 .. x4
                for (int k = 0; k < 4; k++) {
                    const float f = fr[vi][k] + (fr[vi + 1][k] - fr[vi][k]) * fx;
                    const float th = 6.2831853f * f / static_cast<float>(kSampleRate), r = std::exp(-3.1415927f * bw[k] * bs / static_cast<float>(kSampleRate));
                    fof_rc_[k] = r * std::cos(th); fof_rs_[k] = r * std::sin(th);
                    // the burst's size: a burst of a long ring overlaps the next ones, so its start is scaled by 1 - (what is left of it one period later)
                    fof_a_[k] = am[k] * 7000.0f * (1.0f - std::exp(-3.1415927f * bw[k] * bs / inc_to_hz(inc > 1 ? inc : 1)));
                }
                break;
            }
            case OSCX_TERRA:
                te_r_ = static_cast<float>(timbre) * (0.5f / 32768.0f);                                // the orbit's radius: up to half the surface
                te_cx_ = static_cast<float>(morph) * (1.0f / 32768.0f);                                // its centre moves along a diagonal
                te_cy_ = 0.6f * te_cx_ + 0.2f;
                break;
            case OSCX_SCAN:
                sc_k_ = 0.05f + 0.9f * static_cast<float>(timbre) * (1.0f / 32768.0f);                 // stiffness of the chain: how fast the ripples run
                sc_damp_ = 0.01f * (1.0f - static_cast<float>(morph) * (1.0f / 32768.0f)) * (1.0f - static_cast<float>(morph) * (1.0f / 32768.0f));
                sc_period_ = static_cast<float>(kSampleRate) / 400.0f;                                  // the chain moves 400 times a second
                break;
            case OSCX_WIND: {
                const float m = static_cast<float>(morph) * (1.0f / 32768.0f);
                wd_a_ = 0.25f + 0.7f * m;                                                              // the bore's low pass: dull .. bright
                // (the coefficient as at 48 kHz, made a cutoff in Hz at any rate: 1 - (1 - a)^(48000 / fs); the same at 48 kHz)
                if (kSampleRate != 48000) wd_a_ = 1.0f - std::pow(1.0f - wd_a_, 48000.0f / static_cast<float>(kSampleRate));
                const float d = static_cast<float>(kSampleRate) / (2.0f * inc_to_hz(inc > 1 ? inc : 1)) - (1.0f / wd_a_ - 1.0f) - 1.0f;
                wd_len_ = d < 2.0f ? 2.0f : (d > static_cast<float>(kWgLen - 3) ? static_cast<float>(kWgLen - 3) : d);
                wd_breath_ = 0.3f + 0.6f * static_cast<float>(timbre) * (1.0f / 32768.0f);
                break;
            }
            case OSCX_FOLEY: {
                const float t = static_cast<float>(timbre) * (3.0f / 32768.0f), it = static_cast<float>(morph) * (1.0f / 32768.0f), sr = static_cast<float>(kSampleRate);
                auto tri = [](float x) { x = x < 0.0f ? -x : x; return x < 1.0f ? 1.0f - x : 0.0f; };
                fo_rain_ = tri(t); fo_wind_ = tri(t - 1.0f); fo_fire_ = tri(t - 2.0f); fo_crk_ = tri(t - 3.0f);
                fo_drop_ = static_cast<uint32_t>(5.0f * std::exp2f(it * 7.3f) / sr * 4294967040.0f);        // drops a second: 5 .. 800
                fo_crack_ = static_cast<uint32_t>((fo_crk_ > fo_fire_ ? 6.0f : 10.0f) * std::exp2f(it * 4.0f) / sr * 4294967040.0f);   // crackles a second: 6 .. 100
                fo_int_ = 0.25f + 0.75f * it;
                const float fc = inc_to_hz(inc > 1 ? inc : 1) * 2.0f, w = 6.2831853f * fc / sr;
                fo_wc_ = w > 0.5f ? 0.5f : w;
                break;
            }
            default: break;
        }
    }

    void trigger(uint32_t inc, q15 timbre) {
        if (engine_ == OSCX_KARP) {
            const int d = ks_d_ >> 16;
            int32_t lp = 0;
            lp_ = 0;
            const int32_t a = 3000 + ((timbre * 29000) >> 15);
            std::memset(ks_, 0, sizeof(int16_t) * kKsLen);
            for (int i = 0; i < d; i++) {
                lp += ((static_cast<int32_t>(noise_.next()) - lp) * a) >> 15;
                ks_[i] = static_cast<int16_t>((lp * 23000) >> 15);
            }
            w_ = d & (kKsLen - 1);
        } else if (engine_ == OSCX_STR) {
            ph_[0] = noise_.next_u32();                                          // random start phases: the pair never starts in phase
            ph_[1] = noise_.next_u32();
        } else if (engine_ == OSCX_MODAL) {
            for (int k = 0; k < kModes; k++) { mx_[k] = 0; my_[k] = t_->amp[k] << 15; }          // start in sine phase, at full amplitude
        } else if (engine_ == OSCX_TONEW) {
            tw_ck_ = tw_click_;                                                  // a key click at each note
        } else if (engine_ == OSCX_GENDY || engine_ == OSCX_ROSSL || engine_ == OSCX_CA || engine_ == OSCX_SCAN) {
            init_engine(inc);                                                    // each note starts from the same seed shape (CA: a new random row)
        }
    }

    /* ---- engines (one sample each) ---- */
    int32_t karp() {
        const uint32_t pos = (static_cast<uint32_t>(w_) << 16) - static_cast<uint32_t>(ks_d_);
        const int i0 = (pos >> 16) & (kKsLen - 1), i1 = (i0 + 1) & (kKsLen - 1);
        const int32_t fr = static_cast<int32_t>(pos & 0xFFFF);
        const int32_t out = ks_[i0] + (((ks_[i1] - ks_[i0]) * fr) >> 16);
        lp_ += ((out - lp_) * ks_a_) >> 15;
        ks_[w_] = static_cast<int16_t>(sat16((lp_ * ks_g_) >> 15));
        w_ = (w_ + 1) & (kKsLen - 1);
        return out;
    }

    // Each mode is a decaying phasor: (x, y) is rotated by the mode's frequency and scaled by its decay every sample, so it rings as r^n sin(n theta) with
    // four multiplies instead of a sine lookup. The rotation (cos, sin of theta, from the table) is first normalised to exactly unit length (one Newton
    // step of 1/sqrt), because a length error of 1e-5 per sample would otherwise change the decay time of a slow mode by a large factor.
    // Computed once per block (the prepare cache) and, if the pitch moves inside a block, every 4 samples. A mode above Nyquist gets zero coefficients.
    void modal_coefs(uint32_t inc) {
        for (int k = 0; k < kModes; k++) {
            const uint64_t ik = (static_cast<uint64_t>(inc) * static_cast<uint64_t>(ratio_[k])) >> 16;
            if (ik > 0x7FFFFFFFull) { mrc_[k] = mrs_[k] = 0; continue; }
            const uint32_t th = static_cast<uint32_t>(ik);
            const int32_t c = sine(th + 0x40000000u) * 32768, sn = sine(th) * 32768;        // cos, sin: Q30
            const int32_t n2 = mulh(c, c) + mulh(sn, sn);                                   // |.|^2, Q28 (about 1.0 = 2^28)
            const int32_t corr = (3 << 27) - (n2 >> 1);                                     // 1.5 - n2 / 2 = 1 / sqrt(n2) to second order
            const int32_t cn = shl_sat(mulh(c, corr << 2), 2), sg = shl_sat(mulh(sn, corr << 2), 2);   // unit-length cos, sin: Q30 (corr is about 2^28: << 2 stays in 32 bits)
            mrc_[k] = shl_sat(mulh(dec_[k], cn), 1);                                        // r cos theta, Q30 (dec_ = r in Q31)
            mrs_[k] = shl_sat(mulh(dec_[k], sg), 1);
        }
    }

    int32_t modal(uint32_t inc, bool varying, int i) {
        if (varying && (i & 3) == 0) modal_coefs(inc);
        int32_t sum = 0;                                                                    // 8 modes of at most 8192 each: fits 32 bits
        for (int k = 0; k < kModes; k++) {
            const int32_t x = mx_[k], y = my_[k];                                           // Q28: the amplitude is the length of (x, y)
            mx_[k] = (mulh(mrc_[k], x) - mulh(mrs_[k], y)) * 4;
            my_[k] = (mulh(mrs_[k], x) + mulh(mrc_[k], y)) * 4;
            sum += my_[k] >> 15;
        }
        return sum;
    }

    int32_t fm2(uint32_t inc, q15 timbre) {
        fm_ph_ += static_cast<uint32_t>((static_cast<uint64_t>(inc) * static_cast<uint64_t>(fm_ratio_)) >> 16);
        ph_[0] += inc;
        const int64_t prod = static_cast<int64_t>(sine(fm_ph_)) * timbre;                      // Q30
        const uint32_t offset = static_cast<uint32_t>(static_cast<int32_t>((prod * 5214) >> 10));      // up to 8 rad in phase units
        return (static_cast<int32_t>(sine(ph_[0] + offset)) * 23000) >> 15;
    }

    int32_t fold(uint32_t inc, q15 timbre, q15 morph) {
        ph_[0] += inc;
        const int32_t sn = sine(ph_[0]), sw = static_cast<int32_t>(ph_[0] >> 16) - 32768;
        const int32_t src = sn + (static_cast<int32_t>((static_cast<int64_t>(sw - sn) * morph) >> 15));
        const int32_t gq8 = 256 + ((timbre * 2048) >> 15);                                    // 1x .. 9x
        const int32_t v = src * gq8;                                                          // Q23
        return (static_cast<int32_t>(sine(static_cast<uint32_t>(v) << 7)) * 23000) >> 15;
    }

    // STR: the two table oscillators summed (x 0.5); one sample, or a whole steady block with the wave / quality resolved once
    int32_t str() {
        const int32_t y = (wt_osc_any(str_wave_, str_mip_, ph_[0], 0x80000000u, str_tab_[0]) + wt_osc_any(str_wave_, str_mip_, ph_[1], 0x80000000u, str_tab_[1])) >> 1;
        ph_[0] += str_inc_[0];
        ph_[1] += str_inc_[1];
        return y;
    }
    template <int W, bool Mip>
    void str_loop(q15 *out, int n, q15 lv) {
        uint32_t p0 = ph_[0], p1 = ph_[1];
        const uint32_t i0 = str_inc_[0], i1 = str_inc_[1];
        const int16_t *t0 = str_tab_[0], *t1 = str_tab_[1];
        for (int i = 0; i < n; i++) {
            const int32_t y = (wt_osc<W, Mip>(p0, 0x80000000u, t0) + wt_osc<W, Mip>(p1, 0x80000000u, t1)) >> 1;
            out[i] = mul15(static_cast<q15>(y), lv);
            p0 += i0;
            p1 += i1;
        }
        ph_[0] = p0;
        ph_[1] = p1;
    }
    void str_block(q15 *out, int n, q15 lv) {
        switch (str_wave_ * 2 + (str_mip_ ? 1 : 0)) {
            case WT_SAW * 2:       str_loop<WT_SAW, false>(out, n, lv); break;
            case WT_SAW * 2 + 1:   str_loop<WT_SAW, true>(out, n, lv); break;
            case WT_PULSE * 2:     str_loop<WT_PULSE, false>(out, n, lv); break;
            case WT_PULSE * 2 + 1: str_loop<WT_PULSE, true>(out, n, lv); break;
            case WT_TRI * 2:       str_loop<WT_TRI, false>(out, n, lv); break;
            default:               str_loop<WT_TRI, true>(out, n, lv); break;
        }
    }

    int32_t ssaw(uint32_t, q15, q15 morph) {
        // The band-limiting correction only matters within one step of the saw's wrap point (about 1 sample in 100 per saw), so the plain ramp is
        // taken directly and osc_saw() only when a saw is at its edge: same output, without the call for the other samples.
        int32_t side = 0;
        for (int k = 1; k < kSaws; k++) {
            const uint32_t t = (ph_[k] += saw_inc_[k]), dt = saw_inc_[k];
            side += (t < dt || t > 0u - dt) ? osc_saw(t, dt) : static_cast<int32_t>(t >> 16) - 32768;
        }
        ph_[0] += saw_inc_[0];
        const uint32_t t0 = ph_[0], dt0 = saw_inc_[0];
        const int32_t centre = (t0 < dt0 || t0 > 0u - dt0) ? osc_saw(t0, dt0) : static_cast<int32_t>(t0 >> 16) - 32768;
        const int32_t wc = 32767 - (morph >> 1), ws = (5000 + ((morph * 6000) >> 15)) * 3 / 2;      // four side saws carry what six did: x 6/4
        return (((centre * wc) >> 15) * 9 / 20) + (((side * ws) >> 15) * 9 / 20);
    }

    int32_t vowel(uint32_t inc) {
        ph_[0] += inc;
        const float x = static_cast<float>(osc_saw(ph_[0], inc)) * (0.5f / 32768.0f);
        float sum = 0.0f;
        for (int f = 0; f < 3; f++) {
            float lp, bp, hp;
            svf_tick_f(vc_[f], sv_[f], x, lp, bp, hp);                                       // (single-precision float: the FPU does this in ~25 cycles)
            sum += vc_[f].k * bp * vwf_[f];                                                  // band-pass normalised to unity peak, weighted (Q15) x 3 make-up gain
        }
        return static_cast<int32_t>(sum);                                                     // float 1.0 x weight Q15 = q15
    }

    // The harmonics sin(k phi) come from two table reads and the Chebyshev recurrence s(k) = 2 cos(phi) s(k-1) - s(k-2), instead of one table read each.
    // States are Q29, cos(phi) Q30; the amplitudes sum to at most 24000, so the weighted sum fits 32 bits.
    int32_t additive(uint32_t inc) {
        ph_[0] += inc;
        const uint32_t ph = ph_[0];
        if (add_top_ == 0) return 0;
        const int32_t c = sine(ph + 0x40000000u) * 32768;                                   // cos(phi), Q30
        int32_t s_prev = 0, s = sine(ph) * 16384;                                           // sin(0 phi), sin(phi): Q29
        int32_t sum = harm_amp_[0] * (s >> 14);
        for (int k = 1; k < add_top_; k++) {
            const int32_t next = mulh(c, s) * 8 - s_prev;                                  // 2 cos(phi) s(k) - s(k-1)
            s_prev = s;
            s = next;
            sum += harm_amp_[k] * (s >> 14);
        }
        return sum >> 15;
    }

    int32_t dust() {
        const uint32_t r = noise_.next_u32();
        int32_t imp = 0;
        if (r < dust_thr_) imp = (static_cast<int32_t>(noise_.next()) >> 1) + (noise_.next() < 0 ? -8192 : 8192);
        float lp, bp, hp;
        svf_tick_f(ds_c_, ds_, static_cast<float>(imp) * (1.0f / 32768.0f), lp, bp, hp);
        return sat16(static_cast<int32_t>(ds_c_.k * bp * ds_gain_));
    }

    /* ---- GENDY, CHAOS, CA: their state lives in the Karplus buffer (a float view of it): the engines never run together ---- */
    float *fs() { return reinterpret_cast<float *>(ks_); }
    float rnd() { return static_cast<float>(noise_.next()) * (1.0f / 32768.0f); }            // -1 .. 1

    void init_engine(uint32_t inc) {
        float *f = fs();
        const bool fresh = scratch_owner_ != engine_;                                            // another engine used the scratch last: it holds its data
        if (engine_ == OSCX_KARP || engine_ == OSCX_TERRA || engine_ == OSCX_SCAN || engine_ == OSCX_WIND || engine_ == OSCX_GENDY || engine_ == OSCX_CA) scratch_owner_ = engine_;
        if (engine_ == OSCX_KARP) {
            if (fresh) std::memset(ks_, 0, sizeof(int16_t) * kKsLen);
        } else if (engine_ == OSCX_TERRA) {
            if (fresh) terra_build();
            te_dx_ = te_dy_ = 0.0f;
        } else if (engine_ == OSCX_SCAN) {
            for (int i = 0; i < kScN; i++) { f[i] = rnd(); f[kScN + i] = 0.0f; }                 // a random pluck, smoothed below
            for (int pass = 0; pass < 3; pass++) {
                for (int i = 0; i < kScN; i++) f[2 * kScN + i] = 0.25f * (f[(i + kScN - 1) & (kScN - 1)] + 2.0f * f[i] + f[(i + 1) & (kScN - 1)]);
                for (int i = 0; i < kScN; i++) f[i] = f[2 * kScN + i];
            }
            sc_count_ = 0.0f;
            float pk = 1e-6f, mean = 0.0f;
            for (int i = 0; i < kScN; i++) mean += f[i];
            mean *= 1.0f / static_cast<float>(kScN);
            for (int i = 0; i < kScN; i++) { f[i] -= mean; pk = std::fmax(pk, std::fabs(f[i])); }
            for (int i = 0; i < kScN; i++) { f[i] /= pk; f[2 * kScN + i] = f[i]; f[3 * kScN + i] = f[i]; }
        } else if (engine_ == OSCX_WIND) {
            for (int i = 0; i < kWgLen; i++) f[i] = 0.0f;
            wd_w_ = 0; wd_lp_ = 0.0f; wd_env_ = 0.0f; wd_dx_ = wd_dy_ = 0.0f;
        } else if (engine_ == OSCX_FOF) {
            for (int k = 0; k < 4; k++) fof_x_[k] = fof_y_[k] = 0.0f;
        } else if (engine_ == OSCX_FOLEY) {
            fo_l1_ = fo_l2_ = fo_l3_ = 0.0f; fo_ping_ = fo_crack_env_ = 0.0f; fo_gust_ = 0.5f; fo_gust_t_ = 0.5f; fo_cnt_ = 0;
        }
        if (engine_ == OSCX_GENDY) {
            static const float sn[kGenN] = {0.0f, 0.5f, 0.866f, 1.0f, 0.866f, 0.5f, 0.0f, -0.5f, -0.866f, -1.0f, -0.866f, -0.5f};
            for (int i = 0; i < kGenN; i++) { f[i] = sn[i] * 0.8f; f[kGenN + i] = 1.0f; }
            gd_total_ = static_cast<float>(kGenN); gd_seg_ = 0; gd_t_ = 0.0f; gd_x_ = gd_y_ = 0.0f;
            gendy_begin(inc);
        } else if (engine_ == OSCX_ROSSL) {
            ch_x_ = 1.0f; ch_y_ = 1.0f; ch_z_ = 1.0f; ch_dx_ = ch_dy_ = 0.0f;
        } else if (engine_ == OSCX_CA) {
            row_ = (static_cast<uint64_t>(noise_.next_u32()) << 32) | noise_.next_u32();
            ca_count_ = 0.0f;
            ca_step();
            for (int i = 0; i < kCaN; i++) f[i] = f[kCaN + i];
        }
    }

    // GENDY: 12 breakpoints, each with an amplitude (-1..1) and a relative length; a segment is a straight line from one amplitude to the next. At the
    // end of each cycle every amplitude and length takes a random step, mirrored back inside its limits (so it cannot blow up). The per-sample work is
    // the interpolation; the lengths are scaled to one period at each segment, so the pitch is followed as it moves.
    void gendy_begin(uint32_t inc) {
        const float *f = fs();
        gd_a0_ = f[gd_seg_]; gd_a1_ = f[gd_seg_ + 1 == kGenN ? 0 : gd_seg_ + 1];
        const float inv = static_cast<float>(inc) * (1.0f / 4294967296.0f) * gd_total_ / f[kGenN + gd_seg_];
        gd_inv_ = inv > 1.0f ? 1.0f : (inv < 1e-6f ? 1e-6f : inv);                           // (at most one segment a sample: the top pitch is about 3.5 kHz)
    }
    void gendy_walk() {
        float *a = fs(), *d = a + kGenN;
        float total = 0.0f;
        for (int i = 0; i < kGenN; i++) {
            float v = a[i] + rnd() * gd_astep_;
            v = v > 1.0f ? 2.0f - v : (v < -1.0f ? -2.0f - v : v);                           // mirror at +-1
            a[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            float l = d[i] + rnd() * gd_dstep_;
            l = l > 1.6f ? 3.2f - l : (l < 0.4f ? 0.8f - l : l);                             // mirror at 0.4 and 1.6
            d[i] = l > 1.6f ? 1.6f : (l < 0.4f ? 0.4f : l);
            total += d[i];
        }
        gd_total_ = total;
    }
    int32_t gendy(uint32_t inc) {
        const float y = gd_a0_ + (gd_a1_ - gd_a0_) * gd_t_;
        gd_t_ += gd_inv_;
        if (gd_t_ >= 1.0f) {
            gd_t_ -= 1.0f;
            if (++gd_seg_ == kGenN) { gd_seg_ = 0; gendy_walk(); }
            gendy_begin(inc);
        }
        const float o = y - gd_x_ + 0.995f * gd_y_;                                          // the walk drifts: a DC blocker (about 35 Hz)
        gd_x_ = y; gd_y_ = o;
        return static_cast<int32_t>(o * 24000.0f);
    }

    // CHAOS: the Roessler attractor (a = b = 0.2, c from Morph), midpoint steps. One loop of the orbit is about 6 time units, so the step is
    // 6 f / sample rate, split in steps of at most 0.15. Timbre mixes the smooth x with the spiky z. A guard restarts the orbit if it leaves its box.
    int32_t chaos(uint32_t inc) {
        const float dt = static_cast<float>(inc) * (6.0f / 4294967296.0f);
        int n = 1 + static_cast<int>(dt * (1.0f / 0.15f));
        if (n > 8) n = 8;
        const float h = dt / static_cast<float>(n), hh = 0.5f * h, c = ch_c_;
        float x = ch_x_, y = ch_y_, z = ch_z_;
        for (int k = 0; k < n; k++) {
            const float mx = x + hh * (-y - z), my = y + hh * (x + 0.2f * y), mz = z + hh * (0.2f + z * (x - c));
            x += h * (-my - mz); y += h * (mx + 0.2f * my); z += h * (0.2f + mz * (mx - c));
        }
        if (!(std::fabs(x) < 100.0f && std::fabs(y) < 100.0f && z > -50.0f && z < 300.0f)) { x = 1.0f; y = 1.0f; z = 1.0f; }       // (also catches NaN)
        ch_x_ = x; ch_y_ = y; ch_z_ = z;
        const float v = x * (0.1f * (1.0f - ch_mix_)) + z * (0.06f * ch_mix_);
        const float o = v - ch_dx_ + 0.997f * ch_dy_;                                        // the attractor's mean: a DC blocker
        ch_dx_ = v; ch_dy_ = o;
        return static_cast<int32_t>((o > 1.0f ? 1.0f : (o < -1.0f ? -1.0f : o)) * 23000.0f);
    }

    // CA: a 64-cell row of an elementary automaton (Wolfram's rule numbers, a ring) is the wave. Each generation (control rate) is smoothed
    // (two [1 2 1] passes), its mean removed and its peak set to 1, and the playing wave glides to it, so a generation is a morph step.
    void ca_step() {
        const uint64_t l = (row_ << 1) | (row_ >> 63), r = (row_ >> 1) | (row_ << 63);
        uint64_t nx = 0;
        for (int k = 0; k < 8; k++) {
            if (!((ca_rule_ >> k) & 1)) continue;
            nx |= ((k & 4) ? l : ~l) & ((k & 2) ? row_ : ~row_) & ((k & 1) ? r : ~r);       // the neighbourhood (left, centre, right) = k
        }
        row_ = nx;
        if (row_ == 0 || row_ == ~0ull) row_ = (static_cast<uint64_t>(noise_.next_u32()) << 32) | noise_.next_u32();     // an automaton that died: a new seed
        float *tgt = fs() + kCaN;
        float a[kCaN], b[kCaN];
        for (int i = 0; i < kCaN; i++) a[i] = ((row_ >> i) & 1) ? 1.0f : -1.0f;
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < kCaN; i++) b[i] = 0.25f * (a[(i + kCaN - 1) & (kCaN - 1)] + 2.0f * a[i] + a[(i + 1) & (kCaN - 1)]);
            for (int i = 0; i < kCaN; i++) a[i] = b[i];
        }
        float mean = 0.0f;
        for (int i = 0; i < kCaN; i++) mean += a[i];
        mean *= 1.0f / static_cast<float>(kCaN);
        float pk = 1e-6f;
        for (int i = 0; i < kCaN; i++) { a[i] -= mean; pk = std::fmax(pk, std::fabs(a[i])); }
        const float g = 1.0f / pk;
        for (int i = 0; i < kCaN; i++) tgt[i] = a[i] * g;
    }
    void ca_block(int n) {
        if (ca_period_ > 0.0f) {
            ca_count_ += static_cast<float>(n);
            while (ca_count_ >= ca_period_) { ca_count_ -= ca_period_; ca_step(); }
        }
        float k = ca_period_ > 0.0f ? static_cast<float>(n) / (ca_period_ * 0.35f) : 1.0f;      // the glide takes about a third of a generation
        if (k > 1.0f) k = 1.0f;
        float *cur = fs(), *tgt = cur + kCaN;
        for (int i = 0; i < kCaN; i++) cur[i] += (tgt[i] - cur[i]) * k;
    }
    int32_t ca(uint32_t inc) {
        ph_[0] += inc;
        const int i0 = static_cast<int>(ph_[0] >> 26);
        const float fr = static_cast<float>((ph_[0] >> 10) & 0xFFFF) * (1.0f / 65536.0f);
        const float *w = fs();
        const float a = w[i0], b = w[(i0 + 1) & (kCaN - 1)];
        return static_cast<int32_t>((a + (b - a) * fr) * 23000.0f);
    }

    /* ---- PULSAR, TONEW, FOF, TERRA, SCAN, WIND, FOLEY ---- */
    float rnd01() { return static_cast<float>(noise_.next_u32() >> 8) * (1.0f / 16777216.0f); }     // 0 .. 1

    // PULSAR (Roads): each period holds one pulsaret, a few cycles of a sine under a raised-cosine window, filling a fraction of the period (Timbre),
    // then silence. Morph is the cycles in the pulsaret (0.5 .. 16): with the fraction it sets the formant, above the pitch. (No masking pattern.)
    int32_t pulsar(uint32_t inc) {
        ph_[0] += inc;
        const float u = static_cast<float>(ph_[0]) * (pu_inv_ * (1.0f / 4294967296.0f));
        if (u >= 1.0f) return 0;
        float c = u * pu_n_;
        c -= std::floor(c);
        const float sn = static_cast<float>(sine(static_cast<uint32_t>(c * 4294967040.0f))) * (1.0f / 32768.0f);
        const float w = static_cast<float>(sine(static_cast<uint32_t>(u * 2147483648.0f))) * (1.0f / 32768.0f);
        return static_cast<int32_t>(sn * w * w * 24000.0f);
    }

    // TONEW: nine sines of a tonewheel organ's drawbars (16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'), all multiples of half the pitch, each a table
    // read at a whole multiple of one phase. Timbre brings the drawbars in (8' alone .. all nine), Morph is the key click and the leakage bed.
    int32_t tonew(uint32_t inc) {
        ph_[0] += inc >> 1;
        int32_t s = 0;
        for (int j = 0; j < 9; j++) s += tw_a_[j] * sine(ph_[0] * kTwMul[j]);
        s >>= 15;
        const int32_t n = noise_.next();
        s += (n * (tw_leak_ + tw_ck_)) >> 15;
        tw_ck_ -= tw_ck_ >> 8;
        return s;
    }

    // FOF: formant wave functions as four damped sines (resonators on the phasor trick) that the pitch's pulse strikes: the formants stay where
    // they are as the pitch moves. Timbre moves through the vowels A E I O U, Morph is the bandwidth (the length of each burst). (No attack ramp.)
    int32_t fof(uint32_t inc) {
        ph_[0] += inc;
        const bool wrap = ph_[0] < inc;
        float sum = 0.0f;
        for (int k = 0; k < 4; k++) {
            const float x = fof_x_[k], y = fof_y_[k];
            fof_x_[k] = fof_rc_[k] * x - fof_rs_[k] * y;
            fof_y_[k] = fof_rs_[k] * x + fof_rc_[k] * y + (wrap ? fof_a_[k] : 0.0f);
            sum += fof_y_[k];
        }
        return static_cast<int32_t>(sum);
    }

    // TERRA: a 32 x 32 surface (a sum of six periodic waves, the same for every instance, built once into the scratch) read along a circular orbit:
    // the phase is the angle, Timbre the radius, Morph moves the orbit's centre. Bilinear, wrapped, then a DC blocker.
    void terra_build() {
        float *z = fs();
        static const uint8_t kx[6] = {1, 2, 0, 3, 1, 2}, ky[6] = {1, 0, 2, 1, 3, 2};
        Noise nz(0x7E44A1u);
        float sx[6][kTerN], cx[6][kTerN], sy[6][kTerN], cy[6][kTerN], amp[6];
        for (int j = 0; j < 6; j++) {
            const uint32_t phx = nz.next_u32(), phy = nz.next_u32();
            amp[j] = 1.0f / static_cast<float>(kx[j] + ky[j]);
            for (int i = 0; i < kTerN; i++) {
                const uint32_t px = phx + static_cast<uint32_t>(i) * (0x08000000u * kx[j]), py = phy + static_cast<uint32_t>(i) * (0x08000000u * ky[j]);
                sx[j][i] = static_cast<float>(sine(px)); cx[j][i] = static_cast<float>(sine(px + 0x40000000u));
                sy[j][i] = static_cast<float>(sine(py)); cy[j][i] = static_cast<float>(sine(py + 0x40000000u));
            }
        }
        float mean = 0.0f, pk = 1e-6f;
        for (int iy = 0; iy < kTerN; iy++) for (int ix = 0; ix < kTerN; ix++) {
            float v = 0.0f;
            for (int j = 0; j < 6; j++) v += amp[j] * (sx[j][ix] * cy[j][iy] + cx[j][ix] * sy[j][iy]);        // sin(x + y) = sin x cos y + cos x sin y
            z[iy * kTerN + ix] = v; mean += v;
        }
        mean *= 1.0f / static_cast<float>(kTerN * kTerN);
        for (int i = 0; i < kTerN * kTerN; i++) { z[i] -= mean; pk = std::fmax(pk, std::fabs(z[i])); }
        const float g = 1.0f / pk;
        for (int i = 0; i < kTerN * kTerN; i++) z[i] *= g;
    }
    int32_t terra(uint32_t inc) {
        ph_[0] += inc;
        const float c = static_cast<float>(sine(ph_[0] + 0x40000000u)) * (1.0f / 32768.0f), sn = static_cast<float>(sine(ph_[0])) * (1.0f / 32768.0f);
        const float u = (1.0f + te_cx_ + te_r_ * c) * static_cast<float>(kTerN), v = (1.0f + te_cy_ + te_r_ * sn) * static_cast<float>(kTerN);
        const int ix = static_cast<int>(u), iy = static_cast<int>(v);
        const float fx = u - static_cast<float>(ix), fy = v - static_cast<float>(iy);
        const float *z = fs();
        const int x0 = ix & (kTerN - 1), x1 = (ix + 1) & (kTerN - 1), y0 = (iy & (kTerN - 1)) * kTerN, y1 = ((iy + 1) & (kTerN - 1)) * kTerN;
        const float a = z[y0 + x0] + (z[y0 + x1] - z[y0 + x0]) * fx, b = z[y1 + x0] + (z[y1 + x1] - z[y1 + x0]) * fx;
        const float val = a + (b - a) * fy;
        const float o = val - te_dx_ + 0.997f * te_dy_;
        te_dx_ = val; te_dy_ = o;
        return static_cast<int32_t>(o * 22000.0f);
    }

    // SCAN (scanned synthesis): a ring of 32 masses joined by springs, moving 400 times a second (the chain's own slow motion), is the wave an
    // oscillator scans at the pitch. A note plucks it. Timbre is the stiffness (the speed of the ripples), Morph the sustain (the damping).
    // Scratch: x[32], v[32], the wave played, the wave's target.
    void scan_step() {
        float *x = fs(), *v = x + kScN;
        for (int i = 0; i < kScN; i++) {
            const float f = sc_k_ * (x[(i + kScN - 1) & (kScN - 1)] + x[(i + 1) & (kScN - 1)] - 2.0f * x[i]) - 0.02f * x[i];
            v[i] = (v[i] + f) * (1.0f - sc_damp_);
        }
        float mean = 0.0f;
        for (int i = 0; i < kScN; i++) { x[i] += v[i]; mean += x[i]; }
        mean *= 1.0f / static_cast<float>(kScN);
        float *tg = x + 3 * kScN;
        for (int i = 0; i < kScN; i++) tg[i] = x[i] - mean;
    }
    void scan_block(int n) {
        sc_count_ += static_cast<float>(n);
        while (sc_count_ >= sc_period_) { sc_count_ -= sc_period_; scan_step(); }
        float k = static_cast<float>(n) / (sc_period_ * 0.5f);
        if (k > 1.0f) k = 1.0f;
        float *cur = fs() + 2 * kScN;
        const float *tg = fs() + 3 * kScN;
        for (int i = 0; i < kScN; i++) cur[i] += (tg[i] - cur[i]) * k;
    }
    int32_t scan(uint32_t inc) {
        ph_[0] += inc;
        const int i0 = static_cast<int>(ph_[0] >> 27);
        const float fr = static_cast<float>((ph_[0] >> 11) & 0xFFFF) * (1.0f / 65536.0f);
        const float *w = fs() + 2 * kScN;
        const float a = w[i0], b = w[(i0 + 1) & (kScN - 1)];
        return static_cast<int32_t>((a + (b - a) * fr) * 21000.0f);
    }

    // WIND: a reed instrument on a waveguide (a clarinet's bore: one delay line of half a period with an inverting, low-passed reflection, the reed's
    // table at its end). The gate is the breath (a 10 ms ramp), Timbre the breath pressure, Morph the brightness of the bore.
    // The same at any sample rate: the reflection works once per round trip (per period), the bore's low pass and the output's DC blocker
    // are cutoffs in Hz (their 48 kHz values converted). Lowest note: kWgLen bounds the bore, 23 Hz at 48 kHz, 47 Hz at 96 kHz.
    static inline const float kWindDc = kSampleRate == 48000 ? 0.995f : std::pow(0.995f, 48000.0f / static_cast<float>(kSampleRate));
    int32_t wind() {
        float *buf = fs();
        wd_env_ += ((gate_ ? 1.0f : 0.0f) - wd_env_) * (1.0f / (0.01f * static_cast<float>(kSampleRate)));
        const float breath = wd_env_ * wd_breath_ * (1.0f + 0.04f * rnd());
        float pos = static_cast<float>(wd_w_) - wd_len_;
        if (pos < 0.0f) pos += static_cast<float>(kWgLen);
        const int i0 = static_cast<int>(pos);
        const float fr = pos - static_cast<float>(i0);
        const float b0 = buf[i0 & (kWgLen - 1)];
        const float d = b0 + (buf[(i0 + 1) & (kWgLen - 1)] - b0) * fr;
        wd_lp_ += wd_a_ * (d - wd_lp_);
        const float pd = -0.95f * wd_lp_ - breath;
        float rt = 0.7f - 0.3f * pd;
        rt = rt > 1.0f ? 1.0f : (rt < -1.0f ? -1.0f : rt);
        float in = breath + pd * rt;
        in = in > 4.0f ? 4.0f : (in < -4.0f ? -4.0f : in);
        buf[wd_w_] = in;
        wd_w_ = (wd_w_ + 1) & (kWgLen - 1);
        const float o = d - wd_dx_ + kWindDc * wd_dy_;
        wd_dx_ = d; wd_dy_ = o;
        return static_cast<int32_t>(o * 16000.0f);
    }

    // FOLEY: procedural ambience from layers of filtered noise and random events. Timbre crosses rain -> wind -> fire -> crackle, Morph is the
    // intensity (how many drops and crackles, how loud); the pitch is the cutoff of the wind and the fire's rumble.
    int32_t foley(uint32_t) {
        const float n = rnd();
        float y = 0.0f;
        if (fo_rain_ > 0.0f) {                                                               // rain: hiss + drops (a ping of 2..6 kHz, random size)
            fo_l1_ += 0.3f * (n - fo_l1_);
            if (noise_.next_u32() < fo_drop_) { fo_ping_ = 0.3f + 0.7f * rnd01(); fo_pinc_ = hz_to_inc(2000.0f + 4000.0f * rnd01()); }
            fo_ping_ *= 0.9965f;
            fo_pph_ += fo_pinc_;
            y += fo_rain_ * ((n - fo_l1_) * 0.25f * fo_int_ + fo_ping_ * static_cast<float>(sine(fo_pph_)) * (0.5f / 32768.0f));
        }
        if (fo_wind_ > 0.0f || fo_fire_ > 0.0f) {                                            // wind: two low passes of noise under slow gusts; fire: a rumble
            fo_l2_ += fo_wc_ * (n - fo_l2_);
            fo_l3_ += fo_wc_ * (fo_l2_ - fo_l3_);
            if (--fo_cnt_ <= 0) { fo_cnt_ = static_cast<int>(static_cast<float>(kSampleRate) * (0.3f + 0.6f * rnd01())); fo_gust_t_ = 0.3f + 0.7f * rnd01(); }
            fo_gust_ += (fo_gust_t_ - fo_gust_) * 0.0002f;
            y += fo_wind_ * fo_l3_ * fo_gust_ * fo_int_ * 6.0f + fo_fire_ * fo_l3_ * (0.5f + 0.5f * fo_gust_) * 3.0f;
        }
        if (fo_fire_ > 0.0f || fo_crk_ > 0.0f) {                                             // crackles: short noise bursts of random size
            if (noise_.next_u32() < fo_crack_) fo_crack_env_ = (0.15f + rnd01() * rnd01()) * (fo_crk_ > 0.0f ? 1.5f : 0.8f);
            fo_crack_env_ *= 0.985f;
            const float c = fo_crack_env_ * n * fo_int_;
            y += (fo_fire_ + 1.4f * fo_crk_) * c;
        }
        return static_cast<int32_t>((y > 1.0f ? 1.0f : (y < -1.0f ? -1.0f : y)) * 22000.0f);
    }

    Heap *heap_ = nullptr;
    int16_t *ks_ = nullptr;
    uint32_t a4_ = 0, one_hz_ = 0;
    int32_t engine_ = OSCX_KARP, pitch_ = 60 * kSemi, pmod_ = 12 * kSemi;
    q15 timbre_ = 16384, morph_ = 16384, level_ = kUnity;
    bool gate_ = false;
    Noise noise_{0xA11CE5u};
    // tables (init)
    const Tabs *t_ = nullptr;
    // per block
    int32_t ks_d_ = 100 << 16, ks_a_ = 16000, ks_g_ = 32000;
    int32_t dec_[kModes] = {}, ratio_[kModes] = {}, fm_ratio_ = 65536, harm_amp_[kHarm] = {}, vw_[3] = {};
    uint32_t saw_inc_[kSaws] = {}, dust_thr_ = 0;
    int32_t mrc_[kModes] = {}, mrs_[kModes] = {};            // modal: r cos theta, r sin theta per mode (Q30)
    bool prep_ok_ = false;                                    // the last prepare() was for these values
    int32_t prep_engine_ = 0;
    uint32_t prep_inc_ = 0;
    q15 prep_timbre_ = 0, prep_morph_ = 0;
    int add_top_ = 0;                                         // highest harmonic with a non-zero amplitude
    SvfCoefF vc_[3], ds_c_;
    // GENDY / CHAOS / CA (their arrays are in the Karplus buffer)
    int live_engine_ = -1, gd_seg_ = 0, ca_rule_ = 30;
    float gd_astep_ = 0.0f, gd_dstep_ = 0.0f, gd_total_ = 12.0f, gd_t_ = 0.0f, gd_inv_ = 0.01f, gd_a0_ = 0.0f, gd_a1_ = 0.0f, gd_x_ = 0.0f, gd_y_ = 0.0f;
    float ch_c_ = 6.0f, ch_mix_ = 0.0f, ch_x_ = 1.0f, ch_y_ = 1.0f, ch_z_ = 1.0f, ch_dx_ = 0.0f, ch_dy_ = 0.0f;
    float ca_period_ = 0.0f, ca_count_ = 0.0f;
    uint64_t row_ = 1;
    // PULSAR, TONEW, FOF, TERRA, SCAN, WIND, FOLEY
    static constexpr uint32_t kTwMul[9] = {1, 3, 2, 4, 6, 8, 10, 12, 16};
    int scratch_owner_ = -1, wd_w_ = 0, fo_cnt_ = 0;
    float pu_inv_ = 1.0f, pu_n_ = 2.0f;
    int32_t tw_a_[9] = {}, tw_click_ = 0, tw_leak_ = 0, tw_ck_ = 0;
    float fof_rc_[4] = {}, fof_rs_[4] = {}, fof_a_[4] = {}, fof_x_[4] = {}, fof_y_[4] = {};
    float te_r_ = 0.25f, te_cx_ = 0.5f, te_cy_ = 0.5f, te_dx_ = 0.0f, te_dy_ = 0.0f;
    float sc_k_ = 0.3f, sc_damp_ = 0.01f, sc_period_ = 100.0f, sc_count_ = 0.0f;
    float wd_a_ = 0.6f, wd_len_ = 100.0f, wd_breath_ = 0.6f, wd_lp_ = 0.0f, wd_env_ = 0.0f, wd_dx_ = 0.0f, wd_dy_ = 0.0f;
    float fo_rain_ = 1.0f, fo_wind_ = 0.0f, fo_fire_ = 0.0f, fo_crk_ = 0.0f, fo_int_ = 0.5f, fo_wc_ = 0.05f, fo_l1_ = 0.0f, fo_l2_ = 0.0f, fo_l3_ = 0.0f;
    float fo_ping_ = 0.0f, fo_crack_env_ = 0.0f, fo_gust_ = 0.5f, fo_gust_t_ = 0.5f;
    uint32_t fo_drop_ = 0, fo_crack_ = 0, fo_pinc_ = 0, fo_pph_ = 0;
    float vwf_[3] = {};
    float ds_gain_ = 32768.0f * 14.0f;
    // state
    uint32_t ph_[kSaws] = {}, fm_ph_ = 0;
    int32_t qual_ = 0, str_wave_ = WT_SAW;                    // STR: quality (0 Blep = Mip here, 1 Mip, 2 Naive), wave from Morph
    bool str_mip_ = true;
    uint32_t str_inc_[2] = {};
    const int16_t *str_tab_[2] = {nullptr, nullptr};
    int32_t mx_[kModes] = {}, my_[kModes] = {}, lp_ = 0;      // modal phasors
    int w_ = 0;
    SvfStateF sv_[3], ds_;
};

template <typename T>
ModuleType type_of() { return module_type_of<T>(); }

}  // namespace

#if defined(ENGINE_PROFILE) && defined(ARDUINO_ARCH_ESP32)
uint32_t g_osc_prof[OSCX_ENGINES];
#endif

void register_osc_engines(Registry &r) { r.add(T_OSCX, type_of<OscEngines>()); }

}  // namespace sc
