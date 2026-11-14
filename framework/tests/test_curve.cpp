// Response curves (control/curve.h): the built-ins, a bank's user curves, their text.
#include <cstring>
#include "control/curve.h"
#include "test.h"

TEST(curve_builtins_bend_and_keep_their_ends) {
    for (int id = 0; id < CRV_BUILTIN; id++) {
        CHECK_NEAR(crv_eval(nullptr, id, 0.0f), 0.0, 1e-6);
        CHECK_NEAR(crv_eval(nullptr, id, 1.0f), 1.0, 1e-6);
    }
    CHECK(crv_eval(nullptr, CRV_EXP, 0.5f) < 0.2f);                    // slow start
    CHECK(crv_eval(nullptr, CRV_LOG, 0.5f) > 0.8f);                    // its mirror
    CHECK_NEAR(crv_eval(nullptr, CRV_S, 0.5f), 0.5, 1e-6);
    CHECK_NEAR(crv_eval(nullptr, CRV_LIN, 1.7f), 1.0, 1e-6);           // clamped
    CHECK_NEAR(crv_eval(nullptr, CRV_USER_FIRST, 0.3f), 0.3, 1e-6);    // no bank: a user curve is linear
    CHECK_EQ(crv_by_name("exp"), CRV_EXP);
    CHECK_EQ(crv_by_name("u2"), CRV_USER_FIRST + 1);
    CHECK_EQ(crv_by_name("nope"), -1);
}

TEST(curve_user_curves_step_hold_and_list) {
    static crv_bank_t b;
    crv_init(&b);
    CHECK_EQ(crv_count(&b), CRV_BUILTIN);
    CHECK_EQ(crv_user_new(&b, 2), 2);                                  // U3: (0,0) -> (100,100)
    CHECK_EQ(crv_user_new(&b, 2), -1);
    CHECK_NEAR(crv_eval(&b, CRV_USER_FIRST + 2, 0.25f), 0.25, 1e-3);
    CHECK_EQ(crv_user_add_point(&b, 2, 0), 1);                         // halfway: (50,50)
    crv_user_set_point(&b, 2, 1, 50, 80);
    crv_user_set_mode(&b, 2, 0, CRV_PT_STEP);                          // held at 0 until x = 50
    CHECK_NEAR(crv_eval(&b, CRV_USER_FIRST + 2, 0.4f), 0.0, 1e-3);
    CHECK_NEAR(crv_eval(&b, CRV_USER_FIRST + 2, 0.75f), 0.9, 1e-2);
    CHECK_EQ(crv_count(&b), CRV_BUILTIN + 1);
    CHECK_EQ(crv_step(&b, CRV_S, 1), CRV_USER_FIRST + 2);              // after the built-ins, then round
    CHECK_EQ(crv_step(&b, CRV_USER_FIRST + 2, 1), CRV_LIN);
}

TEST(curve_text_round_trip) {
    static crv_bank_t a, b;
    crv_init(&a);
    crv_user_new(&a, 0);
    crv_user_add_point(&a, 0, 0);
    crv_user_set_mode(&a, 0, 1, CRV_PT_STEP);
    char t1[1024], t2[1024];
    crv_to_text(&a, "# curves", t1, sizeof t1);
    CHECK(std::strstr(t1, "curve 1\npoint 0 0 lin\npoint 50 50 step\npoint 100 100 lin\n") != nullptr);
    crv_init(&b);
    CHECK(crv_from_text(&b, t1));
    crv_to_text(&b, "# curves", t2, sizeof t2);
    CHECK(std::strcmp(t1, t2) == 0);
    CHECK(!crv_from_text(&b, "no curves here\n"));                    // nothing changed
    CHECK(crv_from_text(&b, "curve 4\n"));                             // a curve with no point: completed
    CHECK(b.user[3].used && b.user[3].n == 2 && !b.user[0].used);
}
