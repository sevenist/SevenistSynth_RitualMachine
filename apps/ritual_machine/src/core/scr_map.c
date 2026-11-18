// The RACK tab of the menu: the rack as a map of nodes (apps/ritual_machine/docs/GUI_NODES.md level L0, the look of apps/gui_mockup). It replaced
// the strip of three rows (scr_rack.c, which still shows the Pads section's FX chains) on 2026-11-17.
//
//   joystick       to the nearest node on that side of the screen (nav_graph.h: nav_move)
//   push           the node's menu (the rack's own, scr_rack.c): Edit (the node's page: scr_node.c), Type, Insert (before this module), Tgt, Prm,
//                  Dpth (a modulator), Row, Para, PEnv, Delete; on the MIX its levels and pans; on a "+" (an empty place at the end of a branch,
//                  of row M, or among the modulators) Type and Insert. Back closes it; Back never deletes (ADR-046)
//   + nodes        where a module can be added, while the rack has room; a push there offers the types that fit
// The edits are the rack's (rack_dirty: applied when the menu closes, the header reads "RACK*"); the map follows them at once.
//
// A declarative screen (see ui_screen.h) with its own joystick move (`move`):
//   - the nodes are elements 0..NAV_NODES-1, element i = node i of nav_graph.h (slot i, the MIX, the OUT, the four "+"), placed by layout() where
//     they float (nav_place: pulled by their cables, never overlapping); the strip under the map is a static widget; the menu's items follow,
//     stacked in a box over the map;
//   - a node is a tile (core/ui_widgets.h): its family's band, its name, its main value as a bar; the cursor outlines it;
//   - audio cables are solid, modulation cables dashed (dotted in one colour), straight runs with elbows in the gaps between the tiles;
//   - the strip names the selected node and its ports, so a cable is read without opening the module.
// Two sizes: a tall screen (an area of 120 px or more: 128 x 160) has 28 x 14 tiles 4 px apart and a three-line strip with the ports as chips;
// a short one (128 x 128) 28 x 12 tiles 4 x 3 px apart and a two-line strip. A map taller than the room shows the part around the cursor.
// Zoom (ACT_ZOOM, ui->map_zoom; the user's idea, 2026-11-18): 0 the tiles; 1 the same map three times as wide (84 px tiles, 12 px apart:
// the room for the cables), each tile its name, its type and its values as bars, the map scrolling sideways to keep the cursor in view;
// -1 and -2 the graph zoomed out: small rounded nodes, a module's colour and number (no "+"), which spawn where zoom 0's tiles were and
// float into a graph pulled by their cables (core/nav_force.h: audio, FM and pitch strongly, the other modulations weakly), the screen
// drawing each step (synth_ui_map_animating); straight cables from centre to centre. There a push on a module opens the graph's own menu:
// Connect (a modulator or an oscillator: then a push on the target, then its parameter and depth, the rack's Prm and Dpth; Back or a zoom
// cancels, a popup says so), Disconnect all (its modulation cables, both ways), Delete.
#include "core/ui_screen.h"
#include "core/ui_widgets.h"
#include "core/nav_graph.h"
#include "core/nav_force.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAP_EDIT 100                     // the menu's first item, the map's own: the node's page
#define MAP_CONNECT 101                  // the graph's menu (zoomed out): Connect, Disconnect all, Delete
#define MAP_UNLINK  102
#define MAP_DELETE  103
enum { E_INFO = NAV_NODES, E_M0, E_G0 = E_M0 + 1 + (RM_END - RM_TYPE), E_COUNT = E_G0 + 3 };
enum { MAP_ACT_NONE, MAP_ACT_MENU, MAP_ACT_PICK, MAP_ACT_PARAM };      // ui->map_act

#define TILE_PAD 2                       // the margin left and right of the map (the cursor's outline is 2 px outside a tile)
#define MENU_W   96
#define ZOOM_MIN (-2)
#define ZOOM_MAX 1
typedef struct { int tile_w, tile_h, gap_x, gap_y, strip_h; bool tall; } metrics_t;
static metrics_t metrics(gui_rect_t area, int zoom) {
    const bool tall = area.h >= 120;
    if (zoom > 0) return tall ? (metrics_t){84, 30, 12, 10, 32, true} : (metrics_t){84, 23, 12, 8, 21, false};     // (3 x the pitch across)
    if (zoom == -1) return (metrics_t){16, 11, 0, 0, tall ? 32 : 21, tall};                                         // (a node; no grid)
    if (zoom < -1) return (metrics_t){12, 7, 0, 0, tall ? 32 : 21, tall};
    return tall ? (metrics_t){28, 14, 4, 4, 32, true} : (metrics_t){28, 12, 4, 3, 21, false};
}
static gui_rect_t g_menu_box;            // the menu's box (layout -> strip_draw, which paints it before the items)
static metrics_t g_m = {28, 14, 4, 4, 32, true}, g_m0 = {28, 14, 4, 4, 32, true};   // the sizes in use and those of zoom 0 (layout -> graph, draw_extra)
static int g_zoom, g_map_w = 128 - 2 * TILE_PAD, g_map_x, g_map_y, g_scroll, g_scroll_x;
static nav_force_t g_force;              // zoomed out: where the nodes float (layout -> graph)

