// The MAP tab's node page and PICK list (apps/ritual_machine/docs/GUI_NODES.md levels L1 and L2, the look of apps/gui_mockup). A push on a module
// of the map (scr_map.c) opens its page; ui_screen.c's screen_shown() shows this screen while ui->map_at names a module, the PICK list while
// ui->map_pick is set too. Both are declarative screens placed by data: every element has a `box` (ui_screen.h), no layout() code.
//
// The node page: the title (the family's colour), then two tabs, VALUES and PORTS: a module's parameters and its cables are split so the page
// fits a 128 x 128 screen.
//   VALUES   the module's picture (as on its first page), then its values as bars (a list: it scrolls). Push latches a value, the joystick or
//            encoder B changes it, as on the module's own pages. A value a modulator drives shows "~" (and an amber line in colour).
//   PORTS    one row per cable: audio in, audio out, each modulation coming in ("Cut <- LF1 +1.00"), a modulator's own ("-> FL1 Cut"). A push
//            follows the cable: the page of the module at its other end. Left / right on a modulation row change its depth. The last row,
//            "+ modulation", opens the PICK list.
// Back: the map, the cursor on this node.
//
// The PICK list: the modulators of the rack (and the oscillators: FM), each with where it goes now; "To" picks which parameter of this module
// the cable goes to. A push connects: the modulator now drives that parameter (a modulator has one target: it leaves the one it had, which the
// list said). Applied when the menu closes, as every edit of the rack's structure (rack_dirty). Back: the node page.
#include "core/ui_screen.h"
#include "core/ui_widgets.h"
#include "core/nav_graph.h"
#include <stdio.h>
#include <string.h>

#define VALUES  20                       // value rows (MOD_PARAM_MAX)
#define PORTS   10                       // port rows
#define SOURCES 16                       // PICK rows (RACK_MAX)

enum { E_TITLE, E_TABS, E_PIC, E_V0, E_P0 = E_V0 + VALUES, E_COUNT = E_P0 + PORTS };
enum { K_TITLE, K_TO, K_INFO, K_S0, K_COUNT = K_S0 + SOURCES };
_Static_assert(VALUES == MOD_PARAM_MAX && SOURCES == RACK_MAX, "one row per parameter, one per module");

static int node_of(const ui_ctx_t *c) { return c->ui->map_at - 1; }                       // also the module's slot (nav_graph.h)
static rack_slot_t *slot_of(const ui_ctx_t *c) { return &c->rack->slot[node_of(c)]; }
static module_type_t type_of(const ui_ctx_t *c) { return (module_type_t)slot_of(c)->type; }

static const nav_graph_t *graph(const ui_ctx_t *c) {
    static nav_graph_t g;
    nav_build(&g, c->rack);
    return &g;
}

static void node_name(const ui_ctx_t *c, int node, char *out, int n) {
    if (node == NAV_MIX) snprintf(out, (size_t)n, "MIX");
    else if (node == NAV_OUT) snprintf(out, (size_t)n, "OUT");
    else rack_slot_name(c->rack, node, out, n);
}

/* ---------------- the title and the tabs ---------------- */

static void title_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)st; (void)focused; (void)latched;
    char name[8];
    rack_slot_name(c->rack, node_of(c), name, sizeof name);
    uw_title(g, r, name, rack_type_name(type_of(c)), ui_family(type_of(c)));
}

static const char *const tab_names[2] = {"VALUES", "PORTS"};
static bool tabs_adjust(const ui_ctx_t *c, int dir) { (void)dir; c->ui->map_page = (uint8_t)!c->ui->map_page; return false; }
static void tabs_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)st; (void)latched;
    uw_tabs(g, r, tab_names, 2, c->ui->map_page, focused);
}

/* ---------------- VALUES ---------------- */

static bool on_values(const ui_ctx_t *c) { return c->ui->map_page == 0; }

static void pic_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)focused; (void)latched;
    if (r.h < 12) return;
    if (ui_colour(g)) ui_fill(g, r.x, r.y, r.w, r.h, UC_PANEL); else u8g2_DrawFrame(g, r.x, r.y, r.w, r.h);
    ui_ink(g, ui_family(type_of(c)));
    u8g2_SetClipWindow(g, r.x, r.y, r.x + r.w, r.y + r.h);
    ui_module_picture(g, st, r, slot_of(c));
    u8g2_SetMaxClipWindow(g);
    ui_ink(g, UC_TEXT);
}

