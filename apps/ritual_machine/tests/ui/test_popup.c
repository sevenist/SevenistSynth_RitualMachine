// The list popup of the drawing kit (framework/ui/popup.h): a list longer than the screen, headings the focus skips, labels asked
// from the caller (ADR-044 step 1).
#include "ui_test.h"
#include "ui/popup.h"

static int chosen = -2;
static void on_choice(void *ctx, int i) { (void)ctx; chosen = i; }

// 40 options: 0 and 20 are headings, the others "Item n".
static bool label(void *ctx, int i, char *out, int n) {
    (void)ctx;
    if (i == 0 || i == 20) { snprintf(out, (size_t)n, "HEAD %d", i); return true; }
    snprintf(out, (size_t)n, "Item %d", i);
    return false;
}

TEST(popup_list_scrolls_past_six_and_its_focus_skips_the_headings) {
    ui_fresh();                                        // (for its display)
    popup_t p;
    popup_init(&p);
    chosen = -2;
    CHECK(popup_choose_labels(&p, "LIST", 40, 0, label, on_choice, NULL));
    CHECK_EQ(p.queue[0].n_options, 40);
    CHECK_EQ(p.queue[0].focus, 1);                     // asked on a heading: the next option
    popup_move(&p, -1);
    CHECK_EQ(p.queue[0].focus, 39);                    // around, over the heading
    popup_move(&p, 1);
    for (int k = 0; k < 18; k++) popup_move(&p, 1);
    CHECK_EQ(p.queue[0].focus, 19);
    popup_move(&p, 1);
    CHECK_EQ(p.queue[0].focus, 21);                    // heading 20 skipped
    popup_move(&p, -1);
    CHECK_EQ(p.queue[0].focus, 19);
    popup_confirm(&p);
    CHECK_EQ(chosen, 19);                              // the index counts the headings
    CHECK(!popup_modal(&p));
    // drawn on the app's display at the end of the list and on a heading's neighbour: something is drawn, nothing goes wrong
    CHECK(popup_choose_labels(&p, "LIST", 40, 39, label, on_choice, NULL));
    u8g2_t *g = ui_app.display;
    u8g2_ClearBuffer(g);
    popup_draw(&p, g, &gui_default_style);
    int lit = 0;
    for (int i = 0; i < 128 * 128 / 8; i++) lit += g->tile_buf_ptr[i] != 0;
    CHECK(lit > 100);
    popup_cancel(&p);
    CHECK_EQ(chosen, -1);
    // the strings form keeps working, with more than six options now
    static const char *const many[] = {"a", "b", "c", "d", "e", "f", "g", "h"};
    CHECK(popup_choose(&p, "MANY", many, 8, 7, on_choice, NULL));
    CHECK_EQ(p.queue[0].n_options, 8);
    popup_move(&p, 1);
    popup_confirm(&p);
    CHECK_EQ(chosen, 0);
}
