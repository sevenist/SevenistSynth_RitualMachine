// The MAP tab (core/nav_graph.[ch], core/scr_map.c): the rack as nodes. The graph (where each module sits, which cables join them), where the
// cursor goes, what a push does, and that the screen draws on every rack and every height without trouble.
#include "ui_test.h"
#include "app/app_internal.h"
#include "core/nav_graph.h"
#include "core/ui_screen.h"
#include "core/ui_theme.h"

// Two branches, a modulator, row M:  slot 0 OSC and 1 FILTER (branch 1), 2 OSC (branch 2), 3 LFO -> the filter's cutoff, 4 DELAY, 5 REVERB (row M).
static void sample_rack(rack_t *r) {
    rack_clear(r);
    rack_insert_row(r, 0, 0, MOD_OSC);
    rack_insert_row(r, 1, 0, MOD_FILTER);
    rack_insert_row(r, 2, 1, MOD_OSC);
    rack_insert_row(r, 3, 0, MOD_LFO);
    rack_insert_row(r, 4, ROW_M, MOD_DELAY);
    rack_insert_row(r, 5, ROW_M, MOD_REVERB);
    r->slot[3].tgt_id = r->slot[1].id;
    r->slot[3].tgt_param = MP_FL_CUT;
}

static bool has_edge(const nav_graph_t *g, int from, int to, nav_edge_kind_t k) {
    for (int e = 0; e < g->n_edges; e++) if (g->edge[e].from == from && g->edge[e].to == to && g->edge[e].kind == k) return true;
    return false;
}

TEST(the_graph_puts_each_branch_in_a_column_the_mix_under_them_and_the_modulators_aside) {
    rack_t r;
    nav_graph_t g;
    sample_rack(&r);
    CHECK_EQ(r.count, 6);
    nav_build(&g, &r);
    CHECK_EQ(g.node[0].col, 0);  CHECK_EQ(g.node[0].depth, 0);                 // branch 1, from the top
    CHECK_EQ(g.node[1].col, 0);  CHECK_EQ(g.node[1].depth, 1);
    CHECK_EQ(g.node[2].col, 1);  CHECK_EQ(g.node[2].depth, 0);                 // branch 2
    CHECK_EQ(g.node[3].col, 2);  CHECK_EQ(g.node[3].depth, 0);                 // the modulator
    CHECK_EQ(g.node[NAV_PLUS1].kind, NODE_PLUS);                                // the empty places: the end of each branch ...
    CHECK_EQ(g.node[NAV_PLUS1].col, 0); CHECK_EQ(g.node[NAV_PLUS1].depth, 2);
    CHECK_EQ(g.node[NAV_PLUS2].col, 1); CHECK_EQ(g.node[NAV_PLUS2].depth, 1);
    CHECK_EQ(g.node[NAV_PLUS_MOD].col, 3); CHECK_EQ(g.node[NAV_PLUS_MOD].depth, 0);   // ... the next modulator's
    CHECK_EQ(g.node[NAV_MIX].kind, NODE_MIX);
    CHECK_EQ(g.node[NAV_MIX].depth, 3);                                         // under the longer branch and its +
    CHECK_EQ(g.node[4].depth, 4);  CHECK_EQ(g.node[5].depth, 5);                // row M under the MIX
    CHECK_EQ(g.node[NAV_PLUSM].depth, 6);  CHECK_EQ(g.node[NAV_PLUSM].row, ROW_M);
    CHECK_EQ(g.node[NAV_OUT].depth, 7);
    CHECK_EQ(g.rows, 8);
    CHECK_EQ(g.node[6].kind, NODE_NONE);                                        // no slot 6
    CHECK(has_edge(&g, 0, 1, EDGE_AUDIO));
    CHECK(has_edge(&g, 1, NAV_MIX, EDGE_AUDIO));
    CHECK(has_edge(&g, 2, NAV_MIX, EDGE_AUDIO));
    CHECK(has_edge(&g, NAV_MIX, 4, EDGE_AUDIO));
    CHECK(has_edge(&g, 4, 5, EDGE_AUDIO));
    CHECK(has_edge(&g, 5, NAV_OUT, EDGE_AUDIO));
    CHECK(has_edge(&g, 3, 1, EDGE_MOD));                                        // the LFO's cable to the filter
    CHECK(!has_edge(&g, 3, 1, EDGE_AUDIO));
}

