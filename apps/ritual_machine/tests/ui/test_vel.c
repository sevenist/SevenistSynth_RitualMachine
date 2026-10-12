// The Velocity module (MOD_VEL, "VL"; user, 2026-10-11: velocity as a source in the rack): a modulator like an LFO or an envelope,
// with a DEST page (Tgt / Prm / Dpth), saved with the sound.
#include "ui_test.h"
#include "core/patch.h"
#include "hal/hal_storage.h"

static char txt[STORAGE_FILE_MAX];

TEST(velocity_module_is_a_modulator_with_its_page_and_goes_with_the_sound) {
    ui_fresh();
    ui_set_type(SYNTH_MODULAR);
    rack_t *r = &ui_app.rack;
    CHECK_STR(rack_type_code(MOD_VEL), "VL");
    CHECK(rack_is_modulator(MOD_VEL) && rack_can_target(MOD_VEL) && !rack_is_audio(MOD_VEL) && !rack_is_fx(MOD_VEL));
    CHECK_EQ(rack_depth_index(MOD_VEL), MP_VL_DEPTH);
    CHECK(rack_can_insert(r, 0, 0, MOD_VEL) && rack_can_insert(r, r->count, ROW_M, MOD_VEL));   // like the other modulators: any row
    int osc = -1;
    for (int i = 0; i < r->count && osc < 0; i++) if (r->slot[i].type == MOD_OSC) osc = i;
    CHECK(osc >= 0);
    if (osc < 0) return;
    const int osc_id = r->slot[osc].id;
    CHECK(rack_insert_row(r, osc + 1, r->slot[osc].row, MOD_VEL));
    const int vl = osc + 1;
    CHECK_EQ(r->slot[vl].type, MOD_VEL);
    CHECK(rack_slot_is_mod(r, vl));
    synth_ui_rebuild_pages(&ui_app.ui, r);
    // its page: VEL 1 with Tgt, Prm, Dpth
    char t[24];
    bool found = false;
    for (int p = 0; p < ui_app.ui.page_count && !found; p++) {
        ui_app.ui.page = p;
        ui_page_title(t, sizeof t);
        found = !strcmp(t, "VEL 1");
    }
    CHECK(found);
    page_t pg;
    get_page(&ui_app.ui, r, ui_app.ui.page, &pg);
    CHECK_EQ(pg.count, 3);
    CHECK_EQ(pg.params[0], PRM_TGT); CHECK_EQ(pg.params[1], PRM_TPRM); CHECK_EQ(pg.params[2], MP_VL_DEPTH);
    ui_app.ui.row = 1;
    for (int i = 0; i < 40 && r->slot[vl].tgt_id != osc_id; i++) ev_turn(CTL_ENC_B, 1);     // Tgt: around to the oscillator
    CHECK_EQ(r->slot[vl].tgt_id, osc_id);
    ui_app.ui.row = 2;
    for (int i = 0; i < 8 && r->slot[vl].tgt_param != 1; i++) ev_turn(CTL_ENC_B, 1);        // Prm: its level
    CHECK_EQ(r->slot[vl].tgt_param, 1);
    ui_app.ui.row = 3;
    const float d0 = r->slot[vl].v[MP_VL_DEPTH];
    ev_turn(CTL_ENC_B, -2);
    CHECK(r->slot[vl].v[MP_VL_DEPTH] < d0);                              // Dpth moves, in the target's unit
    ev_idle(2);                                                          // (drawn: the box names it)
    // it is in the sound's text, and comes back
    const float depth = r->slot[vl].v[MP_VL_DEPTH];
    CHECK(patch_to_text(r, &ui_app.params, &ui_app.ui, false, NULL, false, txt, sizeof txt) > 0);
    CHECK(strstr(txt, " VL ") != NULL);
    rack_delete(r, vl);
    CHECK(patch_from_text(r, &ui_app.params, &ui_app.ui, NULL, NULL, txt));
    int back = -1;
    for (int i = 0; i < r->count; i++) if (r->slot[i].type == MOD_VEL) back = i;
    CHECK(back >= 0);
    if (back < 0) return;
    CHECK_EQ(r->slot[back].tgt_id, osc_id); CHECK_EQ(r->slot[back].tgt_param, 1);
    CHECK(r->slot[back].v[MP_VL_DEPTH] > depth - 0.001f && r->slot[back].v[MP_VL_DEPTH] < depth + 0.001f);
}
