// Push = Enter on the synth's pages (apps/ritual_machine/docs/DESIGN.md ADR-046 phase 2, core/ui_input.c page_enter): a push latches a value (never "right"),
// up / down then step it by four, a push or Back releases it; a two-state row flips; encoder A's move releases.
#include "ui_test.h"

static float pw(void) { return ui_app.rack.slot[0].v[MP_OC_PW]; }      // OSC 1's row 2 (Timb / PW) on the startup rack

TEST(enter_latches_a_page_value_without_changing_it) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    ui->page = ui_find_page(0, 0); ui->row = 2;
    const float v0 = pw();
    ev_tap(CTL_JOY_SW);
    CHECK(ui->latched);
    CHECK(pw() == v0);                                                  // (the push used to step the value up)
    ev_tap(CTL_JOY_SW);
    CHECK(!ui->latched);
    CHECK(pw() == v0);
}

TEST(enter_latched_up_down_step_by_four) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    ui->page = ui_find_page(0, 0); ui->row = 2;
    const float v0 = pw();
    ev_tap(CTL_JOY_RIGHT);                                              // one step (no latch needed, as before)
    const float one = pw() - v0;
    CHECK(one > 0.0f);
    ev_tap(CTL_JOY_SW);
    ev_tap(CTL_JOY_UP);                                                 // latched: up = four steps, the focus stays
    CHECK_EQ(ui->row, 2);
    CHECK(pw() - v0 > 4.5f * one && pw() - v0 < 5.5f * one);
    ev_tap(CTL_JOY_DOWN);
    CHECK(pw() - v0 > 0.5f * one && pw() - v0 < 1.5f * one);
    ev_tap(CTL_BTN_2);                                                  // Back releases, nothing else
    CHECK(!ui->latched);
    CHECK_EQ(ui->row, 2);
    ev_tap(CTL_JOY_DOWN);                                               // released: down moves the focus again
    CHECK_EQ(ui->row, 3);
}

TEST(enter_released_by_another_row_or_page) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    ui->page = ui_find_page(0, 0); ui->row = 2;
    ev_tap(CTL_JOY_SW);
    CHECK(ui->latched);
    ev_turn(CTL_ENC_A, 1);                                              // encoder A: the next row
    CHECK(!ui->latched);
    CHECK_EQ(ui->row, 3);
    ui->latched = false;
    ui->row = 0;
    ev_tap(CTL_JOY_SW);                                                 // the header: nothing
    CHECK_EQ(ui->row, 0);
}

TEST(enter_short_push_latches_held_push_binds_the_joystick) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    ui->page = ui_find_page(0, 0); ui->row = 2;
    ev_press(CTL_JOY_SW);
    CHECK(!ui->latched);                                                // a parameter row: it waits for the release
    ev_release(CTL_JOY_SW);
    CHECK(ui->latched);
    CHECK_EQ(ui->joy[0].t.kind, MACRO_NONE);                            // not bound
    ev_tap(CTL_JOY_SW);                                                 // released
    ev_press(CTL_JOY_SW);
    ev_idle(60);                                                        // held: bound to X, nothing latched
    ev_release(CTL_JOY_SW);
    CHECK(!ui->latched);
    CHECK(ui->joy[0].t.kind != MACRO_NONE);
    CHECK_STR(ui_app.popup.info.title, "JOY");
}

TEST(enter_flips_a_two_state_row) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    ui->page = ui_find_page(GLOBAL_PAGE, 9);                            // PATTERNS (GP_SEQ_PAT)
    CHECK(ui->page >= 0);
    CHECK(ui_app.ui.bank != NULL);
    ui->row = PAT_ROW_CHAIN;
    const int was = ui_app.ui.bank->chain_on;
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui_app.ui.bank->chain_on, !was);
    CHECK(!ui->latched);
}
