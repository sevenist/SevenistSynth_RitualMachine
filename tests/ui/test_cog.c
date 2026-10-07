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
    CHECK_EQ(pg.count, 2);
    CHECK_EQ(pg.params[0], MP_OC_QUAL);
    CHECK_EQ(pg.params[1], MP_OC_MUTE);
    CHECK_EQ(ui->row, 3);                                      // the focus stays on the cog
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