static bool on_graph(const nav_node_t *n) { return n->kind == NODE_MODULE || n->kind == NODE_MIX || n->kind == NODE_OUT; }   // (zoomed out)

// The graph of the rack on show, its nodes floating (nav_place). The graph is rebuilt at every use (16 modules: cheap, and nothing to keep
// in step with the rack); its layout is only worked out again when the graph or the sizes changed. Zoom 1 is zoom 0's layout spread out:
// three times across (the pitch of 84 px tiles 12 px apart), each row at its own height. Zoomed out: the nodes where the force graph has
// them (a node's top left from its centre), the "+" off the map.
static bool g_moved;                     // (the last placed() worked the layout out again)
static const nav_graph_t *placed_graph(const ui_ctx_t *c) {
    static nav_graph_t built, placed;
    static nav_metrics_t at;
    static bool have;
    nav_graph_t fresh;
    nav_build(&fresh, c->rack);
    const nav_metrics_t m = {g_map_w, g_m0.tile_w, g_m0.tile_h, g_m0.gap_x, g_m0.gap_y};
    g_moved = !have || memcmp(&fresh, &built, sizeof fresh) != 0 || memcmp(&m, &at, sizeof m) != 0;
    if (g_moved) {
        built = fresh; placed = fresh; at = m; have = true;
        nav_place(&placed, &m);
    }
    return &placed;
}

static const nav_graph_t *graph(const ui_ctx_t *c) {
    static nav_graph_t wide, floating;
    static metrics_t at_wide;
    const nav_graph_t *p = placed_graph(c);
    const bool moved = g_moved;
    if (g_zoom == 0) return p;
    if (g_zoom < 0) {
        floating = *p;
        floating.height = 0;
        for (int i = 0; i < NAV_NODES; i++) {
            nav_node_t *n = &floating.node[i];
            if (!on_graph(n) || !g_force.on[i]) { n->kind = NODE_NONE; continue; }
            n->x = (int16_t)(g_force.x[i] - g_force.hw + 0.5f);
            n->y = (int16_t)(g_force.y[i] - g_force.hh + 0.5f);
        }
        return &floating;
    }
    if (moved || memcmp(&g_m, &at_wide, sizeof g_m) != 0) {
        const int across = (g_m.tile_w + g_m.gap_x) / (g_m0.tile_w + g_m0.gap_x), down0 = g_m0.tile_h + g_m0.gap_y, down = g_m.tile_h + g_m.gap_y;
        wide = *p;
        at_wide = g_m;
        wide.height = 0;
        for (int i = 0; i < NAV_NODES; i++) {
            if (wide.node[i].kind == NODE_NONE) continue;
            wide.node[i].x = (int16_t)(p->node[i].x * across);
            wide.node[i].y = (int16_t)((p->node[i].y + down0 / 2) / down0 * down);
            if (wide.node[i].y + g_m.tile_h > wide.height) wide.height = wide.node[i].y + g_m.tile_h;
        }
    }
    return &wide;
}

// A Connect choosing its target stops (Back, a zoom); the popup says so.
static void pick_cancel(synth_ui_t *ui) {
    ui->map_act = MAP_ACT_NONE;
    ui->map_src = 0;
    snprintf(ui->rack_msg, sizeof ui->rack_msg, "Connect cancelled");
}

bool synth_ui_map_zoom(synth_ui_t *ui, int steps) {
    if (!ui->in_rack || (ui->menu_tab != TAB_RACK && ui->menu_tab != TAB_MAP) || ui->map_at || ui->rack_menu) return false;   // (the map on show)
    int z = ui->map_zoom + steps;
    z = z < ZOOM_MIN ? ZOOM_MIN : z > ZOOM_MAX ? ZOOM_MAX : z;
    if (z == ui->map_zoom) return false;
    if (ui->map_act == MAP_ACT_PICK) pick_cancel(ui);
    ui->map_zoom = (int8_t)z;
    return true;
}

