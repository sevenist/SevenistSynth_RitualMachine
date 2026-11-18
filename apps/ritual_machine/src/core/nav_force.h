#pragma once
// The map zoomed out (scr_map.c, zoom -1 and -2; the user's idea, 2026-11-18: "the nodes rearrange themselves into a graph like Obsidian's,
// the links attracting, audio in / out, FM and pitch strongly, the others weakly"): a small force simulation over the graph's nodes. Every
// node pushes the others away, each cable is a spring and a weak pull toward a centre keeps the graph together. No walls (the user,
// 2026-11-18): the graph takes the room it needs and the screen scrolls over it (scr_map.c). It runs a step at a time (the screen draws the steps: the animation) and stops once the nodes have settled (it cools down: at
// most 300 steps, about 5 s on the screen). Pure data and rules,
// no drawing, no clock: tested on the desktop (tests/ui/test_map.c).
#include <stdbool.h>
#include <stdint.h>
#include "core/nav_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NAV_SPRINGS   NAV_EDGES
#define NAV_STRONG    0.06f             // a spring's pull: audio, FM, a pitch modulation
#define NAV_WEAK      0.015f            // ... any other modulation

typedef struct { int8_t a, b; float k; } nav_spring_t;      // a cable between nodes a and b, its pull

typedef struct {
    float x[NAV_NODES], y[NAV_NODES], vx[NAV_NODES], vy[NAV_NODES];   // the nodes' centres and speeds (px, px per step), from the room's top left
    bool  on[NAV_NODES];                                         // on the graph (the screen puts the modules, the MIX and the OUT, never a "+")
    nav_spring_t spring[NAV_SPRINGS];
    int   n_springs;
    float cx, cy;                                                // the centre it is pulled toward
    float hw, hh;                                                // a node's half width and half height
    float rest, push;                                            // a spring's length at rest; how hard two nodes push each other away
    int   calm, steps;                                           // steps in a row with every node slow; steps since it was last set going
    bool  moving;                                                // false once settled (then a step does nothing)
} nav_force_t;

// An empty graph pulled toward (cx, cy), for nodes of hw x hh half sizes.
void nav_force_init(nav_force_t *f, float cx, float cy, float hw, float hh, float rest, float push);
void nav_force_put(nav_force_t *f, int node, float x, float y);   // a node on the graph, still, at (x, y)
void nav_force_spring(nav_force_t *f, int a, int b, float k);     // a cable (both ends on the graph)
void nav_force_go(nav_force_t *f);                                // set it moving again (after a change)
bool nav_force_step(nav_force_t *f);                              // one step; false once settled

#ifdef __cplusplus
}
#endif
