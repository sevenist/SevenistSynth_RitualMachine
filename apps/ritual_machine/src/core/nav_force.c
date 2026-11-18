#include "core/nav_force.h"
#include <math.h>
#include <string.h>

#define PULL_IN   0.004f                // toward the centre, per px away
#define DAMP      0.8f                  // the speed kept from one step to the next
#define MAX_SPEED 4.0f                  // px per step, at first: it cools down to nothing over MAX_STEPS, so the graph always comes to rest
#define CALM      0.05f                 // a node slower than this is still ...
#define CALM_RUN  8                     // ... and the graph settled after this many steps of it
#define MAX_STEPS 300                   // (about 5 s at the screen's pace)
#define GAP       4.0f                  // px between two nodes at least (room for a cable)

void nav_force_init(nav_force_t *f, float cx, float cy, float hw, float hh, float rest, float push) {
    memset(f, 0, sizeof *f);
    f->cx = cx; f->cy = cy; f->hw = hw; f->hh = hh; f->rest = rest; f->push = push;
}

void nav_force_put(nav_force_t *f, int node, float x, float y) {
    if (node < 0 || node >= NAV_NODES) return;
    f->x[node] = x; f->y[node] = y; f->vx[node] = f->vy[node] = 0;
    f->on[node] = true;
    nav_force_go(f);
}

void nav_force_spring(nav_force_t *f, int a, int b, float k) {
    if (f->n_springs >= NAV_SPRINGS || a == b || a < 0 || b < 0 || a >= NAV_NODES || b >= NAV_NODES) return;
    f->spring[f->n_springs++] = (nav_spring_t){(int8_t)a, (int8_t)b, k};
}

void nav_force_go(nav_force_t *f) { f->moving = true; f->calm = 0; f->steps = 0; }

bool nav_force_step(nav_force_t *f) {
    if (!f->moving) return false;
    float fx[NAV_NODES] = {0}, fy[NAV_NODES] = {0};
    for (int i = 0; i < NAV_NODES; i++) {                       // every node pushes every other away (harder when close)
        if (!f->on[i]) continue;
        for (int j = i + 1; j < NAV_NODES; j++) {
            if (!f->on[j]) continue;
            float dx = f->x[i] - f->x[j], dy = f->y[i] - f->y[j];
            if (dx * dx + dy * dy < 0.01f) { dx = (float)(i - j) * 0.1f; dy = 0.05f; }   // (two on one spot: apart, the same way every time)
            const float d2 = dx * dx + dy * dy, d = sqrtf(d2);
            float p = f->push / d2;
            if (p > 2.0f) p = 2.0f;
            fx[i] += p * dx / d; fy[i] += p * dy / d;
            fx[j] -= p * dx / d; fy[j] -= p * dy / d;
        }
    }
    for (int s = 0; s < f->n_springs; s++) {                    // a cable pulls its ends to its length at rest
        const int a = f->spring[s].a, b = f->spring[s].b;
        if (!f->on[a] || !f->on[b]) continue;
        const float dx = f->x[b] - f->x[a], dy = f->y[b] - f->y[a], d = sqrtf(dx * dx + dy * dy);
        if (d < 0.01f) continue;
        const float p = f->spring[s].k * (d - f->rest);
        fx[a] += p * dx / d; fy[a] += p * dy / d;
        fx[b] -= p * dx / d; fy[b] -= p * dy / d;
    }
    float fastest = 0;
    for (int i = 0; i < NAV_NODES; i++) {
        if (!f->on[i]) continue;
        fx[i] += PULL_IN * (f->cx - f->x[i]);
        fy[i] += PULL_IN * (f->cy - f->y[i]);
        float vx = (f->vx[i] + fx[i]) * DAMP, vy = (f->vy[i] + fy[i]) * DAMP;
        const float v = sqrtf(vx * vx + vy * vy), cap = MAX_SPEED * (1.0f - (float)f->steps / MAX_STEPS);
        if (v > cap) { vx *= cap / v; vy *= cap / v; }
        f->vx[i] = vx; f->vy[i] = vy;
        f->x[i] += vx; f->y[i] += vy;
        const float s = sqrtf(vx * vx + vy * vy);
        if (s > fastest) fastest = s;
    }
    for (int pass = 0; pass < 2; pass++)                        // no two nodes overlap: pushed apart along the shorter way out
        for (int i = 0; i < NAV_NODES; i++) {
            if (!f->on[i]) continue;
            for (int j = i + 1; j < NAV_NODES; j++) {
                if (!f->on[j]) continue;
                const float dx = f->x[j] - f->x[i], dy = f->y[j] - f->y[i];
                const float ox = 2 * f->hw + GAP - fabsf(dx), oy = 2 * f->hh + GAP - fabsf(dy);
                if (ox <= 0 || oy <= 0) continue;
                if (ox * f->hh < oy * f->hw) { const float m = (dx < 0 || (dx == 0 && i > j) ? -ox : ox) / 2; f->x[i] -= m; f->x[j] += m; }
                else { const float m = (dy < 0 || (dy == 0 && i > j) ? -oy : oy) / 2; f->y[i] -= m; f->y[j] += m; }
            }
        }
    f->calm = fastest < CALM ? f->calm + 1 : 0;
    if (f->calm >= CALM_RUN || ++f->steps >= MAX_STEPS) f->moving = false;
    return f->moving;
}
