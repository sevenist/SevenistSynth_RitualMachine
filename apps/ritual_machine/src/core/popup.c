#include "core/popup.h"
#include <stdio.h>
#include <string.h>

#define POPUP_LINES   5            // wrapped text lines at most (fewer on a short screen)
#define POPUP_ARROW   3            // INFO arrow size in pixels

static void msg_set(popup_msg_t *m, popup_kind_t kind, const char *title, const char *text) {
    memset(m, 0, sizeof *m);
    m->kind = (uint8_t)kind;
    snprintf(m->title, sizeof m->title, "%s", title ? title : "");
    snprintf(m->text, sizeof m->text, "%s", text ? text : "");
}

void popup_init(popup_t *p) { memset(p, 0, sizeof *p); }

void popup_info(popup_t *p, const char *title, const char *text, int arrow, uint32_t now_ms, uint32_t ms) {
    msg_set(&p->info, POPUP_INFO, title, text);
    p->info.arrow = (int8_t)(arrow < 0 ? -1 : arrow > 0 ? 1 : 0);
    p->info_until = now_ms + ms;
}

static popup_msg_t *queue_add(popup_t *p) { return p->count < POPUP_QUEUE ? &p->queue[p->count++] : NULL; }

bool popup_error(popup_t *p, const char *title, const char *text, popup_answer_fn answer, void *ctx) {
    popup_msg_t *m = queue_add(p);
    if (!m) return false;
    msg_set(m, POPUP_ERROR, title ? title : "ERROR", text);
    m->answer = answer; m->ctx = ctx;
    return true;
}

bool popup_ask(popup_t *p, const char *title, const char *text, bool default_yes, popup_answer_fn answer, void *ctx) {
    popup_msg_t *m = queue_add(p);
    if (!m) return false;
    msg_set(m, POPUP_ASK, title, text);
    m->yes = default_yes;
    m->answer = answer; m->ctx = ctx;
    return true;
}

bool popup_modal(const popup_t *p) { return p->count > 0; }

bool popup_tick(popup_t *p, uint32_t now_ms) {
    if (p->info.kind == POPUP_NONE || (int32_t)(now_ms - p->info_until) < 0) return false;
    p->info.kind = POPUP_NONE;
    return p->count == 0;                                   // under a modal popup it was not visible anyway
}

void popup_move(popup_t *p, int dir) {
    if (p->count && p->queue[0].kind == POPUP_ASK && dir) p->queue[0].yes = dir < 0;
}

// Closes the popup on screen, then answers: the callback may raise the next popup.
static void close_top(popup_t *p, bool yes) {
    if (!p->count) return;
    const popup_msg_t m = p->queue[0];
    memmove(&p->queue[0], &p->queue[1], (size_t)(p->count - 1) * sizeof p->queue[0]);
    p->count--;
    if (m.answer) m.answer(m.ctx, yes);
}

void popup_confirm(popup_t *p) { if (p->count) close_top(p, p->queue[0].kind == POPUP_ERROR || p->queue[0].yes); }
void popup_cancel(popup_t *p)  { if (p->count) close_top(p, p->queue[0].kind == POPUP_ERROR); }

/* ---------------- drawing ---------------- */

// Splits `text` into lines no wider than `w` pixels, at spaces and '\n' (a word wider than a line is cut). Returns the line count.
static int wrap(u8g2_t *g, const char *text, int w, char lines[][POPUP_TEXT_LEN], int max_lines) {
    int n = 0;
    const char *s = text;
    while (*s && n < max_lines) {
        char *l = lines[n];
        int fit = -1;                                       // length of the line up to the last word end that fits
        for (int i = 0; ; ) {
            int j = i;
            while (s[j] && s[j] != ' ' && s[j] != '\n') j++;   // end of the next word
            memcpy(l, s, (size_t)j); l[j] = 0;
            if (gui_text_w(g, l) > w) break;
            fit = j;
            if (s[j] != ' ') break;                         // end of the text or of the paragraph
            i = j + 1;
        }
        if (fit < 0) {                                      // the first word is wider than the line: cut it where it stops fitting
            fit = 1;
            while (s[fit] && s[fit] != ' ' && s[fit] != '\n') {
                memcpy(l, s, (size_t)fit + 1); l[fit + 1] = 0;
                if (gui_text_w(g, l) > w) break;
                fit++;
            }
        }
        memcpy(l, s, (size_t)fit); l[fit] = 0;
        n++;
        s += fit;
        if (*s == ' ' || *s == '\n') s++;
    }
    return n;
}

