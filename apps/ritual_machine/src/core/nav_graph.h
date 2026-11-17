#pragma once
// The rack as a graph of nodes, for the MAP tab (scr_map.c; apps/ritual_machine/docs/GUI_NODES.md): which node sits where on the map, which
// cables join them, and where the cursor goes from a node. Pure data and rules, no drawing: rebuilt from the rack whenever it is needed (16
// modules, so it costs nothing), it keeps no state of its own and is tested on the desktop (tests/ui/test_map.c).
//
// Nodes: node i < RACK_MAX is the module in slot i (kind NODE_NONE when the rack has no such slot); NAV_MIX is the MIX where the two branches
// meet (Modular only) and NAV_OUT the output; then the empty places where a module can be added (NODE_PLUS, while the rack has room): the end
// of branch 1, of branch 2, of row M, and the next place among the modulators. So a node's index is also its element number in the screen's table.
// Layout: the signal runs down. Branch 1 is column 0 and branch 2 column 1, from the top; the MIX comes under the longer one, then row M's
// modules, then OUT, all in column 0. The modulators (not in the audio chain) stand in columns 2 and 3, from the top, two by two. FM and
// Strings have no branches: their row M chain starts at the top of column 0, and the modules left in the branches (they do not play there) are
// not on the map. A chain's "+" is under its last module.
#include <stdint.h>
#include "core/rack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NAV_MIX   RACK_MAX
#define NAV_OUT   (RACK_MAX + 1)
#define NAV_PLUS1 (RACK_MAX + 2)        // the "+" at the end of branch 1 (row 0)
#define NAV_PLUS2 (RACK_MAX + 3)        // ... branch 2 (row 1)
#define NAV_PLUSM (RACK_MAX + 4)        // ... row M
#define NAV_PLUS_MOD (RACK_MAX + 5)     // the next place among the modulators
#define NAV_NODES (RACK_MAX + 6)
#define NAV_COLS  4
#define NAV_EDGES 48

typedef enum { NODE_NONE, NODE_MODULE, NODE_MIX, NODE_OUT, NODE_PLUS } nav_kind_t;
typedef enum { EDGE_AUDIO, EDGE_MOD } nav_edge_kind_t;

typedef struct { uint8_t kind; int8_t slot; int8_t col, depth; int8_t row; } nav_node_t;   // slot: the module (NODE_MODULE), else -1; row: the
                                                                                    // rack row it is in (a module, a "+"), ROW_M for the MIX and the OUT
typedef struct { int8_t from, to; uint8_t kind; } nav_edge_t;                         // nodes; a modulation cable runs from the modulator to its target

typedef struct {
    nav_node_t node[NAV_NODES];
    nav_edge_t edge[NAV_EDGES];
    int        n_edges;
    int        rows;                // the map's depth: one more than the deepest node
} nav_graph_t;

void nav_build(nav_graph_t *g, const rack_t *r);

// Where the cursor goes from node `cur` (-1: the header, for 'd'). dir: 'u', 'd', 'l', 'r'. Returns a node, -1 for the header (up from the top
// of a column), or `cur` when there is nowhere to go. Down and up follow a column (a branch's last module continues into the MIX; the MIX goes
// up into the longer branch); left and right go to the nearest column on that side, to the node at about the same depth.
int nav_move(const nav_graph_t *g, int cur, char dir);

#ifdef __cplusplus
}
#endif
