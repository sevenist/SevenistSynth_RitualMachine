// Macros (control/macro.h): mappings, destinations, links between macros (a chain, never a loop), applying one.
#include <vector>
#include "control/macro.h"
#include "test.h"

namespace {
struct Seen { ctl_target_t t; float v; };
bool record(void *ctx, const ctl_target_t *t, float v) {
    static_cast<std::vector<Seen> *>(ctx)->push_back({*t, v});
    return t->kind != 9;                                               // kind 9: a target the app does not have
}
ctl_target_t value(int i) { return ctl_target(1, 0, i); }
}  // namespace

TEST(macro_mapping_range_curve_inverse) {
    ctl_map_t m = ctl_map_plain(value(3));
    CHECK(ctl_map_is_plain(&m));
    CHECK_NEAR(ctl_map_apply(nullptr, &m, 0.25f), 0.25, 1e-6);
    m.min = 0.8f; m.max = 0.2f;                                        // inverted
    CHECK_NEAR(ctl_map_apply(nullptr, &m, 0.0f), 0.8, 1e-6);
    CHECK_NEAR(ctl_map_apply(nullptr, &m, 1.0f), 0.2, 1e-6);
    CHECK_EQ(ctl_map_inverse(nullptr, &m, 0.5f, 101), 50);
    CHECK_EQ(ctl_map_inverse(nullptr, &m, 0.9f, 101), 0);              // out of reach: the nearest end
    m.curve = CRV_EXP;
    CHECK(!ctl_map_is_plain(&m));
    CHECK_EQ(ctl_pct(0.494f), 49);
    CHECK_EQ(ctl_pct(0.495f), 50);
    const ctl_map_t d{};                                               // (C++: the defaults are the plain mapping)
    CHECK(d.max == 1.0f && d.curve == CRV_LIN);
}

TEST(macro_destinations_add_once_fill_and_remove) {
    mac_t set[MAC_MAX] = {};
    CHECK_EQ(mac_add(set, 0, ctl_map_plain(value(0))), 0);
    CHECK_EQ(mac_add(set, 0, ctl_map_plain(value(0))), -1);            // there already
    CHECK_EQ(mac_add(set, 0, ctl_map_plain(ctl_target(MAC_KIND_NONE, 0, 0))), -1);
    for (int i = 1; i < MAC_DESTS; i++) CHECK_EQ(mac_add(set, 0, ctl_map_plain(value(i))), i);
    CHECK_EQ(mac_add(set, 0, ctl_map_plain(value(99))), -1);           // full
    mac_remove(&set[0], 0);
    CHECK_EQ(set[0].n, MAC_DESTS - 1);
    CHECK_EQ(set[0].d[0].t.prm, 1);                                    // the others moved up
    const ctl_target_t t = value(5);
    CHECK_EQ(mac_find(&set[0], &t), 4);
    CHECK_EQ(mac_add(set, MAC_MAX, ctl_map_plain(value(0))), -1);
}

TEST(macro_links_form_chains_never_loops) {
    mac_t set[MAC_MAX] = {};
    CHECK(mac_add(set, 0, ctl_map_plain(mac_target(1))) >= 0);         // M1 -> M2 -> M3
    CHECK(mac_add(set, 1, ctl_map_plain(mac_target(2))) >= 0);
    CHECK(mac_reaches(set, 0, 2) && !mac_reaches(set, 2, 0));
    CHECK_EQ(mac_add(set, 2, ctl_map_plain(mac_target(0))), -1);       // M3 -> M1: a loop
    CHECK_EQ(mac_add(set, 1, ctl_map_plain(mac_target(1))), -1);       // itself
    CHECK_EQ(mac_add(set, 0, ctl_map_plain(mac_target(MAC_MAX))), -1); // not a macro of the set
    CHECK(mac_can_link(set, 0, 2));                                    // a second way down is fine (not a loop)
    CHECK(!mac_can_link(set, 0, 1));                                   // (there already)
    CHECK(mac_add(set, 3, ctl_map_plain(mac_target(2))) >= 0);         // two macros on one
    CHECK(mac_of(&set[0].d[0].t) == 1 && mac_of(&set[3].d[0].t) == 2);
    const ctl_target_t v = value(1);
    CHECK_EQ(mac_of(&v), -1);
}

TEST(macro_apply_moves_the_chain_in_order) {
    mac_t set[MAC_MAX] = {};
    ctl_map_t half = ctl_map_plain(mac_target(1));
    half.max = 0.5f;
    mac_add(set, 0, ctl_map_plain(value(10)));
    mac_add(set, 0, half);                                             // M1 sweeps M2 over 0..0.5
    ctl_map_t inv = ctl_map_plain(value(20));
    inv.min = 1.0f; inv.max = 0.0f;
    mac_add(set, 1, inv);
    mac_add(set, 1, ctl_map_plain(mac_target(2)));
    mac_add(set, 2, ctl_map_plain(ctl_target(9, 0, 0)));              // a target the app does not have
    std::vector<Seen> seen;
    CHECK(mac_apply(set, nullptr, 0, 0.8f, record, &seen));
    CHECK_NEAR(set[0].pos, 0.8, 1e-6);
    CHECK_NEAR(set[1].pos, 0.4, 1e-6);
    CHECK_NEAR(set[2].pos, 0.4, 1e-6);
    CHECK_EQ(static_cast<int>(seen.size()), 3);
    CHECK(seen[0].t.prm == 10 && seen[1].t.prm == 20 && seen[2].t.kind == 9);
    CHECK_NEAR(seen[0].v, 0.8, 1e-6);
    CHECK_NEAR(seen[1].v, 0.6, 1e-6);                                  // M2 at 0.4, inverted
    seen.clear();
    CHECK(mac_apply_from(set, nullptr, 0, 1, 0.2f, record, &seen));    // the first one left to the app
    CHECK_NEAR(set[0].pos, 0.8, 1e-6);                                 // (its position too)
    CHECK(seen.size() == 2 && seen[0].t.prm == 20);
    CHECK_NEAR(set[1].pos, 0.1, 1e-6);
    seen.clear();
    CHECK(!mac_apply(set, nullptr, 2, 0.5f, record, &seen));           // only a target the app does not have: nothing set
    CHECK(!mac_apply(set, nullptr, 5, 0.5f, record, &seen));           // no destination
    // a loop that mac_add never makes (a hand-edited file read without it) still ends
    set[2].d[0] = ctl_map_plain(mac_target(0));
    seen.clear();
    mac_apply(set, nullptr, 0, 1.0f, record, &seen);
    CHECK(seen.size() < 40);
}
