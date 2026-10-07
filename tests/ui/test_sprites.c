// UI sprites (assets/UI_Sprites/README.md): masked drawing (gui.c), the generated lookup (ui_sprites_gen.c) and the module fallback.
#include "ui_test.h"
#include "core/ui_sprites_gen.h"

static bool pixel(u8g2_t *g, int x, int y) {               // the SH1107 full buffer: pages of 8 rows, one byte per column
    const uint8_t *buf = u8g2_GetBufferPtr(g);
    return (buf[(y / 8) * 128 + x] >> (y % 8)) & 1;
}

TEST(sprites_mask_keeps_transparent_pixels) {
    u8g2_t *g = ui_app.display;
    // 8x1: lit, off (drawn), transparent x6
    static const uint8_t data[1] = {0x80}, mask[1] = {0xC0};
    static const gui_sprite_t sp = {8, 1, data, 1, mask};
    u8g2_ClearBuffer(g);
    u8g2_DrawBox(g, 0, 0, 8, 1);                               // a lit line behind
    gui_draw_sprite(g, &sp, 0, 0);
    CHECK(pixel(g, 0, 0));                                     // lit
    CHECK(!pixel(g, 1, 0));                                    // opaque off: erased
    for (int x = 2; x < 8; x++) CHECK(pixel(g, x, 0));         // transparent: the line shows through
    CHECK_EQ(u8g2_GetDrawColor(g), 1);
    u8g2_ClearBuffer(g);                                       // drawn in colour 0 (on a selected box): the roles swap
    u8g2_DrawBox(g, 0, 0, 8, 1);
    u8g2_SetDrawColor(g, 0);
    gui_draw_sprite(g, &sp, 0, 0);
    u8g2_SetDrawColor(g, 1);
    CHECK(!pixel(g, 0, 0) && pixel(g, 1, 0) && pixel(g, 2, 0));
}

TEST(sprites_lookup_and_fallback) {
    CHECK(ui_sprite("8/ui_xy") == &spr8_ui_xy);                // assets/UI_Sprites/8/ui_xy.png
    CHECK(spr8_ui_xy.w == 8 && spr8_ui_xy.h == 8 && spr8_ui_xy.mask != NULL);
    CHECK(ui_sprite("24/mod_nothing") == NULL);                // no image: the caller draws its fallback
}
