// Rack rows (ADR-041): two branches and row M, the Para switch, the MIX; the model's rules (rack.c) and the RACK tab (scr_rack.c).
#include "ui_test.h"
#include <math.h>

#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) < (eps))

static int count_row(const rack_t *r, int row) { int n = 0; for (int i = 0; i < r->count; i++) n += r->slot[i].row == row; return n; }
static int find_type(const rack_t *r, int type) { for (int i = 0; i < r->count; i++) if (r->slot[i].type == type) return i; return RACK_NONE; }

TEST(branches_keep_their_own_chains) {
    rack_t r;
    rack_clear(&r);
    CHECK(rack_insert_row(&r, 0, 0, MOD_OSC));                   // branch 1: OC FL
    CHECK(rack_insert_row(&r, 1, 1, MOD_OSC));                   // branch 2: OC SA (interleaved in the slot order)
    CHECK(rack_insert_row(&r, 2, 0, MOD_FILTER));
    CHECK(rack_insert_row(&r, 3, 1, MOD_SAT));
    CHECK_EQ(count_row(&r, 0), 2);
    CHECK_EQ(rack_audio_next(&r, 0), 2);                          // the filter, skipping branch 2's oscillator
    CHECK_EQ(rack_audio_next(&r, 1), 3);
    CHECK_EQ(rack_audio_next(&r, 2), RACK_OUT);
    CHECK_EQ(rack_audio_prev(&r, 3), 1);
    CHECK(rack_has_signal(&r, 3));
    rack_t e;
    rack_clear(&e);
    CHECK(rack_insert_row(&e, 0, 1, MOD_FILTER));
    CHECK(!rack_has_signal(&e, 0));                               // branch 2 has no source of its own
}

TEST(row_m_takes_no_sources_and_hears_the_branches) {
    rack_t r;
    rack_clear(&r);
    CHECK(!rack_insert_row(&r, 0, ROW_M, MOD_OSC));
    CHECK(!rack_insert_row(&r, 0, ROW_M, MOD_SAMPLER));
    CHECK(!rack_insert_row(&r, 0, ROW_M, MOD_COMB));             // follows the key: per voice only
    CHECK(rack_insert_row(&r, 0, ROW_M, MOD_FILTER));
    CHECK(!rack_has_signal(&r, 0));
    CHECK(rack_insert_row(&r, 0, 1, MOD_OSC));                    // a source in branch 2 feeds the MIX
    CHECK(rack_has_signal(&r, 1));
    CHECK(rack_slot_is_shared(&r, 1));                            // row M runs once
    CHECK(!rack_slot_is_shared(&r, 0));
    CHECK(!rack_set_row(&r, 0, ROW_M));                           // the oscillator cannot move there
    CHECK_EQ(rack_add_m(&r, MOD_REVERB), 2);
    CHECK_EQ(r.slot[2].row, ROW_M);
}

TEST(para_makes_the_rest_of_the_branch_shared) {
    rack_t r;
    rack_clear(&r);
    CHECK(rack_insert_row(&r, 0, 0, MOD_OSC));
    CHECK(rack_insert_row(&r, 1, 0, MOD_FILTER));
    CHECK(rack_insert_row(&r, 2, 0, MOD_SAT));
    CHECK(rack_insert_row(&r, 3, 1, MOD_OSC));
    CHECK(rack_insert_row(&r, 4, 1, MOD_FILTER));
    CHECK(rack_insert_row(&r, 5, 0, MOD_LFO));
    r.slot[5].tgt_id = r.slot[2].id;                              // LFO -> the saturator
    CHECK(!rack_can_para(&r, 0));                                 // a source never
    CHECK(rack_can_para(&r, 1));
    CHECK_EQ(rack_shared_start(&r, 0), RACK_NONE);
    CHECK(!rack_slot_is_shared(&r, 2));
    CHECK(!rack_slot_is_shared(&r, 5));                           // a modulator runs where its target runs
    CHECK(rack_set_para(&r, 1, true));
    CHECK_EQ(rack_shared_start(&r, 0), 1);
    CHECK(!rack_slot_is_shared(&r, 0));
    CHECK(rack_slot_is_shared(&r, 1));
    CHECK(rack_slot_is_shared(&r, 2));
    CHECK(rack_slot_is_shared(&r, 5));
    CHECK(!rack_slot_is_shared(&r, 4));                           // branch 2 keeps its own (per voice)
    CHECK_EQ(rack_shared_start(&r, 1), RACK_NONE);
    CHECK_EQ(r.slot[1].penv, PARA_ENV_LEGATO);
}

