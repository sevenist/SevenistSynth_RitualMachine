// Safety (ENGINE_DESIGN.md ADR-046 phase 4): Back goes up one level and never destroys; pending rack edits are marked and said when
// applied; the header shows a recording armed / running.
#include "ui_test.h"
#include "app/app_internal.h"

#define MENU CTL_BTN_1
#define BACK CTL_BTN_2

TEST(safety_back_on_rack_never_deletes_and_goes_up) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    ev_tap(MENU);
    ui->menu_tab = tab_index_of(&ui_app.rack, TAB_RACK);
    ui->row = 1;                                                        // a cell of branch 1: its first module
    const int count = ui_app.rack.count;
    ev_tap(BACK);
    CHECK_EQ(ui_app.rack.count, count);                                 // (it used to delete the module)
    CHECK(!ui->rack_dirty);
    CHECK_EQ(ui->row, 0);                                               // up: the header
    CHECK(ui->in_rack);
    ev_tap(BACK);                                                       // up again: out of the menu
    CHECK(!ui->in_rack);
    CHECK_EQ(ui_app.rack.count, count);
}

TEST(safety_back_closes_the_cell_menu_first) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    ev_tap(MENU);
    ui->menu_tab = tab_index_of(&ui_app.rack, TAB_RACK);
    ui->row = 1;
    ev_tap(CTL_JOY_SW);                                                 // the cell's menu
    CHECK(ui->rack_menu);
    ev_tap(BACK);
    CHECK(!ui->rack_menu);
    CHECK(ui->in_rack);
    CHECK(ui->row >= 1);                                                // the focus back on the cell
}

TEST(safety_back_in_a_list_tab_goes_to_the_header_then_out) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    ev_tap(MENU);
    ui->menu_tab = tab_index_of(&ui_app.rack, TAB_MACROS);
    ui->row = 3;
    ev_tap(BACK);
    CHECK_EQ(ui->row, 0);
    CHECK(ui->in_rack);
    ev_tap(BACK);
    CHECK(!ui->in_rack);
}

TEST(safety_pending_rack_edits_are_said_when_applied) {
    ui_fresh();
    synth_ui_t *ui = &ui_app.ui;
    ev_tap(MENU);
    ui->menu_tab = tab_index_of(&ui_app.rack, TAB_RACK);
    CHECK(rack_delete(&ui_app.rack, ui_app.rack.count - 1));            // an edit waits (the header reads "< RACK* >")
    ui->rack_dirty = true;
    ui->row = 0;
    ev_tap(BACK);                                                       // leaving the menu applies it
    ev_idle(1);
    CHECK(!ui->in_rack);
    CHECK(!ui->rack_dirty);
    CHECK_STR(ui_app.popup.info.title, "RACK");
    CHECK_STR(ui_app.popup.info.text, "Applied");
}

TEST(safety_header_shows_the_recording) {
    ui_fresh();
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.rec_mark, REC_MARK_NONE);
    ui_app.ui.seq_rec = true;                                           // Rec on, stopped: armed
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.rec_mark, REC_MARK_ARMED);
    ui_app.seq.running = true;                                          // running: recording
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.rec_mark, REC_MARK_ON);
    ui_app.ui.seq_erase = true;
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.rec_mark, REC_MARK_ERASE);
    ui_app.ui.seq_erase = false; ui_app.ui.seq_rec = false; ui_app.seq.running = false;
    app_set_section(&ui_app, true);
    ui_app.ui.cap_state = CAPTURE_ST_ARMED;                             // Pads: the capture waits for a sound
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.rec_mark, REC_MARK_ARMED);
    ui_app.ui.cap_state = CAPTURE_ST_RECORDING;
    app_draw_frame(&ui_app);
    CHECK_EQ(ui_app.ui.rec_mark, REC_MARK_ON);
    ui_app.ui.cap_state = CAPTURE_ST_IDLE;
}
