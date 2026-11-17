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
        if (row_of[i] < 0) continue;
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
}

static bool shown(const nav_graph_t *g, int i) { return g->node[i].kind != NODE_NONE; }

int nav_move(const nav_graph_t *g, int cur, char dir) {
    if (cur < 0) {                                              // from the header: the first node, the top of the first column that has one
        int best = -1;
        for (int i = 0; i < NAV_NODES; i++) {
            if (!shown(g, i)) continue;
            if (best < 0 || g->node[i].depth < g->node[best].depth || (g->node[i].depth == g->node[best].depth && g->node[i].col < g->node[best].col)) best = i;
        }
        return dir == 'd' && best >= 0 ? best : -1;
    }
    const nav_node_t *c = &g->node[cur];
    int best = -1;
    switch (dir) {
        case 'd':
            for (int i = 0; i < NAV_NODES; i++)                 // the next one down the column
                if (shown(g, i) && g->node[i].col == c->col && g->node[i].depth > c->depth && (best < 0 || g->node[i].depth < g->node[best].depth)) best = i;
            if (best >= 0) return best;
            for (int e = 0; e < g->n_edges; e++)                // the end of a branch continues into the MIX
                if (g->edge[e].from == cur && g->edge[e].kind == EDGE_AUDIO && g->node[g->edge[e].to].col != c->col) return g->edge[e].to;
            if (c->col == 1 && g->node[NAV_MIX].kind == NODE_MIX && c->depth < g->node[NAV_MIX].depth) return NAV_MIX;   // (its "+")
            return cur;
        case 'u':
            for (int i = 0; i < NAV_NODES; i++)
                if (shown(g, i) && g->node[i].col == c->col && g->node[i].depth < c->depth && (best < 0 || g->node[i].depth > g->node[best].depth)) best = i;
            if (best >= 0) return best;
            if (c->kind == NODE_MIX) {                          // up from the MIX: the end of the longer branch (the first one when they are even)
                for (int i = 0; i < RACK_MAX; i++)
                    if (shown(g, i) && g->node[i].col <= 1 && g->node[i].depth < c->depth && (best < 0 || g->node[i].depth > g->node[best].depth)) best = i;
                if (best >= 0) return best;
            }
            return -1;                                          // the top of the column: the header
        default: {                                              // 'l' / 'r': the nearest column on that side, the node at about the same depth
            const int step = dir == 'r' ? 1 : -1;
            for (int col = c->col + step; col >= 0 && col < NAV_COLS; col += step) {
                best = -1;
                int gap = 1 << 20;
                for (int i = 0; i < NAV_NODES; i++) {
                    if (!shown(g, i) || g->node[i].col != col) continue;
                    const int diff = g->node[i].depth - c->depth;
                    const int dist = (diff < 0 ? -diff : diff) * 2 + (diff > 0);     // a tie goes to the one above
                    if (dist < gap) { gap = dist; best = i; }
                }
                if (best >= 0) return best;
            }
            return cur;
        }
    }
}
