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
// drawing each step (synth_ui_map_animating); straight cables from centre to centre. No walls: the graph grows as big as it needs and the
// view scrolls over it both ways, following the cursor (a graph that fits is centred). There a push on a module opens the graph's own menu:
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
#define MAP_CONNECT 101                  // the graph's menu (zoomed out): Mod to, Audio to, its cables, Disconnect all, Delete
#define MAP_AUDIO   102
#define MAP_UNLINK  103
#define MAP_DELETE  104
#define MAP_CPRM    105                  // ... a cable just made: its parameter (modulation) ...
#define MAP_CAMT    106                  // ... and its depth (modulation) or its level (audio)
#define MAP_ATYPE   107                  // the Add menu (a push on the header): the type, the row, Add
#define MAP_AROW    108
#define MAP_AADD    109
#define MAP_K0      110                  // ... the module's cables out, a row each (its chain's, its audio cables, its targets)
#define OUT_MAX     (RACK_AUDIO_MAX + RACK_MOD_MAX)
enum { E_INFO = NAV_NODES, E_M0, E_G0 = E_M0 + 1 + (RM_END - RM_TYPE), E_A0 = E_G0 + 6 + OUT_MAX, E_COUNT = E_A0 + 3 };
// ui->map_act: the graph's menu open; a cable choosing its target (modulation, audio); the rack's Prm / Dpth of a first target; a cable's own
enum { MAP_ACT_NONE, MAP_ACT_MENU, MAP_ACT_PICK, MAP_ACT_PARAM, MAP_ACT_PICK_AUDIO, MAP_ACT_CABLE, MAP_ACT_ADD };

#define TILE_PAD 2                       // the margin left and right of the map (the cursor's outline is 2 px outside a tile)
#define MENU_W   96
#define ZOOM_MIN (-2)
#define ZOOM_MAX 1
typedef struct { int tile_w, tile_h, gap_x, gap_y, strip_h; bool tall; } metrics_t;
static metrics_t metrics(gui_rect_t area, int zoom) {
    const bool tall = area.h >= 120;
    if (zoom > 0) return tall ? (metrics_t){84, 30, 12, 10, 22, true} : (metrics_t){84, 23, 12, 8, 21, false};     // (3 x the pitch across)
    if (zoom == -1) return (metrics_t){24, 16, 0, 0, tall ? 22 : 21, tall};                                         // (a node; no grid)
    if (zoom < -1) return (metrics_t){12, 8, 0, 0, tall ? 22 : 21, tall};                                           // (half of -1's)
    return tall ? (metrics_t){28, 14, 4, 4, 22, true} : (metrics_t){28, 12, 4, 3, 21, false};
}
static gui_rect_t g_menu_box;            // the menu's box (layout -> strip_draw, which paints it before the items)
static metrics_t g_m = {28, 14, 4, 4, 32, true}, g_m0 = {28, 14, 4, 4, 32, true};   // the sizes in use and those of zoom 0 (layout -> graph, draw_extra)
static metrics_t g_m1 = {24, 16, 0, 0, 32, true};                                     // ... and of zoom -1, the force graph's own (-2 is it at half size)
#define HALF 0.5f                                                                     // zoom -2: the graph of -1 at half its size, about the room's middle
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
            const float s = g_zoom < -1 ? HALF : 1.0f, x = g_force.cx + (g_force.x[i] - g_force.cx) * s,
                        y = g_force.cy + (g_force.y[i] - g_force.cy) * s;
            n->x = (int16_t)(x - (float)g_m.tile_w / 2 + 0.5f);
            n->y = (int16_t)(y - (float)g_m.tile_h / 2 + 0.5f);
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

// A cable choosing its target stops (Back, a zoom); the popup says so.
static void pick_cancel(synth_ui_t *ui) {
    ui->map_act = MAP_ACT_NONE;
    ui->map_src = 0;
    snprintf(ui->rack_msg, sizeof ui->rack_msg, "Connect cancelled");
}

static bool picking(const synth_ui_t *ui);
bool synth_ui_map_zoom(synth_ui_t *ui, int steps) {
    if (!ui->in_rack || (ui->menu_tab != TAB_RACK && ui->menu_tab != TAB_MAP) || ui->map_at || ui->rack_menu) return false;   // (the map on show)
    int z = ui->map_zoom + steps;
    z = z < ZOOM_MIN ? ZOOM_MIN : z > ZOOM_MAX ? ZOOM_MAX : z;
    if (z == ui->map_zoom) return false;
    if (picking(ui)) pick_cancel(ui);
    ui->map_zoom = (int8_t)z;
    return true;
}

/* ---------------- zoomed out: the force graph (core/nav_force.h) ---------------- */

static bool g_force_set;                 // the graph is set going; false: the next zoom out spawns it again (and lays it out anew)
static uint8_t g_force_key[NAV_NODES];   // what each of its nodes is: a module's number, KEY_MIX, KEY_OUT (a slot's index moves on a delete)
static uint32_t g_force_ms;              // the clock at its last step
#define KEY_MIX 254
#define KEY_OUT 255
#define STEP_MS 16                       // a step every 16 ms (about 60 a second, whatever the frame rate)

// A cable's length at rest (zoom -1's units: -2 shows it at half size), whatever the rack: the graph takes the room it needs and the screen
// scrolls over it; the push between nodes goes with its square.
#define REST 31.0f
#define PUSH (0.26f * REST * REST)
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