TEST(a_synth_without_branches_has_one_chain_and_no_mix) {
    rack_t r;
    nav_graph_t g;
    rack_clear(&r);
    r.cfg.type = SYNTH_FM;
    rack_add_m(&r, MOD_DELAY);
    rack_add_m(&r, MOD_REVERB);
    nav_build(&g, &r);
    CHECK_EQ(g.node[NAV_MIX].kind, NODE_NONE);
    CHECK_EQ(g.node[0].depth, 0);  CHECK_EQ(g.node[1].depth, 1);
    CHECK_EQ(g.node[NAV_PLUSM].depth, 2);
    CHECK_EQ(g.node[NAV_PLUS1].kind, NODE_NONE);                                // no branches: no branch's +
    CHECK_EQ(g.node[NAV_OUT].depth, 3);
    CHECK(has_edge(&g, 1, NAV_OUT, EDGE_AUDIO));
}

TEST(the_cursor_follows_the_cables_and_the_columns) {
    rack_t r;
    nav_graph_t g;
    sample_rack(&r);
    nav_build(&g, &r);
    CHECK_EQ(nav_move(&g, 0, 'd'), 1);
    CHECK_EQ(nav_move(&g, 1, 'd'), NAV_PLUS1);                                  // the branch's +, then the MIX
    CHECK_EQ(nav_move(&g, NAV_PLUS1, 'd'), NAV_MIX);
    CHECK_EQ(nav_move(&g, 2, 'd'), NAV_PLUS2);
    CHECK_EQ(nav_move(&g, NAV_PLUS2, 'd'), NAV_MIX);                            // the end of branch 2 goes on into the MIX, across
    CHECK_EQ(nav_move(&g, NAV_MIX, 'u'), NAV_PLUS1);                            // back up into the longer branch
    CHECK_EQ(nav_move(&g, 0, 'u'), -1);                                         // the top of a column: the header
    CHECK_EQ(nav_move(&g, -1, 'd'), 0);                                         // from the header: the top left
    CHECK_EQ(nav_move(&g, 0, 'r'), 2);                                          // the next column, the same depth
    CHECK_EQ(nav_move(&g, 2, 'r'), 3);
    CHECK_EQ(nav_move(&g, 3, 'r'), NAV_PLUS_MOD);
    CHECK_EQ(nav_move(&g, 3, 'l'), 2);
    CHECK_EQ(nav_move(&g, 2, 'l'), 0);
    CHECK_EQ(nav_move(&g, 1, 'r'), NAV_PLUS2);                                  // from the deeper node of column 0: the same depth over there
    CHECK_EQ(nav_move(&g, NAV_MIX, 'l'), NAV_MIX);                              // nowhere to go
    CHECK_EQ(nav_move(&g, NAV_OUT, 'd'), NAV_OUT);
    CHECK_EQ(nav_move(&g, 4, 'u'), NAV_MIX);
    CHECK_EQ(nav_move(&g, NAV_MIX, 'd'), 4);
}

// The tab, on the app's own rack and screen.
static void open_map(void) {
    ui_fresh();
    ui_app.ui.in_rack = true;
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_RACK);
    ui_app.ui.row = 0;
}
#define MENU_ROW(i) (NAV_NODES + 1 + (i) + 1)          // ui->row of the menu's item i (0 Edit, 1 Type, 2 Insert ... 13 Delete: scr_map.c)
static ui_ctx_t ctx(void) { return (ui_ctx_t){&ui_app.ui, &ui_app.rack, 0, &ui_app.params, &ui_app.seq}; }

TEST(the_rack_tab_is_the_map) {
    ui_fresh();
    CHECK(screen_for_tab(TAB_RACK) == &scr_map_screen);
    CHECK_EQ(tab_count(&ui_app.rack), 10);
    CHECK(tab_index_of(&ui_app.rack, TAB_MAP) < 0);                             // (a tab of its own for a day: no longer)
    CHECK(screen_for_tab(TAB_MAP) == &scr_map_screen);                          // a jump slot saved on it still lands on the map
}

