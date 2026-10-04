#include "core/gui.h"
#include <stdio.h>

const gui_style_t gui_default_style = {
    .font = u8g2_font_5x7_tr,
    .margin = 0,
    .padding = 1,
    .gap = 1,
    .list_w = 64, .list_top = 2,
    .graph = {66, 14, 61, 49},
    .roll_pitch_px = 1, .roll_bar_h = 2, .roll_pad = 1, .roll_beat = 4, .strip_h = 2,
    .rack_pitch = 29, .rack_lane_gap = 3,   // pitch = module sprite + 4 (RACK_PITCH in ui_internal.h)
};

// The default style is the 128 x 64 layout; other sizes keep its fonts and spacings and stretch what depends on the screen:
// the list column takes half the width and the graph box fills the rest, below the header (graph.y) down to the bottom edge.
void gui_style_init(gui_style_t *st, int w, int h) {
    *st = gui_default_style;
    st->list_w = w / 2;
    st->graph.x = st->list_w + 2;
    st->graph.w = w - st->graph.x - 1;
    st->graph.h = h - st->graph.y - 1;
}

void gui_begin(u8g2_t *g, const gui_style_t *st) { u8g2_SetFont(g, st->font); }

gui_rect_t gui_rect(int x, int y, int w, int h) { return (gui_rect_t){x, y, w, h}; }
gui_rect_t gui_screen(u8g2_t *g) { return gui_rect(0, 0, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g)); }
gui_rect_t gui_inset(gui_rect_t r, int n) { return gui_rect(r.x + n, r.y + n, r.w - 2 * n, r.h - 2 * n); }
gui_point_t gui_center(gui_rect_t r) { return (gui_point_t){r.x + r.w / 2, r.y + r.h / 2}; }
int gui_right(gui_rect_t r)  { return r.x + r.w; }
int gui_bottom(gui_rect_t r) { return r.y + r.h; }

gui_rect_t gui_below(gui_rect_t r, int gap, int h)    { return gui_rect(r.x, r.y + r.h + gap, r.w, h); }
gui_rect_t gui_above(gui_rect_t r, int gap, int h)    { return gui_rect(r.x, r.y - gap - h, r.w, h); }
gui_rect_t gui_right_of(gui_rect_t r, int gap, int w) { return gui_rect(r.x + r.w + gap, r.y, w, r.h); }
gui_rect_t gui_left_of(gui_rect_t r, int gap, int w)  { return gui_rect(r.x - gap - w, r.y, w, r.h); }

gui_rect_t gui_take_top(gui_rect_t *r, int h) {
    gui_rect_t t = gui_rect(r->x, r->y, r->w, h);
    r->y += h; r->h -= h;
    return t;
}

gui_rect_t gui_take_left(gui_rect_t *r, int w) {
    gui_rect_t t = gui_rect(r->x, r->y, w, r->h);
    r->x += w; r->w -= w;
    return t;
}

gui_rect_t gui_grid_cell(gui_rect_t r, int cols, int rows, int index, int gap) {
    int cw = (r.w - gap * (cols - 1)) / cols;
    int ch = (r.h - gap * (rows - 1)) / rows;
    int cx = index % cols, cy = index / cols;
    return gui_rect(r.x + cx * (cw + gap), r.y + cy * (ch + gap), cw, ch);
}

gui_rect_t gui_center_box(gui_rect_t r, int w, int h) {
    return gui_rect(r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h);
}

/* ---------------- text ---------------- */

int gui_text_w(u8g2_t *g, const char *s) { return u8g2_GetStrWidth(g, s); }
int gui_text_h(u8g2_t *g) { return u8g2_GetAscent(g) - u8g2_GetDescent(g); }
int gui_row_h(u8g2_t *g, const gui_style_t *st) { return gui_text_h(g) + 2 * st->padding; }

// Baseline y that vertically centers one line of text in r.
static int baseline_in(u8g2_t *g, gui_rect_t r) {
    return r.y + (r.h - gui_text_h(g)) / 2 + u8g2_GetAscent(g);
}

gui_point_t gui_text_center(u8g2_t *g, gui_rect_t r, const char *s) {
    return (gui_point_t){r.x + (r.w - gui_text_w(g, s)) / 2, baseline_in(g, r)};
}