// Before each frame zoomed out: the graph set going (each node where its tile was at zoom 0, the camera on zoom 0's view: `scroll0`, that
// map's scroll; anew each time the map zooms out from 0 or 1; -1 <-> -2 is the same graph at half size, at once), kept in step with the rack
// (a node keeps its place by what it is; one joining starts beside a node it is cabled to), then the steps the time since the last frame
// asks for (no clock, as in the tests: one a frame).
static int g_cam_x, g_cam_y;             // zoomed out: the view's top left on the graph (kept: it moves only as far as the cursor needs)
static void force_update(const ui_ctx_t *c, int scroll0) {
    if (g_zoom >= 0) { g_force_set = false; return; }
    const nav_graph_t *p = placed_graph(c);
    const rack_t *r = c->rack;
    if (!g_force_set) {
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;                       // the centre: the middle of zoom 0's map
        for (int i = 0; i < NAV_NODES; i++) {
            if (!on_graph(&p->node[i])) continue;
            const float x = (float)p->node[i].x + (float)g_m0.tile_w / 2, y = (float)p->node[i].y + (float)g_m0.tile_h / 2;
            x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1; y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1;
        }
        const float cx = x1 >= x0 ? (x0 + x1) / 2 : (float)g_map_w / 2, cy = y1 >= y0 ? (y0 + y1) / 2 : 0;
        const float unscale = g_zoom < -1 ? 1.0f / HALF : 1.0f;                    // (spawned straight into -2: where its tiles were there)
        nav_force_init(&g_force, cx, cy, (float)g_m1.tile_w / 2, (float)g_m1.tile_h / 2, REST, PUSH);
        for (int i = 0; i < NAV_NODES; i++) {
            g_force_key[i] = key_of(p, r, i);
            const float x = (float)p->node[i].x + (float)g_m0.tile_w / 2, y = (float)p->node[i].y + (float)g_m0.tile_h / 2;
            if (on_graph(&p->node[i])) nav_force_put(&g_force, i, cx + (x - cx) * unscale, cy + (y - cy) * unscale);
        }
        force_springs(p, r);
        g_force_set = true;
        g_force_ms = c->ui->now_ms;
        g_cam_x = 0;
        g_cam_y = scroll0;
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
            float x = g_force.cx, y = g_force.cy;
            for (int e = 0; e < p->n_edges; e++) {
                const int o = p->edge[e].from == i ? p->edge[e].to : p->edge[e].to == i ? p->edge[e].from : -1;
                if (o >= 0 && kept[o]) { x = g_force.x[o] + 6; y = g_force.y[o] + 6; break; }
            }
            nav_force_put(&g_force, i, x, y);
        }
        force_springs(p, r);
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
    return ui->in_rack && (ui->menu_tab == TAB_RACK || ui->menu_tab == TAB_MAP) && !ui->map_at && ui->map_zoom < 0 && g_force_set &&
           g_force.moving;
}

// The graph's items in their order on screen: Mod to, Audio to, the cable rows, Disconnect all, Delete; then a new cable's Prm and Dpth / Lvl.
static int item_arg(int e) {
    if (e < E_G0) return e == E_M0 ? MAP_EDIT : RM_TYPE + (e - E_M0 - 1);
    if (e >= E_A0) return MAP_ATYPE + (e - E_A0);
    const int k = e - E_G0;
    static const int tail[4] = {MAP_UNLINK, MAP_DELETE, MAP_CPRM, MAP_CAMT};
    return k == 0 ? MAP_CONNECT : k == 1 ? MAP_AUDIO : k < 2 + OUT_MAX ? MAP_K0 + (k - 2) : tail[k - 2 - OUT_MAX];
}
static bool picking(const synth_ui_t *ui) { return ui->map_act == MAP_ACT_PICK || ui->map_act == MAP_ACT_PICK_AUDIO; }

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
// Audio to: a push on a processor further down takes it, at level 1, then its Lvl row; refused (rack_audio_can) the popup says why.
// Mod to: its first target (then the rack's Prm and Dpth) or one more (then that cable's Prm and Dpth: rack_mod_can).
static void pick_target(const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    const nav_node_t *n = &graph(c)->node[c->arg];
    const int src = ui->map_src - 1;
    const bool audio = ui->map_act == MAP_ACT_PICK_AUDIO;
    if (n->kind != NODE_MODULE) {
        snprintf(ui->rack_msg, sizeof ui->rack_msg, "%s takes no %s", n->kind == NODE_MIX ? "MIX" : "OUT", audio ? "cable" : "modulation");
        return;
    }
    if (audio ? !rack_audio_can(c->rack, src, n->slot, ui->rack_msg, sizeof ui->rack_msg)
              : !rack_mod_can(c->rack, src, n->slot, ui->rack_msg, sizeof ui->rack_msg)) return;
    const bool is_first = !audio && rack_mod_targets(c->rack, src) == 0;
    if (audio ? !rack_audio_connect(c->rack, src, n->slot) : !rack_mod_connect(c->rack, src, n->slot, 0)) return;
    ui->rack_dirty = true;
    ui->map_cable = is_first ? 0 : (uint8_t)c->rack->n_cables;
    scr_rack_select(ui, c->rack, c->rack->slot[src].row, src);
    ui->rack_menu = true;
    ui->map_menu = (uint8_t)(src + 1);
    ui->map_act = is_first ? MAP_ACT_PARAM : MAP_ACT_CABLE;
    ui->latched = false;
    const int first = first_item(c);
    ui->row = first >= 0 ? first + 1 : src + 1;
}

