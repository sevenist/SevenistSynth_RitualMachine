// Fine steps (core/fine_step.h, ACT_VALUE_FINE: Shift + joystick left / right) and continuous knobs (rack_mparam_set_norm, param_set_norm,
// the exact drivers of ui_input.c).
#include "ui_test.h"
#include "core/fine_step.h"
#include <math.h>

#define SHIFT CTL_BTN_3

static bool near(float a, float b) { return fabsf(a - b) < 0.0005f; }

TEST(steps_shift_joystick_left_right_is_fine) {
    ui_fresh();
    CHECK(modifiers_get(MODL_SHIFT, CTL_JOY_RIGHT).act == ACT_VALUE_FINE && modifiers_get(MODL_SHIFT, CTL_JOY_RIGHT).arg == 1);
    ui_app.ui.page = 0; ui_app.ui.row = 2;
    macro_t m;
    CHECK(synth_ui_target_at_cursor(&ui_app.ui, &ui_app.rack, &m) && m.kind == MACRO_MODULE && m.prm == MP_OC_PW);
    float *pw = &ui_app.rack.slot[0].v[MP_OC_PW];
    const float v0 = *pw;
    ev_press(SHIFT); ev_tap(CTL_JOY_RIGHT); ev_release(SHIFT);
    CHECK(near(*pw, v0 + 0.01f));                                        // a fifth of the 0.05 step
    ev_press(SHIFT); ev_tap(CTL_JOY_LEFT); ev_tap(CTL_JOY_LEFT); ev_release(SHIFT);
    CHECK(near(*pw, v0 - 0.01f));
    ev_turn(CTL_ENC_B, 1);                                               // a normal step is still 0.05
    CHECK(near(*pw, v0 + 0.04f));
    CHECK(!g_fine_step);
}

TEST(steps_fine_log_and_int_parameters) {
    ui_fresh();
    rack_slot_t s = ui_app.rack.slot[0];
    s.v[MP_OC_COARSE] = 0;
    g_fine_step = true;
    rack_mparam_adjust(&s, MP_OC_COARSE, 1);
    g_fine_step = false;
    CHECK(near(s.v[MP_OC_COARSE], 1.0f));                                // whole semitones: a fine step is never finer than what is shown
    s.v[MP_OC_PW] = 0.5f;
    g_fine_step = true;
    rack_mparam_adjust(&s, MP_OC_PW, 1);
    g_fine_step = false;
    CHECK(near(s.v[MP_OC_PW], 0.51f));
    synth_params_t p = ui_app.params;
    p.amp_env.a_curve = 0;
    g_fine_step = true;
    param_adjust(&p, P_AMP_ACV, 1);                                      // 5 % steps: 1 %
    g_fine_step = false;
    CHECK(near(p.amp_env.a_curve, 1.0f));
    p.cutoff_hz = 1000;
    g_fine_step = true;
    param_adjust(&p, P_CUTOFF, 1);
    g_fine_step = false;
    CHECK(near(p.cutoff_hz, 1000 * powf(1.12f, 0.2f)));                  // a log factor: its 5th root
}

TEST(steps_knobs_are_continuous) {
    ui_fresh();
    ui_app.rack.cfg.knob_mode = 1;                                       // Direct
    float *pw = &ui_app.rack.slot[0].v[MP_OC_PW];
    ui_app.ui.page = 0;
    ev_knob(CTL_COL_KNOB_1, 300);                                        // row 2 = PW 0.05..0.95
    const float a = *pw;
    ev_knob(CTL_COL_KNOB_1, 312);
    const float b = *pw;
    CHECK(b > a && b - a < 0.049f);                                      // finer than a 0.05 step
    CHECK(near(a, roundf((0.05f + 300.0f / INPUT_VALUE_MAX * 0.9f) * 100.0f) / 100.0f));   // rounded to the 2 decimals PW shows
    rack_slot_t s = ui_app.rack.slot[0];
    rack_mparam_set_norm(&s, MP_OC_COARSE, 0.51f);                       // -24..24 st: whole semitones
    CHECK(s.v[MP_OC_COARSE] == roundf(s.v[MP_OC_COARSE]));
    CHECK(!rack_mparam_is_continuous(MOD_OSC, MP_OC_WAVE));              // an enumeration is still walked
    synth_params_t p = ui_app.params;
    param_set_norm(&p, P_CUTOFF, 0.5f);                                  // log: the middle of 20..18000 Hz is their geometric mean
    CHECK(fabsf(p.cutoff_hz - sqrtf(20.0f * 18000.0f)) < 1.0f);
    CHECK(fabsf(param_norm(&p, P_CUTOFF) - 0.5f) < 0.001f);
}
