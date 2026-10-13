// A board's controls as the app's roles (core/controls.h). No hardware call here: the board's table and the map come in as data,
// so the host tests bind every board the app knows.
#include "core/controls.h"
#include <string.h>
#include "core/boards/map_hwv1.h"
#include "core/boards/map_panel.h"

#define ROWS(a) a, (int)(sizeof a / sizeof a[0])
static const control_map_t maps[] = {
    {"hwv1",  ROWS(kMapHwv1Rows)},
    {"panel", ROWS(kMapPanelRows)},
};

const control_map_t *controls_map_of(const char *board) {
    for (int i = 0; board && i < (int)(sizeof maps / sizeof maps[0]); i++) if (!strcmp(maps[i].board, board)) return &maps[i];
    return NULL;
}

static uint8_t role_of[CONTROLS_BOARD_MAX];         // board control -> role (CTL_NONE: none)
static uint8_t board_of[CTL_COUNT];                 // role -> board control + 1 (0: none)

static void assign(int b, int role) {
    if (b < 0 || b >= CONTROLS_BOARD_MAX || role <= CTL_NONE || role >= CTL_HW_COUNT) return;
    if (role_of[b]) board_of[role_of[b]] = 0;                           // the control leaves the role it had
    if (board_of[role]) role_of[board_of[role] - 1] = CTL_NONE;         // ... and the role its control
    role_of[b] = (uint8_t)role;
    board_of[role] = (uint8_t)(b + 1);
}

void controls_bind(const input_control_t *table, int count, const control_map_t *map) {
    memset(role_of, 0, sizeof role_of);
    memset(board_of, 0, sizeof board_of);
    if (!table) return;
    if (count > CONTROLS_BOARD_MAX) count = CONTROLS_BOARD_MAX;
    for (int b = 0; b < count; b++)
        if (table[b].kind == CK_KEY && table[b].row < KEY_ROWS && table[b].col < KEY_COLS) assign(b, CTL_KEY(table[b].row, table[b].col));
    for (int i = 0; map && i < map->count; i++)
        if (map->rows[i].board_ctl < count) assign(map->rows[i].board_ctl, map->rows[i].role);
}

control_id_t controls_role(int board_ctl) {
    return board_ctl >= 0 && board_ctl < CONTROLS_BOARD_MAX ? (control_id_t)role_of[board_ctl] : CTL_NONE;
}

int controls_board_ctl(control_id_t role) {
    return role > CTL_NONE && role < CTL_COUNT && board_of[role] ? board_of[role] - 1 : INPUT_NO_CONTROL;
}

bool controls_listed(control_id_t role) { return controls_board_ctl(role) != INPUT_NO_CONTROL; }

input_event_t controls_event(input_event_t e) {
    if (e.kind == IN_NONE) { e.ctl = CTL_NONE; return e; }             // nothing happened (or quit, which travels with it)
    e.ctl = controls_role(e.ctl);
    if (e.ctl == CTL_NONE) return (input_event_t){CTL_NONE, IN_NONE, 0, false};
    return e;
}