static bool value_enabled(const ui_ctx_t *c) { return on_values(c) && c->arg < rack_mparam_count(type_of(c)); }

// A modulator drives this value: its target is this module and its parameter has the value's name (the rack names a target's parameters
// with the same short names as the module's own values).
static bool modulated(const ui_ctx_t *c, int i) {
    const rack_t *r = c->rack;
    const char *label = rack_mparam_label(type_of(c), i);
    for (int k = 0; k < r->count; k++) {
        const rack_slot_t *m = &r->slot[k];
        if (k == node_of(c) || !m->tgt_id || m->tgt_id != slot_of(c)->id || !rack_can_target((module_type_t)m->type)) continue;
        const char *p = rack_param_name(type_of(c), m->tgt_param);
        if (p && label && !strcmp(p, label)) return true;
    }
    return false;
}

static void value_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    const rack_slot_t *s = slot_of(c);
    char val[16];
    rack_mparam_format(s, c->arg, val, sizeof val);
    const float frac = rack_mparam_is_continuous(type_of(c), c->arg) ? rack_mparam_norm(s, c->arg) : -1.0f;
    uw_value_row(g, st, r, rack_mparam_label(type_of(c), c->arg), val, frac, ui_family(type_of(c)),
                 latched ? GUI_LATCHED : focused ? GUI_FOCUSED : GUI_PLAIN, modulated(c, c->arg));
}

static bool value_adjust(const ui_ctx_t *c, int dir) { return rack_mparam_adjust(slot_of(c), c->arg, dir); }

/* ---------------- PORTS ---------------- */

typedef enum { PORT_IN, PORT_OUT, PORT_MOD_IN, PORT_MOD_OUT, PORT_ADD } port_kind_t;
typedef struct { port_kind_t kind; int other, mod_slot; char left[16], right[40]; ui_colour_t col; } port_t;

static int ports(const ui_ctx_t *c, port_t *p) {
    const nav_graph_t *gr = graph(c);
    const rack_t *r = c->rack;
    const int node = node_of(c);
    const module_type_t t = type_of(c);
    int n = 0;
    char who[8], depth[16];
    if (rack_slot_is_audio(r, node)) {
        bool fed = false;
        for (int e = 0; e < gr->n_edges && n < PORTS - 1; e++) {
            if (gr->edge[e].kind != EDGE_AUDIO || gr->edge[e].to != node) continue;
            node_name(c, gr->edge[e].from, who, sizeof who);
            p[n] = (port_t){PORT_IN, gr->edge[e].from, -1, "in  audio", "", UC_TEXT};
            snprintf(p[n].right, sizeof p[n].right, "<- %s", who);
            n++; fed = true;
        }
        if (!fed) { p[n] = (port_t){PORT_IN, -1, -1, "in  audio", "--", UC_DIM}; n++; }
        for (int e = 0; e < gr->n_edges && n < PORTS - 1; e++) {
            if (gr->edge[e].kind != EDGE_AUDIO || gr->edge[e].from != node) continue;
            node_name(c, gr->edge[e].to, who, sizeof who);
            p[n] = (port_t){PORT_OUT, gr->edge[e].to, -1, "out", "", UC_TEXT};
            snprintf(p[n].right, sizeof p[n].right, "-> %s", who);
            n++;
        }
    }
    for (int e = 0; e < gr->n_edges && n < PORTS - 1; e++) {                       // the modulations coming in
        if (gr->edge[e].kind != EDGE_MOD || gr->edge[e].to != node) continue;
        const int m = gr->edge[e].from;
        const rack_slot_t *ms = &r->slot[m];
        node_name(c, m, who, sizeof who);
        p[n] = (port_t){PORT_MOD_IN, m, ms->type == MOD_MSEQ ? -1 : m, "", "", UC_MOD};
        snprintf(p[n].left, sizeof p[n].left, "mod %s", ms->type == MOD_MSEQ ? "lane" : rack_param_name(t, ms->tgt_param));
        depth[0] = 0;
        if (ms->type != MOD_MSEQ && rack_depth_index((module_type_t)ms->type) >= 0) rack_depth_format(r, m, depth, sizeof depth);
        snprintf(p[n].right, sizeof p[n].right, "<- %s %s", who, depth);
        n++;
    }
    for (int e = 0; e < gr->n_edges && n < PORTS - 1; e++) {                       // this module's own modulation cables
        if (gr->edge[e].kind != EDGE_MOD || gr->edge[e].from != node) continue;
        const int ti = gr->edge[e].to;
        node_name(c, ti, who, sizeof who);
        const rack_slot_t *s = slot_of(c);
        p[n] = (port_t){PORT_MOD_OUT, ti, s->type == MOD_MSEQ ? -1 : node, "mod out", "", UC_MOD};
        depth[0] = 0;
        if (s->type != MOD_MSEQ && rack_depth_index((module_type_t)s->type) >= 0) rack_depth_format(r, node, depth, sizeof depth);
        snprintf(p[n].right, sizeof p[n].right, "-> %s %s %s", who, s->type == MOD_MSEQ ? "" : rack_param_name((module_type_t)r->slot[ti].type, s->tgt_param), depth);
        n++;
    }
    bool any_source = false;                                                       // the PICK list has something to offer
    for (int k = 0; k < r->count; k++) any_source |= k != node && rack_can_target((module_type_t)r->slot[k].type);
    if (rack_param_count(t) > 0 && any_source) { p[n] = (port_t){PORT_ADD, -1, -1, "+ modulation", "pick", UC_DIM}; n++; }
    return n;
}