TEST(the_joystick_walks_the_nodes_and_a_push_opens_the_nodes_page) {
    open_map();
    const ui_ctx_t c = ctx();
    screen_event(&scr_map_screen, &c, UI_NAV_DOWN);                              // from the header to the first node
    CHECK_EQ(ui_app.ui.row, 1);
    screen_event(&scr_map_screen, &c, UI_NAV_DOWN);                              // down the column: the next module of the branch
    CHECK(ui_app.ui.row >= 2);
    CHECK(ui_app.ui.row - 1 < ui_app.rack.count);
    screen_event(&scr_map_screen, &c, UI_NAV_UP);
    screen_event(&scr_map_screen, &c, UI_NAV_UP);
    CHECK_EQ(ui_app.ui.row, 0);                                                   // up from the top: the header
    screen_event(&scr_map_screen, &c, UI_NAV_DOWN);
    screen_event(&scr_map_screen, &c, UI_LATCH);                                  // a push: the node's menu, Edit first
    CHECK(ui_app.ui.rack_menu);
    CHECK_EQ(ui_app.ui.map_menu, 1);
    CHECK_EQ(ui_app.ui.row, MENU_ROW(0));
    screen_event(&scr_map_screen, &c, UI_LATCH);                                  // Edit: that node's page, on its values
    CHECK(!ui_app.ui.rack_menu);
    CHECK(ui_app.ui.in_rack);
    CHECK_EQ(ui_app.ui.map_at, 1);
    CHECK_EQ(ui_app.ui.map_page, 0);
    CHECK(screen_shown(&ui_app.ui, &ui_app.rack, TAB_MAP) == &scr_node_screen);
    CHECK_EQ(ui_app.ui.row, 4);                                                   // the first value row
    CHECK(scr_node_screen.back(&c));                                              // Back: the map, the cursor on that node
    CHECK_EQ(ui_app.ui.map_at, 0);
    CHECK_EQ(ui_app.ui.row, 1);
    CHECK(screen_shown(&ui_app.ui, &ui_app.rack, TAB_MAP) == &scr_map_screen);
}

// The sample rack in the app, the MAP tab on show.
static void map_on_sample_rack(void) {
    open_map();
    sample_rack(&ui_app.rack);
    synth_ui_rebuild_pages(&ui_app.ui, &ui_app.rack);
    ui_app.ui.in_rack = true;
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_RACK);
}
static bool ev(ui_event_t e) { const ui_ctx_t c = ctx(); return screen_event(screen_shown(&ui_app.ui, &ui_app.rack, TAB_MAP), &c, e); }

TEST(a_node_page_edits_its_values_and_follows_its_cables) {
    map_on_sample_rack();
    ui_app.ui.row = 2;                                                             // the filter (node 1): its menu, Edit
    ev(UI_LATCH);
    ev(UI_LATCH);
    CHECK_EQ(ui_app.ui.map_at, 2);
    ui_app.ui.row = 5;                                                             // its second value: the cutoff (element 3 + 1, row + 1)
    const float before = ui_app.rack.slot[1].v[MP_FL_CUT];
    ev(UI_LATCH);                                                                  // a push latches it
    CHECK(ui_app.ui.latched);
    CHECK(ev(UI_NAV_RIGHT));                                                       // the joystick changes it: a sound value changed
    CHECK(ui_app.rack.slot[1].v[MP_FL_CUT] > before);
    ui_app.ui.latched = false;
    ui_app.ui.row = 2;                                                             // the tabs: to PORTS
    ev(UI_NAV_RIGHT);
    CHECK_EQ(ui_app.ui.map_page, 1);
    // its ports: in <- OC1, out -> MIX, the LFO's modulation, "+ modulation"
    int shown = 0;
    for (int i = 0; i < 10; i++) { const ui_ctx_t x = {&ui_app.ui, &ui_app.rack, i, &ui_app.params, &ui_app.seq}; shown += scr_node_screen.el[23 + i].enabled(&x); }
    CHECK_EQ(shown, 4);
    ui_app.ui.row = 23 + 3;                                                        // the modulation row: left / right change the LFO's depth
    const float d = ui_app.rack.slot[3].v[rack_depth_index(MOD_LFO)];
    CHECK(ev(UI_NAV_RIGHT));
    CHECK(ui_app.rack.slot[3].v[rack_depth_index(MOD_LFO)] != d);
    ui_app.ui.row = 23 + 1;                                                        // in <- OC1: a push follows the cable
    ev(UI_LATCH);
    CHECK_EQ(ui_app.ui.map_at, 1);
    CHECK_EQ(ui_app.ui.map_page, 1);                                               // ... still on the ports
}

