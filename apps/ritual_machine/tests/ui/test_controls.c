// The app's roles on each board it knows (core/controls.h, core/boards/map_*.h) and the boards' own tables
// (framework/boards/hwv1/hwv1_controls.h, framework/boards/desktop/panel_controls.h). The other tests send role events straight to
// the app; these check the step before: a board's control arrives as the role it had before the tables (2026-10-13). And what the
// simulator's panel window makes of each table (framework/boards/desktop/sim_board.c).
#include "ui_test.h"
#include "core/controls.h"
#include "boards/hwv1/hwv1_controls.h"
#include "boards/hwv2/hwv2_controls.h"
#include "boards/hwv2/hwv2_layout.h"
#include "boards/desktop/panel_controls.h"
#include "boards/desktop/sim_board.h"

static void bind_hwv1(void)  { controls_bind(kHwv1Controls, HWV1_CONTROL_COUNT, controls_map_of("hwv1")); }
static void bind_panel(void) { controls_bind(kPanelControls, PANEL_CONTROL_COUNT, controls_map_of("panel")); }

// The second prototype is the simulator's panel in hardware: the same parts, the same order, the same places (the user, 2026-11-04: "it should look
// like the sim panel"). Only encoder A's LED differs: that LED stays off for now.
TEST(controls_hwv2_is_the_panel) {
    CHECK_EQ(HWV2_CONTROL_COUNT, PANEL_CONTROL_COUNT);
    for (int i = 0; i < HWV2_CONTROL_COUNT; i++) {
        const input_control_t *a = &kHwv2Controls[i], *b = &kPanelControls[i];
        CHECK_STR(a->name, b->name);
        CHECK_EQ(a->kind, b->kind);
        CHECK_EQ(a->row, b->row);
        CHECK_EQ(a->col, b->col);
        CHECK_EQ(a->px, b->px);
        CHECK_EQ(a->py, b->py);
        if (i != HWV2_ENC_A) CHECK_EQ(a->led, b->led);
    }
    CHECK_EQ(kHwv2Controls[HWV2_ENC_A].led, 0);
    controls_bind(kHwv2Controls, HWV2_CONTROL_COUNT, controls_map_of("hwv2"));
    CHECK_EQ(controls_role(HWV2_PLAY), CTL_PLAY);
    CHECK_EQ(controls_role(HWV2_BTN_3), CTL_BTN_3);
    CHECK_EQ(controls_role(HWV2_ENC_A_SW), CTL_ENC_A_SW);
    CHECK_EQ(controls_role(HWV2_COL_KNOB_3), CTL_COL_KNOB_3);
    CHECK_EQ(controls_role(HWV2_KEY(0, 3)), CTL_KEY(0, 3));
    CHECK_EQ(controls_role(HWV2_KEY(4, 0)), CTL_KEY(4, 0));
    CHECK_EQ(controls_board_ctl(CTL_KEY(1, 4)), INPUT_NO_CONTROL);              // the columns the board does not have
    for (int i = 0; i < HWV2_CONTROL_COUNT; i++) CHECK(controls_role(i) != CTL_NONE);
}

// The scanner's frame counts the schematic's rows (r1..r4, the 4 x 4 from the top, then r5, the top row); the table's row 0 is that top row.
TEST(controls_hwv2_scanner_bits_are_the_tables_keys) {
    CHECK_EQ(hwv2_scanner_control(0), HWV2_KEY(1, 0));                          // r1 c1: SW1, the 4 x 4's top left
    CHECK_EQ(hwv2_scanner_control(3), HWV2_KEY(1, 3));
    CHECK_EQ(hwv2_scanner_control(15), HWV2_KEY(4, 3));                         // r4 c4: SW16, its bottom right
    CHECK_EQ(hwv2_scanner_control(16), HWV2_KEY(0, 0));                         // r5 c1: SW17, the top row's left
    CHECK_EQ(hwv2_scanner_control(19), HWV2_KEY(0, 3));
    CHECK_EQ(hwv2_scanner_control(20), HWV2_ENC_A_SW);
    CHECK_EQ(hwv2_scanner_control(21), HWV2_ENC_B_SW);
    CHECK_EQ(hwv2_scanner_control(22), INPUT_NO_CONTROL);
    int seen[HWV2_CONTROL_COUNT] = {0};
    for (int bit = 0; bit < HWV2_SCANNER_INPUTS; bit++) seen[hwv2_scanner_control(bit)]++;
    for (int r = 0; r < HWV2_KEY_ROWS; r++) for (int c = 0; c < HWV2_KEY_COLS; c++) CHECK_EQ(seen[HWV2_KEY(r, c)], 1);
}

TEST(controls_hwv2_vu_lights_from_the_bottom_with_the_level) {
    CHECK_EQ(hwv2_vu_lit(0), 0);
    CHECK_EQ(hwv2_vu_lit(518), 0);                                              // below -36 dB
    CHECK_EQ(hwv2_vu_lit(519), 1);
    CHECK_EQ(hwv2_vu_lit(32768), HWV2_VU_COUNT);                                // full scale
    for (int p = 0; p < 32768; p += 64) CHECK(hwv2_vu_lit(p + 64) >= hwv2_vu_lit(p));
}

