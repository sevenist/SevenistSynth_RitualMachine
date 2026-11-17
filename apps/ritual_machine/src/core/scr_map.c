// The RACK tab of the menu: the rack as a map of nodes (apps/ritual_machine/docs/GUI_NODES.md level L0, the look of apps/gui_mockup). It replaced
// the strip of three rows (scr_rack.c, which still shows the Pads section's FX chains) on 2026-11-17.
//
//   joystick       follows the cables from node to node (nav_graph.h: down / up a column, left / right to the next column)
//   push           the node's menu (the rack's own, scr_rack.c): Edit (the node's page: scr_node.c), Type, Insert (before this module), Tgt, Prm,
//                  Dpth (a modulator), Row, Para, PEnv, Delete; on the MIX its levels and pans; on a "+" (an empty place at the end of a branch,
//                  of row M, or among the modulators) Type and Insert. Back closes it; Back never deletes (ADR-046)
//   + nodes        where a module can be added, while the rack has room; a push there offers the types that fit
// The edits are the rack's (rack_dirty: applied when the menu closes, the header reads "RACK*"); the map follows them at once.
//
// A declarative screen (see ui_screen.h) with its own joystick move (`move`):
//   - the nodes are elements 0..NAV_NODES-1, element i = node i of nav_graph.h (slot i, the MIX, the OUT, the four "+"), placed by layout() where
//     the graph's layout puts them; the strip under the map is a static widget; the menu's items follow, stacked in a box over the map;
//   - a node is a tile (core/ui_widgets.h): its family's band, its name, its main value as a bar; the cursor outlines it;
//   - audio cables are solid, modulation cables dashed (dotted in one colour) and run along the gaps between the tiles;
//   - the strip names the selected node and its ports, so a cable is read without opening the module.
// Two sizes: a tall screen (an area of 120 px or more: 128 x 160) has 28 x 14 tiles in 32 x 16 cells and a three-line strip with the ports as
// chips; a short one (128 x 128) 28 x 12 tiles in 32 x 14 cells and a two-line strip. A map deeper than the room shows the rows around the cursor.
#include "core/ui_screen.h"
#include "core/ui_widgets.h"
#include "core/nav_graph.h"
#include <stdio.h>
#include <string.h>

#define MAP_EDIT 100                     // the menu's first item, the map's own: the node's page
enum { E_INFO = NAV_NODES, E_M0, E_COUNT = E_M0 + 1 + (RM_END - RM_TYPE) };

#define TILE_PAD 2                       // the gap left of a tile in its column
#define MENU_W   96
typedef struct { int cell_h, tile_h, strip_h; bool tall; } metrics_t;
static metrics_t metrics(gui_rect_t area) {
    return area.h >= 120 ? (metrics_t){16, 14, 32, true} : (metrics_t){14, 12, 21, false};
}
static gui_rect_t g_menu_box;            // the menu's box (layout -> strip_draw, which paints it before the items)

// The graph of the rack on show. It is rebuilt at every use (16 modules: nothing to keep in step with the rack).
static const nav_graph_t *graph(const ui_ctx_t *c) {
    static nav_graph_t g;
    nav_build(&g, c->rack);
    return &g;
}

static int item_arg(int e) { return e == E_M0 ? MAP_EDIT : RM_TYPE + (e - E_M0 - 1); }

/* ---------------- the nodes ---------------- */

static bool node_shown(const ui_ctx_t *c) { return graph(c)->node[c->arg].kind != NODE_NONE; }
static bool node_enabled(const ui_ctx_t *c) { return !c->ui->rack_menu && node_shown(c); }     // (behind the menu they are drawn, not visited)

static bool item_enabled(const ui_ctx_t *c);
static int first_item(const ui_ctx_t *c) {
    for (int i = E_M0; i < E_COUNT; i++) { ui_ctx_t x = *c; x.arg = item_arg(i); if (item_enabled(&x)) return i; }
    return -1;
}

