// The MACROS tab of the menu: macros 1..8, each driving up to 8 parameters at once (synth_ui.h: macro_def_t). Macro picks a macro; Value
// plays it as its knob would, 5 % a step (Shift: 1 %), for a macro no knob plays (ADR-046 phase 5: HWV1 plays 1 and 2 only); Dest
// picks one of its destinations; Tgt names it; Min / Max (% of the parameter's range, Min > Max inverts) and Curve shape how the knob's
// travel reaches it (core/curves.h); Add (left / right: what it adds) is Learn, which goes back to the pages where the next push on a row
// adds that row's parameter as a destination (Shift + R knob or a "Learn M" entry does the same from the pages), or another macro M2..M8:
// this macro then drives that one's position, and that one its own destinations (framework/control/macro.h; a macro that would drive
// itself back is not offered); Remove drops the destination.
// On the right: the selected destination's response (knob left to right, the parameter bottom to top) and the destination count.
// A macro is played by R1..R3 (macros 1..3) or by any knob given "Macro N" in the MODIFIERS tab. Saved with the sound (core/patch.h).
// A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include "core/ui_widgets.h"
#include "core/led_roles.h"
#include "core/curves.h"
#include "core/fine_step.h"
#include <stdio.h>

static macro_def_t *mac(const ui_ctx_t *c) { return &c->ui->macro[c->ui->macro_cur % SYNTH_UI_MACROS]; }
static mapping_t *dest(const ui_ctx_t *c) {
    macro_def_t *m = mac(c);
    if (m->n == 0) return NULL;
    if (c->ui->macro_dest >= m->n) c->ui->macro_dest = m->n - 1;
    return &m->d[c->ui->macro_dest];
}

static void macro_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "M%d", c->ui->macro_cur + 1); }
static bool macro_adjust(const ui_ctx_t *c, int dir) {
    c->ui->macro_cur = (c->ui->macro_cur + dir + SYNTH_UI_MACROS) % SYNTH_UI_MACROS;
    c->ui->macro_dest = 0;
    c->ui->macro_link = 0;
    return false;
}

static bool has_dest(const ui_ctx_t *c) { return mac(c)->n > 0; }

// Value: where the macro is (synth_ui_macro_pos), set like a knob at that position (every destination through its mapping).
static void pos_value(const ui_ctx_t *c, char *out, int n) {
    const int p = synth_ui_macro_pos(c->ui, c->params, c->seq, c->rack, c->ui->macro_cur, true);
    if (p < 0) snprintf(out, (size_t)n, "--"); else snprintf(out, (size_t)n, "%d%%", p);
}
static bool pos_adjust(const ui_ctx_t *c, int dir) {
    const int k = c->ui->macro_cur;
    if (!c->params || !c->seq) return false;
    int p = synth_ui_macro_pos(c->ui, c->params, c->seq, c->rack, k, false);
    p = (p < 0 ? 50 : p) + dir * fine_int_step(5);
    p = p < 0 ? 0 : p > 100 ? 100 : p;
    return synth_ui_macro(c->ui, c->params, c->seq, c->rack, -1, k, (p * INPUT_VALUE_MAX + 50) / 100);
}
// Color: the macro's LED (R1..R3, and a knob that plays it through Shift / Mod; ADR-045), a palette colour (never Off).
static void color_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", led_palette_name(mac(c)->color)); }
static bool color_adjust(const ui_ctx_t *c, int dir) {
    macro_def_t *m = mac(c);
    m->color = (uint8_t)((m->color - 1 + dir + (LED_PALETTE - 1)) % (LED_PALETTE - 1) + 1);
    return false;
}
static void dest_value(const ui_ctx_t *c, char *out, int n) {
    if (!has_dest(c)) snprintf(out, (size_t)n, "--");
    else { dest(c); snprintf(out, (size_t)n, "%d/%d", c->ui->macro_dest + 1, mac(c)->n); }
}
static bool dest_adjust(const ui_ctx_t *c, int dir) {
    const int n = mac(c)->n;
    if (n) c->ui->macro_dest = (c->ui->macro_dest + dir + n) % n;
    return false;
}

static void tgt_value(const ui_ctx_t *c, char *out, int n) {
    const mapping_t *d = dest(c);
    char name[24];
    if (!d || !synth_ui_target_describe(NULL, NULL, c->rack, &d->t, name, sizeof name, NULL, 0)) snprintf(out, (size_t)n, "--");
    else snprintf(out, (size_t)n, "%s", name);
}

// Min / Max / Curve (arg 0 / 1 / 2) of the selected destination.
static void map_value(const ui_ctx_t *c, char *out, int n) {
    const mapping_t *d = dest(c);
    if (!d) { snprintf(out, (size_t)n, "--"); return; }
    if (c->arg == 2) snprintf(out, (size_t)n, "%s", curve_name(d->curve));
    else snprintf(out, (size_t)n, "%d%%", c->arg == 0 ? mapping_min(d) : mapping_max(d));
}
static bool map_adjust(const ui_ctx_t *c, int dir) {
    mapping_t *d = dest(c);
    if (!d) return false;
    if (c->arg == 2) { d->curve = curve_step(d->curve, dir); return false; }
    int v = (c->arg == 0 ? mapping_min(d) : mapping_max(d)) + 5 * dir;
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    if (c->arg == 0) mapping_set_min(d, v); else mapping_set_max(d, v);
    return false;
}