TEST(heavy_fx_start_the_shared_part) {
    rack_t r;
    rack_clear(&r);
    CHECK(rack_insert_row(&r, 0, 0, MOD_OSC));
    CHECK(rack_insert_row(&r, 1, 0, MOD_PHASER));                // a cheap FX: per voice
    CHECK(!rack_slot_is_shared(&r, 1));
    CHECK(rack_insert_row(&r, 2, 0, MOD_REVERB));                // a heavy one: shared from here on
    CHECK_EQ(rack_shared_start(&r, 0), 2);
    CHECK(!rack_can_para(&r, 2));                                 // always shared: no switch
    CHECK(!rack_insert_row(&r, 3, 0, MOD_OSC));                   // one way: no source after it
    CHECK(rack_insert_row(&r, 0, 0, MOD_OSC));                    // before it is fine
    CHECK(!rack_insert_row(&r, 0, 0, MOD_DELAY));                  // nor a heavy FX before a source
    CHECK(rack_type_shared_only(MOD_SPECTRAL) && !rack_type_shared_only(MOD_COMP) && rack_is_fx(MOD_COMP) && !rack_is_fx(MOD_FILTER));
}

TEST(one_way_rule_refuses_para_before_a_source) {
    rack_t r;
    rack_clear(&r);
    CHECK(rack_insert_row(&r, 0, 0, MOD_OSC));
    CHECK(rack_insert_row(&r, 1, 0, MOD_FILTER));
    CHECK(rack_insert_row(&r, 2, 0, MOD_OSC));                    // OC FL OC
    CHECK(!rack_set_para(&r, 1, true));                           // the second oscillator would follow the shared filter
    CHECK_EQ(r.slot[1].para, 0);
    CHECK(rack_set_row(&r, 2, 1));                                // move it to branch 2
    CHECK(rack_set_para(&r, 1, true));
    CHECK(!rack_set_row(&r, 2, 0));                               // and it cannot come back after the Para point
    CHECK(!rack_insert_row(&r, 3, 0, MOD_SAMPLER));
    CHECK(rack_set_para(&r, 1, false));
    CHECK(rack_set_row(&r, 2, 0));
}

TEST(sixteen_slots_shared_by_the_rows) {
    rack_t r;
    rack_clear(&r);
    for (int i = 0; i < RACK_MAX; i++) CHECK(rack_insert_row(&r, i, i % RACK_ROWS, i % RACK_ROWS == ROW_M ? MOD_EQ : MOD_OSC));
    CHECK_EQ(r.count, 16);
    CHECK(!rack_insert_row(&r, 16, 0, MOD_FILTER));
    CHECK_EQ(count_row(&r, 0) + count_row(&r, 1) + count_row(&r, ROW_M), 16);
}

TEST(fm_and_strings_take_effects_in_row_m_only) {
    for (int type = SYNTH_FM; type <= SYNTH_STRINGS; type++) {
        rack_t r;
        rack_clear(&r);
        r.cfg.type = (uint8_t)type;
        CHECK(!rack_insert_row(&r, 0, 0, MOD_FILTER));
        CHECK(!rack_insert_row(&r, 0, ROW_M, MOD_LFO));
        CHECK(rack_insert_row(&r, 0, ROW_M, MOD_REVERB));
        CHECK(rack_insert_row(&r, 1, ROW_M, MOD_FILTER));
        CHECK(!rack_set_row(&r, 1, 0));
        CHECK(rack_has_signal(&r, 0));                            // their voices feed row M
    }
}

TEST(types_are_modular_fm_strings_and_one_voice_is_mono) {
    synth_config_t c;
    synth_config_init(&c);
    CHECK_EQ(c.type, SYNTH_MODULAR);
    CHECK_EQ(c.voices, 1);
    CHECK(synth_config_is_mono(&c));
    CHECK_EQ(synth_config_adjust(&c, CFGP_VOICES, 1), CFG_REBUILD);
    CHECK(!synth_config_is_mono(&c));
    CHECK_EQ(synth_config_adjust(&c, CFGP_TYPE, 1), CFG_REBUILD);
    CHECK_STR(synth_type_name((synth_type_t)c.type), "FM");
    CHECK_EQ(synth_config_adjust(&c, CFGP_TYPE, 1), CFG_REBUILD);
    CHECK_STR(synth_type_name((synth_type_t)c.type), "Strings");
    CHECK(!synth_config_is_mono(&c));                             // Strings is never mono
    CHECK_EQ(synth_config_adjust(&c, CFGP_TYPE, 1), CFG_UNCHANGED);
}