/* ---------------- zoomed out: the force graph (core/nav_force.h) ---------------- */

static int g_force_zoom;                 // the zoom the graph was set going for; 0: none (the next zoom out spawns it again)
static uint8_t g_force_key[NAV_NODES];   // what each of its nodes is: a module's number, KEY_MIX, KEY_OUT (a slot's index moves on a delete)
static uint32_t g_force_ms;              // the clock at its last step
#define KEY_MIX 254
#define KEY_OUT 255
#define STEP_MS 16                       // a step every 16 ms (about 60 a second, whatever the frame rate)

// A cable's length at rest: 24 px at -1, 14 at -2, shorter when the room is crowded (the room each node has, three quarters of its side);
// the push between nodes goes with its square.
static float rest_of(int zoom, float w, float h, const nav_graph_t *p) {
    int n = 0;
    for (int i = 0; i < NAV_NODES; i++) n += on_graph(&p->node[i]);
    const float most = zoom < -1 ? 14.0f : 24.0f, fit = n ? 0.75f * sqrtf(w * h / (float)n) : most;
    return fit < most ? fit : most;
}
static float push_of(float rest) { return 0.26f * rest * rest; }
static uint8_t key_of(const nav_graph_t *g, const rack_t *r, int i) {
    const nav_node_t *n = &g->node[i];
    return n->kind == NODE_MODULE ? r->slot[n->slot].id : n->kind == NODE_MIX ? KEY_MIX : n->kind == NODE_OUT ? KEY_OUT : 0;
}

// The cables as springs: audio, FM (an oscillator modulating) and a pitch modulation pull strongly, any other modulation weakly.
static void force_springs(const nav_graph_t *g, const rack_t *r) {
    g_force.n_springs = 0;
    for (int e = 0; e < g->n_edges; e++) {
        const nav_edge_t *ed = &g->edge[e];
        float k = NAV_STRONG;
        if (ed->kind == EDGE_MOD && ed->from < RACK_MAX && ed->to < RACK_MAX) {
            const rack_slot_t *s = &r->slot[ed->from];
            const depth_unit_t *u = s->type == MOD_MSEQ ? NULL : rack_depth_unit((module_type_t)r->slot[ed->to].type, s->tgt_param);
            k = s->type == MOD_OSC || (u && !strcmp(u->unit, "st")) ? NAV_STRONG : NAV_WEAK;
        }
        nav_force_spring(&g_force, ed->from, ed->to, k);
    }
}

