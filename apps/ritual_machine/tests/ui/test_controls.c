// The app's roles on each board it knows (core/controls.h, core/boards/map_*.h) and the boards' own tables
// (framework/boards/hwv1/hwv1_controls.h, framework/boards/desktop/panel_controls.h). The other tests send role events straight to
// the app; these check the step before: a board's control arrives as the role it had before the tables (2026-10-13). And what the
// simulator's panel window makes of each table (framework/boards/desktop/sim_board.c).
#include "ui_test.h"
#include "core/controls.h"
#include "boards/hwv1/hwv1_controls.h"
#include "boards/desktop/panel_controls.h"
#include "boards/desktop/sim_board.h"

static void bind_hwv1(void)  { controls_bind(kHwv1Controls, HWV1_CONTROL_COUNT, controls_map_of("hwv1")); }
static void bind_panel(void) { controls_bind(kPanelControls, PANEL_CONTROL_COUNT, controls_map_of("panel")); }

TEST(controls_board_tables_are_in_id_order) {
    for (int r = 0; r < HWV1_KEY_ROWS; r++)
        for (int c = 0; c < HWV1_KEY_COLS; c++) {
            const int id = HWV1_KEY(r, c);
            if (r == 0 && c >= HWV1_FN_KEYS) { CHECK_EQ(id, INPUT_NO_CONTROL); continue; }
            CHECK_EQ(kHwv1Controls[id].kind, CK_KEY);
            CHECK_EQ(kHwv1Controls[id].row, r);
            CHECK_EQ(kHwv1Controls[id].col, c);
            CHECK_EQ(kHwv1Controls[id].led, 1);
        }
    CHECK_STR(kHwv1Controls[HWV1_KEY_F4].name, "F4");
    CHECK_STR(kHwv1Controls[HWV1_KEY(4, 7)].name, "KEY 4.7");
    for (int p = HWV1_POT_0; p <= HWV1_POT_6; p++) CHECK_EQ(kHwv1Controls[p].kind, CK_ENDLESS);
    CHECK_STR(kHwv1Controls[HWV1_POT_6].name, "POT 6");
    CHECK_EQ(kHwv1Controls[HWV1_JOY_X].kind, CK_AXIS);
    CHECK_EQ(kHwv1Controls[HWV1_JOY_Y].kind, CK_AXIS);
    CHECK_EQ(kHwv1Controls[HWV1_JOY_SW].kind, CK_BUTTON);
    CHECK_EQ(HWV1_CONTROL_COUNT, 36 + 7 + 3);

    for (int r = 0; r < PANEL_KEY_ROWS; r++)
        for (int c = 0; c < PANEL_KEY_COLS; c++) {
            const input_control_t *k = &kPanelControls[PANEL_KEY(r, c)];
            CHECK_EQ(k->kind, CK_KEY);
            CHECK_EQ(k->row, r);
            CHECK_EQ(k->col, c);
        }
    CHECK_EQ(PANEL_KEY_COLS, 4);                                                // the future prototype's 4 columns, nothing hidden
    CHECK_EQ(PANEL_CONTROL_COUNT, 20 + 6 + 13);
    CHECK_STR(kPanelControls[PANEL_VOLUME].name, "VOLUME");
    CHECK_EQ(kPanelControls[PANEL_ENC_B].kind, CK_ENCODER);
    CHECK_STR(kPanelControls[PANEL_ENC_B_SW].name, "ENC B SW");
    CHECK_STR(kPanelControls[PANEL_COL_KNOB_3].name, "K4");
    CHECK_STR(kPanelControls[PANEL_KNOB_R1].name, "R1");
    CHECK_STR(kPanelControls[PANEL_JOY_SW].name, "JOY PUSH");
    CHECK_STR(kPanelControls[PANEL_BTN_3].name, "B3");
    for (int i = 0; i < PANEL_CONTROL_COUNT; i++) CHECK(kPanelControls[i].name && kPanelControls[i].name[0]);
    for (int i = 0; i < HWV1_CONTROL_COUNT; i++) CHECK(kHwv1Controls[i].name && kHwv1Controls[i].name[0]);
}

