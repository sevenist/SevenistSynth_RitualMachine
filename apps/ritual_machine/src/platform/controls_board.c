// The app's controls on the board the program runs on (core/controls.h): the board's table and its LEDs come from the library's HAL.
// Shared by the firmware and the simulator; the host tests replace it (tools/ui_stubs.c).
#include "core/controls.h"

void controls_init(void) {
    int count = 0;
    const input_control_t *const table = input_controls(&count);
    const control_map_t *const map = controls_map_of(input_board_name());
    controls_bind(table, count, map);
    for (int i = 0; map && i < map->count; i++)                        // how each endless pot reports: as its role wants it
        if (map->rows[i].reading) input_control_mode(map->rows[i].board_ctl, (input_kind_t)map->rows[i].reading, map->rows[i].start);
}

bool controls_key_present(int row, int col) {
    return row >= 0 && row < KEY_ROWS && col >= 0 && col < KEY_COLS && controls_listed((control_id_t)CTL_KEY(row, col));
}

bool controls_present(control_id_t c) {
    if (c >= CTL_JOY_LEFT && c <= CTL_JOY_DOWN) return controls_listed(CTL_JOY_X) && controls_listed(CTL_JOY_Y);   // made from the two axes
    return controls_listed(c);
}

void controls_led(control_id_t c, led_color_t color) {
    const int b = controls_board_ctl(c);
    if (b != INPUT_NO_CONTROL) leds_set(b, color);
}