// Before each frame zoomed out: the graph set going (each node where its tile was at zoom 0: `scroll0`, that map's scroll), scaled between
// -1 and -2, kept in step with the rack (a node keeps its place by what it is; one joining starts beside a node it is cabled to), then the
// steps the time since the last frame asks for (no clock, as in the tests: one a frame).
static void force_update(const ui_ctx_t *c, int room, int scroll0) {
    if (g_zoom >= 0) { g_force_zoom = 0; return; }
    const nav_graph_t *p = placed_graph(c);
    const rack_t *r = c->rack;
    const float w = (float)g_map_w, h = (float)room;
    if (!g_force_zoom || g_force.w != w || g_force.h != h) {
        const float rest = rest_of(g_zoom, w, h, p);
        nav_force_init(&g_force, w, h, (float)g_m.tile_w / 2, (float)g_m.tile_h / 2, rest, push_of(rest));
        const float fit = p->height > room ? (float)room / (float)p->height : 1.0f;    // (a map taller than the screen: all of it, squeezed)
        for (int i = 0; i < NAV_NODES; i++) {
            g_force_key[i] = key_of(p, r, i);
            const float y = fit < 1 ? (float)p->node[i].y * fit : (float)(p->node[i].y - scroll0);
            if (on_graph(&p->node[i])) nav_force_put(&g_force, i, (float)p->node[i].x + (float)g_m0.tile_w / 2, y + (float)g_m0.tile_h / 2 * fit);
        }
        force_springs(p, r);
        g_force_zoom = g_zoom;
        g_force_ms = c->ui->now_ms;
    } else if (g_force_zoom != g_zoom) {                                           // -1 <-> -2: the graph scaled about the middle
        const float rest = rest_of(g_zoom, w, h, p), s = rest / g_force.rest;
        g_force.rest = rest; g_force.push = push_of(rest);
        g_force.hw = (float)g_m.tile_w / 2; g_force.hh = (float)g_m.tile_h / 2;
        for (int i = 0; i < NAV_NODES; i++)
            if (g_force.on[i]) nav_force_put(&g_force, i, w / 2 + (g_force.x[i] - w / 2) * s, h / 2 + (g_force.y[i] - h / 2) * s);
        g_force_zoom = g_zoom;
    }
    bool changed = false;
    for (int i = 0; i < NAV_NODES && !changed; i++) changed = key_of(p, r, i) != g_force_key[i] || on_graph(&p->node[i]) != g_force.on[i];
    if (changed) {                                                                 // the rack changed under it
        const nav_force_t old = g_force;
        uint8_t old_key[NAV_NODES];
        memcpy(old_key, g_force_key, sizeof old_key);
        bool kept[NAV_NODES] = {false};
        for (int i = 0; i < NAV_NODES; i++) {
            g_force_key[i] = key_of(p, r, i);
            g_force.on[i] = false;
            if (!on_graph(&p->node[i])) continue;
            for (int j = 0; j < NAV_NODES; j++)
                if (old.on[j] && old_key[j] == g_force_key[i]) { nav_force_put(&g_force, i, old.x[j], old.y[j]); kept[i] = true; break; }
        }
        for (int i = 0; i < NAV_NODES; i++) {                                     // a new node: beside one it is cabled to, else in the middle
            if (!on_graph(&p->node[i]) || kept[i]) continue;
            float x = w / 2, y = h / 2;
            for (int e = 0; e < p->n_edges; e++) {
                const int o = p->edge[e].from == i ? p->edge[e].to : p->edge[e].to == i ? p->edge[e].from : -1;
                if (o >= 0 && kept[o]) { x = g_force.x[o] + 6; y = g_force.y[o] + 6; break; }
            }
            nav_force_put(&g_force, i, x, y);
        }
        force_springs(p, r);
        g_force.rest = rest_of(g_zoom, w, h, p);
        g_force.push = push_of(g_force.rest);
        nav_force_go(&g_force);
    }
    const uint32_t now = c->ui->now_ms;
    int steps = 1;
    if (now) {
        if (now - g_force_ms > 500) g_force_ms = now - STEP_MS;                   // (back after a while: no catching up)
        steps = (int)((now - g_force_ms) / STEP_MS);
        g_force_ms += (uint32_t)(steps * STEP_MS);
    }
    for (int k = 0; k < steps; k++) nav_force_step(&g_force);
}

bool synth_ui_map_animating(const synth_ui_t *ui) {
    return ui->in_rack && (ui->menu_tab == TAB_RACK || ui->menu_tab == TAB_MAP) && !ui->map_at && ui->map_zoom < 0 && g_force_zoom < 0 &&
           g_force.moving;
}

static int item_arg(int e) { return e >= E_G0 ? MAP_CONNECT + (e - E_G0) : e == E_M0 ? MAP_EDIT : RM_TYPE + (e - E_M0 - 1); }

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

// Zoomed out, a Connect choosing its target: a push on a module takes it (its parameter 0 at first), then the rack's menu on the source
// with only its Prm and Dpth; anything else is refused, the popup says why, and the choosing goes on.
static void pick_target(const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    const nav_node_t *n = &graph(c)->node[c->arg];
    const int src = ui->map_src - 1;
    char name[8] = "";
    if (n->kind == NODE_MODULE) rack_slot_name(c->rack, n->slot, name, sizeof name);
    if (n->kind == NODE_MODULE && n->slot == src) { snprintf(ui->rack_msg, sizeof ui->rack_msg, "Pick another module"); return; }
    if (n->kind != NODE_MODULE || !rack_set_target(c->rack, src, n->slot, 0)) {
        snprintf(ui->rack_msg, sizeof ui->rack_msg, "%s takes no modulation", n->kind == NODE_MODULE ? name : n->kind == NODE_MIX ? "MIX" : "OUT");
        return;
    }
    ui->rack_dirty = true;
    scr_rack_select(ui, c->rack, c->rack->slot[src].row, src);
    ui->rack_menu = true;
    ui->map_menu = (uint8_t)(src + 1);
    ui->map_act = MAP_ACT_PARAM;
    ui->latched = false;
    const int first = first_item(c);
    ui->row = first >= 0 ? first + 1 : src + 1;
}