static bool port_enabled(const ui_ctx_t *c) { port_t p[PORTS]; return !on_values(c) && c->arg < ports(c, p); }

static void port_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)latched;
    port_t p[PORTS];
    if (c->arg >= ports(c, p)) return;
    uw_port_row(g, st, r, p[c->arg].left, p[c->arg].right, p[c->arg].col, focused);
}

// The first parameter of type t a modulator can drive, from `from` on, stepping by dir (wraps); -1: none.
static int next_param(module_type_t t, int from, int dir) {
    const int n = rack_param_count(t);
    for (int k = 0; k < n; k++) {
        const int p = ((from + dir * k) % n + n) % n;
        if (rack_depth_unit(t, p)) return p;
    }
    return -1;
}

static void port_activate(const ui_ctx_t *c) {
    port_t p[PORTS];
    if (c->arg >= ports(c, p)) return;
    const port_t *q = &p[c->arg];
    if (q->kind == PORT_ADD) {                                                     // the PICK list
        const int first = next_param(type_of(c), 0, 1);
        c->ui->pick_param = (uint8_t)(first < 0 ? 0 : first);
        c->ui->map_pick = true;
        c->ui->row = K_S0 + 1;
        c->ui->latched = false;
        return;
    }
    if (q->other < 0 || q->other >= c->rack->count) return;                        // (the MIX and the OUT have no page)
    c->ui->map_at = (uint8_t)(q->other + 1);                                       // follow the cable: the other end's ports
    c->ui->row = E_P0 + 1;
    c->ui->latched = false;
}

static bool port_adjust(const ui_ctx_t *c, int dir) {
    port_t p[PORTS];
    if (c->arg >= ports(c, p) || p[c->arg].mod_slot < 0) return false;
    return rack_depth_adjust(c->rack, p[c->arg].mod_slot, dir);
}

static bool node_back(const ui_ctx_t *c) {
    c->ui->row = c->ui->map_at;                                                    // the map, its cursor on this node (element node + 1)
    c->ui->map_at = 0;
    c->ui->latched = false;
    return true;
}

/* ---------------- the node page's table: all placed by data ---------------- */