// The type an empty place offers first: the one chosen last if it fits there, else the first that does (a modulator among the modulators,
// a sound or an effect at the end of a chain).
static void fit_type(const ui_ctx_t *c, bool modulator) {
    const int pos = scr_rack_insert_pos(c);
    for (int k = 0; k < MOD_TYPE_COUNT; k++) {
        const int t = (c->ui->rack_type + k) % MOD_TYPE_COUNT;
        if (rack_is_modulator((module_type_t)t) == modulator && rack_can_insert(c->rack, pos, c->ui->rack_row, (module_type_t)t)) { c->ui->rack_type = t; return; }
    }
}

// A push on a node: its menu (the rack's), on the cell it is.
static void node_activate(const ui_ctx_t *c) {
    const nav_node_t *n = &graph(c)->node[c->arg];
    synth_ui_t *ui = c->ui;
    if (n->kind == NODE_MODULE) scr_rack_select(ui, c->rack, c->rack->slot[n->slot].row, n->slot);
    else if (n->kind == NODE_MIX) scr_rack_select(ui, c->rack, ROW_M, RACK_SEL_MIX);
    else if (n->kind == NODE_PLUS) scr_rack_select(ui, c->rack, n->row, RACK_SEL_PLUS);
    else return;                                               // (the OUT: nothing to do there)
    ui->rack_menu = true;
    ui->map_menu = (uint8_t)(c->arg + 1);
    if (n->kind == NODE_PLUS) fit_type(c, c->arg == NAV_PLUS_MOD);
    const int first = first_item(c);
    if (first < 0) { scr_rack_menu_close(c); return; }
    ui->row = first + 1;
    ui->latched = false;
}

// The joystick: from node to node on the map; in the menu, up and down its items (left / right change a value: there is nowhere to go).
static int node_move(const ui_ctx_t *c, int cur, char dir) {
    if (!c->ui->rack_menu) return nav_move(graph(c), cur, dir);
    if (cur < E_M0) return first_item(c);
    if (dir == 'l' || dir == 'r') return cur;
    const int step = dir == 'd' ? 1 : -1;
    for (int i = cur + step; i >= E_M0 && i < E_COUNT; i += step) {
        ui_ctx_t x = *c;
        x.arg = item_arg(i);
        if (item_enabled(&x)) return i;
    }
    return cur;
}

static bool map_back(const ui_ctx_t *c) {
    if (!c->ui->rack_menu) return false;                       // (then up: the header, then out of the menu: ui_input.c)
    scr_rack_menu_close(c);
    return true;
}

static void node_label(const ui_ctx_t *c, int node, char *out, int n) {
    const nav_node_t *nd = &graph(c)->node[node];
    if (nd->kind == NODE_MIX) snprintf(out, (size_t)n, "MIX");
    else if (nd->kind == NODE_OUT) snprintf(out, (size_t)n, "OUT");
    else if (nd->kind == NODE_PLUS) snprintf(out, (size_t)n, "+");
    else if (nd->kind == NODE_MODULE) rack_slot_name(c->rack, nd->slot, out, n);
    else out[0] = 0;
}

static ui_colour_t node_family(const ui_ctx_t *c, int node) {
    const nav_node_t *nd = &graph(c)->node[node];
    if (nd->kind == NODE_MIX) return UC_UTL;
    if (nd->kind == NODE_OUT) return UC_OUT;
    if (nd->kind == NODE_PLUS) return UC_DIM;
    return ui_family((module_type_t)c->rack->slot[nd->slot].type);
}

// The value a tile shows as its bar: the level of an oscillator, the cutoff of a filter ..., else the module's first continuous one.
static float main_value(const rack_slot_t *s) {
    int i = -1;
    switch (s->type) {
        case MOD_OSC: i = MP_OC_LEVEL; break;
        case MOD_FILTER: i = MP_FL_CUT; break;
        case MOD_SAT: i = MP_SA_DRIVE; break;
        case MOD_LFO: i = MP_LF_RATE; break;
        default:
            for (int k = 0; k < rack_mparam_count((module_type_t)s->type); k++) if (rack_mparam_is_continuous((module_type_t)s->type, k)) { i = k; break; }
            break;
    }
    return i >= 0 && rack_mparam_is_continuous((module_type_t)s->type, i) ? rack_mparam_norm(s, i) : -1.0f;
}