// A push on a node: its menu (the rack's; zoomed out, the graph's), on the cell it is.
static void node_activate(const ui_ctx_t *c) {
    const nav_node_t *n = &graph(c)->node[c->arg];
    synth_ui_t *ui = c->ui;
    if (ui->map_zoom < 0) {
        if (ui->map_act == MAP_ACT_PICK) { pick_target(c); return; }
        if (n->kind != NODE_MODULE) return;                    // (the MIX, the OUT: nothing to do there)
        ui->map_act = MAP_ACT_MENU;
    }
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
    if (c->ui->map_act == MAP_ACT_PICK) { pick_cancel(c->ui); return true; }
    if (!c->ui->rack_menu) return false;                       // (then up: the header, then out of the menu: ui_input.c)
    scr_rack_menu_close(c);
    c->ui->map_act = MAP_ACT_NONE;
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

// The value a tile shows as its bar: the level of an oscillator, the cutoff of a filter ..., else the module's first continuous one
// (main_index: its index, -1 for none).
static int main_index(const rack_slot_t *s) {
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
    return i >= 0 && rack_mparam_is_continuous((module_type_t)s->type, i) ? i : -1;
}
static float main_value(const rack_slot_t *s) { const int i = main_index(s); return i >= 0 ? rack_mparam_norm(s, i) : -1.0f; }

/* ---------------- drawing ---------------- */

static void tile_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    (void)st; (void)latched;
    if (r.w <= 0 || r.h <= 0) return;                           // not in the part of the map on show
    const nav_node_t *n = &graph(c)->node[c->arg];
    char name[8];
    node_label(c, c->arg, name, sizeof name);
    const bool selected = focused || (c->ui->rack_menu && c->ui->map_menu == c->arg + 1);     // (the node the menu is open on)
    const bool modulator = n->kind == NODE_MODULE && n->col >= 2;
    if (g_zoom < 0) {                                          // a small node: its colour and its number
        char label[8];
        if (n->kind == NODE_MODULE) snprintf(label, sizeof label, "%d", c->rack->slot[n->slot].id);
        else snprintf(label, sizeof label, "%s", g_zoom < -1 ? (n->kind == NODE_MIX ? "M" : "O") : name);
        uw_node(g, r, label, node_family(c, c->arg), selected, g_zoom < -1 || n->kind != NODE_MODULE);
        return;
    }
    if (g_zoom > 0) {
        uw_bar_t bar[4];
        char lbl[4][8];
        int k = 0;
        const char *kind = "";
        if (n->kind == NODE_MODULE) {                          // its values: the main one first, then the other continuous ones in order
            const rack_slot_t *s = &c->rack->slot[n->slot];
            const module_type_t t = (module_type_t)s->type;
            kind = rack_type_name(t);
            const int first = main_index(s);
            for (int i = -1; i < rack_mparam_count(t) && k < 4; i++) {
                const int p = i < 0 ? first : i;
                if (p < 0 || (i >= 0 && p == first) || !rack_mparam_is_continuous(t, p)) continue;
                snprintf(lbl[k], sizeof lbl[k], "%s", rack_mparam_label(t, p));
                bar[k] = (uw_bar_t){lbl[k], rack_mparam_norm(s, p)};
                k++;
            }
        } else if (n->kind == NODE_MIX) {                      // the branches' levels
            kind = "1 + 2";
            for (int b = 0; b < RACK_BRANCHES && k < 4; b++) {
                snprintf(lbl[k], sizeof lbl[k], "Lvl%d", b + 1);
                bar[k] = (uw_bar_t){lbl[k], c->rack->br_lvl[b]};
                k++;
            }
        } else if (n->kind == NODE_OUT) kind = "the output";
        else if (n->kind == NODE_PLUS) kind = "add a module";
        uw_tile_wide(g, r, name, kind, node_family(c, c->arg), bar, k, selected, modulator);
        return;
    }
    const float bar = n->kind == NODE_MODULE ? main_value(&c->rack->slot[n->slot]) : -1.0f;
    uw_tile(g, r, name, node_family(c, c->arg), bar, selected, modulator);
}

// A node's tile on the screen, whether or not it is in view (the cables are drawn from it; the clip window keeps them on the map). Zoom 1:
// a "+" and the OUT, which have no values, are one line high.
#define WIDE_LINE_H 12
static gui_rect_t tile_at(const nav_graph_t *gr, int i) {
    const bool line = g_zoom > 0 && (gr->node[i].kind == NODE_PLUS || gr->node[i].kind == NODE_OUT);
    return gui_rect(g_map_x + gr->node[i].x - g_scroll_x, g_map_y + gr->node[i].y - g_scroll, g_m.tile_w, line ? WIDE_LINE_H : g_m.tile_h);
}