gui_rect_t gui_text_rect(u8g2_t *g, const char *s, int x, int y) {
    return gui_rect(x, y - u8g2_GetAscent(g), gui_text_w(g, s), gui_text_h(g));
}

void gui_draw_text_centered(u8g2_t *g, gui_rect_t r, const char *s) {
    gui_point_t p = gui_text_center(g, r, s);
    u8g2_DrawStr(g, p.x, p.y, s);
}

void gui_draw_text_left(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *s) {
    u8g2_DrawStr(g, r.x + st->padding + 1, baseline_in(g, r), s);
}

void gui_draw_text_right(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *s) {
    u8g2_DrawStr(g, gui_right(r) - st->padding - 1 - gui_text_w(g, s), baseline_in(g, r), s);
}

void gui_draw_field(u8g2_t *g, const gui_style_t *st, gui_rect_t r,
                    const char *label, const char *value, bool selected) {
    if (selected) { u8g2_DrawBox(g, r.x, r.y, r.w, r.h); u8g2_SetDrawColor(g, 0); }
    gui_draw_text_left(g, st, r, label);
    if (value) gui_draw_text_right(g, st, r, value);
    u8g2_SetDrawColor(g, 1);
}

void gui_draw_field_state(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *label, const char *value, gui_state_t state) {
    if (state == GUI_LATCHED) { u8g2_DrawBox(g, r.x, r.y, r.w, r.h); u8g2_SetDrawColor(g, 0); }
    else if (state == GUI_FOCUSED) u8g2_DrawFrame(g, r.x, r.y, r.w, r.h);
    gui_draw_text_left(g, st, r, label);
    if (value) {
        if (state == GUI_LATCHED) {
            char buf[40];
            snprintf(buf, sizeof buf, "<%s>", value);
            gui_draw_text_right(g, st, r, buf);
        } else {
            gui_draw_text_right(g, st, r, value);
        }
    }
    u8g2_SetDrawColor(g, 1);
}

void gui_draw_button(u8g2_t *g, const gui_style_t *st, gui_rect_t r, const char *label, gui_state_t state) {
    (void)st;
    if (state != GUI_PLAIN) { u8g2_DrawBox(g, r.x, r.y, r.w, r.h); u8g2_SetDrawColor(g, 0); }
    else u8g2_DrawFrame(g, r.x, r.y, r.w, r.h);
    gui_draw_text_centered(g, r, label);
    u8g2_SetDrawColor(g, 1);
}

void gui_draw_arrow(u8g2_t *g, int x, int y, char dir, int size) {
    for (int k = 0; k < size; k++) {                       // one line per step, from the base (widest) to the tip (1 pixel)
        const int half = size - 1 - k;
        switch (dir) {
            case 'd': u8g2_DrawHLine(g, x - half, y - (size - 1) + k, 2 * half + 1); break;
            case 'u': u8g2_DrawHLine(g, x - half, y + (size - 1) - k, 2 * half + 1); break;
            case 'r': u8g2_DrawVLine(g, x - (size - 1) + k, y - half, 2 * half + 1); break;
            default:  u8g2_DrawVLine(g, x + (size - 1) - k, y - half, 2 * half + 1); break;   // 'l'
        }
    }
}

/* ---------------- sprites ---------------- */

void gui_draw_sprite_frame(u8g2_t *g, const gui_sprite_t *sp, int frame, int x, int y) {
    int row_bytes = (sp->w + 7) / 8;
    int nframes = sp->frames ? sp->frames : 1;
    if (frame < 0 || frame >= nframes) frame = 0;
    u8g2_DrawBitmap(g, (u8g2_uint_t)x, (u8g2_uint_t)y, row_bytes, sp->h,
                    sp->data + (size_t)frame * sp->h * row_bytes);
}

void gui_draw_sprite_selected(u8g2_t *g, const gui_sprite_t *sp, int x, int y) {
    u8g2_DrawBox(g, (u8g2_uint_t)x, (u8g2_uint_t)y, sp->w, sp->h);
    u8g2_SetDrawColor(g, 0);
    u8g2_SetBitmapMode(g, 1);          // transparent: only the 1-bits are drawn (in color 0)
    gui_draw_sprite_frame(g, sp, 0, x, y);
    u8g2_SetBitmapMode(g, 0);
    u8g2_SetDrawColor(g, 1);
}