// Rev A's pots read reversed, and both ends of the travel are reached.
TEST(controls_hwv2_knob_value_is_reversed_and_reaches_its_ends) {
    CHECK_EQ(hwv2_knob_value(4095), 0);
    CHECK_EQ(hwv2_knob_value(4095 - HWV2_KNOB_MARGIN), 0);
    CHECK_EQ(hwv2_knob_value(0), INPUT_VALUE_MAX);
    CHECK_EQ(hwv2_knob_value(HWV2_KNOB_MARGIN), INPUT_VALUE_MAX);
    CHECK(hwv2_knob_value(2048) > 500 && hwv2_knob_value(2048) < 524);
    for (int raw = 100; raw < 4000; raw += 100) CHECK(hwv2_knob_value(raw) > hwv2_knob_value(raw + 100));
}

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

/* ---------------- the built-in key layouts on each board ---------------- */

extern int ui_stub_key_cols;                                                    // tools/ui_stubs.c: the key columns controls_key_present reports

static int key_cols_bound(void) {
    int cols = 0;
    for (int r = 1; r < KEY_ROWS; r++)
        for (int c = 0; c < KEY_COLS; c++) if (controls_listed((control_id_t)CTL_KEY(r, c)) && c + 1 > cols) cols = c + 1;
    return cols;
}

// The notes of a board's note keys in a layout: every note from the base up to the highest, each once.
static void check_notes_have_no_gap(int layout, int cols, int expect) {
    bool seen[KEYMAP_NOTE_MAX + 1] = {false};
    int notes = 0, top = -1;
    keymap_init();
    keymap_select(layout);
    for (int r = 1; r < KEY_ROWS; r++)
        for (int c = 0; c < cols; c++) {
            const key_fn_t f = keymap_get(r * KEY_COLS + c);
            if (f.act != ACT_NOTE) continue;
            CHECK(!seen[f.arg]);
            seen[f.arg] = true;
            notes++;
            if (f.arg > top) top = f.arg;
        }
    CHECK_EQ(notes, expect);
    CHECK_EQ(top, expect - 1);
}

// Found by the user in the simulator (2026-10-14): "Keys 8x4" counted 8 notes a row on the panel's 4 columns, which played
// 0..3, 8..11, 16..19, 24..27. A row now starts where the row under it ends, on any board.
TEST(keymap_layouts_skip_no_note_on_either_board) {
    bind_panel();
    ui_stub_key_cols = key_cols_bound();
    CHECK_EQ(ui_stub_key_cols, 4);
    check_notes_have_no_gap(0, 4, 16);
    CHECK_EQ(keymap_get(4 * KEY_COLS + 0).arg, 0);                              // bottom left = the base note
    CHECK_EQ(keymap_get(4 * KEY_COLS + 3).arg, 3);
    CHECK_EQ(keymap_get(3 * KEY_COLS + 0).arg, 4);                              // the row above goes on
    CHECK_EQ(keymap_get(1 * KEY_COLS + 3).arg, 15);
    check_notes_have_no_gap(1, 4, 16);
    check_notes_have_no_gap(2, 4, 16);

    bind_hwv1();
    ui_stub_key_cols = key_cols_bound();
    CHECK_EQ(ui_stub_key_cols, 8);
    check_notes_have_no_gap(0, 8, 32);
    CHECK_EQ(keymap_get(4 * KEY_COLS + 7).arg, 7);                              // the prototype: as before, 8 a row
    CHECK_EQ(keymap_get(3 * KEY_COLS + 0).arg, 8);
    CHECK_EQ(keymap_get(1 * KEY_COLS + 7).arg, 31);
    check_notes_have_no_gap(1, 8, 32);
    check_notes_have_no_gap(2, 8, 16);
    keymap_init();
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

// The next prototype's LEDs (ADR-045): every key, every pot and encoder A; nothing else. HWV1: the keys only.
TEST(controls_panel_has_an_led_under_every_key_every_pot_and_encoder_a) {
    int keys = 0, pots = 0, other = 0;
    for (int i = 0; i < PANEL_CONTROL_COUNT; i++) {
        const input_control_t *c = &kPanelControls[i];
        if (!c->led) continue;
        if (c->kind == CK_KEY) keys++;
        else if (c->kind == CK_KNOB) pots++;
        else other++;
    }
    CHECK_EQ(keys, PANEL_KEY_ROWS * PANEL_KEY_COLS);
    CHECK_EQ(pots, 8);
    CHECK_EQ(other, 1);
    CHECK_EQ(kPanelControls[PANEL_ENC_A].led, 1);
    CHECK_EQ(kPanelControls[PANEL_ENC_B].led, 0);
    CHECK_EQ(kPanelControls[PANEL_ENC_A_SW].led, 0);                           // (the push sits at the encoder's place: one ring)
    for (int p = HWV1_POT_0; p <= HWV1_POT_6; p++) CHECK_EQ(kHwv1Controls[p].led, 0);
}