// Add: 0 = Learn (back to the pages: the app closes the menu), k + 1 = a link to macro k (one this macro can take: mac_can_link).
static int add_choice(const ui_ctx_t *c) {
    const int j = c->ui->macro_link - 1;
    return j >= 0 && mac_can_link(c->ui->macro, c->ui->macro_cur, j) ? c->ui->macro_link : 0;
}
static void add_value(const ui_ctx_t *c, char *out, int n) {
    const int a = add_choice(c);
    if (a == 0) snprintf(out, (size_t)n, "Learn"); else snprintf(out, (size_t)n, "M%d", a);
}
static bool add_adjust(const ui_ctx_t *c, int dir) {
    int a = add_choice(c);
    for (int i = 0; i <= SYNTH_UI_MACROS; i++) {
        a = (a + dir + SYNTH_UI_MACROS + 1) % (SYNTH_UI_MACROS + 1);
        if (a == 0 || mac_can_link(c->ui->macro, c->ui->macro_cur, a - 1)) break;
    }
    c->ui->macro_link = a;
    return false;
}
static void add_activate(const ui_ctx_t *c) {
    const int a = add_choice(c);
    if (a == 0) { c->ui->learn_macro = (int8_t)c->ui->macro_cur; c->ui->learn_req = true; return; }
    const macro_t t = mac_target(a - 1);
    if (synth_ui_macro_add(c->ui, c->ui->macro_cur, &t)) c->ui->macro_dest = mac(c)->n - 1;
    c->ui->macro_link = 0;
}
static void remove_activate(const ui_ctx_t *c) { synth_ui_macro_remove(c->ui, c->ui->macro_cur, c->ui->macro_dest); if (c->ui->macro_dest > 0) c->ui->macro_dest--; }

static const el_def_t elements[] = {
    {"Macro",  EL_VALUE,  0, 0, 1, macro_value, macro_adjust, NULL, NULL, NULL},
    {"Value",  EL_VALUE,  1, 0, 1, pos_value,   pos_adjust,   NULL, NULL, has_dest},
    {"Color",  EL_VALUE,  2, 0, 1, color_value, color_adjust, NULL, NULL, NULL},
    {"Dest",   EL_VALUE,  3, 0, 1, dest_value,  dest_adjust,  NULL, NULL, has_dest},
    {"Tgt",    EL_VALUE,  4, 0, 1, tgt_value,   NULL,         NULL, NULL, has_dest},
    {"Min",    EL_VALUE,  5, 0, 1, map_value,   map_adjust,   NULL, NULL, has_dest, NULL, 0},
    {"Max",    EL_VALUE,  6, 0, 1, map_value,   map_adjust,   NULL, NULL, has_dest, NULL, 1},
    {"Curve",  EL_VALUE,  7, 0, 1, map_value,   map_adjust,   NULL, NULL, has_dest, NULL, 2},
    {"Add",    EL_DIRECT, 8, 0, 1, add_value,   add_adjust,   add_activate,    NULL, NULL},
    {"Remove", EL_BUTTON, 9, 0, 1, NULL,        NULL,         remove_activate, NULL, has_dest},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    (void)c;
    ui_layout_column(g, st, area, N_ELEMENTS, rect);
}

// The response of the selected destination: knob position left to right, the parameter's value bottom to top (Min / Max dotted).
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)area; (void)rect;
    const gui_rect_t box = ui_picture_box(st);
    uw_box(g, box);                                         // (the picture's frame: dim in colour)
    char line[32];
    snprintf(line, sizeof line, "M%d: %d dest", c->ui->macro_cur + 1, mac(c)->n);
    const int lh = gui_text_h(g) + 1;
    u8g2_DrawStr(g, box.x + 3, box.y + lh, line);
    const mapping_t *d = dest(c);
    if (!d) return;
    const int x0 = box.x + 3, x1 = box.x + box.w - 4, y0 = box.y + lh + 4, y1 = box.y + box.h - 4;
    if (x1 <= x0 + 4 || y1 <= y0 + 4) return;
    for (int x = x0; x <= x1; x += 3) {                                   // Min and Max levels, dotted
        u8g2_DrawPixel(g, x, y1 - (int)((float)mapping_min(d) / 100.0f * (float)(y1 - y0)));
        u8g2_DrawPixel(g, x, y1 - (int)((float)mapping_max(d) / 100.0f * (float)(y1 - y0)));
    }
    int px = 0, py = 0;
    for (int x = x0; x <= x1; x++) {
        const float y = mapping_apply(d, (float)(x - x0) / (float)(x1 - x0));
        const int yy = y1 - (int)(y * (float)(y1 - y0) + 0.5f);
        if (x > x0) u8g2_DrawLine(g, px, py, x, yy);
        px = x; py = yy;
    }
}

const screen_def_t scr_macros_screen = {elements, N_ELEMENTS, false, false, layout, NULL, NULL, draw_extra};
