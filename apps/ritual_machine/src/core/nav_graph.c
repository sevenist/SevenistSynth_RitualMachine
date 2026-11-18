#include "core/nav_graph.h"
#include <string.h>

static void add_edge(nav_graph_t *g, int from, int to, nav_edge_kind_t kind) {
    if (g->n_edges >= NAV_EDGES) return;
    g->edge[g->n_edges++] = (nav_edge_t){(int8_t)from, (int8_t)to, (uint8_t)kind};
}

void nav_build(nav_graph_t *g, const rack_t *r) {
    memset(g, 0, sizeof *g);                                    // NODE_NONE is 0
    const bool branches = synth_type_is_rack(r->cfg.type) && !r->fx;
    int row_of[RACK_MAX] = {0};                                 // the chain a module is in (everything is row M without branches)
    int d[RACK_ROWS] = {0};                                     // the next depth of each chain
    int n_mod = 0;
    for (int i = 0; i < r->count && i < RACK_MAX; i++) {
        row_of[i] = -1;
        if (!branches && r->slot[i].row != ROW_M) continue;   // FM, Strings: the branches' modules stay in the rack but do not play (not shown)
        nav_node_t *n = &g->node[i];
        n->kind = NODE_MODULE;
        n->slot = (int8_t)i;
        n->row = (int8_t)r->slot[i].row;
        if (rack_slot_is_audio(r, i)) {
            const int row = r->slot[i].row;
            row_of[i] = row;
            n->col = row == 1 ? 1 : 0;
            n->depth = (int8_t)d[row]++;
        } else {
            row_of[i] = -1;
            n->col = (int8_t)(2 + (n_mod & 1));
            n->depth = (int8_t)(n_mod / 2);
            n_mod++;
        }
    }
    const bool room = r->count < RACK_MAX;                      // the "+" places: the end of each chain, the next modulator's
    const int add = room ? 1 : 0;
    if (room && branches) {
        g->node[NAV_PLUS1] = (nav_node_t){NODE_PLUS, -1, 0, (int8_t)d[0], 0};
        g->node[NAV_PLUS2] = (nav_node_t){NODE_PLUS, -1, 1, (int8_t)d[1], 1};
    }
    if (room) g->node[NAV_PLUS_MOD] = (nav_node_t){NODE_PLUS, -1, (int8_t)(2 + (n_mod & 1)), (int8_t)(n_mod / 2), (int8_t)(branches ? 0 : ROW_M)};
    const int mix_depth = branches ? (d[0] > d[1] ? d[0] : d[1]) + add : 0;
    const int m_base = branches ? mix_depth + 1 : 0;            // row M starts under the MIX
    for (int i = 0; i < r->count && i < RACK_MAX; i++) if (row_of[i] == ROW_M) g->node[i].depth = (int8_t)(g->node[i].depth + m_base);
    if (branches) g->node[NAV_MIX] = (nav_node_t){NODE_MIX, -1, 0, (int8_t)mix_depth, ROW_M};
    if (room) g->node[NAV_PLUSM] = (nav_node_t){NODE_PLUS, -1, 0, (int8_t)(m_base + d[ROW_M]), ROW_M};
    g->node[NAV_OUT] = (nav_node_t){NODE_OUT, -1, 0, (int8_t)(m_base + d[ROW_M] + add), ROW_M};
    for (int i = 0; i < NAV_NODES; i++) if (g->node[i].kind && g->node[i].depth + 1 > g->rows) g->rows = g->node[i].depth + 1;

    // the audio cables: each chain ends in the MIX (a branch) or the output (row M); the MIX leads into row M
    int first_m = NAV_OUT;
    for (int i = 0; i < r->count && i < RACK_MAX; i++) if (row_of[i] == ROW_M) { first_m = i; break; }
    for (int i = 0; i < r->count && i < RACK_MAX; i++) {
        if (row_of[i] < 0 || r->slot[i].cut) continue;                          // (a cut chain cable: none)
        const int next = rack_audio_next(r, i);
        add_edge(g, i, next >= 0 ? next : (row_of[i] == ROW_M ? NAV_OUT : NAV_MIX), EDGE_AUDIO);
    }
    if (branches) add_edge(g, NAV_MIX, first_m, EDGE_AUDIO);

    // the modulation cables: a modulator (or an oscillator with a target) to its target; a motion sequencer has one per lane
    for (int i = 0; i < r->count && i < RACK_MAX; i++) {
        const rack_slot_t *s = &r->slot[i];
        if (s->type == MOD_MSEQ) {
            const ms_pattern_t *p = rack_ms_const(r, i);
            for (int k = 0; p && k < MS_LANES; k++) {
                const int ti = p->lane[k].tgt_id ? rack_find(r, p->lane[k].tgt_id) : RACK_NONE;
                if (ti != RACK_NONE) add_edge(g, i, ti, EDGE_MOD);
            }
        } else if (rack_can_target((module_type_t)s->type) && s->tgt_id) {
            const int ti = rack_find(r, s->tgt_id);
            if (ti != RACK_NONE) add_edge(g, i, ti, EDGE_MOD);
        }
    }
    for (int k = 0; k < r->n_cables; k++) {                                     // the cables beyond the chains and the first targets
        const int a = rack_find(r, r->cable[k].from), b = rack_find(r, r->cable[k].to);
        if (a != RACK_NONE && b != RACK_NONE && a < RACK_MAX && b < RACK_MAX) add_edge(g, a, b, r->cable[k].kind == CABLE_AUDIO ? EDGE_AUDIO : EDGE_MOD);
    }
}