// The prototype, as hwv1_layout.h had it: every key at its place, pots 0..2 as Volume / Enc A / Enc B in steps, pots 3..6 as the
// column knobs 2, 4, 3, 1 as positions from the middle, the joystick; no encoder push, Play, B1..B3 or right-hand knob.
TEST(controls_hwv1_plays_the_roles_it_had) {
    bind_hwv1();
    for (int r = 0; r < KEY_ROWS; r++)
        for (int c = 0; c < KEY_COLS; c++) {
            const bool has = r > 0 || c < 4;
            CHECK_EQ(controls_listed((control_id_t)CTL_KEY(r, c)), has);
            if (!has) continue;
            CHECK_EQ(controls_role(HWV1_KEY(r, c)), CTL_KEY(r, c));
            CHECK_EQ(controls_board_ctl((control_id_t)CTL_KEY(r, c)), HWV1_KEY(r, c));
        }
    static const struct { int pot; control_id_t role; int reading, start; } pots[] = {
        {HWV1_POT_0, CTL_VOLUME, IN_DELTA, 0},       {HWV1_POT_1, CTL_ENC_A, IN_DELTA, 0},        {HWV1_POT_2, CTL_ENC_B, IN_DELTA, 0},
        {HWV1_POT_3, CTL_COL_KNOB_1, IN_VALUE, 512}, {HWV1_POT_4, CTL_COL_KNOB_3, IN_VALUE, 512}, {HWV1_POT_5, CTL_COL_KNOB_2, IN_VALUE, 512},
        {HWV1_POT_6, CTL_COL_KNOB_0, IN_VALUE, 512},
    };
    const control_map_t *map = controls_map_of("hwv1");
    CHECK(map != NULL);
    for (int i = 0; i < 7; i++) {
        CHECK_EQ(controls_role(pots[i].pot), pots[i].role);
        CHECK_EQ(controls_board_ctl(pots[i].role), pots[i].pot);
        bool found = false;
        for (int k = 0; map && k < map->count; k++) {
            if (map->rows[k].board_ctl != pots[i].pot) continue;
            found = true;
            CHECK_EQ(map->rows[k].reading, pots[i].reading);
            CHECK_EQ(map->rows[k].start, pots[i].start);
        }
        CHECK(found);
    }
    CHECK_EQ(controls_role(HWV1_JOY_X), CTL_JOY_X);
    CHECK_EQ(controls_role(HWV1_JOY_Y), CTL_JOY_Y);
    CHECK_EQ(controls_role(HWV1_JOY_SW), CTL_JOY_SW);
    static const control_id_t absent[] = {CTL_ENC_A_SW, CTL_ENC_B_SW, CTL_PLAY, CTL_KNOB_R1, CTL_KNOB_R2, CTL_KNOB_R3, CTL_BTN_1, CTL_BTN_2, CTL_BTN_3};
    for (int i = 0; i < (int)(sizeof absent / sizeof absent[0]); i++) {
        CHECK(!controls_listed(absent[i]));
        CHECK_EQ(controls_board_ctl(absent[i]), INPUT_NO_CONTROL);
    }
}

// The desktop's panel: every role one to one, but the key roles of columns 5..8: the panel has 4 key columns.
TEST(controls_panel_plays_every_role) {
    bind_panel();
    for (int role = CTL_NONE + 1; role < CTL_HW_COUNT; role++) {
        const int b = controls_board_ctl((control_id_t)role);
        const bool key = role >= CTL_KEY(0, 0) && role <= CTL_KEY(KEY_ROWS - 1, KEY_COLS - 1);
        if (key && (role - CTL_KEY(0, 0)) % KEY_COLS >= PANEL_KEY_COLS) { CHECK_EQ(b, INPUT_NO_CONTROL); continue; }
        CHECK(b != INPUT_NO_CONTROL);
        CHECK_EQ(controls_role(b), role);
    }
    CHECK_EQ(controls_board_ctl(CTL_VOLUME), PANEL_VOLUME);
    CHECK_EQ(controls_board_ctl(CTL_ENC_A_SW), PANEL_ENC_A_SW);
    CHECK_EQ(controls_board_ctl(CTL_COL_KNOB_2), PANEL_COL_KNOB_2);
    CHECK_EQ(controls_board_ctl(CTL_KNOB_R3), PANEL_KNOB_R3);
    CHECK_EQ(controls_board_ctl(CTL_JOY_Y), PANEL_JOY_Y);
    CHECK_EQ(controls_board_ctl(CTL_BTN_2), PANEL_BTN_2);
    for (int r = 0; r < KEY_ROWS; r++)
        for (int c = 0; c < KEY_COLS; c++) {
            if (c < PANEL_KEY_COLS) CHECK_EQ(controls_role(PANEL_KEY(r, c)), CTL_KEY(r, c));
            CHECK_EQ(controls_listed((control_id_t)CTL_KEY(r, c)), c < PANEL_KEY_COLS);
        }
    const control_map_t *map = controls_map_of("panel");
    CHECK(map != NULL);
    for (int k = 0; map && k < map->count; k++) CHECK_EQ(map->rows[k].reading, 0);     // no endless pot on the panel
}

