#pragma once
// Minimal test harness for the application core (C): TEST(name) { CHECK(cond); CHECK_EQ(a, b); }  run by test_main.c.
// The core runs on the stubs of tools/ui_stubs.c (no audio, no SDL; a RAM card on request). Each test starts from a fresh app (ui_fresh)
// and drives it with control events, as the board or the SDL window would.
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "core/app.h"
#include "core/modifiers.h"
#include "core/keymap.h"
#include "core/ui_settings.h"
#include "core/ui_internal.h"

typedef void (*ui_test_fn)(void);
void ui_test_register(const char *name, ui_test_fn fn);
extern int ui_test_failures;

#define TEST(name) \
    static void name(void); \
    __attribute__((constructor)) static void reg_##name(void) { ui_test_register(#name, name); } \
    static void name(void)

#define CHECK(cond) do { if (!(cond)) { printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ui_test_failures++; } } while (0)
#define CHECK_EQ(a, b) do { long long va_ = (long long)(a), vb_ = (long long)(b); if (va_ != vb_) { \
    printf("    FAIL %s:%d: %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); ui_test_failures++; } } while (0)
#define CHECK_STR(a, b) do { const char *sa_ = (a), *sb_ = (b); if (strcmp(sa_, sb_)) { \
    printf("    FAIL %s:%d: %s == \"%s\"  (got \"%s\")\n", __FILE__, __LINE__, #a, sb_, sa_); ui_test_failures++; } } while (0)

// The app under test (test_main.c). ui_fresh(): a new app as at power-on (key layout, layers and UI state reset; the RAM card is kept).
extern app_t ui_app;
void ui_fresh(void);
void ui_stub_ramcard(bool on);              // tools/ui_stubs.c
void ui_board(bool prototype);              // the controls of the prototype (no R knobs, B1..B3, Play, encoder pushes) or every control

// Control events.
void ev_press(control_id_t c);
void ev_release(control_id_t c);
void ev_tap(control_id_t c);                // press + release
void ev_knob(control_id_t c, int value);    // an absolute knob at 0..INPUT_VALUE_MAX
void ev_turn(control_id_t c, int detents);  // an encoder
void ev_idle(int steps);                    // app steps without input (popups expire, autosave, the catch refresh)

// Helpers.
void ui_set_type(int synth_type);           // switches the synth type as the GENERAL tab would, then rebuilds the pages
int  ui_find_page(int slot, int def);       // index of that page, -1 when it is not shown
void ui_page_title(char *out, int n);       // title of the page on screen