static bool shown(const nav_graph_t *g, int i) { return g->node[i].kind != NODE_NONE; }

/* ---------------- the floating layout ---------------- */

// A spring across: node b wants to sit `dx` right of node a (and a `dx` left of b); w its strength.
typedef struct { int8_t a, b; float dx, w; } link_t;
#define NAV_LINKS (NAV_EDGES + NAV_NODES)
#define SEED_W    0.01f                  // the pull toward the grid's column (weak: it only settles what the cables leave free)
#define MOD_W     0.7f                   // a modulator's pull toward what it modulates (the audio links: 1)
#define PASSES    60

static bool in_chain(const nav_node_t *n) { return (n->kind == NODE_MODULE || n->kind == NODE_PLUS) && n->col < 2; }
static bool is_modulator(const nav_node_t *n) { return n->kind == NODE_MODULE && n->col >= 2; }

// The chains, each in depth order: branch 1, branch 2, row M (a "+" ends its chain).
static void chains(const nav_graph_t *g, int seq[3][NAV_NODES], int len[3]) {
    static const int rows[3] = {0, 1, ROW_M};
    for (int k = 0; k < 3; k++) {
        len[k] = 0;
        for (int d = 0; d <= g->rows; d++)
            for (int i = 0; i < NAV_NODES; i++)
                if (in_chain(&g->node[i]) && g->node[i].row == rows[k] && g->node[i].depth == d) seq[k][len[k]++] = i;
    }
}

static int in_layer(const int *layer, int l) { int n = 0; for (int i = 0; i < NAV_NODES; i++) n += layer[i] == l; return n; }

// The free layer nearest `want` (below first on a tie), for a node beside the chains.
static int free_layer(const int *layer, int want, int room) {
    for (int d = 0; d < 2 * NAV_NODES; d++) {
        if (in_layer(layer, want + d) < room) return want + d;
        if (want - d >= 0 && in_layer(layer, want - d) < room) return want - d;
    }
    return want;
}

// A unit moves as one: a chain (its column, all its nodes on one x: NAV_NODES + 0 branch 1, + 1 the MIX and row M, + 2 branch 2), or a
// node alone (a modulator, a "+" among the modulators).
#define UNITS (NAV_NODES + 3)

static int i_min(int a, int b) { return a < b ? a : b; }

// The tiles of each layer, in their fixed order left to right (`key`, then the node), no closer than `span`: two units too close are pushed
// apart, half each. The order never changes (a node pulled across another would swap with it and tangle the cables).
static void separate(const nav_graph_t *g, const int *layer, int layers, const int *key, const int *unit, float *ux, float span) {
    for (int l = 0; l < layers; l++) {
        int at[NAV_NODES], n = 0;
        for (int i = 0; i < NAV_NODES; i++) if (shown(g, i) && layer[i] == l) at[n++] = i;
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && key[at[j - 1]] > key[at[j]]; j--) { const int t = at[j - 1]; at[j - 1] = at[j]; at[j] = t; }
        for (int k = 1; k < n; k++) {
            const int a = unit[at[k - 1]], b = unit[at[k]];
            const float over = ux[a] + span - ux[b];
            if (a != b && over > 0) { ux[a] -= over / 2; ux[b] += over / 2; }
        }
    }
}