/* ---------------- drawing ---------------- */

static void tile_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)st; (void)latched;
    if (r.w <= 0 || r.h <= 0) return;                           // not in the part of the map on show
    const nav_node_t *n = &graph(c)->node[c->arg];
    char name[8];
    node_label(c, c->arg, name, sizeof name);
    const float bar = n->kind == NODE_MODULE ? main_value(&c->rack->slot[n->slot]) : -1.0f;
    const bool selected = focused || (c->ui->rack_menu && c->ui->map_menu == c->arg + 1);     // (the node the menu is open on)
    uw_tile(g, r, name, node_family(c, c->arg), bar, selected, n->kind == NODE_MODULE && n->col >= 2);
}

// The cables, under the tiles. rect[i] is node i's tile (empty while it is out of view).
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)st;
    const nav_graph_t *gr = graph(c);
    u8g2_SetClipWindow(g, area.x, area.y, area.x + area.w, rect[E_INFO].y);      // not over the strip
    for (int e = 0; e < gr->n_edges; e++) {
        if (gr->edge[e].kind != EDGE_AUDIO) continue;
        const gui_rect_t a = rect[gr->edge[e].from], b = rect[gr->edge[e].to];
        if (a.w <= 0 && b.w <= 0) continue;
        const int ax = a.x + a.w / 2, ay = a.y + a.h;
        if (gr->node[gr->edge[e].from].col == gr->node[gr->edge[e].to].col) {    // down the column
            if (a.w > 0 && b.w > 0 && b.y > ay) uw_line(g, ax, ay, ax, b.y - 1, UW_CABLE_AUDIO);
            else if (a.w > 0 && b.w <= 0) uw_line(g, ax, ay, ax, ay + 2, UW_CABLE_AUDIO);       // goes on below the part on show
        } else if (a.w > 0 && b.w > 0) {                                           // the end of branch 2 into the MIX: down, then across
            const int my = b.y + b.h / 2;
            uw_line(g, ax, ay, ax, my, UW_CABLE_AUDIO);
            uw_line(g, b.x + b.w, my, ax, my, UW_CABLE_AUDIO);
        }
    }
    for (int e = 0; e < gr->n_edges; e++) {                                        // modulation: out of the modulator's left side, along the gap column and the gap row under the target
        if (gr->edge[e].kind != EDGE_MOD) continue;
        const gui_rect_t m = rect[gr->edge[e].from], t = rect[gr->edge[e].to];
        if (m.w <= 0 || t.w <= 0) continue;
        const int gx = m.x - TILE_PAD, gy = t.y + t.h, tx = t.x + t.w - 4;
        uw_line(g, gx, m.y + m.h / 2, m.x - 1, m.y + m.h / 2, UW_CABLE_MOD);
        uw_line(g, gx, m.y + m.h / 2, gx, gy, UW_CABLE_MOD);
        uw_line(g, tx, gy, gx, gy, UW_CABLE_MOD);
    }
    u8g2_SetClipWindow(g, area.x, area.y, area.x + area.w, area.y + area.h);
}

/* ---------------- the strip: what the selected node is and where its ports go ---------------- */

// The node's ports: audio in, audio out, and its first modulation input ("Cut<-LF1"); "" when it has none.
static void ports(const ui_ctx_t *c, int node, char *in, char *out, char *mod, int n) {
    const nav_graph_t *gr = graph(c);
    const rack_t *r = c->rack;
    snprintf(in, (size_t)n, "--"); snprintf(out, (size_t)n, "OUT"); mod[0] = 0;
    const nav_node_t *nd = &gr->node[node];
    if (nd->kind != NODE_MODULE) return;
    const int prev = rack_audio_prev(r, nd->slot), next = rack_audio_next(r, nd->slot);
    if (prev != RACK_NONE) rack_slot_name(r, prev, in, n);
    if (next >= 0) rack_slot_name(r, next, out, n);
    else if (gr->node[NAV_MIX].kind == NODE_MIX && r->slot[nd->slot].row < RACK_BRANCHES) snprintf(out, (size_t)n, "MIX");
    for (int e = 0; e < gr->n_edges; e++) {
        if (gr->edge[e].kind != EDGE_MOD || gr->edge[e].to != node) continue;
        const rack_slot_t *m = &r->slot[gr->edge[e].from];
        char who[8];
        rack_slot_name(r, gr->edge[e].from, who, sizeof who);
        snprintf(mod, (size_t)n, "%s<-%s", m->type == MOD_MSEQ ? "MS" : rack_param_name((module_type_t)r->slot[nd->slot].type, m->tgt_param), who);
        break;
    }
}