void gui_draw_sprite(u8g2_t *g, const gui_sprite_t *sp, int x, int y) {
    gui_draw_sprite_frame(g, sp, 0, x, y);
}

void gui_draw_sprite_centered(u8g2_t *g, const gui_sprite_t *sp, gui_rect_t r) {
    gui_rect_t b = gui_center_box(r, sp->w, sp->h);
    gui_draw_sprite(g, sp, b.x, b.y);
}

void gui_draw_sprite_right(u8g2_t *g, const gui_style_t *st, const gui_sprite_t *sp, gui_rect_t r) {
    gui_draw_sprite(g, sp, gui_right(r) - st->padding - 1 - sp->w, r.y + (r.h - sp->h) / 2);
}

/* ---------------- sprite animations ---------------- */

typedef struct {
    const gui_sprite_t *sp;     // NULL = free slot
    int      first, last;
    int      cur;               // current frame (sheet index)
    uint32_t frame_ms;
    uint32_t last_at;           // time the current frame started
    bool     loop, finished, fresh;
} anim_t;

static anim_t anims[GUI_ANIM_MAX];

static anim_t *anim_get(int id) {
    return (id >= 0 && id < GUI_ANIM_MAX && anims[id].sp) ? &anims[id] : NULL;
}

int gui_anim_add(const gui_sprite_t *sp, int first, int last, uint32_t frame_ms, bool loop) {
    int n = sp->frames ? sp->frames : 1;
    if (first < 0 || first >= n || last < 0 || last >= n) return GUI_ANIM_INVALID;
    for (int i = 0; i < GUI_ANIM_MAX; i++) {
        if (anims[i].sp) continue;
        anims[i] = (anim_t){sp, first, last, first, frame_ms ? frame_ms : 1, 0, loop, false, true};
        return i;
    }
    return GUI_ANIM_INVALID;      // table full
}

int gui_anim_add_total(const gui_sprite_t *sp, int first, int last, uint32_t total_ms, bool loop) {
    int count = (last >= first ? last - first : first - last) + 1;
    return gui_anim_add(sp, first, last, total_ms / (uint32_t)count, loop);
}

void gui_anim_remove(int id) { if (anim_get(id)) anims[id].sp = NULL; }

void gui_anim_clear(void) { for (int i = 0; i < GUI_ANIM_MAX; i++) anims[i].sp = NULL; }

void gui_anim_restart(int id) {
    anim_t *a = anim_get(id);
    if (!a) return;
    a->cur = a->first; a->finished = false; a->fresh = true;
}

bool gui_anim_finished(int id) { anim_t *a = anim_get(id); return a && a->finished; }
int  gui_anim_frame(int id)    { anim_t *a = anim_get(id); return a ? a->cur : -1; }

bool gui_anim_tick(uint32_t now_ms) {
    bool changed = false;
    for (int i = 0; i < GUI_ANIM_MAX; i++) {
        anim_t *a = &anims[i];
        if (!a->sp || a->finished) continue;
        if (a->fresh) { a->fresh = false; a->last_at = now_ms; changed = true; continue; }   // show frame `first`
        int dir = a->last >= a->first ? 1 : -1;
        while ((uint32_t)(now_ms - a->last_at) >= a->frame_ms && !a->finished) {
            a->last_at += a->frame_ms;
            if (a->cur == a->last) {
                if (a->loop) a->cur = a->first;
                else { a->finished = true; break; }
            } else {
                a->cur += dir;
            }
            changed = true;
        }
    }
    return changed;
}

void gui_anim_draw(u8g2_t *g, int id, int x, int y) {
    anim_t *a = anim_get(id);
    if (a) gui_draw_sprite_frame(g, a->sp, a->cur, x, y);
}

void gui_anim_draw_centered(u8g2_t *g, int id, gui_rect_t r) {
    anim_t *a = anim_get(id);
    if (!a) return;
    gui_rect_t b = gui_center_box(r, a->sp->w, a->sp->h);
    gui_draw_sprite_frame(g, a->sp, a->cur, b.x, b.y);
}

void gui_anim_draw_right(u8g2_t *g, const gui_style_t *st, int id, gui_rect_t r) {
    anim_t *a = anim_get(id);
    if (!a) return;
    gui_draw_sprite_frame(g, a->sp, a->cur, gui_right(r) - st->padding - 1 - a->sp->w,
                          r.y + (r.h - a->sp->h) / 2);
}
