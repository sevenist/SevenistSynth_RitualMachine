// The library's colour canvas (framework/ui/canvas.h): u8g2 and the drawing kit painting RGB565.
#include "tests/test.h"
#include "tests/stubs.h"
#include "ui/canvas.h"
#include "ui/gui.h"

namespace {
uint16_t frame[DISPLAY_WIDTH * DISPLAY_HEIGHT];
canvas_t cv;
constexpr uint16_t kRed = CANVAS_HEX(0xFF0000), kBlue = CANVAS_HEX(0x0000FF);

int count(uint16_t color) {
    int n = 0;
    for (uint16_t v : frame) n += v == color;
    return n;
}
}  // namespace

TEST(canvas_sizes) {
    CHECK(!canvas_init(&cv, frame, 0, 160));
    CHECK(!canvas_init(&cv, frame, 130, 160));                  // not a multiple of 8
    CHECK(!canvas_init(&cv, frame, 128, CANVAS_MAX_H + 8));
    CHECK(!canvas_init(&cv, nullptr, 128, 160));
    CHECK(canvas_init(&cv, frame, DISPLAY_WIDTH, DISPLAY_HEIGHT));
    CHECK_EQ(u8g2_GetDisplayWidth(canvas_u8g2(&cv)), DISPLAY_WIDTH);
    CHECK_EQ(u8g2_GetDisplayHeight(canvas_u8g2(&cv)), DISPLAY_HEIGHT);
    CHECK_EQ(count(0), DISPLAY_WIDTH * DISPLAY_HEIGHT);         // cleared to black
}

TEST(canvas_colors) {
    CHECK_EQ(CANVAS_HEX(0xFFFFFF), 0xFFFF);
    CHECK_EQ(CANVAS_HEX(0xFF0000), 0xF800);
    CHECK_EQ(CANVAS_HEX(0x00FF00), 0x07E0);
    CHECK_EQ(CANVAS_HEX(0x0000FF), 0x001F);
}

TEST(canvas_draws_in_the_foreground_and_background) {
    canvas_init(&cv, frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    u8g2_t *g = canvas_u8g2(&cv);
    canvas_color(&cv, kRed, kBlue);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawBox(g, 10, 20, 8, 4);
    CHECK_EQ(count(kRed), 32);
    CHECK_EQ(canvas_get(&cv, 10, 20), kRed);
    CHECK_EQ(canvas_get(&cv, 17, 23), kRed);
    CHECK_EQ(canvas_get(&cv, 18, 23), 0);
    u8g2_SetDrawColor(g, 0);                                    // clear = the background colour
    u8g2_DrawHLine(g, 10, 20, 8);
    CHECK_EQ(count(kRed), 24);
    CHECK_EQ(count(kBlue), 8);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawVLine(g, 0, 0, 5);                                 // vertical lines too
    CHECK_EQ(canvas_get(&cv, 0, 4), kRed);
    CHECK_EQ(canvas_get(&cv, 0, 5), 0);
}

TEST(canvas_xor_swaps_the_two_colours) {
    canvas_init(&cv, frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    u8g2_t *g = canvas_u8g2(&cv);
    canvas_color(&cv, kRed, kBlue);
    canvas_fill(&cv, 0, 0, 4, 1, kBlue);
    canvas_fill(&cv, 4, 0, 4, 1, kRed);
    u8g2_SetDrawColor(g, 2);
    u8g2_DrawHLine(g, 0, 0, 8);
    for (int x = 0; x < 4; x++) CHECK_EQ(canvas_get(&cv, x, 0), kRed);
    for (int x = 4; x < 8; x++) CHECK_EQ(canvas_get(&cv, x, 0), kBlue);
}

TEST(canvas_clips_to_the_frame) {
    canvas_init(&cv, frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    u8g2_t *g = canvas_u8g2(&cv);
    canvas_color(&cv, kRed, 0);
    u8g2_DrawBox(g, DISPLAY_WIDTH - 4, DISPLAY_HEIGHT - 2, 10, 10);   // past the right and bottom edges
    CHECK_EQ(count(kRed), 4 * 2);
    canvas_fill(&cv, -3, -3, 5, 5, kBlue);                            // past the top left
    CHECK_EQ(count(kBlue), 2 * 2);
    canvas_fill(&cv, DISPLAY_WIDTH, 0, 5, 5, kBlue);                  // wholly outside
    CHECK_EQ(count(kBlue), 4);
    CHECK_EQ(canvas_get(&cv, -1, 0), 0);
}

TEST(canvas_text_and_the_drawing_kit) {
    canvas_init(&cv, frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    u8g2_t *g = canvas_u8g2(&cv);
    gui_style_t style;
    gui_style_init(&style, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    gui_begin(g, &style);
    canvas_color(&cv, kRed, 0);
    u8g2_SetDrawColor(g, 1);
    gui_draw_text_centered(g, gui_rect_t{0, 0, DISPLAY_WIDTH, 16}, "KICK");
    const int lit = count(kRed);
    CHECK(lit > 20);                                            // the glyphs are painted in the foreground colour ...
    CHECK_EQ(lit + count(0), DISPLAY_WIDTH * DISPLAY_HEIGHT);   // ... and nothing else is touched
    for (int y = 16; y < DISPLAY_HEIGHT; y++) CHECK_EQ(canvas_get(&cv, DISPLAY_WIDTH / 2, y), 0);
}

TEST(canvas_send_hands_the_frame_over) {
    canvas_init(&cv, frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    canvas_fill(&cv, 0, 0, 1, 1, kBlue);
    const int before = stub_frames;
    canvas_send(&cv);
    CHECK_EQ(stub_frames, before + 1);
    CHECK_EQ(stub_frame[0], kBlue);
}