static void plus_text(int node, char *l1, char *l2, int n) {
    snprintf(l1, (size_t)n, "+ %s", node == NAV_PLUS1 ? "end of branch 1" : node == NAV_PLUS2 ? "end of branch 2" : node == NAV_PLUSM ? "end of row M" : "a modulator");
    snprintf(l2, (size_t)n, "push: Type, Insert");
}

static void strip_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)focused; (void)latched;
    const synth_ui_t *ui = c->ui;
    const int node = ui->rack_menu ? ui->map_menu - 1 : ui->row - 1;
    const nav_graph_t *gr = graph(c);
    const rack_t *rk = c->rack;
    uw_panel(g, r);
    if (node < 0 || node >= NAV_NODES || gr->node[node].kind == NODE_NONE) {
        uw_text(g, r.x + 3, r.y + 2 + u8g2_GetAscent(g), "push: a node's menu", UC_DIM);
    } else {
        const nav_node_t *nd = &gr->node[node];
        const ui_colour_t fam = node_family(c, node);
        const int rh = gui_row_h(g, st), asc = u8g2_GetAscent(g);
        char l1[40], l2[40] = "", in[16], out[16], mod[16];
        if (nd->kind == NODE_MIX) snprintf(l1, sizeof l1, "MIX of 1 + 2");
        else if (nd->kind == NODE_OUT) snprintf(l1, sizeof l1, "OUT");
        else if (nd->kind == NODE_PLUS) plus_text(node, l1, l2, sizeof l1);
        else rack_describe(rk, nd->slot, l1, sizeof l1);
        ports(c, node, in, out, mod, sizeof in);
        u8g2_SetClipWindow(g, r.x, r.y + 1, r.x + r.w, r.y + r.h);
        const bool tall = r.h >= 30;                                                // (the tall strip: metrics())
        if (ui_colour(g)) ui_fill(g, r.x + 3, r.y + 3, 5, 5, fam);
        else u8g2_DrawBox(g, r.x + 3, r.y + 3, 5, 5);
        uw_text(g, r.x + 11, r.y + 2 + asc, l1, UC_TEXT);
        if (nd->kind == NODE_PLUS) {
            uw_text(g, r.x + 3, r.y + 2 + rh + asc, l2, UC_DIM);
        } else if (!tall) {
            if (nd->kind == NODE_MODULE && nd->col < 2) snprintf(l2, sizeof l2, "in %s  out %s", in, out);
            else if (nd->kind == NODE_MIX) snprintf(l2, sizeof l2, "-> row M");
            uw_text(g, r.x + 3, r.y + 2 + rh + asc, l2, UC_TEXT);
        } else {
            if (nd->kind == NODE_MODULE) {                                         // two of its values ("Cut 2.4k  Res 0.35")
                const rack_slot_t *s = &rk->slot[nd->slot];
                int shown = 0;
                for (int i = 0; i < rack_mparam_count((module_type_t)s->type) && shown < 2; i++) {
                    if (!rack_mparam_is_continuous((module_type_t)s->type, i)) continue;
                    char v[12];
                    rack_mparam_format(s, i, v, sizeof v);
                    const size_t len = strlen(l2);
                    snprintf(l2 + len, sizeof l2 - len, "%s%s %s", shown ? "  " : "", rack_mparam_label((module_type_t)s->type, i), v);
                    shown++;
                }
            } else if (nd->kind == NODE_MIX) snprintf(l2, sizeof l2, "-> row M");
            else snprintf(l2, sizeof l2, "the output");
            uw_text(g, r.x + 3, r.y + 2 + rh + asc, l2, UC_TEXT);
            if (nd->kind == NODE_MODULE) {                                         // the ports as chips
                char t[3][24];
                int k = 0;
                if (nd->col < 2) { snprintf(t[k++], 24, "in<-%s", in); snprintf(t[k++], 24, "out->%s", out); }
                else if (rk->slot[nd->slot].tgt_id) {
                    const int ti = rack_find(rk, rk->slot[nd->slot].tgt_id);
                    char tn[8] = "--";
                    if (ti != RACK_NONE) rack_slot_name(rk, ti, tn, sizeof tn);
                    snprintf(t[k++], 24, "out->%s", tn);
                }
                if (mod[0]) snprintf(t[k++], 24, "%s", mod);
                int x = r.x + 3;
                for (int i = 0; i < k; i++) {
                    const int w = uw_chip_w(g, st, t[i]);
                    if (x + w > r.x + r.w) break;
                    uw_chip(g, st, gui_rect(x, r.y + 2 * rh + 3, w, 8), t[i], i == k - 1 && mod[0] ? UC_MOD : UC_TEXT);
                    x += w + 3;
                }
            }
        }
        u8g2_SetMaxClipWindow(g);
    }
    if (ui->rack_menu && g_menu_box.w > 0) {                                       // the menu's box, under its items (drawn next)
        const gui_rect_t b = g_menu_box;
        if (ui_colour(g)) ui_fill(g, b.x, b.y, b.w, b.h, UC_PANEL);
        else { u8g2_SetDrawColor(g, 0); u8g2_DrawBox(g, b.x, b.y, b.w, b.h); u8g2_SetDrawColor(g, 1); }
        ui_ink(g, UC_CURSOR);
        u8g2_DrawFrame(g, b.x, b.y, b.w, b.h);
        ui_ink(g, UC_TEXT);
    }
}