TEST(the_pick_list_connects_a_modulator_to_a_parameter) {
    map_on_sample_rack();
    ui_app.ui.row = 1;                                                             // the first oscillator: its menu, Edit
    ev(UI_LATCH);
    ev(UI_LATCH);
    ui_app.ui.map_page = 1;
    // its ports: in --, out -> FL1, "+ modulation"
    ui_app.ui.row = 23 + 3;
    ev(UI_LATCH);
    CHECK(ui_app.ui.map_pick);
    CHECK(screen_shown(&ui_app.ui, &ui_app.rack, TAB_MAP) == &scr_pick_screen);
    CHECK(rack_depth_unit(MOD_OSC, ui_app.ui.pick_param) != NULL);                // "To": a parameter a modulator can drive
    // the sources: OC2 (slot 2) and LF1 (slot 3); LF1 is the second
    ui_app.ui.rack_dirty = false;
    ui_app.ui.row = 3 + 1 + 1;
    ev(UI_LATCH);
    CHECK_EQ(ui_app.rack.slot[3].tgt_id, ui_app.rack.slot[0].id);
    CHECK(ui_app.ui.rack_dirty);                                                  // applied when the menu closes, as every structural edit
    CHECK(!ui_app.ui.map_pick);
    CHECK_EQ(ui_app.ui.map_page, 1);
}

TEST(the_node_pages_draw_on_every_module_and_both_tabs) {
    map_on_sample_rack();
    for (int n = 0; n < ui_app.rack.count; n++) {
        ui_app.ui.map_at = (uint8_t)(n + 1);
        for (int page = 0; page < 2; page++) {
            ui_app.ui.map_page = (uint8_t)page;
            for (int row = 0; row < 34; row += 3) { ui_app.ui.row = row; app_draw_frame(&ui_app); }
        }
    }
    ui_app.ui.map_at = 2; ui_app.ui.map_pick = true;
    for (int row = 0; row < 19; row++) { ui_app.ui.row = row; app_draw_frame(&ui_app); }
    ui_app.ui.map_at = 40;                                                         // a node that is no longer there: the map
    CHECK(screen_shown(&ui_app.ui, &ui_app.rack, TAB_MAP) == &scr_map_screen);
}

// On a colour board the app draws into a canvas (core/ui_theme.h): the map's tiles carry their family's colour, nothing else changes colour.
// Both colour modes (UI_RGB: an RGB565 canvas, UI_PALETTE: an indexed one) must give the same colours.
static void colours_on(canvas_t *cv) {
    map_on_sample_rack();
    ui_colour_attach(cv);
    u8g2_t *g = canvas_u8g2(cv);
    CHECK(ui_colour(g));
    CHECK(!ui_colour(ui_app.display));
    ui_app.ui.row = 0;
    synth_ui_draw(&ui_app.ui, &ui_app.params, &ui_app.seq, &ui_app.rack, g);
    CHECK_EQ(canvas_get(cv, 2, 22), ui_rgb(UC_SRC));                               // OC1: the source's green band, top left
    CHECK_EQ(canvas_get(cv, 2 + 32, 22), ui_rgb(UC_SRC));                          // OC2, branch 2
    CHECK_EQ(canvas_get(cv, 2, 22 + 16), ui_rgb(UC_SHP));                          // FL1 under OC1: a shaper's violet
    CHECK_EQ(canvas_get(cv, 2 + 64, 22), ui_rgb(UC_MOD));                          // LF1, a modulator's amber
    CHECK_EQ(canvas_get(cv, 127, 100), ui_rgb(UC_BG));                             // the background
    ui_app.ui.map_at = 2;                                                          // the filter's page: its title in the shaper's colour
    ui_app.ui.row = 4;
    synth_ui_draw(&ui_app.ui, &ui_app.params, &ui_app.seq, &ui_app.rack, g);
    CHECK_EQ(canvas_get(cv, 1, 18), ui_rgb(UC_SHP));
    ui_colour_attach(NULL);
    CHECK(!ui_colour(g));
}