void popup_draw(const popup_t *p, u8g2_t *g, const gui_style_t *st) {
    const popup_msg_t *m = p->count ? &p->queue[0] : p->info.kind != POPUP_NONE ? &p->info : NULL;
    if (!m) return;
    gui_begin(g, st);
    const gui_rect_t scr = gui_screen(g);
    const int row_h = gui_row_h(g, st);
    const int line_h = gui_text_h(g) + 1;
    const bool titled = m->title[0] != 0;
    const bool buttons = m->kind != POPUP_INFO;
    const int w = scr.w > 96 ? scr.w - 16 : scr.w - 4;
    const int text_w = w - 2 * (st->padding + 2) - (m->arrow ? POPUP_ARROW + 2 : 0);
    const int fixed = 2 + (titled ? row_h + 1 : 1) + 2 + (buttons ? row_h + 3 : 0);
    int max_lines = (scr.h - 4 - fixed) / line_h;
    if (max_lines > POPUP_LINES) max_lines = POPUP_LINES;
    if (max_lines < 1) max_lines = 1;
    char lines[POPUP_LINES][POPUP_TEXT_LEN];
    const int n = wrap(g, m->text, text_w, lines, max_lines);
    const gui_rect_t box = gui_center_box(scr, w, fixed + n * line_h);

    u8g2_SetDrawColor(g, 0);                                // clear the screen behind, one pixel around the frame
    u8g2_DrawBox(g, box.x - 1, box.y - 1, box.w + 2, box.h + 2);
    u8g2_SetDrawColor(g, 1);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);

    int y = box.y + 1;
    if (titled) {                                           // title bar: filled, text dark, like the full-screen notices
        const gui_rect_t bar = gui_rect(box.x, box.y, box.w, row_h + 1);
        u8g2_DrawBox(g, bar.x, bar.y, bar.w, bar.h);
        u8g2_SetDrawColor(g, 0);
        gui_draw_text_centered(g, bar, m->title);
        u8g2_SetDrawColor(g, 1);
        y = gui_bottom(bar);
    }
    y += 2;
    for (int i = 0; i < n; i++, y += line_h) {
        const int tw = gui_text_w(g, lines[i]);
        const int aw = m->arrow && i == n - 1 ? POPUP_ARROW + 2 : 0;   // the arrow follows the last line
        const int x = box.x + (box.w - tw - aw) / 2;
        u8g2_DrawStr(g, x, y + u8g2_GetAscent(g), lines[i]);
        if (aw) {
            const int ay = y + u8g2_GetAscent(g) / 2;
            if (m->arrow > 0) gui_draw_arrow(g, x + tw + aw - 1, ay, 'r', POPUP_ARROW);
            else              gui_draw_arrow(g, x + tw + 2, ay, 'l', POPUP_ARROW);
        }
    }
    if (buttons) {
        const gui_rect_t area = gui_rect(box.x + 4, gui_bottom(box) - row_h - 3, box.w - 8, row_h);
        if (m->kind == POPUP_ASK) {
            gui_draw_button(g, st, gui_center_box(gui_grid_cell(area, 2, 1, 0, 4), 30, row_h), "Yes", m->yes ? GUI_FOCUSED : GUI_PLAIN);
            gui_draw_button(g, st, gui_center_box(gui_grid_cell(area, 2, 1, 1, 4), 30, row_h), "No", m->yes ? GUI_PLAIN : GUI_FOCUSED);
        } else {
            gui_draw_button(g, st, gui_center_box(area, 30, row_h), "OK", GUI_FOCUSED);
        }
    }
}

bool popup_info_showing(const popup_t *p) { return p->info.kind != POPUP_NONE; }
