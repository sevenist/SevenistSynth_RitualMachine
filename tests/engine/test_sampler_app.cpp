// The sampler as the application uses it: a rack with an SM module, the sample library (catalog + storage), the loader
// pumped between renders. Whole path: rack_t -> mapper -> engine -> Sampler -> bank -> simulated card.
#include <algorithm>
#include <vector>
#include "rig.h"
#include "platform/sim/sim_storage.h"
#include "engine/sampler/smp_format.h"
#include "platform/engine/engine_synth.h"
#include "platform/engine/sample_catalog.h"

extern "C" {
#include "core/rack.h"
#include "core/synth_config.h"
#include "core/synth_params.h"
}

using namespace tst;

namespace {
const double kPi2 = 6.28318530717958647692;

std::vector<int16_t> sine_pcm(double hz, size_t frames, double amp = 0.5) {
    std::vector<int16_t> x(frames);
    for (size_t i = 0; i < frames; i++) x[i] = static_cast<int16_t>(amp * 32767.0 * std::sin(kPi2 * hz * static_cast<double>(i) / kSampleRate));
    return x;
}

struct SamplerApp {
    std::vector<uint8_t> fast, bulk;
    SimStorage card{StorageModel{500, 4000000, 0, 0}};
    audio_sample_info_t cat[3];
    SamplerApp() : fast(6u << 20), bulk(6u << 20) {
        CHECK_EQ(engine_synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), 0);
        CHECK(engine_synth_attach_storage(&card));
    }
    ~SamplerApp() { engine_synth_shutdown(); }

    void add(int index, const char *name, const std::vector<int16_t> &pcm, int root, const std::vector<uint32_t> &slices = {}) {
        SmpHeader h;
        h.sample_rate = static_cast<uint32_t>(kSampleRate);
        h.frames = static_cast<uint32_t>(pcm.size());
        h.root_note = static_cast<uint16_t>(root);
        h.slice_count = static_cast<uint8_t>(slices.size());
        for (size_t i = 0; i < slices.size(); i++) h.slice[i] = slices[i];
        card.add_file((std::string(name) + ".smp").c_str(), smp_build(h, pcm.data()));
        audio_sample_info_t &in = cat[index];
        std::memset(&in, 0, sizeof in);
        std::snprintf(in.name, sizeof in.name, "%s", name);
        in.frames = h.frames; in.rate = h.sample_rate; in.root = static_cast<uint8_t>(root); in.slices = h.slice_count;
        for (size_t i = 0; i < slices.size(); i++) in.slice[i] = slices[i];
        engine_synth_catalog_set(cat, index + 1);
    }

    // renders with the loader pumped between pulls, like the I/O thread would
    std::vector<double> run(double seconds) {
        const int frames = static_cast<int>(seconds * engine_synth_sample_rate());
        std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
        int done = 0;
        while (done < frames) {
            const int n = std::min(frames - done, 64);
            engine_synth_render(&buf[static_cast<size_t>(done) * 2], n);
            engine_synth_io_pump();
            done += n;
        }
        std::vector<double> l(static_cast<size_t>(frames));
        for (int i = 0; i < frames; i++) l[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * 2];
        return l;
    }
};

double tone(const std::vector<double> &x, size_t from, size_t to, double hz) {
    double re = 0, im = 0;
    for (size_t i = from; i < to; i++) { const double ph = kPi2 * hz * static_cast<double>(i) / kSampleRate; re += x[i] * std::cos(ph); im += x[i] * std::sin(ph); }
    const double n = static_cast<double>(to - from);
    return (re * re + im * im) / (n * n);
}

int dominant(const std::vector<double> &x, size_t from, size_t to, const std::vector<double> &cands) {
    int best = 0;
    double bp = -1;
    for (size_t k = 0; k < cands.size(); k++) { const double p = tone(x, from, to, cands[k]); if (p > bp) { bp = p; best = static_cast<int>(k); } }
    return best;
}
}  // namespace

