// A module's first page: the cog (the row after the last one) opens the module's hidden settings in place of its rows (ui_pages.c mod_hidden,
// ui_input.c, ui_draw.c draw_module_head).
#include "ui_test.h"

TEST(cog_opens_and_closes_the_hidden_settings) {
    synth_ui_t *ui = &ui_app.ui;
    const int p = ui_find_page(0, 0);                          // the startup rack: slot 0 is an oscillator
    CHECK(p >= 0);
    ui->page = p; ui->row = 0;
    page_t pg;
    get_page(ui, &ui_app.rack, p, &pg);
    CHECK(page_has_cog(&ui_app.rack, &pg));
    CHECK_EQ(pg.count, 4);
    ui->row = pg.count + 1;                                    // the cog
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui->cog_page, p);
    get_page(ui, &ui_app.rack, p, &pg);
    CHECK_EQ(pg.count, 4);
    CHECK_EQ(pg.params[0], MP_OC_QUAL);
    CHECK_EQ(pg.params[1], MP_OC_MUTE);
    CHECK_EQ(pg.params[2], MP_OC_HARM);         // the MI models' third control
    CHECK_EQ(pg.params[3], PRM_RESET);          // then Reset (ADR-046 phase 5)
    CHECK_EQ(ui->row, 5);                                      // the focus stays on the cog
    char title[32];
    ui_page_title(title, sizeof title);
    CHECK_STR(title, "OSC 1 SET");
    ev_tap(CTL_BTN_2);                                         // Back closes
    CHECK_EQ(ui->cog_page, -1);
    CHECK_EQ(ui->row, 5);
    ev_tap(CTL_JOY_SW);                                        // a push opens, a second push closes
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui->cog_page, -1);
}

TEST(cog_settings_left_their_pages) {
    const int tune = ui_find_page(0, 1), dest = ui_find_page(0, 2);
    page_t pg;
    get_page(&ui_app.ui, &ui_app.rack, tune, &pg);
    CHECK_EQ(pg.count, 2);                                     // Crs, Fine (Q is behind the cog)
    CHECK(!page_has_cog(&ui_app.rack, &pg));                   // only the first page has a cog
    if (dest >= 0) {
        get_page(&ui_app.ui, &ui_app.rack, dest, &pg);
        for (int i = 0; i < pg.count; i++) CHECK(pg.params[i] != MP_OC_MUTE);
    }
}

TEST(cog_closes_when_the_page_changes) {
    synth_ui_t *ui = &ui_app.ui;
    ui->page = ui_find_page(0, 0);
    ui->row = 5;
    ev_tap(CTL_JOY_SW);
    CHECK(ui->cog_page >= 0);
    ui->row = 0;
    ev_tap(CTL_JOY_RIGHT);                                     // next page
    CHECK_EQ(ui->cog_page, -1);
}

/* ---------------- defaults (ADR-046 phase 5): the cog's Reset on every module, the action Reset on one value ---------------- */

static int slot_of(module_type_t t) { for (int i = 0; i < ui_app.rack.count; i++) if (ui_app.rack.slot[i].type == t) return i; return -1; }

TEST(cog_reset_asks_then_puts_every_value_of_the_module_back) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    const int fl = slot_of(MOD_FILTER);
    CHECK(fl >= 0);
    rack_slot_t *s = &ui_app.rack.slot[fl];
    rack_t fresh;                                              // what a new filter starts with
    rack_init_fx(&fresh);
    CHECK(rack_insert(&fresh, 0, MOD_FILTER));
    const float cut0 = fresh.slot[0].v[MP_FL_CUT], res0 = fresh.slot[0].v[MP_FL_RES];
    s->v[MP_FL_CUT] = cut0 * 0.25f; s->v[MP_FL_RES] = res0 + 1.0f;
    ui->page = ui_find_page(fl, 0);
    page_t pg;
    get_page(ui, &ui_app.rack, ui->page, &pg);
    CHECK(page_has_cog(&ui_app.rack, &pg));                    // a filter has a cog now: Reset only behind it
    ui->row = pg.count + 1;
    ev_tap(CTL_JOY_SW);
    get_page(ui, &ui_app.rack, ui->page, &pg);
    CHECK(pg.count == 1 && pg.params[0] == PRM_RESET);
    ui->row = 1;
    ev_tap(CTL_JOY_SW);                                        // Reset: asked, No focused
    CHECK(popup_modal(&ui_app.popup));
    ev_tap(CTL_JOY_SW);
    CHECK(s->v[MP_FL_CUT] == cut0 * 0.25f);                    // No: nothing
    ev_tap(CTL_JOY_SW);
    ev_tap(CTL_JOY_LEFT);                                      // Yes
    ev_tap(CTL_JOY_SW);
    CHECK(s->v[MP_FL_CUT] == cut0 && s->v[MP_FL_RES] == res0);
    CHECK(!popup_modal(&ui_app.popup));
    // no cog where the first page is drawn by hand
    const int eg = slot_of(MOD_EG);
    if (eg >= 0) { get_page(ui, &ui_app.rack, ui_find_page(eg, 0), &pg); CHECK(!page_has_cog(&ui_app.rack, &pg)); }
}