TEST(in_colour_a_tile_carries_its_familys_band_rgb565) {
    static uint16_t px[128 * 160];
    static canvas_t cv;
    CHECK(canvas_init(&cv, px, 128, 160));
    colours_on(&cv);
}

TEST(in_colour_a_tile_carries_its_familys_band_palette) {
    static uint8_t px[128 * 160];
    static canvas_t cv;
    CHECK(canvas_init_indexed(&cv, px, 128, 160, ui_palette()));
    colours_on(&cv);
    for (int i = 0; i < 128 * 160; i++) if (px[i] >= UC_COUNT) { CHECK(px[i] < UC_COUNT); break; }   // only the theme's colours
}

TEST(the_map_draws_on_every_rack_and_the_cursor_can_reach_every_node) {
    open_map();
    // a full rack: branch 1 as deep as the rack allows, then the same with the modulators
    rack_clear(&ui_app.rack);
    for (int i = 0; i < RACK_MAX - 3; i++) rack_insert_row(&ui_app.rack, ui_app.rack.count, 0, i ? MOD_FILTER : MOD_OSC);
    rack_insert_row(&ui_app.rack, ui_app.rack.count, 1, MOD_OSC);
    rack_insert_row(&ui_app.rack, ui_app.rack.count, 0, MOD_LFO);
    rack_insert_row(&ui_app.rack, ui_app.rack.count, ROW_M, MOD_DELAY);
    synth_ui_rebuild_pages(&ui_app.ui, &ui_app.rack);
    ui_app.ui.in_rack = true;
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_RACK);
    nav_graph_t g;
    nav_build(&g, &ui_app.rack);
    int drawn = 0;
    for (int i = 0; i < NAV_NODES; i++) {
        if (g.node[i].kind == NODE_NONE) continue;
        ui_app.ui.row = i + 1;
        app_draw_frame(&ui_app);                                                  // (the rows around the cursor: it may sit deeper than the screen)
        drawn++;
    }
    CHECK(drawn >= RACK_MAX);
    ui_app.ui.row = 0;
    app_draw_frame(&ui_app);
}

/* ---------------- editing on the map: the rack's menu on its nodes ---------------- */

static int find_slot(module_type_t t, int from) { for (int i = from; i < ui_app.rack.count; i++) if (ui_app.rack.slot[i].type == t) return i; return -1; }
static void focus_item(int i) { ui_app.ui.row = MENU_ROW(i); }

TEST(a_plus_at_the_end_of_a_branch_inserts_a_module_there) {
    map_on_sample_rack();
    const int count = ui_app.rack.count;
    ui_app.ui.row = NAV_PLUS1 + 1;
    ev(UI_LATCH);                                                                  // the +'s menu: Type and Insert (no Edit: no module)
    CHECK(ui_app.ui.rack_menu);
    CHECK_EQ(ui_app.ui.row, MENU_ROW(1));
    CHECK(!rack_is_modulator((module_type_t)ui_app.ui.rack_type));                // a type that fits a branch
    ui_app.ui.rack_type = MOD_SAT;
    ev(UI_NAV_DOWN);                                                               // down to Insert
    CHECK_EQ(ui_app.ui.row, MENU_ROW(2));
    ev(UI_LATCH);
    CHECK_EQ(ui_app.rack.count, count + 1);
    const int sa = find_slot(MOD_SAT, 0);                                          // in branch 1, after its filter (its last module: the LFO, row 0 too)
    CHECK(sa >= 0);
    if (sa < 0) return;
    CHECK_EQ(ui_app.rack.slot[sa].row, 0);
    CHECK_EQ(rack_audio_prev(&ui_app.rack, sa), 1);
    CHECK(ui_app.ui.rack_dirty);
    CHECK(!ui_app.ui.rack_menu);
    CHECK_EQ(ui_app.ui.row, sa + 1);                                               // the cursor on the new module
}

