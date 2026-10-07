// Runs the UI tests: tests\ui\*.c on the core (src/core) and the platform stubs (tools/ui_stubs.c). Build and run: tools/build_ui_tests.ps1.
//   ui_tests.exe            every test
//   ui_tests.exe fm         only the tests whose name contains "fm"
#include "ui_test.h"
#include <stdlib.h>

#define MAX_TESTS 256
static struct { const char *name; ui_test_fn fn; } tests[MAX_TESTS];
static int n_tests;
int ui_test_failures;

void ui_test_register(const char *name, ui_test_fn fn) {
    if (n_tests < MAX_TESTS) { tests[n_tests].name = name; tests[n_tests].fn = fn; n_tests++; }
}

app_t ui_app;
static u8g2_t disp;

void ui_board(bool prototype) { putenv(prototype ? "UI_DUMP_BOARD=hwv1" : "UI_DUMP_BOARD="); }

void ui_fresh(void) {
    memset(&ui_app, 0, sizeof ui_app);
    app_init(&ui_app, &disp);
    ev_idle(1);
}

static void send(input_event_t e) { app_step(&ui_app, e); }
void ev_press(control_id_t c)   { send((input_event_t){c, IN_PRESS, 0, false}); }
void ev_release(control_id_t c) { send((input_event_t){c, IN_RELEASE, 0, false}); }
void ev_tap(control_id_t c)     { ev_press(c); ev_release(c); }
void ev_knob(control_id_t c, int value)   { send((input_event_t){c, IN_VALUE, value, false}); }
void ev_turn(control_id_t c, int detents) { send((input_event_t){c, IN_DELTA, detents, false}); }
void ev_idle(int steps) { for (int i = 0; i < steps; i++) send((input_event_t){CTL_NONE, IN_NONE, 0, false}); }

void ui_set_type(int synth_type) {
    ui_app.rack.cfg.type = (uint8_t)synth_type;
    synth_ui_rebuild_pages(&ui_app.ui, &ui_app.rack);
    ui_app.ui.page = 0; ui_app.ui.row = 0;
    ev_idle(1);
}

int ui_find_page(int slot, int def) {
    for (int p = 0; p < ui_app.ui.page_count; p++) if (ui_app.ui.pg_slot[p] == slot && ui_app.ui.pg_def[p] == def) return p;
    return -1;
}

void ui_page_title(char *out, int n) {
    page_t pg;
    get_page(&ui_app.ui, &ui_app.rack, ui_app.ui.page, &pg);
    snprintf(out, (size_t)n, "%s", pg.title);
}

int main(int argc, char **argv) {
    u8g2_Setup_sh1107_128x128_f(&disp, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);
    u8g2_SetFont(&disp, u8g2_font_5x7_tr);
    int run = 0, failed_tests = 0;
    for (int i = 0; i < n_tests; i++) {
        if (argc > 1 && !strstr(tests[i].name, argv[1])) continue;
        ui_board(false);
        ui_stub_ramcard(false);
        const int before = ui_test_failures;
        printf("  %s\n", tests[i].name);
        tests[i].fn();
        run++;
        if (ui_test_failures != before) failed_tests++;
    }
    printf("%d UI tests, %d failed (%d failed checks)\n", run, failed_tests, ui_test_failures);
    return ui_test_failures != 0;
}