void nav_place(nav_graph_t *g, const nav_metrics_t *m) {
    const float down = (float)(m->tile_h + m->gap_y), aside = (float)(m->tile_w + m->gap_x);
    const float pitch = (float)m->w / NAV_COLS, max_x = (float)(m->w - m->tile_w);
    const int room = (m->w + m->gap_x) / (m->tile_w + m->gap_x);   // tiles a layer holds
    int seq[3][NAV_NODES], len[3], layer[NAV_NODES];
    chains(g, seq, len);

    // Heights: from the signal. A branch hangs just above the MIX it feeds (a short one lower down: its cable to the MIX stays short), row M
    // runs under the MIX, the OUT ends it. A modulator stands at the height of what it modulates (the nearest one with room), the
    // modulators' "+" under the last modulator.
    for (int i = 0; i < NAV_NODES; i++) layer[i] = -1;
    int top = 0;
    if (g->node[NAV_MIX].kind == NODE_MIX) {
        const int deep = len[0] > len[1] ? len[0] : len[1];
        for (int k = 0; k < 2; k++) for (int j = 0; j < len[k]; j++) layer[seq[k][j]] = deep - len[k] + j;
        layer[NAV_MIX] = deep;
        top = deep + 1;
    }
    for (int j = 0; j < len[2]; j++) layer[seq[2][j]] = top + j;
    layer[NAV_OUT] = top + len[2];
    int last_mod = -1;
    for (int i = 0; i < RACK_MAX; i++) {
        if (!is_modulator(&g->node[i])) continue;
        int want = g->node[i].depth;
        for (int e = 0; e < g->n_edges; e++)
            if (g->edge[e].from == i && g->edge[e].kind == EDGE_MOD && layer[g->edge[e].to] >= 0) { want = layer[g->edge[e].to]; break; }
        layer[i] = free_layer(layer, want, room);
        last_mod = i;
    }
    if (g->node[NAV_PLUS_MOD].kind == NODE_PLUS) layer[NAV_PLUS_MOD] = free_layer(layer, last_mod >= 0 ? layer[last_mod] + 1 : 0, room);

    // Across: springs. Along an audio cable a node wants to stand right under the one feeding it; a modulator one tile beside what it
    // modulates, on the outer side of the map (left of branch 1, right of anything else); every node a little toward its grid column.
    link_t lk[NAV_LINKS];
    int n = 0;
    #define LINK(a_, b_, dx_, w_) do { if (n < NAV_LINKS) lk[n++] = (link_t){(int8_t)(a_), (int8_t)(b_), (dx_), (w_)}; } while (0)
    for (int k = 0; k < 3; k++) for (int j = 1; j < len[k]; j++) LINK(seq[k][j - 1], seq[k][j], 0.0f, 1.0f);
    if (g->node[NAV_MIX].kind == NODE_MIX) {
        for (int k = 0; k < 2; k++) if (len[k]) LINK(seq[k][len[k] - 1], NAV_MIX, 0.0f, 1.0f);
        LINK(NAV_MIX, len[2] ? seq[2][0] : NAV_OUT, 0.0f, 1.0f);
    }
    if (len[2]) LINK(seq[2][len[2] - 1], NAV_OUT, 0.0f, 1.0f);
    for (int e = 0; e < g->n_edges; e++) {
        const nav_edge_t *ed = &g->edge[e];
        if (ed->kind != EDGE_MOD || !is_modulator(&g->node[ed->from])) continue;
        const nav_node_t *t = &g->node[ed->to];
        const bool outer_left = g->node[NAV_MIX].kind == NODE_MIX && in_chain(t) && t->row == 0;
        LINK(ed->to, ed->from, outer_left ? -aside : aside, MOD_W);
    }
    if (last_mod >= 0 && g->node[NAV_PLUS_MOD].kind == NODE_PLUS) LINK(last_mod, NAV_PLUS_MOD, 0.0f, 1.0f);
    #undef LINK

    // The order in a layer, left to right: the modulators of branch 1, branch 1, the MIX and row M, branch 2, the other modulators (the
    // modulators' "+" on the side of the last modulator). A chain is one straight column: its nodes share one x (column[], -1: a node alone).
    int key[NAV_NODES], column[NAV_NODES];
    for (int i = 0; i < NAV_NODES; i++) {
        const nav_node_t *nd = &g->node[i];
        column[i] = -1;
        key[i] = 4 * NAV_NODES + i;
        if (in_chain(nd)) { column[i] = nd->row == 0 ? 0 : nd->row == 1 ? 2 : 1; key[i] = (column[i] + 1) * NAV_NODES + i; }
        else if (nd->kind == NODE_MIX || nd->kind == NODE_OUT) { column[i] = 1; key[i] = 2 * NAV_NODES + i; }
    }
    for (int k = 0; k < n; k++)                                 // a modulator pulled to the left of its target goes first
        if (lk[k].dx < 0) key[lk[k].b] = i_min(key[lk[k].b], lk[k].b);
    if (last_mod >= 0 && g->node[NAV_PLUS_MOD].kind == NODE_PLUS) key[NAV_PLUS_MOD] = key[last_mod] < NAV_NODES ? NAV_NODES - 1 : 5 * NAV_NODES;

    int unit[NAV_NODES];
    for (int i = 0; i < NAV_NODES; i++) unit[i] = column[i] >= 0 ? NAV_NODES + column[i] : i;
    float sx[NAV_NODES], ux[UNITS];
    for (int i = 0; i < NAV_NODES; i++) sx[i] = (float)g->node[i].col * pitch + (pitch - (float)m->tile_w) / 2;
    for (int u = 0; u < UNITS; u++) {                           // where each starts: its grid column (a chain: its nodes' mean)
        float sum = 0;
        int k = 0;
        for (int i = 0; i < NAV_NODES; i++) if (shown(g, i) && unit[i] == u) { sum += sx[i]; k++; }
        ux[u] = k ? sum / (float)k : 0;
    }
    int layers = 0;
    for (int i = 0; i < NAV_NODES; i++) if (shown(g, i) && layer[i] + 1 > layers) layers = layer[i] + 1;
    for (int pass = 0; pass < PASSES; pass++) {
        float ax[UNITS], aw[UNITS];                             // each unit goes where its nodes' springs, on average, want it
        for (int u = 0; u < UNITS; u++) { ax[u] = 0; aw[u] = 0; }
        for (int i = 0; i < NAV_NODES; i++) if (shown(g, i)) { ax[unit[i]] += SEED_W * sx[i]; aw[unit[i]] += SEED_W; }
        for (int k = 0; k < n; k++) {
            const link_t *l = &lk[k];
            const int ua = unit[l->a], ub = unit[l->b];
            if (ua == ub) continue;                             // (along a chain: its column)
            ax[ub] += l->w * (ux[ua] + l->dx); aw[ub] += l->w;
            ax[ua] += l->w * (ux[ub] - l->dx); aw[ua] += l->w;
        }
        for (int u = 0; u < UNITS; u++) if (aw[u] > 0) ux[u] = ax[u] / aw[u];
        for (int s = 0; s < 4; s++) separate(g, layer, layers, key, unit, ux, aside);
        for (int u = 0; u < UNITS; u++) ux[u] = ux[u] < 0 ? 0 : ux[u] > max_x ? max_x : ux[u];
    }
    for (int s = 0; s < 40; s++) {                              // the last overlaps out (springs off)
        separate(g, layer, layers, key, unit, ux, aside);
        for (int u = 0; u < UNITS; u++) ux[u] = ux[u] < 0 ? 0 : ux[u] > max_x ? max_x : ux[u];
    }
    float px[NAV_NODES];
    for (int i = 0; i < NAV_NODES; i++) px[i] = ux[unit[i]];

    float x0 = 1e9f, x1 = -1e9f;                               // centred across
    for (int i = 0; i < NAV_NODES; i++) {
        if (!shown(g, i)) continue;
        if (px[i] < x0) x0 = px[i];
        if (px[i] > x1) x1 = px[i];
    }
    const float shift_x = ((float)m->w - (x1 - x0 + (float)m->tile_w)) / 2 - x0;
    g->height = 0;
    for (int i = 0; i < NAV_NODES; i++) {
        if (!shown(g, i)) { g->node[i].x = g->node[i].y = 0; continue; }
        g->node[i].x = (int16_t)(px[i] + shift_x + 0.5f);
        g->node[i].y = (int16_t)((float)layer[i] * down + 0.5f);
        if (g->node[i].y + m->tile_h > g->height) g->height = g->node[i].y + m->tile_h;
    }
}