TEST(the_modulators_plus_offers_a_modulator) {
    map_on_sample_rack();
    ui_app.ui.rack_type = MOD_OSC;
    ui_app.ui.row = NAV_PLUS_MOD + 1;
    ev(UI_LATCH);
    CHECK(ui_app.ui.rack_menu);
    CHECK(rack_is_modulator((module_type_t)ui_app.ui.rack_type));
    const int count = ui_app.rack.count;
    focus_item(2);
    ev(UI_LATCH);
    CHECK_EQ(ui_app.rack.count, count + 1);
    nav_graph_t g;
    nav_build(&g, &ui_app.rack);
    CHECK(ui_app.ui.row - 1 < ui_app.rack.count);
    CHECK(g.node[ui_app.ui.row - 1].col >= 2);                                     // it stands among the modulators
}

TEST(insert_on_a_module_puts_the_new_one_before_it_and_delete_removes_it) {
    map_on_sample_rack();
    ui_app.ui.row = 2;                                                             // FL1 (slot 1)
    ev(UI_LATCH);
    focus_item(1);                                                                 // Type: to a saturator
    for (int k = 0; k < MOD_TYPE_COUNT && ui_app.ui.rack_type != MOD_SAT; k++) ev(UI_NAV_RIGHT);
    CHECK_EQ(ui_app.ui.rack_type, MOD_SAT);
    focus_item(2);
    ev(UI_LATCH);                                                                  // Insert: before the filter
    CHECK_EQ(ui_app.rack.slot[1].type, MOD_SAT);
    CHECK_EQ(ui_app.rack.slot[2].type, MOD_FILTER);
    CHECK_EQ(ui_app.ui.row, 2);                                                    // the cursor on it
    const int count = ui_app.rack.count;
    ev(UI_LATCH);                                                                  // its menu again: Delete is the last item
    focus_item(13);
    ev(UI_LATCH);
    CHECK_EQ(ui_app.rack.count, count - 1);
    CHECK_EQ(ui_app.rack.slot[1].type, MOD_FILTER);
    CHECK(!ui_app.ui.rack_menu);
    CHECK(find_slot(MOD_SAT, 0) < 0);
}

TEST(the_mix_menu_sets_the_branches_levels) {
    map_on_sample_rack();
    ui_app.ui.row = NAV_MIX + 1;
    ev(UI_LATCH);
    CHECK(ui_app.ui.rack_menu);
    CHECK_EQ(ui_app.ui.row, MENU_ROW(9));                                          // Lvl1 first: the MIX has no Edit, Type, Delete
    ev(UI_NAV_LEFT); ev(UI_NAV_LEFT);
    CHECK(ui_app.rack.br_lvl[0] < 0.95f);
}

TEST(back_closes_the_nodes_menu_and_never_deletes) {
    map_on_sample_rack();
    const int count = ui_app.rack.count;
    ui_app.ui.row = 2;
    ev(UI_LATCH);
    CHECK(ui_app.ui.rack_menu);
    const ui_ctx_t c = ctx();
    CHECK(scr_map_screen.back(&c));
    CHECK(!ui_app.ui.rack_menu);
    CHECK_EQ(ui_app.ui.row, 2);                                                    // back on the node
    CHECK_EQ(ui_app.rack.count, count);
    CHECK(!scr_map_screen.back(&c));                                               // no menu: Back goes up (ui_input.c)
}

TEST(the_menu_and_the_plus_draw_on_every_node) {
    map_on_sample_rack();
    for (int n = 0; n < NAV_NODES; n++) {
        ui_app.ui.rack_menu = false; ui_app.ui.map_menu = 0;
        ui_app.ui.row = n + 1;
        app_draw_frame(&ui_app);
        ev(UI_LATCH);
        app_draw_frame(&ui_app);
        if (ui_app.ui.rack_menu) { const ui_ctx_t c = ctx(); scr_map_screen.back(&c); }
    }
}
