// A listening test, not a measurement: every oscillator of the rack's Wav list, one after the other, through the production path (rack ->
// mapper -> engine): C3 held for 1.5 s while the Timbre control (the OSC's PW row) sweeps over its whole range (0.05 .. 0.95), then a
// short release. The percussive ones (kPercussive below) play twice. Nothing is checked except that the engine starts.
//
// Output: build/osc_sweep/all_oscillators.wav (16-bit mono) and all_oscillators.txt (the time at which each one starts, its Wav value and
// type A..E). The Morph / Harm / Q rows stay at their defaults.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include "rig.h"
#include "rack_m.h"
#include "platform/engine/engine_synth.h"

extern "C" {
#include "core/rack.h"
#include "core/synth_config.h"
#include "core/synth_params.h"
}

using namespace tst;

namespace {

const int kNote = 48;                                      // C3
const double kHold = 1.5, kRelease = 0.4, kGap = 0.3;      // seconds: the note, what rings after it, the silence between oscillators
const double kTimbreLo = 0.05, kTimbreHi = 0.95;
const char *const kOutDir = "build/osc_sweep";
// the ones played twice (drums and the plucked / struck ones that need a new hit to be heard again); edit freely
const char *const kPercussive[] = {"Kick", "PBass", "PBassS", "BSnare", "PSnare", "PSnrS", "PHat", "PHat2", "Cymbal", "Karp", "Modal"};

bool percussive(const char *name) {
    for (const char *p : kPercussive) if (std::string(p) == name) return true;
    return false;
}

void write_wav(const std::string &path, const std::vector<int16_t> &s, int rate) {
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t bytes = static_cast<uint32_t>(s.size() * 2);
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(static_cast<uint32_t>(rate)); u32(static_cast<uint32_t>(rate) * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    std::fwrite(s.data(), 2, s.size(), f);
    std::fclose(f);
}

}  // namespace

TEST(every_oscillator_plays_a_c3_with_a_timbre_sweep) {
    std::filesystem::create_directories(kOutDir);
    std::vector<uint8_t> fast(6u << 20), bulk(6u << 20);
    CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
    const int rate = engine_synth_sample_rate();
    const int step = rate / 50;                                            // the control moves every 20 ms
    std::vector<int16_t> all, buf(static_cast<size_t>(step) * 2);
    std::string index;

    // renders `seconds` into `all`; the sweep runs from t0 to t1 (fractions of the note) over the rendered time
    auto render = [&](rack_t &rack, synth_params_t &params, double seconds, double t0, double t1) {
        const int total = static_cast<int>(seconds * rate);
        for (int done = 0; done < total; done += step) {
            if (t1 >= 0) {
                const double t = t0 + (t1 - t0) * static_cast<double>(done) / total;
                rack.slot[0].v[MP_OC_PW] = static_cast<float>(kTimbreLo + (kTimbreHi - kTimbreLo) * t);
                engine_synth_set_params(&rack, &params);
            }
            engine_synth_render(buf.data(), step);
            for (int i = 0; i < step; i++) all.push_back(buf[static_cast<size_t>(i) * 2]);
        }
    };

    for (int wav = 0; wav < OC_WAVE_COUNT; wav++) {
        rack_t rack;
        synth_params_t params;
        synth_params_default(&params);
        rack_init(&rack);
        rack_m_dry(rack);
        rack_clear(&rack);
        CHECK(rack_insert(&rack, 0, MOD_OSC));
        rack.slot[0].v[MP_OC_WAVE] = static_cast<float>(wav);
        rack.slot[0].v[MP_OC_PW] = static_cast<float>(kTimbreLo);
        params.amp_env.attack_ms = 2; params.amp_env.decay_ms = 1; params.amp_env.sustain = 1.0f; params.amp_env.release_ms = 100;
        params.amp_env.a_curve = params.amp_env.d_curve = params.amp_env.r_curve = 0;
        engine_synth_build(&rack, &params);

        char name[24];
        rack_mparam_format(&rack.slot[0], MP_OC_WAVE, name, sizeof name);
        const int times = percussive(name) ? 2 : 1;

        char line[96];
        std::snprintf(line, sizeof line, "%6.1f s  Wav %2d  %-7s  type %c%s\n", static_cast<double>(all.size()) / rate, wav, name, rack_wave_class(wav), times > 1 ? "  (twice)" : "");
        index += line;
        std::printf("    %s", line);

        for (int k = 0; k < times; k++) {
            engine_synth_note_on(kNote);
            render(rack, params, kHold, 0.0, 1.0);                         // the sweep restarts with each hit
            engine_synth_note_off(kNote);
            render(rack, params, kRelease, 1.0, -1.0);
        }
        all.insert(all.end(), static_cast<size_t>(kGap * rate), 0);
    }
    write_wav(std::string(kOutDir) + "/all_oscillators.wav", all, rate);
    if (std::FILE *f = std::fopen((std::string(kOutDir) + "/all_oscillators.txt").c_str(), "wb")) { std::fputs(index.c_str(), f); std::fclose(f); }
    std::printf("    %.0f s of audio in %s/all_oscillators.wav\n", static_cast<double>(all.size()) / rate, kOutDir);
    engine_synth_shutdown();
}