TEST(controls_event_carries_the_role) {
    bind_hwv1();
    input_event_t e = controls_event((input_event_t){HWV1_KEY(2, 5), IN_PRESS, 0, false});
    CHECK_EQ(e.ctl, CTL_KEY(2, 5));
    CHECK_EQ(e.kind, IN_PRESS);
    e = controls_event((input_event_t){HWV1_POT_4, IN_VALUE, 700, false});
    CHECK_EQ(e.ctl, CTL_COL_KNOB_3);
    CHECK_EQ(e.kind, IN_VALUE);
    CHECK_EQ(e.value, 700);
    e = controls_event((input_event_t){HWV1_POT_2, IN_DELTA, -3, false});
    CHECK_EQ(e.ctl, CTL_ENC_B);
    CHECK_EQ(e.value, -3);
    e = controls_event((input_event_t){INPUT_NO_CONTROL, IN_NONE, 0, false});          // nothing happened
    CHECK_EQ(e.ctl, CTL_NONE);
    CHECK_EQ(e.kind, IN_NONE);
    CHECK(!e.quit);
    e = controls_event((input_event_t){INPUT_NO_CONTROL, IN_NONE, 0, true});           // the window was closed
    CHECK(e.quit);
    CHECK_EQ(e.kind, IN_NONE);
    e = controls_event((input_event_t){200, IN_PRESS, 0, false});                      // not a control of the board
    CHECK_EQ(e.kind, IN_NONE);
}

// A board the app has no map for: its matrix keys still play the key roles of their places, the rest does nothing.
TEST(controls_unknown_board_keeps_its_keys) {
    CHECK(controls_map_of("another") == NULL);
    controls_bind(kPanelControls, PANEL_CONTROL_COUNT, NULL);
    CHECK_EQ(controls_role(PANEL_KEY(3, 2)), CTL_KEY(3, 2));
    CHECK_EQ(controls_role(PANEL_ENC_A), CTL_NONE);
    CHECK_EQ(controls_board_ctl(CTL_ENC_A), INPUT_NO_CONTROL);
    const input_event_t e = controls_event((input_event_t){PANEL_ENC_A, IN_DELTA, 1, false});
    CHECK_EQ(e.kind, IN_NONE);
    controls_bind(NULL, 0, NULL);
    CHECK_EQ(controls_role(PANEL_KEY(3, 2)), CTL_NONE);
}

// A map's row may give a key another role than its place's: the place's role is then free.
TEST(controls_a_row_overrides_a_key_place) {
    static const control_map_row_t rows[] = {{PANEL_KEY(0, 0), CTL_PLAY, 0, 0}};
    static const control_map_t map = {"test", rows, 1};
    controls_bind(kPanelControls, PANEL_CONTROL_COUNT, &map);
    CHECK_EQ(controls_role(PANEL_KEY(0, 0)), CTL_PLAY);
    CHECK_EQ(controls_board_ctl(CTL_PLAY), PANEL_KEY(0, 0));
    CHECK_EQ(controls_board_ctl((control_id_t)CTL_KEY(0, 0)), INPUT_NO_CONTROL);
    CHECK(!controls_listed((control_id_t)CTL_KEY(0, 0)));
    CHECK(controls_listed(CTL_PLAY));
    CHECK_EQ(controls_role(PANEL_PLAY), CTL_NONE);                                      // one control per role: the panel's own Play has none here
}

/* ---------------- the simulator's panel, drawn from a board's table ---------------- */

static int parts_of(const sim_panel_t *panel, int kind) {
    int n = 0;
    for (int i = 0; i < panel->count; i++) n += panel->parts[i].kind == kind;
    return n;
}

static const sim_part_t *part_of(const sim_panel_t *panel, int ctl) {
    for (int i = 0; i < panel->count; i++) if (panel->parts[i].ctl == ctl) return &panel->parts[i];
    return NULL;
}

