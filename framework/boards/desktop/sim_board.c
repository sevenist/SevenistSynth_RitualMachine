// A board's control table as the parts of the simulator's panel window (sim_board.h). No SDL here.
#include "boards/desktop/sim_board.h"
#include <string.h>

static bool same_place(const input_control_t *a, const input_control_t *b) { return a->px == b->px && a->py == b->py; }

static bool carries_a_push(uint8_t kind) { return kind == CK_ENCODER || kind == CK_ENDLESS || kind == CK_KNOB || kind == CK_AXIS; }

// The button at the place of control `i` (its push), or the first control with a push at the place of button `i`.
static int at_place(const input_control_t *table, int count, int i, bool want_button) {
    for (int k = 0; k < count; k++) {
        if (k == i || !same_place(&table[k], &table[i])) continue;
        if (want_button ? table[k].kind == CK_BUTTON : carries_a_push(table[k].kind)) return k;
    }
    return INPUT_NO_CONTROL;
}

static sim_part_t *add(sim_panel_t *panel, sim_part_kind_t kind, const input_control_t *table, int ctl, int sw, int hw, int hh) {
    if (panel->count >= SIM_PARTS_MAX) return NULL;
    sim_part_t *p = &panel->parts[panel->count++];
    *p = (sim_part_t){(uint8_t)kind, (int16_t)ctl, INPUT_NO_CONTROL, (int16_t)sw,
                      (int16_t)(table[ctl].px * SIM_HALF_PITCH), (int16_t)(table[ctl].py * SIM_HALF_PITCH), (int16_t)hw, (int16_t)hh};
    return p;
}

void sim_part_box(const sim_part_t *p, int *left, int *top, int *right, int *bottom) {
    const bool round = p->kind != PART_KEY && p->kind != PART_BUTTON;
    const int half = round && p->hw < 22 ? 22 : p->hw;                       // a short text under a small knob is wider than the knob
    const int texts = !round ? 0 : p->kind == PART_KNOB ? 14 : 24;            // a knob: its name; the others: the name and the PC keys
    *left = p->x - half; *right = p->x + half;
    *top = p->y - p->hh; *bottom = p->y + p->hh + texts;
}

void sim_panel_build(sim_panel_t *panel, const input_control_t *table, int count) {
    memset(panel, 0, sizeof *panel);
    for (int i = 0; table && i < count; i++) {
        const int push = at_place(table, count, i, true);
        switch (table[i].kind) {
            case CK_KEY:     add(panel, PART_KEY, table, i, INPUT_NO_CONTROL, 24, 22); break;
            case CK_BUTTON:  if (at_place(table, count, i, false) == INPUT_NO_CONTROL) add(panel, PART_BUTTON, table, i, INPUT_NO_CONTROL, 26, 17); break;
            case CK_ENCODER: add(panel, PART_ENCODER, table, i, push, 22, 22); break;
            case CK_ENDLESS: add(panel, PART_ENDLESS, table, i, push, 20, 20); break;
            case CK_KNOB:    add(panel, PART_KNOB, table, i, push, 18, 18); break;
            case CK_AXIS: {
                sim_part_t *stick = NULL;                                     // the second axis at a place joins the first one's joystick
                for (int k = 0; k < panel->count && !stick; k++) {
                    sim_part_t *p = &panel->parts[k];
                    if (p->kind == PART_JOYSTICK && p->ctl2 == INPUT_NO_CONTROL && same_place(&table[p->ctl], &table[i])) stick = p;
                }
                if (stick) stick->ctl2 = (int16_t)i;
                else add(panel, PART_JOYSTICK, table, i, push, 42, 42);
                break;
            }
            default: break;
        }
    }
    int l = 0, t = 0, r = 0, b = 0;                                           // what the parts take, then moved to the margins
    for (int i = 0; i < panel->count; i++) {
        int pl, pt, pr, pb;
        sim_part_box(&panel->parts[i], &pl, &pt, &pr, &pb);
        if (i == 0 || pl < l) l = pl;
        if (i == 0 || pt < t) t = pt;
        if (i == 0 || pr > r) r = pr;
        if (i == 0 || pb > b) b = pb;
    }
    for (int i = 0; i < panel->count; i++) {
        panel->parts[i].x = (int16_t)(panel->parts[i].x - l + SIM_PANEL_MARGIN);
        panel->parts[i].y = (int16_t)(panel->parts[i].y - t + SIM_PANEL_TOP);
    }
    panel->width = r - l + 2 * SIM_PANEL_MARGIN;
    if (panel->width < 300) panel->width = 300;                               // the status lines need it
    panel->height = SIM_PANEL_TOP + b - t + SIM_PANEL_MARGIN + SIM_PANEL_STATUS;
}
