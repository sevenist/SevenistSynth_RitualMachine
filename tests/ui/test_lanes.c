// Rack lanes, Sum points and FX modules (ADR-040): the model's rules (rack.c). The editor and the engine come later.
#include "ui_test.h"
#include <math.h>

#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) < (eps))

static int count_lane(const rack_t *r, int lane) { int n = 0; for (int i = 0; i < r->count; i++) n += r->slot[i].lane == lane; return n; }

TEST(lanes_keep_their_own_chains) {
    rack_t r;
    rack_clear(&r);
    CHECK(rack_insert_lane(&r, 0, 0, MOD_OSC));                  // lane A: OC FL
    CHECK(rack_insert_lane(&r, 1, 1, MOD_OSC));                  // lane B: OC SA (interleaved in the slot order)
    CHECK(rack_insert_lane(&r, 2, 0, MOD_FILTER));
    CHECK(rack_insert_lane(&r, 3, 1, MOD_SAT));
    CHECK_EQ(count_lane(&r, 0), 2);
    CHECK_EQ(rack_audio_next(&r, 0), 2);                          // the filter, skipping lane B's oscillator
    CHECK_EQ(rack_audio_next(&r, 1), 3);
    CHECK_EQ(rack_audio_next(&r, 2), RACK_OUT);
    CHECK_EQ(rack_audio_prev(&r, 3), 1);
    CHECK(rack_has_signal(&r, 3));
    rack_t e;
    rack_clear(&e);
    CHECK(rack_insert_lane(&e, 0, 0, MOD_OSC));
    CHECK(rack_insert_lane(&e, 1, 2, MOD_FILTER));
    CHECK(!rack_has_signal(&e, 1));                               // lane C has no source of its own
}

TEST(one_sum_per_lane_and_heavy_fx_only_after_it) {
    rack_t r;
    rack_clear(&r);
    CHECK(rack_insert_lane(&r, 0, 0, MOD_OSC));
    CHECK(rack_insert_lane(&r, 1, 0, MOD_PHASER));               // a cheap FX: per voice, anywhere
    CHECK(!rack_insert_lane(&r, 2, 0, MOD_REVERB));              // no Sum yet
    CHECK(rack_insert_lane(&r, 2, 0, MOD_SUM));
    CHECK(!rack_insert_lane(&r, 3, 0, MOD_SUM));                 // one per lane
    CHECK(!rack_insert_lane(&r, 2, 0, MOD_REVERB));              // before the Sum
    CHECK(rack_insert_lane(&r, 3, 0, MOD_REVERB));
    CHECK(rack_insert_lane(&r, 4, 0, MOD_TREM));                 // a cheap FX after the Sum runs once
    CHECK_EQ(rack_lane_sum(&r, 0), 2);
    CHECK(!rack_slot_is_global(&r, 1));
    CHECK(!rack_slot_is_global(&r, 2));                           // the Sum itself is the boundary
    CHECK(rack_slot_is_global(&r, 3));
    CHECK(rack_slot_is_global(&r, 4));
    CHECK(!rack_insert_lane(&r, 5, 1, MOD_DELAY));               // lane B has no Sum
    CHECK(rack_insert_lane(&r, 5, 1, MOD_SUM));
    CHECK(rack_insert_lane(&r, 6, 1, MOD_DELAY));
    CHECK(rack_type_global_only(MOD_SPECTRAL) && !rack_type_global_only(MOD_COMP) && rack_is_fx(MOD_COMP) && !rack_is_fx(MOD_SUM));
}

TEST(a_sum_holds_the_lane_level_and_pan) {
    rack_t r;
    rack_clear(&r);
    CHECK(rack_insert_lane(&r, 0, 1, MOD_OSC));
    CHECK_NEAR(*rack_lane_level(&r, 1), 1.0, 1e-6);              // the implicit Sum: full level, centre
    CHECK_NEAR(*rack_lane_pan(&r, 1), 0.0, 1e-6);
    *rack_lane_level(&r, 1) = 0.5f;
    *rack_lane_pan(&r, 1) = -40.0f;
    CHECK(rack_insert_lane(&r, 1, 1, MOD_SUM));                  // takes over the implicit values
    CHECK_NEAR(r.slot[1].v[MP_SU_LEVEL], 0.5, 1e-6);
    CHECK_NEAR(r.slot[1].v[MP_SU_PAN], -40.0, 1e-6);
    r.slot[1].v[MP_SU_LEVEL] = 0.25f;
    CHECK_NEAR(*rack_lane_level(&r, 1), 0.25, 1e-6);
    CHECK(rack_delete(&r, 1));                                    // and gives them back
    CHECK_NEAR(*rack_lane_level(&r, 1), 0.25, 1e-6);
}

TEST(a_sum_needed_by_heavy_fx_cannot_go) {
    rack_t r;
    rack_clear(&r);
    CHECK(rack_insert_lane(&r, 0, 0, MOD_OSC));
    CHECK(rack_insert_lane(&r, 1, 0, MOD_SUM));
    CHECK(rack_insert_lane(&r, 2, 0, MOD_DELAY));
    CHECK(!rack_delete(&r, 1));                                   // the delay needs it
    CHECK(!rack_set_lane(&r, 1, 2));                              // nor can it move away
    CHECK(!rack_set_lane(&r, 2, 1));                              // the delay cannot move to a lane without a Sum
    CHECK(rack_delete(&r, 2));
    CHECK(rack_set_lane(&r, 1, 2));                               // now the Sum may move
    CHECK_EQ(rack_lane_sum(&r, 2), 1);
    CHECK_EQ(rack_lane_sum(&r, 0), RACK_NONE);
}

TEST(sixteen_slots_shared_by_the_lanes) {
    rack_t r;
    rack_clear(&r);
    for (int i = 0; i < RACK_MAX; i++) CHECK(rack_insert_lane(&r, i, i % RACK_LANES, MOD_OSC));
    CHECK_EQ(r.count, 16);
    CHECK(!rack_insert_lane(&r, 16, 0, MOD_FILTER));
    CHECK_EQ(count_lane(&r, 0) + count_lane(&r, 1) + count_lane(&r, 2), 16);
}

TEST(fx_modules_have_pages_and_spectral_a_cog) {
    rack_t r;
    rack_clear(&r);
    CHECK(rack_insert_lane(&r, 0, 0, MOD_OSC));
    CHECK(rack_insert_lane(&r, 1, 0, MOD_SUM));
    CHECK(rack_insert_lane(&r, 2, 0, MOD_SPECTRAL));
    CHECK_EQ(rack_mparam_count(MOD_SPECTRAL), 7);
    CHECK_EQ(module_hidden_count(MOD_SPECTRAL), 3);
    char v[16];
    rack_mparam_format(&r.slot[2], MP_SP_HI, v, sizeof v);
    CHECK_STR(v, "20.0kHz");
    for (int t = MOD_SUM; t < MOD_TYPE_COUNT; t++) {             // every new type: a code, a name, parameters, a modulation target
        CHECK(rack_type_code((module_type_t)t)[0] != 0);
        CHECK(rack_mparam_count((module_type_t)t) >= 2);
        CHECK(rack_param_count((module_type_t)t) >= 1);
        CHECK(rack_is_audio((module_type_t)t));
    }
}