// The cables, under the tiles, as straight runs with elbows. Audio: out of the bottom of a tile, down its own column, then across just above
// the tile it feeds (so two branches meet in the gap over the MIX). Modulation (dashed): out of the modulator's side toward its target,
// straight across when they stand side by side, else across to the target's middle and then up or down to it.
static void draw_extra(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, const gui_rect_t *rect) {
    (void)st;
    const nav_graph_t *gr = graph(c);
    const int tw = g_m.tile_w, th = g_m.tile_h;
    u8g2_SetClipWindow(g, area.x, area.y, area.x + area.w, rect[E_INFO].y);      // not over the strip
    if (g_zoom < 0) {                                                              // zoomed out: straight, centre to centre (under the nodes)
        for (int pass = 0; pass < 2; pass++)                                       // (the modulation over the audio)
            for (int e = 0; e < gr->n_edges; e++) {
                const nav_edge_t *ed = &gr->edge[e];
                if ((ed->kind == EDGE_MOD) != (pass == 1) || gr->node[ed->from].kind == NODE_NONE || gr->node[ed->to].kind == NODE_NONE) continue;
                const gui_rect_t a = tile_at(gr, ed->from), b = tile_at(gr, ed->to);
                uw_wire(g, a.x + a.w / 2, a.y + a.h / 2, b.x + b.w / 2, b.y + b.h / 2, ed->kind == EDGE_MOD ? UW_CABLE_MOD : UW_CABLE_AUDIO);
            }
        const int src = c->ui->map_src - 1, to = c->ui->row - 1;                    // a Connect choosing: the cable to be, to the cursor
        if (c->ui->map_act == MAP_ACT_PICK && src >= 0 && src < NAV_NODES && to >= 0 && to < NAV_NODES && to != src &&
            gr->node[src].kind != NODE_NONE && gr->node[to].kind != NODE_NONE) {
            const gui_rect_t a = tile_at(gr, src), b = tile_at(gr, to);
            uw_wire(g, a.x + a.w / 2, a.y + a.h / 2, b.x + b.w / 2, b.y + b.h / 2, UW_CABLE_GATE);
        }
        u8g2_SetClipWindow(g, area.x, area.y, area.x + area.w, area.y + area.h);
        return;
    }
    for (int e = 0; e < gr->n_edges; e++) {
        if (gr->edge[e].kind != EDGE_AUDIO) continue;
        const gui_rect_t a = tile_at(gr, gr->edge[e].from), b = tile_at(gr, gr->edge[e].to);
        const int ax = a.x + tw / 2, ay = a.y + a.h, bx = b.x + tw / 2, by = b.y - 1;
        if (ax == bx) { uw_line(g, ax, ay, ax, by, UW_CABLE_AUDIO); continue; }
        const int my = by - g_m.gap_y / 2;
        uw_line(g, ax, ay, ax, my, UW_CABLE_AUDIO);
        uw_line(g, ax, my, bx, my, UW_CABLE_AUDIO);
        uw_line(g, bx, my, bx, by, UW_CABLE_AUDIO);
    }
    for (int e = 0; e < gr->n_edges; e++) {
        if (gr->edge[e].kind != EDGE_MOD) continue;
        const gui_rect_t m = tile_at(gr, gr->edge[e].from), t = tile_at(gr, gr->edge[e].to);
        const bool left = t.x < m.x;                                               // the target is on the modulator's left
        const int mx = left ? m.x - 1 : m.x + tw, my = m.y + th / 2;
        bool blocked = false;                                                      // another tile between them on that row (a second modulator of one target)
        for (int i = 0; i < NAV_NODES && my >= t.y && my < t.y + th; i++) {
            if (gr->node[i].kind == NODE_NONE || i == gr->edge[e].from || i == gr->edge[e].to || gr->node[i].y != gr->node[gr->edge[e].from].y) continue;
            const int x = gr->node[i].x;
            if (left ? (x < gr->node[gr->edge[e].from].x && x > gr->node[gr->edge[e].to].x) : (x > gr->node[gr->edge[e].from].x && x < gr->node[gr->edge[e].to].x)) blocked = true;
        }
        if (blocked) {                                                             // around, through the gap under the row
            const int bx = m.x + tw / 2, gy = m.y + th + g_m.gap_y / 2 - 1, tx = t.x + tw / 2 + (left ? 4 : -4);
            uw_line(g, bx, m.y + th, bx, gy, UW_CABLE_MOD);
            uw_line(g, bx, gy, tx, gy, UW_CABLE_MOD);
            uw_line(g, tx, gy, tx, t.y + th, UW_CABLE_MOD);
        } else if (my >= t.y && my < t.y + th) {                                   // side by side: straight across
            uw_line(g, mx, my, left ? t.x + tw : t.x - 1, my, UW_CABLE_MOD);
        } else {
            const int tx = t.x + tw / 2 + (left ? 4 : -4);                         // (beside the audio cable through its middle)
            uw_line(g, mx, my, tx, my, UW_CABLE_MOD);
            uw_line(g, tx, my, tx, t.y > m.y ? t.y - 1 : t.y + th, UW_CABLE_MOD);
        }
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
    if (ui->map_act == MAP_ACT_PICK && ui->map_src) {                              // a Connect choosing its target
        const int asc = u8g2_GetAscent(g), rh = gui_row_h(g, st);
        char src[8], to[8] = "?", l1[40];
        rack_slot_name(rk, ui->map_src - 1, src, sizeof src);
        if (node >= 0 && node < NAV_NODES && gr->node[node].kind == NODE_MODULE) rack_slot_name(rk, gr->node[node].slot, to, sizeof to);
        else if (node >= 0 && node < NAV_NODES && gr->node[node].kind != NODE_NONE) snprintf(to, sizeof to, "%s", gr->node[node].kind == NODE_MIX ? "MIX" : "OUT");
        snprintf(l1, sizeof l1, "Connect %s to %s", src, to);
        uw_text(g, r.x + 3, r.y + 2 + asc, l1, UC_MOD);
        uw_text(g, r.x + 3, r.y + 2 + rh + asc, "push: pick  Back: cancel", UC_DIM);
        return;
    }
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

// The graph's menu (zoomed out) on a module: Connect from what can modulate (a modulator, an oscillator: FM; not a motion sequencer, whose
// lanes have their own targets), Disconnect all while it has a modulation cable, Delete.
static bool graph_item_enabled(const ui_ctx_t *c) {
    const int slot = scr_rack_selected_slot(c);
    if (slot < 0) return false;
    const module_type_t t = (module_type_t)c->rack->slot[slot].type;
    if (c->arg == MAP_CONNECT) return rack_can_target(t) && t != MOD_MSEQ;
    if (c->arg == MAP_UNLINK) {
        const nav_graph_t *gr = graph(c);
        for (int e = 0; e < gr->n_edges; e++) if (gr->edge[e].kind == EDGE_MOD && (gr->edge[e].from == slot || gr->edge[e].to == slot)) return true;
        return false;
    }
    return true;
}
static void graph_item_activate(const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    const int slot = scr_rack_selected_slot(c);
    if (c->arg == MAP_DELETE) {                                                    // (the rack's Delete)
        ui_ctx_t x = *c;
        x.arg = RM_DELETE;
        scr_rack_item_activate(&x);
    } else {
        if (c->arg == MAP_UNLINK && slot >= 0 && rack_unlink(c->rack, slot)) ui->rack_dirty = true;
        scr_rack_menu_close(c);
    }
    ui->map_act = MAP_ACT_NONE;
    if (c->arg == MAP_CONNECT && slot >= 0) { ui->map_act = MAP_ACT_PICK; ui->map_src = (uint8_t)(slot + 1); }
}

static bool item_enabled(const ui_ctx_t *c) {
    const uint8_t act = c->ui->map_act;
    if (c->arg >= MAP_CONNECT) return c->ui->rack_menu && act == MAP_ACT_MENU && graph_item_enabled(c);
    if (act == MAP_ACT_MENU) return false;
    if (act == MAP_ACT_PARAM) return (c->arg == RM_PRM || c->arg == RM_DPTH) && scr_rack_item_enabled(c);   // (a Connect: its parameter, its depth)
    return c->arg == MAP_EDIT ? edit_enabled(c) : scr_rack_item_enabled(c);
}
static void item_activate(const ui_ctx_t *c) {
    if (c->arg >= MAP_CONNECT) graph_item_activate(c);
    else if (c->arg == MAP_EDIT) edit_activate(c);
    else scr_rack_item_activate(c);
}

static void item_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    static const char *const graph_label[] = {"Connect", "Disconnect all", "Delete"};
    const char *label = c->arg >= MAP_CONNECT ? graph_label[c->arg - MAP_CONNECT] : c->arg == MAP_EDIT ? "Edit" : scr_rack_item_label(c->arg);
    const bool button = c->arg >= MAP_CONNECT || c->arg == MAP_EDIT || scr_rack_item_kind(c->arg) == EL_BUTTON;
    char val[24] = "";
    if (button) snprintf(val, sizeof val, ">");
    else scr_rack_item_value(c, val, sizeof val);
    uw_value_row(g, st, r, label, val, -1.0f, UC_MOD, latched ? GUI_LATCHED : focused ? GUI_FOCUSED : GUI_PLAIN, false);
}

/* ---------------- layout ---------------- */

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    g_zoom = c->ui->map_zoom < ZOOM_MIN ? ZOOM_MIN : c->ui->map_zoom > ZOOM_MAX ? ZOOM_MAX : c->ui->map_zoom;
    g_m = metrics(area, g_zoom);
    g_m0 = metrics(area, 0);
    g_map_w = area.w - 2 * TILE_PAD;
    g_map_x = area.x + TILE_PAD;
    g_map_y = area.y + 2;                                                          // (room for the cursor's outline over the top tile)
    rect[E_INFO] = gui_rect(area.x, area.y + area.h - g_m.strip_h, area.w, g_m.strip_h);
    const int room = rect[E_INFO].y - 2 - g_map_y;                                 // the map's height on show (the outline under the lowest tile)
    const nav_graph_t *gr = graph(c);
    const int focus = c->ui->rack_menu ? c->ui->map_menu - 1 : c->ui->row - 1;
    const bool on_node = focus >= 0 && focus < NAV_NODES && gr->node[focus].kind != NODE_NONE;
    g_scroll = 0;                                                                  // a map taller than the room: the part around the cursor (no state)
    if (on_node && gr->node[focus].y + g_m.tile_h > room) g_scroll = gr->node[focus].y + g_m.tile_h - room;
    if (g_zoom < 0) {                                                              // zoomed out: the whole graph in the room, no scroll
        const nav_graph_t *p = placed_graph(c);
        int scroll0 = 0;                                                           // (zoom 0's scroll: where its tiles were on the screen)
        if (focus >= 0 && focus < NAV_NODES && p->node[focus].kind != NODE_NONE && p->node[focus].y + g_m0.tile_h > room)
            scroll0 = p->node[focus].y + g_m0.tile_h - room;
        force_update(c, room, scroll0);
        g_scroll = 0;
        gr = graph(c);
        if (!c->ui->rack_menu && focus >= 0 && focus < NAV_NODES && gr->node[focus].kind == NODE_NONE) {   // on a "+": the nearest node instead
            int best = -1, best_d = 0;
            for (int i = 0; i < NAV_NODES; i++) {
                if (gr->node[i].kind == NODE_NONE) continue;
                const int dx = p->node[i].x - p->node[focus].x, dy = p->node[i].y - p->node[focus].y, d = dx * dx + dy * dy;
                if (best < 0 || d < best_d) { best = i; best_d = d; }
            }
            if (best >= 0) c->ui->row = best + 1;
        }
    }
    g_scroll_x = 0;                                                                // wider than the screen (zoom 1): the cursor's tile in the middle
    if (g_zoom > 0) {
        int x0 = 1 << 14, x1 = -(1 << 14);
        for (int i = 0; i < NAV_NODES; i++) {
            if (gr->node[i].kind == NODE_NONE) continue;
            if (gr->node[i].x < x0) x0 = gr->node[i].x;
            if (gr->node[i].x + g_m.tile_w > x1) x1 = gr->node[i].x + g_m.tile_w;
        }
        if (x1 - x0 <= g_map_w) g_scroll_x = x0 - (g_map_w - (x1 - x0)) / 2;     // it fits: centred
        else {
            const int want = on_node ? gr->node[focus].x + g_m.tile_w / 2 - g_map_w / 2 : x0;
            g_scroll_x = want < x0 ? x0 : want > x1 - g_map_w ? x1 - g_map_w : want;
        }
    }
    for (int i = 0; i < NAV_NODES; i++) {
        const gui_rect_t t = tile_at(gr, i);
        const bool in_view = t.y >= g_map_y && t.y + t.h <= g_map_y + room && t.x + t.w > area.x && t.x < area.x + area.w;   // (cut at the sides)
        rect[i] = gr->node[i].kind != NODE_NONE && in_view ? t : gui_rect(0, 0, 0, 0);
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
    MENU_ITEM(E_G0, EL_BUTTON, MAP_CONNECT),  MENU_ITEM(E_G0 + 1, EL_BUTTON, MAP_UNLINK), MENU_ITEM(E_G0 + 2, EL_BUTTON, MAP_DELETE),
};
_Static_assert(RACK_MAX == 16 && NAV_NODES == RACK_MAX + 6, "N4 x 4 covers the module nodes, then the MIX, the OUT and the four +");
_Static_assert(E_G0 == E_M0 + 14 && E_COUNT == E_G0 + 3 && RM_DELETE - RM_TYPE == 12,
               "the menu's items: Edit, then the rack's Type .. Delete, then the graph's Connect, Disconnect all, Delete");

const screen_def_t scr_map_screen = {elements, E_COUNT, false, false, layout, map_back, NULL, draw_extra, node_move};
