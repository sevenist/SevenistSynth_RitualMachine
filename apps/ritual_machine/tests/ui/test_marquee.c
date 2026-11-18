// Sliding text (core/ui_widgets.h uw_marquee; the user, 2026-11-19): a text wider than its window is cut there, and while it (or what holds
// it) is selected it holds 1 s at its start, then slides through the window and wraps around; the app draws frames meanwhile. A text that
// fits is drawn as it is. A menu row whose label and value do not fit side by side slides while focused.
#include "ui_test.h"
#include "core/ui_widgets.h"
#include "core/ui_theme.h"
#include <string.h>

static uint32_t pixels(u8g2_t *g) {                      // a hash of the one-colour buffer
    const uint8_t *b = u8g2_GetBufferPtr(g);
    const int n = 8 * u8g2_GetBufferTileWidth(g) * u8g2_GetBufferTileHeight(g);
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static bool pixel(u8g2_t *g, int x, int y) {             // (u8g2's full buffer: 8 rows a byte, a tile row after the other)
    const uint8_t *b = u8g2_GetBufferPtr(g);
    return (b[(y / 8) * 8 * u8g2_GetBufferTileWidth(g) + x] >> (y & 7)) & 1;
}

static uint32_t marquee_at(u8g2_t *g, uint32_t ms, const char *s, bool moving) {
    u8g2_ClearBuffer(g);
    u8g2_SetMaxClipWindow(g);
    uw_frame_begin(ms);
    uw_marquee(g, 10, 20, 40, s, UC_TEXT, moving);
    return pixels(g);
}

TEST(a_long_text_holds_then_slides_and_wraps_while_selected) {
    ui_fresh();
    u8g2_t *g = ui_app.display;
    gui_style_t st;
    gui_style_init(&st, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    gui_begin(g, &st);
    const char *longer = "Spectral freeze and more";
    const uint32_t still = marquee_at(g, 0, longer, false);
    CHECK_EQ(marquee_at(g, 5000, longer, false), still);                 // not selected: cut, never moving
    const uint32_t start = marquee_at(g, 1000, longer, true);              // selected: its start at first ...
    CHECK_EQ(start, still);
    CHECK_EQ(marquee_at(g, 1900, longer, true), still);                    // ... held for a second
    CHECK(uw_marquee_moving());
    CHECK(marquee_at(g, 2600, longer, true) != still);                     // then sliding
    for (int i = 0; i < 2; i++) uw_frame_begin(3000);                      // gone from the screen two frames: forgotten, not moving
    CHECK(!uw_marquee_moving());
    const char *fits = "Filter";                                           // a text that fits: as it is, selected or not
    CHECK_EQ(marquee_at(g, 4000, fits, true), marquee_at(g, 4000, fits, false));
    u8g2_ClearBuffer(g);
    u8g2_SetMaxClipWindow(g);
    uw_text(g, 10, 20, fits, UC_TEXT);
    CHECK_EQ(pixels(g), marquee_at(g, 9000, fits, true));
    // nothing drawn outside the window, sliding or not
    marquee_at(g, 2600, longer, true);
    for (int y = 0; y < u8g2_GetDisplayHeight(g); y++)
        for (int x = 0; x < u8g2_GetDisplayWidth(g); x++)
            if (x < 10 || x >= 50) CHECK(!pixel(g, x, y));
}

// A focused menu row too long for its label and value: they slide (the frame changes over time); not focused, it stays still.
TEST(a_focused_row_too_long_for_its_label_and_value_slides) {
    ui_fresh();
    u8g2_t *g = ui_app.display;
    gui_style_t st;
    gui_style_init(&st, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    gui_begin(g, &st);
    const gui_rect_t r = gui_rect(0, 20, 96, gui_row_h(g, &st) + 2);
    uint32_t h[2][2];
    for (int focused = 0; focused < 2; focused++)
        for (int k = 0; k < 2; k++) {
            u8g2_ClearBuffer(g);
            uw_frame_begin(k ? 4000 : 500);
            uw_field(g, &st, r, "Type", "Motion sequencer long name", UC_MOD, focused ? GUI_FOCUSED : GUI_PLAIN, false);
            h[focused][k] = pixels(g);
        }
    CHECK_EQ(h[0][0], h[0][1]);
    CHECK(h[1][0] != h[1][1]);
}