#define TITLE_BOX {PL_IN, 0, {0, UN_PX, 0}, {0, UN_PX, 0}, {100, UN_PCT, 0}, {1, UN_ROW, 3}}
#define TABS_BOX  {PL_BELOW, 0, {0, UN_PX, 0}, {1, UN_PX, 0}, {0, UN_SAME, 0}, {1, UN_ROW, 1}}
#define PIC_BOX   {PL_BELOW, 0, {0, UN_PX, 0}, {2, UN_PX, 0}, {0, UN_SAME, 0}, {30, UN_PCT, 0}}
#define ROW_FIRST {PL_BELOW, 0, {0, UN_PX, 0}, {2, UN_PX, 0}, {0, UN_SAME, 0}, {1, UN_ROW, 2}}          // under the picture
#define ROW_NEXT  {PL_BELOW, 0, {0, UN_PX, 0}, {1, UN_PX, 0}, {0, UN_SAME, 0}, {0, UN_SAME, 0}}
#define PORT_FIRST {PL_BELOW, UI_REF(E_TABS), {0, UN_PX, 0}, {3, UN_PX, 0}, {0, UN_SAME, 0}, {1, UN_ROW, 1}}   // under the tabs
#define LIST (EF_SCROLL | EF_HIDE_WHEN_DISABLED)

#define VAL(i, box) [E_V0 + i] = {"", EL_VALUE, (uint8_t)(2 + i), 0, 1, NULL, value_adjust, NULL, value_draw, value_enabled, NULL, i, LIST, box}
#define PRT(i, box) [E_P0 + i] = {"", EL_DIRECT, (uint8_t)(2 + i), 0, 1, NULL, port_adjust, port_activate, port_draw, port_enabled, NULL, i, LIST, box}
#define VAL4(i) VAL(i, ROW_NEXT), VAL(i + 1, ROW_NEXT), VAL(i + 2, ROW_NEXT), VAL(i + 3, ROW_NEXT)

static const el_def_t node_el[E_COUNT] = {
    [E_TITLE] = {"", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, title_draw, NULL, NULL, 0, EF_STATIC, TITLE_BOX},
    [E_TABS]  = {"", EL_DIRECT, 1, 0, 1, NULL, tabs_adjust, NULL, tabs_draw, NULL, NULL, 0, 0, TABS_BOX},
    [E_PIC]   = {"", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, pic_draw, on_values, NULL, 0, EF_STATIC | EF_HIDE_WHEN_DISABLED, PIC_BOX},
    VAL(0, ROW_FIRST), VAL(1, ROW_NEXT), VAL(2, ROW_NEXT), VAL(3, ROW_NEXT), VAL4(4), VAL4(8), VAL4(12), VAL4(16),
    PRT(0, PORT_FIRST), PRT(1, ROW_NEXT), PRT(2, ROW_NEXT), PRT(3, ROW_NEXT), PRT(4, ROW_NEXT),
    PRT(5, ROW_NEXT), PRT(6, ROW_NEXT), PRT(7, ROW_NEXT), PRT(8, ROW_NEXT), PRT(9, ROW_NEXT),
};
_Static_assert(VALUES == 20 && PORTS == 10, "the tables below list them");

const screen_def_t scr_node_screen = {node_el, E_COUNT, true, true, NULL, node_back, NULL, NULL, NULL};

/* ---------------- PICK ---------------- */

// The rack's modulation sources for this module (every module that can have a target, but itself), in slot order.
static int sources(const ui_ctx_t *c, int *out) {
    int n = 0;
    for (int k = 0; k < c->rack->count; k++) if (k != node_of(c) && rack_can_target((module_type_t)c->rack->slot[k].type)) out[n++] = k;
    return n;
}

static void pick_title_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)st; (void)focused; (void)latched;
    char t[24], name[8];
    rack_slot_name(c->rack, node_of(c), name, sizeof name);
    snprintf(t, sizeof t, "PATCH %s", name);
    uw_title(g, r, t, "<- source", UC_MOD);
}

static void to_value(const ui_ctx_t *c, char *out, int n) { snprintf(out, (size_t)n, "%s", rack_param_long(type_of(c), c->ui->pick_param)); }
static bool to_adjust(const ui_ctx_t *c, int dir) {
    const int p = next_param(type_of(c), c->ui->pick_param + dir, dir);
    if (p >= 0) c->ui->pick_param = (uint8_t)p;
    return false;
}
static void to_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    char v[20];
    to_value(c, v, sizeof v);
    uw_value_row(g, st, r, "To", v, -1.0f, UC_MOD, latched ? GUI_LATCHED : focused ? GUI_FOCUSED : GUI_PLAIN, false);
}

