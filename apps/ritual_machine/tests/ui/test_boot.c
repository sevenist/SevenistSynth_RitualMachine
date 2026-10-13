// The boot animation (app_boot_animation): the frames of assets/UI_Sprites/64/boot_NN.png, once, over the UI.
#include "ui_test.h"
#include "ui/gui.h"
#include "core/ui_sprites_gen.h"

TEST(boot_animation_plays_once_then_the_ui_shows) {
    ui_fresh();
    CHECK_EQ(ui_app.boot_anim, GUI_ANIM_INVALID);                        // app_init alone starts none (the platforms ask for it)
    const gui_sprite_t *sp = ui_sprite("64/boot");
    CHECK(sp != NULL && sp->frames >= 2);
    if (!sp) return;
    app_boot_animation(&ui_app);
    CHECK(ui_app.boot_anim != GUI_ANIM_INVALID);
    CHECK_EQ(gui_anim_frame(ui_app.boot_anim), 0);
    ev_idle(8);                                                          // the stub's clock: 10 ms a call, so some frames went by
    CHECK(ui_app.boot_anim != GUI_ANIM_INVALID);
    const int frame = gui_anim_frame(ui_app.boot_anim);
    CHECK(frame > 0 && frame < sp->frames - 1);
    ev_knob(CTL_COL_KNOB_0, 300);                                        // a knob's value (its noise at rest) does not end it
    CHECK(ui_app.boot_anim != GUI_ANIM_INVALID);
    ev_idle(sp->frames * BOOT_FRAME_MS / 10 + 10);                       // every frame, then the UI
    CHECK_EQ(ui_app.boot_anim, GUI_ANIM_INVALID);
    app_boot_animation(&ui_app);                                         // (again: it can be played at any time)
    CHECK(ui_app.boot_anim != GUI_ANIM_INVALID);
    ev_idle(sp->frames * BOOT_FRAME_MS / 10 + 10);
    CHECK_EQ(ui_app.boot_anim, GUI_ANIM_INVALID);
}

// The pixels lit in the rows y0..y1 of the frame the application drew last.
static int lit_rows(int y0, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < DISPLAY_WIDTH; x++) n += u8g2_GetBufferPtr(ui_app.display)[(y / 8) * DISPLAY_WIDTH + x] >> (y & 7) & 1;
    return n;
}

TEST(boot_screen_names_the_machine_and_its_version) {
    ui_fresh();
    app_boot_animation(&ui_app);
    if (ui_app.boot_anim == GUI_ANIM_INVALID || DISPLAY_HEIGHT < 112) return;   // (no image, or no room for the words)
    const gui_sprite_t *sp = ui_sprite("64/boot");
    const int top = (DISPLAY_HEIGHT - sp->h) / 2;                        // the image is centred: the words are above and under it
    CHECK(top >= 24);
    CHECK(lit_rows(10, 22) > 100);                                       // BOOT_TITLE, bold
    CHECK(lit_rows(DISPLAY_HEIGHT - 10, DISPLAY_HEIGHT - 3) > 30);       // BOOT_VERSION
    CHECK_EQ(lit_rows(0, 8), 0);
    ev_idle(sp->frames * BOOT_FRAME_MS / 10 + 10);                       // the animation plays to its end as before
    CHECK_EQ(ui_app.boot_anim, GUI_ANIM_INVALID);
}

extern int ui_stub_boot_ends, ui_stub_request_steps, ui_stub_boot_progress, ui_stub_boot_progress_back;   // tools/ui_stubs.c

// The board's boot animation loops until the application says its start is over (hal_display.h display_boot_end).
TEST(start_is_reported_once_when_the_pads_files_are_in) {
    ui_fresh();                                                          // (the stub's card is there at once, no pad has a file)
    ev_idle(5);
    CHECK(ui_app.started);
    const int ends = ui_stub_boot_ends;
    ev_idle(50);
    CHECK_EQ(ui_stub_boot_ends, ends);                                   // once
    CHECK_EQ(ui_stub_boot_progress, 100);                                // the loading bar is full when the start is over
    ui_app.started = false;                                              // a start with two pads whose files the card is slow to give
    ui_app.start_pct = 0;
    ui_stub_request_steps = 20;
    snprintf(ui_app.pads.pad[1].file, PAD_NAME_MAX, "demo0");            // (in the catalog already)
    snprintf(ui_app.pads.pad[2].file, PAD_NAME_MAX, "kit/snare");
    snprintf(ui_app.pads.pad[3].file, PAD_NAME_MAX, "kit/tom");
    app_pad_changed(&ui_app, 2);
    ev_idle(5);
    CHECK(!ui_app.started);                                              // still being read: the animation goes on
    CHECK_EQ(ui_stub_boot_ends, ends);
    CHECK_EQ(ui_stub_boot_progress, 30 + 70 * 1 / 3);                    // the settings, and one file of three
    ev_idle(25);
    CHECK(!ui_app.started);
    CHECK_EQ(ui_stub_boot_progress, 30 + 70 * 2 / 3);
    ev_idle(40);
    CHECK(ui_app.started);
    CHECK(audio_sample_find("kit/snare") >= 0 && audio_sample_find("kit/tom") >= 0);
    CHECK_EQ(ui_stub_boot_ends, ends + 1);
    CHECK_EQ(ui_stub_boot_progress, 100);
    CHECK_EQ(ui_stub_boot_progress_back, 1);                             // (only this test's restart took it back)
    ui_stub_request_steps = 0;
}

TEST(boot_animation_is_skipped_by_a_control_which_still_acts) {
    ui_fresh();
    app_boot_animation(&ui_app);
    if (ui_app.boot_anim == GUI_ANIM_INVALID) return;                    // (no image)
    const int row = ui_app.ui.row;
    ev_turn(CTL_ENC_A, 1);                                               // an encoder step: the animation goes, the row moves
    CHECK_EQ(ui_app.boot_anim, GUI_ANIM_INVALID);
    CHECK_EQ(ui_app.ui.row, row + 1);
    app_boot_animation(&ui_app);
    ev_press(CTL_KEY(1, 0));                                             // a note key
    CHECK_EQ(ui_app.boot_anim, GUI_ANIM_INVALID);
    ev_release(CTL_KEY(1, 0));
}