TEST(cog_reset_keeps_the_samplers_file) {
    rack_t r;
    rack_init_fx(&r);
    r.fx = false;
    CHECK(rack_insert(&r, 0, MOD_SAMPLER));
    r.slot[0].v[MP_SM_FILE] = 3; r.slot[0].v[MP_SM_LEVEL] = 0.1f;
    CHECK(rack_slot_reset(&r, 0));
    CHECK(r.slot[0].v[MP_SM_FILE] == 3 && r.slot[0].v[MP_SM_LEVEL] != 0.1f);
}

TEST(value_reset_on_shift_push_while_latched_and_as_a_mappable_action) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    const int fl = slot_of(MOD_FILTER);
    rack_slot_t *s = &ui_app.rack.slot[fl];
    rack_t fresh;
    rack_init_fx(&fresh);
    CHECK(rack_insert(&fresh, 0, MOD_FILTER));
    ui->page = ui_find_page(fl, 0);
    page_t pg;
    get_page(ui, &ui_app.rack, ui->page, &pg);
    int row = 0;
    for (int i = 0; i < pg.count; i++) if (pg.params[i] == MP_FL_CUT) row = i + 1;
    CHECK(row > 0);
    s->v[MP_FL_CUT] = fresh.slot[0].v[MP_FL_CUT] * 0.5f;
    ui->row = row;
    ev_press(CTL_BTN_3); ev_tap(CTL_JOY_SW); ev_release(CTL_BTN_3);    // not latched: Shift + push is XY mode, as before
    CHECK(ui->joy_xy && s->v[MP_FL_CUT] != fresh.slot[0].v[MP_FL_CUT]);
    ev_press(CTL_BTN_3); ev_tap(CTL_JOY_SW); ev_release(CTL_BTN_3);
    CHECK(!ui->joy_xy);
    ev_tap(CTL_JOY_SW);                                                  // latched: the value's default
    CHECK(ui->latched);
    ev_press(CTL_BTN_3); ev_tap(CTL_JOY_SW); ev_release(CTL_BTN_3);
    CHECK(s->v[MP_FL_CUT] == fresh.slot[0].v[MP_FL_CUT] && ui->latched && !ui->joy_xy);
    ev_tap(CTL_JOY_SW);
    // the action Reset on a layer entry (Shift + a note key here): the focused value, latched or not; a global page too
    const control_id_t k = (control_id_t)CTL_KEY(4, 7);
    modifiers_set(MODL_SHIFT, k, (mod_entry_t){ME_ACTION, ACT_VALUE_RESET, 0, {MACRO_NONE, 0, 0}});
    s->v[MP_FL_CUT] *= 0.5f;
    ev_press(CTL_BTN_3); ev_tap(k); ev_release(CTL_BTN_3);
    CHECK(s->v[MP_FL_CUT] == fresh.slot[0].v[MP_FL_CUT]);
    synth_params_t d;
    synth_params_default(&d);
    ui_app.params.amp_env.attack_ms = d.amp_env.attack_ms + 100;
    ui->page = ui_find_page(GLOBAL_PAGE, 0);                     // (GP_AMP_ENV)
    ui->row = 1;                                                         // AMP ENV's Att
    ev_press(CTL_BTN_3); ev_tap(k); ev_release(CTL_BTN_3);
    CHECK(ui_app.params.amp_env.attack_ms == d.amp_env.attack_ms);
    modifiers_reset();
}