// A push on a node: its menu (the rack's; zoomed out, the graph's), on the cell it is.
static void node_activate(const ui_ctx_t *c) {
    const nav_node_t *n = &graph(c)->node[c->arg];
    synth_ui_t *ui = c->ui;
    if (ui->map_zoom < 0) {
        if (picking(ui)) { pick_target(c); return; }
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
    if (picking(c->ui)) { pick_cancel(c->ui); return true; }
    if (c->ui->map_act == MAP_ACT_ADD) {                       // the Add menu: closed, the focus back on the header
        c->ui->rack_menu = false; c->ui->map_act = MAP_ACT_NONE; c->ui->row = 0; c->ui->latched = false;
        return true;
    }
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

static gui_rect_t g_view;                // the map's part of the screen (layout -> tile_draw: zoomed out, a node cut at its edges)
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
        u8g2_SetClipWindow(g, g_view.x, g_view.y, g_view.x + g_view.w, g_view.y + g_view.h);   // (a node at the view's edge: cut there)
        uw_node(g, r, label, node_family(c, c->arg), selected, g_zoom < -1 || n->kind != NODE_MODULE);
        u8g2_SetMaxClipWindow(g);
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
            snprintf(name, sizeof name, "%d", s->id);                // (the number alone, the type beside it in grey)
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
        if (picking(c->ui) && src >= 0 && src < NAV_NODES && to >= 0 && to < NAV_NODES && to != src &&
            gr->node[src].kind != NODE_NONE && gr->node[to].kind != NODE_NONE) {
            const gui_rect_t a = tile_at(gr, src), b = tile_at(gr, to);
            uw_wire(g, a.x + a.w / 2, a.y + a.h / 2, b.x + b.w / 2, b.y + b.h / 2, c->ui->map_act == MAP_ACT_PICK_AUDIO ? UW_CABLE_AUDIO : UW_CABLE_GATE);
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
    // The first modulation into it, in the graph's order (nav_graph.c: the slots' first targets and sequencer lanes, then the cables), with
    // the parameter of that modulation: a cable's own, not its source's first target's (that one indexed another module's parameters: a NULL
    // name past this type's count crashed the snprintf, 2026-11-19).
    const uint8_t id = r->slot[nd->slot].id;
    const module_type_t t = (module_type_t)r->slot[nd->slot].type;
    char who[8];
    for (int j = 0; j < r->count && j < RACK_MAX && !mod[0]; j++) {
        const rack_slot_t *m = &r->slot[j];
        bool hit = false;
        if (m->type == MOD_MSEQ) {
            const ms_pattern_t *p = rack_ms_const(r, j);
            for (int l = 0; p && l < MS_LANES; l++) hit = hit || p->lane[l].tgt_id == id;
        } else hit = rack_can_target((module_type_t)m->type) && m->tgt_id == id;
        if (!hit) continue;
        rack_slot_name(r, j, who, sizeof who);
        snprintf(mod, (size_t)n, "%s<-%s", m->type == MOD_MSEQ ? "MS" : rack_param_name(t, m->tgt_param), who);
    }
    for (int k = 0; k < r->n_cables && !mod[0]; k++) {
        const int from = r->cable[k].kind == CABLE_MOD && r->cable[k].to == id ? rack_find(r, r->cable[k].from) : RACK_NONE;
        if (from == RACK_NONE || from >= RACK_MAX) continue;
        rack_slot_name(r, from, who, sizeof who);
        snprintf(mod, (size_t)n, "%s<-%s", rack_param_name(t, r->cable[k].param), who);
    }
}

// The tall strip (128 x 160; the user, 2026-11-18): only the node's cables, its audio on the first line, its modulation on the second (in the
// modulation's colour): "<1 OC <3 OC >MIX", "<4 LF Cut >2 FL Res". As many as the line holds, then "...".
// Each cable a chip (the user, 2026-11-18): its label on a contrasting fill (audio: the dim grey; modulation: the amber's dim), a drawn arrow
// before it (left: in, right: out). One colour: a frame around it.
#define CABLES_MAX 16
#define CHIP_PAD   9                     // where the text starts: the frame, 1 px, the arrow (5), 2 px; after it 1 px and the frame
#define CHIP_GAP   2
typedef char cable_item_t[16];           // its direction ('<' in, '>' out), then its label
static int chip_w(u8g2_t *g, const char *item) { return CHIP_PAD + 2 + u8g2_GetStrWidth(g, item + 1); }

static void cable_chip(u8g2_t *g, int x, int base, const char *item, ui_colour_t col) {
    const int asc = u8g2_GetAscent(g), y = base - asc - 1, w = chip_w(g, item), h = asc + 3, my = base - 3;
    if (ui_colour(g)) {
        ui_ink(g, col == UC_MOD ? UC_MOD_DIM : UC_DIM);
        u8g2_DrawRBox(g, x, y, w, h, 1);
    } else u8g2_DrawRFrame(g, x, y, w, h, 1);
    ui_ink(g, col);
    if (item[0] == '<') { gui_draw_arrow(g, x + 2, my, 'l', 3); u8g2_DrawHLine(g, x + 4, my, 3); }    // (head 3 px, shaft to 5 px)
    else                { gui_draw_arrow(g, x + 6, my, 'r', 3); u8g2_DrawHLine(g, x + 2, my, 3); }
    ui_ink(g, UC_TEXT);
    uw_text(g, x + CHIP_PAD, base, item + 1, col);
}

static void cable_line(u8g2_t *g, int x, int base, int max_w, cable_item_t *it, int n, const char *none, ui_colour_t col) {
    if (!n) { uw_text(g, x, base, none, UC_DIM); return; }
    const int dots = u8g2_GetStrWidth(g, "...");
    int at = x;
    for (int i = 0; i < n; i++) {
        const int w = chip_w(g, it[i]);
        if (at + w - x > max_w - (i == n - 1 ? 0 : CHIP_GAP + dots)) { uw_text(g, at, base, "...", col); return; }
        cable_chip(g, at, base, it[i], col);
        at += w + CHIP_GAP;
    }
}

static void add_item(cable_item_t *it, int *n, char dir, const rack_t *r, int slot, const char *other, int param_of) {
    if (*n >= CABLES_MAX) return;
    char name[8];
    if (other) snprintf(name, sizeof name, "%s", other); else rack_slot_name(r, slot, name, sizeof name);
    if (param_of >= 0) snprintf(it[*n], sizeof it[*n], "%c%s %s", dir, name, rack_param_name((module_type_t)r->slot[param_of >> 8].type, param_of & 255));
    else snprintf(it[*n], sizeof it[*n], "%c%s", dir, name);
    (*n)++;
}
#define PARAM_OF(target_slot, p) (((target_slot) << 8) | (p))

static void cables_of(const ui_ctx_t *c, int node, cable_item_t *au, int *na, cable_item_t *md, int *nm) {
    const nav_graph_t *gr = graph(c);
    const rack_t *r = c->rack;
    *na = *nm = 0;
    const nav_node_t *nd = &gr->node[node];
    if (nd->kind != NODE_MODULE) {                                                 // the MIX, the OUT: their audio edges
        for (int e = 0; e < gr->n_edges; e++) {
            const nav_edge_t *ed = &gr->edge[e];
            if (ed->kind != EDGE_AUDIO || (ed->to != node && ed->from != node)) continue;
            const int o = ed->to == node ? ed->from : ed->to;
            const char *other = o == NAV_MIX ? "MIX" : o == NAV_OUT ? "OUT" : NULL;
            add_item(au, na, ed->to == node ? '<' : '>', r, o < RACK_MAX ? o : 0, other, -1);
        }
        return;
    }
    const int slot = nd->slot;
    const uint8_t id = r->slot[slot].id;
    if (rack_slot_is_audio(r, slot)) {                                             // audio in: its chain's, the cables; out: its chain's, the cables
        const int prev = rack_audio_prev(r, slot);
        if (prev != RACK_NONE && !r->slot[prev].cut) add_item(au, na, '<', r, prev, NULL, -1);
        else if (prev == RACK_NONE && r->slot[slot].row == ROW_M && gr->node[NAV_MIX].kind == NODE_MIX) add_item(au, na, '<', r, 0, "MIX", -1);
        for (int k = 0; k < r->n_cables; k++)
            if (r->cable[k].kind == CABLE_AUDIO && r->cable[k].to == id && rack_find(r, r->cable[k].from) != RACK_NONE)
                add_item(au, na, '<', r, rack_find(r, r->cable[k].from), NULL, -1);
        const int next = rack_chain_to(r, slot);
        if (next >= 0) add_item(au, na, '>', r, next, NULL, -1);
        else if (next == RACK_OUT) add_item(au, na, '>', r, 0, r->slot[slot].row < ROW_M && gr->node[NAV_MIX].kind == NODE_MIX ? "MIX" : "OUT", -1);
        for (int k = 0; k < r->n_cables; k++)
            if (r->cable[k].kind == CABLE_AUDIO && r->cable[k].from == id && rack_find(r, r->cable[k].to) != RACK_NONE)
                add_item(au, na, '>', r, rack_find(r, r->cable[k].to), NULL, -1);
    }
    for (int j = 0; j < r->count; j++) {                                           // modulation in: first targets, the cables, a sequencer's lanes
        const rack_slot_t *m = &r->slot[j];
        if (j != slot && rack_can_target((module_type_t)m->type) && m->tgt_id == id) add_item(md, nm, '<', r, j, NULL, PARAM_OF(slot, m->tgt_param));
        if (m->type == MOD_MSEQ) {
            const ms_pattern_t *p = rack_ms_const(r, j);
            for (int l = 0; p && l < MS_LANES; l++) if (p->lane[l].tgt_id == id) add_item(md, nm, '<', r, j, NULL, PARAM_OF(slot, p->lane[l].tgt_param));
        }
    }
    for (int k = 0; k < r->n_cables; k++)
        if (r->cable[k].kind == CABLE_MOD && r->cable[k].to == id && rack_find(r, r->cable[k].from) != RACK_NONE)
            add_item(md, nm, '<', r, rack_find(r, r->cable[k].from), NULL, PARAM_OF(slot, r->cable[k].param));
    const int ti = r->slot[slot].tgt_id ? rack_find(r, r->slot[slot].tgt_id) : RACK_NONE;    // out: its first target, its cables
    if (rack_can_target((module_type_t)r->slot[slot].type) && ti != RACK_NONE) add_item(md, nm, '>', r, ti, NULL, PARAM_OF(ti, r->slot[slot].tgt_param));
    for (int k = 0; k < r->n_cables; k++) {
        const int t = r->cable[k].kind == CABLE_MOD && r->cable[k].from == id ? rack_find(r, r->cable[k].to) : RACK_NONE;
        if (t != RACK_NONE) add_item(md, nm, '>', r, t, NULL, PARAM_OF(t, r->cable[k].param));
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
    if (picking(ui) && ui->map_src) {                                              // a cable choosing its target
        const int asc = u8g2_GetAscent(g), rh = gui_row_h(g, st);
        char src[8], to[8] = "?", l1[40];
        rack_slot_name(rk, ui->map_src - 1, src, sizeof src);
        if (node >= 0 && node < NAV_NODES && gr->node[node].kind == NODE_MODULE) rack_slot_name(rk, gr->node[node].slot, to, sizeof to);
        else if (node >= 0 && node < NAV_NODES && gr->node[node].kind != NODE_NONE) snprintf(to, sizeof to, "%s", gr->node[node].kind == NODE_MIX ? "MIX" : "OUT");
        snprintf(l1, sizeof l1, "%s %s to %s", ui->map_act == MAP_ACT_PICK_AUDIO ? "Audio" : "Mod", src, to);
        uw_text(g, r.x + 3, r.y + 2 + asc, l1, UC_MOD);
        uw_text(g, r.x + 3, r.y + 2 + rh + asc, "push: pick  Back: cancel", UC_DIM);
        return;
    }
    if (ui->map_act == MAP_ACT_ADD) {                                              // the Add menu
        uw_text(g, r.x + 3, r.y + 2 + u8g2_GetAscent(g), "Add: outside the chains,", UC_TEXT);
        uw_text(g, r.x + 3, r.y + 2 + gui_row_h(g, st) + u8g2_GetAscent(g), "then cable it", UC_DIM);
    } else if (node < 0 || node >= NAV_NODES || gr->node[node].kind == NODE_NONE) {
        uw_text(g, r.x + 3, r.y + 2 + u8g2_GetAscent(g), ui->row == 0 && !rk->fx ? "push: add a module" : "push: a node's menu", UC_DIM);
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
        const bool tall = g_m.tall;                                                 // (the tall strip: its cables only)
        if (tall && nd->kind != NODE_PLUS) {
            cable_item_t au[CABLES_MAX], md[CABLES_MAX];
            int na, nm;
            cables_of(c, node, au, &na, md, &nm);
            cable_line(g, r.x + 3, r.y + 2 + asc, r.w - 6, au, na, "no audio", UC_TEXT);
            cable_line(g, r.x + 3, r.y + 3 + rh + asc, r.w - 6, md, nm, "no modulation", UC_MOD);   // (1 px lower: the chips apart)
            u8g2_SetMaxClipWindow(g);
            goto menu_box;
        }
        if (ui_colour(g)) ui_fill(g, r.x + 3, r.y + 3, 5, 5, fam);
        else u8g2_DrawBox(g, r.x + 3, r.y + 3, 5, 5);
        uw_marquee(g, r.x + 11, r.y + 2 + asc, r.w - 14, l1, UC_TEXT, true);         // (the selected node: a long one slides)
        if (nd->kind == NODE_PLUS) {
            uw_text(g, r.x + 3, r.y + 2 + rh + asc, l2, UC_DIM);
        } else {
            if (nd->kind == NODE_MODULE && nd->col < 2) snprintf(l2, sizeof l2, "in %s  out %s", in, out);
            else if (nd->kind == NODE_MIX) snprintf(l2, sizeof l2, "-> row M");
            uw_text(g, r.x + 3, r.y + 2 + rh + asc, l2, UC_TEXT);
        }
        u8g2_SetMaxClipWindow(g);
    }
menu_box:
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

// The graph's menu (zoomed out) on a module. Mod to: from what can modulate (a modulator, an oscillator: FM; not a motion sequencer, whose
// lanes have their own targets), while it has fewer than 6 targets. Audio to: from an audio module with fewer than 4 cables out. Then a
// row per cable out of it (its chain cable, its audio cables, its targets): left / right its level or depth, a push removes it. Disconnect
// all while it has a cable. Delete.
typedef enum { OUT_CHAIN, OUT_FIRST, OUT_CABLE } out_kind_t;
typedef struct { uint8_t kind; int8_t k; } out_t;
static int outs_of(const rack_t *r, int slot, out_t *o) {
    int n = 0;
    if (rack_chain_to(r, slot) != RACK_NONE) o[n++] = (out_t){OUT_CHAIN, -1};
    const uint8_t id = r->slot[slot].id;
    for (int k = 0; k < r->n_cables && n < OUT_MAX; k++) if (r->cable[k].kind == CABLE_AUDIO && r->cable[k].from == id) o[n++] = (out_t){OUT_CABLE, (int8_t)k};
    if (r->slot[slot].tgt_id && rack_find(r, r->slot[slot].tgt_id) != RACK_NONE && n < OUT_MAX) o[n++] = (out_t){OUT_FIRST, -1};
    for (int k = 0; k < r->n_cables && n < OUT_MAX; k++) if (r->cable[k].kind == CABLE_MOD && r->cable[k].from == id) o[n++] = (out_t){OUT_CABLE, (int8_t)k};
    return n;
}
static bool out_at(const ui_ctx_t *c, int slot, out_t *o) {   // the cable row c->arg stands for
    out_t all[OUT_MAX];
    const int n = outs_of(c->rack, slot, all), i = c->arg - MAP_K0;
    if (i < 0 || i >= n) return false;
    *o = all[i];
    return true;
}
static void out_label(const ui_ctx_t *c, int slot, out_t o, char *out, int n) {
    const rack_t *r = c->rack;
    char to[8] = "?";
    if (o.kind == OUT_CHAIN) {
        const int next = rack_chain_to(r, slot);
        if (next >= 0) rack_slot_name(r, next, to, sizeof to);
        else snprintf(to, sizeof to, "%s", r->slot[slot].row < ROW_M && synth_type_is_rack(r->cfg.type) ? "MIX" : "OUT");
        snprintf(out, (size_t)n, ">%s", to);
        return;
    }
    const int ti = rack_find(r, o.kind == OUT_FIRST ? r->slot[slot].tgt_id : r->cable[o.k].to);
    if (ti != RACK_NONE) rack_slot_name(r, ti, to, sizeof to);
    if (o.kind == OUT_CABLE && r->cable[o.k].kind == CABLE_AUDIO) { snprintf(out, (size_t)n, ">%s", to); return; }
    const int p = o.kind == OUT_FIRST ? r->slot[slot].tgt_param : r->cable[o.k].param;
    snprintf(out, (size_t)n, ">%s %s", to, ti != RACK_NONE ? rack_param_name((module_type_t)r->slot[ti].type, p) : "");
}
static void out_value(const ui_ctx_t *c, int slot, out_t o, char *out, int n) {
    if (o.kind == OUT_CHAIN) snprintf(out, (size_t)n, "%.2f", (double)rack_chain_level(c->rack, slot));
    else if (o.kind == OUT_FIRST) rack_depth_format(c->rack, slot, out, n);
    else rack_cable_format(c->rack, o.k, out, n);
}

static bool graph_item_enabled(const ui_ctx_t *c) {
    const int slot = scr_rack_selected_slot(c);
    if (slot < 0) return false;
    const rack_t *r = c->rack;
    const module_type_t t = (module_type_t)r->slot[slot].type;
    out_t o;
    switch (c->arg) {
        case MAP_CONNECT: return rack_can_target(t) && t != MOD_MSEQ && rack_mod_targets(r, slot) < RACK_MOD_MAX;
        case MAP_AUDIO:   return rack_slot_is_audio(r, slot) && rack_audio_outs(r, slot) < RACK_AUDIO_MAX;
        case MAP_UNLINK: {
            const nav_graph_t *gr = graph(c);
            for (int e = 0; e < gr->n_edges; e++) if (gr->edge[e].from == slot || gr->edge[e].to == slot) return true;
            return false;
        }
        case MAP_DELETE:  return true;
        default:          return c->arg >= MAP_K0 && out_at(c, slot, &o);
    }
}

// A new cable's own items (MAP_ACT_CABLE): its parameter (modulation) and its depth, or its level (audio).
static rack_cable_t *new_cable(const ui_ctx_t *c) {
    const int k = c->ui->map_cable - 1;
    return k >= 0 && k < c->rack->n_cables ? &c->rack->cable[k] : NULL;
}
static bool cable_item_enabled(const ui_ctx_t *c) {
    const rack_cable_t *cb = new_cable(c);
    return c->ui->rack_menu && c->ui->map_act == MAP_ACT_CABLE && cb && (c->arg == MAP_CAMT || cb->kind == CABLE_MOD);
}

static void graph_value(const ui_ctx_t *c, char *out, int n) {
    out[0] = 0;
    const int slot = scr_rack_selected_slot(c);
    out_t o;
    if (c->arg == MAP_CPRM || c->arg == MAP_CAMT) {
        const rack_cable_t *cb = new_cable(c);
        if (!cb) return;
        if (c->arg == MAP_CAMT) { rack_cable_format(c->rack, c->ui->map_cable - 1, out, n); return; }
        const int ti = rack_find(c->rack, cb->to);
        snprintf(out, (size_t)n, "%s", ti != RACK_NONE ? rack_param_name((module_type_t)c->rack->slot[ti].type, cb->param) : "--");
    } else if (slot >= 0 && c->arg >= MAP_K0 && out_at(c, slot, &o)) out_value(c, slot, o, out, n);
}

static bool graph_adjust(const ui_ctx_t *c, int dir) {
    const int slot = scr_rack_selected_slot(c);
    rack_t *r = c->rack;
    out_t o;
    if (c->arg == MAP_CPRM) rack_cable_cycle_param(r, c->ui->map_cable - 1, dir);
    else if (c->arg == MAP_CAMT) rack_cable_adjust(r, c->ui->map_cable - 1, dir);
    else if (slot < 0 || c->arg < MAP_K0 || !out_at(c, slot, &o)) return false;
    else if (o.kind == OUT_CHAIN) rack_set_chain_level(r, slot, rack_chain_level(r, slot) + 0.05f * (float)dir);
    else if (o.kind == OUT_FIRST) rack_depth_adjust(r, slot, dir);
    else rack_cable_adjust(r, o.k, dir);
    c->ui->rack_dirty = true;
    return false;
}

static void graph_item_activate(const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    rack_t *r = c->rack;
    const int slot = scr_rack_selected_slot(c);
    out_t o;
    if (c->arg >= MAP_K0) {                                                        // a cable row: the cable goes, the menu stays
        if (slot < 0 || !out_at(c, slot, &o)) return;
        if (o.kind == OUT_CHAIN) r->slot[slot].cut = 1;
        else if (o.kind == OUT_FIRST) rack_target_remove(r, slot);
        else rack_cable_remove(r, o.k);
        ui->rack_dirty = true;
        snprintf(ui->rack_msg, sizeof ui->rack_msg, "Cable removed");
        const int first = first_item(c);
        if (first >= 0) ui->row = first + 1;
        return;
    }
    if (c->arg == MAP_CPRM || c->arg == MAP_CAMT) return;                          // (values: left / right)
    if (c->arg == MAP_DELETE) {                                                    // (the rack's Delete)
        ui_ctx_t x = *c;
        x.arg = RM_DELETE;
        scr_rack_item_activate(&x);
    } else {
        if (c->arg == MAP_UNLINK && slot >= 0 && rack_unlink(r, slot)) ui->rack_dirty = true;
        scr_rack_menu_close(c);
    }
    ui->map_act = MAP_ACT_NONE;
    if ((c->arg == MAP_CONNECT || c->arg == MAP_AUDIO) && slot >= 0) {
        ui->map_act = c->arg == MAP_AUDIO ? MAP_ACT_PICK_AUDIO : MAP_ACT_PICK;
        ui->map_src = (uint8_t)(slot + 1);
    }
}

/* ---------------- the Add menu (a push on the header): a module outside the chains, cabled by hand ---------------- */

static const char *row_name(int row) { return row == 0 ? "Branch 1" : row == 1 ? "Branch 2" : "Row M"; }
// A row that takes the type: the rack's rules at its end or its start (a shared-only effect: row M, where it runs once on all the voices).
static bool add_row_ok(const ui_ctx_t *c, int row) {
    const rack_t *r = c->rack;
    const module_type_t t = (module_type_t)c->ui->rack_type;
    if (row < ROW_M && (!synth_type_is_rack(r->cfg.type) || rack_type_shared_only(t))) return false;
    int end = r->count, first = r->count;
    for (int i = 0; i < r->count; i++) if (r->slot[i].row == row) { if (first == r->count) first = i; end = i + 1; }
    return !rack_is_audio(t) || rack_can_insert(r, end, row, t) || rack_can_insert(r, first, row, t);
}
static void add_row_fix(const ui_ctx_t *c) {                   // (the type changed: a row that takes it)
    for (int k = 0; k < RACK_ROWS && !add_row_ok(c, c->ui->rack_row); k++) c->ui->rack_row = (c->ui->rack_row + 1) % RACK_ROWS;
}

static void header_push(const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    if (c->rack->fx || ui->rack_menu) return;                  // (the Pads' FX chains: their own strip)
    ui->rack_menu = true;
    ui->map_menu = 0;
    ui->map_act = MAP_ACT_ADD;
    add_row_fix(c);
    ui->latched = false;
    ui->row = E_A0 + 1;                                         // (its Type)
}

static bool add_item_enabled(const ui_ctx_t *c) {
    if (!c->ui->rack_menu || c->ui->map_act != MAP_ACT_ADD) return false;
    return c->arg != MAP_AROW || rack_is_audio((module_type_t)c->ui->rack_type);   // (a modulator: no row, it has no chain)
}
static void add_value(const ui_ctx_t *c, char *out, int n) {
    if (c->arg == MAP_ATYPE) snprintf(out, (size_t)n, "%s", rack_type_name((module_type_t)c->ui->rack_type));
    else if (c->arg == MAP_AROW) snprintf(out, (size_t)n, "%s", row_name(c->ui->rack_row));
    else snprintf(out, (size_t)n, ">");
}
static bool add_adjust(const ui_ctx_t *c, int dir) {
    synth_ui_t *ui = c->ui;
    if (c->arg == MAP_ATYPE) { ui->rack_type = (ui->rack_type + dir + MOD_TYPE_COUNT) % MOD_TYPE_COUNT; add_row_fix(c); }
    else if (c->arg == MAP_AROW)
        for (int k = 1; k <= RACK_ROWS; k++) {
            const int row = (ui->rack_row + dir * k + 2 * RACK_ROWS) % RACK_ROWS;
            if (add_row_ok(c, row)) { ui->rack_row = row; break; }
        }
    return false;
}
static void add_activate(const ui_ctx_t *c) {
    synth_ui_t *ui = c->ui;
    if (c->arg != MAP_AADD) return;
    const module_type_t t = (module_type_t)ui->rack_type;
    const int row = rack_is_audio(t) ? ui->rack_row : synth_type_is_rack(c->rack->cfg.type) ? 0 : ROW_M;
    const int slot = rack_add_loose(c->rack, row, t);
    if (slot == RACK_NONE) { snprintf(ui->rack_msg, sizeof ui->rack_msg, "No room for %s there", rack_type_code(t)); return; }
    ui->rack_dirty = true;
    char name[8];
    rack_slot_name(c->rack, slot, name, sizeof name);
    snprintf(ui->rack_msg, sizeof ui->rack_msg, "%s added: cable it", name);
    ui->rack_menu = false;
    ui->map_act = MAP_ACT_NONE;
    ui->row = slot + 1;                                          // the cursor on it (its node is its slot)
}

static bool item_enabled(const ui_ctx_t *c) {
    const uint8_t act = c->ui->map_act;
    if (c->arg >= MAP_ATYPE && c->arg <= MAP_AADD) return add_item_enabled(c);
    if (act == MAP_ACT_ADD) return false;
    if (c->arg == MAP_CPRM || c->arg == MAP_CAMT) return cable_item_enabled(c);
    if (c->arg >= MAP_CONNECT) return c->ui->rack_menu && act == MAP_ACT_MENU && graph_item_enabled(c);
    if (act == MAP_ACT_MENU || act == MAP_ACT_CABLE) return false;
    if (act == MAP_ACT_PARAM) return (c->arg == RM_PRM || c->arg == RM_DPTH) && scr_rack_item_enabled(c);   // (a first target: its parameter, its depth)
    return c->arg == MAP_EDIT ? edit_enabled(c) : scr_rack_item_enabled(c);
}
static void item_activate(const ui_ctx_t *c) {
    if (c->arg >= MAP_ATYPE && c->arg <= MAP_AADD) add_activate(c);
    else if (c->arg >= MAP_CONNECT) graph_item_activate(c);
    else if (c->arg == MAP_EDIT) edit_activate(c);
    else scr_rack_item_activate(c);
}
static void item_value(const ui_ctx_t *c, char *out, int n) {
    if (c->arg >= MAP_ATYPE && c->arg <= MAP_AADD) add_value(c, out, n);
    else if (c->arg >= MAP_CONNECT) graph_value(c, out, n);
    else scr_rack_item_value(c, out, n);
}
static bool item_adjust(const ui_ctx_t *c, int dir) {
    if (c->arg >= MAP_ATYPE && c->arg <= MAP_AADD) return add_adjust(c, dir);
    return c->arg >= MAP_CONNECT ? graph_adjust(c, dir) : scr_rack_item_adjust(c, dir);
}

static void item_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t r, bool focused, bool latched) {
    static const char *const graph_label[] = {"Mod to", "Audio to", "Disconnect all", "Delete", "Prm", "Dpth"};
    char label_buf[24];
    static const char *const add_label[] = {"Type", "Row", "Add"};
    const char *label = c->arg == MAP_EDIT ? "Edit" : c->arg < MAP_CONNECT ? scr_rack_item_label(c->arg)
                      : c->arg >= MAP_ATYPE && c->arg <= MAP_AADD ? add_label[c->arg - MAP_ATYPE] : c->arg < MAP_K0 ? graph_label[c->arg - MAP_CONNECT] : label_buf;
    if (c->arg == MAP_CAMT && new_cable(c) && new_cable(c)->kind == CABLE_AUDIO) label = "Lvl";
    const int slot = scr_rack_selected_slot(c);
    out_t o;
    if (c->arg >= MAP_K0) { label_buf[0] = 0; if (slot >= 0 && out_at(c, slot, &o)) out_label(c, slot, o, label_buf, sizeof label_buf); }
    const bool button = (c->arg >= MAP_CONNECT && c->arg <= MAP_DELETE) || c->arg == MAP_EDIT || c->arg == MAP_AADD ||
                        (c->arg < MAP_CONNECT && scr_rack_item_kind(c->arg) == EL_BUTTON);
    char val[24] = "";
    if (button) snprintf(val, sizeof val, ">");
    else item_value(c, val, sizeof val);
    uw_field(g, st, r, label, val, UC_MOD, latched ? GUI_LATCHED : focused ? GUI_FOCUSED : GUI_PLAIN, false);
}

/* ---------------- layout ---------------- */

// Zoomed out, the view along one side (the graph from lo to hi, a view `view` px long, `cam` its start): a graph that fits is centred;
// a bigger one is followed: the view moves only as far as it takes to show the cursor's node (at `at`, `len` long, 2 px around it for
// its outline), and never past the graph's ends.
#define CAM_PAD 2
static void follow(int *cam, int lo, int hi, int view, bool on, int at, int len) {
    lo -= CAM_PAD; hi += CAM_PAD;
    if (hi - lo <= view) { *cam = lo - (view - (hi - lo)) / 2; return; }
    if (on && at - CAM_PAD < *cam) *cam = at - CAM_PAD;
    if (on && at + len + CAM_PAD > *cam + view) *cam = at + len + CAM_PAD - view;
    *cam = *cam < lo ? lo : *cam > hi - view ? hi - view : *cam;
}

static void layout(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t area, gui_rect_t *rect) {
    g_zoom = c->ui->map_zoom < ZOOM_MIN ? ZOOM_MIN : c->ui->map_zoom > ZOOM_MAX ? ZOOM_MAX : c->ui->map_zoom;
    g_m = metrics(area, g_zoom);
    g_m0 = metrics(area, 0);
    g_m1 = metrics(area, -1);
    if (g_zoom >= 0) g_force_set = false;                                          // (back out to -1 later: laid out anew)
    g_map_w = area.w - 2 * TILE_PAD;
    g_map_x = area.x + TILE_PAD;
    g_map_y = area.y + 2;                                                          // (room for the cursor's outline over the top tile)
    rect[E_INFO] = gui_rect(area.x, area.y + area.h - g_m.strip_h, area.w, g_m.strip_h);
    g_view = gui_rect(area.x, area.y, area.w, rect[E_INFO].y - area.y);
    const int room = rect[E_INFO].y - 2 - g_map_y;                                 // the map's height on show (the outline under the lowest tile)
    const nav_graph_t *gr = graph(c);
    const int focus = c->ui->rack_menu ? c->ui->map_menu - 1 : c->ui->row - 1;
    const bool on_node = focus >= 0 && focus < NAV_NODES && gr->node[focus].kind != NODE_NONE;
    g_scroll = 0;                                                                  // a map taller than the room: the part around the cursor (no state)
    if (on_node && gr->node[focus].y + g_m.tile_h > room) g_scroll = gr->node[focus].y + g_m.tile_h - room;
    g_scroll_x = 0;
    if (g_zoom < 0) {                                                              // zoomed out: the graph as big as it grows, the view over it
        const nav_graph_t *p = placed_graph(c);
        int scroll0 = 0;                                                           // (zoom 0's scroll: where its tiles were on the screen)
        if (focus >= 0 && focus < NAV_NODES && p->node[focus].kind != NODE_NONE && p->node[focus].y + g_m0.tile_h > room)
            scroll0 = p->node[focus].y + g_m0.tile_h - room;
        force_update(c, scroll0);
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
        const int f = c->ui->rack_menu ? c->ui->map_menu - 1 : c->ui->row - 1;
        const bool on = f >= 0 && f < NAV_NODES && gr->node[f].kind != NODE_NONE;
        int x0 = 1 << 14, x1 = -(1 << 14), y0 = 1 << 14, y1 = -(1 << 14);
        for (int i = 0; i < NAV_NODES; i++) {
            if (gr->node[i].kind == NODE_NONE) continue;
            const int x = gr->node[i].x, y = gr->node[i].y;
            x0 = x < x0 ? x : x0; y0 = y < y0 ? y : y0;
            x1 = x + g_m.tile_w > x1 ? x + g_m.tile_w : x1; y1 = y + g_m.tile_h > y1 ? y + g_m.tile_h : y1;
        }
        if (x1 > x0) {
            follow(&g_cam_x, x0, x1, g_map_w, on, on ? gr->node[f].x : 0, g_m.tile_w);
            follow(&g_cam_y, y0, y1, room, on, on ? gr->node[f].y : 0, g_m.tile_h);
        }
        g_scroll_x = g_cam_x;
        g_scroll = g_cam_y;
    }
    if (g_zoom > 0) {                                                              // wider than the screen (zoom 1): the cursor's tile in the middle
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
        const bool in_view = (g_zoom < 0 ? t.y + t.h > area.y && t.y < rect[E_INFO].y : t.y >= g_map_y && t.y + t.h <= g_map_y + room) &&
                             t.x + t.w > area.x && t.x < area.x + area.w;           // (cut at the sides; zoomed out at every edge)
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
#define GRAPH_ITEM(e, kind, arg) [e] = {"", kind, 0, 0, 1, item_value, item_adjust, item_activate, item_draw, item_enabled, NULL, arg, \
                                        EF_HIDE_WHEN_DISABLED | EF_SCROLL, {0}}
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
    GRAPH_ITEM(E_G0, EL_BUTTON, MAP_CONNECT), GRAPH_ITEM(E_G0 + 1, EL_BUTTON, MAP_AUDIO),
    GRAPH_ITEM(E_G0 + 2, EL_DIRECT, MAP_K0),     GRAPH_ITEM(E_G0 + 3, EL_DIRECT, MAP_K0 + 1), GRAPH_ITEM(E_G0 + 4, EL_DIRECT, MAP_K0 + 2),
    GRAPH_ITEM(E_G0 + 5, EL_DIRECT, MAP_K0 + 3), GRAPH_ITEM(E_G0 + 6, EL_DIRECT, MAP_K0 + 4), GRAPH_ITEM(E_G0 + 7, EL_DIRECT, MAP_K0 + 5),
    GRAPH_ITEM(E_G0 + 8, EL_DIRECT, MAP_K0 + 6), GRAPH_ITEM(E_G0 + 9, EL_DIRECT, MAP_K0 + 7), GRAPH_ITEM(E_G0 + 10, EL_DIRECT, MAP_K0 + 8),
    GRAPH_ITEM(E_G0 + 11, EL_DIRECT, MAP_K0 + 9),
    GRAPH_ITEM(E_G0 + 12, EL_BUTTON, MAP_UNLINK), GRAPH_ITEM(E_G0 + 13, EL_BUTTON, MAP_DELETE),
    GRAPH_ITEM(E_G0 + 14, EL_VALUE, MAP_CPRM),   GRAPH_ITEM(E_G0 + 15, EL_VALUE, MAP_CAMT),
    GRAPH_ITEM(E_A0, EL_VALUE, MAP_ATYPE),       GRAPH_ITEM(E_A0 + 1, EL_VALUE, MAP_AROW),   GRAPH_ITEM(E_A0 + 2, EL_BUTTON, MAP_AADD),
};
_Static_assert(RACK_MAX == 16 && NAV_NODES == RACK_MAX + 6, "N4 x 4 covers the module nodes, then the MIX, the OUT and the four +");
_Static_assert(E_G0 == E_M0 + 14 && E_A0 == E_G0 + 16 && E_COUNT == E_A0 + 3 && OUT_MAX == 10 && RM_DELETE - RM_TYPE == 12,
               "the menu's items: Edit, then the rack's Type .. Delete, then the graph's Mod to, Audio to, 10 cables, Disconnect all, Delete, Prm, Dpth");

const screen_def_t scr_map_screen = {elements, E_COUNT, false, false, layout, map_back, NULL, draw_extra, node_move, header_push};