// Where a source goes now: "-> FL2 Res", or "free".
static void source_now(const ui_ctx_t *c, int k, char *out, int n) {
    const rack_slot_t *s = &c->rack->slot[k];
    const int ti = s->tgt_id ? rack_find(c->rack, s->tgt_id) : RACK_NONE;
    if (ti == RACK_NONE) { snprintf(out, (size_t)n, "free"); return; }
    char tn[8];
    rack_slot_name(c->rack, ti, tn, sizeof tn);
    snprintf(out, (size_t)n, "-> %s %s", tn, rack_param_name((module_type_t)c->rack->slot[ti].type, s->tgt_param));
}

static void info_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)st; (void)focused; (void)latched;
    int src[SOURCES];
    const int n = sources(c, src), f = c->ui->row - 1 - K_S0;
    char line[40] = "push: connect";
    if (f >= 0 && f < n) {
        char who[8], now[24];
        rack_slot_name(c->rack, src[f], who, sizeof who);
        source_now(c, src[f], now, sizeof now);
        if (strcmp(now, "free")) snprintf(line, sizeof line, "%s leaves %s", who, now + 3);
        else snprintf(line, sizeof line, "%s is free", who);
    }
    uw_text(g, r.x + 3, r.y + r.h - 2, line, UC_DIM);
}

static bool source_enabled(const ui_ctx_t *c) { int s[SOURCES]; return c->arg < sources(c, s); }

static void source_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)latched;
    int src[SOURCES];
    if (c->arg >= sources(c, src)) return;
    const int k = src[c->arg];
    char name[24], now[24], code[8];
    rack_slot_name(c->rack, k, code, sizeof code);
    snprintf(name, sizeof name, "%s %s", code, rack_type_name((module_type_t)c->rack->slot[k].type));
    source_now(c, k, now, sizeof now);
    uw_list_row(g, st, r, name, now, ui_family((module_type_t)c->rack->slot[k].type), focused);
}

static void source_activate(const ui_ctx_t *c) {
    int src[SOURCES];
    if (c->arg >= sources(c, src)) return;
    if (rack_set_target(c->rack, src[c->arg], node_of(c), c->ui->pick_param)) c->ui->rack_dirty = true;   // applied when the menu closes
    c->ui->map_pick = false;
    c->ui->map_page = 1;                                                           // back on the ports: the new cable is there
    c->ui->row = E_P0 + 1;
    c->ui->latched = false;
}

static bool pick_back(const ui_ctx_t *c) {
    c->ui->map_pick = false;
    c->ui->row = E_P0 + 1;
    c->ui->latched = false;
    return true;
}

#define TO_BOX   {PL_BELOW, 0, {0, UN_PX, 0}, {2, UN_PX, 0}, {0, UN_SAME, 0}, {1, UN_ROW, 2}}
#define INFO_BOX {PL_BELOW, 0, {0, UN_PX, 0}, {1, UN_PX, 0}, {0, UN_SAME, 0}, {1, UN_ROW, 1}}
#define SRC_FIRST {PL_BELOW, 0, {0, UN_PX, 0}, {1, UN_PX, 0}, {0, UN_SAME, 0}, {1, UN_ROW, 3}}
#define SRC(i, box) [K_S0 + i] = {"", EL_BUTTON, (uint8_t)(3 + i), 0, 1, NULL, NULL, source_activate, source_draw, source_enabled, NULL, i, LIST, box}
#define SRC4(i) SRC(i, ROW_NEXT), SRC(i + 1, ROW_NEXT), SRC(i + 2, ROW_NEXT), SRC(i + 3, ROW_NEXT)

static const el_def_t pick_el[K_COUNT] = {
    [K_TITLE] = {"", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, pick_title_draw, NULL, NULL, 0, EF_STATIC, TITLE_BOX},
    [K_TO]    = {"", EL_VALUE, 1, 0, 1, to_value, to_adjust, NULL, to_draw, NULL, NULL, 0, 0, TO_BOX},
    [K_INFO]  = {"", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, info_draw, NULL, NULL, 0, EF_STATIC, INFO_BOX},
    SRC(0, SRC_FIRST), SRC(1, ROW_NEXT), SRC(2, ROW_NEXT), SRC(3, ROW_NEXT), SRC4(4), SRC4(8), SRC4(12),
};

const screen_def_t scr_pick_screen = {pick_el, K_COUNT, true, true, NULL, pick_back, NULL, NULL, NULL};
