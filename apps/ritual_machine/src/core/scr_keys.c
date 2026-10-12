// The KEYS tab of the menu: what every key of the matrix does (core/keymap.h). Layout picks one of the built-in layouts or User; Key picks a
// key (pressing a key on the board does the same); Func gives it a function (that switches to User); Reset goes back to the first layout.
// On the right: the list of the keys with their functions, the selected one highlighted. Saved to the card when the menu closes.
// A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include "core/keymap.h"
#include "hal/hal_input.h"
#include <stdio.h>

// "F1".."F8" for the function row, then "A1".."D8" for the note rows from the top.
static void key_label(int key, char *out, int n) {
    const int r = key / KEY_COLS, c = key % KEY_COLS;
    if (r == 0) snprintf(out, (size_t)n, "F%d", c + 1);
    else        snprintf(out, (size_t)n, "%c%d", 'A' + r - 1, c + 1);
}

static bool present(int key) { return input_key_present(key / KEY_COLS, key % KEY_COLS); }

static void layout_value(const ui_ctx_t *c, char *out, int n) { (void)c; snprintf(out, (size_t)n, "%s", keymap_layout_name(keymap_layout())); }
static bool layout_adjust(const ui_ctx_t *c, int dir) {
    (void)c;
    keymap_select((keymap_layout() + dir + KEYMAP_LAYOUTS) % KEYMAP_LAYOUTS);
    return false;
}

static void key_value(const ui_ctx_t *c, char *out, int n) { key_label(c->ui->key_cur, out, n); }
static bool key_adjust(const ui_ctx_t *c, int dir) {
    int k = c->ui->key_cur;
    for (int i = 0; i < KEY_COUNT; i++) {                     // the next key the board has
        k = (k + dir + KEY_COUNT) % KEY_COUNT;
        if (present(k)) break;
    }
    c->ui->key_cur = k;
    return false;
}

static void fn_value(const ui_ctx_t *c, char *out, int n) { keymap_fn_name(keymap_get(c->ui->key_cur), out, n); }
static bool fn_adjust(const ui_ctx_t *c, int dir) {
    const int count = keymap_fn_count();
    const int i = (keymap_fn_index(keymap_get(c->ui->key_cur)) + dir + count) % count;
    keymap_set(c->ui->key_cur, keymap_fn_at(i));
    return false;
}

static void reset_activate(const ui_ctx_t *c) { (void)c; keymap_reset(); }

static const el_def_t elements[] = {
    {"Layout", EL_VALUE,  0, 0, 1, layout_value, layout_adjust, NULL, NULL, NULL},
    {"Key",    EL_VALUE,  1, 0, 1, key_value,    key_adjust,    NULL, NULL, NULL},
    {"Func",   EL_VALUE,  2, 0, 1, fn_value,     fn_adjust,     NULL, NULL, NULL},
    {"Reset",  EL_BUTTON, 3, 0, 1, NULL,         NULL,          reset_activate, NULL, NULL},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

// The key list in the picture box: one line per key the board has, scrolled so the selected key stays in view (a third from the top).
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    const gui_rect_t box = ui_picture_box(st);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int lh = gui_text_h(g) + 1;
    const int lines = (box.h - 2) / lh;
    int keys[KEY_COUNT], n = 0, sel = 0;
    for (int k = 0; k < KEY_COUNT; k++) if (present(k)) { if (k == c->ui->key_cur) sel = n; keys[n++] = k; }
    int first = sel - lines / 3;
    if (first > n - lines) first = n - lines;
    if (first < 0) first = 0;
    for (int i = 0; i < lines && first + i < n; i++) {
        const int k = keys[first + i];
        char name[6], fn[12], line[20];
        key_label(k, name, sizeof name);
        keymap_fn_name(keymap_get(k), fn, sizeof fn);
        snprintf(line, sizeof line, "%-3s %s", name, fn);
        const gui_rect_t r = gui_rect(box.x + 1, box.y + 1 + i * lh, box.w - 2, lh);
        if (k == c->ui->key_cur) { u8g2_DrawBox(g, r.x, r.y, r.w, r.h); u8g2_SetDrawColor(g, 0); }
        u8g2_DrawStr(g, r.x + 2, r.y + lh - 2, line);
        u8g2_SetDrawColor(g, 1);
    }
    // under the rows on the left: whether the layout still has to go to the card
    const int rh = gui_row_h(g, st);
    const gui_rect_t list = gui_rect(area.x, area.y, st->list_w, area.h);
    if (gui_bottom(rect[N_ELEMENTS - 1]) + 2 * rh <= gui_bottom(list)) {
        gui_draw_text_left(g, st, gui_rect(list.x, gui_bottom(list) - 2 * rh, list.w, rh), "Press a key");
        gui_draw_text_left(g, st, gui_rect(list.x, gui_bottom(list) - rh, list.w, rh), keymap_dirty() ? "Saved on exit" : "Saved");
    }
}

const screen_def_t scr_keys_screen = {elements, N_ELEMENTS, false, false, layout, NULL, NULL, draw_extra};