/* ---------------- the menu's items: the rack's (scr_rack.c), run by number, and the map's Edit ---------------- */

static bool edit_enabled(const ui_ctx_t *c) { return c->ui->rack_menu && scr_rack_selected_slot(c) >= 0 && !c->rack->fx; }
static void edit_activate(const ui_ctx_t *c) {                                     // the node's page (scr_node.c), on its values
    const int slot = scr_rack_selected_slot(c);
    scr_rack_menu_close(c);
    if (slot < 0) return;
    c->ui->map_at = (uint8_t)(slot + 1);
    c->ui->map_page = 0;
    c->ui->map_pick = false;
    c->ui->row = 4;                                                                // the node page's first value row (scr_node.c: element 3)
}

static bool item_enabled(const ui_ctx_t *c) { return c->arg == MAP_EDIT ? edit_enabled(c) : scr_rack_item_enabled(c); }
static void item_activate(const ui_ctx_t *c) { if (c->arg == MAP_EDIT) edit_activate(c); else scr_rack_item_activate(c); }

static void item_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    const char *label = c->arg == MAP_EDIT ? "Edit" : scr_rack_item_label(c->arg);
    const bool button = c->arg == MAP_EDIT || scr_rack_item_kind(c->arg) == EL_BUTTON;
    char val[24] = "";
    if (button) snprintf(val, sizeof val, ">");
    else scr_rack_item_value(c, val, sizeof val);
    uw_value_row(g, st, r, label, val, -1.0f, UC_MOD, latched ? GUI_LATCHED : focused ? GUI_FOCUSED : GUI_PLAIN, false);
}