// No part on top of another (with the texts under it), every part inside the drawing and above the status lines.
static void check_parts_fit(const sim_panel_t *panel) {
    for (int i = 0; i < panel->count; i++) {
        int l, t, r, b;
        sim_part_box(&panel->parts[i], &l, &t, &r, &b);
        CHECK(l >= SIM_PANEL_MARGIN && r <= panel->width - SIM_PANEL_MARGIN);
        CHECK(t >= SIM_PANEL_TOP && b <= panel->height - SIM_PANEL_STATUS - SIM_PANEL_MARGIN);
        for (int k = i + 1; k < panel->count; k++) {
            int l2, t2, r2, b2;
            sim_part_box(&panel->parts[k], &l2, &t2, &r2, &b2);
            const bool apart = r <= l2 || r2 <= l || b <= t2 || b2 <= t;
            if (!apart) printf("    parts %d and %d (controls %d, %d) overlap\n", i, k, panel->parts[i].ctl, panel->parts[k].ctl);
            CHECK(apart);
        }
    }
}

// The panel: 20 keys, Play and B1..B3, two encoders with their push, 8 knobs, the joystick with its two axes and its push.
TEST(sim_panel_draws_the_panel_board) {
    static sim_panel_t panel;
    sim_panel_build(&panel, kPanelControls, PANEL_CONTROL_COUNT);
    CHECK_EQ(parts_of(&panel, PART_KEY), 20);
    CHECK_EQ(parts_of(&panel, PART_BUTTON), 4);
    CHECK_EQ(parts_of(&panel, PART_ENCODER), 2);
    CHECK_EQ(parts_of(&panel, PART_KNOB), 8);
    CHECK_EQ(parts_of(&panel, PART_JOYSTICK), 1);
    CHECK_EQ(parts_of(&panel, PART_ENDLESS), 0);
    CHECK_EQ(panel.count, 35);
    const sim_part_t *enc = part_of(&panel, PANEL_ENC_B), *joy = part_of(&panel, PANEL_JOY_X);
    CHECK(enc && joy);
    if (!enc || !joy) return;
    CHECK_EQ(enc->kind, PART_ENCODER);
    CHECK_EQ(enc->sw, PANEL_ENC_B_SW);
    CHECK_EQ(joy->kind, PART_JOYSTICK);
    CHECK_EQ(joy->ctl2, PANEL_JOY_Y);
    CHECK_EQ(joy->sw, PANEL_JOY_SW);
    CHECK(part_of(&panel, PANEL_ENC_B_SW) == NULL);                            // a push is not a part of its own
    CHECK(part_of(&panel, PANEL_JOY_SW) == NULL);
    CHECK_EQ(part_of(&panel, PANEL_PLAY)->kind, PART_BUTTON);
    CHECK_EQ(part_of(&panel, PANEL_KEY(1, 1))->x - part_of(&panel, PANEL_KEY(1, 0))->x, 2 * SIM_HALF_PITCH);
    CHECK_EQ(part_of(&panel, PANEL_KEY(2, 0))->y - part_of(&panel, PANEL_KEY(1, 0))->y, 2 * SIM_HALF_PITCH);
    check_parts_fit(&panel);
}

// The first prototype: 36 keys, 7 pots without end stops, the joystick with its push.
TEST(sim_panel_draws_hwv1) {
    static sim_panel_t panel;
    sim_panel_build(&panel, kHwv1Controls, HWV1_CONTROL_COUNT);
    CHECK_EQ(parts_of(&panel, PART_KEY), 36);
    CHECK_EQ(parts_of(&panel, PART_ENDLESS), 7);
    CHECK_EQ(parts_of(&panel, PART_JOYSTICK), 1);
    CHECK_EQ(panel.count, 44);
    const sim_part_t *joy = part_of(&panel, HWV1_JOY_X), *pot = part_of(&panel, HWV1_POT_4);
    CHECK(joy && pot);
    if (!joy || !pot) return;
    CHECK_EQ(joy->ctl2, HWV1_JOY_Y);
    CHECK_EQ(joy->sw, HWV1_JOY_SW);
    CHECK_EQ(pot->kind, PART_ENDLESS);
    CHECK_EQ(pot->sw, INPUT_NO_CONTROL);
    CHECK(part_of(&panel, HWV1_KEY(4, 7))->x > part_of(&panel, HWV1_KEY(4, 0))->x);
    CHECK(part_of(&panel, HWV1_KEY_F1)->y < part_of(&panel, HWV1_KEY(1, 0))->y);
    check_parts_fit(&panel);
}

TEST(sim_panel_of_an_empty_table_is_empty) {
    static sim_panel_t panel;
    sim_panel_build(&panel, NULL, 0);
    CHECK_EQ(panel.count, 0);
    CHECK(panel.width > 0 && panel.height > 0);
}
