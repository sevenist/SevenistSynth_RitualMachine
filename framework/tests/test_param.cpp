// The parameter descriptor (control/param.h): steps, fine steps, encoder detents, positions and both text styles.
#include <cstring>
#include "control/param.h"
#include "test.h"

namespace {
const char *const kWaves[] = {"Sine", "Saw", "Sq"};
// the Ritual Machine's style
const param_desc_t kCut = {"Cut", 20, 18000, 4000, "Hz", PARAM_LOG, nullptr, 1.12f, 0, PARAM_TEXT_FIXED};
const param_desc_t kSus = {"Sus", 0, 1, 0, "", PARAM_LIN, nullptr, 0.05f, 2, PARAM_TEXT_FIXED};
const param_desc_t kCrs = {"Crs", -24, 24, 0, "st", PARAM_LIN, nullptr, 1, 0, PARAM_TEXT_FIXED};
const param_desc_t kWav = {"Wav", 0, 2, 0, "", PARAM_ENUM, kWaves, 1, 0, PARAM_TEXT_FIXED};
// SynthLab's style (the short initializer)
const param_desc_t kDecay = {"Decay", 5, 4000, 300, "ms", PARAM_LOG};
const param_desc_t kMix = {"Mix", 0, 100, 50, "%"};
const param_desc_t kShape = {"Shape", 0, 2, 0, nullptr, PARAM_INT, kWaves};

bool text_is(const param_desc_t &d, float v, const char *want) {
    char out[32];
    pdesc_format(&d, v, out, sizeof out);
    if (std::strcmp(out, want)) std::printf("    %s %g: '%s', not '%s'\n", d.name, (double)v, out, want);
    return !std::strcmp(out, want);
}
}  // namespace

TEST(param_text_in_both_styles) {
    CHECK(text_is(kCut, 4000, "4.0kHz"));
    CHECK(text_is(kCut, 350, "350Hz"));
    CHECK(text_is(kSus, 0.7f, "0.70"));
    CHECK(text_is(kWav, 1, "Saw"));
    CHECK(text_is(kDecay, 1200, "1.20 s"));
    CHECK(text_is(kDecay, 300, "300 ms"));
    CHECK(text_is(kDecay, 12.5f, "12.5 ms"));
    CHECK(text_is(kMix, 50, "50 %"));
    CHECK(text_is(kShape, 2, "Sq"));
}

TEST(param_steps_add_multiply_and_clamp) {
    CHECK_NEAR(pdesc_step(&kCut, 4000, 1, false), 4480, 0.01);
    CHECK_NEAR(pdesc_step(&kCut, 4000, -1, false), 4000 / 1.12f, 0.01);
    CHECK_EQ(pdesc_step(&kCut, 18000, 1, false), 18000);                   // clamped
    CHECK_NEAR(pdesc_step(&kSus, 0.5f, 1, false), 0.55f, 1e-6);
    CHECK_EQ(pdesc_step(&kWav, 2, 1, false), 2);                           // the last choice stays
    CHECK_EQ(pdesc_step(&kWav, 0, 1, true), 1);                            // a choice keeps its step when fine
}

TEST(param_fine_steps_are_a_fifth_but_never_finer_than_shown) {
    CHECK_NEAR(pdesc_step(&kSus, 0.5f, 1, true), 0.51f, 1e-6);            // 0.05 / 5 = 0.01: two decimals show it
    CHECK_EQ(pdesc_step(&kCrs, 0, 1, true), 1);                            // 1 st / 5 is finer than whole: 1 st
    CHECK_NEAR(pdesc_step(&kCut, 1000, 1, true), 1000 * std::pow(1.12f, 0.2f), 0.01);
}

TEST(param_detents_move_a_hundredth_of_the_range) {
    CHECK_EQ(pdesc_detents(&kMix, 50, 1), 51);
    CHECK_EQ(pdesc_detents(&kMix, 50.4f, 0), 50);                          // snapped to the steps
    CHECK_NEAR(pdesc_detents(&kDecay, 300, 10), 300 * std::pow(4000.0f / 5, 0.1f), 0.01);
    CHECK_EQ(pdesc_detents(&kShape, 1, 5), 2);                             // whole numbers, clamped
}

TEST(param_positions_follow_the_scale_and_round_to_what_shows) {
    CHECK_NEAR(pdesc_to_norm(&kCut, std::sqrt(20.0f * 18000.0f)), 0.5, 1e-5);   // the middle of a log range: the geometric mean
    CHECK_NEAR(pdesc_to_norm(&kMix, 25), 0.25, 1e-6);
    CHECK_EQ(pdesc_from_norm(&kCrs, 0.51f), 0);                            // 0.48 st: whole semitones
    CHECK_NEAR(pdesc_from_norm(&kSus, 0.333f), 0.33f, 1e-6);               // two decimals
    CHECK_EQ(pdesc_from_norm(&kShape, 0.8f), 2);
    CHECK(!pdesc_is_continuous(&kWav) && pdesc_is_continuous(&kShape));
    CHECK_EQ(pdesc_choices(&kShape), 3);
    CHECK_EQ(pdesc_choices(&kMix), 0);
}