/* ---------------- layout ---------------- */

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    const metrics_t m = metrics(area);
    rect[E_INFO] = gui_rect(area.x, area.y + area.h - m.strip_h, area.w, m.strip_h);
    const int room = rect[E_INFO].y - area.y;                                      // the map's height
    const int vis = room / m.cell_h > 0 ? room / m.cell_h : 1;
    const nav_graph_t *gr = graph(c);
    const int focus = c->ui->rack_menu ? c->ui->map_menu - 1 : c->ui->row - 1;
    int scroll = 0;
    if (focus >= 0 && focus < NAV_NODES && gr->node[focus].kind != NODE_NONE && gr->node[focus].depth >= vis) scroll = gr->node[focus].depth - vis + 1;
    const int pitch = area.w / NAV_COLS;
    for (int i = 0; i < NAV_NODES; i++) {
        const nav_node_t *n = &gr->node[i];
        const int row = n->depth - scroll;
        if (n->kind == NODE_NONE || row < 0 || row >= vis) { rect[i] = gui_rect(0, 0, 0, 0); continue; }
        rect[i] = gui_rect(area.x + n->col * pitch + TILE_PAD, area.y + row * m.cell_h + 2, pitch - 2 * TILE_PAD, m.tile_h);
    }
    // the menu: its items that apply, stacked in a box over the map (a list: it scrolls on a short screen)
    const int h = gui_row_h(g, st) + 2, x = area.x + (area.w - MENU_W) / 2;
    int y = area.y + 4, n = 0;
    for (int i = E_M0; i < E_COUNT; i++) {
        ui_ctx_t ix = *c;
        ix.arg = item_arg(i);
        if (c->ui->rack_menu && item_enabled(&ix)) { rect[i] = gui_rect(x, y, MENU_W, h); y += h + 1; n++; }
        else rect[i] = gui_rect(0, 0, 0, 0);
    }
    const int bh = n * (h + 1) + 5;
    g_menu_box = n ? gui_rect(x - 3, area.y + 1, MENU_W + 6, bh < area.h - 1 ? bh : area.h - 1) : gui_rect(0, 0, 0, 0);
}

#define NODE(i) [i] = {"", EL_BUTTON, 0, 0, 1, NULL, NULL, node_activate, tile_draw, node_enabled, NULL, i, 0, {0}}
#define N4(i) NODE(i), NODE(i + 1), NODE(i + 2), NODE(i + 3)
#define MENU_ITEM(e, kind, arg) [e] = {"", kind, 0, 0, 1, scr_rack_item_value, scr_rack_item_adjust, item_activate, item_draw, item_enabled, NULL, arg, \
                                       EF_HIDE_WHEN_DISABLED | EF_SCROLL, {0}}
static const el_def_t elements[E_COUNT] = {
    N4(0), N4(4), N4(8), N4(12), NODE(NAV_MIX), NODE(NAV_OUT), NODE(NAV_PLUS1), NODE(NAV_PLUS2), NODE(NAV_PLUSM), NODE(NAV_PLUS_MOD),
    [E_INFO] = {"", EL_VALUE, 0, 0, 1, NULL, NULL, NULL, strip_draw, NULL, NULL, 0, EF_STATIC, {0}},
    MENU_ITEM(E_M0, EL_BUTTON, MAP_EDIT),
    MENU_ITEM(E_M0 + 1, EL_VALUE, RM_TYPE),   MENU_ITEM(E_M0 + 2, EL_BUTTON, RM_INSERT), MENU_ITEM(E_M0 + 3, EL_VALUE, RM_TGT),
    MENU_ITEM(E_M0 + 4, EL_VALUE, RM_PRM),    MENU_ITEM(E_M0 + 5, EL_VALUE, RM_DPTH),    MENU_ITEM(E_M0 + 6, EL_VALUE, RM_ROW),
    MENU_ITEM(E_M0 + 7, EL_VALUE, RM_PARA),   MENU_ITEM(E_M0 + 8, EL_VALUE, RM_PENV),    MENU_ITEM(E_M0 + 9, EL_VALUE, RM_LVL1),
    MENU_ITEM(E_M0 + 10, EL_VALUE, RM_PAN1),  MENU_ITEM(E_M0 + 11, EL_VALUE, RM_LVL2),   MENU_ITEM(E_M0 + 12, EL_VALUE, RM_PAN2),
    MENU_ITEM(E_M0 + 13, EL_BUTTON, RM_DELETE),
};
_Static_assert(RACK_MAX == 16 && NAV_NODES == RACK_MAX + 6, "N4 x 4 covers the module nodes, then the MIX, the OUT and the four +");
_Static_assert(E_COUNT == E_M0 + 14 && RM_DELETE - RM_TYPE == 12, "the menu's items: Edit, then the rack's Type .. Delete");

const screen_def_t scr_map_screen = {elements, E_COUNT, false, false, layout, map_back, NULL, draw_extra, node_move};