TEST(sm_module_plays_the_assigned_file_at_the_played_pitch) {
    SamplerApp a;
    a.add(0, "tone", sine_pcm(261.6256, static_cast<size_t>(kSampleRate)), 60);
    rack_t rack;
    synth_params_t params;
    synth_params_default(&params);
    params.amp_env.attack_ms = 2; params.amp_env.sustain = 1.0f;
    rack_clear(&rack);
    CHECK(rack_insert(&rack, 0, MOD_SAMPLER));
    rack.slot[0].v[MP_SM_FILE] = 1;                                         // the catalog's first file
    rack.slot[0].v[MP_SM_LOOP] = 0;                                         // File default (none here)
    engine_synth_build(&rack, &params);
    a.run(0.1);                                                             // the heads arrive through the loader
    engine_synth_note_on(60);
    std::vector<double> y = a.run(0.5);
    const size_t n = y.size();
    const double p261 = tone(y, n / 4, n, 261.6256), p523 = tone(y, n / 4, n, 523.2511);
    std::printf("    sample at its root: power at 261 Hz %.2e, at 523 Hz %.2e, rms %.0f\n", p261, p523, rms(y));
    CHECK(rms(std::vector<double>(y.begin() + static_cast<long>(n / 4), y.end())) > 3000.0);
    CHECK(p261 > 1000 * (p523 + 1.0));
    engine_synth_note_off(60);
    a.run(0.6);
    engine_synth_note_on(72);                                               // an octave up
    std::vector<double> z = a.run(0.4);
    CHECK(tone(z, z.size() / 4, z.size(), 523.2511) > 1000 * (tone(z, z.size() / 4, z.size(), 261.6256) + 1.0));
    // Crs +12 plays the same note an octave up (a live parameter edit)
    engine_synth_note_off(72);
    a.run(0.6);
    rack.slot[0].v[MP_SM_COARSE] = 12;
    engine_synth_set_params(&rack, &params);
    engine_synth_note_on(60);
    std::vector<double> w = a.run(0.4);
    CHECK(tone(w, w.size() / 4, w.size(), 523.2511) > 1000 * (tone(w, w.size() / 4, w.size(), 261.6256) + 1.0));
}

TEST(sm_slices_start_where_asked_and_drum_mode_ignores_the_note) {
    SamplerApp a;
    const size_t q = static_cast<size_t>(kSampleRate) / 4;                  // four quarter-second slices of different pitches
    std::vector<int16_t> pcm;
    const double f[4] = {220, 330, 440, 660};
    for (int k = 0; k < 4; k++) { std::vector<int16_t> s = sine_pcm(f[k], q); pcm.insert(pcm.end(), s.begin(), s.end()); }
    a.add(0, "slices", pcm, 60, {0, static_cast<uint32_t>(q), static_cast<uint32_t>(2 * q), static_cast<uint32_t>(3 * q)});
    rack_t rack;
    synth_params_t params;
    synth_params_default(&params);
    params.amp_env.attack_ms = 2; params.amp_env.sustain = 1.0f; params.amp_env.release_ms = 20;
    rack_clear(&rack);
    rack_insert(&rack, 0, MOD_SAMPLER);
    rack.slot[0].v[MP_SM_FILE] = 1;
    rack.slot[0].v[MP_SM_TRACK] = 1;                                        // Drum: original speed whatever the key
    rack.slot[0].v[MP_SM_SMODE] = 1;                                        // stop at the next slice
    engine_synth_build(&rack, &params);
    a.run(0.1);
    const std::vector<double> cands = {220, 330, 440, 660};
    for (int slice = 1; slice <= 4; slice++) {
        rack.slot[0].v[MP_SM_SLICE] = static_cast<float>(slice);
        engine_synth_set_params(&rack, &params);
        engine_synth_note_on(48 + slice * 5);                               // different keys: no effect in Drum mode
        std::vector<double> y = a.run(0.2);
        const int got = dominant(y, y.size() / 5, y.size() * 4 / 5, cands);
        std::printf("    slice %d: dominant %.0f Hz\n", slice, cands[static_cast<size_t>(got)]);
        CHECK_EQ(got, slice - 1);
        engine_synth_note_off(48 + slice * 5);
        a.run(0.15);
    }
    // stop-at-next-slice: slice 1 holds for a quarter second only, then falls silent
    rack.slot[0].v[MP_SM_SLICE] = 1;
    engine_synth_set_params(&rack, &params);
    engine_synth_note_on(60);
    std::vector<double> y = a.run(0.6);
    const double early = rms(std::vector<double>(y.begin() + 3000, y.begin() + 9000));
    const double late = rms(std::vector<double>(y.begin() + static_cast<long>(kSampleRate / 2), y.end()));
    std::printf("    stop mode: rms %.0f inside the slice, %.0f after it\n", early, late);
    CHECK(early > 3000.0 && late < 100.0);
}