/* ---------------- the cursor ---------------- */

int nav_move(const nav_graph_t *g, int cur, char dir) {
    if (cur < 0) {                                              // from the header: the top node (the leftmost of a tie)
        int best = -1;
        for (int i = 0; i < NAV_NODES; i++) {
            if (!shown(g, i)) continue;
            if (best < 0 || g->node[i].y < g->node[best].y || (g->node[i].y == g->node[best].y && g->node[i].x < g->node[best].x)) best = i;
        }
        return dir == 'd' && best >= 0 ? best : -1;
    }
    const int ux = dir == 'r' ? 1 : dir == 'l' ? -1 : 0, uy = dir == 'd' ? 1 : dir == 'u' ? -1 : 0;
    int best = -1, best_rank = 2, best_score = 0;
    for (int i = 0; i < NAV_NODES; i++) {
        if (i == cur || !shown(g, i)) continue;
        const int dx = g->node[i].x - g->node[cur].x, dy = g->node[i].y - g->node[cur].y;
        const int along = dx * ux + dy * uy, cross = dx * uy - dy * ux, perp = cross < 0 ? -cross : cross;
        if (along <= 0) continue;                               // not on that side
        const int rank = perp <= along ? 0 : 1;                 // within 45 degrees of the direction first
        const int score = along + 2 * perp;
        if (rank < best_rank || (rank == best_rank && score < best_score)) { best = i; best_rank = rank; best_score = score; }
    }
    if (best >= 0) return best;
    return dir == 'u' ? -1 : cur;                               // nothing above: the header
}