TEST(startup_patch_has_delay_and_reverb_in_row_m) {
    rack_t r;
    rack_init_startup(&r);
    const int dl = find_type(&r, MOD_DELAY), rv = find_type(&r, MOD_REVERB);
    CHECK(dl != RACK_NONE && rv != RACK_NONE);
    CHECK_EQ(r.slot[dl].row, ROW_M);
    CHECK_EQ(r.slot[rv].row, ROW_M);
    CHECK_NEAR(r.slot[dl].v[MP_DL_TIME], 1000, 1e-3);
    CHECK_NEAR(r.slot[rv].v[MP_RV_MIX], 0.4, 1e-6);
    CHECK_EQ(count_row(&r, 0), 5);                                 // four oscillators and the filter
    CHECK_NEAR(r.br_lvl[0], 1.0, 1e-6);
    CHECK_NEAR(r.br_pan[1], 0.0, 1e-6);
}

TEST(fx_modules_have_pages_and_spectral_a_cog) {
    rack_t r;
    rack_clear(&r);
    const int k = rack_add_m(&r, MOD_SPECTRAL);
    CHECK(k != RACK_NONE);
    CHECK_EQ(rack_mparam_count(MOD_SPECTRAL), 7);
    CHECK_EQ(module_hidden_count(MOD_SPECTRAL), 3);
    char v[16];
    rack_mparam_format(&r.slot[k], MP_SP_HI, v, sizeof v);
    CHECK_STR(v, "20.0kHz");
    for (int t = MOD_TREM; t < MOD_TYPE_COUNT; t++) {            // every FX type: a code, a name, parameters, a modulation target
        CHECK(rack_type_code((module_type_t)t)[0] != 0);
        CHECK(rack_mparam_count((module_type_t)t) >= 2);
        CHECK(rack_param_count((module_type_t)t) >= 1);
        CHECK(rack_is_audio((module_type_t)t) && rack_is_processor((module_type_t)t));
    }
}

// The RACK tab: rows 1 / 2 / M; the menu's Para row on a branch filter, the MIX cell's levels; FM shows row M only.
enum { ROW_E_M = 3, MENU_PARA = 10, MENU_PENV = 11, MENU_LVL1 = 12 };     // ui->row of row M and of the menu rows (element index + 1, scr_rack.c)

static int open_rack_tab(void) {
    ev_tap(CTL_BTN_1);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_RACK);
    return ui_app.ui.menu_tab;
}

TEST(rack_tab_para_row_and_mix_cell) {
    ui_fresh();
    rack_init_startup(&ui_app.rack);
    synth_ui_init(&ui_app.ui, &ui_app.rack);
    CHECK(open_rack_tab() >= 0);
    const int fl = find_type(&ui_app.rack, MOD_FILTER);
    ui_app.ui.row = 1;                                            // branch 1, the filter's cell
    ui_app.ui.rack_col[0] = fl;
    ev_tap(CTL_JOY_SW);                                           // push: the menu
    CHECK(ui_app.ui.rack_menu);
    ui_app.ui.row = MENU_PARA;
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.rack.slot[fl].para, 1);
    CHECK_EQ(rack_shared_start(&ui_app.rack, 0), fl);
    ui_app.ui.row = MENU_PENV;                                    // the shared start offers PEnv
    ev_turn(CTL_ENC_B, 1);
    CHECK_EQ(ui_app.rack.slot[fl].penv, PARA_ENV_RETRIG);
    ev_tap(CTL_BTN_2);                                            // Back closes the menu
    CHECK(!ui_app.ui.rack_menu);
    ui_app.ui.row = ROW_E_M;                                      // row M, the MIX cell (column 0)
    ui_app.ui.rack_col[ROW_M] = 0;
    ev_tap(CTL_JOY_SW);
    CHECK(ui_app.ui.rack_menu);
    ui_app.ui.row = MENU_LVL1;
    ev_turn(CTL_ENC_B, -2);
    CHECK_NEAR(ui_app.rack.br_lvl[0], 0.9, 1e-4);
}

TEST(rack_tab_fm_shows_row_m_only) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    CHECK_EQ(tab_kind(&ui_app.rack, 0), TAB_RACK);
    CHECK(tab_index_of(&ui_app.rack, TAB_FX) < 0);
    CHECK(open_rack_tab() >= 0);
    ev_idle(1);
    CHECK_EQ(ui_app.ui.rack_row, ROW_M);
}