TEST(a_file_that_is_not_in_the_library_makes_no_sound_and_no_node) {
    SamplerApp a;
    a.add(0, "tone", sine_pcm(440, static_cast<size_t>(kSampleRate)), 69);
    rack_t rack;
    synth_params_t params;
    synth_params_default(&params);
    rack_clear(&rack);
    rack_insert(&rack, 0, MOD_SAMPLER);
    rack.slot[0].v[MP_SM_FILE] = 9;                                         // beyond the library
    engine_synth_build(&rack, &params);
    engine_synth_note_on(69);
    std::vector<double> y = a.run(0.3);
    CHECK(rms(y) < 5.0);
    rack.slot[0].v[MP_SM_FILE] = 0;                                         // none
    engine_synth_set_params(&rack, &params);
    std::vector<double> z = a.run(0.2);
    CHECK(rms(z) < 5.0);
}

namespace {
SamplerApp *g_lazy_app = nullptr;
int g_lazy_conversions = 0;
bool lazy_convert(int index) {                                     // stands in for the platform's .wav / .mp3 converter
    if (!g_lazy_app || index != 0) return false;
    g_lazy_conversions++;
    g_lazy_app->add(0, "later", sine_pcm(330.0, static_cast<size_t>(kSampleRate)), 64);   // cooks the file, puts it on the card, updates the catalog
    return true;
}
}  // namespace

TEST(a_pending_file_is_converted_when_a_sampler_uses_it) {
    SamplerApp a;
    g_lazy_app = &a;
    g_lazy_conversions = 0;
    // the catalog lists the file, but it is not converted yet
    std::memset(&a.cat[0], 0, sizeof a.cat[0]);
    std::snprintf(a.cat[0].name, sizeof a.cat[0].name, "later");
    a.cat[0].pending = 1; a.cat[0].kind = 2;
    engine_synth_catalog_set(a.cat, 1);
    engine_synth_set_importer(&lazy_convert);
    audio_sample_info_t info;
    CHECK(engine_synth_sample_info(0, &info) && info.pending == 1);
    rack_t rack;
    synth_params_t params;
    synth_params_default(&params);
    params.amp_env.attack_ms = 2; params.amp_env.sustain = 1.0f;
    rack_clear(&rack);
    rack_insert(&rack, 0, MOD_SAMPLER);
    // not assigned: nothing converted
    engine_synth_build(&rack, &params);
    CHECK_EQ(g_lazy_conversions, 0);
    // assigned: the build converts it, loads it and it plays at its pitch (root 64 = 329.6 Hz on note 64)
    rack.slot[0].v[MP_SM_FILE] = 1;
    engine_synth_set_params(&rack, &params);
    CHECK_EQ(g_lazy_conversions, 1);
    CHECK(engine_synth_sample_info(0, &info) && info.pending == 0 && info.frames > 0);
    a.run(0.1);
    engine_synth_note_on(64);
    std::vector<double> y = a.run(0.4);
    CHECK(rms(std::vector<double>(y.begin() + 4000, y.end())) > 2000.0);
    CHECK(tone(y, y.size() / 4, y.size(), 329.6276) > 1000 * (tone(y, y.size() / 4, y.size(), 261.6256) + 1.0));
    engine_synth_set_importer(nullptr);
    g_lazy_app = nullptr;
}
